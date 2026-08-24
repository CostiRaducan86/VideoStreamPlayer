using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System;

namespace VilsSharpX
{
    public partial class FlickerControlWindow : Window
    {
        public event EventHandler<RoutedEventArgs>? FlickerLogColumnHeaderClicked;
        public event RoutedEventHandler? FlickerLogCopyRequested;
        public event RoutedEventHandler? FlickerLogClearRequested;
        public event RoutedEventHandler? FlickerLogSaveRequested;
        public event RoutedEventHandler? FlickerFolderOpenRequested;

        public FlickerControlWindow()
        {
            InitializeComponent();
        }

        public Border FlickerStatusBadge => StatusBadge;
        public TextBlock FlickerStatusHeaderText => StatusHeaderText;
        public Button FlickerInjectButton { get; set; } = null!;
        public ToggleButton FlickerPolarityToggle { get; set; } = null!;
        public ToggleButton FlickerEnableToggle { get; set; } = null!;
        public TextBlock FlickerEnableStateText => (TextBlock)FindName("EnableStateText");
        public Button FlickerClearLogButton => ClearFlickerLogButton;
        public Button FlickerSaveLogButton => SaveFlickerLogButton;
        public Button FlickerOpenFolderButton => OpenFlickerFolderButton;
        public TextBlock FlickerInfoText => InfoText;
        public ListView FlickerEventLogView => FlickerEventLog;
        public Grid FlickerInjectionControlsHost => InjectionControlsHost;
        public Grid FlickerEnableControlHost => (Grid)FindName("EnableControlHost");
        private void FlickerEventLog_ColumnHeaderClick(object sender, RoutedEventArgs e)
            => FlickerLogColumnHeaderClicked?.Invoke(sender, e);

        private void FlickerEventLog_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            if (e.Key == System.Windows.Input.Key.C &&
                (System.Windows.Input.Keyboard.Modifiers & System.Windows.Input.ModifierKeys.Control) != 0)
            {
                FlickerLogCopyRequested?.Invoke(sender, e);
                e.Handled = true;
            }
        }

        private void FlickerEventLog_PreviewMouseRightButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.OriginalSource is not DependencyObject source)
                return;

            if (ItemsControl.ContainerFromElement(FlickerEventLog, source) is not ListViewItem item)
                return;

            item.IsSelected = true;
        }

        private void CopyFlickerLogMenuItem_Click(object sender, RoutedEventArgs e)
            => FlickerLogCopyRequested?.Invoke(sender, e);

        private void ClearFlickerLogButton_Click(object sender, RoutedEventArgs e)
            => FlickerLogClearRequested?.Invoke(sender, e);

        private void SaveFlickerLogButton_Click(object sender, RoutedEventArgs e)
            => FlickerLogSaveRequested?.Invoke(sender, e);

        private void OpenFlickerFolderButton_Click(object sender, RoutedEventArgs e)
            => FlickerFolderOpenRequested?.Invoke(sender, e);
    }
}
