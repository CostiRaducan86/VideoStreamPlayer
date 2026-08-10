using System;
using System.Windows;
using System.Windows.Media;

namespace VilsSharpX;

public partial class CommunicationFaultControlWindow : Window
{
    private readonly CommunicationFaultState _state;

    public event Action? FaultStateChanged;

    public bool IsAvtpFaultAvailable
    {
        get => AvtpFaultCheckBox.IsEnabled;
        set
        {
            AvtpFaultCheckBox.IsEnabled = value;
            AvtpFaultCheckBox.Foreground = value
                ? new SolidColorBrush(Colors.Black)
                : new SolidColorBrush(Colors.Gray);
            AvtpFaultCheckBox.Opacity = value ? 1.0 : 0.75;
            if (!value && _state.AvtpFaultEnabled)
            {
                _state.AvtpFaultEnabled = false;
                RefreshUi();
                FaultStateChanged?.Invoke();
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

        AvtpFaultCheckBox.Checked += (_, _) => SetAvtpFault(true);
        AvtpFaultCheckBox.Unchecked += (_, _) => SetAvtpFault(false);

        RefreshUi();
    }

    private void SetAvtpFault(bool enabled)
    {
        _state.AvtpFaultEnabled = enabled;
        RefreshUi();
        FaultStateChanged?.Invoke();
    }

    private void RefreshUi()
    {
        AvtpFaultCheckBox.IsChecked = _state.AvtpFaultEnabled;
        AvtpStatusText.Text = !AvtpFaultCheckBox.IsEnabled
            ? "[Generator mode only]"
            : _state.AvtpFaultEnabled ? "[Enabled]" : "[Disabled]";
        AvtpStatusText.Foreground = _state.AvtpFaultEnabled
            ? new SolidColorBrush(Colors.DarkGreen)
            : new SolidColorBrush(Colors.DarkRed);
    }
}