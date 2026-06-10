using System;
using System.Text.Json;

namespace VilsSharpX.Api
{
    /// <summary>
    /// Service layer that dispatches command envelopes to the GUI automation bridge.
    /// Owns command validation and the uniform response shape. Keeps the REST host
    /// (transport) decoupled from the WPF application (behavior).
    /// </summary>
    public sealed class CommandRouter(IGuiAutomationBridge bridge)
    {
        private readonly IGuiAutomationBridge _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));

        /// <summary>
        /// Executes a command request and returns a uniform response. Never throws;
        /// failures are reported via <see cref="CommandResponse.Failure"/>.
        /// </summary>
        public CommandResponse Execute(CommandRequest request)
        {
            string command = request?.Command?.Trim() ?? string.Empty;
            string? reqId = request?.RequestId;

            if (string.IsNullOrEmpty(command))
                return CommandResponse.Failure(command, reqId, "BAD_REQUEST", "Missing 'command' field.");

            try
            {
                switch (command.ToLowerInvariant())
                {
                    case "ping":
                        return CommandResponse.Success(command, reqId, new
                        {
                            pong = true,
                            utc = DateTime.UtcNow.ToString("o"),
                            isRunning = _bridge.IsRunning,
                            isPaused = _bridge.IsPaused
                        });

                    case "startsimulation":
                    {
                        int fps = ReadInt(request!.Payload, "fps", 100);
                        if (fps <= 0) fps = 100;
                        _bridge.StartSimulation(fps);
                        return CommandResponse.Success(command, reqId, new { started = true, fps });
                    }

                    case "stopsimulation":
                        _bridge.StopSimulation();
                        return CommandResponse.Success(command, reqId, new { stopped = true });

                    case "pausesimulation":
                        _bridge.PauseSimulation();
                        return CommandResponse.Success(command, reqId, new { paused = true });

                    case "resumesimulation":
                        _bridge.ResumeSimulation();
                        return CommandResponse.Success(command, reqId, new { resumed = true });

                    case "setcomparisonsettings":
                    {
                        int? mode = ReadOptionalInt(request!.Payload, "mode");
                        int? deadband = ReadOptionalInt(request.Payload, "deadband");
                        int? bDelta = ReadOptionalInt(request.Payload, "bDelta");

                        if (mode is null && deadband is null && bDelta is null)
                            return CommandResponse.Failure(command, reqId, "BAD_REQUEST",
                                "Provide at least one of: mode, deadband, bDelta.");

                        if (mode is < 0 or > 2)
                            return CommandResponse.Failure(command, reqId, "BAD_REQUEST",
                                "mode must be 0 (LVDS-AVTP), 1 (LSM-LVDS) or 2 (LSM-AVTP).");
                        if (deadband is < 0 or > 255)
                            return CommandResponse.Failure(command, reqId, "BAD_REQUEST",
                                "deadband must be in range 0..255.");

                        _bridge.SetComparisonSettings(mode, deadband, bDelta);
                        return CommandResponse.Success(command, reqId, new { updated = true, mode, deadband, bDelta });
                    }

                    case "getcomparisonstats":
                        return CommandResponse.Success(command, reqId, _bridge.GetComparisonStats());

                    case "getframesnapshot":
                    {
                        string pane = ReadString(request!.Payload, "pane", "D").ToUpperInvariant();
                        if (pane != "A" && pane != "B" && pane != "D")
                            return CommandResponse.Failure(command, reqId, "BAD_REQUEST",
                                "pane must be 'A', 'B' or 'D'.");

                        byte[] png = _bridge.GetFrameSnapshotPng(pane);
                        return CommandResponse.Success(command, reqId, new
                        {
                            pane,
                            format = "png-base64",
                            image = Convert.ToBase64String(png)
                        });
                    }

                    default:
                        return CommandResponse.Failure(command, reqId, "UNKNOWN_COMMAND",
                            $"Command '{command}' is not supported in v1.");
                }
            }
            catch (Exception ex)
            {
                return CommandResponse.Failure(command, reqId, "INTERNAL_ERROR", ex.Message);
            }
        }

        // ---- payload helpers (tolerant JSON reads) ----

        private static int ReadInt(JsonElement? payload, string name, int fallback)
            => ReadOptionalInt(payload, name) ?? fallback;

        private static int? ReadOptionalInt(JsonElement? payload, string name)
        {
            if (payload is not JsonElement el || el.ValueKind != JsonValueKind.Object)
                return null;
            if (!el.TryGetProperty(name, out var prop))
                return null;

            return prop.ValueKind switch
            {
                JsonValueKind.Number when prop.TryGetInt32(out var i) => i,
                JsonValueKind.Number => (int)Math.Round(prop.GetDouble()),
                JsonValueKind.String when int.TryParse(prop.GetString(), out var s) => s,
                _ => null
            };
        }

        private static string ReadString(JsonElement? payload, string name, string fallback)
        {
            if (payload is not JsonElement el || el.ValueKind != JsonValueKind.Object)
                return fallback;
            if (!el.TryGetProperty(name, out var prop))
                return fallback;
            return prop.ValueKind == JsonValueKind.String ? (prop.GetString() ?? fallback) : fallback;
        }
    }
}
