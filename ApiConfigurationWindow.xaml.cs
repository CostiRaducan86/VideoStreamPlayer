using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Controls;

namespace VilsSharpX;

public partial class ApiConfigurationWindow : Window
{
    private string _currentApiKey = string.Empty;
    private bool _showPassword = false;
    private readonly ObservableCollection<string> _cidrs = [];

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
        
        // Load CIDRs
        if (settings.ApiAllowedCidrs != null)
        {
            foreach (var cidr in settings.ApiAllowedCidrs)
            {
                _cidrs.Add(cidr);
            }
        }
        
        LstCidrs.ItemsSource = _cidrs;
    }

    private void UpdatePasswordDisplay()
    {
        if (_showPassword)
        {
            TxtApiKey.Text = _currentApiKey;
            BtnToggleShowPassword.Content = "🙈";
        }
        else
        {
            TxtApiKey.Text = new string('•', _currentApiKey.Length > 0 ? _currentApiKey.Length : 0);
            BtnToggleShowPassword.Content = "👁️";
        }
    }

    private void BtnToggleShowPassword_Click(object sender, RoutedEventArgs e)
    {
        _showPassword = !_showPassword;
        UpdatePasswordDisplay();
    }

    private void BtnRotateKey_Click(object sender, RoutedEventArgs e)
    {
        var result = MessageBox.Show(
            "This will generate a new API key and invalidate the previous one.\n\n" +
            "Remote clients must be updated with the new token.\n\n" +
            "Continue?",
            "Rotate API Key",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);

        if (result == MessageBoxResult.Yes)
        {
            _currentApiKey = GenerateSecureToken(32);
            UpdatePasswordDisplay();
            DiagnosticLogger.Log("[api] Token rotated. New token will be activated on save.");
        }
    }

    private static string GenerateSecureToken(int length)
    {
        // Genereaza un token criptografic sigur
        byte[] tokenData = new byte[length];
        using (var rng = RandomNumberGenerator.Create())
        {
            rng.GetBytes(tokenData);
        }
        
        // Converteste in format base64url-safe (minus + si / din URL encoding)
        string token = Convert.ToBase64String(tokenData)
            .Replace("+", "-")
            .Replace("/", "_")
            .TrimEnd('=');
        
        return token;
    }

    private void BtnAddCidr_Click(object sender, RoutedEventArgs e)
    {
        string cidr = TxtNewCidr.Text?.Trim() ?? string.Empty;
        
        if (string.IsNullOrWhiteSpace(cidr))
        {
            MessageBox.Show("Please enter a valid CIDR range (e.g., 10.168.50.0/24)", "Validation Error", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        
        if (!ValidateCidr(cidr))
        {
            MessageBox.Show("Invalid CIDR format. Use format: XXX.XXX.XXX.XXX/NN", "Validation Error", MessageBoxButton.OK, MessageBoxImage.Warning);
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
        {
            _cidrs.Remove(selectedCidr);
        }
        else
        {
            MessageBox.Show("Please select a CIDR range to remove.", "Selection Required", MessageBoxButton.OK, MessageBoxImage.Information);
        }
    }

    private static bool ValidateCidr(string cidr)
    {
        // Simplă validare de format: "X.X.X.X/N" sau "::1/128"
        if (string.IsNullOrWhiteSpace(cidr))
            return false;
        
        var parts = cidr.Split('/');
        if (parts.Length != 2)
            return false;
        
        // Verifica prefixul (nr de biti)
        if (!int.TryParse(parts[1], out int prefixLength))
            return false;
        
        // IPv4: /0-32, IPv6: /0-128
        bool isIpv6 = parts[0].Contains(':');
        int maxPrefix = isIpv6 ? 128 : 32;
        
        if (prefixLength < 0 || prefixLength > maxPrefix)
            return false;
        
        return true;
    }

    private void BtnOK_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var settings = AppSettingsStore.LoadOrDefault();
            
            settings.ApiAllowRemote = ChkAllowRemote.IsChecked ?? false;
            settings.ApiBindAddress = TxtBindAddress.Text?.Trim() ?? "127.0.0.1";
            
            if (!int.TryParse(TxtPort.Text?.Trim(), out int port))
            {
                MessageBox.Show("Port must be a valid number.", "Validation Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            
            if (port < 1 || port > 65535)
            {
                MessageBox.Show("Port must be between 1 and 65535.", "Validation Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            
            settings.ApiPort = port;
            settings.ApiKey = _currentApiKey;
            settings.ApiAllowedCidrs = _cidrs.ToArray();
            
            AppSettingsStore.Save(settings);
            
            DiagnosticLogger.Log(
                $"[api] Settings saved. Remote={settings.ApiAllowRemote}, " +
                $"Bind={settings.ApiBindAddress}:{settings.ApiPort}, " +
                $"CIDRs={_cidrs.Count}");
            
            MessageBox.Show(
                "API configuration saved successfully.\n\n" +
                "Restart the application for changes to take full effect.",
                "Success",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
            
            DialogResult = true;
            Close();
        }
        catch (Exception ex)
        {
            DiagnosticLogger.Log($"[api] Error saving settings: {ex.Message}");
            MessageBox.Show($"Error saving settings: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void BtnCancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
