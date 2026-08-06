using System;

namespace VilsSharpX;

public enum LsmCanDiagOperation : byte
{
    Read = 0,
    Write = 1,
    CanRaw = 2,
}

public enum LsmCanDiagStatus : byte
{
    Ok = 0,
    Timeout = 1,
    CrcMismatch = 2,
    Malformed = 3,
    Unsupported = 255,
}

public sealed class LsmCanDiagRecord
{
    public const ushort EtherType = 0x88B5;
    public const ushort Magic = 0x4344;
    public const byte ProtocolVersion = 2;
    public const byte RegisterIoRecordType = 1;
    public const int HeaderLength = 8;
    public const int PayloadFixed = 22;  // ts(4)+addr(2)+rspDly(2)+ifDly(2)+val(4)+crc(4)+devId(1)+op(1)+status(1)+rawLen(1)
    public const int PayloadRawMax = 72;
    public const int PayloadLength = PayloadFixed + PayloadRawMax;  // 94

    public ushort Sequence { get; init; }
    public byte RecordType { get; init; }
    public uint SourceTimestamp { get; init; }
    public ushort Address { get; init; }
    public ushort ResponseDelayUs { get; init; }
    public ushort InterFrameDelayUs { get; init; }
    public uint Value { get; init; }
    public uint Checksum { get; init; }
    public byte DeviceId { get; init; }
    public LsmCanDiagOperation Operation { get; init; }
    public LsmCanDiagStatus Status { get; init; }
    public byte RawLength { get; init; }
    public byte[] RawPayload { get; init; } = [];
    public DateTime ReceivedUtc { get; init; }

    public string DeviceName => DeviceId switch
    {
        1 => "OSRAM",
        0 => "NICHIA",
        _ => $"DEV{DeviceId:X2}",
    };

    public string OperationName => Operation switch
    {
        LsmCanDiagOperation.Write => "W",
        LsmCanDiagOperation.Read => "R",
        LsmCanDiagOperation.CanRaw => "CAN",
        _ => "?",
    };

    /// <summary>CAN message ID extracted from Value field (bit 31 = extended flag).</summary>
    public uint CanId => Value & 0x1FFFFFFFu;
    public bool IsExtendedCanId => (Value & 0x80000000u) != 0;
    public bool IsCanRawFrame => Operation == LsmCanDiagOperation.CanRaw;

    /// <summary>
    /// True when this is a Nichia/TLD816K EEPROM-space access. The TLD816K UART FUN field
    /// selects the address space: FUN 4/5 = ASIC (1-byte address), FUN 6/7 = EEPROM (2-byte
    /// offset). Used to resolve the correct memory type / register name, because ASIC and
    /// EEPROM addresses overlap numerically (e.g. 0x80 = ASIC PIXEL_ID_0 vs EEPROM offset
    /// 0x0080 = Gamma LUT). See docs/13_Nichia_Control_UART_Frame_And_CRC.md.
    /// </summary>
    public bool IsNichiaEepromAccess
    {
        get
        {
            if (DeviceId != 0 || IsCanRawFrame) return false;
            if (RawPayload.Length < 3 || RawPayload[0] != 0x55) return false;
            byte fun = (byte)(RawPayload[2] & 0x07);
            return fun == 6 || fun == 7;
        }
    }

    /// <summary>
    /// CRC value for the diagnostics view. For Nichia the CRC8 is recomputed from the raw
    /// UART payload (poly 0x1D, seed 0xFF, xorout 0xFF, over address + data), because the
    /// firmware-provided <see cref="Checksum"/> is not persisted in replayed traces.
    /// FUN=7 (Read EEPROM) frames carry no CRC, so "N/A" is returned. Non-Nichia devices
    /// keep the legacy 16-bit checksum display.
    /// </summary>
    public string CrcDisplay
    {
        get
        {
            if (DeviceId == 0 && !IsCanRawFrame &&
                RawPayload.Length >= 3 && RawPayload[0] == 0x55)
            {
                byte dlcFun = RawPayload[2];
                byte fun = (byte)(dlcFun & 0x07);
                if (fun < 4 || fun > 7)
                    return "/";
                if (fun == 7)
                    return "N/A"; // Read EEPROM: no CRC byte per TLD816K datasheet §7.3.1.5

                int addrLen = (fun == 6) ? 2 : 1;
                int dataLen = NichiaDataLength((byte)((dlcFun >> 3) & 0x07));
                int spanStart = 3;                    // skip SYNC, master request, DLC/FUN
                int spanLen = addrLen + dataLen;      // CRC8 covers address + data
                int crcIdx = spanStart + spanLen;
                if (RawPayload.Length <= crcIdx || spanStart + spanLen > RawPayload.Length)
                    return "/";

                byte received = RawPayload[crcIdx];
                byte computed = Crc8Nichia(RawPayload.AsSpan(spanStart, spanLen));
                return received == computed
                    ? $"0x{received:X2}"
                    : $"0x{received:X2} (calc 0x{computed:X2})";
            }

            // Non-Nichia / fallback: legacy 16-bit checksum display.
            return $"0x{(Checksum & 0xFFFF):X4}";
        }
    }

    /// <summary>
    /// TLD816K control-UART CRC8 (CRC-8-AUTOSAR / SAE J1850): poly 0x1D, seed 0xFF,
    /// xorout 0xFF, no reflection. Verified against real trace frames.
    /// </summary>
    private static byte Crc8Nichia(ReadOnlySpan<byte> data)
    {
        byte crc = 0xFF;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int i = 0; i < 8; i++)
                crc = (byte)(((crc & 0x80) != 0) ? (crc << 1) ^ 0x1D : crc << 1);
        }
        return (byte)(crc ^ 0xFF);
    }

    /// <summary>All register values decoded from RawPayload (address, value pairs).</summary>
    public (ushort Address, ushort Value)[] DecodedRegisters
    {
        get
        {
            // CAN raw frames have CAN data bytes, not UART register payload
            if (IsCanRawFrame)
                return [];

            if (DeviceId == 0)
                return DecodeNichiaRegisters();

            return DecodeOsramRegisters();
        }
    }

    /// <summary>
    /// Calculate timestamp in microseconds combining SourceTimestamp (milliseconds) and InterFrameDelayUs.
    /// Result: SourceTimestamp_ms * 1000 + InterFrameDelayUs.
    /// Useful for high-precision timing reconstruction and display in microseconds.
    /// </summary>
    public long GetCalculatedTimestampMicroseconds()
    {
        return (long)SourceTimestamp * 1000 + InterFrameDelayUs;
    }

    private (ushort Address, ushort Value)[] DecodeOsramRegisters()
    {
        if (RawLength < 4 || RawPayload.Length < 4)
            return [];

        // UART frame: [SYNC0][SYNC1][HCTRL][HADR][data pairs...][CRC16(2B)]
        // Data starts at byte 4; each register is 2 bytes MSB first; CRC-16 is 2 bytes at end
        int dataStart = 4;
        int dataEnd = RawLength - 2; // exclude CRC-16 (2 bytes)
        if (dataEnd <= dataStart)
            return [];

        int count = (dataEnd - dataStart) / 2;
        var result = new (ushort, ushort)[count];
        ushort baseAddr = Address;
        for (int i = 0; i < count; i++)
        {
            int idx = dataStart + i * 2;
            if (idx + 1 >= RawPayload.Length)
                break;
            ushort val = (ushort)((RawPayload[idx] << 8) | RawPayload[idx + 1]);
            result[i] = ((ushort)(baseAddr + i), val);
        }
        return result;
    }

    private (ushort Address, ushort Value)[] DecodeNichiaRegisters()
    {
        if (RawLength < 4 || RawPayload.Length < 4 || RawPayload[0] != 0x55)
            return [];

        byte dlcFun = RawPayload[2];
        byte fun = (byte)(dlcFun & 0x07);
        byte dlc = (byte)((dlcFun >> 3) & 0x07);

        if ((dlcFun & 0xC0) != 0 || fun < 4 || fun > 7)
            return [];

        int addrLen = (fun == 6 || fun == 7) ? 2 : 1;
        int dataLen = NichiaDataLength(dlc);
        int dataStart = 3 + addrLen;
        bool hasCrc = fun != 7; // TLD816K READ_EEPROM responses carry data without CRC8.
        int minLength = dataStart + dataLen + (hasCrc ? 1 : 0);

        // Read requests are header+address only; emitted records should have data
        // and, except for READ_EEPROM responses, CRC8.
        if (RawLength < minLength || RawPayload.Length < minLength)
            return [];

        ushort baseAddr = addrLen == 2
            ? (ushort)((RawPayload[3] << 8) | RawPayload[4])
            : RawPayload[3];

        if (dataLen == 1)
            return [(baseAddr, RawPayload[dataStart])];

        int count = dataLen / 2;
        var result = new (ushort, ushort)[count];
        for (int i = 0; i < count; i++)
        {
            int idx = dataStart + i * 2;
            if (idx + 1 >= RawPayload.Length)
                break;

            ushort val = (ushort)((RawPayload[idx] << 8) | RawPayload[idx + 1]);
            result[i] = ((ushort)(baseAddr + i), val);
        }
        return result;
    }

    private static int NichiaDataLength(byte dlc) => dlc switch
    {
        0 => 1,
        1 => 2,
        2 => 4,
        3 => 8,
        4 => 16,
        5 => 24,
        6 => 32,
        _ => 64,
    };

    /// <summary>Raw payload as uppercase hex string.</summary>
    public string RawHex
    {
        get
        {
            if (IsCanRawFrame && RawLength > 0)
            {
                // CAN format: "ID=0x123 [8] AA BB CC DD EE FF 00 11"
                string idStr = IsExtendedCanId ? $"0x{CanId:X8}" : $"0x{CanId:X3}";
                string dataHex = BitConverter.ToString(RawPayload, 0, Math.Min(RawLength, RawPayload.Length)).Replace("-", " ");
                return $"ID={idStr} [{RawLength}] {dataHex}";
            }
            return RawLength > 0
                ? BitConverter.ToString(RawPayload, 0, Math.Min(RawLength, RawPayload.Length)).Replace("-", "")
                : $"{Value:X8}";
        }
    }
}
