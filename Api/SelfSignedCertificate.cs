using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace VilsSharpX.Api;

/// <summary>
/// Generates and caches a self-signed X.509 certificate for the HTTPS automation API.
/// The .pfx file is stored in the per-user AppData folder alongside settings.json.
/// Regenerated on every application start to ensure all current network IPs are in the SAN.
/// </summary>
internal static class SelfSignedCertificate
{
    private const string CertFileName = "vilssharpx_api.pfx";
    private const string SubjectName = "CN=VilsSharpX Automation API";
    private const int KeySizeRsa = 2048;
    private const int ValidityYears = 5;

    /// <summary>
    /// Always regenerates the certificate to include current machine IPs in SAN.
    /// This ensures that if network adapters change, the cert is always up to date.
    /// </summary>
    public static string GetOrCreateCertificatePath(string bindAddress = "127.0.0.1")
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VilsSharpX");
        Directory.CreateDirectory(dir);
        string pfxPath = Path.Combine(dir, CertFileName);

        // Always regenerate — self-signed cert generation is ~50ms, ensures all current IPs are in SAN.
        GenerateAndSave(pfxPath, bindAddress);
        return pfxPath;
    }

    /// <summary>
    /// Loads the certificate from the .pfx file (regenerated on each app start).
    /// </summary>
    public static X509Certificate2 LoadCertificate(string bindAddress = "127.0.0.1")
    {
        string path = GetOrCreateCertificatePath(bindAddress);
        return new X509Certificate2(path, (string?)null, X509KeyStorageFlags.MachineKeySet | X509KeyStorageFlags.PersistKeySet);
    }

    /// <summary>
    /// Returns the SHA-256 thumbprint of the current certificate (for display in the UI).
    /// Returns null if the certificate doesn't exist yet.
    /// </summary>
    public static string? GetThumbprint()
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VilsSharpX");
        string pfxPath = Path.Combine(dir, CertFileName);

        if (!File.Exists(pfxPath)) return null;

        try
        {
            using var cert = new X509Certificate2(pfxPath);
            return cert.Thumbprint;
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// Forces regeneration of the certificate. Use when the user explicitly requests a new cert.
    /// </summary>
    public static void Regenerate(string bindAddress = "127.0.0.1")
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VilsSharpX");
        Directory.CreateDirectory(dir);
        string pfxPath = Path.Combine(dir, CertFileName);
        GenerateAndSave(pfxPath, bindAddress);
    }

    private static void GenerateAndSave(string pfxPath, string bindAddress)
    {
        using var rsa = RSA.Create(KeySizeRsa);

        var request = new CertificateRequest(SubjectName, rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);

        // Key usage: digital signature + key encipherment (TLS server)
        request.CertificateExtensions.Add(
            new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, critical: false));

        // Enhanced key usage: Server Authentication
        request.CertificateExtensions.Add(
            new X509EnhancedKeyUsageExtension(
                new OidCollection { new("1.3.6.1.5.5.7.3.1") }, // serverAuth
                critical: false));

        // Subject Alternative Names: localhost, loopback, + all machine IPs
        var sanBuilder = new SubjectAlternativeNameBuilder();
        sanBuilder.AddDnsName("localhost");
        sanBuilder.AddIpAddress(IPAddress.Loopback);              // 127.0.0.1
        sanBuilder.AddIpAddress(IPAddress.IPv6Loopback);          // ::1
        sanBuilder.AddDnsName("*.local");

        // Always add ALL non-loopback IPv4 addresses so the cert works regardless of which IP is contacted.
        // Uses NetworkInterface (reliable on multi-homed machines) instead of Dns.GetHostEntry.
        try
        {
            var allIps = NetworkInterface.GetAllNetworkInterfaces()
                .Where(ni => ni.OperationalStatus == OperationalStatus.Up
                          && ni.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                .SelectMany(ni => ni.GetIPProperties().UnicastAddresses)
                .Where(ua => ua.Address.AddressFamily == AddressFamily.InterNetwork
                          && !IPAddress.IsLoopback(ua.Address))
                .Select(ua => ua.Address)
                .ToArray();

            foreach (var addr in allIps)
            {
                sanBuilder.AddIpAddress(addr);
            }
            DiagnosticLogger.Log($"[cert] SAN IPs: {string.Join(", ", allIps.Select(a => a.ToString()))}");
        }
        catch (Exception ex)
        {
            DiagnosticLogger.Log($"[cert] Warning: could not enumerate NICs: {ex.Message}");
        }

        // Also add explicit bind address if it's a hostname (non-IP)
        if (!IPAddress.TryParse(bindAddress, out _) && bindAddress != "0.0.0.0" && bindAddress != "::")
        {
            sanBuilder.AddDnsName(bindAddress);
        }

        request.CertificateExtensions.Add(sanBuilder.Build());

        var notBefore = DateTimeOffset.UtcNow.AddDays(-1);
        var notAfter = DateTimeOffset.UtcNow.AddYears(ValidityYears);

        using var cert = request.CreateSelfSigned(notBefore, notAfter);

        // Export as PFX (no password — DPAPI protects the file via OS-level ACL)
        byte[] pfxBytes = cert.Export(X509ContentType.Pfx);
        File.WriteAllBytes(pfxPath, pfxBytes);

        DiagnosticLogger.Log($"[api] Self-signed TLS certificate generated: {pfxPath} (valid until {notAfter:yyyy-MM-dd})");
    }
}
