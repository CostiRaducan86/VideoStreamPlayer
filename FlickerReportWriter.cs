using System;
using ClosedXML.Excel;

namespace VilsSharpX;

/// <summary>
/// Writes the single-sheet XLSX evidence report for a detected flicker event.
/// </summary>
public static class FlickerReportWriter
{
    public static void SaveXlsx(
        string path,
        string eventId,
        DateTime timestampUtc,
        FlickerDetectionStatus status,
        byte[] referenceGrayTopDown,
        byte[] measuredGrayTopDown,
        int width,
        int height,
        byte deviationThreshold,
        int eventFrameCount,
        int detectorMaxPositiveDeviation,
        int detectorMaxNegativeDeviation,
        double detectorMeanAbsoluteDeviation)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentException.ThrowIfNullOrWhiteSpace(eventId);
        ArgumentNullException.ThrowIfNull(referenceGrayTopDown);
        ArgumentNullException.ThrowIfNull(measuredGrayTopDown);
        if (width <= 0 || height <= 0)
            throw new ArgumentOutOfRangeException(nameof(width));

        int pixelCount = width * height;
        if (referenceGrayTopDown.Length < pixelCount)
            throw new ArgumentException("Reference buffer too small.", nameof(referenceGrayTopDown));
        if (measuredGrayTopDown.Length < pixelCount)
            throw new ArgumentException("Measured buffer too small.", nameof(measuredGrayTopDown));

        int maxPositiveDeviation = 0;
        int maxNegativeDeviation = 0;
        long absoluteDeviationSum = 0;
        int deviatedPixelCount = 0;
        for (int index = 0; index < pixelCount; index++)
        {
            int deviation = measuredGrayTopDown[index] - referenceGrayTopDown[index];
            if (Math.Abs(deviation) < deviationThreshold)
                continue;

            deviatedPixelCount++;
            absoluteDeviationSum += Math.Abs(deviation);
            if (deviation > maxPositiveDeviation)
                maxPositiveDeviation = deviation;
            else if (deviation < maxNegativeDeviation)
                maxNegativeDeviation = deviation;
        }

        double meanAbsoluteDeviation = deviatedPixelCount == 0
            ? 0.0
            : (double)absoluteDeviationSum / deviatedPixelCount;
        double deviatedPixelRatio = (double)deviatedPixelCount / pixelCount;

        using var workbook = new XLWorkbook();
        var worksheet = workbook.Worksheets.Add("FlickerEvent");
        worksheet.Cell(1, 1).Value = "Flicker detection evidence";
        worksheet.Range(1, 1, 1, 2).Merge();
        worksheet.Cell(1, 1).Style.Font.Bold = true;
        worksheet.Cell(1, 1).Style.Font.FontSize = 14;

        worksheet.Cell(3, 1).Value = "Event ID";
        worksheet.Cell(3, 2).Value = eventId;
        worksheet.Cell(4, 1).Value = "Timestamp UTC";
        worksheet.Cell(4, 2).Value = timestampUtc;
        worksheet.Cell(4, 2).Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss.000";
        worksheet.Cell(5, 1).Value = "Status";
        worksheet.Cell(5, 2).Value = status.ToString();
        worksheet.Cell(6, 1).Value = "Comparison resolution";
        worksheet.Cell(6, 2).Value = $"{width} x {height}";
        worksheet.Range(3, 2, 17, 2).Style.Alignment.Horizontal = XLAlignmentHorizontalValues.Right;
        worksheet.Cell(7, 1).Value = "Deviation threshold";
        worksheet.Cell(7, 2).Value = deviationThreshold;
        worksheet.Cell(8, 1).Value = "Event duration (frames)";
        worksheet.Cell(8, 2).Value = eventFrameCount;
        worksheet.Cell(9, 1).Value = "Peak positive deviation";
        worksheet.Cell(9, 2).Value = detectorMaxPositiveDeviation;
        worksheet.Cell(10, 1).Value = "Peak negative deviation";
        worksheet.Cell(10, 2).Value = detectorMaxNegativeDeviation;
        worksheet.Cell(11, 1).Value = "Peak mean absolute deviation";
        worksheet.Cell(11, 2).Value = detectorMeanAbsoluteDeviation;

        worksheet.Cell(13, 1).Value = "Pixels above threshold";
        worksheet.Cell(13, 2).Value = deviatedPixelCount;
        worksheet.Cell(14, 1).Value = "Pixels above threshold ratio";
        worksheet.Cell(14, 2).Value = deviatedPixelRatio;
        worksheet.Cell(14, 2).Style.NumberFormat.Format = "0.00%";
        worksheet.Cell(15, 1).Value = "Maximum positive deviation";
        worksheet.Cell(15, 2).Value = maxPositiveDeviation;
        worksheet.Cell(16, 1).Value = "Maximum negative deviation";
        worksheet.Cell(16, 2).Value = maxNegativeDeviation;
        worksheet.Cell(17, 1).Value = "Mean absolute deviation";
        worksheet.Cell(17, 2).Value = meanAbsoluteDeviation;

        const int headerRow = 19;
        string[] headers = ["Pixel ID", "X", "Y", "Reference", "Measured", "Deviation"];
        for (int column = 0; column < headers.Length; column++)
            worksheet.Cell(headerRow, column + 1).Value = headers[column];

        var tableHeaderRange = worksheet.Range(headerRow, 1, headerRow, headers.Length);
        tableHeaderRange.Style.Font.Bold = true;
        tableHeaderRange.Style.Alignment.Horizontal = XLAlignmentHorizontalValues.Center;
        tableHeaderRange.Style.Alignment.Vertical = XLAlignmentVerticalValues.Center;

        int row = headerRow + 1;
        for (int index = 0; index < pixelCount; index++)
        {
            int deviation = measuredGrayTopDown[index] - referenceGrayTopDown[index];
            if (Math.Abs(deviation) < deviationThreshold)
                continue;

            int y = index / width;
            worksheet.Cell(row, 1).Value = index + 1;
            worksheet.Cell(row, 2).Value = index - (y * width);
            worksheet.Cell(row, 3).Value = y;
            worksheet.Cell(row, 4).Value = referenceGrayTopDown[index];
            worksheet.Cell(row, 5).Value = measuredGrayTopDown[index];
            worksheet.Cell(row, 6).Value = deviation;
            row++;
        }

        if (row > headerRow + 1)
        {
            var valuesRange = worksheet.Range(headerRow + 1, 1, row - 1, headers.Length);
            valuesRange.Style.Alignment.Horizontal = XLAlignmentHorizontalValues.Center;
            valuesRange.Style.Alignment.Vertical = XLAlignmentVerticalValues.Center;
        }

        worksheet.Columns(1, headers.Length).AdjustToContents();
        worksheet.SheetView.FreezeRows(headerRow);
        workbook.SaveAs(path);
    }
}