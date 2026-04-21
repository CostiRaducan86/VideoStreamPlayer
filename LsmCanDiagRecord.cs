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
    public byte[] RawPayload { get; init; } = Array.Empty<byte>();
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

    /// <summary>All register values decoded from RawPayload (address, value pairs).</summary>
    public (ushort Address, ushort Value)[] DecodedRegisters
    {
        get
        {
            // CAN raw frames have CAN data bytes, not UART register payload
            if (IsCanRawFrame)
                return Array.Empty<(ushort, ushort)>();

            if (RawLength < 4 || RawPayload.Length < 4)
                return Array.Empty<(ushort, ushort)>();

            // UART frame: [SYNC0][SYNC1][HCTRL][HADR][data pairs...][CRC16(2B)]
            // Data starts at byte 4; each register is 2 bytes MSB first; CRC-16 is 2 bytes at end
            int dataStart = 4;
            int dataEnd = RawLength - 2; // exclude CRC-16 (2 bytes)
            if (dataEnd <= dataStart)
                return Array.Empty<(ushort, ushort)>();

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
    }

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