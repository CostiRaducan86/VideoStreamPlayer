using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;

namespace VilsSharpX;

public partial class CommunicationFaultControlWindow : Window
{
    private readonly CommunicationFaultState _state;
    private readonly DispatcherTimer _avtpFaultTimer;
    private readonly Stopwatch _avtpFaultStopwatch = new();
    private int _avtpFaultDurationMilliseconds;

    public event Action? FaultStateChanged;

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

    public CommunicationFaultControlWindow(CommunicationFaultState state)
    {
        InitializeComponent();
        _state = state ?? throw new ArgumentNullException(nameof(state));
        _avtpFaultTimer = new DispatcherTimer();
        _avtpFaultTimer.Interval = TimeSpan.FromMilliseconds(50);
        _avtpFaultTimer.Tick += AvtpFaultTimer_Tick;

        AvtpInjectButton.Click += AvtpInjectButton_Click;
        AvtpStopButton.Click += (_, _) => StopAvtpFault();
        AvtpDurationUpButton.Click += (_, _) => ChangeDuration(100);
        AvtpDurationDownButton.Click += (_, _) => ChangeDuration(-100);
        Closed += (_, _) => StopAvtpFault(notify: false);

        RefreshUi();
    }

    private void AvtpInjectButton_Click(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(AvtpDurationTextBox.Text, out int durationMilliseconds)
            || durationMilliseconds < 0 || durationMilliseconds > 60000)
        {
            FaultInfoText.Foreground = new SolidColorBrush(Colors.DarkRed);
            FaultInfoText.Text = "Fault duration must be between 0 and 60000 ms.";
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

        if (active)
        {
            AvtpStatusBadge.Background = new SolidColorBrush(Color.FromRgb(244, 204, 204));
            AvtpStatusText.Foreground = new SolidColorBrush(Colors.DarkRed);
            AvtpStatusText.Text = "● ACTIVE";
            AvtpCountdownText.Text = _avtpFaultDurationMilliseconds == 0
                ? "PERMANENT"
                : $"{Math.Max(0, _avtpFaultDurationMilliseconds - (int)_avtpFaultStopwatch.ElapsedMilliseconds)} ms remaining";
            FaultInfoText.Foreground = new SolidColorBrush(Colors.DarkRed);
            FaultInfoText.Text = _avtpFaultDurationMilliseconds == 0
                ? "AVTP fault active. Transmission suspended."
                : $"AVTP fault active. Transmission suspended for {_avtpFaultDurationMilliseconds} ms.";
        }
        else if (!available)
        {
            AvtpStatusBadge.Background = new SolidColorBrush(Color.FromRgb(224, 224, 224));
            AvtpStatusText.Foreground = new SolidColorBrush(Colors.Gray);
            AvtpStatusText.Text = "● UNAVAILABLE";
            FaultInfoText.Foreground = new SolidColorBrush(Colors.Gray);
            FaultInfoText.Text = "AVTP fault injection is unavailable outside AVTP Generator mode. LVDS and CAN-UART fault injection are not available in current firmware.";
        }
        else
        {
            AvtpStatusBadge.Background = new SolidColorBrush(Color.FromRgb(217, 234, 211));
            AvtpStatusText.Foreground = new SolidColorBrush(Color.FromRgb(46, 107, 46));
            AvtpStatusText.Text = "● READY";
            FaultInfoText.Foreground = new SolidColorBrush(Colors.Gray);
            FaultInfoText.Text = "No communication fault active.";
        }
    }
}