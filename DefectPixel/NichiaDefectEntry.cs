using System;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// Defect types for Nichia/TLD816K. Unlike OSRAM (S2G/Open/Stuck), the TLD816K
/// diagnostic model distinguishes only dark and bright failures.
/// </summary>
public enum NichiaDefectType
{
    /// <summary>Pixel expected to emit but detected too dark / inactive.</summary>
    Dark = 0,

    /// <summary>Pixel expected off/low but detected too bright / overactive.</summary>
    Bright = 1
}

/// <summary>
/// A single Nichia/TLD816K defect pixel definition created in the UI.
///
/// Coordinates use the datasheet convention: 256x64 matrix, row 0 at the bottom,
/// column 0 at the left, pixel numbering row-wise (pixel_index0 = row * 256 + column).
/// The 1-based <see cref="PixelIdDisplay"/> mirrors the OSRAM convention and matches the
/// Cal Mod / EEPROM defect map validated on hardware (byte 0x21AE + pixel_index0 = 0x00).
/// </summary>
public sealed class NichiaDefectEntry : IEquatable<NichiaDefectEntry>
{
    public const int MaxX = 255;
    public const int MaxY = 63;
    public const int TotalPixels = 256 * 64; // 16384

    /// <summary>Column, 0..255.</summary>
    public int X { get; set; }

    /// <summary>Row, 0..63 (row 0 = bottom).</summary>
    public int Y { get; set; }

    /// <summary>0-based pixel index (row * 256 + column), 0..16383.</summary>
    public int PixelId0 { get; set; }

    /// <summary>1-based pixel ID for display, PixelId0 + 1.</summary>
    public int PixelIdDisplay => PixelId0 + 1;

    /// <summary>Defect type (Dark or Bright).</summary>
    public NichiaDefectType DefectType { get; set; }

    /// <summary>
    /// Segment pair derived from the column: columns 0..127 -> pair "0&amp;1",
    /// columns 128..255 -> pair "2&amp;3". Determines the PIXEL_ID storage block for
    /// the runtime-injection path.
    /// </summary>
    public int SegmentPair => X < 128 ? 0 : 1;

    public NichiaDefectEntry() { }

    public NichiaDefectEntry(int x, int y, NichiaDefectType defectType)
    {
        if (x < 0 || x > MaxX)
            throw new ArgumentOutOfRangeException(nameof(x), "X must be 0..255");
        if (y < 0 || y > MaxY)
            throw new ArgumentOutOfRangeException(nameof(y), "Y must be 0..63");

        X = x;
        Y = y;
        PixelId0 = y * 256 + x;
        DefectType = defectType;
    }

    /// <summary>Create an entry from a 0-based pixel index.</summary>
    public static NichiaDefectEntry FromPixelId0(int pixelId0, NichiaDefectType defectType)
    {
        if (pixelId0 < 0 || pixelId0 >= TotalPixels)
            throw new ArgumentOutOfRangeException(nameof(pixelId0), "Pixel ID must be 0..16383");

        return new NichiaDefectEntry(pixelId0 % 256, pixelId0 / 256, defectType);
    }

    public string SegmentPairLabel => SegmentPair == 0 ? "0&1" : "2&3";

    public override bool Equals(object? obj) => Equals(obj as NichiaDefectEntry);

    public bool Equals(NichiaDefectEntry? other)
        => other != null && PixelId0 == other.PixelId0 && DefectType == other.DefectType;

    public override int GetHashCode() => HashCode.Combine(PixelId0, DefectType);

    public override string ToString()
        => $"NICHIA X={X} Y={Y} ID0={PixelId0} ID_display={PixelIdDisplay} " +
           $"Type={DefectType} SegPair={SegmentPairLabel}";
}
