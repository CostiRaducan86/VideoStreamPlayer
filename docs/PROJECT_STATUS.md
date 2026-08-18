# VilsSharpX - Comprehensive Project Status (Last Updated: 2026-05-28)

**Read this file first after any VS Code restart or session interruption.**

---

## 0. Current Baseline (2026-05-28)

This section is the current source of truth for session recovery. Some older sections below are kept for historical context and may describe earlier milestones.

### Application Baseline

- The WPF application builds cleanly on .NET 8 with `dotnet build .\VilsSharpX.csproj` (0 warnings, 0 errors).
- The app now has **4 display panes**: A (AVTP/Generator), B (LVDS from Aurix), C (LSM Camera — Basler), D (Comparison).
- **Three comparison modes** selectable via ComboBox: LVDS-AVTP (default), LSM-LVDS, LSM-AVTP.
- **Basler camera integration** via Pylon SDK with auto-calibration, .pfs config import, and live preview.
- **FrameDownscaler** block-averages camera frames to LVDS resolution for pixel-accurate comparison.
- **SmartVisio Adapter** control: Ethernet commands to switch between ECU mode and direct mode.
- **Camera trigger sync**: Aurix drives P23.1 in sync with LVDS frame-complete for Basler external trigger.
- FPS display is stable and accurate (EMA-based, separate for each pane).

### Implemented Data Paths

- AVTP/RVF live capture and PCAP replay via SharpPcap, `AvtpRvfParser`, and `RvfReassembler`.
- LVDS pixel capture from Aurix via Ethernet fragments (`OsramEthCapture`, `NichiaEthCapture`).
- Basler camera capture via `BaslerCameraCapture` (Pylon SDK, LatestImageOnly strategy).
- Scene, sequence, PGM/BMP, and uncompressed AVI playback as file sources.
- A/B/C/D rendering, multi-mode comparison, dark-pixel reporting/compensation, snapshots, AVI recording, and AVTP TX.
- CAN/UART diagnostic monitor: Monitor, RawCan, filters, paging, detail popup, capture counters, start/stop sniff commands.

### AURIX / Firmware Baseline

- Platform: AURIX TC397 TFT board.
- LVDS pixel capture: ASCLIN1/P14.8, DMA channel 1 (Osram 20 Mbaud 8O1 / Nichia 12.5 Mbaud 8N1).
- Diagnostic UART bridge: ASCLIN5/ASCLIN4 on X103-28/29/31/34 (2 Mbaud 8O2 through Adapter_V2 transceivers; CPU2 byte relay).
- Camera trigger: STM0 timer on P23.1 (free-run or frame-synced modes).
- SmartVisio Adapter GPIO control: `adapter_ctrl.c/h` (ECU mode / direct mode switching).
- CD:0 stall bug fully resolved (RFO mask extended to 0xFFF + recovery watchdog).

### Known Gaps / Next Work

- Nichia diagnostic semantic validation against captures still pending.
- UartTransaction tab is still a placeholder.
- CAN/UART session recording, chronological paging, `.rply` export, and multiple Detail windows are implemented.
- Unit test coverage: 0% (no test project).

## 1. Project Overview & Mission

VilsSharpX is a **pixel-accurate inspection tool** for 8-bit grayscale video frames in automotive ECU development:

**Core Capabilities:**

- Ingest frames from **AVTP/RVF** (Ethernet live capture or PCAP replay)
- Receive real **LVDS frames** from Aurix (Osram 320×80 or Nichia 256×64) via Ethernet fragments
- Capture frames from **Basler USB3 camera** (Pylon SDK) with auto-calibration AOI
- Support **Scene mode** (loops through image files for A/B toggle testing)
- Support **AVI playback** as input source (indexed, uncompressed only)
- Visualize **A (AVTP/Generator)**, **B (LVDS)**, **C (LSM Camera)**, **D (Comparison)** with pixel-perfect zoom/pan
- Multi-mode comparison: **LVDS-AVTP**, **LSM-LVDS**, **LSM-AVTP** with block-average downscaling
- Provide diagnostics (FPS per pane, dropped frames, gaps, sequence tracking)
- Record A/B/D video streams (AVI) and generate Excel compare reports (.xlsx)
- Detect and report **dark pixels** (A>0 but ECU output B==0)
- Optional **dark pixel compensation** (Cassandra-style kernel applied to B before render/record)
- Transmit AVTP/RVF frames over Ethernet
- One-click frame snapshot export (PNG + XLSX report)
- CAN/UART diagnostic monitor with live register decoding
- SmartVisio Adapter mode switching (ECU vs. direct mode)
- Camera trigger synchronization with LVDS frame-complete

**Target Users:** ECU validation engineers, test automation, visual regression testing

---

## 2. Current Architecture State

### 2.1 Comprehensive Documentation

Recent architecture analysis produced detailed documentation:

📄 **[ARCHITECTURE_DIAGRAM.md](tehnical_docs/ARCHITECTURE_DIAGRAM.md)**

- Mermaid block diagram with 10 subgraphs
- Color-coded layers (Ingress, Processing, Rendering, UI, Storage, Transmit)
- 3 documented data flow paths:
  1. Live AVTP → Packet Capture → Parser → Reassembler → Frame Ready → UI
  2. File Playback → Scene/AVI/PCAP → Frame Ready → UI
  3. UI → TX Manager → Packet Builder → Ethernet Send

📄 **[ARCHITECTURE_REVIEW.md](tehnical_docs/ARCHITECTURE_REVIEW.md)**

- 11-section technical review (~1200 lines, English)
- Executive summary, system overview, 8 architectural layers
- Concurrency model, design patterns, performance characteristics
- Protocol constraints (RVF/AVTP specifics)
- 8 identified strengths (separation of concerns, event-driven, WriteableBitmap efficiency, etc.)
- 6 improvement opportunities (PGM parser hardening, MVVM migration, unit tests, etc.)
- Short/Medium/Long-term recommendations

### 2.2 Architecture Layers

The application follows a layered, manager-based architecture:

```text
┌─────────────────────────────────────────────────────────────┐
│ DATA SOURCES                                                │
│  • Live Ethernet (SharpPcap)                               │
│  • PCAP files (replay)                                     │
│  • Scene files (.scene) – image sequences                  │
│  • AVI files (uncompressed, indexed)                       │
│  • PGM/BMP single images                                   │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ NETWORK & PARSING LAYER                                    │
│  • AvtpLiveCapture – live packet sniffing (ethertype 0x22F0)│
│  • AvtpRvfParser – RVF protocol parsing                    │
│  • RvfReassembler – line-based frame reassembly            │
│  • PcapAvtpRvfReplay – PCAP playback                       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ FRAME PROCESSING                                           │
│  • Frame cloning (avoid shared-buffer races)               │
│  • Dark pixel detection (A>0 && B==0)                      │
│  • DarkPixelCompensation – Cassandra kernel                │
│  • DiffRenderer – compute |A−B| with threshold             │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ RENDERING LAYER                                            │
│  • BitmapUtils.Blit() – WriteableBitmap + PixelFormats.Gray8│
│  • OverlayRenderer – numeric overlays, pixel inspector     │
│  • ZoomPanManager – per-pane transforms, letterbox-aware  │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ UI LAYER (WPF)                                             │
│  • MainWindow.xaml – Grid layout (60% left / 40% right)    │
│  • 3 panes: Cadran A (AVTP), Cadran B (LVDS), Cadran D (DIFF)│
│  • Right panels: CAN/UART monitor, AVTP Status              │
│  • UiSettingsManager – persist settings to AppData        │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ STORAGE & OUTPUT                                           │
│  • AviRecorder – 3-stream AVI (SharpAvi)                   │
│  • FrameSnapshotSaver – PNG + XLSX (ClosedXML)            │
│  • RecordingManager – orchestrate recording lifecycle      │
│  • DiagnosticLogger – file-based logging (no console)     │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ TRANSMIT LAYER                                             │
│  • AvtpTransmitManager – orchestrate TX state              │
│  • AvtpPacketBuilder – construct RVF packets               │
│  • AvtpEthernetSender – send via SharpPcap                 │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 Manager Classes (Separation of Concerns)

- **PlaybackStateManager** – controls Start/Stop/Pause, coordinates source switching
- **LiveCaptureManager** – owns AvtpLiveCapture, handles frame-ready events
- **RecordingManager** – toggles recording, manages AviRecorder lifecycle
- **AvtpTransmitManager** – toggles TX, manages AvtpRvfTransmitter lifecycle
- **UiSettingsManager** – load/save settings from `%APPDATA%\VilsSharpX\settings.json`
- **ZoomPanManager** – per-pane zoom/pan state, unified transforms

---

## 3. Non-Negotiable Invariants

**Resolution & Geometry:**

- Width: **W = 320 px**
- Active height: **H_ACTIVE = 80 px**
- LVDS height: **H_LVDS = 84 px** (bottom 4 lines are metadata, cropped for display)
- Frame size: **25,600 bytes** (320×80)

**Threading & Concurrency:**

- All WPF control updates must be on the UI thread (`Dispatcher.Invoke/BeginInvoke`)
- Background loops run in `Task.Run(...)` with `CancellationToken` for clean shutdown
- Frames are **cloned** on publish to avoid shared-buffer races

**Protocol Constraints:**

- AVTP ethertype: **0x22F0**
- RVF reassembly: line numbers are **1-based**, chunk payload is `numLines * width` bytes
- Frame-ready publishes a **copy** of the buffer (safe for multi-consumer)

**Rendering & UX:**

- Rendering: `WriteableBitmap`, `PixelFormats.Gray8`, nearest-neighbor scaling
- Pixel inspector: letterbox-aware, pixel_ID is **1..(W*H_ACTIVE)**
- Zoom/pan: per-pane, overlays stay aligned

---

## 4. Recent UI Improvements (2026-01-16 Session)

### 4.1 Layout Redesign

**Goal:** Add real-time monitoring panels without compromising main visualization

**Changes:**

- Grid layout changed from 2 columns to **3 columns** with proportions **3* : 3* : 4*** (60% left, 40% right)
- Row 0: **Cadran A (AVTP)** spanning cols 0-1, **CAN/UART Communication** panel (col 2, RowSpan 2)
- Row 1: **Cadran B (LVDS)** col 0, **Cadran D (DIFF)** col 1
- Row 2: Control buttons + 3 config groups (cols 0-1), **AVTP Status** panel (col 2)

**Result:** Clean visual separation, no overlap, proper alignment

### 4.2 CAN/UART Monitor Panel (Right-Top)

**Purpose:** Live diagnostic monitor for LSM CAN/UART records.

**Implementation:**

- Monitor tab with paginated decoded records and filters
- RawCan tab with scrollable raw diagnostic payloads
- UartTransaction tab reserved for the next detailed transaction view
- Record/Stop buttons controlling AURIX diagnostic sniffing via `DiagSniffCommand`
- Double-click detail popup (`CanDetailWindow`) with classic VILS fields

### 4.3 AVTP Status Panel (Right-Bottom)

**Purpose:** Real-time AVTP diagnostics

**Metrics:**

- `LblStatus` – current state (e.g., "Running @ 30 fps")
- `LblDiffStats` – diff statistics (e.g., "Diff max=42, avg=12.3")
- `LblAvtpInFps` – AVTP input FPS
- `LblAvtpDropped` – dropped frame count

**Previous Location:** Footer (Row 3, spanning all columns)  
**New Location:** Dedicated panel in col 2, row 2 (aligned with config groups)

### 4.4 Config Group Proportions

**Challenge:** 3 groups (Hardware, App Settings, Ethernet) had equal widths, causing text truncation and cramped controls

**Solution:**

- Changed from `Auto` width to **star-sizing** with proportions **5* : 2* : 3*** (50% / 20% / 30%)
- **Hardware Config** (50%): Needs wide combo (Device type, NIC selection)
- **App Settings** (20%): Small numeric inputs (FPS, threshold, pixel ID)
- **Ethernet Config** (30%): Medium-width text boxes (MAC address, Stream ID)

**Control Width Optimizations:**

- NIC ComboBox: 160px → **80px** (with HorizontalAlignment="Stretch" for responsiveness)
- MAC TextBoxes: 160px → **110px**
- Label widths increased: Hardware/App Settings **110px**, Ethernet **85px**
- Abbreviated labels: "Deviation threshold" → "Dev. threshold", "Force dead pixel ID" → "Dead pixel ID"

**Result:** All text fully visible, no truncation, proportional scaling works correctly

### 4.5 Git Commit Baseline

**Commit:** `8481453`  
**Message:** `feat(ui): Add CAN/UART and AVTP Status panels with optimized layout`

**Why:** Stable checkpoint before further development; all UI changes validated with `dotnet build`

---

## 5. Data Flow (Detailed)

### 5.1 Live AVTP Capture Flow

```text
Ethernet wire
  → AvtpLiveCapture (SharpPcap, filter ethertype 0x22F0)
    → AvtpRvfParser.TryParseAvtpRvfEthernet(pkt)
      → RvfReassembler.Push(lineNum, lineCount, payload)
        → copies lines into internal 320×80 buffer
        → on EndFrame → OnFrameReady(clonedFrame, metadata)
          → MainWindow subscribes → Dispatcher.Invoke(...)
            → updates _avtpFrame, _avtp_meta
            → RenderAll() → Blit() → WriteableBitmap update
```

**Important:** MainWindow uses `Dispatcher.Invoke` because `OnFrameReady` fires from SharpPcap's background thread.

### 5.2 PCAP Replay Flow

```text
User clicks "Load Files…" → selects .pcap
  → PcapAvtpRvfReplay.Start()
    → background loop reads packets
      → same AvtpRvfParser → RvfReassembler path
        → OnFrameReady → UI update (marshaled)
```

### 5.3 Scene Playback Flow

```text
User clicks "Load Files…" → selects .scene
  → SceneLoader.Load(path) → parses steps + delays
    → ScenePlayer.Start()
      → background loop: load each image (PGM/BMP)
        → wait delayMs
        → loop back to step 0 if loop=true
          → every image load triggers _avtpFrame update → RenderAll()
```

### 5.4 AVI Playback Flow

```text
User clicks "Load Files…" → selects .avi
  → AviUncompressedVideoReader opens AVI (requires idx1 chunk)
    → AviSourcePlayer.Start()
      → background loop: ReadFrame()
        → convert to Gray8 (if 24/32bpp)
        → crop to 320×80
        → emit frame → _avtpFrame update → RenderAll()
```

**Frame duration:** Uses AVI's inherent frame timing (independent of FPS textbox)  
**Pause behavior:** Prev/Next steps through AVI frames, UI updates immediately

### 5.5 Transmit Flow

```text
User clicks "Toggle TX"
  → AvtpTransmitManager.Start()
    → AvtpRvfTransmitter starts background loop
      → reads _latest B frame (or fallback)
        → AvtpPacketBuilder.BuildRvfPackets(frame)
          → splits into chunks (numLines per packet)
            → AvtpEthernetSender.Send(pkt) via SharpPcap
```

---

## 6. Key Features & Semantics

### 6.1 Compare/DIFF Semantics (CRITICAL)

**Deviation Definition:** **B − A** (measured minus reference)

**Multi-Mode Comparison:**

- Mode 0 (LVDS-AVTP): A = AVTP frame, B = LVDS frame
- Mode 1 (LSM-LVDS): A = LVDS frame, B = downscaled camera frame
- Mode 2 (LSM-AVTP): A = AVTP frame, B = downscaled camera frame

**Consistency Across:**

- DIFF pane rendering (color-coded BGR24)
- Numeric overlay labels (per-pixel values)
- XLSX report columns (`PixelValue_A`, `PixelValue_B`, `Diff_B_A`)
- Tooltip labels (mode-specific: AVTP/LVDS/LSM)

**User-Facing Term:** "Deviation threshold" (formerly "deadband")

**Implementation:** `DiffRenderer.ComparePixelToBgr(a, b, deadband, zeroZeroIsWhite, zeroThreshold)` → color-coded BGR visualization.

### 6.2 (0=0)→White Mapping

**Purpose:** Optional visualization enhancement

**Behavior:** When both A and B are 0, render pixel as **white** (instead of black) in DIFF pane

**Default:** **OFF** (see `AppSettings` migration)

**Use Case:** Quickly identify areas where both streams are inactive vs. areas with actual signal

### 6.3 Dark Pixel Detection

**Definition:** **A > 0 && B == 0** (input has signal but ECU output is black)

**Detection Points:**

- During compare computation
- During recording (tracked per frame)
- During one-click Save

**Reporting:**

- Highlighted rows in XLSX report
- Dedicated **`DarkPixels`** worksheet with pixel_ID, coordinates, A value

### 6.4 Dark Pixel Compensation

**Purpose:** Simulate ECU correction by boosting neighbors around dark pixels

**Cassandra-Style Kernel (applied to B before render/record):**

- **+15%:** N/S/E/W neighbors at distance 1
- **+10%:** Diagonal neighbors at distance 1
- **+5%:** N/S/E/W at distance 2

**Default:** **OFF**

**Implementation:** `DarkPixelCompensation.Apply(frame, darkPixelMask)` → modifies B in-place

**Effect Visibility:** Visible in DIFF pane, recorded in AVI, reflected in XLSX report

### 6.5 Force Dead Pixel ID

**Purpose:** Simulate a specific pixel failure for testing

**Behavior:** When set to a valid pixel_ID (1..25600), forces `B[pixel_ID] = 0` before compare/render

**Default:** **0** (disabled)

**Use Case:** Validate dark pixel detection + compensation logic without needing real defective hardware

---

## 7. Recording & Reporting

### 7.1 AVI Recording

**Output Location:** `docs/outputs/videoRecords/`

**Files Generated:**

- `<timestamp>_A.avi` (AVTP/Generator stream)
- `<timestamp>_B.avi` (LVDS stream with compensation applied if enabled)
- `<timestamp>_D.avi` (DIFF stream)

**Codec:** Uncompressed (Gray8), indexed (`EmitIndex1=true`)

**Frame Rate:** User-configurable (FPS textbox in App Settings group)

**Recording Lifecycle:**

1. User clicks "Record" → `RecordingManager.StartRecording()`
2. Background loop: every frame ready → `AviRecorder.WriteFrame(a, b, d)`
3. User clicks "Stop" → `AviRecorder.Finish()` → flushes + closes files

### 7.2 Compare Report (XLSX)

**Output Location:** `docs/outputs/videoRecords/` (during recording) or `docs/outputs/frameSnapshots/` (one-click Save)

**Report Structure:**

- **Main Sheet:** `FrameNr_XX`
  - Columns: `Pixel_ID`, `LinNr`, `ColNr`, `PixelValue_A`, `PixelValue_B`, `Diff_B_A`
  - All 25,600 pixels listed
- **DarkPixels Sheet:** Only rows where A>0 && B==0
  - Same columns as main sheet
  - Strong highlighting (yellow fill + bold font)

**Generation:** `AviRecorder.GenerateCompareReport()` using ClosedXML

### 7.3 One-Click Frame Snapshot (Save Button)

**Purpose:** Export currently displayed frame without starting full recording

**Workflow:**

1. User pauses playback
2. Navigate with Prev/Next to desired frame
3. Click **Save**
4. Generates:
   - `<timestamp>_AVTP.png` (pane A, 320×80, 1:1 pixels)
   - `<timestamp>_LVDS.png` (pane B, 320×80, 1:1 pixels)
   - `<timestamp>_Compare.png` (pane D, 320×80, 1:1 pixels)
   - `<timestamp>_Compare.xlsx` (same format as recording report)

**Output Location:** `docs/outputs/frameSnapshots/`

**Implementation:** `FrameSnapshotSaver.SaveSnapshot(a, b, d, meta)`

---

## 8. Settings Persistence

**File Location:** `%APPDATA%\VilsSharpX\settings.json`

**Managed By:** `UiSettingsManager` (singleton)

**Persisted Settings:**

- Device type (Osram_1Chip / Nichia_1Chip)
- NIC name (network interface for live capture)
- FPS (recording frame rate)
- Deviation threshold (compare sensitivity)
- Force dead pixel ID (simulation)
- Dark pixel compensation enabled (checkbox)
- (0=0)→white mapping enabled (checkbox)
- MAC address, Stream ID (Ethernet config)

**Migration Logic:**

- `AppSettings.Migrate()` ensures old defaults are updated:
  - Dark pixel compensation: **true → false**
  - (0=0)→white: **true → false**

**Load on Startup:** `UiSettingsManager.Load()` called in `MainWindow` constructor

**Save Triggers:**

- TextBox `LostFocus`
- ComboBox `SelectionChanged`
- CheckBox `Checked/Unchecked`

---

## 9. Logging & Diagnostics

**App Type:** `WinExe` (no console window)

**Log Files:**

- **`diagnostic.log`** – runtime traces for AVTP/capture/reassembly
  - Written by `DiagnosticLogger.Log(msg)`
  - Append mode, timestamped entries
  - Use for debugging packet loss, sequence gaps, frame timing

- **`crash.log`** – unhandled exceptions
  - Configured in `App.xaml.cs` (`DispatcherUnhandledException`, `UnhandledException`)
  - Captures stack trace + exception details

**Best Practice:** Always check `diagnostic.log` when investigating AVTP frame drops or reassembly issues.

---

## 10. Scene Format (Supported Subset)

**Purpose:** Loop through image files for A/B toggle testing

**File Extension:** `.scene`

**Minimal Format:**

```text
loop = true
delayMs = 500

img1 = 320x80_black.bmp
img2 = SLB_BL1_LeftMD_Osram_1Chip_320x84.pgm
img3 = HighBeam_Lane_OS.pgm
```

**Optional Per-Step Delay:**

```text
img1 = black.bmp
delayMs1 = 1000
img2 = white.bmp
delayMs2 = 200
```

**Comment Support:**

- Lines starting with `//`, `#`, `;` are ignored

**Path Resolution:**

- Relative paths resolve against the `.scene` file directory
- Absolute paths are supported

**Legacy Compatibility:**

- Old object-style scenes with `filename = "..."` still work

**Implementation:** `SceneLoader.cs` + `ScenePlayer.cs`

---

## 11. AVI Playback Implementation

**Purpose:** Load pre-recorded AVI files as input source (pane A)

**Requirements:**

- AVI must be **indexed** (`idx1` chunk present)
- Codec: **Uncompressed** only (8bpp Gray or 24/32bpp RGB converted to Gray8)

**Supported Formats:**

- 8bpp grayscale (direct copy)
- 24bpp RGB (converted to Gray8 via `Bgr24→Gray8`)
- 32bpp ARGB (converted to Gray8 via `Bgr32→Gray8`)

**Crop Behavior:**

- Frames are cropped to top-left **320×80** for display
- If AVI dimensions < 320×80, only available pixels are used

**Frame Timing:**

- Uses AVI's inherent frame duration (from `MicroSecPerFrame` in AVI header)
- Independent of the "FPS" textbox (which only affects recording)

**Pause/Step Behavior:**

- Prev/Next buttons step through AVI frames
- UI updates immediately (no delay)

**FPS Display:**

- "Running @" label shows estimated **content change FPS**
- Counts frame-to-frame differences per second (useful for detecting repeated frames)

**Implementation:** `AviUncompressedVideoReader.cs` + `AviSourcePlayer.cs`

**Known Limitations:**

- No codec support (MJPEG, H.264, etc.) – only uncompressed
- PGM loader uses simple heuristic for P5 binary format (skip 4 newlines) – may fail on malformed files

---

## 12. Key Files Reference

### 12.1 UI Layer

- **`MainWindow.xaml`** – WPF layout (Grid, 3 columns, 3 rows), control definitions, default checkbox states
- **`MainWindow.xaml.cs`** – code-behind, render pipeline, event handlers, frame processing orchestration

### 12.2 Network & Protocol

- **`AvtpLiveCapture.cs`** – SharpPcap wrapper, ethertype 0x22F0 filter
- **`AvtpRvfParser.cs`** – RVF packet parsing
- **`RvfReassembler.cs`** – line-based frame reassembly, gap tracking
- **`RvfProtocol.cs`** – constants (W, H, ethertype, etc.)
- **`PcapAvtpRvfReplay.cs`** – PCAP file playback
- **`OsramEthCapture.cs`** – Osram LVDS Ethernet capture (magic "OS", ethertype 0x88B5)
- **`NichiaEthCapture.cs`** – Nichia LVDS Ethernet capture (magic "NI", ethertype 0x88B5)
- **`BaslerCameraCapture.cs`** – Pylon SDK camera capture (LatestImageOnly strategy)

### 12.3 Frame Processing

- **`DiffRenderer.cs`** – multi-mode comparison with color coding (BGR24 output)
- **`FrameDownscaler.cs`** – block-average downscaler (camera→LVDS resolution)
- **`DarkPixelCompensation.cs`** – Cassandra kernel implementation
- **`BitmapUtils.cs`** – WriteableBitmap blitting (Gray8)
- **`ImageUtils.cs`** – image conversion utilities

### 12.4 Rendering & UI

- **`OverlayRenderer.cs`** – numeric overlays, pixel inspector, zeroThreshold support
- **`ZoomPanManager.cs`** – per-pane zoom/pan state
- **`PixelInspector.cs`** – hover tooltips, pixel_ID calculations, mode-specific labels
- **`CameraConfigWindow.xaml/.cs`** – Basler camera preview, parameter editing, .pfs import

### 12.5 Recording & Output

- **`AviRecorder.cs`** – 3-stream AVI writer + XLSX report generation
- **`FrameSnapshotSaver.cs`** – one-click Save (PNG + XLSX)
- **`RecordingManager.cs`** – recording lifecycle orchestration

### 12.6 Playback Sources

- **`SceneLoader.cs`** – parse `.scene` files
- **`ScenePlayer.cs`** – loop through scene steps
- **`AviSourcePlayer.cs`** – AVI playback orchestration
- **`AviUncompressedVideoReader.cs`** – AVI parsing (idx1 required)
- **`PgmLoader.cs`** – P2/P5 PGM file loading

### 12.7 Transmit & Control

- **`AvtpTransmitManager.cs`** – TX lifecycle orchestration
- **`AvtpRvfTransmitter.cs`** – transmit loop (100fps cap, gateway MAC filtering)
- **`AvtpPacketBuilder.cs`** – construct RVF packets from frame
- **`AvtpEthernetSender.cs`** – SharpPcap send wrapper
- **`AdapterModeCommand.cs`** – SmartVisio adapter Ethernet command (ECU/direct mode)
- **`DeviceModeCommand.cs`** – LSM device type Ethernet command (Osram/Nichia)
- **`DiagSniffCommand.cs`** – CAN diagnostic start/stop command

### 12.8 Managers & State

- **`PlaybackStateManager.cs`** – Start/Stop/Pause coordination
- **`LiveCaptureManager.cs`** – owns AvtpLiveCapture, frame-ready subscription
- **`UiSettingsManager.cs`** – settings persistence
- **`AppSettings.cs`** – settings model + migration

### 12.9 Utilities

- **`DiagnosticLogger.cs`** – file-based logging
- **`StatusFormatter.cs`** – format status strings for UI
- **`NetworkInterfaceUtils.cs`** – enumerate NICs
- **`SourceLoaderHelper.cs`** – unified file loading (PCAP/Scene/AVI/PGM/BMP)

### 12.10 Types & Enums

- **`RvfTypes.cs`** – Frame, FrameMeta, RvfChunk structures
- **`LsmDeviceType.cs`** – Osram_1Chip / Nichia_1Chip enum

### 12.11 CAN Diagnostic Monitor

- **`LsmCanDiagRecord.cs`** – v2 record model (RawPayload, DecodedRegisters, RawHex)
- **`LsmCanDiagParser.cs`** – binary parser (v1/v2 compat, VLAN strip)
- **`LsmCanDiagCapture.cs`** – SharpPcap capture thread (magic 0x4344)
- **`LsmCanDiagStore.cs`** – thread-safe ring buffer (512 entries)
- **`LsmRegisterMap.cs`** – TLD816K register name/type lookup (50+ entries)
- **`CanDetailWindow.xaml/.cs`** – modal detail popup (classic VILS fields)

### 12.12 Firmware (Aurix_Firmware/)

- **`asclin1_dma.h/.c`** – LVDS pixel DMA (ASCLIN1, P14.8, DMA ch1)
- **`lvds_frame_mode.h`** – LvdsFrameMode enum (8N1/8O1)
- **`can_hw.h/.c`** – diagnostic UART sniffer (ASCLIN9, P20.7, DMA ch0) with device-specific UART config, idle-gap timing, RFO recovery, and Osram/Nichia frame parsers
- **`can_diag.h/.c`** – protocol v2, ring queue, UART frame bridge
- **`frame_eth.h/.c`** – pixel + diagnostic Ethernet TX, command RX
- **`device_mode.c`** – ASCLIN1 reconfiguration, parser selection
- **`Cpu0_Main.c`** – dual DMA drain + parsers + diag bridge + recovery watchdog in main loop
- **`camera_trigger.h/.c`** – Basler camera trigger (STM0, P23.1, free-run/sync modes)
- **`adapter_ctrl.h/.c`** – SmartVisio adapter GPIO control (ECU/direct mode, CAN/UART routing)
- **`osram_crc32.h/.c`** – CRC-32 for Osram LVDS frames (MSB-first, seed 0xDEADAFFE)
- **`osram_frame.h/.c`** – Osram LVDS frame parser (header hunt + pixel assembly)
- **`rxmon.h/.c`** – Nichia LVDS line parser
- **`rx_crc.c`** – CRC-16 for Nichia/TLD816K protocol

---

## 13. Technical Debt & Improvement Opportunities

### 13.1 Identified Issues

1. **PGM Loader Hardening**
   - Current P5 parser uses "skip 4 newlines" heuristic
   - Fails on malformed PGM files with non-standard comments
   - **Recommendation:** Implement proper header parser that handles arbitrary comments

2. **No Unit Tests**
   - Zero test coverage for protocol parsing, reassembly, rendering
   - **Recommendation:** Add xUnit project with tests for:
     - AvtpRvfParser (valid/invalid packets)
     - RvfReassembler (gap detection, sequence tracking)
     - DiffRenderer (threshold behavior, edge cases)
     - DarkPixelCompensation (kernel correctness)

3. **MVVM Migration**
   - Current code-behind pattern mixes UI logic with business logic
   - **Recommendation:** Migrate to MVVM with ViewModels for testability + separation

4. **CAN/UART Monitor — M1/M2 Complete, Nichia Follow-up Pending**
   - Milestone 1 (synthetic data, GUI, protocol v2) fully implemented
   - Milestone 2 real diagnostic UART path implemented for Osram and initial Nichia/TLD816K
   - **Next:** Nichia semantic validation, missing-response analysis, and delay verification

5. **Error Handling Gaps**
   - Some paths (file I/O, network) lack try/catch
   - **Recommendation:** Add structured exception handling + user-friendly error dialogs

6. **Performance Profiling**
   - No metrics for render pipeline latency, memory usage
   - **Recommendation:** Add performance counters, profile high-frequency paths

### 13.2 Recommendations by Timeline

**Short-Term (1-2 weeks):**

- Harden PGM loader with proper comment parsing
- Add error handling for file I/O operations
- Write unit tests for AvtpRvfParser and RvfReassembler

**Medium-Term (1-2 months):**

- Validate Nichia/TLD816K CAN-UART message correctness and timing against captures
- Migrate core logic to ViewModels (MVVM)
- Add performance metrics (FPS actual vs. target, frame drop %)

**Long-Term (3+ months):**

- Full unit test suite (>80% coverage)
- Refactor to async/await where appropriate (reduce Dispatcher.Invoke overhead)
- Add codec support for AVI playback (MJPEG, H.264 via FFmpeg)

---

## 14. How to Validate Quickly

### 14.1 Basic Smoke Test

1. `dotnet build` (ensure no errors)
2. `dotnet run`
3. Load a known PCAP (`docs/inputs/AVTP_Trace_001_Osram.pcap`)
4. Verify:
   - Pane A (AVTP) updates with frames
   - Pane B (LVDS) shows fallback or loaded PGM
   - Pane D (DIFF) shows computed difference
   - Status labels update (FPS, dropped count)

### 14.2 Scene Playback Test

1. Load `docs/inputs/Black_LB_HB_LB.scene`
2. Click "Start"
3. Verify scene loops through images (500ms delay default)

### 14.3 AVI Playback Test

1. Record a short AVI (toggle "Record" → wait 5 seconds → toggle "Stop")
2. Load the generated `<timestamp>_A.avi`
3. Verify:
   - Playback starts automatically
   - Prev/Next buttons step through frames
   - "Running @" FPS displays content change rate

### 14.4 Dark Pixel Test

1. Set "Dead pixel ID" to 100 (force pixel 100 to black in B)
2. Click "Save"
3. Open generated `.xlsx` report
4. Verify:
   - Main sheet has 25,600 rows
   - `DarkPixels` sheet includes row for pixel 100
   - Row is highlighted (yellow + bold)

### 14.5 Compensation Test

1. Enable "Dark pixel compensation" checkbox
2. Force a dead pixel (e.g., pixel 100)
3. Observe DIFF pane:
   - Neighbors of pixel 100 should brighten (compensation kernel applied)

### 14.6 Zoom/Pan Test

1. Mouse wheel on pane A → verify zoom in/out
2. Left-click drag → verify pan
3. Hover over pixel → verify tooltip shows correct pixel_ID + coordinates
4. Verify overlays stay aligned during zoom/pan

---

## 15. Build & Run Commands

**Prerequisites:**

- .NET 8 SDK
- Windows OS (WPF requirement)

**Build:**

```powershell
dotnet build
```

**Run:**

```powershell
dotnet run
```

**Run with specific project file:**

```powershell
dotnet run --project VilsSharpX.csproj
```

**Clean:**

```powershell
dotnet clean
```

**Restore packages:**

```powershell
dotnet restore
```

---

## 16. Recent Session History (2026-01-16)

### 16.1 UI Layout Iteration

**Objective:** Add CAN/UART monitoring and AVTP status panels without disrupting main visualization

**Iterations:**

1. Initial 60/40 split (2 columns) → CAN/UART and Status stacked in col 1
2. Adjusted to 3 columns (3*, 3*, 4*) → separated CAN/UART (rows 0-1) and Status (row 2)
3. Optimized config group proportions from equal widths to **5* : 2* : 3***
4. Reduced control widths: NIC combo 160→80, MAC textboxes 160→110
5. Increased label widths for full text visibility (110px Hardware/App, 85px Ethernet)
6. Abbreviated long labels: "Deviation threshold" → "Dev. threshold", etc.
7. Translated CAN/UART text from Romanian to English

**Final Outcome:** Clean layout, no overlaps, all text visible, proportional scaling works correctly

### 16.2 Git Commit Baseline

**Commit:** `8481453`  
**Message:** `feat(ui): Add CAN/UART and AVTP Status panels with optimized layout`

**Purpose:** Stable checkpoint for future development; all changes validated with `dotnet build`

---

## 17. CAN Diagnostic Monitor — Milestone 1 (2026-04-03)

### 17.1 Overview

Full end-to-end CAN diagnostic transport implemented: firmware synthetic producer → Ethernet → C# parser → GUI.

Three GUI views matching classic VILS layout:

- **Monitor**: paginated decoded table (14 rows/page) with register names, filters, sorting, error highlighting
- **RawCan**: dark Consolas console with raw hex packets
- **Detail popup**: double-click opens modal with all classic VILS fields

### 17.2 Protocol v2

- **Ethertype**: 0x88B5 (shared with frame transport)
- **Magic**: 0x4344 ("CD" for CAN Diagnostic)
- **Payload**: 94 bytes (22 fixed + 72 raw UART)
- **Fixed fields**: sourceTimestamp(4) + address(2) + responseDelayUs(2) + interFrameDelayUs(2) + value(4) + checksum(4) + deviceId(1) + operation(1) + status(1) + rawLen(1)
- **Raw UART**: up to 72 bytes of actual UART frame, either Osram `[0x80][0xA5][HCTRL][HADR][Data...][CRC16]` or Nichia `[0x55][MasterRequest][DLC/FUN][Addr][Data...][CRC8/ACK]`

### 17.3 New files

| File | Type | Purpose |
| --- | --- | --- |
| `Aurix_Firmware/can_diag.h` | C header | Protocol v2 constants, `CanDiagRecord` struct |
| `Aurix_Firmware/can_diag.c` | C source | Ring queue (32), synthetic producer (32 ASIC addresses) |
| `LsmCanDiagRecord.cs` | C# | Record model with RawPayload, DecodedRegisters, RawHex |
| `LsmCanDiagParser.cs` | C# | Binary parser (v1/v2 backward compat, VLAN strip) |
| `LsmCanDiagCapture.cs` | C# | SharpPcap capture thread for magic 0x4344 |
| `LsmCanDiagStore.cs` | C# | Thread-safe ring buffer (512 entries) |
| `LsmRegisterMap.cs` | C# | TLD816K register name/type lookup (50+ entries) |
| `CanDetailWindow.xaml/.cs` | WPF | Modal detail popup (classic VILS fields) |

### 17.4 Modified files

| File | Changes |
| --- | --- |
| `Aurix_Firmware/frame_eth.h` | Added `FE_DIAG_PAYLOAD_FIXED=22`, `FE_DIAG_PAYLOAD_LEN=94` |
| `Aurix_Firmware/frame_eth.c` | Added `send_can_diag_record()`, queue drain function |
| `Aurix_Firmware/device_mode.c` | Initializes diagnostic queue/sniffer and resets state on mode changes |
| `Aurix_Firmware/Cpu0_Main.c` | Polls diagnostic UART, parses frames, bridges records, and drains diagnostic Ethernet queue |
| `MainWindow.xaml` | Tab buttons, Monitor ListView, RawCan panel, filters/paging |
| `MainWindow.xaml.cs` | Tab switching, RawCan feed, detail popup, register decode |

### 17.5 Validated on hardware

- Osram 2.05 LSM: LVDS 48.5 FPS unaffected, synthetic CAN records flowing
- Osram regression on 2026-04-30: rebuilt, flashed, run, and manually checked from the C# monitor before switching hardware; behavior stayed correct.
- Nichia smoke validation on 2026-04-30: temporary `FE_DEVICE_NICHIA`, rebuilt/flashed/run with physical Nichia LSM, monitor records visible, `badDlc=0`, PC parser errors `0`, and `framesDecoded` increasing.
- Monitor: correct register names (CR, HwSTAT, SR, OSHRS, FCR0, NVMDAT*, ELEDER*)
- Detail popup: all fields correct (timing, CRC 16-bit, nested decoded registers)
- RawCan: hex packets scrolling in dark console
- Filters: Device/R-W/Status filtering verified
- Error highlighting: timeout=red, CRC=yellow (tested via synthetic status injection)

### 17.6 Nichia follow-up scope (next)

- Validate Nichia register interpretation against captures and ECU expectations
- Check for missing request/response pairs
- Verify `ResponseDelay` and `InterFrameDelay` against Saleae timing
- UartTransaction tab content
- CAN monitor data export/recording

### 17.7 Detailed documentation

See `docs/tehnical_docs/`:

- `docs/CAN_UART_OSRAM/LSM_CAN_System_Requirements.md` — system requirements
- `docs/CAN_UART_OSRAM/LSM_CAN_Software_Requirements.md` — software requirements
- `docs/CAN_UART_OSRAM/LSM_CAN_System_Architecture.md` — block diagram, data flow, constraints
- `docs/CAN_UART_OSRAM/LSM_CAN_Software_Architecture.md` — module inventory, protocol layout, threading
- `docs/CAN_UART_OSRAM/LSM_CAN_Functionality_Description.md` — runtime behavior, UART format, GUI views
- `docs/CAN_UART_OSRAM/CAN_UART_OSRAM_Architecture.md` — OSRAM concept and Saleae timing analysis
- `docs/CAN_UART_OSRAM/CAN_UART_OSRAM_Implementation_Tracking.md` — implementation and validation tracking

### 16.3 Documentation Phase

**Generated Files:**

1. `docs/tehnical_docs/ARCHITECTURE_DIAGRAM.md` – Mermaid block diagram with 10 subgraphs
2. `docs/tehnical_docs/ARCHITECTURE_REVIEW.md` – 11-section technical review (~1200 lines, English)
3. `docs/PROJECT_STATUS.md` (this file) – comprehensive project state documentation

**Purpose:** Enable session recovery, onboard new developers, maintain institutional knowledge

---

## 17. Next Steps & Continuation Plan

### 17.1 Immediate Actions

- ✅ **Commit updated PROJECT_STATUS.md** (if not done already)
- **Test UI layout** on different screen resolutions (verify responsiveness)
- **Validate documentation accuracy** (ensure all references are correct)

### 17.2 Short-Term Priorities

1. **Implement CAN/UART functional logic**
   - Choose library (SocketCAN bridge, PCAN API, serial port reader)
   - Populate ListView with real CAN/UART frames
   - Add filtering/search capabilities

2. **Harden PGM loader**
   - Replace "skip 4 newlines" heuristic with proper parser
   - Add error handling for malformed files

3. **Add basic unit tests**
   - Test AvtpRvfParser with known-good packets
   - Test RvfReassembler gap detection

### 17.3 Medium-Term Priorities

1. **MVVM migration**
   - Extract ViewModels for PlaybackState, Settings, RecordingState
   - Reduce Dispatcher.Invoke calls
   - Improve testability

2. **Performance metrics**
   - Add FPS actual vs. target counters
   - Track frame drop percentage
   - Profile render pipeline latency

3. **Error handling improvements**
   - Add try/catch for file I/O
   - User-friendly error dialogs
   - Retry logic for network operations

### 17.4 Long-Term Vision

- Full unit test suite (>80% coverage)
- Codec support for AVI (MJPEG, H.264)
- Plugin architecture for custom processing pipelines
- Web-based remote monitoring (SignalR dashboard)

---

## 18. Key Decisions & Lessons Learned

### 18.1 Layout Design Decisions

**Decision:** Use star-sizing (*) for columns instead of Auto  
**Rationale:** Proportional scaling prevents controls from overflowing/truncating at different window sizes  
**Lesson:** Always account for GroupBox padding + margins when calculating proportions

**Decision:** Separate CAN/UART and Status panels into different row spans  
**Rationale:** Prevents visual imbalance and alignment issues  
**Lesson:** RowSpan=2 for top panel allows clean separation without footer

**Decision:** Proportions 50/20/30 for config groups instead of equal widths  
**Rationale:** Reflects actual content density (Hardware has wide combos, App Settings has small textboxes)  
**Lesson:** Group width should match control requirements, not arbitrary equality

### 18.2 Architecture Decisions

**Decision:** Manager-based separation of concerns  
**Rationale:** Avoids monolithic code-behind, easier to test and maintain  
**Lesson:** Managers should own lifecycle of their resources (e.g., LiveCaptureManager owns AvtpLiveCapture)

**Decision:** Frame cloning on publish  
**Rationale:** Prevents shared-buffer race conditions between capture/render/record threads  
**Lesson:** Safety over performance for correctness-critical code (can optimize later if needed)

**Decision:** WriteableBitmap + Gray8 for rendering  
**Rationale:** Fastest path for WPF grayscale display (no conversion overhead)  
**Lesson:** Nearest-neighbor scaling + BackBufferLock for pixel-perfect zoom

### 18.3 Documentation Decisions

**Decision:** Comprehensive PROJECT_STATUS.md instead of scattered README files  
**Rationale:** Single source of truth for session recovery and onboarding  
**Lesson:** Markdown links to architecture docs provide layered detail without overwhelming readers

**Decision:** Mermaid diagram for architecture visualization  
**Rationale:** Text-based, version-controllable, renders in GitHub/VS Code  
**Lesson:** Color-coding layers makes complex diagrams easier to parse

---

## 19. Contact & Contribution

**Project Maintainer:** (Add contact info if public)

**Contributing:**

- Follow C# coding conventions (PascalCase for public members, _camelCase for private fields)
- Run `dotnet build` before committing to ensure no errors
- Update PROJECT_STATUS.md if adding major features or architectural changes
- Write unit tests for new logic (aim for >70% coverage)

**Issue Tracking:** (Add link if using GitHub Issues, Azure DevOps, etc.)

---

## 20. Recent Features (2026-05 — Camera & Comparison)

### 20.1 Basler Camera Integration (Pane C)

**Purpose:** Capture real optical output from the LED matrix via Basler USB3 Vision camera for pixel-accurate comparison against electrical signals.

**Implementation:**

- `BaslerCameraCapture.cs` — Pylon SDK wrapper, LatestImageOnly grab strategy, FPS EMA tracking.
- `CameraConfigWindow.xaml.cs` — Live preview, zoom/pan, parameter editing, .pfs config import, auto-calibration AOI.
- Pane C in `MainWindow.xaml` displays camera frames independently of A/B.

**Auto-Calibration:**

- Finds optimal Area of Interest (AOI) that matches the LED matrix boundaries.
- Result cached in settings for consistent frame-to-frame comparison.

### 20.2 Camera Trigger Sync (Firmware)

**Purpose:** Synchronize Basler camera exposure with LVDS frame-complete signal from Aurix.

**Implementation:**

- `camera_trigger.c/h` — STM0 timer on P23.1, two modes:
  - `CAM_TRIG_FREERUN`: periodic trigger at configurable rate.
  - `CAM_TRIG_SYNC`: single-shot pulse fired on LVDS frame-complete.
- API: `camera_trigger_init()`, `camera_trigger_set_period_us()`, `camera_trigger_set_mode()`, `camera_trigger_start()`, `camera_trigger_fire_sync()`.

### 20.3 Multi-Mode Comparison (Pane D)

**Purpose:** Compare any two data sources pixel-by-pixel with color-coded visualization.

**Modes (selectable via ComboBox):**

- **LVDS-AVTP** (mode 0): Reference = AVTP (A), Measured = LVDS (B). Default mode.
- **LSM-LVDS** (mode 1): Reference = LVDS (B), Measured = Camera (C). Compares real electrical with optical.
- **LSM-AVTP** (mode 2): Reference = AVTP (A), Measured = Camera (C). Compares generator with optical.

**FrameDownscaler:**

- `FrameDownscaler.DownscaleBlockAverage()` — static utility that reduces camera resolution to LVDS resolution using block averaging.
- Pre-computed in `HandleBaslerFrameReady` and cached in `_downscaledCameraFrame` to avoid per-render overhead.
- Reuses pre-allocated buffer to minimize GC pressure.

**DiffRenderer Color Coding:**

- Green: within deadband (no significant deviation).
- Yellow → Red: B > A (measured brighter than reference, intensity scales with deviation).
- Turquoise → Blue: B < A (measured darker than reference).
- Magenta: dark pixel (reference > 0, measured == 0).
- White: optional "Flip" — reference is exactly 0 AND measured ≤ zeroThreshold.
- Black: optional "Flip" — reference is exactly 255 AND measured ≥ (255 − zeroThreshold).

**zeroThreshold Logic (camera modes):**

- Only applies when Flip checkbox is enabled.
- `zeroThreshold = 5` for camera modes (1, 2) to handle optical noise near black.
- `zeroThreshold = 0` for mode 0 (digital comparison, exact match expected).
- CRITICAL: Reference must be **exactly** 0 (or 255) for the threshold to activate. The threshold only relaxes the measured side. This prevents false whites on dim pixels (e.g., LVDS=5, camera=5 should be GREEN, not white).

**Tooltip Labels (PixelInspector):**

- Mode 0: "AVTP=X LVDS=Y diff(LVDS−AVTP)=Z"
- Mode 1: "LVDS=X LSM=Y diff(LSM−LVDS)=Z"
- Mode 2: "AVTP=X LSM=Y diff(LSM−AVTP)=Z"

### 20.4 SmartVisio Adapter Control

**Purpose:** Switch the SmartVisio adapter board between ECU passthrough mode and direct Aurix-to-LSM mode.

**Implementation:**

- `AdapterModeCommand.cs` — Sends SET_ADAPTER_MODE Ethernet command (ethertype 0x88B5, magic "CM", 3× for reliability).
- `DeviceModeCommand.cs` — Sends SET_DEVICE_MODE command (Osram/Nichia switching).
- `adapter_ctrl.c/h` (firmware) — GPIO control for relay switching, supports ECU mode, direct mode, and CAN/UART routing.

### 20.5 FPS Stability Fixes

- TFT display on Aurix shows stable FPS (Stopwatch-based measurement).
- PC Pane C FPS uses displayed-frame EMA (not raw grab EMA) for accuracy.
- Frame Statistics panel resets correctly on Start/Stop.

### 20.6 CD:0 Stall Bug Resolution

- **Root cause:** ASCLIN9 RFO (RX FIFO Overflow, FLAGS bit 8) never cleared because error mask was `0x3F`.
- **Fix:** Extended mask to `0xFFF`, on RFO → flush RX FIFO + clear. Added recovery watchdog (5s timeout, 30s cooldown).
- **Safe reinit:** Never call `IfxAsclin_resetModule()` on ASCLIN9 — causes DAE trap. Use safe sequence: stop DMA → enableModule → disable SRC → flush → clear → reconfigure.

---

## 21. Appendix: External Dependencies

**NuGet Packages:**

- **SharpPcap** – packet capture (libpcap/Npcap wrapper)
- **PacketDotNet** – protocol parsing
- **SharpAvi** – AVI file writer
- **ClosedXML** – Excel file generation (.xlsx)
- **DocumentFormat.OpenXml** – Office Open XML manipulation
- **Basler.Pylon** – Basler camera SDK (.NET wrapper)

**System Requirements:**

- Windows 10/11 (WPF)
- .NET 8 SDK
- Npcap or WinPcap (for live capture)
- Basler Pylon SDK (for camera capture, optional)

**Optional:**

- Wireshark (for PCAP analysis)
- CANoe/CANalyzer (for AVTP/CAN trace generation)

---

*For architecture details, see [ARCHITECTURE_DIAGRAM.md](tehnical_docs/ARCHITECTURE_DIAGRAM.md) and [ARCHITECTURE_REVIEW.md](tehnical_docs/ARCHITECTURE_REVIEW.md).*

*Last updated: 2026-05-28 after camera integration and multi-mode comparison implementation.*
