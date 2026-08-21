namespace VilsSharpX;

/// <summary>
/// Lifecycle state of a flicker detection session.
/// </summary>
public enum FlickerDetectionStatus
{
    Idle,
    Injecting,
    Candidate,
    Detected,
    Cooldown,
    Error,
}

/// <summary>
/// Simulation strategy used to create a controlled LSM camera event.
/// </summary>
public enum FlickerInjectionMode
{
    ReplayLatestFrame,
}

/// <summary>
/// Polarity of the injected text pixels relative to the captured camera frame.
/// </summary>
public enum FlickerInjectionPolarity
{
    White,
    Black,
}

/// <summary>
/// Validated configuration for flicker simulation and detection.
/// </summary>
public sealed record FlickerDetectionConfiguration
{
    public const int MinFrameCount = 1;
    public const int MaxFrameCount = 250;
    public const int MinTriggerThreshold = 0;
    public const int MaxTriggerThreshold = 255;
    public const int DefaultTriggerThreshold = 32;
    public const int MinConsecutiveSamples = 1;
    public const int MaxConsecutiveSamples = 100;
    public const int MinCooldownMilliseconds = 100;
    public const int MaxCooldownMilliseconds = 200;

    public int FlickeringFramesThreshold { get; init; } = 10;
    public int DeviationTrigger { get; init; } = DefaultTriggerThreshold;
    public int ConsecutiveSamples { get; init; } = 1;
    public int CooldownMilliseconds { get; init; } = 150;
    public FlickerInjectionMode InjectionMode { get; init; } = FlickerInjectionMode.ReplayLatestFrame;
    public FlickerInjectionPolarity InjectionPolarity { get; init; } = FlickerInjectionPolarity.White;

    public IReadOnlyList<string> Validate()
    {
        var errors = new List<string>();

        if (FlickeringFramesThreshold is < MinFrameCount or > MaxFrameCount)
            errors.Add($"Frame count must be between {MinFrameCount} and {MaxFrameCount}.");
        if (DeviationTrigger is < MinTriggerThreshold or > MaxTriggerThreshold)
            errors.Add($"Trigger threshold must be between {MinTriggerThreshold} and {MaxTriggerThreshold}.");
        if (ConsecutiveSamples is < MinConsecutiveSamples or > MaxConsecutiveSamples)
            errors.Add($"Consecutive samples must be between {MinConsecutiveSamples} and {MaxConsecutiveSamples}.");
        if (CooldownMilliseconds is < MinCooldownMilliseconds or > MaxCooldownMilliseconds)
            errors.Add($"Cooldown must be between {MinCooldownMilliseconds} and {MaxCooldownMilliseconds} ms.");

        return errors;
    }
}

/// <summary>
/// Immutable status snapshot exposed to the UI and future automation endpoints.
/// </summary>
public sealed record FlickerDetectionStatusSnapshot
{
    public FlickerDetectionStatus Status { get; init; } = FlickerDetectionStatus.Idle;
    public DateTime? LastEventUtc { get; init; }
    public double LastMeasuredMetric { get; init; }
    public int DeviationTrigger { get; init; } = FlickerDetectionConfiguration.DefaultTriggerThreshold;
    public string? EventId { get; init; }
    public string? OutputDirectory { get; init; }
    public string? ErrorMessage { get; init; }
    public int MaxPositiveDeviation { get; init; }
    public int MaxNegativeDeviation { get; init; }
    public int DeviatedPixelCount { get; init; }
    public double MeanAbsoluteDeviation { get; init; }
    public int CandidateFrameCount { get; init; }
}
