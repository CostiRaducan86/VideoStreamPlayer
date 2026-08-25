using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// WPF window for defining Nichia/TLD816K defect pixels (256x64).
///
/// Mirrors <see cref="OsramDefectControlWindow"/>: it only DEFINES defects and toggles
/// injection. The actual injection is performed by the SmartVisio Box firmware; the host
/// (MainWindow) pushes the list on every <see cref="DefectStateChanged"/> via
/// <see cref="SetDefectListCommand.SendNichia"/>.
/// </summary>
public partial class NichiaDefectControlWindow : Window
{
    // Nichia active display resolution. The preview bitmap is native 256x64 (one bitmap
    // pixel per logical pixel), displayed with Stretch=Fill + NearestNeighbor.
    private const int PreviewW = 256;
    private const int PreviewH = 64;

    private readonly NichiaDefectStore m_store;
    private WriteableBitmap? m_gridBitmap;

    // Fullscreen zoom/pan transforms (same interaction model as OSRAM / pane B):
    //   - plain left-drag           : pan (only when zoomed in)
    //   - Ctrl + left-click/drag    : paint pixels (pen) and add them to the list
    //   - Ctrl + right-click/drag   : erase pixels (eraser) and remove them
    //   - Ctrl + scroll             : zoom around the cursor
    //   - double-click (no Ctrl)    : close fullscreen
    private readonly ScaleTransform m_fsScale = new(1.0, 1.0);
    private readonly TranslateTransform m_fsPan = new(0.0, 0.0);
    private bool m_fsPanning;
    private bool m_fsPainting;
    private bool m_fsErasing;
    private bool m_fsStrokeChanged;
    private Point m_fsPanStart;
    private double m_fsPanStartX;
    private double m_fsPanStartY;

    private const double ZoomFactor = 1.15;
    private const double MinZoom = 1.0;
    private const double MaxZoom = 25.0;

    private bool m_suppressCoordSync;

    /// <summary>Raised whenever the injection enable state or the active defect list changes.</summary>
    public event Action? DefectStateChanged;

    public NichiaDefectControlWindow(NichiaDefectStore store)
    {
        InitializeComponent();

        m_store = store ?? throw new ArgumentNullException(nameof(store));

        InjectionEnabledCheckBox.Checked += (s, e) => EnableInjection();
        InjectionEnabledCheckBox.Unchecked += (s, e) => DisableInjection();
        AddDefectButton.Click += (s, e) => AddDefect_Click();
        ClearButton.Click += (s, e) => ClearAllDefects_Click();

        PixelIdInput.TextChanged += (s, e) => SyncFromPixelId();
        XCoordInput.TextChanged += (s, e) => SyncFromXY();
        YCoordInput.TextChanged += (s, e) => SyncFromXY();
        SaveButton.Click += (s, e) => SaveDefects_Click();
        OpenButton.Click += (s, e) => OpenDefects_Click();
        PreviewImage.MouseDown += PreviewImage_MouseDown;

        FullscreenContent.RenderTransformOrigin = new Point(0, 0);
        FullscreenContent.RenderTransform = new TransformGroup { Children = { m_fsScale, m_fsPan } };
        FullscreenImageHost.PreviewMouseWheel += Fs_MouseWheel;
        FullscreenImageHost.MouseLeftButtonDown += Fs_MouseLeftButtonDown;
        FullscreenImageHost.MouseLeftButtonUp += Fs_MouseLeftButtonUp;
        FullscreenImageHost.MouseRightButtonDown += Fs_MouseRightButtonDown;
        FullscreenImageHost.MouseRightButtonUp += Fs_MouseRightButtonUp;
        FullscreenImageHost.MouseMove += Fs_MouseMove;

        RefreshUI();
    }

    private void PreviewImage_MouseDown(object sender, MouseButtonEventArgs e)
    {
        ShowFullscreenPreview();
    }

    private void SyncFromPixelId()
    {
        if (m_suppressCoordSync)
            return;
        if (!int.TryParse(PixelIdInput.Text, out int pixelIdDisplay)
            || !DefectPixelUiMath.TryGetCoordinatesFromDisplayId(pixelIdDisplay, PreviewW, PreviewH, out int x, out int y))
            return;
        m_suppressCoordSync = true;
        try
        {
            XCoordInput.Text = x.ToString(CultureInfo.InvariantCulture);
            YCoordInput.Text = y.ToString(CultureInfo.InvariantCulture);
        }
        finally
        {
            m_suppressCoordSync = false;
        }
    }

    private void SyncFromXY()
    {
        if (m_suppressCoordSync)
            return;
        if (!int.TryParse(XCoordInput.Text, out int x)
            || !int.TryParse(YCoordInput.Text, out int y)
            || !DefectPixelUiMath.TryGetDisplayId(x, y, PreviewW, PreviewH, out int pixelIdDisplay))
            return;
        m_suppressCoordSync = true;
        try
        {
            PixelIdInput.Text = pixelIdDisplay.ToString(CultureInfo.InvariantCulture);
        }
        finally
        {
            m_suppressCoordSync = false;
        }
    }

    /// <summary>Update all UI elements with the current state.</summary>
    private void RefreshUI()
    {
        InjectionEnabledCheckBox.IsChecked = m_store.InjectionEnabled;
        StatusText.Text = m_store.InjectionEnabled ? "[Enabled - sent to SmartVisio Box]" : "[Disabled]";
        StatusText.Foreground = m_store.InjectionEnabled
            ? new SolidColorBrush(Colors.DarkGreen)
            : new SolidColorBrush(Colors.DarkRed);

        RefreshDefectsList();
        RenderPreview();

        ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString(CultureInfo.InvariantCulture);
    }

    private void RefreshDefectsList()
    {
        DefectListPanel.Children.Clear();

        var defects = m_store.GetActiveDefects().OrderBy(d => d.PixelId0).ToList();
        if (defects.Count == 0)
        {
            DefectListPanel.Children.Add(new TextBlock
            {
                Text = "(No active defects)",
                Foreground = new SolidColorBrush(Colors.Gray),
                Padding = new Thickness(5),
            });
            return;
        }

        foreach (var defect in defects)
            DefectListPanel.Children.Add(CreateDefectRow(defect));
    }

    /// <summary>Create a single defect row: Seg pair, X, Y, Pixel_ID (blue), Type, Remove (trash).</summary>
    private Border CreateDefectRow(NichiaDefectEntry defect)
    {
        var panel = new StackPanel { Orientation = Orientation.Horizontal, Height = 30 };

        panel.Children.Add(new TextBlock { Width = 70, Text = defect.SegmentPairLabel, VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });
        panel.Children.Add(new TextBlock { Width = 70, Text = defect.X.ToString(CultureInfo.InvariantCulture), VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });
        panel.Children.Add(new TextBlock { Width = 70, Text = defect.Y.ToString(CultureInfo.InvariantCulture), VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });

        panel.Children.Add(new TextBlock
        {
            Width = 90,
            Text = defect.PixelIdDisplay.ToString(CultureInfo.InvariantCulture),
            VerticalAlignment = VerticalAlignment.Center,
            Padding = new Thickness(3),
            Foreground = new SolidColorBrush(Colors.DarkBlue),
        });

        panel.Children.Add(new TextBlock
        {
            Width = 100,
            Text = defect.DefectType.ToString(),
            VerticalAlignment = VerticalAlignment.Center,
            Padding = new Thickness(3),
        });

        var removeButton = new Button
        {
            Width = 50,
            Content = "🗑️",
            FontSize = 14,
            Background = new SolidColorBrush(Colors.Transparent),
            Foreground = new SolidColorBrush(Color.FromRgb(45, 60, 95)),
            Padding = new Thickness(3),
            Margin = new Thickness(3),
            Tag = defect.PixelId0,
            Cursor = Cursors.Hand,
            ToolTip = "Remove defect",
        };
        removeButton.Click += (s, e) => RemoveDefect_Click((int)removeButton.Tag);
        panel.Children.Add(removeButton);

        return new Border
        {
            Child = panel,
            BorderBrush = new SolidColorBrush(Color.FromRgb(200, 200, 200)),
            BorderThickness = new Thickness(0, 0, 0, 1),
        };
    }

    private void EnableInjection()
    {
        m_store.InjectionEnabled = true;
        DiagnosticLogger.Log("[NichiaDefectControlWindow] Injection enabled");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private void DisableInjection()
    {
        m_store.InjectionEnabled = false;
        DiagnosticLogger.Log("[NichiaDefectControlWindow] Injection disabled");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private void AddDefect_Click()
    {
        try
        {
            if (!int.TryParse(XCoordInput.Text, out int x))
                throw new FormatException("X coordinate must be an integer");
            if (!int.TryParse(YCoordInput.Text, out int y))
                throw new FormatException("Y coordinate must be an integer");

            var defectType = ReadAddDefectSelection();

            m_store.AddDefect(new NichiaDefectEntry(x, y, defectType));

            PixelIdInput.Clear();
            XCoordInput.Clear();
            YCoordInput.Clear();
            ShowAddStatus("\u2713 Defect added", Colors.Green);

            RefreshUI();
            DefectStateChanged?.Invoke();

            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            timer.Tick += (s, e) => { ClearAddStatus(); timer.Stop(); };
            timer.Start();
        }
        catch (Exception ex)
        {
            ShowAddStatus($"\u2717 Error: {ex.Message}", Colors.Red);
            DiagnosticLogger.Log($"[NichiaDefectControlWindow] Error adding defect: {ex.Message}");
        }
    }

    /// <summary>Shows the transient Add-Defect status in the Info bar (toggling the static text).</summary>
    private void ShowAddStatus(string text, Color color)
    {
        AddStatusText.Text = text;
        AddStatusText.Foreground = new SolidColorBrush(color);
        AddStatusText.Visibility = Visibility.Visible;
        InjectionInfoText.Visibility = Visibility.Collapsed;
    }

    private void ClearAddStatus()
    {
        AddStatusText.Text = string.Empty;
        AddStatusText.Visibility = Visibility.Collapsed;
        InjectionInfoText.Visibility = Visibility.Visible;
    }

    private void RemoveDefect_Click(int pixelId0)
    {
        m_store.RemoveDefect(pixelId0);
        DiagnosticLogger.Log($"[NichiaDefectControlWindow] Defect removed: pixelId0={pixelId0}");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private void ClearAllDefects_Click()
    {
        if (m_store.GetActiveDefects().Count == 0)
            return;

        m_store.ClearAllDefects();
        DiagnosticLogger.Log("[NichiaDefectControlWindow] All defects cleared");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private static string GetDefectsDirectory()
    {
        string? root = RecordingManager.FindRepoRootWithDocs(AppContext.BaseDirectory)
                       ?? RecordingManager.FindRepoRootWithDocs(System.IO.Directory.GetCurrentDirectory());
        string baseDir = root ?? System.IO.Directory.GetCurrentDirectory();
        string outDir = System.IO.Path.Combine(baseDir, "docs", "outputs", "nichiaDefects");
        System.IO.Directory.CreateDirectory(outDir);
        return outDir;
    }

    private void SaveDefects_Click()
    {
        var defects = m_store.GetActiveDefects();
        if (defects.Count == 0)
        {
            MessageBox.Show("No defects to save.", "Save Defects", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        string dir = GetDefectsDirectory();
        string defaultName = $"Nichia_Defect_Pixels_{DateTime.Now:yyyyMMdd_HHmmss}.csv";

        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Save NICHIA defect list",
            Filter = "CSV file (*.csv)|*.csv",
            InitialDirectory = dir,
            FileName = defaultName,
        };

        if (dlg.ShowDialog(this) != true)
            return;

        try
        {
            var ordered = defects.OrderBy(d => d.PixelId0).ToList();
            using var sw = new System.IO.StreamWriter(dlg.FileName, false, System.Text.Encoding.UTF8);
            sw.WriteLine("X,Y,PixelId0,PixelIdDisplay,Type,SegPair");
            foreach (var d in ordered)
            {
                sw.WriteLine(string.Join(",",
                    d.X.ToString(CultureInfo.InvariantCulture),
                    d.Y.ToString(CultureInfo.InvariantCulture),
                    d.PixelId0.ToString(CultureInfo.InvariantCulture),
                    d.PixelIdDisplay.ToString(CultureInfo.InvariantCulture),
                    d.DefectType.ToString(),
                    d.SegmentPairLabel));
            }

            DiagnosticLogger.Log($"[NichiaDefectControlWindow] Saved {ordered.Count} defects to {dlg.FileName}");
            ShowAddStatus($"\u2713 Saved {ordered.Count} defects", Colors.Green);

            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            timer.Tick += (s, e) => { ClearAddStatus(); timer.Stop(); };
            timer.Start();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Save failed: {ex.Message}", "Save Defects", MessageBoxButton.OK, MessageBoxImage.Error);
            DiagnosticLogger.Log($"[NichiaDefectControlWindow] Save failed: {ex.Message}");
        }
    }

    private void OpenDefects_Click()
    {
        string dir = GetDefectsDirectory();

        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Open NICHIA defect list",
            Filter = "CSV file (*.csv)|*.csv|All files (*.*)|*.*",
            InitialDirectory = dir,
        };

        if (dlg.ShowDialog(this) != true)
            return;

        try
        {
            var loaded = ParseDefectsCsv(dlg.FileName);
            if (loaded.Count == 0)
            {
                MessageBox.Show("No valid defects found in the file.", "Open Defects", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            m_store.ClearAllDefects();
            foreach (var d in loaded)
                m_store.AddDefect(d);

            DiagnosticLogger.Log($"[NichiaDefectControlWindow] Loaded {loaded.Count} defects from {dlg.FileName}");
            RefreshUI();
            DefectStateChanged?.Invoke();
            ShowAddStatus($"\u2713 Loaded {loaded.Count} defects", Colors.Green);

            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            timer.Tick += (s, e) => { ClearAddStatus(); timer.Stop(); };
            timer.Start();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Load failed: {ex.Message}", "Open Defects", MessageBoxButton.OK, MessageBoxImage.Error);
            DiagnosticLogger.Log($"[NichiaDefectControlWindow] Load failed: {ex.Message}");
        }
    }

    private static List<NichiaDefectEntry> ParseDefectsCsv(string path)
    {
        var list = new List<NichiaDefectEntry>();
        foreach (string raw in System.IO.File.ReadLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("//"))
                continue;

            string[] parts = line.Split(',');
            if (parts.Length < 3)
                continue;

            // Skip the header row (X is non-numeric there).
            if (!int.TryParse(parts[0], out int x))
                continue;
            if (!int.TryParse(parts[1], out int y))
                continue;
            if (x < 0 || x > NichiaDefectEntry.MaxX || y < 0 || y > NichiaDefectEntry.MaxY)
                continue;

            // Type column: prefer the named column (index 4), fall back to Dark.
            var type = NichiaDefectType.Dark;
            if (parts.Length > 4 && parts[4].Trim().Equals("Bright", StringComparison.OrdinalIgnoreCase))
                type = NichiaDefectType.Bright;

            list.Add(new NichiaDefectEntry(x, y, type));
        }
        return list;
    }

    private void InitPreviewBitmap()
    {
        m_gridBitmap = new WriteableBitmap(PreviewW, PreviewH, 96, 96, PixelFormats.Bgra32, null);
        PreviewImage.Source = m_gridBitmap;
        FullscreenImage.Source = m_gridBitmap;
    }

    /// <summary>
    /// Renders a single static frame into the native 256x64 bitmap: grey background plus
    /// the active defects as single pixels (Dark = black, Bright = white), matching the
    /// OSRAM preview. The same bitmap feeds both the small and the fullscreen preview.
    /// </summary>
    private void RenderPreview()
    {
        if (m_gridBitmap == null)
            InitPreviewBitmap();

        byte[] px = new byte[PreviewW * PreviewH * 4];

        const byte bg = 110;   // background grey (same as OSRAM)
        for (int i = 0; i < px.Length; i += 4)
        {
            px[i] = bg; px[i + 1] = bg; px[i + 2] = bg; px[i + 3] = 0xFF;
        }

        foreach (var defect in m_store.GetActiveDefects())
        {
            if (defect.X < 0 || defect.X >= PreviewW || defect.Y < 0 || defect.Y >= PreviewH)
                continue;

            byte c = defect.DefectType == NichiaDefectType.Bright ? (byte)0xFF : (byte)0x00;
            int idx = (defect.Y * PreviewW + defect.X) * 4;
            px[idx] = c; px[idx + 1] = c; px[idx + 2] = c; px[idx + 3] = 0xFF;
        }

        m_gridBitmap!.Lock();
        m_gridBitmap.WritePixels(new Int32Rect(0, 0, PreviewW, PreviewH), px, PreviewW * 4, 0);
        m_gridBitmap.Unlock();
    }

    private void ShowFullscreenPreview()
    {
        RenderPreview();
        ResetFullscreenZoom();
        FullscreenOverlay.Visibility = Visibility.Visible;
    }

    private void HideFullscreenPreview()
    {
        FullscreenOverlay.Visibility = Visibility.Collapsed;
    }

    private void ResetFullscreenZoom()
    {
        m_fsScale.ScaleX = m_fsScale.ScaleY = 1.0;
        m_fsPan.X = m_fsPan.Y = 0.0;
    }

    private void Fs_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        e.Handled = true;

        double oldScale = m_fsScale.ScaleX;
        double factor = e.Delta > 0 ? ZoomFactor : (1.0 / ZoomFactor);
        double newScale = Math.Clamp(oldScale * factor, MinZoom, MaxZoom);
        if (Math.Abs(newScale - oldScale) < 1e-9)
            return;

        Point p = e.GetPosition(FullscreenImageHost);
        double localX = (p.X - m_fsPan.X) / oldScale;
        double localY = (p.Y - m_fsPan.Y) / oldScale;
        m_fsPan.X = p.X - localX * newScale;
        m_fsPan.Y = p.Y - localY * newScale;
        m_fsScale.ScaleX = m_fsScale.ScaleY = newScale;

        if (Math.Abs(newScale - 1.0) < 1e-6)
        {
            m_fsPan.X = m_fsPan.Y = 0.0;
        }
    }

    private void Fs_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        bool ctrl = (Keyboard.Modifiers & ModifierKeys.Control) != 0;

        if (ctrl)
        {
            m_fsPainting = true;
            m_fsStrokeChanged = false;
            FullscreenImageHost.CaptureMouse();
            if (TryGetPixel(e, out int sx, out int sy))
                PaintAt(sx, sy);
            return;
        }

        if (e.ClickCount >= 2)
        {
            HideFullscreenPreview();
            return;
        }

        if (m_fsScale.ScaleX > 1.0 + 1e-6)
        {
            m_fsPanning = true;
            m_fsPanStart = e.GetPosition(this);
            m_fsPanStartX = m_fsPan.X;
            m_fsPanStartY = m_fsPan.Y;
            FullscreenImageHost.CaptureMouse();
        }
    }

    private void Fs_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (m_fsPainting)
        {
            m_fsPainting = false;
            FullscreenImageHost.ReleaseMouseCapture();
            EndStroke();
            return;
        }

        if (m_fsPanning)
        {
            m_fsPanning = false;
            FullscreenImageHost.ReleaseMouseCapture();
        }
    }

    private void Fs_MouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        if ((Keyboard.Modifiers & ModifierKeys.Control) == 0)
            return;

        e.Handled = true;
        m_fsErasing = true;
        m_fsStrokeChanged = false;
        FullscreenImageHost.CaptureMouse();
        if (TryGetPixel(e, out int sx, out int sy))
            EraseAt(sx, sy);
    }

    private void Fs_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (!m_fsErasing)
            return;
        e.Handled = true;
        m_fsErasing = false;
        FullscreenImageHost.ReleaseMouseCapture();
        EndStroke();
    }

    private void Fs_MouseMove(object sender, MouseEventArgs e)
    {
        if (m_fsPanning)
        {
            Point cur = e.GetPosition(this);
            Vector d = cur - m_fsPanStart;
            m_fsPan.X = m_fsPanStartX + d.X;
            m_fsPan.Y = m_fsPanStartY + d.Y;
        }
        else if (m_fsPainting)
        {
            if (TryGetPixel(e, out int sx, out int sy))
                PaintAt(sx, sy);
        }
        else if (m_fsErasing)
        {
            if (TryGetPixel(e, out int sx, out int sy))
                EraseAt(sx, sy);
        }

        if (FullscreenImage.ActualWidth > 0 && FullscreenImage.ActualHeight > 0)
        {
            Point pos = e.GetPosition(FullscreenImage);
            int ix = Math.Clamp((int)(pos.X / FullscreenImage.ActualWidth * PreviewW), 0, PreviewW - 1);
            int iy = Math.Clamp((int)(pos.Y / FullscreenImage.ActualHeight * PreviewH), 0, PreviewH - 1);
            FullscreenInfoText.Text = $"x={ix}, y={iy}, Pixel_ID={iy * PreviewW + ix + 1}";
        }
    }

    /// <summary>
    /// Maps the cursor to a logical 256x64 pixel. Returns false if the cursor is outside
    /// the image bounds (so paint/erase strokes ignore off-image movement).
    /// </summary>
    private bool TryGetPixel(MouseEventArgs e, out int sx, out int sy)
    {
        sx = 0;
        sy = 0;
        double w = FullscreenImage.ActualWidth;
        double h = FullscreenImage.ActualHeight;
        if (w <= 0 || h <= 0)
            return false;

        Point pos = e.GetPosition(FullscreenImage);
        if (pos.X < 0 || pos.Y < 0 || pos.X >= w || pos.Y >= h)
            return false;

        sx = Math.Clamp((int)(pos.X / w * PreviewW), 0, PreviewW - 1);
        sy = Math.Clamp((int)(pos.Y / h * PreviewH), 0, PreviewH - 1);
        return true;
    }

    /// <summary>Paint one pixel as a defect using the current Add Defect type selection.</summary>
    private void PaintAt(int sx, int sy)
    {
        var type = ReadAddDefectSelection();
        m_store.AddDefect(new NichiaDefectEntry(sx, sy, type));
        m_fsStrokeChanged = true;

        RenderPreview();
        ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Erase the defect at one pixel, if present.</summary>
    private void EraseAt(int sx, int sy)
    {
        int pixelId0 = sy * PreviewW + sx;
        if (m_store.RemoveDefect(pixelId0))
        {
            m_fsStrokeChanged = true;
            RenderPreview();
            ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString(CultureInfo.InvariantCulture);
        }
    }

    /// <summary>Finalize a paint/erase stroke: rebuild the list and notify the host.</summary>
    private void EndStroke()
    {
        if (!m_fsStrokeChanged)
            return;
        m_fsStrokeChanged = false;
        RefreshDefectsList();
        DefectStateChanged?.Invoke();
    }

    /// <summary>Reads the Defect Type currently selected in the Add Defect form.</summary>
    private NichiaDefectType ReadAddDefectSelection()
    {
        if (DefectTypeCombo.SelectedItem is ComboBoxItem di &&
            di.Tag is string tag && tag == "1")
            return NichiaDefectType.Bright;
        return NichiaDefectType.Dark;
    }
}
