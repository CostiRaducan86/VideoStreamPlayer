using System;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;

namespace VilsSharpX;

/// <summary>
/// Sends a device-mode command packet to the Aurix ECU via Ethernet.
/// Protocol: ethertype 0x88B5, magic "CM" (0x434D), cmd 0x01 = SET_DEVICE_MODE.
/// </summary>
public static class DeviceModeCommand
{
    private const ushort Ethertype = 0x88B5;
    private const ushort MagicCommand = 0x434D;  // "CM"
    private const byte CmdSetDevice = 0x01;

    // FrameEthDevice values (must match firmware frame_eth.h)
    private const byte FeDeviceNichia = 0;
    private const byte FeDeviceOsram  = 1;

    /// <summary>
    /// Sends SET_DEVICE_MODE command to the ECU.
    /// Sends the packet 3× for reliability (UDP-style, no ACK).
    /// </summary>
    public static void SendDeviceMode(string pcapDeviceName, LsmDeviceType deviceType, Action<string>? log = null)
    {
        byte devByte = deviceType == LsmDeviceType.Nichia ? FeDeviceNichia : FeDeviceOsram;

        // Build 60-byte Ethernet frame (minimum size excl. FCS)
        var pkt = new byte[60];

        // Dst MAC: broadcast
        pkt[0] = pkt[1] = pkt[2] = pkt[3] = pkt[4] = pkt[5] = 0xFF;

        // Src MAC: locally-administered, distinct from ECU's 02:0A:F0:4E:49:01
        pkt[6] = 0x02; pkt[7] = 0x0A; pkt[8] = 0xF0;
        pkt[9] = 0x4E; pkt[10] = 0x49; pkt[11] = 0x02;

        // EtherType
        pkt[12] = (byte)(Ethertype >> 8);
        pkt[13] = (byte)(Ethertype & 0xFF);

        // Command header: magic + cmd + payload
        pkt[14] = (byte)(MagicCommand >> 8);
        pkt[15] = (byte)(MagicCommand & 0xFF);
        pkt[16] = CmdSetDevice;
        pkt[17] = devByte;

        // Remaining bytes 18..59 stay 0x00 (padding)

        var dev = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(d => string.Equals(d.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (dev == null)
        {
            log?.Invoke($"[cmd] NIC not found for device-mode command: {pcapDeviceName}");
            return;
        }

        dev.Open(DeviceModes.Promiscuous, read_timeout: 1);
        try
        {
            // Send 3× for reliability
            for (int i = 0; i < 3; i++)
                dev.SendPacket(pkt);

            log?.Invoke($"[cmd] Sent SET_DEVICE_MODE → {deviceType.GetDisplayName()} (0x{devByte:X2})");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] Send error: {ex.Message}");
        }
        finally
        {
            dev.Close();
        }
    }
}
