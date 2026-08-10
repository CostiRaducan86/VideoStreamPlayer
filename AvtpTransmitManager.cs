using System;
using System.Threading;
using System.Threading.Tasks;

namespace VilsSharpX;

/// <summary>
/// Manages AVTP frame transmission (TX) for PlayerFromFiles mode.
/// AVTP packets always use 320×80 (25,600 bytes) format as per protocol spec.
/// For Nichia (256×64), frames are zero-padded to match the protocol requirement.
/// </summary>
public sealed class AvtpTransmitManager(int width, int height, Action<string> log) : IDisposable
{
    // AVTP protocol always uses 320×80 frame format (like CANoe implementation)
    private const int AVTP_FRAME_SIZE = 320 * 80; // 25,600 bytes

    private readonly Action<string> _log = log ?? (_ => { });
    private readonly int _width = width;
    private readonly int _height = height;

    private AvtpRvfTransmitter? _tx;
    private CancellationTokenSource? _blackCts;
    private Task? _blackTask;
    private volatile bool _avtpFaultEnabled;

    private int _txErrOnce;
    private int _txNoDevOnce;
    private long _txLogCounter;
    private const int TxLogInterval = 500; // log every 500 frames (~5 seconds at 100fps)

    // BLACK frame is always AVTP size (320×80) - already zero-filled by CLR
    private readonly byte[] _blackFrame = new byte[AVTP_FRAME_SIZE];
    // Reusable buffer for padding smaller frames
    private readonly byte[] _paddedFrame = new byte[AVTP_FRAME_SIZE];

    /// <summary>
    /// Whether transmitter is initialized and ready.
    /// </summary>
    public bool IsReady => _tx != null;

    /// <summary>
    /// Enables or disables the AVTP communication fault. Enabling the fault
    /// blocks subsequent sends and stops the black-frame loop without closing
    /// the shared capture device used by the LVDS receivers.
    /// </summary>
    public bool AvtpFaultEnabled
    {
        get => _avtpFaultEnabled;
        set
        {
            if (_avtpFaultEnabled == value)
                return;

            _avtpFaultEnabled = value;
            if (value)
            {
                StopBlackLoop();
                _log("[avtp-tx] Communication fault enabled: TX sending blocked");
            }
        }
    }

    /// <summary>
    /// Initializes the transmitter on the specified device.
    /// </summary>
    /// <returns>True if successful, false otherwise.</returns>
    public bool Initialize(string? deviceName, string srcMac = "3C:CE:15:00:00:19", string dstMac = "01:00:5E:16:00:12",
        int vlanId = 70, int vlanPriority = 5, ushort etherType = 0x22F0, byte streamIdLastByte = 0x50)
    {
        Dispose();

        if (string.IsNullOrWhiteSpace(deviceName))
        {
            _log("[avtp-tx] TX disabled: no TX device selected/found.");
            return false;
        }

        try
        {
            _tx = new AvtpRvfTransmitter(deviceName, srcMac, dstMac, vlanId, vlanPriority, streamIdLastByte, etherType);
            _log($"[avtp-tx] TX ready on {deviceName} (src={srcMac}, dst={dstMac}, vlan={vlanId}, pcp={vlanPriority}, ethType=0x{etherType:X4}, streamIdByte=0x{streamIdLastByte:X2})");
            _txErrOnce = 0;
            _txNoDevOnce = 0;
            return true;
        }
        catch (Exception ex)
        {
            _log($"[avtp-tx] TX init ERROR: {ex.GetType().Name}: {ex.Message}");
            try { _tx?.Dispose(); } catch { }
            _tx = null;
            return false;
        }
    }

    /// <summary>
    /// Sends a frame asynchronously. Pads smaller frames to AVTP protocol size (25,600 bytes).
    /// For Nichia (256×64 = 16,384 bytes), the frame is zero-padded linearly (CANoe approach).
    /// </summary>
    public async Task<bool> SendFrameAsync(byte[] frameData, CancellationToken ct)
    {
        if (_avtpFaultEnabled)
            return false;

        if (_tx == null)
        {
            if (Interlocked.Exchange(ref _txNoDevOnce, 1) == 0)
                _log("[avtp-tx] TX is NULL -> nothing will be sent (select NIC and press Start).");
            return false;
        }

        try
        {
            // Pad frame to AVTP protocol size if needed (e.g., Nichia 256×64 -> 320×80)
            byte[] txFrame = PadToAvtpSize(frameData);
            await _tx.SendFrame320x80Async(txFrame, ct);

            // Periodic diagnostic: log actual TX rate every ~5 seconds
            long count = Interlocked.Increment(ref _txLogCounter);
            if (count % TxLogInterval == 0)
            {
                long sent = _tx.TxFramesSent;
                long dropped = _tx.TxFramesDropped;
                _log($"[avtp-tx] rate-limiter: sent={sent} dropped={dropped} (max 100fps enforced)");
            }

            return true;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            if (Interlocked.Exchange(ref _txErrOnce, 1) == 0)
                _log($"[avtp-tx] SEND ERROR (first): {ex.GetType().Name}: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Pads frame data to AVTP protocol size (25,600 bytes) if needed.
    /// Uses linear padding (copy source bytes, rest is zero) like CANoe implementation.
    /// </summary>
    private byte[] PadToAvtpSize(byte[] frameData)
    {
        if (frameData.Length == AVTP_FRAME_SIZE)
            return frameData; // Already correct size (320×80)

        if (frameData.Length > AVTP_FRAME_SIZE)
        {
            // Truncate if larger (shouldn't happen, but be safe)
            Buffer.BlockCopy(frameData, 0, _paddedFrame, 0, AVTP_FRAME_SIZE);
        }
        else
        {
            // Copy source frame and zero-pad the rest (linear padding like CANoe)
            Buffer.BlockCopy(frameData, 0, _paddedFrame, 0, frameData.Length);
            Array.Clear(_paddedFrame, frameData.Length, AVTP_FRAME_SIZE - frameData.Length);
        }
        return _paddedFrame;
    }

    /// <summary>
    /// Starts a background loop that sends BLACK frames at the specified FPS.
    /// Used when Player mode is stopped but we need to keep AVTP signal alive.
    /// </summary>
    public void StartBlackLoop(int fps)
    {
        StopBlackLoop();

        if (_tx == null) return;
        if (fps <= 0) fps = 100;

        _blackCts = new CancellationTokenSource();
        var ct = _blackCts.Token;

        _blackTask = Task.Run(async () =>
        {
            var period = TimeSpan.FromMilliseconds(1000.0 / fps);

            while (!ct.IsCancellationRequested)
            {
                try
                {
                    if (!_avtpFaultEnabled)
                        await _tx.SendFrame320x80Async(_blackFrame, ct);
                    await Task.Delay(period, ct);
                }
                catch (OperationCanceledException) { break; }
                catch (Exception ex)
                {
                    _log($"[avtp-tx] BLACK loop error: {ex.GetType().Name}: {ex.Message}");
                    break;
                }
            }
        }, ct);

        _log($"[avtp-tx] BLACK loop started @ {fps} fps");
    }

    /// <summary>
    /// Stops the BLACK frame loop if running.
    /// </summary>
    public void StopBlackLoop()
    {
        try
        {
            _blackCts?.Cancel();
            _blackCts?.Dispose();
        }
        catch { }
        finally
        {
            _blackCts = null;
            _blackTask = null;
        }
    }

    /// <summary>
    /// Disposes the transmitter and stops any loops.
    /// </summary>
    public void Dispose()
    {
        StopBlackLoop();
        try { _tx?.Dispose(); } catch { }
        _tx = null;
    }
}
