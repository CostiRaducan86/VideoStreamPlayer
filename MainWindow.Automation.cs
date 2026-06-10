using System;
using System.IO;
using System.Windows.Media.Imaging;
using VilsSharpX.Api;

namespace VilsSharpX
{
    /// <summary>
    /// Automation bridge: connects the localhost REST API to the existing playback,
    /// capture and comparison logic. All public bridge methods marshal onto the UI
    /// thread so REST handlers never touch WPF controls from a background thread.
    /// </summary>
    public partial class MainWindow : IGuiAutomationBridge
    {
        private ApiHost? _apiHost;

        // Latest comparison stats, captured on the UI thread in FormatComparisonStats
        // and read off-thread by the automation API.
        private readonly object _statsLock = new();
        private int _statMaxDiff;
        private int _statMinDiff;
        private double _statMeanAbsDiff;
        private int _statAboveDeadband;   // == total_pixels_dev
        private int _statTotalDarkPixels;

        /// <summary>Stores the most recent comparison statistics for the automation API.</summary>
        private void StoreComparisonStats(int maxDiff, int minDiff, double meanAbsDiff, int aboveDeadband, int totalDarkPixels)
        {
            lock (_statsLock)
            {
                _statMaxDiff = maxDiff;
                _statMinDiff = minDiff;
                _statMeanAbsDiff = meanAbsDiff;
                _statAboveDeadband = aboveDeadband;
                _statTotalDarkPixels = totalDarkPixels;
            }
        }

        /// <summary>Starts the localhost automation API. Best-effort; never throws.</summary>
        private void StartAutomationApi()
        {
            try
            {
                string bindAddress = _apiAllowRemote ? _apiBindAddress : "127.0.0.1";
                string apiKey = _apiAllowRemote ? _apiKey : string.Empty;

                if (_apiAllowRemote && string.IsNullOrWhiteSpace(apiKey))
                {
                    AppendDiagLog("[api] Remote API requested, but ApiKey is empty. Falling back to localhost-only mode.");
                    bindAddress = "127.0.0.1";
                }

                _apiHost = new ApiHost(this, bindAddress, _apiPort, apiKey, _apiAllowedCidrs);
                _apiHost.Start();
                AppendDiagLog($"[api] Automation REST API listening on {_apiHost.BaseUrl}");
                AppendDiagLog($"[api] mode={(bindAddress == "127.0.0.1" ? "loopback-only" : "remote-enabled")}, apiKeyRequiredForRemote={!string.IsNullOrWhiteSpace(apiKey)}");
                if (_apiAllowedCidrs.Length > 0)
                    AppendDiagLog($"[api] allowlist={string.Join(", ", _apiAllowedCidrs)}");
            }
            catch (Exception ex)
            {
                _apiHost = null;
                AppendDiagLog($"[api] Failed to start automation API: {ex.Message}");
            }
        }

        /// <summary>Stops the localhost automation API.</summary>
        private void StopAutomationApi()
        {
            try { _apiHost?.Stop(); } catch { /* ignore */ }
            _apiHost = null;
        }

        // ---- IGuiAutomationBridge implementation ----

        bool IGuiAutomationBridge.IsRunning => _playback.IsRunning;

        bool IGuiAutomationBridge.IsPaused => _playback.IsPaused;

        void IGuiAutomationBridge.StartSimulation(int fps)
        {
            Dispatcher.Invoke(() =>
            {
                if (_playback.IsRunning)
                {
                    if (_playback.IsPaused) Resume();
                    return;
                }

                if (TxtFps != null) TxtFps.Text = fps.ToString();
                Start(fps);
                if (BtnStart != null) BtnStart.Content = "⏸ Pause";
                ApplyButtonStates(isRunning: true, isPaused: false);
            });
        }

        void IGuiAutomationBridge.StopSimulation()
        {
            Dispatcher.Invoke(() => SendBlackAndStop("STOP"));
        }

        void IGuiAutomationBridge.PauseSimulation()
        {
            Dispatcher.Invoke(() =>
            {
                if (_playback.IsRunning && !_playback.IsPaused)
                    Pause();
            });
        }

        void IGuiAutomationBridge.ResumeSimulation()
        {
            Dispatcher.Invoke(() =>
            {
                if (_playback.IsRunning && _playback.IsPaused)
                    Resume();
            });
        }

        void IGuiAutomationBridge.SetComparisonSettings(int? mode, int? deadband, int? bDelta)
        {
            Dispatcher.Invoke(() =>
            {
                if (mode.HasValue)
                {
                    _comparisonMode = Math.Clamp(mode.Value, 0, ComparisonModeLabels.Length - 1);
                    if (CmbComparisonMode != null) CmbComparisonMode.SelectedIndex = _comparisonMode;
                    UpdateComparisonModeLabel();
                }

                if (deadband.HasValue)
                    SetDiffThreshold(deadband.Value, updateText: true);

                if (bDelta.HasValue)
                {
                    _bValueDelta = Math.Clamp(bDelta.Value, -255, 255);
                    if (TxtBDelta != null) TxtBDelta.Text = _bValueDelta.ToString();
                }

                SaveUiSettings();
                RenderAll();
            });
        }

        ComparisonStats IGuiAutomationBridge.GetComparisonStats()
        {
            lock (_statsLock)
            {
                return new ComparisonStats
                {
                    MaxPositiveDev = Math.Max(0, _statMaxDiff),
                    MaxNegativeDev = Math.Min(0, _statMinDiff),
                    AveragePixelsDev = _statMeanAbsDiff,
                    TotalPixelsDev = _statAboveDeadband,
                    TotalDarkPixels = _statTotalDarkPixels
                };
            }
        }

        byte[] IGuiAutomationBridge.GetFrameSnapshotPng(string pane)
        {
            return Dispatcher.Invoke(() =>
            {
                WriteableBitmap wb = pane switch
                {
                    "A" => _wbA,
                    "B" => _wbB,
                    "D" => _wbD,
                    _ => throw new InvalidOperationException($"Unknown pane '{pane}'.")
                };

                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(wb));
                using var ms = new MemoryStream();
                encoder.Save(ms);
                return ms.ToArray();
            });
        }
    }
}
