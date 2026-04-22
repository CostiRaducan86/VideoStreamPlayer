using System.Collections.Generic;

namespace VilsSharpX;

public sealed class LsmCanDiagStore
{
    private readonly object _gate = new();
    private readonly LinkedList<LsmCanDiagRecord> _records = new();
    private readonly int _capacity;

    public LsmCanDiagStore(int capacity)
    {
        _capacity = capacity > 0 ? capacity : 256;
    }

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

    public void Append(LsmCanDiagRecord record)
    {
        lock (_gate)
        {
            _records.AddFirst(record);
            while (_records.Count > _capacity)
                _records.RemoveLast();
        }
    }

    public void Clear()
    {
        lock (_gate)
            _records.Clear();
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