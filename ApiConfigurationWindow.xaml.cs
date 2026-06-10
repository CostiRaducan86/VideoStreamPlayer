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
            EyeSlashLine.Visibility = Visibility.Collapsed;  // no slash = eye open = visible
            BtnCopyKey.Visibility = Visibility.Visible;
        }
        else
        {
            TxtApiKey.Text = _currentApiKey.Length > 0 ? new string('•', _currentApiKey.Length) : string.Empty;
            EyeSlashLine.Visibility = Visibility.Visible;  // slash through eye = hidden
            BtnCopyKey.Visibility = Visibility.Collapsed;
        }
    }

    private void BtnToggleShowPassword_Click(object sender, RoutedEventArgs e)
    {
        _showPassword = !_showPassword;
        UpdatePasswordDisplay();
    }

    private void BtnCopyKey_Click(object sender, RoutedEventArgs e)
    {
        if (!string.IsNullOrEmpty(_currentApiKey))
        {
            Clipboard.SetText(_currentApiKey);
        }
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
        string input = TxtNewCidr.Text?.Trim() ?? string.Empty;

        if (string.IsNullOrWhiteSpace(input))
        {
            MessageBox.Show("Enter a CIDR (10.168.50.0/24), single IP (10.168.55.149),\nor IP range (10.168.55.149/151).", "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        // Expand input into one or more entries to add
        var entries = ExpandIpInput(input);
        if (entries == null)
        {
            MessageBox.Show("Invalid format. Accepted:\n" +
                "• CIDR: 10.168.50.0/24\n" +
                "• Single IP: 10.168.55.149\n" +
                "• IP range: 10.168.55.149/151 (last octet range)",
                "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        foreach (var entry in entries)
        {
            if (!_cidrs.Contains(entry))
                _cidrs.Add(entry);
        }

        TxtNewCidr.Clear();
    }

    /// <summary>
    /// Expands user input into CIDR entries. Supports:
    /// - Standard CIDR: "10.168.50.0/24" (prefix 0-32)
    /// - Single IP: "10.168.55.149" → "10.168.55.149/32"
    /// - IP range: "10.168.55.149/151" → individual /32 entries for .149, .150, .151
    /// Returns null if input is invalid.
    /// </summary>
    private static string[]? ExpandIpInput(string input)
    {
        if (input.Contains(':'))
        {
            // IPv6 CIDR — just validate prefix
            var parts = input.Split('/');
            if (parts.Length == 2 && int.TryParse(parts[1], out int pfx) && pfx >= 0 && pfx <= 128)
                return [input];
            return null;
        }

        if (!input.Contains('/'))
        {
            // Single IP (no slash) — validate it looks like an IPv4 address
            if (IsValidIpv4(input))
                return [$"{input}/32"];
            return null;
        }

        // Has a slash — could be CIDR or IP range
        var slashParts = input.Split('/');
        if (slashParts.Length != 2)
            return null;

        string ipPart = slashParts[0];
        string suffixPart = slashParts[1];

        if (!int.TryParse(suffixPart, out int suffixValue))
            return null;

        // Standard CIDR: prefix is 0-32
        if (suffixValue >= 0 && suffixValue <= 32 && IsValidIpv4(ipPart))
        {
            // Disambiguate: if suffix > last octet, it's likely a range, not a CIDR prefix
            var octets = ipPart.Split('.');
            int lastOctet = int.Parse(octets[3]);

            if (suffixValue > 32)
            {
                // Definitely a range
            }
            else if (suffixValue > lastOctet && suffixValue <= 255)
            {
                // Looks like a range (e.g., 10.168.55.149/151 — 151 > 32 handled above,
                // but 10.168.55.5/8 is ambiguous; treat as CIDR /8 since <= 32)
                // For values > 32, always range. For <= 32, assume CIDR.
                return [input];
            }
            else
            {
                return [input];
            }
        }

        // IP range: suffix is 33-255 (must be > 32 to distinguish from CIDR prefix)
        if (IsValidIpv4(ipPart) && suffixValue > 32 && suffixValue <= 255)
        {
            var octets = ipPart.Split('.');
            int lastOctet = int.Parse(octets[3]);
            string basePrefix = $"{octets[0]}.{octets[1]}.{octets[2]}";

            if (suffixValue < lastOctet)
                return null;  // range end < range start

            var result = new System.Collections.Generic.List<string>();
            for (int i = lastOctet; i <= suffixValue; i++)
                result.Add($"{basePrefix}.{i}/32");

            return [.. result];
        }

        return null;
    }

    private static bool IsValidIpv4(string ip)
    {
        var octets = ip.Split('.');
        if (octets.Length != 4) return false;
        foreach (var o in octets)
        {
            if (!int.TryParse(o, out int v) || v < 0 || v > 255)
                return false;
        }
        return true;
    }

    private void BtnRemoveCidr_Click(object sender, RoutedEventArgs e)
    {
        if (LstCidrs.SelectedItem is string selectedCidr)
            _cidrs.Remove(selectedCidr);
        else
            MessageBox.Show("Please select a CIDR range to remove.", "Selection Required", MessageBoxButton.OK, MessageBoxImage.Information);
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

            MessageBox.Show(
                "API configuration saved successfully.\n\n" +
                "Restart the application for changes to take full effect.",
                "Success",
                MessageBoxButton.OK,
                MessageBoxImage.Information);

            Close();
        }
        catch (Exception ex)
        {
            DiagnosticLogger.Log($"[api] Error saving settings: {ex.Message}");
            MessageBox.Show($"Error saving settings: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
