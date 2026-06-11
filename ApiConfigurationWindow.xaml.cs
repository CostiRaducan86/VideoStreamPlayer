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
    public Action<bool, bool, string, int, string, string[]>? OnSettingsSaved { get; set; }

    public ApiConfigurationWindow()
    {
        InitializeComponent();
        LoadSettings();
    }

    private void LoadSettings()
    {
        var settings = AppSettingsStore.LoadOrDefault();

        ChkAllowRemote.IsChecked = settings.ApiAllowRemote;
        ChkEnableHttps.IsChecked = settings.ApiEnableHttps;
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
            MessageBox.Show("Enter a single IP (e.g., 10.168.55.149)\nor an IP range (e.g., 10.168.55.149-151).",
                "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        // Validate and normalize
        string? displayEntry = ValidateIpEntry(input);
        if (displayEntry == null)
        {
            MessageBox.Show("Invalid format. Accepted:\n" +
                "• Single IP: 10.168.55.149\n" +
                "• IP range: 10.168.55.149-151",
                "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_cidrs.Contains(displayEntry))
        {
            MessageBox.Show($"\"{displayEntry}\" is already in the list.",
                "Duplicate", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        _cidrs.Add(displayEntry);
        TxtNewCidr.Clear();
    }

    /// <summary>
    /// Validates the user input and returns the display string to store.
    /// Returns null if invalid. Accepted formats:
    /// - Single IP: "10.168.55.149" (stored as-is)
    /// - IP range: "10.168.55.149-151" (stored as-is, expanded at runtime)
    /// </summary>
    private static string? ValidateIpEntry(string input)
    {
        // IPv6 address (simple validation)
        if (input.Contains(':'))
            return System.Net.IPAddress.TryParse(input, out _) ? input : null;

        // IP range format: xxx.xxx.xxx.aaa-bbb
        if (input.Contains('-'))
        {
            int dashIdx = input.LastIndexOf('-');
            string ipPart = input[..dashIdx];
            string endStr = input[(dashIdx + 1)..];

            if (!IsValidIpv4(ipPart) || !int.TryParse(endStr, out int endOctet))
                return null;

            var octets = ipPart.Split('.');
            int startOctet = int.Parse(octets[3]);

            if (endOctet < startOctet || endOctet > 255)
                return null;

            return input;  // store as-is: "10.168.55.149-151"
        }

        // Single IP
        if (IsValidIpv4(input))
            return input;

        return null;
    }

    /// <summary>
    /// Expands stored allowlist entries into CIDR strings for the runtime engine.
    /// - "10.168.55.149" → ["10.168.55.149/32"]
    /// - "10.168.55.149-151" → ["10.168.55.149/32", "10.168.55.150/32", "10.168.55.151/32"]
    /// </summary>
    public static string[] ExpandAllowlistForRuntime(string[] storedEntries)
    {
        var result = new System.Collections.Generic.List<string>();
        foreach (var entry in storedEntries)
        {
            if (string.IsNullOrWhiteSpace(entry)) continue;

            if (entry.Contains('-'))
            {
                // Range: expand to individual /32 entries
                int dashIdx = entry.LastIndexOf('-');
                string ipPart = entry[..dashIdx];
                string endStr = entry[(dashIdx + 1)..];
                if (IsValidIpv4(ipPart) && int.TryParse(endStr, out int endOctet))
                {
                    var octets = ipPart.Split('.');
                    int startOctet = int.Parse(octets[3]);
                    string basePrefix = $"{octets[0]}.{octets[1]}.{octets[2]}";
                    for (int i = startOctet; i <= endOctet; i++)
                        result.Add($"{basePrefix}.{i}/32");
                }
            }
            else if (entry.Contains('/'))
            {
                // Already has CIDR prefix (legacy entries)
                result.Add(entry);
            }
            else
            {
                // Single IP → /32
                result.Add($"{entry}/32");
            }
        }
        return [.. result];
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
            bool enableHttps = ChkEnableHttps.IsChecked ?? false;
            string bindAddress = TxtBindAddress.Text?.Trim() ?? "127.0.0.1";
            string[] cidrs = [.. _cidrs];

            settings.ApiAllowRemote = allowRemote;
            settings.ApiEnableHttps = enableHttps;
            settings.ApiBindAddress = bindAddress;
            settings.ApiPort = port;
            settings.ApiKey = _currentApiKey;
            settings.ApiAllowedCidrs = cidrs;

            AppSettingsStore.Save(settings);

            // Notify parent MainWindow to update its in-memory API fields
            OnSettingsSaved?.Invoke(allowRemote, enableHttps, bindAddress, port, _currentApiKey, cidrs);

            DiagnosticLogger.Log(
                $"[api] Settings saved. Remote={allowRemote}, HTTPS={enableHttps}, " +
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
