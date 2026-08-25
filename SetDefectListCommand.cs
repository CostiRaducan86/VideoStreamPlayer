using System;
using System.Collections.Generic;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;
using VilsSharpX.DefectPixel;

namespace VilsSharpX;

/// <summary>
/// Sends the OSRAM defect-injection list to the SmartVisio Box via Ethernet.
/// Protocol: ethertype 0x88B5, magic "CM" (0x434D), cmd 0x04 = SET_DEFECT_LIST.
///
/// The C# side only DEFINES defects. The actual ELEDERP/ELEDERS injection into the
/// CAN-UART stream (LSM -> SmartVisio Box -> ECU) is performed by the SmartVisio Box firmware while the
/// received enable flag is set. This command pushes the current defect table to the SmartVisio Box.
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
    private const byte CmdSetDefectListNichia = 0x05;  // Nichia/TLD816K layout

    /// <summary>Maximum number of defect slots supported by the OSRAM diagnostic register banks.</summary>
    public const int MaxDefects = 64;

    /// <summary>Maximum number of Nichia defect slots (per-segment-pair PIXEL_ID banks).</summary>
    public const int MaxNichiaDefects = 64;

    private const int BytesPerDefect = 5;
    private const int NichiaBytesPerDefect = 4;   // idx_hi, idx_lo, type, segPair
    private const int HeaderOffset = 19;   // 14 eth + 2 magic + 1 cmd + 1 enable + 1 count
    private const int MinFrameSize = 60;

    /// <summary>
    /// Sends SET_DEFECT_LIST to the SmartVisio Box. Sends 3x for reliability (no ACK protocol).
    /// </summary>
    /// <param name="pcapDeviceName">TX NIC device name.</param>
    /// <param name="enable">True to enable injection in the SmartVisio Box, false to disable.</param>
    /// <param name="defects">Active defect definitions (truncated to <see cref="MaxDefects"/>).</param>
    /// <param name="log">Optional diagnostic logger.</param>
    public static void Send(
        string pcapDeviceName,
        bool enable,
        IReadOnlyList<OsramDefectEntry> defects,
        Action<string>? log = null)
    {
        var pkt = SetDefectListPacketBuilder.BuildOsramPacket(enable, defects);

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

            log?.Invoke($"[cmd] Sent SET_DEFECT_LIST → enable={(enable ? 1 : 0)} count={pkt[18]}");
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

    /// <summary>
    /// Sends the Nichia/TLD816K defect-injection list to the SmartVisio Box.
    /// Same ethertype/magic as the OSRAM command but cmd 0x05 with a Nichia record layout.
    ///
    /// Payload layout (after 14-byte Ethernet header):
    ///   [14..15] magic 0x434D
    ///   [16]     cmd 0x05
    ///   [17]     enable (0 = disable, 1 = enable)
    ///   [18]     count  (number of defect records, 0..64)
    ///   [19..]   count x 4-byte records:
    ///              +0 pixelId0 high ((pixelId0 >> 8) & 0xFF)   pixelId0 = row*256 + col, 0..16383
    ///              +1 pixelId0 low  (pixelId0 & 0xFF)
    ///              +2 type          (0 = dark, 1 = bright)
    ///              +3 segPair       (0 = segments 0&amp;1, 1 = segments 2&amp;3)
    /// </summary>
    public static void SendNichia(
        string pcapDeviceName,
        bool enable,
        IReadOnlyList<NichiaDefectEntry> defects,
        Action<string>? log = null)
    {
        var pkt = SetDefectListPacketBuilder.BuildNichiaPacket(enable, defects);

        var existing = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(d => string.Equals(d.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (existing == null)
        {
            log?.Invoke($"[cmd] NIC not found for Nichia defect-list command: {pcapDeviceName}");
            return;
        }

        // Independent handle so we don't disrupt ongoing captures.
        var txDev = new LibPcapLiveDevice(existing.Interface);
        txDev.Open(DeviceModes.Promiscuous, read_timeout: 1);
        try
        {
            for (int i = 0; i < 3; i++)
                txDev.SendPacket(pkt);

            log?.Invoke($"[cmd] Sent SET_DEFECT_LIST (Nichia) → enable={(enable ? 1 : 0)} count={pkt[18]}");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] Nichia defect-list send error: {ex.Message}");
        }
        finally
        {
            txDev.Close();
        }
    }

}
