using System.Collections.Generic;

namespace VilsSharpX;

public sealed class LsmCanDiagStore(int capacity)
{
    private readonly object _gate = new();
    private readonly LinkedList<LsmCanDiagRecord> _records = new();
    private readonly int _capacity = capacity > 0 ? capacity : 256;
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
                return _records.Count >= _capacity;
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
            while (_records.Count > _capacity)
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
}
