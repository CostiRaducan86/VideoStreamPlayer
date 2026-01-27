namespace VideoStreamPlayer;

public sealed record RvfChunk(
    ushort Width,
    ushort Height,
    ushort LineNumber1Based,
    byte NumLines,
    bool EndFrame,
    uint FrameId,
    uint Seq,
    byte[] Payload);
