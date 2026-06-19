using System.Text.Json;
using System.Text.Json.Serialization;

namespace VilsSharpX.Api
{
    /// <summary>
    /// Incoming command envelope sent by an external test tool (e.g. the Python SDK).
    /// Mirrors the ACTSmart-style "command + payload" shape, transported as JSON over REST.
    /// </summary>
    public sealed class CommandRequest
    {
        /// <summary>Command name, e.g. "StartSimulation" (case-insensitive on dispatch).</summary>
        [JsonPropertyName("command")]
        public string? Command { get; set; }

        /// <summary>Optional command-specific payload. Shape depends on <see cref="Command"/>.</summary>
        [JsonPropertyName("payload")]
        public JsonElement? Payload { get; set; }

        /// <summary>Optional correlation id echoed back in the response.</summary>
        [JsonPropertyName("requestId")]
        public string? RequestId { get; set; }
    }

    /// <summary>
    /// Uniform response envelope returned for every command.
    /// </summary>
    public sealed class CommandResponse
    {
        [JsonPropertyName("requestId")]
        public string? RequestId { get; set; }

        [JsonPropertyName("ok")]
        public bool Ok { get; set; }

        [JsonPropertyName("command")]
        public string? Command { get; set; }

        [JsonPropertyName("data")]
        public object? Data { get; set; }

        [JsonPropertyName("error")]
        public ApiError? Error { get; set; }

        public static CommandResponse Success(string? command, string? requestId, object? data) => new()
        {
            RequestId = requestId,
            Ok = true,
            Command = command,
            Data = data,
            Error = null
        };

        public static CommandResponse Failure(string? command, string? requestId, string code, string message, object? details = null) => new()
        {
            RequestId = requestId,
            Ok = false,
            Command = command,
            Data = null,
            Error = new ApiError { Code = code, Message = message, Details = details }
        };
    }

    /// <summary>
    /// Structured error block, present only when <see cref="CommandResponse.Ok"/> is false.
    /// </summary>
    public sealed class ApiError
    {
        [JsonPropertyName("code")]
        public string Code { get; set; } = "ERROR";

        [JsonPropertyName("message")]
        public string Message { get; set; } = "";

        [JsonPropertyName("details")]
        public object? Details { get; set; }
    }

    /// <summary>
    /// Comparison statistics snapshot. <c>TotalPixelsDev</c> maps to the internal
    /// "aboveDeadband" count (pixels whose |B-A| exceeds the deadband).
    /// </summary>
    public sealed class ComparisonStats
    {
        [JsonPropertyName("max_positive_dev")]
        public int MaxPositiveDev { get; set; }

        [JsonPropertyName("max_negative_dev")]
        public int MaxNegativeDev { get; set; }

        [JsonPropertyName("average_pixels_dev")]
        public double AveragePixelsDev { get; set; }

        [JsonPropertyName("total_pixels_dev")]
        public int TotalPixelsDev { get; set; }

        [JsonPropertyName("total_dark_pixels")]
        public int TotalDarkPixels { get; set; }
    }

    /// <summary>
    /// Comparison settings update. All fields optional; only provided fields are applied.
    /// </summary>
    public sealed class ComparisonSettings
    {
        /// <summary>0=LVDS-AVTP, 1=LSM-LVDS, 2=LSM-AVTP.</summary>
        [JsonPropertyName("mode")]
        public int? Mode { get; set; }

        /// <summary>Deadband threshold, 0..255.</summary>
        [JsonPropertyName("deadband")]
        public int? Deadband { get; set; }

        /// <summary>B value delta offset.</summary>
        [JsonPropertyName("bDelta")]
        public int? BDelta { get; set; }
    }
    /// <summary>CAN/UART monitoring</summary>
    public sealed class CanUartState
    {
        [JsonPropertyName("state")]
        public string State { get; set; } = "";
    
        [JsonPropertyName("stored")]
        public int Stored { get; set; }
    
        [JsonPropertyName("rx")]
        public int Rx { get; set; }
    
        [JsonPropertyName("cd")]
        public int Cd { get; set; }
    
        [JsonPropertyName("parseErr")]
        public int ParseErr { get; set; }
    
        [JsonPropertyName("health")]
        public string Health { get; set; } = "ok";
    
        [JsonPropertyName("currentPage")]
        public int CurrentPage { get; set; }
    
        [JsonPropertyName("totalPages")]
        public int TotalPages { get; set; }
    
        [JsonPropertyName("canPrevious")]
        public bool CanPrevious { get; set; }
    
        [JsonPropertyName("canNext")]
        public bool CanNext { get; set; }
    }
}
