using System;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;

namespace VilsSharpX;

/// <summary>
/// Writes one immutable A/B/C/D flicker evidence set and its CSV metadata.
/// </summary>
public sealed class FlickerEvidenceExporter
{
    public static async Task<string> ExportAsync(
        string eventId,
        Frame frameA,
        Frame frameB,
        Frame frameC,
        byte[] diffBgr,
        byte[] reportReference,
        byte[] reportMeasured,
        int comparisonWidth,
        int comparisonHeight,
        FlickerDetectionStatusSnapshot snapshot)
    {
        string root = RecordingManager.FindRepoRootWithDocs(AppContext.BaseDirectory)
            ?? Directory.GetCurrentDirectory();
        string folderName = eventId;
        string outputDirectory = Path.Combine(root, "docs", "outputs", "flickerDetections", folderName);
        Directory.CreateDirectory(outputDirectory);
        string xlsxPath = Path.Combine(outputDirectory, "flicker_report.xlsx");

        byte[] a = (byte[])frameA.Data.Clone();
        byte[] b = (byte[])frameB.Data.Clone();
        byte[] c = (byte[])frameC.Data.Clone();
        byte[] d = (byte[])diffBgr.Clone();
        byte[] reportA = (byte[])reportReference.Clone();
        byte[] reportB = (byte[])reportMeasured.Clone();

        await Task.Run(() =>
        {
            ImageUtils.SaveGray8Png(Path.Combine(outputDirectory, "A_AVTP.png"), a, frameA.Width, frameA.Height);
            ImageUtils.SaveGray8Png(Path.Combine(outputDirectory, "B_LVDS.png"), b, frameB.Width, frameB.Height);
            ImageUtils.SaveGray8Png(Path.Combine(outputDirectory, "C_LSM.png"), c, frameC.Width, frameC.Height);
            ImageUtils.SaveBgr24Png(Path.Combine(outputDirectory, "D_Compare.png"), d, comparisonWidth, comparisonHeight);
            FlickerReportWriter.SaveXlsx(
                xlsxPath, eventId, snapshot.LastEventUtc ?? DateTime.UtcNow,
                snapshot.Status, reportA, reportB,
                comparisonWidth, comparisonHeight,
                (byte)Math.Max(snapshot.DeviationTrigger, FlickerDetector.MinimumPixelDeviation),
                snapshot.CandidateFrameCount, snapshot.MaxPositiveDeviation,
                snapshot.MaxNegativeDeviation, snapshot.MeanAbsoluteDeviation);
        });

        return Path.Combine("~", "outputs", "flickerDetections", folderName);
    }

}
