using System;

namespace VilsSharpX;

/// <summary>
/// Detects short-lived positive or negative comparison anomalies.
/// </summary>
public sealed class FlickerDetector
{
    private const int MinimumDeviatedPixels = 3;
    private const int MinimumBaselineDelta = 16;
    private const double BaselineAlpha = 0.1;

    private FlickerDetectionConfiguration _configuration = new();
    private FlickerDetectionStatus _status = FlickerDetectionStatus.Idle;
    private FlickerDetectionStatusSnapshot _snapshot = new();
    private FlickerComparisonSample? _baseline;
    private int _candidateFrames;
    private int _cooldownFrames;
    private bool _eventRaised;
    private FlickerComparisonSample? _peakSample;

    public event Action<FlickerDetectionStatusSnapshot>? StatusChanged;

    public FlickerDetectionStatusSnapshot Snapshot => _snapshot;

    public void UpdateConfiguration(FlickerDetectionConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        _configuration = configuration;
    }

    public void Reset()
    {
        _baseline = null;
        _candidateFrames = 0;
        _cooldownFrames = 0;
        _eventRaised = false;
        _peakSample = null;
        SetStatus(FlickerDetectionStatus.Idle, 0, null);
    }

    public void Process(FlickerComparisonSample sample)
    {
        if (sample.TotalPixels <= 0)
            return;

        if (_cooldownFrames > 0)
        {
            _cooldownFrames--;
            SetStatus(FlickerDetectionStatus.Cooldown, sample.MaxAbsoluteDeviation, null);
            return;
        }

        if (_baseline == null)
        {
            _baseline = sample;
            SetStatus(FlickerDetectionStatus.Idle, sample.MaxAbsoluteDeviation, null);
            return;
        }

        bool positive = sample.MaxPositiveDeviation >= _configuration.DeviationTrigger
            && sample.MaxPositiveDeviation - _baseline.MaxPositiveDeviation >= MinimumBaselineDelta;
        bool negative = sample.MaxNegativeDeviation <= -_configuration.DeviationTrigger
            && Math.Abs(sample.MaxNegativeDeviation) - Math.Abs(_baseline.MaxNegativeDeviation) >= MinimumBaselineDelta;
        bool hasSpot = sample.DeviatedPixelCount >= MinimumDeviatedPixels
            && sample.DeviatedPixelCount - _baseline.DeviatedPixelCount >= MinimumDeviatedPixels
            && sample.MaxAbsoluteDeviation >= _configuration.DeviationTrigger;
        bool candidate = positive || negative || hasSpot;

        if (candidate)
        {
            _candidateFrames++;
            if (_peakSample == null || sample.MaxAbsoluteDeviation > _peakSample.MaxAbsoluteDeviation)
                _peakSample = sample;
            SetStatus(FlickerDetectionStatus.Candidate, sample.MaxAbsoluteDeviation, null, sample);
            return;
        }

        if (_candidateFrames > 0)
        {
            if (_candidateFrames <= _configuration.FlickeringFramesThreshold)
            {
                _cooldownFrames = Math.Max(1, _configuration.CooldownMilliseconds * 50 / 1000);
                if (!_eventRaised)
                {
                    _eventRaised = true;
                    var detectedSample = _peakSample ?? sample;
                    SetStatus(FlickerDetectionStatus.Detected, detectedSample.MaxAbsoluteDeviation, DateTime.UtcNow, detectedSample);
                }
                else
                {
                    SetStatus(FlickerDetectionStatus.Cooldown, sample.MaxAbsoluteDeviation, null, sample);
                }
            }
            else
            {
                _candidateFrames = 0;
                _eventRaised = false;
                _peakSample = null;
                SetStatus(FlickerDetectionStatus.Idle, sample.MaxAbsoluteDeviation, null);
                return;
            }
        }

        _candidateFrames = 0;
        _eventRaised = false;
        _peakSample = null;
        _baseline = Blend(_baseline, sample);
        if (_status == FlickerDetectionStatus.Detected && _cooldownFrames == 0)
            SetStatus(FlickerDetectionStatus.Idle, sample.MaxAbsoluteDeviation, null);
    }

    private static FlickerComparisonSample Blend(FlickerComparisonSample baseline, FlickerComparisonSample sample)
    {
        return baseline with
        {
            MaxPositiveDeviation = BlendInt(baseline.MaxPositiveDeviation, sample.MaxPositiveDeviation),
            MaxNegativeDeviation = BlendInt(baseline.MaxNegativeDeviation, sample.MaxNegativeDeviation),
            MeanAbsoluteDeviation = BlendDouble(baseline.MeanAbsoluteDeviation, sample.MeanAbsoluteDeviation),
            DeviatedPixelCount = BlendInt(baseline.DeviatedPixelCount, sample.DeviatedPixelCount),
        };
    }

    private static int BlendInt(int oldValue, int newValue) =>
        (int)Math.Round(oldValue + BaselineAlpha * (newValue - oldValue));

    private static double BlendDouble(double oldValue, double newValue) =>
        oldValue + BaselineAlpha * (newValue - oldValue);

    private void SetStatus(FlickerDetectionStatus status, double metric, DateTime? eventUtc, FlickerComparisonSample? sample = null)
    {
        if (_status == status && eventUtc == null)
            return;

        _status = status;
        _snapshot = new FlickerDetectionStatusSnapshot
        {
            Status = status,
            LastEventUtc = eventUtc ?? _snapshot.LastEventUtc,
            LastMeasuredMetric = metric,
            DeviationTrigger = _configuration.DeviationTrigger,
            EventId = status == FlickerDetectionStatus.Detected
                ? $"FLK-{DateTime.UtcNow:yyyyMMdd-HHmmssfff}"
                : _snapshot.EventId,
            MaxPositiveDeviation = sample?.MaxPositiveDeviation ?? _baseline?.MaxPositiveDeviation ?? 0,
            MaxNegativeDeviation = sample?.MaxNegativeDeviation ?? _baseline?.MaxNegativeDeviation ?? 0,
            DeviatedPixelCount = sample?.DeviatedPixelCount ?? _baseline?.DeviatedPixelCount ?? 0,
            MeanAbsoluteDeviation = sample?.MeanAbsoluteDeviation ?? _baseline?.MeanAbsoluteDeviation ?? 0,
            CandidateFrameCount = _candidateFrames,
        };
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
    int TotalPixels)
{
    public int MaxAbsoluteDeviation => Math.Max(
        Math.Abs(MaxPositiveDeviation), Math.Abs(MaxNegativeDeviation));
}
