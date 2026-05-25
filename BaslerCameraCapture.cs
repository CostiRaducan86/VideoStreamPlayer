using System;
using System.Diagnostics;
using Basler.Pylon;

#pragma warning disable CS8602 // _camera is guaranteed non-null after OpenAndStart()

namespace VilsSharpX;

/// <summary>
/// Captures grayscale frames from a Basler USB3 Vision camera using the Pylon .NET SDK.
/// Uses the StreamGrabber's internal grab loop thread with LatestImages strategy
/// so the UI always receives the most recent frame.
/// </summary>
public sealed class BaslerCameraCapture : IDisposable
{
    // ── FPS estimation ──────────────────────────────────────────────
    private const double FpsEmaAlpha = 0.05;
    private readonly Stopwatch _fpsSw = Stopwatch.StartNew();
    private long _lastFrameTicks;

    public double FpsEma { get; private set; }
    public long FramesCompleted { get; private set; }
    public bool IsCapturing { get; private set; }

    public int FrameWidth { get; private set; }
    public int FrameHeight { get; private set; }

    /// <summary>
    /// Fired on the Pylon grab-loop thread when a complete Gray8 frame is available.
    /// The byte[] contains Width*Height pixels in row-major order.
    /// Subscriber must marshal to the UI thread if touching WPF controls.
    /// </summary>
    public event Action<byte[], int, int>? OnFrameReady;

    private Camera _camera = null!;
    private readonly Action<string> _log;
    private bool _disposed;

    /// <summary>
    /// Exposes the underlying Pylon Camera object for parameter reading by CameraConfigWindow.
    /// Do NOT call Open/Close/Dispose on this — lifecycle is managed by BaslerCameraCapture.
    /// </summary>
    public Camera? InternalCamera => _disposed ? null : _camera;

    private BaslerCameraCapture(Action<string> log) => _log = log;

    // ── Factory ─────────────────────────────────────────────────────

    /// <summary>
    /// Finds the first available Basler camera, opens it, configures Mono8 + continuous acquisition,
    /// and starts the internal grab loop. Returns a capture instance that fires <see cref="OnFrameReady"/>.
    /// </summary>
    public static BaslerCameraCapture Start(Action<string> log)
    {
        var cap = new BaslerCameraCapture(log);
        try
        {
            cap.OpenAndStart();
        }
        catch
        {
            cap.Dispose();
            throw;
        }
        return cap;
    }

    private void OpenAndStart()
    {
        _camera = new Camera();
        _camera.CameraOpened += (sender, e) => Configuration.AcquireContinuous(sender!, e);
        _camera.Open();

        // Log camera info
        string model = _camera.CameraInfo[CameraInfoKey.ModelName] ?? "?";
        string serial = _camera.CameraInfo[CameraInfoKey.SerialNumber] ?? "?";
        _log($"[basler] Camera opened: {model} (S/N {serial})");

        // Set pixel format to Mono8 (Gray8) if available
        _camera.Parameters[PLCamera.PixelFormat].TrySetValue(PLCamera.PixelFormat.Mono8);

        // Configure hardware trigger from Aurix P23.1 (Line3, rising edge)
        _camera.Parameters[PLCamera.TriggerSelector].TrySetValue(PLCamera.TriggerSelector.FrameStart);
        if (_camera.Parameters[PLCamera.TriggerMode].TrySetValue(PLCamera.TriggerMode.On))
        {
            _camera.Parameters[PLCamera.TriggerSource].TrySetValue(PLCamera.TriggerSource.Line3);
            _camera.Parameters[PLCamera.TriggerActivation].TrySetValue(PLCamera.TriggerActivation.RisingEdge);
            _log("[basler] Hardware trigger: Line3, RisingEdge");
        }
        else
        {
            _log("[basler] Hardware trigger not available, using free-run");
        }

        // Read actual dimensions
        FrameWidth = (int)_camera.Parameters[PLCamera.Width].GetValue();
        FrameHeight = (int)_camera.Parameters[PLCamera.Height].GetValue();
        _log($"[basler] Resolution: {FrameWidth}×{FrameHeight}, PixelFormat: {_camera.Parameters[PLCamera.PixelFormat].GetValue()}");

        // Subscribe to grab events
        _camera.StreamGrabber.ImageGrabbed += OnImageGrabbed;

        // Start grabbing with the internal grab-loop thread (LatestImages = always get freshest frame)
        _camera.StreamGrabber.Start(GrabStrategy.LatestImages, GrabLoop.ProvidedByStreamGrabber);
        IsCapturing = true;
        _lastFrameTicks = _fpsSw.ElapsedTicks;
        _log("[basler] Grab started (LatestImages, ProvidedByStreamGrabber)");
    }

    // ── Grab callback (runs on Pylon's internal thread) ─────────────

    private void OnImageGrabbed(object? sender, ImageGrabbedEventArgs e)
    {
        try
        {
            IGrabResult grabResult = e.GrabResult;
            if (!grabResult.GrabSucceeded)
            {
                _log($"[basler] Grab error: {grabResult.ErrorCode} {grabResult.ErrorDescription}");
                return;
            }

            int w = grabResult.Width;
            int h = grabResult.Height;

            // Copy pixel data out of the grab result buffer before it is reused
            if (grabResult.PixelData is not byte[] srcPixels || srcPixels.Length < w * h)
                return;

            var frame = new byte[w * h];
            Buffer.BlockCopy(srcPixels, 0, frame, 0, frame.Length);

            // FPS estimation (EMA)
            long now = _fpsSw.ElapsedTicks;
            double dtSec = (now - _lastFrameTicks) / (double)Stopwatch.Frequency;
            _lastFrameTicks = now;
            if (dtSec > 0.0001)
            {
                double instantFps = 1.0 / dtSec;
                FpsEma = FramesCompleted == 0
                    ? instantFps
                    : FpsEma * (1.0 - FpsEmaAlpha) + instantFps * FpsEmaAlpha;
            }

            FramesCompleted++;
            FrameWidth = w;
            FrameHeight = h;

            OnFrameReady?.Invoke(frame, w, h);
        }
        catch (Exception ex)
        {
            _log($"[basler] OnImageGrabbed exception: {ex.Message}");
        }
    }

    // ── Auto-calibration support ────────────────────────────────────

    /// <summary>
    /// Run auto-calibration: temporarily resets AOI to full sensor, grabs one frame,
    /// detects the LED matrix bounding box, then applies the new AOI.
    /// Must be called from the UI thread. The grab loop is stopped/restarted.
    /// Returns the calibration result, or null on failure.
    /// </summary>
    public BaslerAutoCalibration.CalibrationResult? RunAutoCalibration()
    {
        if (_disposed || _camera == null) return null;

        bool wasGrabbing = _camera.StreamGrabber.IsGrabbing;

        try
        {
            // Stop grab loop for AOI changes
            if (wasGrabbing)
                _camera.StreamGrabber.Stop();

            // Reset AOI to full sensor to get the complete FOV
            BaslerAutoCalibration.ResetAoi(_camera, _log);

            // Grab a single frame for analysis
            _camera.StreamGrabber.Start(1, GrabStrategy.OneByOne, GrabLoop.ProvidedByUser);
            var grabResult = _camera.StreamGrabber.RetrieveResult(5000, TimeoutHandling.Return);

            if (grabResult == null || !grabResult.GrabSucceeded)
            {
                _log("[basler-cal] Failed to grab calibration frame");
                RestartGrabLoop();
                return null;
            }

            int w = grabResult.Width;
            int h = grabResult.Height;
            byte[]? pixels = grabResult.PixelData as byte[];
            if (pixels == null || pixels.Length < w * h)
            {
                grabResult.Dispose();
                _log("[basler-cal] Invalid pixel data in calibration frame");
                RestartGrabLoop();
                return null;
            }

            // Copy out before disposing
            var frame = new byte[w * h];
            Buffer.BlockCopy(pixels, 0, frame, 0, frame.Length);
            grabResult.Dispose();

            // Detect matrix region
            var cal = new BaslerAutoCalibration();
            var result = cal.DetectMatrixRegion(frame, w, h);

            if (result == null)
            {
                _log("[basler-cal] No LED matrix detected (is a bright pattern displayed on the LSM?)");
                RestartGrabLoop();
                return null;
            }

            // Apply AOI to camera
            bool ok = BaslerAutoCalibration.ApplyToCamera(_camera, result, _log);
            if (ok)
            {
                FrameWidth = result.Width;
                FrameHeight = result.Height;
            }

            // Restart grab loop
            RestartGrabLoop();
            return result;
        }
        catch (Exception ex)
        {
            _log($"[basler-cal] Calibration error: {ex.Message}");
            RestartGrabLoop();
            return null;
        }
    }

    /// <summary>
    /// Reset AOI back to full sensor resolution and restart.
    /// </summary>
    public void ResetCalibration()
    {
        if (_disposed || _camera == null) return;

        bool wasGrabbing = _camera.StreamGrabber.IsGrabbing;
        try
        {
            if (wasGrabbing)
                _camera.StreamGrabber.Stop();

            BaslerAutoCalibration.ResetAoi(_camera, _log);

            FrameWidth = (int)_camera.Parameters[PLCamera.Width].GetValue();
            FrameHeight = (int)_camera.Parameters[PLCamera.Height].GetValue();

            RestartGrabLoop();
        }
        catch (Exception ex)
        {
            _log($"[basler-cal] Reset error: {ex.Message}");
            RestartGrabLoop();
        }
    }

    private void RestartGrabLoop()
    {
        try
        {
            if (!_camera.StreamGrabber.IsGrabbing)
            {
                _camera.StreamGrabber.Start(GrabStrategy.LatestImages, GrabLoop.ProvidedByStreamGrabber);
                _lastFrameTicks = _fpsSw.ElapsedTicks;
            }
        }
        catch (Exception ex)
        {
            _log($"[basler] Failed to restart grab loop: {ex.Message}");
        }
    }

    /// <summary>
    /// Stop the grab loop externally (e.g. from CameraConfigWindow to apply parameter changes).
    /// </summary>
    public void StopGrab()
    {
        if (_disposed || !_camera.StreamGrabber.IsGrabbing) return;
        try
        {
            _camera.StreamGrabber.Stop();
            IsCapturing = false;
            _log("[basler] Grab stopped (external)");
        }
        catch (Exception ex)
        {
            _log($"[basler] StopGrab error: {ex.Message}");
        }
    }

    /// <summary>
    /// Restart the grab loop externally (e.g. after parameter changes in CameraConfigWindow).
    /// </summary>
    public void StartGrab()
    {
        if (_disposed || _camera.StreamGrabber.IsGrabbing) return;
        try
        {
            _camera.StreamGrabber.Start(GrabStrategy.LatestImages, GrabLoop.ProvidedByStreamGrabber);
            _lastFrameTicks = _fpsSw.ElapsedTicks;
            IsCapturing = true;
            _log("[basler] Grab restarted (external)");
        }
        catch (Exception ex)
        {
            _log($"[basler] StartGrab error: {ex.Message}");
        }
    }

    // ── Cleanup ─────────────────────────────────────────────────────

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        IsCapturing = false;

        try
        {
            if (_camera.StreamGrabber.IsGrabbing)
                _camera.StreamGrabber.Stop();
        }
        catch { /* ignore */ }

        try { _camera.Close(); } catch { /* ignore */ }
        try { _camera.Dispose(); } catch { /* ignore */ }

        _log("[basler] Camera capture disposed");
    }
}
