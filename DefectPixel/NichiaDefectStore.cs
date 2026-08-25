using System.Collections.Generic;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// Store for Nichia/TLD816K defect definitions created in the UI.
///
/// Like <see cref="OsramDefectStore"/>, this only holds the active defect list and the
/// injection enable flag. The actual injection into the CAN-UART stream is performed by
/// the SmartVisio Box firmware; the PC pushes this list via the SET_DEFECT_LIST command
/// (Nichia layout) whenever the state changes.
/// </summary>
public class NichiaDefectStore
{
    public const int MaxDefects = 64;

    /// <summary>Active defects, keyed by 0-based pixel index.</summary>
    private readonly Dictionary<int, NichiaDefectEntry> m_activeDefects = [];

    /// <summary>Enable/disable injection globally (mirrored to the SmartVisio Box).</summary>
    public bool InjectionEnabled { get; set; }

    /// <summary>Add or update a defect (keyed by pixel index; last write wins).</summary>
    public void AddDefect(NichiaDefectEntry defect)
    {
        System.ArgumentNullException.ThrowIfNull(defect);

        if (defect.X < 0 || defect.X > NichiaDefectEntry.MaxX)
            throw new System.ArgumentException($"Invalid X coordinate: {defect.X}", nameof(defect));
        if (defect.Y < 0 || defect.Y > NichiaDefectEntry.MaxY)
            throw new System.ArgumentException($"Invalid Y coordinate: {defect.Y}", nameof(defect));
        if (defect.PixelId0 != defect.Y * 256 + defect.X)
            throw new System.ArgumentException($"Pixel ID does not match coordinates: {defect.PixelId0}", nameof(defect));

        if (!m_activeDefects.ContainsKey(defect.PixelId0) && m_activeDefects.Count >= MaxDefects)
            throw new System.InvalidOperationException($"Defect table full (max {MaxDefects} defects)");

        m_activeDefects[defect.PixelId0] = defect;
        DiagnosticLogger.Log($"[NichiaDefectStore] Added defect: {defect}");
    }

    /// <summary>Remove a defect by 0-based pixel index.</summary>
    public bool RemoveDefect(int pixelId0)
    {
        bool removed = m_activeDefects.Remove(pixelId0);
        if (removed)
            DiagnosticLogger.Log($"[NichiaDefectStore] Removed defect at pixelId0={pixelId0}");
        return removed;
    }

    /// <summary>Clear all active defects.</summary>
    public void ClearAllDefects()
    {
        int count = m_activeDefects.Count;
        m_activeDefects.Clear();
        if (count > 0)
            DiagnosticLogger.Log($"[NichiaDefectStore] Cleared all {count} defects");
    }

    /// <summary>Snapshot of all active defects.</summary>
    public List<NichiaDefectEntry> GetActiveDefects() => [.. m_activeDefects.Values];

    /// <summary>Reset to safe state: injection disabled and all defects cleared.</summary>
    public void Reset()
    {
        InjectionEnabled = false;
        ClearAllDefects();
        DiagnosticLogger.Log("[NichiaDefectStore] Reset: injection disabled and all defects cleared");
    }
}
