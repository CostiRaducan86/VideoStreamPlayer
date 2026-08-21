namespace VilsSharpX;

/// <summary>
/// Controls deterministic injection of a visible flicker pattern into the LSM camera stream.
/// </summary>
public sealed class FlickerInjectionController : IDisposable
{
    private readonly object _sync = new();
    private byte[]? _injectedFrame;
    private int _frameWidth;
    private int _frameHeight;
    private int _servedFrameCount;
    private FlickerDetectionConfiguration? _configuration;

    public bool IsActive
    {
        get
        {
            lock (_sync)
                return _configuration != null && IsActiveLocked();
        }
    }

    public event Action? InjectionCompleted;

    public bool TryStart(byte[] sourceFrame, int width, int height, FlickerDetectionConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(sourceFrame);
        ArgumentNullException.ThrowIfNull(configuration);

        if (width <= 0 || height <= 0 || sourceFrame.Length < width * height)
            return false;
        if (configuration.Validate().Count != 0)
            return false;

        lock (_sync)
        {
            if (_configuration != null && IsActiveLocked())
                return false;

            _injectedFrame = CreateFrameWithCenterText(
                sourceFrame, width, height, configuration.InjectionPolarity);
            _frameWidth = width;
            _frameHeight = height;
            _servedFrameCount = 0;
            _configuration = configuration;
            return true;
        }
    }

    public bool TryGetFrame(byte[] sourceFrame, int width, int height, out byte[] frame)
    {
        bool completed = false;
        lock (_sync)
        {
            if (_configuration == null || !IsActiveLocked()
                || _injectedFrame == null || width != _frameWidth || height != _frameHeight)
            {
                frame = sourceFrame;
                return false;
            }

            _servedFrameCount++;
            frame = (byte[])_injectedFrame.Clone();
            completed = !IsActiveLocked();
        }

        if (completed)
            CompleteInjection();

        return true;
    }

    public void Cancel()
    {
        bool wasActive;
        lock (_sync)
        {
            wasActive = _configuration != null;
            _configuration = null;
            _injectedFrame = null;
            _servedFrameCount = 0;
        }

        if (wasActive)
            InjectionCompleted?.Invoke();
    }

    public void Dispose() => Cancel();

    private static byte[] CreateFrameWithCenterText(
        byte[] sourceFrame, int width, int height, FlickerInjectionPolarity polarity)
    {
        var frame = new byte[width * height];
        Buffer.BlockCopy(sourceFrame, 0, frame, 0, frame.Length);
        byte textValue = polarity == FlickerInjectionPolarity.White ? (byte)200 : (byte)0;
        const string text = "FLICKER";
        const int glyphWidth = 7;
        const int glyphHeight = 9;
        const int glyphSpacing = 5;
        const int scaleX = 7;
        const int scaleY = 8;
        int textWidth = (text.Length * glyphWidth + (text.Length - 1) * glyphSpacing) * scaleX;
        int textHeight = glyphHeight * scaleY;
        int startX = Math.Max(0, (width - textWidth) / 2);
        int startY = Math.Max(0, (height - textHeight) / 2);

        for (int glyphIndex = 0; glyphIndex < text.Length; glyphIndex++)
        {
            foreach (var (row, bits) in GetGlyph(text[glyphIndex]))
            {
                for (int column = 0; column < glyphWidth; column++)
                {
                    if ((bits & (1 << (glyphWidth - 1 - column))) == 0)
                        continue;

                    int x = startX + (glyphIndex * (glyphWidth + glyphSpacing) + column) * scaleX;
                    int y = startY + row * scaleY;
                    for (int scaledY = 0; scaledY < scaleY; scaledY++)
                        Array.Fill(frame, textValue, (y + scaledY) * width + x, scaleX);
                }
            }
        }

        return frame;
    }

    private static IEnumerable<(int row, int bits)> GetGlyph(char character)
    {
        string[] rows = character switch
        {
            'F' => ["11110", "10000", "10000", "11110", "10000", "10000", "10000"],
            'L' => ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
            'I' => ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
            'C' => ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
            'K' => ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
            'E' => ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
            'R' => ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
            _ => ["00000", "00000", "00000", "00000", "00000", "00000", "00000"]
        };

        for (int row = 0; row < rows.Length; row++)
        {
            int bits = Convert.ToInt32(rows[row], 2);
            yield return (row, bits);
        }
    }

    private bool IsActiveLocked()
    {
        return _configuration != null
            && _servedFrameCount < _configuration.FlickeringFramesThreshold;
    }

    private void CompleteInjection()
    {
        lock (_sync)
        {
            _configuration = null;
            _injectedFrame = null;
            _servedFrameCount = 0;
        }
        InjectionCompleted?.Invoke();
    }
}
