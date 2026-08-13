using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace VilsSharpX;

public partial class CommunicationFaultControlWindow : Window
{
    private readonly CommunicationFaultState _state;
    private readonly DispatcherTimer _avtpFaultTimer;
    private readonly DispatcherTimer _lvdsFaultTimer;
    private readonly DispatcherTimer _canUartFaultTimer;
    private readonly Stopwatch _avtpFaultStopwatch = new();
    private readonly Stopwatch _lvdsFaultStopwatch = new();
    private readonly Stopwatch _canUartFaultStopwatch = new();
    private int _avtpFaultDurationMilliseconds;
    private int _lvdsFaultDurationMilliseconds;
    private int _canUartFaultDurationMilliseconds;

    public event Action? FaultStateChanged;
    public event Action? LvdsFaultStateChanged;
    public event Action? CanUartFaultStateChanged;

    public int CanUartMode
    {
        get => _canUartMode;
        set
        {
            _canUartMode = Math.Clamp(value, 0, 2);
            _state.CanUartMode = _canUartMode;
            ApplyCanUartControlConstraints();
            RefreshUi();
        }
    }

    private int _canUartMode;

    public bool IsAvtpFaultAvailable
    {
        get => AvtpInjectButton.IsEnabled;
        set
        {
            AvtpInjectButton.IsEnabled = value;
            AvtpDurationTextBox.IsEnabled = value;
            AvtpDurationUpButton.IsEnabled = value && !_state.AvtpFaultEnabled;
            AvtpDurationDownButton.IsEnabled = value && !_state.AvtpFaultEnabled;
            AvtpDurationLabel.Foreground = value ? new SolidColorBrush(Colors.Black) : new SolidColorBrush(Colors.Gray);
            AvtpDurationUnit.Foreground = value ? new SolidColorBrush(Colors.Black) : new SolidColorBrush(Colors.Gray);
            if (!value && _state.AvtpFaultEnabled)
            {
                StopAvtpFault();
            }
            else
            {
                RefreshUi();
            }
        }
    }

    public bool IsCanUartFaultAvailable
    {
        get => CanUartInjectButton.IsEnabled;
        set
        {
            CanUartInjectButton.IsEnabled = value;
            CanUartStopButton.IsEnabled = value;
            CanUartFaultModeComboBox.IsEnabled = value && !_state.CanUartFaultEnabled;
            CanUartFaultDirectionComboBox.IsEnabled = value && !_state.CanUartFaultEnabled;
            CanUartDurationTextBox.IsEnabled = value && !_state.CanUartFaultEnabled;
            CanUartDurationUpButton.IsEnabled = value && !_state.CanUartFaultEnabled;
            CanUartDurationDownButton.IsEnabled = value && !_state.CanUartFaultEnabled;
            if (!value && _state.CanUartFaultEnabled)
                StopCanUartFault();
            else
                RefreshUi();
        }
    }

    public bool IsLvdsFaultAvailable
    {
        get => LvdsInjectButton.IsEnabled;
        set
        {
            LvdsInjectButton.IsEnabled = value;
            LvdsStopButton.IsEnabled = value;
            LvdsDurationTextBox.IsEnabled = value && !_state.LvdsFaultEnabled;
            LvdsDurationUpButton.IsEnabled = value && !_state.LvdsFaultEnabled;
            LvdsDurationDownButton.IsEnabled = value && !_state.LvdsFaultEnabled;
            LvdsDurationLabel.Foreground = value ? new SolidColorBrush(Colors.Black) : new SolidColorBrush(Colors.Gray);
            LvdsDurationUnit.Foreground = value ? new SolidColorBrush(Colors.Black) : new SolidColorBrush(Colors.Gray);
            if (!value && _state.LvdsFaultEnabled)
                StopLvdsFault();
            else
                RefreshUi();
        }
    }

    public CommunicationFaultControlWindow(CommunicationFaultState state)
    {
        InitializeComponent();
        _state = state ?? throw new ArgumentNullException(nameof(state));
        CanUartFaultModeComboBox.Items.Add(new ComboBoxItem { Content = "DROP forwarding" });
        CanUartFaultModeComboBox.Items.Add(new ComboBoxItem { Content = "RELAY_BYPASS" });
        CanUartFaultDirectionComboBox.ItemsSource = new[] { "Both directions", "ECU -> LSM", "LSM -> ECU" };
        CanUartFaultModeComboBox.SelectionChanged += (_, _) =>
        {
            ApplyCanUartControlConstraints();
            RefreshUi();
        };
        _avtpFaultTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(50)
        };
        _avtpFaultTimer.Tick += AvtpFaultTimer_Tick;
        _lvdsFaultTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(50)
        };
        _lvdsFaultTimer.Tick += LvdsFaultTimer_Tick;
        _canUartFaultTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(50)
        };
        _canUartFaultTimer.Tick += CanUartFaultTimer_Tick;

        AvtpInjectButton.Click += AvtpInjectButton_Click;
        AvtpStopButton.Click += (_, _) => StopAvtpFault();
        AvtpDurationUpButton.Click += (_, _) => ChangeDuration(100);
        AvtpDurationDownButton.Click += (_, _) => ChangeDuration(-100);
        LvdsInjectButton.Click += LvdsInjectButton_Click;
        LvdsStopButton.Click += (_, _) => StopLvdsFault();
        LvdsDurationUpButton.Click += (_, _) => ChangeLvdsDuration(100);
        LvdsDurationDownButton.Click += (_, _) => ChangeLvdsDuration(-100);
        CanUartInjectButton.Click += CanUartInjectButton_Click;
        CanUartStopButton.Click += (_, _) => StopCanUartFault();
        CanUartDurationUpButton.Click += (_, _) => ChangeCanUartDuration(100);
        CanUartDurationDownButton.Click += (_, _) => ChangeCanUartDuration(-100);
        Closed += (_, _) => StopAvtpFault(notify: false);
        Closed += (_, _) => StopLvdsFault(notify: true);
        Closed += (_, _) => StopCanUartFault(notify: true);

        RefreshUi();
    }

    private void AvtpInjectButton_Click(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(AvtpDurationTextBox.Text, out int durationMilliseconds)
            || durationMilliseconds < 0 || durationMilliseconds > 60000)
        {
            FaultInfoFaultText.Foreground = new SolidColorBrush(Colors.DarkRed);
            FaultInfoFaultText.Text = "AVTP fault duration must be between 0 and 60000 ms.";
            return;
        }

        _avtpFaultDurationMilliseconds = durationMilliseconds;
        _state.AvtpFaultEnabled = true;
        _avtpFaultStopwatch.Restart();
        if (durationMilliseconds > 0)
            _avtpFaultTimer.Start();
        RefreshUi();
        FaultStateChanged?.Invoke();
    }

    private void AvtpFaultTimer_Tick(object? sender, EventArgs e)
    {
        int remainingMilliseconds = Math.Max(0, _avtpFaultDurationMilliseconds - (int)_avtpFaultStopwatch.ElapsedMilliseconds);
        if (remainingMilliseconds == 0)
        {
            StopAvtpFault();
            return;
        }

        AvtpCountdownText.Text = $"{remainingMilliseconds} ms remaining";
    }

    private void ChangeDuration(int deltaMilliseconds)
    {
        int currentDuration = int.TryParse(AvtpDurationTextBox.Text, out int parsedDuration) ? parsedDuration : 2000;
        int newDuration = Math.Clamp(currentDuration + deltaMilliseconds, 0, 60000);
        AvtpDurationTextBox.Text = newDuration.ToString();
    }

    private void CanUartInjectButton_Click(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(CanUartDurationTextBox.Text, out int durationMilliseconds)
            || durationMilliseconds < 0 || durationMilliseconds > 60000)
        {
            SetCanUartInfo("Fault duration must be between 0 and 60000 ms.", isError: true);
            return;
        }

        _canUartFaultDurationMilliseconds = durationMilliseconds;
        _state.CanUartFaultMode = CanUartFaultModeComboBox.SelectedIndex == 1 ? 2 : 1;
        _state.CanUartFaultDirection = Math.Clamp(CanUartFaultDirectionComboBox.SelectedIndex, 0, 2);
        _state.CanUartFaultDurationMilliseconds = durationMilliseconds;
        _state.CanUartFaultEnabled = true;
        RefreshUi();
        CanUartFaultStateChanged?.Invoke();
        if (!_state.CanUartFaultEnabled)
            return;

        _canUartFaultStopwatch.Restart();
        if (durationMilliseconds > 0)
            _canUartFaultTimer.Start();
    }

    private void LvdsInjectButton_Click(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(LvdsDurationTextBox.Text, out int durationMilliseconds)
            || durationMilliseconds < 0 || durationMilliseconds > 60000)
        {
            SetLvdsInfo("LVDS fault duration must be between 0 and 60000 ms.", isError: true);
            return;
        }

        _lvdsFaultDurationMilliseconds = durationMilliseconds;
        _state.LvdsFaultDurationMilliseconds = durationMilliseconds;
        _state.LvdsFaultEnabled = true;
        RefreshUi();
        LvdsFaultStateChanged?.Invoke();
        if (!_state.LvdsFaultEnabled)
            return;

        _lvdsFaultStopwatch.Restart();
        if (durationMilliseconds > 0)
            _lvdsFaultTimer.Start();
    }

    private void LvdsFaultTimer_Tick(object? sender, EventArgs e)
    {
        int remainingMilliseconds = Math.Max(0, _lvdsFaultDurationMilliseconds - (int)_lvdsFaultStopwatch.ElapsedMilliseconds);
        LvdsCountdownText.Text = $"{remainingMilliseconds} ms remaining";
        if (remainingMilliseconds == 0)
            StopLvdsFault();
    }

    private void ChangeLvdsDuration(int deltaMilliseconds)
    {
        int currentDuration = int.TryParse(LvdsDurationTextBox.Text, out int parsedDuration)
            ? parsedDuration : 2000;
        int newDuration = Math.Clamp(currentDuration + deltaMilliseconds, 0, 60000);
        LvdsDurationTextBox.Text = newDuration.ToString();
    }

    private void CanUartFaultTimer_Tick(object? sender, EventArgs e)
    {
        int remainingMilliseconds = Math.Max(0, _canUartFaultDurationMilliseconds - (int)_canUartFaultStopwatch.ElapsedMilliseconds);
        CanUartCountdownText.Text = $"{remainingMilliseconds} ms remaining";
        if (remainingMilliseconds == 0)
            StopCanUartFault();
    }

    private void ChangeCanUartDuration(int deltaMilliseconds)
    {
        int currentDuration = int.TryParse(CanUartDurationTextBox.Text, out int parsedDuration)
            ? parsedDuration : 2000;
        int newDuration = Math.Clamp(currentDuration + deltaMilliseconds, 0, 60000);
        CanUartDurationTextBox.Text = newDuration.ToString();
    }

    private void StopAvtpFault(bool notify = true)
    {
        _avtpFaultTimer.Stop();
        _avtpFaultStopwatch.Reset();
        bool wasActive = _state.AvtpFaultEnabled;
        _state.AvtpFaultEnabled = false;
        RefreshUi();
        if (notify && wasActive)
            FaultStateChanged?.Invoke();
    }

    private void StopCanUartFault(bool notify = true)
    {
        _canUartFaultTimer.Stop();
        _canUartFaultStopwatch.Reset();
        bool wasActive = _state.CanUartFaultEnabled;
        _state.CanUartFaultEnabled = false;
        RefreshUi();
        if (notify && wasActive)
            CanUartFaultStateChanged?.Invoke();
    }

    private void StopLvdsFault(bool notify = true)
    {
        _lvdsFaultTimer.Stop();
        _lvdsFaultStopwatch.Reset();
        bool wasActive = _state.LvdsFaultEnabled;
        _state.LvdsFaultEnabled = false;
        RefreshUi();
        if (notify && wasActive)
            LvdsFaultStateChanged?.Invoke();
    }

    public void ClearLvdsFaultForExternalChange(bool notify = true)
    {
        StopLvdsFault(notify);
    }

    public void ClearCanUartFaultForExternalChange(bool notify = true)
    {
        StopCanUartFault(notify);
    }

    private void SetLvdsInfo(string message, bool isError)
    {
        FaultInfoFaultText.Foreground = new SolidColorBrush(isError ? Colors.DarkRed : Colors.Gray);
        FaultInfoFaultText.Text = message;
        FaultInfoConstraintText.Text = "LVDS monitor Ethernet remains active and unmodified.";
    }

    private void SetCanUartInfo(string message, bool isError)
    {
        FaultInfoAvtpText.Text = string.Empty;
        FaultInfoFaultText.Foreground = new SolidColorBrush(isError ? Colors.DarkRed : Colors.Gray);
        FaultInfoFaultText.Text = message;
        FaultInfoConstraintText.Text = string.Empty;
    }

    private void RefreshUi()
    {
        bool active = _state.AvtpFaultEnabled;
        bool available = AvtpInjectButton.IsEnabled;
        AvtpInjectButton.Visibility = active ? Visibility.Collapsed : Visibility.Visible;
        AvtpStopButton.Visibility = active ? Visibility.Visible : Visibility.Collapsed;
        AvtpDurationTextBox.IsEnabled = available && !active;
        AvtpDurationUpButton.IsEnabled = available && !active;
        AvtpDurationDownButton.IsEnabled = available && !active;
        bool durationEnabled = available && !active;
        AvtpDurationBorder.IsEnabled = durationEnabled;
        AvtpDurationBorder.Opacity = durationEnabled ? 1.0 : 0.55;
        AvtpCountdownText.Visibility = active ? Visibility.Visible : Visibility.Collapsed;

        bool canActive = _state.CanUartFaultEnabled;
        bool canAvailable = CanUartInjectButton.IsEnabled;
        CanUartInjectButton.Visibility = canActive ? Visibility.Collapsed : Visibility.Visible;
        CanUartStopButton.Visibility = canActive ? Visibility.Visible : Visibility.Collapsed;
        CanUartFaultModeComboBox.IsEnabled = canAvailable && !canActive;
        CanUartFaultDirectionComboBox.IsEnabled = canAvailable && !canActive;
        CanUartDurationTextBox.IsEnabled = canAvailable && !canActive;
        CanUartDurationUpButton.IsEnabled = canAvailable && !canActive;
        CanUartDurationDownButton.IsEnabled = canAvailable && !canActive;
        CanUartDurationBorder.IsEnabled = canAvailable && !canActive;
        CanUartDurationBorder.Opacity = canAvailable && !canActive ? 1.0 : 0.55;

        CanUartStatusBadge.Background = canActive
            ? new SolidColorBrush(Color.FromRgb(244, 204, 204))
            : new SolidColorBrush(canAvailable ? Color.FromRgb(217, 234, 211) : Color.FromRgb(224, 224, 224));
        CanUartStatusText.Foreground = canActive
            ? new SolidColorBrush(Colors.DarkRed)
            : new SolidColorBrush(canAvailable ? Color.FromRgb(46, 107, 46) : Colors.Gray);
        CanUartStatusText.Text = canActive ? "● ACTIVE" : canAvailable ? "● READY" : "● UNAVAILABLE";
        CanUartCountdownText.Visibility = canActive ? Visibility.Visible : Visibility.Collapsed;
        CanUartCountdownText.Text = canActive
            ? _canUartFaultDurationMilliseconds == 0
                ? "PERMANENT"
                : $"{Math.Max(0, _canUartFaultDurationMilliseconds - (int)_canUartFaultStopwatch.ElapsedMilliseconds)} ms remaining"
            : string.Empty;
        ApplyCanUartControlConstraints();

        AvtpStatusBadge.Background = active
            ? new SolidColorBrush(Color.FromRgb(244, 204, 204))
            : new SolidColorBrush(available ? Color.FromRgb(217, 234, 211) : Color.FromRgb(224, 224, 224));
        AvtpStatusText.Foreground = active
            ? new SolidColorBrush(Colors.DarkRed)
            : new SolidColorBrush(available ? Color.FromRgb(46, 107, 46) : Colors.Gray);
        AvtpStatusText.Text = active ? "● ACTIVE" : available ? "● READY" : "● UNAVAILABLE";

        bool lvdsActive = _state.LvdsFaultEnabled;
        bool lvdsAvailable = LvdsInjectButton.IsEnabled;
        LvdsInjectButton.Visibility = lvdsActive ? Visibility.Collapsed : Visibility.Visible;
        LvdsStopButton.Visibility = lvdsActive ? Visibility.Visible : Visibility.Collapsed;
        LvdsDurationTextBox.IsEnabled = lvdsAvailable && !lvdsActive;
        LvdsDurationUpButton.IsEnabled = lvdsAvailable && !lvdsActive;
        LvdsDurationDownButton.IsEnabled = lvdsAvailable && !lvdsActive;
        LvdsDurationBorder.IsEnabled = lvdsAvailable && !lvdsActive;
        LvdsDurationBorder.Opacity = lvdsAvailable && !lvdsActive ? 1.0 : 0.55;
        LvdsCountdownText.Visibility = lvdsActive ? Visibility.Visible : Visibility.Collapsed;
        LvdsCountdownText.Text = lvdsActive
            ? _lvdsFaultDurationMilliseconds == 0
                ? "PERMANENT"
                : $"{Math.Max(0, _lvdsFaultDurationMilliseconds - (int)_lvdsFaultStopwatch.ElapsedMilliseconds)} ms remaining"
            : string.Empty;
        LvdsStatusBadge.Background = lvdsActive
            ? new SolidColorBrush(Color.FromRgb(244, 204, 204))
            : new SolidColorBrush(lvdsAvailable ? Color.FromRgb(217, 234, 211) : Color.FromRgb(224, 224, 224));
        LvdsStatusText.Foreground = lvdsActive
            ? new SolidColorBrush(Colors.DarkRed)
            : new SolidColorBrush(lvdsAvailable ? Color.FromRgb(46, 107, 46) : Colors.Gray);
        LvdsStatusText.Text = lvdsActive ? "● ACTIVE" : lvdsAvailable ? "● READY" : "● UNAVAILABLE";

        SetFaultInfoLines(available, active, lvdsAvailable, lvdsActive, canAvailable, canActive);
    }

    private void ApplyCanUartControlConstraints()
    {
        if (CanUartFaultModeComboBox == null || CanUartFaultDirectionComboBox == null)
            return;

        bool directEcuLsm = _canUartMode == 0;
        if (directEcuLsm && CanUartFaultModeComboBox.SelectedIndex != 1)
            CanUartFaultModeComboBox.SelectedIndex = 1;

        bool faultActive = _state.CanUartFaultEnabled;
        bool canAvailable = CanUartInjectButton.IsEnabled;
        bool relayBypass = CanUartFaultModeComboBox.SelectedIndex == 1;
        CanUartFaultModeComboBox.IsEnabled = canAvailable && !faultActive;
        CanUartFaultDirectionComboBox.IsEnabled = canAvailable && !faultActive && !directEcuLsm && !relayBypass;
        CanUartFaultDirectionComboBox.Opacity = CanUartFaultDirectionComboBox.IsEnabled ? 1.0 : 0.55;

        if (CanUartFaultModeComboBox.Items.Count >= 2)
        {
            ((ComboBoxItem)CanUartFaultModeComboBox.Items[0]).IsEnabled = !directEcuLsm;
            ((ComboBoxItem)CanUartFaultModeComboBox.Items[1]).IsEnabled = true;
        }
    }

    private (string FaultMode, string Constraints) GetCanUartConstraintInfo()
    {
        string faultMode = CanUartFaultModeComboBox.SelectedIndex == 1 ? "RELAY_BYPASS" : "DROP forwarding";
        string constraints = _canUartMode switch
        {
            0 => "CAN-UART mode ECU <-> LSM: RELAY_BYPASS is the only supported fault; DROP forwarding is disabled.",
            1 => "CAN-UART mode ECU <-> SmartVisio <-> LSM: DROP forwarding supports direction selection; RELAY_BYPASS pauses the bridge without changing CAN_SEL.",
            _ => "CAN-UART mode SmartVisio <-> LSM: DROP forwarding supports direction selection; RELAY_BYPASS pauses the bridge."
        };
        return ($"Selected fault: {faultMode}.", constraints);
    }

    private void SetFaultInfoLines(bool avtpAvailable, bool avtpActive,
        bool lvdsAvailable, bool lvdsActive,
        bool canAvailable, bool canActive)
    {
        if (avtpActive || lvdsActive || canActive)
        {
            string activeInfo;
            if (avtpActive)
            {
                activeInfo = _avtpFaultDurationMilliseconds == 0
                    ? "AVTP fault active permanently."
                    : $"AVTP fault active for {_avtpFaultDurationMilliseconds} ms.";
            }
            else if (lvdsActive)
            {
                activeInfo = _lvdsFaultDurationMilliseconds == 0
                    ? "LVDS SELECT_LOCAL_IDLE active permanently."
                    : $"LVDS SELECT_LOCAL_IDLE active for {_lvdsFaultDurationMilliseconds} ms.";
            }
            else
            {
                activeInfo = _state.CanUartFaultMode == 2
                    ? _state.CanUartFaultDurationMilliseconds == 0
                        ? "CAN-UART RELAY_BYPASS active permanently."
                        : $"CAN-UART RELAY_BYPASS active for {_state.CanUartFaultDurationMilliseconds} ms."
                    : _state.CanUartFaultDurationMilliseconds == 0
                        ? "CAN-UART DROP forwarding active permanently."
                        : $"CAN-UART DROP forwarding active for {_state.CanUartFaultDurationMilliseconds} ms.";
            }

            FaultInfoAvtpText.Foreground = new SolidColorBrush(Colors.DarkRed);
            FaultInfoFaultText.Foreground = new SolidColorBrush(Colors.Gray);
            FaultInfoConstraintText.Foreground = new SolidColorBrush(Colors.Gray);
            FaultInfoAvtpText.Text = activeInfo;
            FaultInfoFaultText.Text = " ";
            FaultInfoConstraintText.Text = " ";
            return;
        }

        string avtpInfo = avtpActive
            ? _avtpFaultDurationMilliseconds == 0
                ? "AVTP: active, transmission suspended permanently."
                : $"AVTP: active, transmission suspended for {_avtpFaultDurationMilliseconds} ms."
            : avtpAvailable
                ? "AVTP: ready for fault injection."
                : "AVTP: unavailable outside AVTP Generator mode.";

        string faultInfo;
        if (lvdsAvailable)
        {
            faultInfo = "LVDS: SELECT_LOCAL_IDLE is ready; C# monitoring remains active.";
        }
        else if (canActive)
        {
            faultInfo = _state.CanUartFaultMode == 2
                ? _state.CanUartFaultDurationMilliseconds == 0
                    ? "CAN-UART: RELAY_BYPASS active permanently."
                    : $"CAN-UART: RELAY_BYPASS active for {_state.CanUartFaultDurationMilliseconds} ms."
                : _state.CanUartFaultDurationMilliseconds == 0
                    ? "CAN-UART: DROP forwarding active permanently."
                    : $"CAN-UART: DROP forwarding active for {_state.CanUartFaultDurationMilliseconds} ms.";
        }
        else
        {
            faultInfo = canAvailable
                ? GetCanUartConstraintInfo().FaultMode
                : "CAN-UART: fault injection unavailable.";
        }

        string constraintInfo = lvdsAvailable
            ? "LVDS fault acts through TTL_SEL; the AURIX-to-C# LVDS monitor path is not interrupted."
            : canAvailable
                ? GetCanUartConstraintInfo().Constraints
                : "CAN-UART controls are unavailable.";

        Brush infoBrush = avtpActive || lvdsActive || canActive ? new SolidColorBrush(Colors.DarkRed) : new SolidColorBrush(Colors.Gray);
        FaultInfoAvtpText.Foreground = infoBrush;
        FaultInfoFaultText.Foreground = infoBrush;
        FaultInfoConstraintText.Foreground = new SolidColorBrush(Colors.Gray);
        FaultInfoAvtpText.Text = avtpInfo;
        FaultInfoFaultText.Text = faultInfo;
        FaultInfoConstraintText.Text = constraintInfo;
    }
}