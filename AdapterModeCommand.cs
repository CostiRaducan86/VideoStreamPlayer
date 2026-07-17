using System;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;

namespace VilsSharpX;

/// <summary>
/// Sends adapter-mode command packets to the SmartVisio Box via Ethernet.
/// Protocol: ethertype 0x88B5, magic "CM" (0x434D), cmd 0x03 = SET_ADAPTER_MODE.
/// Payload byte [17] = control_mode (0=ECU, 1=Direct), byte [18] = can_uart_mode (0/1/2).
/// </summary>
public static class AdapterModeCommand
{
    private const ushort Ethertype = 0x88B5;
    private const ushort MagicCommand = 0x434D;  // "CM"
    private const byte CmdSetAdapter = 0x03;

    /// <summary>
    /// Sends SET_ADAPTER_MODE command to the SmartVisio Box.
    /// Sends 3× for reliability (no ACK protocol).
    /// </summary>
    public static void SendAdapterMode(string pcapDeviceName, int controlMode, int canUartMode, Action<string>? log = null)
    {
        byte ctrlByte = (byte)Math.Clamp(controlMode, 0, 1);
        byte canByte  = (byte)Math.Clamp(canUartMode, 0, 2);

        // Build 60-byte Ethernet frame (minimum size excl. FCS)
        var pkt = new byte[60];

        // Dst MAC: broadcast
        pkt[0] = pkt[1] = pkt[2] = pkt[3] = pkt[4] = pkt[5] = 0xFF;

        // Src MAC: locally-administered (same as DeviceModeCommand)
        pkt[6] = 0x02; pkt[7] = 0x0A; pkt[8] = 0xF0;
        pkt[9] = 0x4E; pkt[10] = 0x49; pkt[11] = 0x02;

        // EtherType
        pkt[12] = (byte)(Ethertype >> 8);
        pkt[13] = (byte)(Ethertype & 0xFF);

        // Command header: magic + cmd + payload
        pkt[14] = (byte)(MagicCommand >> 8);
        pkt[15] = (byte)(MagicCommand & 0xFF);
        pkt[16] = CmdSetAdapter;
        pkt[17] = ctrlByte;
        pkt[18] = canByte;

        // Remaining bytes 19..59 stay 0x00 (padding)

        var existing = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(d => string.Equals(d.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (existing == null)
        {
            log?.Invoke($"[cmd] NIC not found for adapter-mode command: {pcapDeviceName}");
            return;
        }

        var txDev = new LibPcapLiveDevice(existing.Interface);
        txDev.Open(DeviceModes.Promiscuous, read_timeout: 1);
        try
        {
            for (int i = 0; i < 3; i++)
                txDev.SendPacket(pkt);

            string ctrlName = ctrlByte == 0 ? "ECU" : "Direct";
            string canName = canByte switch { 0 => "ECU LSM", 1 => "ECU SmartVisio LSM", _ => "SmartVisio LSM" };
            log?.Invoke($"[cmd] Sent SET_ADAPTER_MODE → Control={ctrlName}, CAN={canName}");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] Send error: {ex.Message}");
        }
        finally
        {
            txDev.Close();
        }
    }
}
