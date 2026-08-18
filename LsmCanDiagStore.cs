using System.Collections.Generic;

namespace VilsSharpX;

public sealed class LsmCanDiagStore(int capacity)
{
    private readonly object _gate = new();
    private readonly LinkedList<LsmCanDiagRecord> _records = new();
    private readonly int? _capacity = capacity > 0 ? capacity : null;
    private LsmDeviceType _detectedDevice = LsmDeviceType.Osram20;  // Default to OSRAM

    public int Count
    {
        get
        {
            lock (_gate)
                return _records.Count;
        }
    }

    public bool IsFull
    {
        get
        {
            lock (_gate)
                return _capacity.HasValue && _records.Count >= _capacity.Value;
        }
    }

    /// <summary>
    /// Gets the detected device type (OSRAM or Nichia).
    /// Initially Unknown; auto-detected from first DeviceId in appended records.
    /// </summary>
    public LsmDeviceType DetectedDevice
    {
        get
        {
            lock (_gate)
                return _detectedDevice;
        }
    }

    public void Append(LsmCanDiagRecord record)
    {
        lock (_gate)
        {
            // Auto-detect device type from first valid record (0-indexed records list is empty initially)
            if (_records.Count == 0 && !record.IsCanRawFrame)
            {
                _detectedDevice = LsmDeviceProfile.GetProfileFromDeviceId(record.DeviceId).DeviceType;
            }

            _records.AddFirst(record);
            while (_capacity.HasValue && _records.Count > _capacity.Value)
                _records.RemoveLast();
        }
    }

    public void Clear()
    {
        lock (_gate)
        {
            _records.Clear();
            _detectedDevice = LsmDeviceType.Osram20;  // Reset to default OSRAM
        }
    }

    public IReadOnlyList<LsmCanDiagRecord> SnapshotNewestFirst(int maxCount)
    {
        var result = new List<LsmCanDiagRecord>();

        lock (_gate)
        {
            int remaining = maxCount <= 0 ? _records.Count : maxCount;
            foreach (var record in _records)
            {
                if (remaining-- == 0)
                    break;

                result.Add(record);
            }
        }

        return result;
    }

    public IReadOnlyList<LsmCanDiagRecord> SnapshotNewestFirst(int skip, int take)
    {
        var result = new List<LsmCanDiagRecord>(Math.Max(0, take));

        if (skip < 0) skip = 0;
        if (take <= 0) return result;

        lock (_gate)
        {
            int index = 0;
            foreach (var record in _records)
            {
                if (index++ < skip)
                    continue;

                result.Add(record);
                if (result.Count == take)
                    break;
            }
        }

        return result;
    }

    public IReadOnlyList<LsmCanDiagRecord> SnapshotOldestFirst(int skip, int take)
    {
        var result = new List<LsmCanDiagRecord>(Math.Max(0, take));

        if (skip < 0) skip = 0;
        if (take <= 0) return result;

        lock (_gate)
        {
            int index = 0;
            for (var node = _records.Last; node != null; node = node.Previous)
            {
                if (index++ < skip)
                    continue;

                result.Add(node.Value);
                if (result.Count == take)
                    break;
            }
        }

        return result;
    }
}
