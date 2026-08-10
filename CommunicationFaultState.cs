namespace VilsSharpX;

/// <summary>
/// Local state for communication fault injection.
/// Firmware-backed faults are reserved for a later implementation step.
/// </summary>
public sealed class CommunicationFaultState
{
    public bool AvtpFaultEnabled { get; set; }
    public bool LvdsFaultEnabled { get; set; }
    public bool CanUartFaultEnabled { get; set; }
}