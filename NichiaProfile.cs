using System.Collections.Generic;

namespace VilsSharpX;

/// <summary>
/// Nichia (TLD816K) device profile - 256x64 pixels.
/// Registers include LED_DIAG_BRIGHT_FAIL_N, LED_DIAG_DARK_FAIL_N, and PIXEL_ID storage.
/// </summary>
public sealed class NichiaProfile : LsmDeviceProfile
{
    public static readonly NichiaProfile Instance = new();
    private static readonly Dictionary<ushort, (string Name, string Description)> s_registers = BuildRegisterDictionary();

    private NichiaProfile() { }

    public override LsmDeviceType DeviceType => LsmDeviceType.Nichia;
    public override string DisplayName => "NICHIA";
    public override int MaxPixelsX => 256;
    public override int MaxPixelsY => 64;

    protected override Dictionary<string, (ushort Min, ushort Max)> AddressSpaces => new()
    {
        { "ASIC", (0x0000, 0x00FF) },
        { "EEPROM", (0x0100, 0x17FF) },
        { "FPGA", (0x2000, 0x61FF) }
    };

    protected override Dictionary<ushort, (string Name, string Description)> RegisterDictionary => s_registers;

    private static Dictionary<ushort, (string Name, string Description)> BuildRegisterDictionary()
    {
        var regs = new Dictionary<ushort, (string Name, string Description)>
        {
        // ASIC Registers (0x0000–0x00FF) - Nichia TLD816K specific
        { 0x0000, ("CFG_DEVICE", "Device configuration") },
        { 0x0001, ("IPU_CON", "Image Processing Unit configuration") },
        { 0x0002, ("CGU_CFG0", "Clock generation control 0") },
        { 0x0003, ("CGU_CFG1", "Clock generation control 1") },
        { 0x0004, ("CGU_CFG2", "Clock generation control 2") },
        { 0x0005, ("CFG_BOOT_CTRL", "Boot/startup configuration control (undocumented)") },
        { 0x0008, ("CIF_ADDR_CFG", "Control interface address configuration") },
        { 0x000A, ("STD_DIAG", "Standard diagnostic status (dark/bright failure bits)") },
        { 0x000B, ("CIF_CHK_CON", "CIF checksum control") },
        { 0x000C, ("CIF_UART_TIMING_CFG", "CIF UART timing configuration") },
        { 0x000D, ("CIF_CON", "CIF control configuration") },
        { 0x000E, ("CIF_CON_EXT", "CIF control configuration extended") },
        { 0x000F, ("ADC_FLAG", "ADC diagnostic flags (segment pair flags)") },
        { 0x0010, ("OVT_CFG", "Overvoltage configuration") },
        { 0x0017, ("VIF_RGB_VSYNC_CFG", "VIF RGB VSYNC configuration") },
        { 0x0020, ("VIF_RGB_HBP_CFG", "VIF RGB HBP configuration") },
        { 0x0023, ("VIF_UART_CFG", "VIF UART configuration") },
        { 0x0030, ("SEGMENT_CFG01", "Segment pair 0/1 configuration") },
        { 0x0040, ("SEGMENT_CFG23", "Segment pair 2/3 configuration") },
        { 0x0050, ("ASIC_CFG_50", "ASIC configuration block (undocumented)") },
        { 0x0062, ("ASIC_CFG_62", "ASIC diagnostic/config register (undocumented)") },
        
        // Failure counters (Nichia specific)
        { 0x0070, ("LED_DIAG_BRIGHT_FAIL_N", "Bright failure counter (BRIGHT_S01[5:0] + BRIGHT_S23[5:0])") },
        { 0x0071, ("LED_DIAG_DARK_FAIL_N", "Dark failure counter (DARK_S01[5:0] + DARK_S23[5:0])") },
        
        // Pixel ID storage for dark failures (segment pair 0/1)
        { 0x0080, ("PIXEL_ID_0", "Dark pixel ID [0] (S01)") },
        { 0x0081, ("PIXEL_ID_1", "Dark pixel ID [1] (S01)") },
        { 0x0082, ("PIXEL_ID_2", "Dark pixel ID [2] (S01)") },
        { 0x0083, ("PIXEL_ID_3", "Dark pixel ID [3] (S01)") },
        { 0x0084, ("PIXEL_ID_4", "Dark pixel ID [4] (S01)") },
        { 0x0085, ("PIXEL_ID_5", "Dark pixel ID [5] (S01)") },
        { 0x0086, ("PIXEL_ID_6", "Dark pixel ID [6] (S01)") },
        { 0x0087, ("PIXEL_ID_7", "Dark pixel ID [7] (S01)") },
        { 0x0088, ("PIXEL_ID_8", "Dark pixel ID [8] (S01)") },
        { 0x0089, ("PIXEL_ID_9", "Dark pixel ID [9] (S01)") },
        { 0x008A, ("PIXEL_ID_10", "Dark pixel ID [10] (S01)") },
        { 0x008B, ("PIXEL_ID_11", "Dark pixel ID [11] (S01)") },
        { 0x008C, ("PIXEL_ID_12", "Dark pixel ID [12] (S01)") },
        { 0x008D, ("PIXEL_ID_13", "Dark pixel ID [13] (S01)") },
        { 0x008E, ("PIXEL_ID_14", "Dark pixel ID [14] (S01)") },
        { 0x008F, ("PIXEL_ID_15", "Dark pixel ID [15] (S01)") },
        { 0x0090, ("PIXEL_ID_16", "Dark pixel ID [16] (S01)") },
        { 0x0091, ("PIXEL_ID_17", "Dark pixel ID [17] (S01)") },
        { 0x0092, ("PIXEL_ID_18", "Dark pixel ID [18] (S01)") },
        { 0x0093, ("PIXEL_ID_19", "Dark pixel ID [19] (S01)") },
        { 0x0094, ("PIXEL_ID_20", "Dark pixel ID [20] (S01)") },
        { 0x0095, ("PIXEL_ID_21", "Dark pixel ID [21] (S01)") },
        { 0x0096, ("PIXEL_ID_22", "Dark pixel ID [22] (S01)") },
        { 0x0097, ("PIXEL_ID_23", "Dark pixel ID [23] (S01)") },
        { 0x0098, ("PIXEL_ID_24", "Dark pixel ID [24] (S01)") },
        { 0x0099, ("PIXEL_ID_25", "Dark pixel ID [25] (S01)") },
        { 0x009A, ("PIXEL_ID_26", "Dark pixel ID [26] (S01)") },
        { 0x009B, ("PIXEL_ID_27", "Dark pixel ID [27] (S01)") },
        { 0x009C, ("PIXEL_ID_28", "Dark pixel ID [28] (S01)") },
        { 0x009D, ("PIXEL_ID_29", "Dark pixel ID [29] (S01)") },
        { 0x009E, ("PIXEL_ID_30", "Dark pixel ID [30] (S01)") },
        { 0x009F, ("PIXEL_ID_31", "Dark pixel ID [31] (S01)") },
        
        // Pixel ID storage for bright failures (segment pair 0/1)
        { 0x00A0, ("PIXEL_ID_32", "Bright pixel ID [0] (S01)") },
        { 0x00A1, ("PIXEL_ID_33", "Bright pixel ID [1] (S01)") },
        { 0x00A2, ("PIXEL_ID_34", "Bright pixel ID [2] (S01)") },
        { 0x00A3, ("PIXEL_ID_35", "Bright pixel ID [3] (S01)") },
        { 0x00A4, ("PIXEL_ID_36", "Bright pixel ID [4] (S01)") },
        { 0x00A5, ("PIXEL_ID_37", "Bright pixel ID [5] (S01)") },
        { 0x00A6, ("PIXEL_ID_38", "Bright pixel ID [6] (S01)") },
        { 0x00A7, ("PIXEL_ID_39", "Bright pixel ID [7] (S01)") },
        { 0x00A8, ("PIXEL_ID_40", "Bright pixel ID [8] (S01)") },
        { 0x00A9, ("PIXEL_ID_41", "Bright pixel ID [9] (S01)") },
        { 0x00AA, ("PIXEL_ID_42", "Bright pixel ID [10] (S01)") },
        { 0x00AB, ("PIXEL_ID_43", "Bright pixel ID [11] (S01)") },
        { 0x00AC, ("PIXEL_ID_44", "Bright pixel ID [12] (S01)") },
        { 0x00AD, ("PIXEL_ID_45", "Bright pixel ID [13] (S01)") },
        { 0x00AE, ("PIXEL_ID_46", "Bright pixel ID [14] (S01)") },
        { 0x00AF, ("PIXEL_ID_47", "Bright pixel ID [15] (S01)") },
        { 0x00B0, ("PIXEL_ID_48", "Bright pixel ID [16] (S01)") },
        { 0x00B1, ("PIXEL_ID_49", "Bright pixel ID [17] (S01)") },
        { 0x00B2, ("PIXEL_ID_50", "Bright pixel ID [18] (S01)") },
        { 0x00B3, ("PIXEL_ID_51", "Bright pixel ID [19] (S01)") },
        { 0x00B4, ("PIXEL_ID_52", "Bright pixel ID [20] (S01)") },
        { 0x00B5, ("PIXEL_ID_53", "Bright pixel ID [21] (S01)") },
        { 0x00B6, ("PIXEL_ID_54", "Bright pixel ID [22] (S01)") },
        { 0x00B7, ("PIXEL_ID_55", "Bright pixel ID [23] (S01)") },
        { 0x00B8, ("PIXEL_ID_56", "Bright pixel ID [24] (S01)") },
        { 0x00B9, ("PIXEL_ID_57", "Bright pixel ID [25] (S01)") },
        { 0x00BA, ("PIXEL_ID_58", "Bright pixel ID [26] (S01)") },
        { 0x00BB, ("PIXEL_ID_59", "Bright pixel ID [27] (S01)") },
        { 0x00BC, ("PIXEL_ID_60", "Bright pixel ID [28] (S01)") },
        { 0x00BD, ("PIXEL_ID_61", "Bright pixel ID [29] (S01)") },
        { 0x00BE, ("PIXEL_ID_62", "Bright pixel ID [30] (S01)") },
        { 0x00BF, ("PIXEL_ID_63", "Bright pixel ID [31] (S01)") },
        
        // Pixel ID storage for dark failures (segment pair 2/3)
        { 0x00C0, ("PIXEL_ID_64", "Dark pixel ID [0] (S23)") },
        { 0x00C1, ("PIXEL_ID_65", "Dark pixel ID [1] (S23)") },
        { 0x00C2, ("PIXEL_ID_66", "Dark pixel ID [2] (S23)") },
        { 0x00C3, ("PIXEL_ID_67", "Dark pixel ID [3] (S23)") },
        { 0x00C4, ("PIXEL_ID_68", "Dark pixel ID [4] (S23)") },
        { 0x00C5, ("PIXEL_ID_69", "Dark pixel ID [5] (S23)") },
        { 0x00C6, ("PIXEL_ID_70", "Dark pixel ID [6] (S23)") },
        { 0x00C7, ("PIXEL_ID_71", "Dark pixel ID [7] (S23)") },
        { 0x00C8, ("PIXEL_ID_72", "Dark pixel ID [8] (S23)") },
        { 0x00C9, ("PIXEL_ID_73", "Dark pixel ID [9] (S23)") },
        { 0x00CA, ("PIXEL_ID_74", "Dark pixel ID [10] (S23)") },
        { 0x00CB, ("PIXEL_ID_75", "Dark pixel ID [11] (S23)") },
        { 0x00CC, ("PIXEL_ID_76", "Dark pixel ID [12] (S23)") },
        { 0x00CD, ("PIXEL_ID_77", "Dark pixel ID [13] (S23)") },
        { 0x00CE, ("PIXEL_ID_78", "Dark pixel ID [14] (S23)") },
        { 0x00CF, ("PIXEL_ID_79", "Dark pixel ID [15] (S23)") },
        { 0x00D0, ("PIXEL_ID_80", "Dark pixel ID [16] (S23)") },
        { 0x00D1, ("PIXEL_ID_81", "Dark pixel ID [17] (S23)") },
        { 0x00D2, ("PIXEL_ID_82", "Dark pixel ID [18] (S23)") },
        { 0x00D3, ("PIXEL_ID_83", "Dark pixel ID [19] (S23)") },
        { 0x00D4, ("PIXEL_ID_84", "Dark pixel ID [20] (S23)") },
        { 0x00D5, ("PIXEL_ID_85", "Dark pixel ID [21] (S23)") },
        { 0x00D6, ("PIXEL_ID_86", "Dark pixel ID [22] (S23)") },
        { 0x00D7, ("PIXEL_ID_87", "Dark pixel ID [23] (S23)") },
        { 0x00D8, ("PIXEL_ID_88", "Dark pixel ID [24] (S23)") },
        { 0x00D9, ("PIXEL_ID_89", "Dark pixel ID [25] (S23)") },
        { 0x00DA, ("PIXEL_ID_90", "Dark pixel ID [26] (S23)") },
        { 0x00DB, ("PIXEL_ID_91", "Dark pixel ID [27] (S23)") },
        { 0x00DC, ("PIXEL_ID_92", "Dark pixel ID [28] (S23)") },
        { 0x00DD, ("PIXEL_ID_93", "Dark pixel ID [29] (S23)") },
        { 0x00DE, ("PIXEL_ID_94", "Dark pixel ID [30] (S23)") },
        { 0x00DF, ("PIXEL_ID_95", "Dark pixel ID [31] (S23)") },
        
        // Pixel ID storage for bright failures (segment pair 2/3)
        { 0x00E0, ("PIXEL_ID_96", "Bright pixel ID [0] (S23)") },
        { 0x00E1, ("PIXEL_ID_97", "Bright pixel ID [1] (S23)") },
        { 0x00E2, ("PIXEL_ID_98", "Bright pixel ID [2] (S23)") },
        { 0x00E3, ("PIXEL_ID_99", "Bright pixel ID [3] (S23)") },
        { 0x00E4, ("PIXEL_ID_100", "Bright pixel ID [4] (S23)") },
        { 0x00E5, ("PIXEL_ID_101", "Bright pixel ID [5] (S23)") },
        { 0x00E6, ("PIXEL_ID_102", "Bright pixel ID [6] (S23)") },
        { 0x00E7, ("PIXEL_ID_103", "Bright pixel ID [7] (S23)") },
        { 0x00E8, ("PIXEL_ID_104", "Bright pixel ID [8] (S23)") },
        { 0x00E9, ("PIXEL_ID_105", "Bright pixel ID [9] (S23)") },
        { 0x00EA, ("PIXEL_ID_106", "Bright pixel ID [10] (S23)") },
        { 0x00EB, ("PIXEL_ID_107", "Bright pixel ID [11] (S23)") },
        { 0x00EC, ("PIXEL_ID_108", "Bright pixel ID [12] (S23)") },
        { 0x00ED, ("PIXEL_ID_109", "Bright pixel ID [13] (S23)") },
        { 0x00EE, ("PIXEL_ID_110", "Bright pixel ID [14] (S23)") },
        { 0x00EF, ("PIXEL_ID_111", "Bright pixel ID [15] (S23)") },
        { 0x00F0, ("PIXEL_ID_112", "Bright pixel ID [16] (S23)") },
        { 0x00F1, ("PIXEL_ID_113", "Bright pixel ID [17] (S23)") },
        { 0x00F2, ("PIXEL_ID_114", "Bright pixel ID [18] (S23)") },
        { 0x00F3, ("PIXEL_ID_115", "Bright pixel ID [19] (S23)") },
        { 0x00F4, ("PIXEL_ID_116", "Bright pixel ID [20] (S23)") },
        { 0x00F5, ("PIXEL_ID_117", "Bright pixel ID [21] (S23)") },
        { 0x00F6, ("PIXEL_ID_118", "Bright pixel ID [22] (S23)") },
        { 0x00F7, ("PIXEL_ID_119", "Bright pixel ID [23] (S23)") },
        { 0x00F8, ("PIXEL_ID_120", "Bright pixel ID [24] (S23)") },
        { 0x00F9, ("PIXEL_ID_121", "Bright pixel ID [25] (S23)") },
        { 0x00FA, ("PIXEL_ID_122", "Bright pixel ID [26] (S23)") },
        { 0x00FB, ("PIXEL_ID_123", "Bright pixel ID [27] (S23)") },
        { 0x00FC, ("PIXEL_ID_124", "Bright pixel ID [28] (S23)") },
        { 0x00FD, ("PIXEL_ID_125", "Bright pixel ID [29] (S23)") },
        { 0x00FE, ("PIXEL_ID_126", "Bright pixel ID [30] (S23)") },
        { 0x00FF, ("PIXEL_ID_127", "Bright pixel ID [31] (S23)") },
        };

        // EEPROM configuration shadow blocks (0x0100-0x17FF, 0x40-byte stepping as seen in traces)
        for (ushort addr = 0x0100; addr <= 0x17C0; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x0100) / 0x40;
                regs[addr] = ($"EEPROM_BLOCK_{blockIndex:D2}", "EEPROM configuration shadow block");
            }
        }

        // FPGA table/image blocks (0x2000-0x61FF, 0x40-byte stepping as seen in traces)
        for (ushort addr = 0x2000; addr <= 0x61C0; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x2000) / 0x40;
                regs[addr] = ($"FPGA_BLOCK_{blockIndex:D3}", "FPGA data/LUT block");
            }
        }

        // Special FPGA address observed in startup traces.
        regs[0x61B2] = ("FPGA_ID_61B2", "FPGA/module identification data (undocumented)");

        return regs;
    }
}
