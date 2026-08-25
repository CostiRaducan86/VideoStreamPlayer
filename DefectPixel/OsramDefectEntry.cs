using System;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// Defect types recognized in OSRAM diagnostic registers.
/// Maps directly to PXDIAG bits.
/// </summary>
public enum OsramDefectType
{
    /// <summary>No defect (PXDIAG == 0)</summary>
    None = 0,

    /// <summary>Short-to-GND defect (PXDIAG == 1)</summary>
    ShortToGnd = 1,

    /// <summary>Open circuit defect (PXDIAG == 2)</summary>
    Open = 2,

    /// <summary>Stuck defect (PXDIAG == 3)</summary>
    Stuck = 3
}

/// <summary>
/// Decoded entry from OSRAM ELEDERP (position) and ELEDERS (status) registers.
/// Provides pixel coordinates in 0-based and 1-based (display) formats.
/// </summary>
public sealed class OsramDefectEntry : IEquatable<OsramDefectEntry>
{
    /// <summary>Slot index (0..63), maps to ELEDERP/ELEDERS address offset</summary>
    public int Slot { get; set; }

    /// <summary>Pixel X coordinate (0..319)</summary>
    public int X { get; set; }

    /// <summary>Pixel Y coordinate (0..79)</summary>
    public int Y { get; set; }

    /// <summary>0-based pixel ID for internal use (CAN/ECU level)</summary>
    public int PixelId0 { get; set; }

    /// <summary>1-based pixel ID for LVDS display (user-facing)</summary>
    public int PixelIdDisplay => PixelId0 + 1;

    /// <summary>LED expected state: 0 = OFF, 1 = ON</summary>
    public int PxState { get; set; }

    /// <summary>Defect type (None, ShortToGnd, Open, Stuck)</summary>
    public OsramDefectType DefectType { get; set; }

    /// <summary>
    /// True if this is a dark-visible candidate:
    /// PXSTATE == 1 (LED expected ON) AND PXDIAG != 0 (some defect).
    /// In this case, LED is ON but defective, making it appear dark.
    /// </summary>
    public bool DarkVisibleCandidate => (PxState == 1) && (DefectType != OsramDefectType.None);

    public OsramDefectEntry() { }

    public OsramDefectEntry(
        int slot,
        int x,
        int y,
        int pixelId0,
        int pxState,
        OsramDefectType defectType)
    {
        if (x < 0 || x > 319)
            throw new ArgumentOutOfRangeException(nameof(x), "X must be 0..319");
        if (y < 0 || y > 79)
            throw new ArgumentOutOfRangeException(nameof(y), "Y must be 0..79");
        if (slot < 0 || slot > 63)
            throw new ArgumentOutOfRangeException(nameof(slot), "Slot must be 0..63");

        Slot = slot;
        X = x;
        Y = y;
        PixelId0 = pixelId0;
        PxState = pxState;
        DefectType = defectType;
    }

    public override bool Equals(object? obj) => Equals(obj as OsramDefectEntry);

    public bool Equals(OsramDefectEntry? other)
    {
        return other != null
            && Slot == other.Slot
            && X == other.X
            && Y == other.Y
            && PixelId0 == other.PixelId0
            && PxState == other.PxState
            && DefectType == other.DefectType;
    }

    public override int GetHashCode()
        => HashCode.Combine(Slot, X, Y, PixelId0, PxState, DefectType);

    public override string ToString()
        => $"OSRAM Slot={Slot} X={X} Y={Y} ID0={PixelId0} ID_display={PixelIdDisplay} " +
           $"PXSTATE={PxState} PXDIAG={DefectType} DarkVisibleCandidate={DarkVisibleCandidate}";
}
