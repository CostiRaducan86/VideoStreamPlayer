using System;
using System.Collections.Generic;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;
using VilsSharpX.DefectPixel;

namespace VilsSharpX;

/// <summary>
/// Sends the OSRAM defect-injection list to the Aurix ECU via Ethernet.
/// Protocol: ethertype 0x88B5, magic "CM" (0x434D), cmd 0x04 = SET_DEFECT_LIST.
///
/// The C# side only DEFINES defects. The actual ELEDERP/ELEDERS injection into the
/// CAN-UART stream (LSM -> Aurix -> ECU) is performed by the Aurix firmware while the
/// received enable flag is set. This command pushes the current defect table to Aurix.
///
/// Payload layout (after 14-byte Ethernet header):
///   [14..15] magic 0x434D
///   [16]     cmd 0x04
///   [17]     enable (0 = disable injection, 1 = enable injection)
///   [18]     count  (number of defect records, 0..64)
///   [19..]   count x 5-byte records:
///              +0 slot     (0..63)
///              +1 x_hi     ((x >> 8) & 0xFF)   x is 0..319
///              +2 x_lo     (x & 0xFF)
///              +3 y        (0..79)
///              +4 status   ((pxState &lt;&lt; 2) | (defectType &amp; 0x03))  same bit layout as ELEDERS low bits
/// </summary>
public static class SetDefectListCommand
{
    private const ushort Ethertype = 0x88B5;
    private const ushort MagicCommand = 0x434D;  // "CM"
    private const byte CmdSetDefectList = 0x04;

    /// <summary>Maximum number of defect slots supported by the OSRAM diagnostic register banks.</summary>
    public const int MaxDefects = 64;

    private const int BytesPerDefect = 5;
    private const int HeaderOffset = 19;   // 14 eth + 2 magic + 1 cmd + 1 enable + 1 count
    private const int MinFrameSize = 60;

    /// <summary>
    /// Sends SET_DEFECT_LIST to the Aurix ECU. Sends 3x for reliability (no ACK protocol).
    /// </summary>
    /// <param name="pcapDeviceName">TX NIC device name.</param>
    /// <param name="enable">True to enable injection in Aurix, false to disable.</param>
    /// <param name="defects">Active defect definitions (truncated to <see cref="MaxDefects"/>).</param>
    /// <param name="log">Optional diagnostic logger.</param>
    public static void Send(
        string pcapDeviceName,
        bool enable,
        IReadOnlyList<OsramDefectEntry> defects,
        Action<string>? log = null)
    {
        defects ??= [];
        int count = Math.Min(defects.Count, MaxDefects);

        int frameLen = Math.Max(MinFrameSize, HeaderOffset + count * BytesPerDefect);
        var pkt = new byte[frameLen];

        // Dst MAC: broadcast
        pkt[0] = pkt[1] = pkt[2] = pkt[3] = pkt[4] = pkt[5] = 0xFF;

        // Src MAC: locally-administered (same as other CM commands)
        pkt[6] = 0x02; pkt[7] = 0x0A; pkt[8] = 0xF0;
        pkt[9] = 0x4E; pkt[10] = 0x49; pkt[11] = 0x02;

        // EtherType
        pkt[12] = (byte)(Ethertype >> 8);
        pkt[13] = (byte)(Ethertype & 0xFF);

        // Command header
        pkt[14] = (byte)(MagicCommand >> 8);
        pkt[15] = (byte)(MagicCommand & 0xFF);
        pkt[16] = CmdSetDefectList;
        pkt[17] = enable ? (byte)0x01 : (byte)0x00;
        pkt[18] = (byte)count;

        for (int i = 0; i < count; i++)
        {
            var d = defects[i];
            int slot = Math.Clamp(d.Slot, 0, 63);
            int x = Math.Clamp(d.X, 0, 319);
            int y = Math.Clamp(d.Y, 0, 79);
            int pxState = d.PxState != 0 ? 1 : 0;
            int pxDiag = (int)d.DefectType & 0x03;

            int b = HeaderOffset + i * BytesPerDefect;
            pkt[b + 0] = (byte)slot;
            pkt[b + 1] = (byte)((x >> 8) & 0xFF);
            pkt[b + 2] = (byte)(x & 0xFF);
            pkt[b + 3] = (byte)y;
            pkt[b + 4] = (byte)((pxState << 2) | pxDiag);
        }

        var existing = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(d => string.Equals(d.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (existing == null)
        {
            log?.Invoke($"[cmd] NIC not found for defect-list command: {pcapDeviceName}");
            return;
        }

        // Independent handle so we don't disrupt ongoing captures.
        var txDev = new LibPcapLiveDevice(existing.Interface);
        txDev.Open(DeviceModes.Promiscuous, read_timeout: 1);
        try
        {
            for (int i = 0; i < 3; i++)
                txDev.SendPacket(pkt);

            log?.Invoke($"[cmd] Sent SET_DEFECT_LIST → enable={(enable ? 1 : 0)} count={count}");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] Defect-list send error: {ex.Message}");
        }
        finally
        {
            txDev.Close();
        }
    }
}
