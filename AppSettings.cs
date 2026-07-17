using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace VilsSharpX;

public sealed class AppSettings
{
    public const int CurrentSettingsVersion = 12;

    public int SettingsVersion { get; set; } = CurrentSettingsVersion;

    public int Fps { get; set; } = 100;
    public int BDelta { get; set; } = 0;
    public int Deadband { get; set; } = 0;
    public bool ZeroZeroIsWhite { get; set; } = false;

    // Dark pixel tools
    public int ForcedDeadPixelId { get; set; } = 0; // 0 disables
    public bool DarkPixelCompensationEnabled { get; set; } = false;

    // Live AVTP capture (Ethernet via Npcap/SharpPcap). When enabled, the app will attempt
    // to sniff ethertype 0x22F0 and reassemble RVF frames.
    public bool AvtpLiveEnabled { get; set; } = true;
    public string? AvtpLiveDeviceHint { get; set; } = null;

    // 0 = player (files/pcap/avi/scene), 1 = live AVTP monitor
    public int ModeOfOperation { get; set; } = 1;

    // AVTP TX MAC addresses (format: "XX:XX:XX:XX:XX:XX")
    public string SrcMac { get; set; } = "3C:CE:15:00:00:19";
    public string DstMac { get; set; } = "01:00:5E:16:00:12";

    // LSM Device Type (0=Osram20, 1=Osram205, 2=Nichia)
    public int LsmDeviceType { get; set; } = 0;

    // ECU Variant (index into the list of known variants, 0-based)
    public int EcuVariant { get; set; } = 0;

    // AVTP header fields
    public int VlanId { get; set; } = 70;
    public int VlanPriority { get; set; } = 5;
    public string AvtpEtherType { get; set; } = "0x22F0";
    public string StreamIdLastByte { get; set; } = "0x50";

    // LVDS Mode (0 = Monitoring, 1 = Generator)
    public int LvdsMode { get; set; } = 0;

    // Control Mode (0 = ECU, 1 = Direct control)
    public int ControlMode { get; set; } = 0;

    // CAN UART Mode (0 = ECU↔LSM, 1 = ECU↔SmartVisio↔LSM, 2 = SmartVisio↔LSM)
    public int CanUartMode { get; set; } = 0;

    // Automation REST API settings
    public bool ApiAllowRemote { get; set; } = false;
    public bool ApiEnableHttps { get; set; } = false;
    public string ApiBindAddress { get; set; } = "127.0.0.1";
    public int ApiPort { get; set; } = 8420;
    [JsonIgnore]
    public string ApiKey { get; set; } = string.Empty;

    public string ApiKeyProtected { get; set; } = string.Empty;
    public string[] ApiAllowedCidrs { get; set; } = [];
}

public static class AppSettingsStore
{
    private const string FileName = "settings.json";
    private static readonly JsonSerializerOptions s_writeIndentedJsonOptions = new() { WriteIndented = true };

    public static string GetSettingsPath()
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VilsSharpX");
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, FileName);
    }

    public static AppSettings LoadOrDefault(string? path = null)
    {
        path ??= GetSettingsPath();
        try
        {
            if (!File.Exists(path)) return new AppSettings();
            var json = File.ReadAllText(path);
            var settings = JsonSerializer.Deserialize<AppSettings>(json) ?? new AppSettings();
            string? legacyApiKeyPlaintext = TryReadLegacyPlaintextApiKey(json);

            bool migrated = false;

            if (settings.SettingsVersion < 2)
            {
                // Migration: enforce current defaults.
                settings.ZeroZeroIsWhite = false;
                settings.DarkPixelCompensationEnabled = false;
                settings.SettingsVersion = 2;
                migrated = true;
            }

            if (settings.SettingsVersion < 3)
            {
                settings.SettingsVersion = 3;
                migrated = true;
            }

            if (settings.SettingsVersion < 4)
            {
                // Migration: CANoe gateway settings were removed.
                settings.SettingsVersion = 4;
                migrated = true;
            }

            if (settings.SettingsVersion < 5)
            {
                // Migration: add MAC address settings with defaults.
                settings.SrcMac = "3C:CE:15:00:00:19";
                settings.DstMac = "01:00:5E:16:00:12";
                settings.SettingsVersion = 5;
                migrated = true;
            }

            if (settings.SettingsVersion < 6)
            {
                // Migration: add LSM device type setting with default (Osram 2.0).
                settings.LsmDeviceType = 0;
                settings.SettingsVersion = 6;
                migrated = true;
            }

            if (settings.SettingsVersion < 7)
            {
                // Migration: add ECU Variant + AVTP header fields with defaults.
                settings.EcuVariant = 0;
                settings.VlanId = 70;
                settings.VlanPriority = 5;
                settings.AvtpEtherType = "0x22F0";
                settings.StreamIdLastByte = "0x50";
                settings.SettingsVersion = 7;
                migrated = true;
            }

            if (settings.SettingsVersion < 8)
            {
                settings.SettingsVersion = 8;
                migrated = true;
            }

            if (settings.SettingsVersion < 9)
            {
                // Migration: add LVDS Mode (default Monitoring).
                settings.LvdsMode = 0;
                settings.SettingsVersion = 9;
                migrated = true;
            }

            if (settings.SettingsVersion < 10)
            {
                // Migration: add Control Mode and CAN UART Mode.
                settings.ControlMode = 0;
                settings.CanUartMode = 0;
                settings.SettingsVersion = 10;
                migrated = true;
            }

            if (settings.SettingsVersion < 11)
            {
                // Migration: add automation REST API settings (safe defaults = localhost only).
                settings.ApiAllowRemote = false;
                settings.ApiBindAddress = "127.0.0.1";
                settings.ApiPort = 8420;
                settings.ApiKeyProtected = string.Empty;
                settings.ApiAllowedCidrs = [];
                settings.SettingsVersion = 11;
                migrated = true;
            }

            if (settings.SettingsVersion < 12)
            {
                // Migration: secure API key at rest with DPAPI + add CIDR allowlist.
                settings.ApiAllowedCidrs ??= [];

                if (!string.IsNullOrWhiteSpace(legacyApiKeyPlaintext))
                {
                    settings.ApiKey = legacyApiKeyPlaintext;
                    settings.ApiKeyProtected = ProtectApiKey(legacyApiKeyPlaintext);
                }

                settings.SettingsVersion = 12;
                migrated = true;
            }

            if (!string.IsNullOrWhiteSpace(settings.ApiKeyProtected))
            {
                settings.ApiKey = UnprotectApiKey(settings.ApiKeyProtected);
            }
            else if (!string.IsNullOrWhiteSpace(legacyApiKeyPlaintext))
            {
                settings.ApiKey = legacyApiKeyPlaintext;
                settings.ApiKeyProtected = ProtectApiKey(legacyApiKeyPlaintext);
                migrated = true;
            }
            else
            {
                settings.ApiKey = string.Empty;
            }

            if (settings.SettingsVersion < AppSettings.CurrentSettingsVersion)
            {
                settings.SettingsVersion = AppSettings.CurrentSettingsVersion;
                migrated = true;
            }

            if (migrated)
            {
                try { Save(settings, path); } catch { /* ignore */ }
            }

            return settings;
        }
        catch
        {
            return new AppSettings();
        }
    }

    public static void Save(AppSettings settings, string? path = null)
    {
        path ??= GetSettingsPath();

        // Persist only encrypted secret at rest.
        settings.ApiKeyProtected = ProtectApiKey(settings.ApiKey);

        var json = JsonSerializer.Serialize(settings, s_writeIndentedJsonOptions);
        File.WriteAllText(path, json);
    }

    private static string ProtectApiKey(string plaintext)
    {
        if (string.IsNullOrWhiteSpace(plaintext))
            return string.Empty;

        try
        {
            byte[] clear = Encoding.UTF8.GetBytes(plaintext);
            byte[] cipher = ProtectedData.Protect(clear, null, DataProtectionScope.CurrentUser);
            return Convert.ToBase64String(cipher);
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string UnprotectApiKey(string protectedValue)
    {
        if (string.IsNullOrWhiteSpace(protectedValue))
            return string.Empty;

        try
        {
            byte[] cipher = Convert.FromBase64String(protectedValue);
            byte[] clear = ProtectedData.Unprotect(cipher, null, DataProtectionScope.CurrentUser);
            return Encoding.UTF8.GetString(clear);
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string? TryReadLegacyPlaintextApiKey(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (!doc.RootElement.TryGetProperty("ApiKey", out var keyElement))
                return null;

            return keyElement.ValueKind == JsonValueKind.String
                ? keyElement.GetString()
                : null;
        }
        catch
        {
            return null;
        }
    }
}
