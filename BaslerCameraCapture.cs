using System;
using System.Diagnostics;
using Basler.Pylon;

#pragma warning disable CS8602 // _camera is guaranteed non-null after OpenAndStart()

namespace VilsSharpX;

/// <summary>
/// Captures grayscale frames from a Basler USB3 Vision camera using the Pylon .NET SDK.
/// Uses the StreamGrabber's internal grab loop thread with LatestImages strategy
/// so the UI always receives the most recent frame.
/// </summary>
internal sealed class BaslerCameraCapture : IDisposable
{
    // ── FPS estimation ──────────────────────────────────────────────
    private const double FpsEmaAlpha = 0.05;
    private readonly Stopwatch _fpsSw = Stopwatch.StartNew();
    private long _lastFrameTicks;

    public double FpsEma { get; private set; }
    public long FramesCompleted { get; private set; }
    public bool IsCapturing { get; private set; }

    public int FrameWidth { get; private set; }
    public int FrameHeight { get; private set; }

    /// <summary>
    /// Fired on the Pylon grab-loop thread when a complete Gray8 frame is available.
    /// The byte[] contains Width*Height pixels in row-major order.
    /// Subscriber must marshal to the UI thread if touching WPF controls.
    /// </summary>
    public event Action<byte[], int, int>? OnFrameReady;

    private Camera _camera = null!;
    private readonly Action<string> _log;
    private bool _disposed;

    private BaslerCameraCapture(Action<string> log) => _log = log;

    // ── Factory ─────────────────────────────────────────────────────

    /// <summary>
    /// Finds the first available Basler camera, opens it, configures Mono8 + continuous acquisition,
    /// and starts the internal grab loop. Returns a capture instance that fires <see cref="OnFrameReady"/>.
    /// </summary>
    public static BaslerCameraCapture Start(Action<string> log)
    {
        var cap = new BaslerCameraCapture(log);
        try
        {
            cap.OpenAndStart();
        }
        catch
        {
            cap.Dispose();
            throw;
        }
        return cap;
    }

    private void OpenAndStart()
    {
        _camera = new Camera();
        _camera.CameraOpened += (sender, e) => Configuration.AcquireContinuous(sender!, e);
        _camera.Open();

        // Log camera info
        string model = _camera.CameraInfo[CameraInfoKey.ModelName] ?? "?";
        string serial = _camera.CameraInfo[CameraInfoKey.SerialNumber] ?? "?";
        _log($"[basler] Camera opened: {model} (S/N {serial})");

        // Set pixel format to Mono8 (Gray8) if available
        _camera.Parameters[PLCamera.PixelFormat].TrySetValue(PLCamera.PixelFormat.Mono8);

        // Configure hardware trigger from Aurix P23.1 (Line3, rising edge)
        _camera.Parameters[PLCamera.TriggerSelector].TrySetValue(PLCamera.TriggerSelector.FrameStart);
        if (_camera.Parameters[PLCamera.TriggerMode].TrySetValue(PLCamera.TriggerMode.On))
        {
            _camera.Parameters[PLCamera.TriggerSource].TrySetValue(PLCamera.TriggerSource.Line3);
            _camera.Parameters[PLCamera.TriggerActivation].TrySetValue(PLCamera.TriggerActivation.RisingEdge);
            _log("[basler] Hardware trigger: Line3, RisingEdge");
        }
        else
        {
            _log("[basler] Hardware trigger not available, using free-run");
        }

        // Read actual dimensions
        FrameWidth = (int)_camera.Parameters[PLCamera.Width].GetValue();
        FrameHeight = (int)_camera.Parameters[PLCamera.Height].GetValue();
        _log($"[basler] Resolution: {FrameWidth}×{FrameHeight}, PixelFormat: {_camera.Parameters[PLCamera.PixelFormat].GetValue()}");

        // Subscribe to grab events
        _camera.StreamGrabber.ImageGrabbed += OnImageGrabbed;

        // Start grabbing with the internal grab-loop thread (LatestImages = always get freshest frame)
        _camera.StreamGrabber.Start(GrabStrategy.LatestImages, GrabLoop.ProvidedByStreamGrabber);
        IsCapturing = true;
        _lastFrameTicks = _fpsSw.ElapsedTicks;
        _log("[basler] Grab started (LatestImages, ProvidedByStreamGrabber)");
    }

    // ── Grab callback (runs on Pylon's internal thread) ─────────────

    private void OnImageGrabbed(object? sender, ImageGrabbedEventArgs e)
    {
        try
        {
            IGrabResult grabResult = e.GrabResult;
            if (!grabResult.GrabSucceeded)
            {
                _log($"[basler] Grab error: {grabResult.ErrorCode} {grabResult.ErrorDescription}");
                return;
            }

            int w = grabResult.Width;
            int h = grabResult.Height;

            // Copy pixel data out of the grab result buffer before it is reused
            if (grabResult.PixelData is not byte[] srcPixels || srcPixels.Length < w * h)
                return;

            var frame = new byte[w * h];
            Buffer.BlockCopy(srcPixels, 0, frame, 0, frame.Length);

            // FPS estimation (EMA)
            long now = _fpsSw.ElapsedTicks;
            double dtSec = (now - _lastFrameTicks) / (double)Stopwatch.Frequency;
            _lastFrameTicks = now;
            if (dtSec > 0.0001)
            {
                double instantFps = 1.0 / dtSec;
                FpsEma = FramesCompleted == 0
                    ? instantFps
                    : FpsEma * (1.0 - FpsEmaAlpha) + instantFps * FpsEmaAlpha;
            }

            FramesCompleted++;
            FrameWidth = w;
            FrameHeight = h;

            OnFrameReady?.Invoke(frame, w, h);
        }
        catch (Exception ex)
        {
            _log($"[basler] OnImageGrabbed exception: {ex.Message}");
        }
    }

    // ── Cleanup ─────────────────────────────────────────────────────

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        IsCapturing = false;

        try
        {
            if (_camera.StreamGrabber.IsGrabbing)
                _camera.StreamGrabber.Stop();
        }
        catch { /* ignore */ }

        try { _camera.Close(); } catch { /* ignore */ }
        try { _camera.Dispose(); } catch { /* ignore */ }

        _log("[basler] Camera capture disposed");
    }
}
