using SharpAvi.Output;
using SharpAvi;
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace VilsSharpX;

/// <summary>
/// Records a single uncompressed Gray8 AVI stream (e.g. Basler camera pane C).
/// Thread-safe: frames are enqueued from any thread and written on a background worker.
/// </summary>
public sealed class AviSingleRecorder : IDisposable
{
    private readonly int _width;
    private readonly int _height;
    private readonly int _stride8;
    private readonly string _path;

    private readonly AviWriter _writer;
    private readonly IAviVideoStream _stream;

    private readonly BlockingCollection<byte[]> _queue;
    private readonly CancellationTokenSource _cts = new();
    private readonly Task _worker;

    private readonly byte[] _buf;

    private readonly Stopwatch _recSw = new();
    private int _frameCount;

    public double ActualFps { get; private set; }
    public string FilePath => _path;

    public AviSingleRecorder(string path, int width, int height, int fps, int queueCapacity = 300)
    {
        if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Path is required", nameof(path));

        string? dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(dir) && !Directory.Exists(dir))
            Directory.CreateDirectory(dir);

        _path = path;
        _width = width;
        _height = height;
        _stride8 = AlignTo4(width);
        _buf = new byte[_stride8 * height];

        _writer = new AviWriter(path)
        {
            FramesPerSecond = fps,
            EmitIndex1 = true
        };

        _stream = _writer.AddVideoStream(_width, _height, BitsPerPixel.Bpp8);
        _stream.Codec = CodecIds.Uncompressed;

        _queue = new BlockingCollection<byte[]>(new ConcurrentQueue<byte[]>(), Math.Max(1, queueCapacity));
        _worker = Task.Run(WorkerLoop);
    }

    public bool TryEnqueue(byte[] gray8TopDown)
    {
        if (_queue.IsAddingCompleted) return false;
        return _queue.TryAdd(gray8TopDown);
    }

    public void Dispose()
    {
        try { _queue.CompleteAdding(); } catch { }
        try { _cts.Cancel(); } catch { }
        try { _worker.Wait(TimeSpan.FromSeconds(2)); } catch { }

        double elapsedSec = _recSw.Elapsed.TotalSeconds;
        ActualFps = elapsedSec > 0.01 && _frameCount > 1
            ? (_frameCount - 1) / elapsedSec
            : 0;

        try { _writer.Close(); } catch { }
        _cts.Dispose();
        _queue.Dispose();

        if (ActualFps > 0.5)
        {
            try { AviTripletRecorder.PatchAviFps(_path, ActualFps); } catch { }
        }
    }

    private void WorkerLoop()
    {
        try
        {
            foreach (var frame in _queue.GetConsumingEnumerable(_cts.Token))
            {
                if (frame.Length < _width * _height) continue;

                if (_frameCount == 0) _recSw.Start();
                _frameCount++;

                Gray8ToBottomUp(frame, _buf, _width, _height, _stride8);
                _stream.WriteFrame(true, _buf, 0, _buf.Length);
            }
        }
        catch (OperationCanceledException) { }
        finally
        {
            _recSw.Stop();
        }
    }

    private static int AlignTo4(int x) => (x + 3) & ~3;

    private static void Gray8ToBottomUp(byte[] src, byte[] dst, int w, int h, int dstStride)
    {
        for (int y = 0; y < h; y++)
        {
            int srcRow = y * w;
            int dstRow = (h - 1 - y) * dstStride;
            Buffer.BlockCopy(src, srcRow, dst, dstRow, w);
            for (int i = w; i < dstStride; i++) dst[dstRow + i] = 0;
        }
    }
}
