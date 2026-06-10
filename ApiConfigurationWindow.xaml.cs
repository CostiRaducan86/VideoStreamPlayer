using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Security.Cryptography;
using System.Windows;

namespace VilsSharpX;

public partial class ApiConfigurationWindow : Window
{
    private string _currentApiKey = string.Empty;
    private bool _showPassword = false;
    private readonly ObservableCollection<string> _cidrs = [];

    /// <summary>
    /// Called after successful save so the parent MainWindow can update its in-memory fields
    /// (prevents SaveUiSettings from overwriting the new values on WPF close).
    /// </summary>
    public Action<bool, string, int, string, string[]>? OnSettingsSaved { get; set; }

    public ApiConfigurationWindow()
    {
        InitializeComponent();
        LoadSettings();
    }

    private void LoadSettings()
    {
        var settings = AppSettingsStore.LoadOrDefault();

        ChkAllowRemote.IsChecked = settings.ApiAllowRemote;
        TxtBindAddress.Text = settings.ApiBindAddress ?? "127.0.0.1";
        TxtPort.Text = settings.ApiPort.ToString();

        _currentApiKey = settings.ApiKey ?? string.Empty;
        UpdatePasswordDisplay();

        if (settings.ApiAllowedCidrs != null)
        {
            foreach (var cidr in settings.ApiAllowedCidrs)
                _cidrs.Add(cidr);
        }

        LstCidrs.ItemsSource = _cidrs;
    }

    private void UpdatePasswordDisplay()
    {
        if (_showPassword)
        {
            TxtApiKey.Text = _currentApiKey;
            TxtEyeIcon.Text = "👁";  // open eye = visible
        }
        else
        {
            TxtApiKey.Text = _currentApiKey.Length > 0 ? new string('•', _currentApiKey.Length) : string.Empty;
            TxtEyeIcon.Text = "🚫";  // crossed = hidden (will use Path below)
        }
    }

    private void BtnToggleShowPassword_Click(object sender, RoutedEventArgs e)
    {
        _showPassword = !_showPassword;
        UpdatePasswordDisplay();
    }

    private void BtnGenerateKey_Click(object sender, RoutedEventArgs e)
    {
        var result = MessageBox.Show(
            "This will generate a new API key and invalidate the previous one.\n\n" +
            "Remote clients must be updated with the new token.\n\n" +
            "Continue?",
            "Generate API Key",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);

        if (result == MessageBoxResult.Yes)
        {
            _currentApiKey = GenerateSecureToken(32);
            _showPassword = true;  // show the new key so user can copy it
            UpdatePasswordDisplay();
            DiagnosticLogger.Log("[api] New API key generated. Will be activated on save.");
        }
    }

    private static string GenerateSecureToken(int length)
    {
        byte[] tokenData = new byte[length];
        using (var rng = RandomNumberGenerator.Create())
        {
            rng.GetBytes(tokenData);
        }

        return Convert.ToBase64String(tokenData)
            .Replace("+", "-")
            .Replace("/", "_")
            .TrimEnd('=');
    }

    private void BtnAddCidr_Click(object sender, RoutedEventArgs e)
    {
        string cidr = TxtNewCidr.Text?.Trim() ?? string.Empty;

        if (string.IsNullOrWhiteSpace(cidr))
        {
            MessageBox.Show("Please enter a valid CIDR range (e.g., 10.168.50.0/24)", "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (!ValidateCidr(cidr))
        {
            MessageBox.Show("Invalid CIDR format. Use: XXX.XXX.XXX.XXX/NN (prefix max 32 for IPv4)", "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_cidrs.Contains(cidr))
        {
            MessageBox.Show("This CIDR range is already in the list.", "Duplicate", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        _cidrs.Add(cidr);
        TxtNewCidr.Clear();
    }

    private void BtnRemoveCidr_Click(object sender, RoutedEventArgs e)
    {
        if (LstCidrs.SelectedItem is string selectedCidr)
            _cidrs.Remove(selectedCidr);
        else
            MessageBox.Show("Please select a CIDR range to remove.", "Selection Required", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private static bool ValidateCidr(string cidr)
    {
        if (string.IsNullOrWhiteSpace(cidr))
            return false;

        var parts = cidr.Split('/');
        if (parts.Length != 2)
            return false;

        if (!int.TryParse(parts[1], out int prefixLength))
            return false;

        bool isIpv6 = parts[0].Contains(':');
        int maxPrefix = isIpv6 ? 128 : 32;

        return prefixLength >= 0 && prefixLength <= maxPrefix;
    }

    private void BtnOK_Click(object sender, RoutedEventArgs e)
    {
        // Validate port
        if (!int.TryParse(TxtPort.Text?.Trim(), out int port) || port < 1 || port > 65535)
        {
            MessageBox.Show("Port must be a valid number between 1 and 65535.", "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            var settings = AppSettingsStore.LoadOrDefault();

            bool allowRemote = ChkAllowRemote.IsChecked ?? false;
            string bindAddress = TxtBindAddress.Text?.Trim() ?? "127.0.0.1";
            string[] cidrs = [.. _cidrs];

            settings.ApiAllowRemote = allowRemote;
            settings.ApiBindAddress = bindAddress;
            settings.ApiPort = port;
            settings.ApiKey = _currentApiKey;
            settings.ApiAllowedCidrs = cidrs;

            AppSettingsStore.Save(settings);

            // Notify parent MainWindow to update its in-memory API fields
            OnSettingsSaved?.Invoke(allowRemote, bindAddress, port, _currentApiKey, cidrs);

            DiagnosticLogger.Log(
                $"[api] Settings saved. Remote={allowRemote}, " +
                $"Bind={bindAddress}:{port}, CIDRs={cidrs.Length}");

            Close();
        }
        catch (Exception ex)
        {
            DiagnosticLogger.Log($"[api] Error saving settings: {ex.Message}");
            MessageBox.Show($"Error saving settings: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
