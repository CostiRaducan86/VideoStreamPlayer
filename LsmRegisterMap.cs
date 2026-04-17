using System.Collections.Generic;

namespace VilsSharpX;

/// <summary>
/// Register/address name lookup for TLD816K ASIC register space.
/// Matches classic VILS Monitor naming: all registers show MemoryType = "ASIC".
/// Sources: Classic VILS screenshots, TLD816K datasheet, EEPROM mapping CSV.
/// Address convention: UART register byte address (CanDiagRecord.Address).
/// </summary>
public static class LsmRegisterMap
{
    public sealed record RegEntry(ushort Address, string Name, string Description = "");

    // ── Register map: names matched to classic VILS Monitor screenshots ──
    // All addresses seen in classic VILS are ASIC-space (even NVM/EEPROM block addresses).
    private static readonly RegEntry[] s_regs =
    {
        // Standard ASIC registers 0x00..0x33
        new(0x0000, "CR",              "Control Register"),
        new(0x0001, "SR",              "Status Register"),
        new(0x0002, "CGU_CFG0",        "CGU Config 0"),
        new(0x0003, "CGU_CFG1",        "CGU Config 1"),
        new(0x0004, "CGU_CFG2",        "CGU Config 2"),
        new(0x0005, "CIF_CON",         "Camera Interface Control"),
        new(0x0006, "HwSTAT",          "Hardware Status"),
        new(0x0007, "NVMSTAT",         "NVM Status"),
        new(0x0008, "CIF_ADDR_CFG",    "CIF Address Config"),
        new(0x0009, "OVT_CFG",         "Overvoltage Threshold Config"),
        new(0x000A, "STD_DIAG",        "Standard Diagnostics"),
        new(0x000B, "IF_MON",          "Interface Monitor"),
        new(0x000C, "CIF_STATUS",      "CIF Status"),
        new(0x000D, "IPU_STATUS",      "IPU Status"),
        new(0x000E, "CGU_STATUS",      "CGU Status"),
        new(0x000F, "ADC_FLAG",        "ADC Flag"),
        new(0x0010, "FCR0",            "Frame Control Register 0"),
        new(0x0011, "FCR1",            "Frame Control Register 1"),
        new(0x0012, "FEC",             "Frame Error Counter"),
        new(0x0013, "FSTXR",           "Fail-Safe TX Register"),
        new(0x0014, "EEPROM_CFG",      "EEPROM Config"),
        new(0x0015, "TSTDR",           "Test Data Register"),
        new(0x0016, "TSPDR",           "Test SPI Data Register"),
        new(0x0017, "MBDR",            "Mailbox Data Register"),
        new(0x0018, "DTR",             "Data Transfer Register"),
        new(0x0019, "ADR",             "Address Register"),
        new(0x001A, "MB",              "Mailbox"),
        new(0x001B, "CURR",            "Current Register"),
        new(0x001C, "FRT",             "Frame Rate Timer"),
        new(0x001D, "PWMR",            "PWM Register"),
        new(0x001E, "VIF_UART_CFG",    "VIF UART Config"),
        new(0x001F, "VIF_STATUS",      "VIF Status"),
        new(0x0020, "FSTXR",           "Fail-Safe TX Register (alias)"),
        new(0x0021, "SEG01_CFG",       "Segment 0,1 Config"),
        new(0x0022, "SEG23_CFG",       "Segment 2,3 Config"),
        new(0x0023, "ADC_CFG",         "ADC Config"),
        new(0x0024, "ADC_SINGLE",      "ADC Single Conversion"),
        new(0x0025, "ADC_VDDP_THS",    "ADC VDDP Monitor Threshold"),
        new(0x0026, "ADC_LED_H_THS",   "ADC LED Diag DB High Threshold"),
        new(0x0027, "ADC_LED_L_THS",   "ADC LED Diag DB Low Threshold"),
        new(0x002A, "OTPID0",          "OTP ID 0"),
        new(0x002B, "OTPID1",          "OTP ID 1"),
        new(0x002C, "OTPID2",          "OTP ID 2"),
        new(0x002D, "OTPID3",          "OTP ID 3"),
        new(0x002E, "OTPID4",          "OTP ID 4"),
        new(0x002F, "OTPID5",          "OTP ID 5"),
        new(0x0030, "EVCCP0",          "Event/Capture 0"),
        new(0x0031, "EVCCP1",          "Event/Capture 1"),
        new(0x0032, "EVCCP2",          "Event/Capture 2"),
        new(0x0033, "EVCCP3",          "Event/Capture 3"),

        // ELEDER segments + pixels (seen in classic VILS at ASIC addresses)
        new(0x0070, "ELEDERP0",        "ELEDER pixel 0"),
        new(0x0080, "ELEDERP16",       "ELEDER pixel 16"),
        new(0x00E0, "ELEDERS0",        "ELEDER segment 0"),
        new(0x0090, "ELEDERP32",       "ELEDER pixel 32"),
        new(0x00A0, "ELEDERP48",       "ELEDER pixel 48"),
        new(0x00B0, "ELEDERS64",       "ELEDER segment 64"),
        new(0x00C0, "ELEDERS16",       "ELEDER segment 16"),
        new(0x00D0, "ELEDERS32",       "ELEDER segment 32"),
        new(0x00E0, "ELEDERS48",       "ELEDER segment 48"),
        new(0x00F7, "OSHRS",           "OS Hardware Reset Status"),
        new(0x00F8, "OSHRS",           "OS Hardware Reset Status (alias)"),

        // NVM blocks (seen in classic VILS as ASIC addresses)
        new(0x0100, "NVMDAT0",         "NVM Data block 0"),
        new(0x0108, "NVMPTRH",         "NVM Pointer High"),
        new(0x0110, "NVMDAT16",        "NVM Data block 16"),
        new(0x0120, "NVMDAT32",        "NVM Data block 32"),
        new(0x0130, "NVMDAT48",        "NVM Data block 48"),
        new(0x0140, "NVMDAT64",        "NVM Data block 64"),
        new(0x0150, "NVMDAT80",        "NVM Data block 80"),
        new(0x0160, "NVMDAT96",        "NVM Data block 96"),
        new(0x0170, "NVMDAT112",       "NVM Data block 112"),
    };

    // ── Lookup dictionary (built once) ──
    private static readonly Dictionary<ushort, RegEntry> s_dict;

    static LsmRegisterMap()
    {
        s_dict = new Dictionary<ushort, RegEntry>(s_regs.Length);
        foreach (var e in s_regs)
        {
            if (e.Name != null)
                s_dict.TryAdd(e.Address, e);
        }
    }

    /// <summary>
    /// Resolve register name. Classic VILS shows MemoryType = "ASIC" for all registers.
    /// </summary>
    public static (string Name, string MemType) Resolve(ushort address)
    {
        if (s_dict.TryGetValue(address, out var entry))
            return (entry.Name, "ASIC");

        // Unknown — show "/" like classic VILS for unknown registers
        return ("/", "ASIC");
    }

    /// <summary>
    /// Return description for a known register address.
    /// </summary>
    public static string GetDescription(ushort address, string memType)
    {
        if (s_dict.TryGetValue(address, out var entry))
            return entry.Description;
        return string.Empty;
    }
}
