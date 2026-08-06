using System.Collections.Generic;

namespace VilsSharpX;

/// <summary>
/// Register/address name lookup for LSM devices.
/// Supports device-specific profiles: OSRAM and Nichia/TLD816K.
/// Dynamically extracts register names and memory type from active device profile.
/// </summary>
public static class LsmRegisterMap
{
    private const string UnknownName = "Unknown";

    /// <summary>
    /// Resolve register name and memory type using the active device profile.
    /// If profile is null, defaults to OSRAM.
    /// Returns deterministic fallback names for undocumented addresses.
    /// </summary>
    public static (string Name, string MemType) Resolve(ushort address, LsmDeviceProfile? profile = null)
    {
        profile ??= OsramProfile.Instance;

        var (name, _) = profile.ResolveRegister(address);
        var memType = profile.GetMemoryType(address);

        if (!string.Equals(name, UnknownName, System.StringComparison.Ordinal))
            return (NormalizeDisplayName(name), memType);

        return (BuildFallbackName(address, memType), memType);
    }

    /// <summary>
    /// Resolve register name and memory type using a device type enum.
    /// </summary>
    public static (string Name, string MemType) Resolve(ushort address, LsmDeviceType deviceType)
    {
        var profile = LsmDeviceProfile.GetProfile(deviceType);
        return Resolve(address, profile);
    }

    /// <summary>
    /// Resolve register name and memory type from DeviceId (0=Nichia, 1=OSRAM).
    /// Used for detection from UART responses.
    /// </summary>
    public static (string Name, string MemType) ResolveFromDeviceId(ushort address, byte deviceId)
    {
        var profile = LsmDeviceProfile.GetProfileFromDeviceId(deviceId);
        return Resolve(address, profile);
    }

    /// <summary>
    /// Resolve register name and memory type from DeviceId with an explicit address-space
    /// hint. For Nichia/TLD816K the space is selected by the UART FUN field
    /// (FUN 4/5 = ASIC 1-byte, FUN 6/7 = EEPROM 2-byte offset), not by numeric range.
    /// </summary>
    public static (string Name, string MemType) ResolveFromDeviceId(ushort address, byte deviceId, bool isEepromAccess)
    {
        var profile = LsmDeviceProfile.GetProfileFromDeviceId(deviceId);
        profile ??= OsramProfile.Instance;

        var (name, _) = profile.ResolveRegister(address, isEepromAccess);
        var memType = profile.GetMemoryType(address, isEepromAccess);

        if (!string.Equals(name, UnknownName, System.StringComparison.Ordinal))
            return (NormalizeDisplayName(name), memType);

        return (BuildFallbackName(address, memType), memType);
    }

    /// <summary>
    /// Return description for a known register address using the active profile.
    /// If profile is null, defaults to OSRAM.
    /// </summary>
    public static string GetDescription(ushort address, LsmDeviceProfile? profile = null)
    {
        profile ??= OsramProfile.Instance;
        var (name, description) = profile.ResolveRegister(address);
        if (!string.Equals(name, UnknownName, System.StringComparison.Ordinal))
            return description;

        var memType = profile.GetMemoryType(address);
        return BuildFallbackDescription(address, memType);
    }

    /// <summary>
    /// Return description for a known register address using a device type enum.
    /// </summary>
    public static string GetDescription(ushort address, LsmDeviceType deviceType)
    {
        var profile = LsmDeviceProfile.GetProfile(deviceType);
        return GetDescription(address, profile);
    }

    /// <summary>
    /// Return description for a known register address from DeviceId (0=Nichia, 1=OSRAM).
    /// Used for detection from UART responses.
    /// </summary>
    public static string GetDescription(ushort address, byte deviceId)
    {
        var profile = LsmDeviceProfile.GetProfileFromDeviceId(deviceId);
        return GetDescription(address, profile);
    }

    /// <summary>
    /// Return description for an address from DeviceId with an explicit address-space hint
    /// (see <see cref="ResolveFromDeviceId(ushort, byte, bool)"/>).
    /// </summary>
    public static string GetDescription(ushort address, byte deviceId, bool isEepromAccess)
    {
        var profile = LsmDeviceProfile.GetProfileFromDeviceId(deviceId);
        profile ??= OsramProfile.Instance;

        var (name, description) = profile.ResolveRegister(address, isEepromAccess);
        if (!string.Equals(name, UnknownName, System.StringComparison.Ordinal))
            return description;

        var memType = profile.GetMemoryType(address, isEepromAccess);
        return BuildFallbackDescription(address, memType);
    }

    private static string BuildFallbackName(ushort address, string memType)
    {
        return memType switch
        {
            "ASIC" => $"ASIC_UNDOC_{address:X4}",
            "EEPROM" => $"EEPROM_BLOCK_{((address - 0x0100) / 0x40):D2}",
            "FPGA" => $"FPGA_BLOCK_{((address - 0x2000) / 0x40):D3}",
            _ => $"REG_{address:X4}",
        };
    }

    private static string NormalizeDisplayName(string name)
    {
        return name switch
        {
            "FWC_OR_FEC" => "FWC/FEC",
            "FWCT_OR_FECT" => "FWCT/FECT",
            "EVDDPT_OR_EVCCPT" => "EVDDPT/EVCCPT",
            _ => name,
        };
    }

    private static string BuildFallbackDescription(ushort address, string memType)
    {
        return memType switch
        {
            "ASIC" => "Undocumented ASIC register",
            "EEPROM" => $"EEPROM configuration shadow block (aligned base 0x{(address & 0xFFC0):X4})",
            "FPGA" => $"FPGA data/LUT block (aligned base 0x{(address & 0xFFC0):X4})",
            _ => $"Undocumented register address 0x{address:X4}",
        };
    }
}
