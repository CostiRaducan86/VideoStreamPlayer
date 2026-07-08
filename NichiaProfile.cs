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
        { "ASIC", (0x00, 0xFF) },
        { "EEPROM", (0x0049, 0x623F) },
        { "FPGA", (0x6240, 0x7FFD) }
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
        { 0x0005, ("CIF_CON", "Control interface configuration") },
        { 0x0006, ("CIF_CHK_CON", "Control interface check control") },
        { 0x0007, ("CIF_UART_TIMING_CFG", "Control UART Timing configuration register") },
        { 0x0008, ("CIF_ADDR_CFG", "Control interface address configuration") },
        { 0x0009, ("OVT_CFG", "Over temperature configuration") },
        { 0x000A, ("STD_DIAG", "Standard diagnostic response") },
        { 0x000B, ("IF_MON", "Interface overall status flags") },
        { 0x000C, ("CIF_STATUS", "Control interface status") },
        { 0x000D, ("IPU_STATUS", "IPU status flag") },
        { 0x000E, ("CGU_STATUS", "Clock generation unit status flags") },
        { 0x000F, ("ADC_FLAG", "ADC monitoring flags") },
        { 0x0010, ("OVT_MON", "Over Temperature Monitor") },
        { 0x0011, ("uPLS_ID_0", "Unique μPLS identifier 0") },
        { 0x0012, ("uPLS_ID_1", "Unique μPLS identifier 1") },
        { 0x0013, ("uPLS_ID_2", "Unique μPLS identifier 2") },
        { 0x0014, ("EEPROM_CFG", "External eeprom configuration") },
        { 0x0015, ("VIF_FAIL_SAFE_WRITE_CH", "Fail safe image write channel") },
        { 0x0016, ("VIF_CON", "Video interface configuration") },
        { 0x0017, ("VIF_CHK_CON", "Video interface check control") },
        { 0x0018, ("VIF_RGB_VSYNC_CFG", "Video RGB Vertical sync configuration") },
        { 0x0019, ("VIF_RGB_VBP_CFG", "Video RGB vertical back porch configuration") },
        { 0x001A, ("VIF_RGB_VFP_CFG", "Video RGB vertical front porch configuration") },
        { 0x001B, ("VIF_RGB_HSYNC_CFG", "Video RGB horizontal sync configuration") },
        { 0x001C, ("VIF_RGB_HBP_CFG", "Video RGB horizontal back porch configuration") },
        { 0x001D, ("VIF_RGB_HFP_CFG", "Video RGB horizontal front porch configuration") },
        { 0x001E, ("VIF_UART_CFG", "Video UART interface configuration") },
        { 0x001F, ("VIF_STATUS", "Video interface status") },
        { 0x0020, ("VIF_IN_LATCHES", "Video interface input latches status") },
        { 0x0021, ("SEGMENT01_CFG", "Output current setting segment 0 and 1") },
        { 0x0022, ("SEGMENT23_CFG", "Output current setting segment 2 and 3") },
        { 0x0023, ("ADC_CFG", "ADC LED diagnosis configuration") },
        { 0x0024, ("ADC_SINGLE_CONV", "Pixel-ID selection for ADC single conversion") },
        { 0x0025, ("ADC_VDDP_MON_THS", "LED diagnosis enable threshold") },
        { 0x0026, ("ADC_LED_DIAG_DB_H_THS", "LED diagnosis for dark and bright failures high threshold") },
        { 0x0027, ("ADC_LED_DIAG_DB_L_THS", "LED diagnosis for dark and bright failures low threshold") },
        { 0x0028, ("SEGMENT0_ADC_OVR_THS", "Overrun threshold on segment 0") },
        { 0x0029, ("SEGMENT1_ADC_OVR_THS", "Overrun threshold on segment 1") },
        { 0x002A, ("SEGMENT2_ADC_OVR_THS", "Overrun threshold on segment 2") },
        { 0x002B, ("SEGMENT3_ADC_OVR_THS", "Overrun threshold on segment 3") },
        { 0x002C, ("SEGMENT0_ADC_UDR_THS", "Underrun threshold on segment 0") },
        { 0x002D, ("SEGMENT1_ADC_UDR_THS", "Underrun threshold on segment 1") },
        { 0x002E, ("SEGMENT2_ADC_UDR_THS", "Underrun threshold on segment 2") },
        { 0x002F, ("SEGMENT3_ADC_UDR_THS", "Underrun threshold on segment 3") },
        { 0x0030, ("SEGMENT0_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 0") },
        { 0x0031, ("SEGMENT1_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 1") },
        { 0x0032, ("SEGMENT2_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 2") },
        { 0x0033, ("SEGMENT3_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 3") },
        { 0x0034, ("SEGMENT0_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 0") },
        { 0x0035, ("SEGMENT1_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 1") },
        { 0x0036, ("SEGMENT2_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 2") },
        { 0x0037, ("SEGMENT3_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 3") },
        { 0x0038, ("SEGMENT0_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 0") },
        { 0x0039, ("SEGMENT1_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 1") },
        { 0x003A, ("SEGMENT2_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 2") },
        { 0x003B, ("SEGMENT3_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 3") },
        { 0x003C, ("SEGMENT0_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 0") },
        { 0x003D, ("SEGMENT1_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 1") },
        { 0x003E, ("SEGMENT2_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 2") },
        { 0x003F, ("SEGMENT3_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 3") },
        { 0x0040, ("SEGMENT0_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 0") },
        { 0x0041, ("SEGMENT1_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 1") },
        { 0x0042, ("SEGMENT2_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 2") },
        { 0x0043, ("SEGMENT3_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 3") },
        { 0x0044, ("SEGMENT0_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 0") },
        { 0x0045, ("SEGMENT1_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 1") },
        { 0x0046, ("SEGMENT2_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 2") },
        { 0x0047, ("SEGMENT3_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 3") },
        { 0x0048, ("SEGMENT0_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 0") },
        { 0x0049, ("SEGMENT1_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 1") },
        { 0x004A, ("SEGMENT2_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 2") },
        { 0x004B, ("SEGMENT3_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 3") },
        { 0x004C, ("SEGMENT0_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 0") },
        { 0x004D, ("SEGMENT1_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 1") },
        { 0x004E, ("SEGMENT2_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 2") },
        { 0x004F, ("SEGMENT3_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 3") },
        { 0x0050, ("SEGMENT0_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 0") },
        { 0x0051, ("SEGMENT1_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 1") },
        { 0x0052, ("SEGMENT2_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 2") },
        { 0x0053, ("SEGMENT3_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 3") },
        { 0x0054, ("SEGMENT0_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 0") },
        { 0x0055, ("SEGMENT1_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 1") },
        { 0x0056, ("SEGMENT2_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 2") },
        { 0x0057, ("SEGMENT3_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 3") },
        { 0x0058, ("SEGMENT0_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 0") },
        { 0x0059, ("SEGMENT1_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 1") },
        { 0x005A, ("SEGMENT2_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 2") },
        { 0x005B, ("SEGMENT3_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 3") },
        { 0x005C, ("SEGMENT0_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 0") },
        { 0x005D, ("SEGMENT1_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 1") },
        { 0x005E, ("SEGMENT2_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 2") },
        { 0x005F, ("SEGMENT3_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 3") },
        { 0x0060, ("TEMP_SENSOR_CFG", "Temperature measurement unit configuration") },
        { 0x0061, ("TEMP_OVT_SD", "Programmable overtemperature shutdown threshold configuration") },
        { 0x0062, ("TEMP_OVT_WARN", "Programmable overtemperature warning threshold configuration") },
        { 0x0063, ("SEGMENT01_TEMP_MAX", "Maximum temperature measurement on segment 0 and 1") },
        { 0x0064, ("SEGMENT23_TEMP_MAX", "Maximum temperature measurement on segment 2 and 3") },
        { 0x0065, ("SEGMENT01_TEMP_MIN", "Minimum temperature measurement on segment 0 and 1") },
        { 0x0066, ("SEGMENT23_TEMP_MIN", "Minimum temperature measurement on segment 2 and 3") },
        { 0x0067, ("TEMP_SENSOR_SELECTED", "Temperature measurement on selected sensor") },
        { 0x0068, ("SEGMENT0_TEMP_MAX", "Maximum temperature measurement on segment 0") },
        { 0x0069, ("SEGMENT0_TEMP_MIN", "Minimum temperature measurement on segment 0") },
        { 0x006A, ("SEGMENT1_TEMP_MAX", "Maximum temperature measurement on segment 1") },
        { 0x006B, ("SEGMENT1_TEMP_MIN", "Minimum temperature measurement on segment 1") },
        { 0x006C, ("SEGMENT2_TEMP_MAX", "Maximum temperature measurement on segment 2") },
        { 0x006D, ("SEGMENT2_TEMP_MIN", "Minimum temperature measurement on segment 2") },
        { 0x006E, ("SEGMENT3_TEMP_MAX", "Maximum temperature measurement on segment 3") },
        { 0x006F, ("SEGMENT3_TEMP_MIN", "Minimum temperature measurement on segment 3") },
        { 0x0070, ("LED_DIAG_BRIGHT_FAIL_N", "Number of bright failures") },
        { 0x0071, ("LED_DIAG_DARK_FAIL_N", "Number of dark failures") },
        { 0x007B, ("LUT_RAM_WRITE", "Write gamma correction LUT in RAM") },
        { 0x007C, ("LUT_RAM_READ", "Read gamma correction LUT from RAM") },
        { 0x007D, ("LUT_CRC_REG", "LUT CRC calculated data") },
        { 0x007E, ("LUT_RAM_ACCESS_REG", "RAM access address pointer") },
        { 0x007F, ("LUT_RAM_STATUS_REG", "RAM access address pointer status") },
        };

        // Storage of the addresses of faulty channels
        // Pixel ID storage for dark failures (segment pair 0/1)
        for (int i = 0; i < 32; i++)
        {
            regs[(ushort)(0x80 + i)] = ($"PIXEL_ID_{i}", $"Dark pixel ID [{i}] (S01)");
        }

        // Pixel ID storage for bright failures (segment pair 0/1)
        for (int i = 0; i < 32; i++)
        {
            regs[(ushort)(0xA0 + i)] = ($"PIXEL_ID_{32 + i}", $"Bright pixel ID [{i}] (S01)");
        }

        // Pixel ID storage for dark failures (segment pair 2/3)
        for (int i = 0; i < 32; i++)
        {
            regs[(ushort)(0xC0 + i)] = ($"PIXEL_ID_{64 + i}", $"Dark pixel ID [{i}] (S23)");
        }

        // Pixel ID storage for bright failures (segment pair 2/3)
        for (int i = 0; i < 32; i++)
        {
            regs[(ushort)(0xE0 + i)] = ($"PIXEL_ID_{96 + i}", $"Bright pixel ID [{i}] (S23)");
        }


        // EEPROM configuration blocks (0x0049-0x623F, 0x40-byte stepping as seen in traces)
        // Gamma correction LUT
        for (ushort addr = 0x0049; addr <= 0x018A; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x0049) / 0x40;
                regs[addr] = ($"EEPROM_BLOCK_{blockIndex:D2}", "EEPROM Gamma correction LUT block");
            }
        }

        // FS picture
        for (ushort addr = 0x018B; addr <= 0x21AA; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x018B) / 0x40;
                regs[addr] = ($"EEPROM_BLOCK_{blockIndex:D2}", "EEPROM FS picture block");
            }
        }

        // Calibration module
        for (ushort addr = 0x21AB; addr <= 0x61B9; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x21AB) / 0x40;
                regs[addr] = ($"EEPROM_BLOCK_{blockIndex:D2}", "EEPROM Calibration module block");
            }
        }

        // Traceability data
        for (ushort addr = 0x61BA; addr <= 0x623F; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x61BA) / 0x40;
                regs[addr] = ($"EEPROM_BLOCK_{blockIndex:D2}", "EEPROM Traceability data block");
            }
        }

        // FPGA table/image blocks (0x2000-0x61FF, 0x40-byte stepping as seen in traces)
        for (ushort addr = 0x6240; addr <= 0x7FFD; addr += 0x40)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x6240) / 0x40;
                regs[addr] = ($"FPGA_BLOCK_{blockIndex:D3}", "FPGA data block");
            }
        }

        return regs;
    }
}
