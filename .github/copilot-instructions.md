# Copilot instructions (VilsSharpX)

## Project snapshot
- WPF app targeting `net8.0-windows` (`UseWPF=true`) that visualizes 8-bit grayscale frames (Gray8) and computed comparisons.
- Four panes in the UI: **A (AVTP/Generator)**, **B (LVDS from Aurix)**, **C (LSM Camera — Basler)**, **D (Comparison)** (see `MainWindow.xaml`).
- Comparison modes selectable via ComboBox: **LVDS-AVTP** (default), **LVDS-LSM**, **AVTP-LSM**.

## Big-picture data flow (most important)
- Ethernet AVTP frame -> `AvtpLiveCapture` sniffs ethertype 0x22F0 -> `AvtpRvfParser.TryParseAvtpRvfEthernet` -> `RvfReassembler.Push()` copies lines into a 320×80 frame -> when `EndFrame` emits `OnFrameReady(outFrame, meta)`.
- Ethernet LVDS frame -> `OsramEthCapture`/`NichiaEthCapture` sniffs ethertype 0x88B5, magic "OS"/"NI" -> fragment reassembly -> complete 320×80 (Osram) or 256×64 (Nichia) frame.
- Basler camera -> `BaslerCameraCapture` via Pylon SDK -> `OnFrameReady(bytes, w, h)` -> `FrameDownscaler.DownscaleBlockAverage()` to LVDS resolution for comparison.
- UI updates must be marshaled to the UI thread via `Dispatcher.Invoke(...)`.

## Comparison algorithm (Pane D)
- `DiffRenderer.ComparePixelToBgr()` computes deviation = B - A (measured minus reference).
- Color coding: Green = within deadband, Yellow/Red = B > A, Turquoise/Blue = B < A, Magenta = dark pixel (A>0, B==0), White = both zero (optional Flip).
- `zeroThreshold` parameter (camera modes): reference must be exactly 0 for white-flip; threshold only relaxes measured side.
- `FrameDownscaler.DownscaleBlockAverage()` reduces camera resolution to LVDS resolution via block averaging before comparison.

## RVF/AVTP protocol assumptions (don't break these)
- AVTP frames use ethertype 0x22F0; RVF payload parsing is in `AvtpRvfParser`.
- Stream is currently strict for this device: width=320, height=80 (`RvfProtocol.W/H`). Reassembly ignores chunks outside this.
- Chunk payload is `numLines * width` bytes. `line` is **1-based**.
- `RvfReassembler` tracks missing sequences as "gaps" and emits a **copy** of the frame so the next frame can keep assembling.

## Rendering/concurrency patterns
- Rendering uses `WriteableBitmap` with `PixelFormats.Gray8` and `WritePixels` (see `Blit(...)` in `MainWindow.xaml.cs`).
- Background loops are started with `Task.Run(...)` and stopped via `CancellationTokenSource` (`Start/Stop`). Keep UI thread free.
- If you add new callbacks from background threads, use `Dispatcher.Invoke/BeginInvoke` before touching WPF controls.
- Basler camera frames arrive on a background grab thread — always Dispatcher-marshal.

## Basler camera integration
- `BaslerCameraCapture.cs` wraps Pylon SDK; uses LatestImageOnly strategy.
- `CameraConfigWindow.xaml.cs` provides live preview, parameter editing, and .pfs config import.
- Camera trigger from Aurix (`camera_trigger.c`) drives P23.1 in sync with LVDS frame-complete.
- Auto-calibration finds the optimal AOI to match the LED matrix.

## SmartVisio Adapter control
- `AdapterModeCommand.cs` sends SET_ADAPTER_MODE Ethernet command to Aurix.
- `adapter_ctrl.c/h` on firmware controls GPIO for ECU mode vs. direct mode.

## Logs and crash handling
- App is `WinExe` (no console). Runtime diagnostics are written to files:
    - `diagnostic.log` for AVTP/capture diagnostic traces (written via `DiagnosticLogger`).
    - `crash.log` for unhandled exceptions (see `App.xaml.cs`).
- Prefer file logging (append) over `Console.WriteLine` for anything important.

## Developer workflows
- Build: `dotnet build` (from the project folder).
- Run: `dotnet run` (or `dotnet run --project VilsSharpX.csproj`).
- No test project is present; changes are typically validated by running the app and observing panes + `diagnostic.log`.

## When making changes
- Keep constants consistent: `W=320`, active height `80`, LVDS height `84` with bottom 4 metadata lines cropped.
- Preserve protocol compatibility in `RvfProtocol.cs`, `AvtpRvfParser.cs`, and `RvfReassembler.cs`.
- If you refactor frame handling, mind that `Frame` clones buffers for safety today; avoid introducing shared-buffer race bugs.
- Comparison mode logic: `_comparisonMode` 0=LVDS-AVTP, 1=LVDS-LSM, 2=AVTP-LSM. Always select correct reference/measured pair.

---
