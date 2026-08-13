# AVTP Communication Fault Injection

## 1. Purpose

The AVTP communication fault injection feature simulates loss of the AVTP input received by the ECU while the VilsSharpX application remains operational.

The implementation is intentionally local to the WPF application:

- it blocks AVTP frame transmission from the PC to the ECU;
- it does not close the selected network adapter;
- it does not stop the LVDS Ethernet receiver;
- it does not stop the Basler camera capture;
- it does not send a firmware command to the AURIX controller;
- it allows the ECU response, including delayed LVDS shutdown or failsafe behavior, to be observed by the application.

The AVTP fault does not use the AURIX STM timer or a firmware fault command. Its
duration is measured locally by the WPF `DispatcherTimer`/`Stopwatch`, while the
actual communication fault is the `AvtpTransmitManager` TX-blocking flag. The
STM timeout review therefore does not apply to AVTP.

The feature is currently implemented for **AVTP Generator / PlayerFromFiles mode**. LVDS and CAN-UART fault controls are displayed as reserved firmware features and remain disabled.

## 2. User Interface

The control window is opened from the Communication Fault control menu command in `MainWindow.xaml.cs`.

The window contains three fault paths:

| Fault path | Current state | Effect |
| --- | --- | --- |
| AVTP Communication Fault | Implemented | Blocks PC-side AVTP TX in PlayerFromFiles mode |
| LVDS Communication Fault | Firmware pending | Disabled in the UI |
| CAN-UART Communication Fault | Firmware pending | Disabled in the UI |

The AVTP checkbox is enabled only when the current mode is `PlayerFromFiles`. When another mode is selected, `UpdateCommunicationFaultAvailability()` disables the checkbox and clears an active AVTP fault state if necessary.

The status text has three relevant states:

- `[Disabled]`: fault injection is available and currently off;
- `[Enabled]`: fault injection is active;
- `[Generator mode only]`: the current mode does not support this injection path.

The checkbox state is stored in the shared `CommunicationFaultState` instance. The window does not perform transmission itself; it raises `FaultStateChanged`, and `MainWindow` applies the state to the AVTP transmitter.

## 3. Software Components

### 3.1 `CommunicationFaultState`

`CommunicationFaultState` is the application-level state holder:

```csharp
public sealed class CommunicationFaultState
{
    public bool AvtpFaultEnabled { get; set; }
    public bool LvdsFaultEnabled { get; set; }
    public bool CanUartFaultEnabled { get; set; }
}
```

Only `AvtpFaultEnabled` is currently connected to a working implementation. The LVDS and CAN-UART properties are reserved for future firmware-backed fault injection.

### 3.2 `CommunicationFaultControlWindow`

The control window:

1. binds the checkbox to `CommunicationFaultState.AvtpFaultEnabled`;
2. updates the visual status text;
3. emits `FaultStateChanged` after a checkbox change;
4. automatically clears the AVTP state when AVTP fault injection becomes unavailable.

The control window is modeless and owned by `MainWindow`. Opening the menu item again activates the existing window instead of creating a second control window.

### 3.3 `MainWindow.ApplyCommunicationFaultState()`

`MainWindow` forwards the state to the active `AvtpTransmitManager`:

```csharp
txManager.AvtpFaultEnabled = _communicationFaultState.AvtpFaultEnabled;
```

The method has two separate paths:

- fault enabled: update the UI to show AVTP signal loss;
- fault disabled: restore TX readiness, clear signal-loss latches, preserve active LVDS capture where possible, and restore the normal status text.

## 4. Fault-Enabled Transmission Behavior

The controlling property is `AvtpTransmitManager.AvtpFaultEnabled`.

When set to `true`, the manager:

1. stores the enabled state in a `volatile` field;
1. stops the black-frame loop, if one is running;
1. logs:

```text
[avtp-tx] Communication fault enabled: TX sending blocked
```

1. causes subsequent `SendFrameAsync()` calls to return `false` immediately;
1. leaves the underlying `LibPcapLiveDevice` open.

The important distinction is that this is **TX blocking**, not network-device shutdown. The shared capture infrastructure remains available so LVDS and camera behavior can continue to be measured.

### 4.1 Generator mode

`GeneratorLoopAsync()` continues to execute while the fault is active. It still creates the source frame and maintains the generator loop timing, but every AVTP send is rejected by `AvtpTransmitManager`.

For normal image, AVI, scene, or sequence sources:

```text
source frame -> generator loop -> SendFrameAsync() -> fault check -> no Ethernet TX
```

For PCAP sources, the generator also continues to use the current AVTP frame source, but the manager blocks its transmission. The PCAP replay path is not replaced by a black frame when the fault is enabled.

The transmitter-level 10 ms limiter in `AvtpRvfTransmitter` is therefore not the mechanism that creates the fault. The fault check in `AvtpTransmitManager` occurs before the transmitter is called.

### 4.2 No black-frame replacement

Enabling the AVTP fault does not intentionally send black AVTP frames. The black-frame loop is stopped when the fault is enabled, and normal frame sends are blocked.

This creates an actual absence of AVTP packets from the VilsSharpX TX path. The ECU is therefore able to detect the missing AVTP stream according to its own watchdog, timeout, or failsafe logic.

## 5. Interaction with LVDS and Basler Capture

The AVTP fault is deliberately independent from the other input paths.

### 5.1 LVDS Ethernet receiver

The LVDS Ethernet receiver remains active during an AVTP fault. `MainWindow` does not stop or recreate it merely because the AVTP checkbox is enabled.

This allows the UI to observe the ECU's real behavior after AVTP loss:

- the ECU may continue sending LVDS frames for a period of time;
- the LVDS stream may later stop after an ECU timeout or failsafe transition;
- valid black LVDS frames still count as real signal and must not be classified as signal loss;
- Pane B may continue displaying real LVDS data while Pane A is unavailable.

When the fault is enabled, Pane B is marked unavailable only if the LVDS signal is also unavailable. A valid real LVDS stream, including a valid all-black frame, remains visible and its FPS can continue to be shown.

### 5.2 Basler camera

Basler capture is not stopped by AVTP fault injection. Pane C has an independent signal state and remains live if the camera continues acquiring frames.

The AVTP fault therefore tests the camera and LVDS fallback behavior in a realistic way: AVTP can be absent while the camera and LVDS paths are still active.

## 6. Pane and Status Behavior

The rendering logic treats AVTP and LVDS as independent signals.

### Pane A: AVTP / Generator

When `AvtpFaultEnabled` is true:

- the AVTP image is not blitted as a valid A signal;
- `NoSignalA` is visible;
- the A running/FPS label is cleared;
- the main status text becomes:

```text
AVTP Fault Injection: Signal not available.
```

### Pane B: LVDS

Pane B follows the real LVDS state, not the AVTP fault alone:

- real LVDS received: show the real LVDS frame and keep B available;
- no real LVDS received: show `Signal not available`;
- AVTP fault plus valid black LVDS: keep B valid and retain its FPS information;
- AVTP fault plus lost LVDS: show B unavailable and clear the B running/FPS label.

### Pane C: Basler camera

Pane C is independent from AVTP:

- camera frames continue to render when available;
- the camera `Signal not available` overlay is controlled by the Basler signal state;
- AVTP fault injection alone does not hide a valid camera signal.

### Pane D: comparison

A comparison requires valid operands. When AVTP is unavailable, the default LVDS-AVTP comparison is no longer meaningful, so Pane D shows `Signal not available`.

The exception is a camera/LVDS comparison mode where the available operands remain valid. The exact visibility is controlled by `_comparisonMode`, `_lvdsSignalLost`, and the real LVDS availability state in `RenderAll()`.

## 7. Fault Recovery

When the checkbox is cleared while playback is still running, `MainWindow.ApplyCommunicationFaultState()` performs recovery without requiring a full Stop/Start cycle.

The recovery sequence is:

1. set `AvtpTransmitManager.AvtpFaultEnabled` to `false`;
2. initialize the transmitter if PlayerFromFiles is active and the transmitter is not ready;
3. clear `_latestB`, `_matchedAForDiff`, and `_latestC` to avoid using stale data;
4. latch LVDS as unavailable until a fresh LVDS frame arrives;
5. reset the LVDS and Basler timing state used for signal detection and FPS display;
6. preserve an already active LVDS receiver instead of recreating it;
7. start the LVDS receiver only if it is not active;
8. ensure Basler capture is active;
9. render the current state;
10. restore the normal AVTP Generator status text.

The LVDS signal is intentionally not declared valid immediately after recovery. It becomes valid only after the next real LVDS callback. This prevents stale pre-fault frames from being presented as post-recovery traffic.

If the transmitter cannot be initialized during recovery, the generator remains running but AVTP packets cannot be sent. The diagnostic log reports the initialization result.

## 8. Interaction with Pause, Resume, and Stop

### Pause

Pause is a UI freeze operation for the AVTP Generator workflow, not an AVTP communication fault.

During Pause:

- the displayed panes remain frozen;
- the generator continues to execute;
- AVTP transmission continues at its regular cadence;
- the AVTP fault state is unchanged;
- the communication fault checkbox is still the explicit control for blocking AVTP TX.

This distinction is important because pausing the generator's AVTP transmission would itself look like a communication fault to the ECU and could trigger failsafe behavior.

### Resume

Resume releases the normal playback state and allows new UI frames to be published. If the AVTP fault checkbox remains cleared, AVTP transmission continues. If the fault remains enabled, Resume does not re-enable TX because `AvtpTransmitManager.SendFrameAsync()` continues to reject sends.

### Stop

Stop cancels playback and stops the normal generator loop. The application may use the configured black-frame behavior during stop/end-of-file handling, but enabling AVTP fault explicitly stops the black-frame loop and blocks all subsequent sends.

The AVTP fault checkbox should therefore be understood as a separate signal-loss experiment, not as another form of Pause or Stop.

## 9. AVTP Packet-Level Context

When fault injection is disabled and TX is available, `AvtpRvfTransmitter` sends AVTP/RVF frames with the following implementation characteristics:

- frame geometry: `320 x 80` Gray8 pixels;
- frame payload: `25,600` bytes;
- four image lines per Ethernet packet;
- 20 Ethernet packets per complete frame;
- VLAN ID and priority taken from the AVTP configuration;
- EtherType normally `0x22F0`;
- a 10 ms minimum interval between complete transmitted frames;
- all 20 packets of one frame sent as a burst;
- sequence bytes generated by the transmitter.

The fault injection feature does not modify this packet format. It prevents the send operation from reaching packet construction and `LibPcapLiveDevice.SendPacket()`.

## 10. Diagnostic Evidence

Relevant log messages include:

```text
[avtp-tx] Communication fault enabled: TX sending blocked
[avtp-tx] TX ready on <device> (...)
[avtp-tx] TX is NULL -> nothing will be sent (select NIC and press Start).
[avtp-tx] rate-limiter: sent=<n> dropped=<n> (max 100fps enforced)
```

Interpretation:

- `Communication fault enabled` confirms that the software fault state was applied;
- absence of new `rate-limiter: sent=...` growth during the fault is expected because sends return before the transmitter is called;
- LVDS frame counters and FPS can continue to increase while AVTP is faulted;
- a later LVDS timeout indicates the ECU stopped producing LVDS or the LVDS path became unavailable, not that the AVTP checkbox directly stopped the LVDS receiver.

For hardware correlation, observe both the VilsSharpX log and the AURIX/ECU status. The application only removes AVTP TX; the ECU determines when the missing stream causes a timeout or failsafe transition.

## 11. Recommended Test Procedure

### Test A: Basic AVTP fault injection

1. Select `AVTP Generator` / `PlayerFromFiles` mode.
2. Load a valid image, AVI, scene, sequence, or PCAP source.
3. Select the AVTP TX network adapter and start playback.
4. Confirm that AVTP TX is active and the ECU is operating normally.
5. Open Communication Fault Injection Control.
6. Enable `AVTP Communication Fault`.
7. Confirm:
   - Pane A shows `Signal not available`;
   - the A running/FPS label disappears;
   - the main status reports AVTP fault injection;
   - no new AVTP frames are transmitted;
   - LVDS and Basler capture remain active initially.

### Test B: ECU delayed response

1. Keep the AVTP fault enabled.
2. Observe the ECU status and the LVDS stream over time.
3. Confirm whether LVDS continues temporarily, becomes black, or stops after the ECU timeout.
4. Confirm that a valid real black LVDS frame still counts as LVDS signal.
5. Confirm that Pane B and its FPS label disappear only after LVDS is actually considered unavailable.

### Test C: Recovery without Stop/Start

1. With the AVTP fault active and playback still running, clear the checkbox.
2. Confirm that the transmitter is initialized if necessary.
3. Confirm that the application does not immediately reuse stale LVDS data.
4. Confirm that Pane A becomes valid after AVTP frames resume.
5. Confirm that Pane B becomes valid only after a fresh real LVDS frame arrives.
6. Confirm that the normal `Running @...` status returns.

### Test D: Pause separation

1. Start the AVTP Generator with the fault disabled.
2. Press Pause.
3. Confirm that all panes freeze visually.
4. Confirm that AVTP transmission continues and the ECU does not enter failsafe.
5. Press Resume and confirm normal operation.
6. Repeat the test with AVTP Communication Fault enabled. Confirm that Pause/Resume does not override the explicit fault state.

## 12. Known Scope and Limitations

- Only AVTP fault injection is implemented end to end.
- The feature is available only in `PlayerFromFiles` mode.
- LVDS and CAN-UART controls are placeholders for future firmware-backed implementations.
- The fault is software TX blocking, not physical cable removal, NIC disablement, packet corruption, or packet loss simulation.
- The application does not control the ECU timeout duration or failsafe policy.
- Clearing the fault requires fresh AVTP transmission and fresh LVDS traffic before all panes can return to their normal valid states.
- The transmitter uses the selected capture device for packet injection; a missing or invalid adapter must be diagnosed separately from fault injection.
- Build artifacts and runtime logs are not part of the feature contract; packet counters, application logs, ECU state, and pane behavior should be correlated during validation.

## 13. Source Files

- `CommunicationFaultState.cs` - shared fault state.
- `CommunicationFaultControlWindow.xaml` - fault control UI.
- `CommunicationFaultControlWindow.xaml.cs` - checkbox state and availability logic.
- `AvtpTransmitManager.cs` - AVTP fault gate, TX initialization, frame sending, and black-frame loop.
- `AvtpRvfTransmitter.cs` - AVTP/RVF packet construction and 100 fps limiter.
- `MainWindow.xaml.cs` - mode availability, fault application, generator integration, recovery, rendering, and signal labels.
- `PlaybackStateManager.cs` - running/paused/stopped state and pause gate.

## 14. Expected Behavioral Contract

The implementation should preserve these rules:

1. **AVTP Communication Fault is the only checkbox that blocks AVTP TX.**
2. **Pause freezes the UI but does not stop AVTP TX.**
3. **Stop and end-of-file handling are separate from communication fault injection.**
4. **AVTP signal state and LVDS signal state are independent.**
5. **Real black LVDS frames are valid frames, not automatically signal loss.**
6. **Recovery must wait for fresh traffic instead of presenting stale frames as current data.**
7. **The application must keep diagnostic logs and counters available for hardware correlation.**
