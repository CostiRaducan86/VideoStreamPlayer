using System;
using System.Collections.Generic;
using VilsSharpX.DefectPixel;

namespace VilsSharpX;

/// <summary>Builds SmartVisio defect-list Ethernet frames without accessing hardware.</summary>
internal static class SetDefectListPacketBuilder
{
    private const byte CmdSetDefectList = 0x04;
    private const byte CmdSetDefectListNichia = 0x05;
    private const int BytesPerDefect = 5;
    private const int NichiaBytesPerDefect = 4;
    private const int HeaderOffset = 19;
    private const int MinFrameSize = 60;

    internal static byte[] BuildOsramPacket(bool enable, IReadOnlyList<OsramDefectEntry>? defects)
    {
        defects ??= [];
        int count = Math.Min(defects.Count, SetDefectListCommand.MaxDefects);
        int frameLen = Math.Max(MinFrameSize, HeaderOffset + count * BytesPerDefect);
        var pkt = CreateHeader(enable, CmdSetDefectList, count, frameLen);

        for (int i = 0; i < count; i++)
        {
            var d = defects[i];
            int b = HeaderOffset + i * BytesPerDefect;
            pkt[b + 0] = (byte)Math.Clamp(d.Slot, 0, 63);
            int x = Math.Clamp(d.X, 0, 319);
            pkt[b + 1] = (byte)(x >> 8);
            pkt[b + 2] = (byte)x;
            pkt[b + 3] = (byte)Math.Clamp(d.Y, 0, 79);
            pkt[b + 4] = (byte)(((d.PxState != 0 ? 1 : 0) << 2) | ((int)d.DefectType & 0x03));
        }

        return pkt;
    }

    internal static byte[] BuildNichiaPacket(bool enable, IReadOnlyList<NichiaDefectEntry>? defects)
    {
        defects ??= [];
        int count = Math.Min(defects.Count, SetDefectListCommand.MaxNichiaDefects);
        int frameLen = Math.Max(MinFrameSize, HeaderOffset + count * NichiaBytesPerDefect);
        var pkt = CreateHeader(enable, CmdSetDefectListNichia, count, frameLen);

        for (int i = 0; i < count; i++)
        {
            var d = defects[i];
            int b = HeaderOffset + i * NichiaBytesPerDefect;
            int idx = Math.Clamp(d.PixelId0, 0, NichiaDefectEntry.TotalPixels - 1);
            pkt[b + 0] = (byte)(idx >> 8);
            pkt[b + 1] = (byte)idx;
            pkt[b + 2] = (byte)(d.DefectType == NichiaDefectType.Bright ? 1 : 0);
            pkt[b + 3] = (byte)(d.SegmentPair & 0x01);
        }

        return pkt;
    }

    private static byte[] CreateHeader(bool enable, byte command, int count, int frameLen)
    {
        var pkt = new byte[frameLen];
        Array.Fill(pkt, (byte)0, 0, pkt.Length);
        pkt[0] = pkt[1] = pkt[2] = pkt[3] = pkt[4] = pkt[5] = 0xFF;
        pkt[6] = 0x02; pkt[7] = 0x0A; pkt[8] = 0xF0;
        pkt[9] = 0x4E; pkt[10] = 0x49; pkt[11] = 0x02;
        pkt[12] = 0x88; pkt[13] = 0xB5;
        pkt[14] = 0x43; pkt[15] = 0x4D;
        pkt[16] = command;
        pkt[17] = enable ? (byte)1 : (byte)0;
        pkt[18] = (byte)count;
        return pkt;
    }
}