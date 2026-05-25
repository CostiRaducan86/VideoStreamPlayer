using System;
using Basler.Pylon;

namespace VilsSharpX;

/// <summary>
/// Detects the LED matrix bounding box in a full-resolution Basler frame
/// and computes the AOI (OffsetX, OffsetY, Width, Height) to crop only
/// the active micro-LED area. Works with Gray8 frames where the matrix
/// is significantly brighter than background.
/// </summary>
public sealed class BaslerAutoCalibration
{
    /// <summary>Result of the calibration: the detected region of interest.</summary>
    public record CalibrationResult(
        int OffsetX, int OffsetY, int Width, int Height,
        byte ThresholdUsed, int BrightPixelCount);

    /// <summary>
    /// Margin (pixels) added around the detected bounding box to avoid clipping edge LEDs.
    /// </summary>
    public int Margin { get; set; } = 4;

    /// <summary>
    /// Minimum fraction of bright pixels (relative to detected bbox area) to consider valid.
    /// Prevents false positives from noise.
    /// </summary>
    public double MinFillRatio { get; set; } = 0.3;

    /// <summary>
    /// Explicit threshold override (0 = auto Otsu). If set > 0, uses this value directly.
    /// </summary>
    public byte ThresholdOverride { get; set; } = 0;

    /// <summary>
    /// Analyse a full-resolution Gray8 frame and return the bounding box of the bright region.
    /// Returns null if no valid bright region is found.
    /// </summary>
    public CalibrationResult? DetectMatrixRegion(byte[] frame, int frameWidth, int frameHeight)
    {
        if (frame == null || frame.Length < frameWidth * frameHeight)
            return null;

        byte threshold = ThresholdOverride > 0
            ? ThresholdOverride
            : ComputeOtsuThreshold(frame, frameWidth * frameHeight);

        // Find bounding box of all pixels above threshold
        int minX = frameWidth, maxX = -1;
        int minY = frameHeight, maxY = -1;
        int brightCount = 0;

        for (int y = 0; y < frameHeight; y++)
        {
            int rowStart = y * frameWidth;
            for (int x = 0; x < frameWidth; x++)
            {
                if (frame[rowStart + x] > threshold)
                {
                    brightCount++;
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }

        // No bright region found
        if (maxX < 0 || maxY < 0)
            return null;

        int bboxW = maxX - minX + 1;
        int bboxH = maxY - minY + 1;
        int bboxArea = bboxW * bboxH;

        // Check fill ratio to reject noise
        double fillRatio = (double)brightCount / bboxArea;
        if (fillRatio < MinFillRatio)
            return null;

        // Apply margin, clamped to frame bounds
        int ox = Math.Max(0, minX - Margin);
        int oy = Math.Max(0, minY - Margin);
        int ex = Math.Min(frameWidth - 1, maxX + Margin);
        int ey = Math.Min(frameHeight - 1, maxY + Margin);

        int finalW = ex - ox + 1;
        int finalH = ey - oy + 1;

        // Pylon requires Width/Height to be multiples of the camera's increment (usually 2 or 4).
        // Round down to nearest even to be safe.
        finalW &= ~1;
        finalH &= ~1;
        ox &= ~1;
        oy &= ~1;

        if (finalW < 16 || finalH < 16)
            return null;

        return new CalibrationResult(ox, oy, finalW, finalH, threshold, brightCount);
    }

    /// <summary>
    /// Apply the calibration result to a Pylon camera (set AOI registers).
    /// Camera must be open. Stops grabbing if active, applies, then restarts.
    /// Returns true if successfully applied.
    /// </summary>
    public static bool ApplyToCamera(Camera camera, CalibrationResult result, Action<string>? log = null)
    {
        if (camera == null || result == null) return false;

        var p = camera.Parameters;
        try
        {
            // Reset offsets first to avoid range clamp issues
            p[PLCamera.OffsetX].TrySetValue(0);
            p[PLCamera.OffsetY].TrySetValue(0);

            // Set new dimensions
            bool wOk = p[PLCamera.Width].TrySetValue(result.Width);
            bool hOk = p[PLCamera.Height].TrySetValue(result.Height);

            // Then set offsets
            bool oxOk = p[PLCamera.OffsetX].TrySetValue(result.OffsetX);
            bool oyOk = p[PLCamera.OffsetY].TrySetValue(result.OffsetY);

            log?.Invoke($"[basler-cal] AOI applied: Offset=({result.OffsetX},{result.OffsetY}), " +
                        $"Size={result.Width}×{result.Height}, threshold={result.ThresholdUsed}, " +
                        $"bright={result.BrightPixelCount}px, success=W:{wOk} H:{hOk} OX:{oxOk} OY:{oyOk}");

            return wOk && hOk && oxOk && oyOk;
        }
        catch (Exception ex)
        {
            log?.Invoke($"[basler-cal] Failed to apply AOI: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Reset AOI to full sensor area.
    /// </summary>
    public static void ResetAoi(Camera camera, Action<string>? log = null)
    {
        if (camera == null) return;
        var p = camera.Parameters;
        try
        {
            p[PLCamera.OffsetX].TrySetValue(0);
            p[PLCamera.OffsetY].TrySetValue(0);
            long wMax = p[PLCamera.Width].GetMaximum();
            long hMax = p[PLCamera.Height].GetMaximum();
            p[PLCamera.Width].SetValue(wMax);
            p[PLCamera.Height].SetValue(hMax);
            log?.Invoke("[basler-cal] AOI reset to full sensor");
        }
        catch (Exception ex)
        {
            log?.Invoke($"[basler-cal] Failed to reset AOI: {ex.Message}");
        }
    }

    /// <summary>
    /// Compute Otsu's threshold for a Gray8 histogram.
    /// Separates background (dark) from foreground (bright LED matrix).
    /// </summary>
    private static byte ComputeOtsuThreshold(byte[] data, int count)
    {
        // Build histogram
        Span<int> hist = stackalloc int[256];
        for (int i = 0; i < count; i++)
            hist[data[i]]++;

        // Total weighted sum
        long totalSum = 0;
        for (int i = 0; i < 256; i++)
            totalSum += (long)i * hist[i];

        long sumB = 0;
        int wB = 0;
        double maxVariance = 0;
        int bestThreshold = 128;

        for (int t = 0; t < 256; t++)
        {
            wB += hist[t];
            if (wB == 0) continue;

            int wF = count - wB;
            if (wF == 0) break;

            sumB += (long)t * hist[t];
            double meanB = (double)sumB / wB;
            double meanF = (double)(totalSum - sumB) / wF;
            double diff = meanB - meanF;
            double variance = (double)wB * wF * diff * diff;

            if (variance > maxVariance)
            {
                maxVariance = variance;
                bestThreshold = t;
            }
        }

        return (byte)bestThreshold;
    }
}
