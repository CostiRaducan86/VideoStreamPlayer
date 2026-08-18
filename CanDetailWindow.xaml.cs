using System;
using System.Globalization;
using System.Text;
using System.Windows;

namespace VilsSharpX;

public partial class CanDetailWindow : Window
{
    public CanDetailWindow(LsmCanDiagRecord record)
    {
        InitializeComponent();
        Populate(record);
    }

    private void Populate(LsmCanDiagRecord record)
    {
        if (record.IsCanRawFrame)
        {
            PopulateCanRaw(record);
            return;
        }

        bool isEep = record.IsNichiaEepromAccess;
        var (name, memType) = LsmRegisterMap.ResolveFromDeviceId(record.Address, record.DeviceId, isEep);
        string description = LsmRegisterMap.GetDescription(record.Address, record.DeviceId, isEep);

        TxtTitle.Text = name == "/" ? $"0x{record.Address:X4}" : name;

        // Timing
        TxtTime.Text = record.EffectiveDisplayTimestampUtc.ToLocalTime().ToString("HH:mm:ss.ffffff", CultureInfo.InvariantCulture);
        TxtUnixTs.Text = record.SourceTimestamp.ToString(CultureInfo.InvariantCulture);
        TxtResponseDelay.Text = $"{record.ResponseDelayUs} µs";
        TxtInterFrameDelay.Text = $"{record.InterFrameDelayUs} µs";

        // Identity
        TxtNr.Text = record.EffectiveDisplaySequence.ToString(CultureInfo.InvariantCulture);
        TxtName.Text = name;
        TxtAddress.Text = $"0x{record.Address:X4}";
        TxtMemoryType.Text = memType;
        TxtDevice.Text = $"0x{record.DeviceId:X2}";
        TxtRw.Text = record.OperationName;

        // Diagnostics — Nichia: CRC8 recomputed from raw payload (N/A for FUN=7 EEPROM read);
        // other devices keep the legacy 16-bit checksum display.
        TxtCrc.Text = record.CrcDisplay;
        TxtError.Text = record.Status == LsmCanDiagStatus.Ok ? "/" : record.Status.ToString();
        TxtDescription.Text = string.IsNullOrEmpty(description) ? "/" : description;

        // Data — Raw = full UART frame with 0x prefix; Value = register data only
        TxtRaw.Text = record.RawLength > 0
            ? "0x" + record.RawHex
            : $"0x{record.Value:X8}";

        // Value = data bytes from raw payload (skip UART header 4B, exclude CRC-16 2B)
        if (record.RawLength >= 5 && record.RawPayload.Length >= 5)
        {
            var vsb = new StringBuilder("0x");
            int dataEnd = record.RawLength - 2;
            for (int i = 4; i < dataEnd && i < record.RawPayload.Length; i++)
                vsb.Append(record.RawPayload[i].ToString("X2"));
            TxtValue.Text = vsb.ToString();
        }
        else
        {
            TxtValue.Text = $"0x{record.Value:X8}";
        }

        // Nested registers — JSON with Address, Value, Name, Index like classic VILS
        var decoded = record.DecodedRegisters;
        if (decoded.Length == 0)
        {
            TxtNested.Text = "/";
        }
        else
        {
            var sb = new StringBuilder();
            sb.AppendLine("[");
            for (int i = 0; i < decoded.Length; i++)
            {
                var (addr, val) = decoded[i];
                var (rName, _) = LsmRegisterMap.ResolveFromDeviceId(addr, record.DeviceId, isEep);
                string nameStr = rName == "/" ? "null" : $"\"{rName}\"";
                sb.Append($"  {{ \"Address\": \"0x{addr:X4}\", \"Value\": \"0x{val:X4}\", " +
                          $"\"Name\": {nameStr}, \"Index\": {i * 4} }}");
                if (i < decoded.Length - 1) sb.AppendLine(",");
                else sb.AppendLine();
            }
            sb.Append(']');
            TxtNested.Text = sb.ToString();
        }
    }

    private void BtnClose_Click(object sender, RoutedEventArgs e) => Close();

    private void PopulateCanRaw(LsmCanDiagRecord record)
    {
        string idStr = record.IsExtendedCanId ? $"0x{record.CanId:X8}" : $"0x{record.CanId:X3}";
        TxtTitle.Text = $"CAN Frame {idStr}";

        TxtTime.Text = record.EffectiveDisplayTimestampUtc.ToLocalTime().ToString("HH:mm:ss.ffffff", CultureInfo.InvariantCulture);
        TxtUnixTs.Text = record.SourceTimestamp.ToString(CultureInfo.InvariantCulture);
        TxtResponseDelay.Text = "/";
        TxtInterFrameDelay.Text = "/";

        TxtNr.Text = record.EffectiveDisplaySequence.ToString(CultureInfo.InvariantCulture);
        TxtName.Text = "CAN";
        TxtAddress.Text = idStr;
        TxtMemoryType.Text = "BUS";
        TxtDevice.Text = $"0x{record.DeviceId:X2}";
        TxtRw.Text = "CAN";

        TxtCrc.Text = "/";
        TxtError.Text = record.Status == LsmCanDiagStatus.Ok ? "/" : record.Status.ToString();
        TxtDescription.Text = $"CAN {(record.IsExtendedCanId ? "Extended" : "Standard")} ID, DLC={record.RawLength}";

        string dataHex = record.RawLength > 0
            ? BitConverter.ToString(record.RawPayload, 0, Math.Min(record.RawLength, record.RawPayload.Length)).Replace("-", " ")
            : "/";
        TxtValue.Text = dataHex;
        TxtRaw.Text = record.RawHex;
        TxtNested.Text = "/";
    }
}
