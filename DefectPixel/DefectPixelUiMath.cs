using System.Collections.Generic;

namespace VilsSharpX.DefectPixel;

/// <summary>Pure calculations shared by the DefectPixels WPF input workflows.</summary>
public static class DefectPixelUiMath
{
    public static bool TryGetCoordinatesFromDisplayId(
        int pixelIdDisplay,
        int width,
        int height,
        out int x,
        out int y)
    {
        x = 0;
        y = 0;
        if (width <= 0 || height <= 0 || pixelIdDisplay < 1 || pixelIdDisplay > width * height)
            return false;

        int pixelId0 = pixelIdDisplay - 1;
        x = pixelId0 % width;
        y = pixelId0 / width;
        return true;
    }

    public static bool TryGetDisplayId(int x, int y, int width, int height, out int pixelIdDisplay)
    {
        pixelIdDisplay = 0;
        if (width <= 0 || height <= 0 || x < 0 || x >= width || y < 0 || y >= height)
            return false;

        pixelIdDisplay = y * width + x + 1;
        return true;
    }

    public static int GetNextFreeSlot(IEnumerable<int> usedSlots)
    {
        int max = -1;
        foreach (int slot in usedSlots)
        {
            if (slot > max)
                max = slot;
        }

        return max + 1;
    }
}