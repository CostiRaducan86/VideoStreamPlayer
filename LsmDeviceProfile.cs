using System;
using System.Collections.Generic;

namespace VilsSharpX;

/// <summary>
/// Abstract base class for LSM device-specific profiles.
/// Defines register names, memory type extraction, and pixel decoding per device.
/// </summary>
public abstract class LsmDeviceProfile
{
    /// <summary>Device type this profile supports.</summary>
    public abstract LsmDeviceType DeviceType { get; }
    
    /// <summary>Display name for this profile.</summary>
    public abstract string DisplayName { get; }
    
    /// <summary>Active pixel width (e.g., 320 for OSRAM, 256 for Nichia).</summary>
    public abstract int MaxPixelsX { get; }
    
    /// <summary>Active pixel height (e.g., 80 for OSRAM, 64 for Nichia).</summary>
    public abstract int MaxPixelsY { get; }
    
    /// <summary>
    /// Address ranges for this device.
    /// Maps memory type name to (MinAddress, MaxAddress) tuple.
    /// Supports: ASIC, EEPROM, FPGA, Unknown.
    /// </summary>
    protected abstract Dictionary<string, (ushort Min, ushort Max)> AddressSpaces { get; }
    
    /// <summary>
    /// Register name dictionary: Address -> (Name, Description).
    /// Populated per device type in subclasses.
    /// </summary>
    protected abstract Dictionary<ushort, (string Name, string Description)> RegisterDictionary { get; }
    
    /// <summary>
    /// Resolve register name and description from address.
    /// Returns ("Unknown", "") if address not found.
    /// </summary>
    public (string Name, string Description) ResolveRegister(ushort address)
    {
        return RegisterDictionary.TryGetValue(address, out var reg)
            ? reg
            : ("Unknown", "");
    }
    
    /// <summary>
    /// Get memory type (ASIC, EEPROM, FPGA, or Unknown) for an address.
    /// Context-aware: checks address ranges defined in AddressSpaces.
    /// </summary>
    public string GetMemoryType(ushort address)
    {
        foreach (var (memType, (min, max)) in AddressSpaces)
        {
            if (address >= min && address <= max)
                return memType;
        }
        return "Unknown";
    }
    
    /// <summary>
    /// Get the active profile for a device type.
    /// </summary>
    public static LsmDeviceProfile GetProfile(LsmDeviceType deviceType)
    {
        return deviceType switch
        {
            LsmDeviceType.Osram20 => OsramProfile.Instance,
            LsmDeviceType.Osram205 => OsramProfile.Instance,
            LsmDeviceType.Nichia => NichiaProfile.Instance,
            _ => throw new ArgumentException($"Unknown device type: {deviceType}")
        };
    }
    
    /// <summary>
    /// Get profile from DeviceId (0 = Nichia, 1 = OSRAM).
    /// Used for detection from UART responses.
    /// </summary>
    public static LsmDeviceProfile GetProfileFromDeviceId(byte deviceId)
    {
        return deviceId switch
        {
            0 => NichiaProfile.Instance,
            1 => OsramProfile.Instance,
            _ => OsramProfile.Instance  // Default to OSRAM if unknown
        };
    }
}
