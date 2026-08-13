namespace VilsSharpX;

/// <summary>
/// Local state for communication fault injection.
/// Firmware-backed communication fault state used by the control window.
/// </summary>
public sealed class CommunicationFaultState
{
    public bool AvtpFaultEnabled { get; set; }
    public bool LvdsFaultEnabled { get; set; }
    public int LvdsFaultDurationMilliseconds { get; set; } = 2000;
    public bool CanUartFaultEnabled { get; set; }
    public int CanUartMode { get; set; }
    public int CanUartFaultMode { get; set; } = 1;
    public int CanUartFaultDirection { get; set; }
    public int CanUartFaultDurationMilliseconds { get; set; } = 2000;
}