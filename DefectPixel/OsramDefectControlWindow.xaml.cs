using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace VilsSharpX.DefectPixel;

/// <summary>
/// WPF window for defining OSRAM defect pixels.
///
/// This window only DEFINES defects and toggles injection. The actual
/// ELEDERP/ELEDERS injection is performed by the Aurix firmware; the host
/// (MainWindow) pushes the list to Aurix on every <see cref="DefectStateChanged"/>.
/// </summary>
public partial class OsramDefectControlWindow : Window
{
    // OSRAM active display resolution. The preview bitmap is native 320x80
    // (one bitmap pixel per logical pixel). It is displayed with Stretch=Fill +
    // NearestNeighbor so it autoscales exactly like pane B, and a separate vector
    // grid overlay draws the subtle per-pixel borders that scale with the image.
    private const int PreviewW = 320;
    private const int PreviewH = 80;

    private readonly OsramDefectStore m_store;
    private WriteableBitmap? m_gridBitmap;

    // Fullscreen zoom/pan transforms. Applied to the FullscreenContent container so
    // the image and its grid overlay zoom/pan together, exactly like pane B.
    // Interaction model (fullscreen):
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

    /// <summary>
    /// Raised whenever the injection enable state or the active defect list changes.
    /// The host uses this to push the updated list to the Aurix firmware.
    /// </summary>
    public event Action? DefectStateChanged;

    public OsramDefectControlWindow(OsramDefectStore store)
    {
        InitializeComponent();

        m_store = store ?? throw new ArgumentNullException(nameof(store));

        InjectionEnabledCheckBox.Checked += (s, e) => EnableInjection();
        InjectionEnabledCheckBox.Unchecked += (s, e) => DisableInjection();
        AddDefectButton.Click += (s, e) => AddDefect_Click();
        ClearButton.Click += (s, e) => ClearAllDefects_Click();

        // Keep Pixel_ID and X/Y in sync automatically (guarded against re-entrancy).
        PixelIdInput.TextChanged += (s, e) => SyncFromPixelId();
        XCoordInput.TextChanged += (s, e) => SyncFromXY();
        YCoordInput.TextChanged += (s, e) => SyncFromXY();
        SaveButton.Click += (s, e) => SaveDefects_Click();
        OpenButton.Click += (s, e) => OpenDefects_Click();
        PreviewImage.MouseDown += PreviewImage_MouseDown;

        // Fullscreen preview: zoom/pan transform on the content container + handlers.
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
        // Single click opens the fullscreen preview.
        ShowFullscreenPreview();
    }

    // Guards the two-way coordinate sync so that programmatic updates do not
    // re-trigger the opposite handler and cause infinite recursion.
    private bool m_suppressCoordSync;

    /// <summary>
    /// When a valid 1-based Pixel_ID is entered, compute and fill X and Y.
    /// The Slot is assigned automatically at Add time, not here.
    /// </summary>
    private void SyncFromPixelId()
    {
        if (m_suppressCoordSync)
            return;
        if (!int.TryParse(PixelIdInput.Text, out int pixelIdDisplay))
            return;
        if (pixelIdDisplay < 1 || pixelIdDisplay > PreviewW * PreviewH)
            return;

        int pixelId0 = pixelIdDisplay - 1;
        int x = pixelId0 % PreviewW;
        int y = pixelId0 / PreviewW;

        m_suppressCoordSync = true;
        try
        {
            XCoordInput.Text = x.ToString();
            YCoordInput.Text = y.ToString();
        }
        finally
        {
            m_suppressCoordSync = false;
        }
    }

    /// <summary>
    /// When valid X and Y are entered, compute and fill the 1-based Pixel_ID.
    /// The Slot is assigned automatically at Add time, not here.
    /// </summary>
    private void SyncFromXY()
    {
        if (m_suppressCoordSync)
            return;
        if (!int.TryParse(XCoordInput.Text, out int x) || x < 0 || x >= PreviewW)
            return;
        if (!int.TryParse(YCoordInput.Text, out int y) || y < 0 || y >= PreviewH)
            return;

        int pixelIdDisplay = y * PreviewW + x + 1;

        m_suppressCoordSync = true;
        try
        {
            PixelIdInput.Text = pixelIdDisplay.ToString();
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
        StatusText.Text = m_store.InjectionEnabled ? "[Enabled - sent to Aurix]" : "[Disabled]";
        StatusText.Foreground = m_store.InjectionEnabled
            ? new SolidColorBrush(Colors.DarkGreen)
            : new SolidColorBrush(Colors.DarkRed);

        RefreshDefectsList();
        RenderPreview();

        ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString();
    }

    /// <summary>Rebuild the active defects list view.</summary>
    private void RefreshDefectsList()
    {
        DefectListPanel.Children.Clear();

        var defects = m_store.GetActiveDefects();
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

    /// <summary>Create a single defect row.</summary>
    private Border CreateDefectRow(OsramDefectEntry defect)
    {
        var panel = new StackPanel { Orientation = Orientation.Horizontal, Height = 30 };

        panel.Children.Add(new TextBlock { Width = 60, Text = defect.Slot.ToString(), VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });
        panel.Children.Add(new TextBlock { Width = 70, Text = defect.X.ToString(), VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });
        panel.Children.Add(new TextBlock { Width = 70, Text = defect.Y.ToString(), VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(3) });

        panel.Children.Add(new TextBlock
        {
            Width = 90,
            Text = $"{defect.PixelIdDisplay}",
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

        panel.Children.Add(new TextBlock
        {
            Width = 70,
            Text = defect.PxState == 1 ? "ON" : "OFF",
            VerticalAlignment = VerticalAlignment.Center,
            Padding = new Thickness(3),
            Foreground = defect.PxState == 1 ? new SolidColorBrush(Colors.DarkGreen) : new SolidColorBrush(Colors.Gray),
        });

        // Remove button with trash icon (dark blue-grey)
        var removeButton = new Button
        {
            Width = 50,
            Content = "🗑️",  // Trash icon
            FontSize = 14,
            Background = new SolidColorBrush(Colors.Transparent),
            Foreground = new SolidColorBrush(Color.FromRgb(45, 60, 95)),
            Padding = new Thickness(3),
            Margin = new Thickness(3),
            Tag = defect.PixelId0,
            Cursor = Cursors.Hand,
            ToolTip = "Remove defect"
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
        DiagnosticLogger.Log("[OsramDefectControlWindow] Injection enabled");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private void DisableInjection()
    {
        m_store.InjectionEnabled = false;
        DiagnosticLogger.Log("[OsramDefectControlWindow] Injection disabled");
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

            if (DefectTypeCombo.SelectedItem is not ComboBoxItem defectItem)
                throw new InvalidOperationException("Defect type not selected");
            if (!int.TryParse(defectItem.Tag?.ToString(), out int defectTypeVal))
                throw new FormatException("Invalid defect type");

            if (ExpectedStateCombo.SelectedItem is not ComboBoxItem stateItem)
                throw new InvalidOperationException("Expected state not selected");
            if (!int.TryParse(stateItem.Tag?.ToString(), out int pxState))
                throw new FormatException("Invalid expected state");

            var defectType = (OsramDefectType)defectTypeVal;
            int pixelId0 = y * 320 + x;

            // Slot is assigned automatically: reuse the slot if this pixel is already
            // a defect, otherwise take the next free slot (0 when the list is empty).
            var existing = m_store.GetActiveDefects().Find(d => d.PixelId0 == pixelId0);
            int slot = existing?.Slot ?? NextFreeSlot();
            if (slot > 63)
                throw new InvalidOperationException("Defect table full (max 64 slots)");

            var entry = new OsramDefectEntry(slot, x, y, pixelId0, pxState, defectType);

            m_store.AddDefect(entry);

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
            DiagnosticLogger.Log($"[OsramDefectControlWindow] Error adding defect: {ex.Message}");
        }
    }

    /// <summary>
    /// Shows the transient Add-Defect status in the Info bar, temporarily hiding the
    /// static "Injection is performed..." text (toggle between the two).
    /// </summary>
    private void ShowAddStatus(string text, Color color)
    {
        AddStatusText.Text = text;
        AddStatusText.Foreground = new SolidColorBrush(color);
        AddStatusText.Visibility = Visibility.Visible;
        InjectionInfoText.Visibility = Visibility.Collapsed;
    }

    /// <summary>Restores the static injection info text in the Info bar.</summary>
    private void ClearAddStatus()
    {
        AddStatusText.Text = string.Empty;
        AddStatusText.Visibility = Visibility.Collapsed;
        InjectionInfoText.Visibility = Visibility.Visible;
    }

    private void RemoveDefect_Click(int pixelId0)
    {
        m_store.RemoveDefect(pixelId0);
        DiagnosticLogger.Log($"[OsramDefectControlWindow] Defect removed: pixelId0={pixelId0}");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    /// <summary>Clears the whole defect list and the preview/fullscreen preview.</summary>
    private void ClearAllDefects_Click()
    {
        if (m_store.GetActiveDefects().Count == 0)
            return;

        m_store.ClearAllDefects();
        DiagnosticLogger.Log("[OsramDefectControlWindow] All defects cleared");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }

    private static string GetDefectsDirectory()
    {
        string? root = RecordingManager.FindRepoRootWithDocs(AppContext.BaseDirectory)
                       ?? RecordingManager.FindRepoRootWithDocs(System.IO.Directory.GetCurrentDirectory());
        string baseDir = root ?? System.IO.Directory.GetCurrentDirectory();
        string outDir = System.IO.Path.Combine(baseDir, "docs", "outputs", "osramDefects");
        System.IO.Directory.CreateDirectory(outDir);
        return outDir;
    }

    /// <summary>Saves the current defect list to a timestamped .csv file.</summary>
    private void SaveDefects_Click()
    {
        var defects = m_store.GetActiveDefects();
        if (defects.Count == 0)
        {
            MessageBox.Show("No defects to save.", "Save Defects", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        string dir = GetDefectsDirectory();
        string defaultName = $"Osram_Defect_Pixels_{DateTime.Now:yyyyMMdd_HHmmss}.csv";

        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Save OSRAM defect list",
            Filter = "CSV file (*.csv)|*.csv",
            InitialDirectory = dir,
            FileName = defaultName,
        };

        if (dlg.ShowDialog(this) != true)
            return;

        try
        {
            var ordered = defects.OrderBy(d => d.Slot).ToList();
            using var sw = new System.IO.StreamWriter(dlg.FileName, false, System.Text.Encoding.UTF8);
            sw.WriteLine("Slot,X,Y,PixelId0,PixelIdDisplay,PxState,DefectType");
            foreach (var d in ordered)
            {
                sw.WriteLine(string.Join(",",
                    d.Slot.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    d.X.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    d.Y.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    d.PixelId0.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    d.PixelIdDisplay.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    d.PxState.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    ((int)d.DefectType).ToString(System.Globalization.CultureInfo.InvariantCulture)));
            }

            DiagnosticLogger.Log($"[OsramDefectControlWindow] Saved {ordered.Count} defects to {dlg.FileName}");
            ShowAddStatus($"\u2713 Saved {ordered.Count} defects", Colors.Green);

            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            timer.Tick += (s, e) => { ClearAddStatus(); timer.Stop(); };
            timer.Start();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Save failed: {ex.Message}", "Save Defects", MessageBoxButton.OK, MessageBoxImage.Error);
            DiagnosticLogger.Log($"[OsramDefectControlWindow] Save failed: {ex.Message}");
        }
    }

    /// <summary>Loads a defect list from a .csv file (replaces the current list).</summary>
    private void OpenDefects_Click()
    {
        string dir = GetDefectsDirectory();

        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Open OSRAM defect list",
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

            DiagnosticLogger.Log($"[OsramDefectControlWindow] Loaded {loaded.Count} defects from {dlg.FileName}");
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
            DiagnosticLogger.Log($"[OsramDefectControlWindow] Load failed: {ex.Message}");
        }
    }

    private static System.Collections.Generic.List<OsramDefectEntry> ParseDefectsCsv(string path)
    {
        var list = new System.Collections.Generic.List<OsramDefectEntry>();
        foreach (string raw in System.IO.File.ReadLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("//"))
                continue;

            string[] parts = line.Split(',');
            if (parts.Length < 7)
                continue;

            // Skip header row.
            if (!int.TryParse(parts[0], out int slot))
                continue;
            if (!int.TryParse(parts[1], out int x)) continue;
            if (!int.TryParse(parts[2], out int y)) continue;
            if (!int.TryParse(parts[5], out int pxState)) continue;
            if (!int.TryParse(parts[6], out int defectTypeVal)) continue;

            if (x < 0 || x > 319 || y < 0 || y > 79 || slot < 0 || slot > 63)
                continue;

            int pixelId0 = y * PreviewW + x;
            list.Add(new OsramDefectEntry(slot, x, y, pixelId0, pxState, (OsramDefectType)defectTypeVal));
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
    /// Renders a single static frame (no cyclic rendering) into the native 320x80
    /// bitmap: grey background plus the active defects as single pixels. The image
    /// is displayed with Stretch=Fill + NearestNeighbor, so each logical pixel is
    /// upscaled into a full, always-visible cell (fixes tiny defects vanishing on
    /// downscale). The subtle per-pixel grid is a separate vector overlay.
    /// Called only when the defect list changes; the same bitmap feeds both the
    /// small preview and the fullscreen preview.
    /// </summary>
    private void RenderPreview()
    {
        if (m_gridBitmap == null)
            InitPreviewBitmap();

        byte[] px = new byte[PreviewW * PreviewH * 4];

        const byte bg = 110;   // background grey
        for (int i = 0; i < px.Length; i += 4)
        {
            px[i] = bg; px[i + 1] = bg; px[i + 2] = bg; px[i + 3] = 0xFF;
        }

        // Draw each defect as a single native pixel (upscaled to a full cell).
        foreach (var defect in m_store.GetActiveDefects())
        {
            if (defect.X < 0 || defect.X >= PreviewW || defect.Y < 0 || defect.Y >= PreviewH)
                continue;

            byte c = defect.DefectType switch
            {
                OsramDefectType.Open => 0,        // Black
                OsramDefectType.ShortToGnd => 0,  // Black
                OsramDefectType.Stuck => 0xFF,    // White
                _ => 0x80,                        // Grey
            };

            int idx = (defect.Y * PreviewW + defect.X) * 4;
            px[idx] = c; px[idx + 1] = c; px[idx + 2] = c; px[idx + 3] = 0xFF;
        }

        m_gridBitmap!.Lock();
        m_gridBitmap.WritePixels(new Int32Rect(0, 0, PreviewW, PreviewH), px, PreviewW * 4, 0);
        m_gridBitmap.Unlock();
    }

    private void ShowFullscreenPreview()
    {
        RenderPreview();          // ensure latest frame
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

    /// <summary>Scroll zooms around the cursor (same behavior as pane B).</summary>
    private void Fs_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        e.Handled = true;

        double oldScale = m_fsScale.ScaleX;
        double factor = e.Delta > 0 ? ZoomFactor : (1.0 / ZoomFactor);
        double newScale = Math.Clamp(oldScale * factor, MinZoom, MaxZoom);
        if (Math.Abs(newScale - oldScale) < 1e-9)
            return;

        // Zoom around the cursor position (relative to the fixed host).
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

    /// <summary>
    /// Left button: Ctrl paints the pixel under the cursor (pen), otherwise a plain
    /// drag pans while zoomed in, and a plain double-click closes fullscreen.
    /// </summary>
    private void Fs_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        bool ctrl = (Keyboard.Modifiers & ModifierKeys.Control) != 0;

        if (ctrl)
        {
            // Start a paint stroke.
            m_fsPainting = true;
            m_fsStrokeChanged = false;
            FullscreenImageHost.CaptureMouse();
            if (TryGetPixel(e, out int sx, out int sy))
                PaintAt(sx, sy);
            return;
        }

        // Plain double-click closes fullscreen.
        if (e.ClickCount >= 2)
        {
            HideFullscreenPreview();
            return;
        }

        // Plain single left-drag pans, but only when zoomed in.
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

    /// <summary>Ctrl + right button erases the pixel under the cursor (eraser).</summary>
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

        // Update pixel info (coordinates are in the image's own space, so they are
        // correct regardless of the current zoom/pan transform).
        if (FullscreenImage.ActualWidth > 0 && FullscreenImage.ActualHeight > 0)
        {
            Point pos = e.GetPosition(FullscreenImage);
            int ix = Math.Clamp((int)(pos.X / FullscreenImage.ActualWidth * PreviewW), 0, PreviewW - 1);
            int iy = Math.Clamp((int)(pos.Y / FullscreenImage.ActualHeight * PreviewH), 0, PreviewH - 1);
            FullscreenInfoText.Text = $"x={ix}, y={iy}, Pixel_ID={iy * PreviewW + ix + 1}";
        }
    }

    /// <summary>
    /// Maps the cursor to a logical 320x80 pixel. Returns false if the cursor is
    /// outside the image bounds (so paint/erase strokes ignore off-image movement).
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

    /// <summary>Paint one pixel as a defect using the current Add Defect selection.</summary>
    private void PaintAt(int sx, int sy)
    {
        int pixelId0 = sy * PreviewW + sx;
        var (defectType, pxState) = ReadAddDefectSelection();

        // Reuse the slot if this pixel is already a defect; otherwise take the next
        // free slot (0-based, incrementing from the highest used slot).
        var existing = m_store.GetActiveDefects().Find(d => d.PixelId0 == pixelId0);
        int slot = existing?.Slot ?? NextFreeSlot();
        if (slot > 63)
            return; // defect table full (slots 0..63)

        var entry = new OsramDefectEntry(slot, sx, sy, pixelId0, pxState, defectType);
        m_store.AddDefect(entry);
        m_fsStrokeChanged = true;

        RenderPreview();
        ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString();
    }

    /// <summary>Erase the defect at one pixel, if present.</summary>
    private void EraseAt(int sx, int sy)
    {
        int pixelId0 = sy * PreviewW + sx;
        if (m_store.RemoveDefect(pixelId0))
        {
            m_fsStrokeChanged = true;
            RenderPreview();
            ActiveDefectsCountText.Text = m_store.GetActiveDefects().Count.ToString();
        }
    }

    /// <summary>Finalize a paint/erase stroke: rebuild the list and notify Aurix.</summary>
    private void EndStroke()
    {
        if (!m_fsStrokeChanged)
            return;
        m_fsStrokeChanged = false;
        RefreshDefectsList();
        DefectStateChanged?.Invoke();
    }

    /// <summary>Smallest unused slot index (max used + 1, or 0 when the list is empty).</summary>
    private int NextFreeSlot()
    {
        int max = -1;
        foreach (var d in m_store.GetActiveDefects())
        {
            if (d.Slot > max)
                max = d.Slot;
        }
        return max + 1;
    }

    /// <summary>Reads the Defect Type + Expected State currently selected in the form.</summary>
    private (OsramDefectType defectType, int pxState) ReadAddDefectSelection()
    {
        OsramDefectType defectType = OsramDefectType.Open;
        if (DefectTypeCombo.SelectedItem is ComboBoxItem di
            && int.TryParse(di.Tag?.ToString(), out int dv))
        {
            defectType = (OsramDefectType)dv;
        }

        int pxState = 1;
        if (ExpectedStateCombo.SelectedItem is ComboBoxItem si
            && int.TryParse(si.Tag?.ToString(), out int sv))
        {
            pxState = sv;
        }

        return (defectType, pxState);
    }
}
