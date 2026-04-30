using System;

namespace VilsSharpX;

/// <summary>
/// Represents a single video frame with grayscale pixel data.
/// </summary>
public sealed class Frame(int w, int h, byte[] data, DateTime tsUtc)
{
    public int Width { get; } = w;
    public int Height { get; } = h;
    public int Stride { get; } = w;
    public byte[] Data { get; } = (byte[])data.Clone(); // safe copy; later we can optimize with pooling
    public DateTime TimestampUtc { get; } = tsUtc;
}

/// <summary>
/// Represents a loaded PGM image.
/// </summary>
public sealed class PgmImage
{
    public int Width { get; init; }
    public int Height { get; init; }
    public byte[] Data { get; init; } = [];
}
