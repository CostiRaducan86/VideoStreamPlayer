using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

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
    private readonly OsramDefectStore m_store;

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

        RefreshUI();
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
            Text = $"{defect.PixelIdDisplay} (0:{defect.PixelId0})",
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

        panel.Children.Add(new TextBlock
        {
            Width = 80,
            Text = defect.DarkVisibleCandidate ? "YES" : "NO",
            VerticalAlignment = VerticalAlignment.Center,
            Padding = new Thickness(3),
            Foreground = defect.DarkVisibleCandidate ? new SolidColorBrush(Colors.Red) : new SolidColorBrush(Colors.Gray),
        });

        var removeButton = new Button
        {
            Width = 80,
            Content = "Remove",
            Background = new SolidColorBrush(Color.FromRgb(255, 100, 100)),
            Foreground = new SolidColorBrush(Colors.White),
            Padding = new Thickness(5),
            Margin = new Thickness(3),
            Tag = defect.PixelId0,
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
            if (!int.TryParse(SlotInput.Text, out int slot))
                throw new FormatException("Slot must be an integer");

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
            var entry = new OsramDefectEntry(slot, x, y, pixelId0, pxState, defectType);

            m_store.AddDefect(entry);

            XCoordInput.Clear();
            YCoordInput.Clear();
            SlotInput.Clear();
            AddStatusText.Text = "\u2713 Defect added";
            AddStatusText.Foreground = new SolidColorBrush(Colors.Green);

            RefreshUI();
            DefectStateChanged?.Invoke();

            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            timer.Tick += (s, e) => { AddStatusText.Text = ""; timer.Stop(); };
            timer.Start();
        }
        catch (Exception ex)
        {
            AddStatusText.Text = $"\u2717 Error: {ex.Message}";
            AddStatusText.Foreground = new SolidColorBrush(Colors.Red);
            DiagnosticLogger.Log($"[OsramDefectControlWindow] Error adding defect: {ex.Message}");
        }
    }

    private void RemoveDefect_Click(int pixelId0)
    {
        m_store.RemoveDefect(pixelId0);
        DiagnosticLogger.Log($"[OsramDefectControlWindow] Defect removed: pixelId0={pixelId0}");
        RefreshUI();
        DefectStateChanged?.Invoke();
    }
}
