using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace VilsSharpX.Api
{
    /// <summary>
    /// In-process REST host (Kestrel + Minimal API). Supports HTTP or HTTPS (self-signed).
    /// Exposes command endpoints plus a lightweight health probe.
    /// </summary>
    public sealed class ApiHost(
        IGuiAutomationBridge bridge,
        string bindAddress = "127.0.0.1",
        int port = 8420,
        string? apiKey = null,
        IEnumerable<string>? allowedCidrs = null,
        bool enableHttps = false)
    {
        /// <summary>Default localhost port for the automation API.</summary>
        public const int DefaultPort = 8420;
        private const string ApiKeyHeaderName = "X-Api-Key";
        private static readonly string[] s_supportedCommands =
        [
            "Ping", "StartSimulation", "StopSimulation", "PauseSimulation",
            "ResumeSimulation", "SetComparisonSettings", "GetComparisonStats", "GetFrameSnapshot"
        ];

        private readonly CommandRouter _router = new(bridge);
        private readonly string _bindAddress = string.IsNullOrWhiteSpace(bindAddress) ? "127.0.0.1" : bindAddress.Trim();
        private readonly int _port = port;
        private readonly string _apiKey = apiKey?.Trim() ?? string.Empty;
        private readonly CidrRange[] _allowedCidrs = [.. ParseAllowedCidrs(allowedCidrs)];
        private readonly bool _enableHttps = enableHttps;
        private WebApplication? _app;

        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            PropertyNameCaseInsensitive = true
        };

        /// <summary>True once the host has been started.</summary>
        public bool IsRunning => _app != null;

        /// <summary>Base URL the API listens on (http or https).</summary>
        public string BaseUrl => $"{(_enableHttps ? "https" : "http")}://{_bindAddress}:{_port}";

        /// <summary>Loopback URL always available for local access (HTTP, no auth).</summary>
        public string LoopbackUrl => $"http://127.0.0.1:{_port}";

        /// <summary>
        /// Starts the host on a background thread. Safe to call once.
        /// When HTTPS is enabled, binds BOTH:
        ///   - HTTPS on the configured bind address (remote, requires auth)
        ///   - HTTP on 127.0.0.1 (loopback, no auth required)
        /// This ensures local tools always work without certificate trust issues.
        /// </summary>
        public void Start()
        {
            if (_app != null) return;

            var builder = WebApplication.CreateBuilder();
            builder.Logging.ClearProviders();

            if (_enableHttps)
            {
                var cert = SelfSignedCertificate.LoadCertificate(_bindAddress);
                bool bindIsAllInterfaces = _bindAddress == "0.0.0.0" || _bindAddress == "::";
                bool bindIsLoopback = IsLoopbackBindingAddress(_bindAddress);

                builder.WebHost.ConfigureKestrel(options =>
                {
                    if (bindIsAllInterfaces)
                    {
                        // Bind HTTPS on each non-loopback IP individually so loopback stays free for HTTP.
                        var ips = GetNonLoopbackIPv4Addresses();
                        DiagnosticLogger.Log($"[api] HTTPS bind 0.0.0.0: found {ips.Length} non-loopback IPs: {string.Join(", ", ips.Select(i => i.ToString()))}");
                        foreach (var ip in ips)
                        {
                            options.Listen(ip, _port, lo => ConfigureHttps(lo, cert));
                        }
                        // Plain HTTP on loopback — local tools always work without certs.
                        options.Listen(IPAddress.Loopback, _port);
                    }
                    else if (bindIsLoopback)
                    {
                        // Only loopback requested — HTTPS on loopback (user explicitly chose this).
                        options.Listen(IPAddress.Loopback, _port, lo => ConfigureHttps(lo, cert));
                    }
                    else
                    {
                        // Specific IP: HTTPS on that IP + plain HTTP on loopback.
                        options.Listen(IPAddress.Parse(_bindAddress), _port, lo => ConfigureHttps(lo, cert));
                        options.Listen(IPAddress.Loopback, _port);
                    }
                });
            }
            else
            {
                builder.WebHost.UseUrls($"http://{_bindAddress}:{_port}");
            }

            var app = builder.Build();

            app.MapGet("/api/v1/health", () => Results.Json(new { ok = true, utc = DateTime.UtcNow.ToString("o") }));

            app.MapGet("/api/v1/health/details", (HttpContext ctx) =>
            {
                if (!AuthorizeRequest(ctx))
                    return Results.Json(CommandResponse.Failure(null, null, "UNAUTHORIZED", "Missing or invalid X-Api-Key."),
                        statusCode: StatusCodes.Status401Unauthorized);

                IPAddress? remoteIp = ctx.Connection.RemoteIpAddress;
                bool requestIsLoopback = remoteIp == null || IPAddress.IsLoopback(remoteIp);
                bool remoteIpAllowed = remoteIp == null || IsRemoteIpAllowed(remoteIp);
                bool isLoopbackBinding = IsLoopbackBindingAddress(_bindAddress);

                return Results.Json(new
                {
                    ok = true,
                    utc = DateTime.UtcNow.ToString("o"),
                    host = new
                    {
                        bindAddress = _bindAddress,
                        port = _port,
                        baseUrl = BaseUrl,
                        isLoopbackBinding,
                        remoteRequestsEnabled = !isLoopbackBinding,
                        apiKeyRequiredForRemote = !string.IsNullOrWhiteSpace(_apiKey),
                        allowlistEnabled = _allowedCidrs.Length > 0,
                        allowlistCidrs = _allowedCidrs.Select(c => c.Text).ToArray()
                    },
                    request = new
                    {
                        remoteIp = remoteIp?.ToString() ?? "unknown",
                        isLoopback = requestIsLoopback,
                        remoteIpAllowed,
                        hasApiKeyHeader = ctx.Request.Headers.ContainsKey(ApiKeyHeaderName)
                    }
                });
            });

            app.MapGet("/api/v1/commands", (HttpContext ctx) =>
            {
                if (!AuthorizeRequest(ctx))
                    return Results.Json(CommandResponse.Failure(null, null, "UNAUTHORIZED", "Missing or invalid X-Api-Key."),
                        statusCode: StatusCodes.Status401Unauthorized);

                return Results.Json(new
                {
                    commands = s_supportedCommands
                });
            });

            app.MapPost("/api/v1/command", async (HttpContext ctx) =>
            {
                if (!AuthorizeRequest(ctx))
                    return Results.Json(CommandResponse.Failure(null, null, "UNAUTHORIZED", "Missing or invalid X-Api-Key."),
                        statusCode: StatusCodes.Status401Unauthorized);

                CommandRequest? req;
                try
                {
                    req = await JsonSerializer.DeserializeAsync<CommandRequest>(ctx.Request.Body, JsonOptions);
                }
                catch (Exception ex)
                {
                    return Results.Json(CommandResponse.Failure(null, null, "BAD_JSON", ex.Message),
                        statusCode: StatusCodes.Status400BadRequest);
                }

                if (req == null)
                    return Results.Json(CommandResponse.Failure(null, null, "BAD_REQUEST", "Empty request body."),
                        statusCode: StatusCodes.Status400BadRequest);

                CommandResponse resp = _router.Execute(req);
                int status = resp.Ok ? StatusCodes.Status200OK : MapErrorStatus(resp.Error?.Code);
                return Results.Json(resp, statusCode: status);
            });

            app.Start();
            _app = app;
        }

        /// <summary>
        /// Stops the host. Best-effort with a short timeout.
        /// </summary>
        public void Stop()
        {
            var app = _app;
            if (app == null) return;
            _app = null;

            // Shutdown must never block WPF window close indefinitely.
            // Use bounded waits + aggressive fallback.
            const int stopTimeoutMs = 1200;
            const int disposeTimeoutMs = 1200;

            try
            {
                // Signal host lifetime immediately.
                app.Lifetime.StopApplication();
            }
            catch { /* ignore shutdown races */ }

            try
            {
                using var cts = new CancellationTokenSource(stopTimeoutMs);
                Task stopTask = app.StopAsync(cts.Token);
                _ = stopTask.Wait(stopTimeoutMs);
            }
            catch { /* ignore shutdown races/timeouts */ }

            try
            {
                if (app is IAsyncDisposable asyncDisposable)
                {
                    Task disposeTask = asyncDisposable.DisposeAsync().AsTask();
                    _ = disposeTask.Wait(disposeTimeoutMs);
                }
                else
                {
                    (app as IDisposable)?.Dispose();
                }
            }
            catch
            {
                try { (app as IDisposable)?.Dispose(); } catch { /* last-chance fallback */ }
            }
        }

        private static int MapErrorStatus(string? code) => code switch
        {
            "BAD_REQUEST" or "BAD_JSON" => StatusCodes.Status400BadRequest,
            "UNAUTHORIZED" => StatusCodes.Status401Unauthorized,
            "UNKNOWN_COMMAND" => StatusCodes.Status404NotFound,
            "NOT_IMPLEMENTED" => StatusCodes.Status501NotImplemented,
            _ => StatusCodes.Status500InternalServerError
        };

        private bool AuthorizeRequest(HttpContext ctx)
        {
            IPAddress? remoteIp = ctx.Connection.RemoteIpAddress;
            if (remoteIp == null || IPAddress.IsLoopback(remoteIp))
                return true;

            if (!IsRemoteIpAllowed(remoteIp))
                return false;

            if (string.IsNullOrWhiteSpace(_apiKey))
                return false;

            if (!ctx.Request.Headers.TryGetValue(ApiKeyHeaderName, out var headerValue))
                return false;

            return string.Equals(headerValue.ToString(), _apiKey, StringComparison.Ordinal);
        }

        private bool IsRemoteIpAllowed(IPAddress remoteIp)
        {
            if (_allowedCidrs.Length == 0)
                return true;

            for (int i = 0; i < _allowedCidrs.Length; i++)
            {
                if (_allowedCidrs[i].Contains(remoteIp))
                    return true;
            }

            return false;
        }

        private static IEnumerable<CidrRange> ParseAllowedCidrs(IEnumerable<string>? values)
        {
            if (values == null) yield break;

            // Expand stored entries (clean IPs and ranges) into CIDR format
            string[] expanded = ApiConfigurationWindow.ExpandAllowlistForRuntime(values.ToArray());

            foreach (string token in expanded)
            {
                if (string.IsNullOrWhiteSpace(token))
                    continue;

                if (CidrRange.TryParse(token, out var cidr))
                    yield return cidr;
            }
        }

        private static bool IsLoopbackBindingAddress(string bindAddress)
        {
            if (string.IsNullOrWhiteSpace(bindAddress))
                return true;

            if (string.Equals(bindAddress, "localhost", StringComparison.OrdinalIgnoreCase))
                return true;

            if (!IPAddress.TryParse(bindAddress, out var ip))
                return false;

            return IPAddress.IsLoopback(ip);
        }

        /// <summary>
        /// Configures an HTTPS listener with the given certificate and an explicit
        /// SSL protocol set (TLS 1.2 + 1.3). Pinning the protocols avoids handshake
        /// failures with older clients (e.g. PowerShell 5.1 / .NET Framework) that
        /// negotiate poorly when the server leaves protocol selection to the OS default.
        /// </summary>
        private static void ConfigureHttps(Microsoft.AspNetCore.Server.Kestrel.Core.ListenOptions listenOptions, X509Certificate2 cert)
        {
            listenOptions.UseHttps(httpsOptions =>
            {
                httpsOptions.ServerCertificate = cert;
                httpsOptions.SslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13;
            });
        }

        /// <summary>
        /// Returns all non-loopback IPv4 addresses assigned to this machine.
        /// Uses NetworkInterface enumeration (more reliable than Dns.GetHostEntry which
        /// may miss interfaces on multi-homed machines).
        /// </summary>
        private static IPAddress[] GetNonLoopbackIPv4Addresses()
        {
            try
            {
                return NetworkInterface.GetAllNetworkInterfaces()
                    .Where(ni => ni.OperationalStatus == OperationalStatus.Up
                              && ni.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                    .SelectMany(ni => ni.GetIPProperties().UnicastAddresses)
                    .Where(ua => ua.Address.AddressFamily == AddressFamily.InterNetwork
                              && !IPAddress.IsLoopback(ua.Address))
                    .Select(ua => ua.Address)
                    .ToArray();
            }
            catch
            {
                return [];
            }
        }

        private sealed class CidrRange
        {
            public required string Text { get; init; }
            public required AddressFamily AddressFamily { get; init; }
            public required byte[] NetworkBytes { get; init; }
            public required byte[] MaskBytes { get; init; }

            public static bool TryParse(string token, out CidrRange cidr)
            {
                cidr = null!;

                string[] parts = token.Split('/', 2, StringSplitOptions.TrimEntries);
                if (!IPAddress.TryParse(parts[0], out var ip))
                    return false;

                int bitLength = ip.AddressFamily == AddressFamily.InterNetwork ? 32 : 128;
                int prefix = bitLength;
                if (parts.Length == 2 && !int.TryParse(parts[1], out prefix))
                    return false;

                if (prefix < 0 || prefix > bitLength)
                    return false;

                byte[] mask = BuildMask(bitLength, prefix);
                byte[] network = ApplyMask(ip.GetAddressBytes(), mask);

                cidr = new CidrRange
                {
                    Text = parts.Length == 2 ? token : $"{ip}/{prefix}",
                    AddressFamily = ip.AddressFamily,
                    NetworkBytes = network,
                    MaskBytes = mask
                };

                return true;
            }

            public bool Contains(IPAddress address)
            {
                if (address.AddressFamily != AddressFamily)
                    return false;

                byte[] masked = ApplyMask(address.GetAddressBytes(), MaskBytes);
                return masked.AsSpan().SequenceEqual(NetworkBytes);
            }

            private static byte[] BuildMask(int bitLength, int prefix)
            {
                int byteLength = bitLength / 8;
                byte[] mask = new byte[byteLength];

                int fullBytes = prefix / 8;
                int remainingBits = prefix % 8;

                for (int i = 0; i < fullBytes; i++)
                    mask[i] = 0xFF;

                if (remainingBits > 0 && fullBytes < mask.Length)
                    mask[fullBytes] = (byte)(0xFF << (8 - remainingBits));

                return mask;
            }

            private static byte[] ApplyMask(byte[] ipBytes, byte[] maskBytes)
            {
                byte[] result = new byte[ipBytes.Length];
                for (int i = 0; i < ipBytes.Length; i++)
                    result[i] = (byte)(ipBytes[i] & maskBytes[i]);

                return result;
            }
        }
    }
}
