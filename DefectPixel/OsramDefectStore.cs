using System;
using System.Collections.Generic;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// Store for OSRAM defect definitions created in the UI.
///
/// This class only holds the active defect list and the injection enable flag.
/// The actual ELEDERP/ELEDERS injection into the CAN-UART stream is performed by
/// the SmartVisio Box firmware (see defect_inject.c). The PC pushes this list to the SmartVisio Box via
/// <see cref="SetDefectListCommand"/> whenever the state changes.
/// </summary>
public class OsramDefectStore
{
    public const int MaxDefects = 64;

    /// <summary>Active defects (key: pixelId0, value: defect entry).</summary>
    private readonly Dictionary<int, OsramDefectEntry> m_activeDefects = [];

    /// <summary>Enable/disable injection globally (mirrored to the SmartVisio Box).</summary>
    public bool InjectionEnabled { get; set; }

    /// <summary>
    /// Add or update a defect. Validates coordinates and slot assignment.
    /// </summary>
    public void AddDefect(OsramDefectEntry defect)
    {
        ArgumentNullException.ThrowIfNull(defect);

        if (defect.X < 0 || defect.X > 319)
            throw new ArgumentException($"Invalid X coordinate: {defect.X}", nameof(defect));
        if (defect.Y < 0 || defect.Y > 79)
            throw new ArgumentException($"Invalid Y coordinate: {defect.Y}", nameof(defect));
        if (defect.Slot < 0 || defect.Slot > 63)
            throw new ArgumentException($"Invalid slot: {defect.Slot}", nameof(defect));
        if (defect.PixelId0 != defect.Y * 320 + defect.X)
            throw new ArgumentException($"Pixel ID does not match coordinates: {defect.PixelId0}", nameof(defect));

        if (!m_activeDefects.ContainsKey(defect.PixelId0) && m_activeDefects.Count >= MaxDefects)
            throw new InvalidOperationException($"Defect table full (max {MaxDefects} defects)");

        m_activeDefects[defect.PixelId0] = defect;
        DiagnosticLogger.Log($"[DefectStore] Added OSRAM defect: {defect}");
    }

    /// <summary>Remove a defect by 0-based pixel ID.</summary>
    public bool RemoveDefect(int pixelId0)
    {
        bool removed = m_activeDefects.Remove(pixelId0);
        if (removed)
            DiagnosticLogger.Log($"[DefectStore] Removed OSRAM defect at pixelId0={pixelId0}");
        return removed;
    }

    /// <summary>Clear all active defects.</summary>
    public void ClearAllDefects()
    {
        int count = m_activeDefects.Count;
        m_activeDefects.Clear();
        if (count > 0)
            DiagnosticLogger.Log($"[DefectStore] Cleared all {count} OSRAM defects");
    }

    /// <summary>Snapshot of all active defects.</summary>
    public List<OsramDefectEntry> GetActiveDefects()
    {
        return [.. m_activeDefects.Values];
    }

    /// <summary>Reset to safe state: injection disabled and all defects cleared.</summary>
    public void Reset()
    {
        InjectionEnabled = false;
        ClearAllDefects();
        DiagnosticLogger.Log("[DefectStore] Reset: injection disabled and all defects cleared");
    }
}
