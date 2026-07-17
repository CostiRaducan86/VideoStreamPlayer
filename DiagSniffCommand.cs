using System;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;

namespace VilsSharpX;

/// <summary>
/// Sends a diagnostic sniffing start/stop command to the SmartVisio Box via Ethernet.
/// Protocol: ethertype 0x88B5, magic "CM" (0x434D), cmd 0x02 = DIAG_SNIFF.
/// Payload: 0x01 = start, 0x00 = stop.
/// </summary>
public static class DiagSniffCommand
{
    private const ushort Ethertype = 0x88B5;
    private const ushort MagicCommand = 0x434D;  // "CM"
    private const byte CmdDiagSniff = 0x02;

    public static void Send(string pcapDeviceName, bool start, Action<string>? log = null)
    {
        byte payload = start ? (byte)0x01 : (byte)0x00;

        var pkt = new byte[60];

        // Dst MAC: broadcast
        pkt[0] = pkt[1] = pkt[2] = pkt[3] = pkt[4] = pkt[5] = 0xFF;

        // Src MAC: locally-administered
        pkt[6] = 0x02; pkt[7] = 0x0A; pkt[8] = 0xF0;
        pkt[9] = 0x4E; pkt[10] = 0x49; pkt[11] = 0x02;

        // EtherType
        pkt[12] = (byte)(Ethertype >> 8);
        pkt[13] = (byte)(Ethertype & 0xFF);

        // Command header
        pkt[14] = (byte)(MagicCommand >> 8);
        pkt[15] = (byte)(MagicCommand & 0xFF);
        pkt[16] = CmdDiagSniff;
        pkt[17] = payload;

        var existing = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(d => string.Equals(d.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (existing == null)
        {
            log?.Invoke($"[cmd] NIC not found for diag-sniff command: {pcapDeviceName}");
            return;
        }

        // Create an independent handle so we don't disrupt ongoing captures
        var txDev = new LibPcapLiveDevice(existing.Interface);
        txDev.Open(DeviceModes.Promiscuous, read_timeout: 1);
        try
        {
            for (int i = 0; i < 3; i++)
                txDev.SendPacket(pkt);

            log?.Invoke($"[cmd] Sent DIAG_SNIFF → {(start ? "START" : "STOP")}");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] Diag sniff send error: {ex.Message}");
        }
        finally
        {
            txDev.Close();
        }
    }
}
