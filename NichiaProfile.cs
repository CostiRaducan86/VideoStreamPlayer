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

    /// <summary>
    /// Nichia/TLD816K selects the address space with the UART FUN field, not the numeric
    /// address range: FUN 4/5 = ASIC (1-byte address), FUN 6/7 = EEPROM (2-byte offset).
    /// The same numeric value means different things per space (e.g. 0x80 = ASIC PIXEL_ID_0
    /// vs EEPROM offset 0x0080 = Gamma LUT). See docs 13_Nichia_Control_UART_Frame_And_CRC.md.
    /// </summary>
    public override string GetMemoryType(ushort address, bool isEepromAccess)
        => isEepromAccess ? "EEPROM" : "ASIC";

    public override (string Name, string Description) ResolveRegister(ushort address, bool isEepromAccess)
    {
        if (!isEepromAccess)
        {
            // ASIC space: only the 0x0000..0x00FF register file is valid.
            return s_registers.TryGetValue(address, out var reg) ? reg : ("Unknown", "");
        }

        return ResolveEepromOffset(address);
    }

    /// <summary>
    /// Name/description for an external EEPROM image offset (2-byte address, FUN 6/7).
    /// Regions follow the TLD816K C11 EEPROM map; blocks are named per 64-byte (0x40) chunk.
    /// </summary>
    private static (string Name, string Description) ResolveEepromOffset(ushort off)
    {
        if (off <= 0x0048)
            return ($"EEPROM_CFG_{off:X4}", "EEPROM TLD816K configuration shadow (loaded at start-up)");
        if (off <= 0x018A)
            return ($"EEPROM_GAMMA_LUT_{(off - 0x0049) / 0x40:D2}", "EEPROM Gamma correction LUT block");
        if (off <= 0x21AA)
            return ($"EEPROM_FS_PICTURE_{(off - 0x018B) / 0x40:D3}", "EEPROM failsafe picture block");
        if (off <= 0x61B9)
            return ($"EEPROM_CAL_MOD_{(off - 0x21AB) / 0x40:D3}", "EEPROM calibration module block");
        if (off <= 0x623F)
            return ($"EEPROM_TRACE_{(off - 0x61BA) / 0x40:D2}", "EEPROM traceability data block");
        return ($"EEPROM_USER_{(off - 0x6240) / 0x40:D3}", "EEPROM user / OEM data block");
    }

    private static Dictionary<ushort, (string Name, string Description)> BuildRegisterDictionary()
    {
        var regs = new Dictionary<ushort, (string Name, string Description)>
        {
        // ASIC Registers (0x0000–0x00FF) - Nichia TLD816K specific
        { 0x00, ("CFG_DEVICE", "Device configuration") },
        { 0x01, ("IPU_CON", "Image Processing Unit configuration") },
        { 0x02, ("CGU_CFG0", "Clock generation control 0") },
        { 0x03, ("CGU_CFG1", "Clock generation control 1") },
        { 0x04, ("CGU_CFG2", "Clock generation control 2") },
        { 0x05, ("CIF_CON", "Control interface configuration") },
        { 0x06, ("CIF_CHK_CON", "Control interface check control") },
        { 0x07, ("CIF_UART_TIMING_CFG", "Control UART Timing configuration register") },
        { 0x08, ("CIF_ADDR_CFG", "Control interface address configuration") },
        { 0x09, ("OVT_CFG", "Over temperature configuration") },
        { 0x0A, ("STD_DIAG", "Standard diagnostic response") },
        { 0x0B, ("IF_MON", "Interface overall status flags") },
        { 0x0C, ("CIF_STATUS", "Control interface status") },
        { 0x0D, ("IPU_STATUS", "IPU status flag") },
        { 0x0E, ("CGU_STATUS", "Clock generation unit status flags") },
        { 0x0F, ("ADC_FLAG", "ADC monitoring flags") },
        { 0x10, ("OVT_MON", "Over Temperature Monitor") },
        { 0x11, ("uPLS_ID_0", "Unique μPLS identifier 0") },
        { 0x12, ("uPLS_ID_1", "Unique μPLS identifier 1") },
        { 0x13, ("uPLS_ID_2", "Unique μPLS identifier 2") },
        { 0x14, ("EEPROM_CFG", "External eeprom configuration") },
        { 0x15, ("VIF_FAIL_SAFE_WRITE_CH", "Fail safe image write channel") },
        { 0x16, ("VIF_CON", "Video interface configuration") },
        { 0x17, ("VIF_CHK_CON", "Video interface check control") },
        { 0x18, ("VIF_RGB_VSYNC_CFG", "Video RGB Vertical sync configuration") },
        { 0x19, ("VIF_RGB_VBP_CFG", "Video RGB vertical back porch configuration") },
        { 0x1A, ("VIF_RGB_VFP_CFG", "Video RGB vertical front porch configuration") },
        { 0x1B, ("VIF_RGB_HSYNC_CFG", "Video RGB horizontal sync configuration") },
        { 0x1C, ("VIF_RGB_HBP_CFG", "Video RGB horizontal back porch configuration") },
        { 0x1D, ("VIF_RGB_HFP_CFG", "Video RGB horizontal front porch configuration") },
        { 0x1E, ("VIF_UART_CFG", "Video UART interface configuration") },
        { 0x1F, ("VIF_STATUS", "Video interface status") },
        { 0x20, ("VIF_IN_LATCHES", "Video interface input latches status") },
        { 0x21, ("SEGMENT01_CFG", "Output current setting segment 0 and 1") },
        { 0x22, ("SEGMENT23_CFG", "Output current setting segment 2 and 3") },
        { 0x23, ("ADC_CFG", "ADC LED diagnosis configuration") },
        { 0x24, ("ADC_SINGLE_CONV", "Pixel-ID selection for ADC single conversion") },
        { 0x25, ("ADC_VDDP_MON_THS", "LED diagnosis enable threshold") },
        { 0x26, ("ADC_LED_DIAG_DB_H_THS", "LED diagnosis for dark and bright failures high threshold") },
        { 0x27, ("ADC_LED_DIAG_DB_L_THS", "LED diagnosis for dark and bright failures low threshold") },
        { 0x28, ("SEGMENT0_ADC_OVR_THS", "Overrun threshold on segment 0") },
        { 0x29, ("SEGMENT1_ADC_OVR_THS", "Overrun threshold on segment 1") },
        { 0x2A, ("SEGMENT2_ADC_OVR_THS", "Overrun threshold on segment 2") },
        { 0x2B, ("SEGMENT3_ADC_OVR_THS", "Overrun threshold on segment 3") },
        { 0x2C, ("SEGMENT0_ADC_UDR_THS", "Underrun threshold on segment 0") },
        { 0x2D, ("SEGMENT1_ADC_UDR_THS", "Underrun threshold on segment 1") },
        { 0x2E, ("SEGMENT2_ADC_UDR_THS", "Underrun threshold on segment 2") },
        { 0x2F, ("SEGMENT3_ADC_UDR_THS", "Underrun threshold on segment 3") },
        { 0x30, ("SEGMENT0_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 0") },
        { 0x31, ("SEGMENT1_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 1") },
        { 0x32, ("SEGMENT2_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 2") },
        { 0x33, ("SEGMENT3_ADC_VDDP", "Diagnostic VDDP Voltage digital conversion on segment 3") },
        { 0x34, ("SEGMENT0_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 0") },
        { 0x35, ("SEGMENT1_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 1") },
        { 0x36, ("SEGMENT2_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 2") },
        { 0x37, ("SEGMENT3_ADC_VLED", "Diagnostic LED Forward Voltage digital conversion on segment 3") },
        { 0x38, ("SEGMENT0_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 0") },
        { 0x39, ("SEGMENT1_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 1") },
        { 0x3A, ("SEGMENT2_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 2") },
        { 0x3B, ("SEGMENT3_ADC_VPS", "Diagnostic Current Source Drop Voltage digital conversion on segment 3") },
        { 0x3C, ("SEGMENT0_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 0") },
        { 0x3D, ("SEGMENT1_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 1") },
        { 0x3E, ("SEGMENT2_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 2") },
        { 0x3F, ("SEGMENT3_ADC_VPS_AVG", "Average of the diagnostic Current Source Drop Voltagedigital conversion on segment 3") },
        { 0x40, ("SEGMENT0_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 0") },
        { 0x41, ("SEGMENT1_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 1") },
        { 0x42, ("SEGMENT2_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 2") },
        { 0x43, ("SEGMENT3_ADC_VLED_MAX", "Diagnostic LED Forward Voltage digital conversion maximum on segment 3") },
        { 0x44, ("SEGMENT0_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 0") },
        { 0x45, ("SEGMENT1_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 1") },
        { 0x46, ("SEGMENT2_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 2") },
        { 0x47, ("SEGMENT3_ADC_VPS_MAX", "Diagnostic Current Source Drop Voltage digital conversion maximum on segment 3") },
        { 0x48, ("SEGMENT0_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 0") },
        { 0x49, ("SEGMENT1_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 1") },
        { 0x4A, ("SEGMENT2_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 2") },
        { 0x4B, ("SEGMENT3_ADC_VLED_MIN", "Diagnostic LED Forward Voltage digital conversion minimum on segment 3") },
        { 0x4C, ("SEGMENT0_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 0") },
        { 0x4D, ("SEGMENT1_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 1") },
        { 0x4E, ("SEGMENT2_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 2") },
        { 0x4F, ("SEGMENT3_ADC_VPS_MIN", "Diagnostic Current Source Drop Voltage digital conversion minimum on segment 3") },
        { 0x50, ("SEGMENT0_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 0") },
        { 0x51, ("SEGMENT1_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 1") },
        { 0x52, ("SEGMENT2_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 2") },
        { 0x53, ("SEGMENT3_ADC_ADDR_VLED_MAX", "Pixel address of diagnostic LED Forward Voltage digital conversion maximum on segment 3") },
        { 0x54, ("SEGMENT0_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 0") },
        { 0x55, ("SEGMENT1_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 1") },
        { 0x56, ("SEGMENT2_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 2") },
        { 0x57, ("SEGMENT3_ADC_ADDR_VLED_MIN", "Pixel address of diagnostic LED Forward Voltage digital conversion minimum on segment 3") },
        { 0x58, ("SEGMENT0_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 0") },
        { 0x59, ("SEGMENT1_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 1") },
        { 0x5A, ("SEGMENT2_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 2") },
        { 0x5B, ("SEGMENT3_ADC_ADDR_VPS_MAX", "Pixel address of diagnostic Current Source Drop Voltage digital conversion maximum on segment 3") },
        { 0x5C, ("SEGMENT0_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 0") },
        { 0x5D, ("SEGMENT1_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 1") },
        { 0x5E, ("SEGMENT2_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 2") },
        { 0x5F, ("SEGMENT3_ADC_ADDR_VPS_MIN", "Pixel address of diagnostic Current Source Drop Voltage digital conversion minimum on segment 3") },
        { 0x60, ("TEMP_SENSOR_CFG", "Temperature measurement unit configuration") },
        { 0x61, ("TEMP_OVT_SD", "Programmable overtemperature shutdown threshold configuration") },
        { 0x62, ("TEMP_OVT_WARN", "Programmable overtemperature warning threshold configuration") },
        { 0x63, ("SEGMENT01_TEMP_MAX", "Maximum temperature measurement on segment 0 and 1") },
        { 0x64, ("SEGMENT23_TEMP_MAX", "Maximum temperature measurement on segment 2 and 3") },
        { 0x65, ("SEGMENT01_TEMP_MIN", "Minimum temperature measurement on segment 0 and 1") },
        { 0x66, ("SEGMENT23_TEMP_MIN", "Minimum temperature measurement on segment 2 and 3") },
        { 0x67, ("TEMP_SENSOR_SELECTED", "Temperature measurement on selected sensor") },
        { 0x68, ("SEGMENT0_TEMP_MAX", "Maximum temperature measurement on segment 0") },
        { 0x69, ("SEGMENT0_TEMP_MIN", "Minimum temperature measurement on segment 0") },
        { 0x6A, ("SEGMENT1_TEMP_MAX", "Maximum temperature measurement on segment 1") },
        { 0x6B, ("SEGMENT1_TEMP_MIN", "Minimum temperature measurement on segment 1") },
        { 0x6C, ("SEGMENT2_TEMP_MAX", "Maximum temperature measurement on segment 2") },
        { 0x6D, ("SEGMENT2_TEMP_MIN", "Minimum temperature measurement on segment 2") },
        { 0x6E, ("SEGMENT3_TEMP_MAX", "Maximum temperature measurement on segment 3") },
        { 0x6F, ("SEGMENT3_TEMP_MIN", "Minimum temperature measurement on segment 3") },
        { 0x70, ("LED_DIAG_BRIGHT_FAIL_N", "Number of bright failures") },
        { 0x71, ("LED_DIAG_DARK_FAIL_N", "Number of dark failures") },
        { 0x7B, ("LUT_RAM_WRITE", "Write gamma correction LUT in RAM") },
        { 0x7C, ("LUT_RAM_READ", "Read gamma correction LUT from RAM") },
        { 0x7D, ("LUT_CRC_REG", "LUT CRC calculated data") },
        { 0x7E, ("LUT_RAM_ACCESS_REG", "RAM access address pointer") },
        { 0x7F, ("LUT_RAM_STATUS_REG", "RAM access address pointer status") },
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

        return regs;
    }
}
