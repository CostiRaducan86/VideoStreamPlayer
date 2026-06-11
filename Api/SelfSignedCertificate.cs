using System;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace VilsSharpX.Api;

/// <summary>
/// Generates a self-signed X.509 certificate for the HTTPS automation API.
/// Generated fresh on every application start so SAN always reflects current network adapters.
/// Kept in memory only — no persistent .pfx file is required.
/// </summary>
internal static class SelfSignedCertificate
{
    private const string SubjectName = "CN=VilsSharpX Automation API";
    private const int KeySizeRsa = 2048;
    private const int ValidityYears = 5;

    /// <summary>
    /// Builds a fresh self-signed certificate in memory with all current machine IPs in SAN.
    /// </summary>
    public static X509Certificate2 LoadCertificate(string bindAddress = "127.0.0.1")
    {
        return BuildCertificate(bindAddress);
    }

    /// <summary>Compatibility shim — returns thumbprint of a freshly built cert.</summary>
    public static string? GetThumbprint()
    {
        try
        {
            using var cert = BuildCertificate("127.0.0.1");
            return cert.Thumbprint;
        }
        catch
        {
            return null;
        }
    }

    private static X509Certificate2 BuildCertificate(string bindAddress)
    {
        var rsa = RSA.Create(KeySizeRsa);

        var request = new CertificateRequest(SubjectName, rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);

        request.CertificateExtensions.Add(
            new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, critical: false));

        request.CertificateExtensions.Add(
            new X509EnhancedKeyUsageExtension(
                new OidCollection { new("1.3.6.1.5.5.7.3.1") },
                critical: false));

        var sanBuilder = new SubjectAlternativeNameBuilder();
        sanBuilder.AddDnsName("localhost");
        sanBuilder.AddIpAddress(IPAddress.Loopback);
        sanBuilder.AddIpAddress(IPAddress.IPv6Loopback);
        sanBuilder.AddDnsName("*.local");

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

        if (!IPAddress.TryParse(bindAddress, out _) && bindAddress != "0.0.0.0" && bindAddress != "::")
        {
            sanBuilder.AddDnsName(bindAddress);
        }

        request.CertificateExtensions.Add(sanBuilder.Build());

        var notBefore = DateTimeOffset.UtcNow.AddDays(-1);
        var notAfter = DateTimeOffset.UtcNow.AddYears(ValidityYears);

        using var ephemeral = request.CreateSelfSigned(notBefore, notAfter);

        // CRITICAL for Windows Schannel: the private key must live in a key container,
        // not an ephemeral/in-memory key, otherwise the server-side TLS handshake fails
        // with "An unexpected error occurred on a send". Round-trip through PFX and load
        // with PersistKeySet (user-level key store — no admin required) so Schannel can
        // access the key during the handshake.
        byte[] pfxBytes = ephemeral.Export(X509ContentType.Pfx);
        var cert = new X509Certificate2(
            pfxBytes,
            (string?)null,
            X509KeyStorageFlags.PersistKeySet | X509KeyStorageFlags.Exportable);

        DiagnosticLogger.Log($"[api] Self-signed TLS certificate generated (thumbprint={cert.Thumbprint}, hasPrivateKey={cert.HasPrivateKey}, valid until {notAfter:yyyy-MM-dd})");
        return cert;
    }
}
