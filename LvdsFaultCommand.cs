using System;
using System.Linq;
using SharpPcap;
using SharpPcap.LibPcap;

namespace VilsSharpX;

/// <summary>
/// Sends the physical LVDS SELECT_LOCAL_IDLE command to the SmartVisio box.
/// The AURIX Ethernet LVDS monitor path remains active while this command is used.
/// </summary>
public static class LvdsFaultCommand
{
    private const ushort Ethertype = 0x88B5;
    private const ushort MagicCommand = 0x434D;
    private const byte CommandId = 0x07;
    private const byte SelectLocalIdleMode = 0x01;
    private const byte ProfileCurrent = 0x00;
    private const byte ProfileNichia = 0x01;
    private const byte ProfileOsram = 0x02;

    public static bool Send(
        string pcapDeviceName,
        LsmDeviceType deviceType,
        int durationMilliseconds,
        bool start,
        Action<string>? log = null)
    {
        int durationUnits = durationMilliseconds == 0
            ? 0
            : Math.Clamp((durationMilliseconds + 99) / 100, 1, 600);
        byte profile = deviceType switch
        {
            LsmDeviceType.Nichia => ProfileNichia,
            LsmDeviceType.Osram20 or LsmDeviceType.Osram205 => ProfileOsram,
            _ => ProfileCurrent
        };
        var packet = new byte[60];

        for (int i = 0; i < 6; i++)
            packet[i] = 0xFF;

        packet[6] = 0x02;
        packet[7] = 0x0A;
        packet[8] = 0xF0;
        packet[9] = 0x4E;
        packet[10] = 0x49;
        packet[11] = 0x02;
        packet[12] = (byte)(Ethertype >> 8);
        packet[13] = (byte)(Ethertype & 0xFF);
        packet[14] = (byte)(MagicCommand >> 8);
        packet[15] = (byte)(MagicCommand & 0xFF);
        packet[16] = CommandId;
        packet[17] = SelectLocalIdleMode;
        packet[18] = profile;
        packet[19] = (byte)(durationUnits >> 8);
        packet[20] = (byte)durationUnits;
        packet[21] = start ? (byte)1 : (byte)0;

        var device = CaptureDeviceList.Instance
            .OfType<LibPcapLiveDevice>()
            .FirstOrDefault(candidate => string.Equals(
                candidate.Name, pcapDeviceName, StringComparison.OrdinalIgnoreCase));

        if (device == null)
        {
            log?.Invoke($"[cmd] NIC not found for LVDS fault command: {pcapDeviceName}");
            return false;
        }

        var txDevice = new LibPcapLiveDevice(device.Interface);
        bool opened = false;
        try
        {
            txDevice.Open(DeviceModes.Promiscuous, read_timeout: 1);
            opened = true;
            for (int i = 0; i < 3; i++)
                txDevice.SendPacket(packet);

            string action = start ? "START" : "CLEAR";
            log?.Invoke($"[cmd] Sent LVDS SELECT_LOCAL_IDLE {action} -> profile={profile}, duration={durationUnits * 100} ms");
            return true;
        }
        catch (Exception ex)
        {
            log?.Invoke($"[cmd] LVDS SELECT_LOCAL_IDLE send error: {ex.GetType().Name}: {ex.Message}");
            return false;
        }
        finally
        {
            if (opened)
                txDevice.Close();
        }
    }
}