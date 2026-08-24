using System;

namespace VilsSharpX;

/// <summary>
/// Detects short-lived positive or negative luminance anomalies on the LSM camera pane.
/// </summary>
/// <remarks>
/// The baseline is a <i>stable</i> pane C frame, not the previous frame. It is only adopted
/// after the scene has settled, so a slow fade accumulates deviation against the last steady
/// level instead of being tracked away frame by frame. Events use area hysteresis: they arm at
/// <see cref="EnterChangedPixelRatio"/> of the frame and only close once the deviated area
/// drops to <see cref="ExitAreaFactor"/> of that value, which keeps the return-to-baseline test
/// tolerant to sensor noise. Duration, not area, separates a flicker from an intentional
/// light-function transition.
/// </remarks>
public sealed class FlickerDetector
{
    private const int MinimumChangedPixels = 64;
    private const double EnterChangedPixelRatio = 0.005;
    private const double ExitAreaFactor = 0.25;
    private const int MinimumPixelDeviation = 4;
    private const int RecoveryFrames = 2;
    private const int BaselineStabilityFrames = 3;
    private const double LevelStabilityDelta = 0.5;
    private const double AssumedCameraFrameRate = 50.0;

    private FlickerDetectionConfiguration _configuration = new();
    private FlickerDetectionStatus _status = FlickerDetectionStatus.Idle;
    private FlickerDetectionStatusSnapshot _snapshot = new();
    private byte[]? _baselineFrame;
    private int _baselineWidth;
    private int _baselineHeight;
    private double? _previousMeanLevel;
    private int _eventFrames;
    private int _quietFrames;
    private int _settledFrames;
    private bool _awaitingBaseline;
    private int _cooldownFrames;
    private FlickerComparisonSample? _peakSample;

    public event Action<FlickerDetectionStatusSnapshot>? StatusChanged;

    public FlickerDetectionStatusSnapshot Snapshot => _snapshot;

    public void UpdateConfiguration(FlickerDetectionConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        _configuration = configuration;
    }

    public void Reset(bool notifyStatus = true)
    {
        _baselineFrame = null;
        _baselineWidth = 0;
        _baselineHeight = 0;
        _previousMeanLevel = null;
        _cooldownFrames = 0;
        _awaitingBaseline = false;
        _settledFrames = 0;
        ResetEventState();
        _status = FlickerDetectionStatus.Idle;
        _snapshot = new FlickerDetectionStatusSnapshot { DeviationTrigger = _configuration.DeviationTrigger };
        if (notifyStatus)
            StatusChanged?.Invoke(_snapshot);
    }

    /// <summary>
    /// Advances the state machine with one camera sample and reports whether the sampled frame
    /// should become the new stable baseline.
    /// </summary>
    public bool Process(FlickerComparisonSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        if (sample.TotalPixels <= 0)
            return false;

        if (_cooldownFrames > 0)
            _cooldownFrames--;

        bool settled = _previousMeanLevel is not double previousLevel
            || Math.Abs(sample.MeanLevel - previousLevel) <= LevelStabilityDelta;
        _previousMeanLevel = sample.MeanLevel;

        int enterPixels = Math.Max(
            MinimumChangedPixels,
            (int)Math.Ceiling(sample.TotalPixels * EnterChangedPixelRatio));
        int exitPixels = Math.Max(
            MinimumChangedPixels / 4,
            (int)Math.Floor(enterPixels * ExitAreaFactor));
        bool active = sample.DeviatedPixelCount >= enterPixels;
        bool quiet = sample.DeviatedPixelCount <= exitPixels;

        if (_awaitingBaseline)
            return ProcessBaselineRecovery(sample, settled, quiet);

        if (_eventFrames == 0)
            return ProcessIdle(sample, settled, active, quiet);

        return ProcessEvent(sample, quiet);
    }

    public void ProcessFrame(byte[] frame, int width, int height)
    {
        ArgumentNullException.ThrowIfNull(frame);
        int pixelCount = width * height;
        if (width <= 0 || height <= 0 || frame.Length < pixelCount)
            return;

        if (_baselineFrame == null
            || _baselineWidth != width
            || _baselineHeight != height
            || _baselineFrame.Length != pixelCount)
        {
            AdoptBaseline(frame, width, height, pixelCount);
            return;
        }

        ReadOnlySpan<byte> current = frame.AsSpan(0, pixelCount);
        ReadOnlySpan<byte> baseline = _baselineFrame.AsSpan(0, pixelCount);
        int deviationThreshold = Math.Max(_configuration.DeviationTrigger, MinimumPixelDeviation);
        long absoluteDeviationSum = 0;
        long levelSum = 0;
        int maxPositiveDeviation = 0;
        int maxNegativeDeviation = 0;
        int deviatedPixelCount = 0;
        for (int index = 0; index < pixelCount; index++)
        {
            int value = current[index];
            levelSum += value;
            int deviation = value - baseline[index];
            int absoluteDeviation = deviation < 0 ? -deviation : deviation;
            if (absoluteDeviation < deviationThreshold)
                continue;

            absoluteDeviationSum += absoluteDeviation;
            deviatedPixelCount++;
            if (deviation > maxPositiveDeviation)
                maxPositiveDeviation = deviation;
            else if (deviation < maxNegativeDeviation)
                maxNegativeDeviation = deviation;
        }

        var sample = new FlickerComparisonSample(
            maxPositiveDeviation,
            maxNegativeDeviation,
            deviatedPixelCount == 0 ? 0 : (double)absoluteDeviationSum / deviatedPixelCount,
            deviatedPixelCount,
            pixelCount,
            (double)levelSum / pixelCount);

        if (Process(sample))
            AdoptBaseline(frame, width, height, pixelCount);
    }

    private bool ProcessIdle(FlickerComparisonSample sample, bool settled, bool active, bool quiet)
    {
        if (active)
        {
            _eventFrames = 1;
            _quietFrames = 0;
            _settledFrames = 0;
            _peakSample = sample;
            SetStatus(FlickerDetectionStatus.Candidate, sample, 1);
            return false;
        }

        SetStatus(IdleOrCooldown(), sample, 0);
        _settledFrames = quiet && settled ? _settledFrames + 1 : 0;
        if (_settledFrames < BaselineStabilityFrames)
            return false;

        _settledFrames = 0;
        return true;
    }

    private bool ProcessEvent(FlickerComparisonSample sample, bool quiet)
    {
        if (!quiet)
        {
            _eventFrames++;
            _quietFrames = 0;
            if (_peakSample == null || sample.DeviatedPixelCount > _peakSample.DeviatedPixelCount)
                _peakSample = sample;

            if (_eventFrames <= _configuration.FlickeringFramesThreshold)
            {
                SetStatus(FlickerDetectionStatus.Candidate, sample, _eventFrames);
                return false;
            }

            // Longer than the configured flicker window: an intentional light-function
            // transition. Hold the verdict until the scene stops moving, then rebaseline.
            int transitionFrames = _eventFrames;
            ResetEventState();
            _awaitingBaseline = true;
            _settledFrames = 0;
            SetStatus(IdleOrCooldown(), sample, transitionFrames);
            return false;
        }

        _quietFrames++;
        if (_quietFrames < RecoveryFrames)
        {
            SetStatus(FlickerDetectionStatus.Candidate, sample, _eventFrames);
            return false;
        }

        int eventFrames = _eventFrames;
        FlickerComparisonSample peak = _peakSample ?? sample;
        ResetEventState();

        if (_cooldownFrames == 0
            && eventFrames >= _configuration.ConsecutiveSamples
            && eventFrames <= _configuration.FlickeringFramesThreshold)
        {
            _cooldownFrames = Math.Max(1, (int)Math.Round(
                _configuration.CooldownMilliseconds * AssumedCameraFrameRate / 1000.0));
            SetStatus(FlickerDetectionStatus.Detected, peak, eventFrames, DateTime.UtcNow);
            return false;
        }

        SetStatus(IdleOrCooldown(), sample, eventFrames);
        return false;
    }

    private bool ProcessBaselineRecovery(FlickerComparisonSample sample, bool settled, bool quiet)
    {
        if (quiet)
        {
            // The scene came back to the original level on its own; keep the existing baseline.
            _awaitingBaseline = false;
            _settledFrames = 0;
            SetStatus(IdleOrCooldown(), sample, 0);
            return false;
        }

        _settledFrames = settled ? _settledFrames + 1 : 0;
        SetStatus(IdleOrCooldown(), sample, 0);
        if (_settledFrames < BaselineStabilityFrames)
            return false;

        _awaitingBaseline = false;
        _settledFrames = 0;
        return true;
    }

    private void AdoptBaseline(byte[] frame, int width, int height, int pixelCount)
    {
        _baselineFrame = frame.AsSpan(0, pixelCount).ToArray();
        _baselineWidth = width;
        _baselineHeight = height;
    }

    private FlickerDetectionStatus IdleOrCooldown() => _cooldownFrames > 0
        ? FlickerDetectionStatus.Cooldown
        : FlickerDetectionStatus.Idle;

    private void ResetEventState()
    {
        _eventFrames = 0;
        _quietFrames = 0;
        _peakSample = null;
    }

    private void SetStatus(
        FlickerDetectionStatus status,
        FlickerComparisonSample sample,
        int frameCount,
        DateTime? eventUtc = null)
    {
        bool transition = _status != status || eventUtc != null;
        _status = status;
        _snapshot = new FlickerDetectionStatusSnapshot
        {
            Status = status,
            LastEventUtc = eventUtc ?? _snapshot.LastEventUtc,
            LastMeasuredMetric = sample.MaxAbsoluteDeviation,
            DeviationTrigger = _configuration.DeviationTrigger,
            EventId = status == FlickerDetectionStatus.Detected
                ? $"FLK-{DateTime.UtcNow:yyyyMMdd-HHmmssfff}"
                : _snapshot.EventId,
            MaxPositiveDeviation = sample.MaxPositiveDeviation,
            MaxNegativeDeviation = sample.MaxNegativeDeviation,
            DeviatedPixelCount = sample.DeviatedPixelCount,
            MeanAbsoluteDeviation = sample.MeanAbsoluteDeviation,
            CandidateFrameCount = frameCount,
        };

        // Every sample refreshes the snapshot, but only real transitions reach the event log.
        if (transition)
            StatusChanged?.Invoke(_snapshot);
    }
}

/// <summary>
/// Comparison metrics consumed by the flicker detector.
/// </summary>
public sealed record FlickerComparisonSample(
    int MaxPositiveDeviation,
    int MaxNegativeDeviation,
    double MeanAbsoluteDeviation,
    int DeviatedPixelCount,
    int TotalPixels,
    double MeanLevel)
{
    public int MaxAbsoluteDeviation => Math.Max(
        Math.Abs(MaxPositiveDeviation), Math.Abs(MaxNegativeDeviation));
}
