using System.Collections.Generic;

namespace VilsSharpX;

/// <summary>
/// OSRAM (KEWGBXXD1U) device profile - 320x80 pixels.
/// Register names match classic VILS Monitor screenshots.
/// ELEDER pixel/state blocks, OSHRS and EEPROM shadow blocks included.
/// </summary>
public sealed class OsramProfile : LsmDeviceProfile
{
    public static readonly OsramProfile Instance = new();
    private static readonly Dictionary<ushort, (string Name, string Description)> s_registers = BuildRegisterDictionary();

    private OsramProfile() { }

    public override LsmDeviceType DeviceType => LsmDeviceType.Osram20;
    public override string DisplayName => "OSRAM";
    public override int MaxPixelsX => 320;
    public override int MaxPixelsY => 80;

    protected override Dictionary<string, (ushort Min, ushort Max)> AddressSpaces => new()
    {
        { "ASIC",   (0x0000, 0x00FF) },
        { "EEPROM", (0x0100, 0x17FF) },
        { "FPGA",   (0x2000, 0x61FF) }
    };

    protected override Dictionary<ushort, (string Name, string Description)> RegisterDictionary => s_registers;

    private static Dictionary<ushort, (string Name, string Description)> BuildRegisterDictionary()
    {
        var regs = new Dictionary<ushort, (string Name, string Description)>
        {
            // ─── ASIC control interface registers (section 6.2.18, canonical naming) ───────
            { 0x0000, ("CR",            "Configuration register") },
            { 0x0001, ("SR",            "System state register") },
            { 0x0002, ("MSMR",          "Master state machine register") },
            { 0x0003, ("SSR",           "System status register") },
            { 0x0004, ("RATO",          "Register access timeout") },
            { 0x0005, ("-----",         "-----") },
            { 0x0006, ("HWSTAT",        "ASIC Hardware Status Register") },
            { 0x0007, ("NVMSTAT",       "NVM Status") },
            { 0x0008, ("NVMPTRH",       "NVM pointer high word") },
            { 0x0009, ("NVMPTRL",       "NVM pointer low word") },
            { 0x000A, ("NVMAC",         "NVM accept register") },
            { 0x000B, ("NVMTR",         "NVM training pattern register") },
            { 0x000C, ("NVMCR",         "NVM control register") },
            { 0x000D, ("-----",         "-----") },
            { 0x000E, ("FWC",           "Frame watchdog/error counter") },
            { 0x000F, ("FWCT",          "Frame watchdog/error threshold") },
            { 0x0010, ("FCR0",          "Frame counter low word") },
            { 0x0011, ("FCR1",          "Frame counter high word") },
            { 0x0012, ("FEC",           "Frame error counter") },
            { 0x0013, ("FIFCTRL",       "Frame interface control") },
            { 0x0014, ("FCTRL",         "Frame control register") },
            { 0x0015, ("TSTDR",         "Temperature start derating register") },
            { 0x0016, ("TSPDR",         "Temperature stop derating register") },
            { 0x0017, ("MBDR",          "Minimum brightness derating register") },
            { 0x0018, ("DTR",           "Derating time register") },
            { 0x0019, ("ADR",           "Actual derating register") },
            { 0x001A, ("MB",            "Max brightness register") },
            { 0x001B, ("-----",         "-----") },
            { 0x001C, ("CURR",          "Current setting register") },
            { 0x001D, ("-----",         "-----") },
            { 0x001E, ("FRT",           "Frame time register") },
            { 0x001F, ("PWMCD",         "PWM current dimming register") },
            { 0x0020, ("FSTXR",         "Frame start X register") },
            { 0x0021, ("FSTYR",         "Frame start Y register") },
            { 0x0022, ("TSTIM",         "Test image register") },
            { 0x0023, ("DLSEL",         "Debug LED selector register") },
            { 0x0024, ("-----",         "-----") },
            { 0x0025, ("-----",         "-----") },
            { 0x0026, ("HWSET",         "ASIC hardware setting register") },
            { 0x0027, ("-----",         "-----") },
            { 0x0028, ("-----",         "-----") },
            { 0x0029, ("-----",         "-----") },
            { 0x002A, ("OTPID0",        "EVIYOS ID DOMAIN 0") },
            { 0x002B, ("OTPID1",        "EVIYOS ID DOMAIN 1") },
            { 0x002C, ("EGSET",         "EVIYOS global setting register") },
            { 0x002D, ("EGSTAT",        "EVIYOS global status register") },
            { 0x002E, ("EVDDPT",        "EVIYOS VDDP threshold register") },
            { 0x002F, ("ETEMPT",        "EVIYOS temperature warning threshold register") },
            { 0x003A, ("ETEMPM0",       "LED temperature maximum 0") },
            { 0x003B, ("ETEMPM1",       "LED temperature maximum 1") },
            // ─── OSRAM hardware / reset ───────────────────────────────────────────────────
            { 0x00F7, ("EV_GSTAT0",  "EVIYOS GSTATUS 0") },
            { 0x00F8, ("EV_GSTAT1",  "EVIYOS GSTATUS 1") },
            { 0x00F9, ("TMST0",      "Timestamp word 0 register") },
            { 0x00FA, ("TMST1",      "Timestamp word 1 register") },
            { 0x00FB, ("OSHRS",      "OS Hidden Register Setting") },
            { 0x00FC, ("OSHRA",      "OS Hidden Register Accept") },
            { 0x00FD, ("-----",         "-----") },
            { 0x00FE, ("HDL_ID",     "HDL ID register") },
            { 0x00FF, ("NTB",        "Notebook register") },
        };

        for (int i = 0; i < 10; i++)
        {
            regs[(ushort)(0x0030 + i)] = ($"EVDDP{i}", $"VDDP VOLTAGE SEG{i}");
            regs[(ushort)(0x004A + i)] = ($"ELSEL{i}", $"LED SEL SEG{i}");
            regs[(ushort)(0x0054 + i)] = ($"ELDIAG{i}", $"LED DIAG SEG{i}");
        }

        for (ushort addr = 0x003C; addr <= 0x0049; addr++)
            regs[addr] = ("-----", "-----");

        for (ushort addr = 0x005E; addr <= 0x006F; addr++)
            regs[addr] = ("-----", "-----");

        for (int i = 0; i < 64; i++)
        {
            regs[(ushort)(0x0070 + i)] = ($"ELEDERP{i}", $"LED ERROR POSITION {i}");
            regs[(ushort)(0x00B0 + i)] = ($"ELEDERS{i}", $"LED ERROR STATUS {i}");
        }

        for (ushort addr = 0x00F0; addr <= 0x00F6; addr++)
            regs[addr] = ("-----", "-----");

        for (int i = 0; i < 128; i++)
            regs[(ushort)(0x0100 + i)] = ($"NVMDAT{i}", $"NVM data register {i}");

        // Fill any remaining ASIC register addresses with explicit OSRAM placeholders.
        // This avoids generic fallback names and keeps interpretation device-specific.
        for (ushort addr = 0x0000; addr <= 0x00FF; addr++)
        {
            if (!regs.ContainsKey(addr))
                regs[addr] = ($"OSRAM_ASIC_{addr:X4}", "Reserved/undocumented in current OSRAM mapping documents");
        }

        // ─── EEPROM/NVM shadow blocks (0x0100–0x17FF) ───────────────────────────────────
        // OSRAM runtime UI expects NVMDAT naming for 0x0100..0x0170 and NVM_DATA for higher pages.
        for (ushort addr = 0x0180; addr <= 0x17F0; addr += 0x10)
        {
            if (!regs.ContainsKey(addr))
            {
                int blockIndex = (addr - 0x0100) / 0x10;
                if (addr <= 0x0170)
                {
                    int nvmdatBase = blockIndex * 16;
                    regs[addr] = ($"NVMDAT{nvmdatBase}", "NVM shadow data block");
                }
                else
                {
                    regs[addr] = ($"NVM_DATA_BLOCK_{blockIndex:D2}", "NVM/EEPROM shadow data block");
                }
            }
        }

        return regs;
    }
}
