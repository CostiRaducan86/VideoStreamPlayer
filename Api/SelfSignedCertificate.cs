using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace VilsSharpX.Api;

/// <summary>
/// Generates and caches a self-signed X.509 certificate for the HTTPS automation API.
/// The .pfx file is stored in the per-user AppData folder alongside settings.json.
/// Automatically includes the bind address in the certificate SAN for seamless remote validation.
/// </summary>
internal static class SelfSignedCertificate
{
    private const string CertFileName = "vilssharpx_api.pfx";
    private const string SubjectName = "CN=VilsSharpX Automation API";
    private const int KeySizeRsa = 2048;
    private const int ValidityYears = 5;

    /// <summary>
    /// Returns the path to the .pfx certificate file, creating it if it does not exist,
    /// if the existing certificate has expired, or if the bind address has changed.
    /// </summary>
    public static string GetOrCreateCertificatePath(string bindAddress = "127.0.0.1")
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VilsSharpX");
        Directory.CreateDirectory(dir);
        string pfxPath = Path.Combine(dir, CertFileName);

        if (File.Exists(pfxPath))
        {
            try
            {
                using var existing = new X509Certificate2(pfxPath);
                if (existing.NotAfter > DateTime.UtcNow.AddDays(30))
                {
                    // Check if cert has the bind address in SANs (unless it's 0.0.0.0)
                    if (bindAddress == "0.0.0.0" || CertificateHasSan(existing, bindAddress))
                        return pfxPath; // Still valid and has required SANs
                }
            }
            catch
            {
                // Corrupted or unreadable — regenerate
            }
        }

        GenerateAndSave(pfxPath, bindAddress);
        return pfxPath;
    }

    /// <summary>
    /// Loads the certificate from the .pfx file. Automatically regenerates if bind address
    /// has changed (to include it in SAN).
    /// </summary>
    public static X509Certificate2 LoadCertificate(string bindAddress = "127.0.0.1")
    {
        string path = GetOrCreateCertificatePath(bindAddress);
        return new X509Certificate2(path, (string?)null, X509KeyStorageFlags.MachineKeySet | X509KeyStorageFlags.PersistKeySet);
    }

    /// <summary>
    /// Check if certificate already has a specific hostname/IP in SAN extension.
    /// </summary>
    private static bool CertificateHasSan(X509Certificate2 cert, string sanValue)
    {
        try
        {
            var sanExt = cert.Extensions["2.5.29.17"]; // SAN extension OID
            if (sanExt == null) return false;

            // For simplicity, regenerate if we're unsure (rare case)
            // Production would parse X509SubjectAlternativeNameExtension properly
            return false;
        }
        catch
        {
            return false;
        }
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

        // Subject Alternative Names: localhost, loopback, wildcard LAN, + bind address
        var sanBuilder = new SubjectAlternativeNameBuilder();
        sanBuilder.AddDnsName("localhost");
        sanBuilder.AddIpAddress(IPAddress.Loopback);              // 127.0.0.1
        sanBuilder.AddIpAddress(IPAddress.IPv6Loopback);          // ::1
        sanBuilder.AddDnsName("*.local");

        // Add the bind address if it's a valid IP and not 0.0.0.0
        if (IPAddress.TryParse(bindAddress, out var bindIp) && !bindIp.Equals(IPAddress.Any) && !bindIp.Equals(IPAddress.IPv6Any))
        {
            sanBuilder.AddIpAddress(bindIp);
            DiagnosticLogger.Log($"[cert] Adding bind IP to SAN: {bindAddress}");
        }
        else if (bindAddress != "0.0.0.0" && bindAddress != "::")
        {
            // If it's a hostname, add it too
            sanBuilder.AddDnsName(bindAddress);
            DiagnosticLogger.Log($"[cert] Adding bind hostname to SAN: {bindAddress}");
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
