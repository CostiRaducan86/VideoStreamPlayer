using System;

namespace VilsSharpX;

/// <summary>
/// Downscales a high-resolution grayscale frame to a target resolution using block averaging.
/// Used for comparing Basler camera frames (post-AOI crop) against LVDS/AVTP frames at native LED resolution.
/// </summary>
public static class FrameDownscaler
{
    /// <summary>
    /// Downscales a Gray8 source image to the target dimensions using block averaging.
    /// Each destination pixel is the average of all source pixels in the corresponding block.
    /// </summary>
    /// <param name="src">Source Gray8 pixel data.</param>
    /// <param name="srcW">Source width.</param>
    /// <param name="srcH">Source height.</param>
    /// <param name="dstW">Target width (e.g. 320 for Osram).</param>
    /// <param name="dstH">Target height (e.g. 80 for Osram).</param>
    /// <param name="dst">Pre-allocated destination buffer (dstW*dstH bytes). If null or wrong size, a new one is allocated.</param>
    /// <returns>The destination buffer containing the downscaled image.</returns>
    public static byte[] DownscaleBlockAverage(byte[] src, int srcW, int srcH, int dstW, int dstH, byte[]? dst = null)
    {
        if (dst == null || dst.Length != dstW * dstH)
            dst = new byte[dstW * dstH];

        // If source matches destination, just copy
        if (srcW == dstW && srcH == dstH)
        {
            int len = Math.Min(src.Length, dst.Length);
            Buffer.BlockCopy(src, 0, dst, 0, len);
            return dst;
        }

        // Block dimensions (floating point for non-integer ratios)
        double blockW = (double)srcW / dstW;
        double blockH = (double)srcH / dstH;

        for (int dy = 0; dy < dstH; dy++)
        {
            int srcY0 = (int)(dy * blockH);
            int srcY1 = (int)((dy + 1) * blockH);
            if (srcY1 > srcH) srcY1 = srcH;
            if (srcY1 <= srcY0) srcY1 = srcY0 + 1;

            for (int dx = 0; dx < dstW; dx++)
            {
                int srcX0 = (int)(dx * blockW);
                int srcX1 = (int)((dx + 1) * blockW);
                if (srcX1 > srcW) srcX1 = srcW;
                if (srcX1 <= srcX0) srcX1 = srcX0 + 1;

                // Sum all pixels in the block
                int sum = 0;
                int count = 0;
                for (int sy = srcY0; sy < srcY1; sy++)
                {
                    int rowOff = sy * srcW;
                    for (int sx = srcX0; sx < srcX1; sx++)
                    {
                        sum += src[rowOff + sx];
                        count++;
                    }
                }

                dst[dy * dstW + dx] = (byte)(sum / count);
            }
        }

        return dst;
    }
}
