namespace VilsSharpX;

/// <summary>
/// Metadata about a reassembled LVDS frame (now produced by the Ethernet
/// capture path: OsramEthCapture / NichiaEthCapture).
/// </summary>
public sealed record LvdsFrameMeta
{
    public uint FrameId { get; init; }
    public int Width { get; init; }
    public int Height { get; init; }
    /// <summary>Lines actually received before this frame was emitted.</summary>
    public int LinesReceived { get; init; }
    /// <summary>Lines that were received (placed at correct row) in this frame.</summary>
    public int ValidLines { get; init; }
    /// <summary>Expected total lines per frame (H_LVDS).</summary>
    public int LinesExpected { get; init; }
    /// <summary>Per-line received mask (length = ActiveHeight).</summary>
    public bool[] LineValidityMask { get; init; } = [];
    public int SyncLosses { get; init; }
    public int CrcErrors { get; init; }
    public int ParityErrors { get; init; }
    public long TotalBytes { get; init; }
}
