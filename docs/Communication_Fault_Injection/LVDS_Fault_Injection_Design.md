# LVDS Fault Injection Design

## 1. Scope

This document defines the analysis, technical concept and implementation direction for communication fault injection on the LVDS pixel path between the ECU and the LSM.

The AURIX ASCLIN1 RX path is a monitor only. It observes the ECU stream after the adapter receiver and cannot, by itself, interrupt or alter the signal delivered to the LSM. The physical fault must therefore be injected at the adapter TTL selector, by controlling `TTL_SEL` and driving `TTL_FROM_LOCAL` to a defined local state on AURIX P02.2. Phase 1 uses P02.2 as a GPIO held HIGH for UART idle; a future replacement-stream phase may use ASCLIN1 TX.

The current receive path is:

```text
ECU differential LVDS output
    |
    | ECU_LVDS_IN_H/L
    v
U2 NBA3N012C LVDS receiver
    |
    | TTL_FROM_ECU_3V3
    +------------------------------+
                                   |
AURIX P02.2 local source          | U5 74LVC1G3157
    |                              | 2:1 TTL selector
    | TTL_FROM_LOCAL                | select = TTL_SEL
    +------------------------------+
                                   |
                                   | TTL_TO_LSM
                                   v
                             U6 NBA3N011S
                                   |
                                   | LSM_LVDS_OUT_H/L
                                   v
                                  LSM

Monitor branch:
TTL_FROM_ECU_3V3
    |
    v
ASCLIN1 RX P14.8 (X103 pin 7)
    |
    | DMA channel 1, ping-pong buffers
    v
CPU0 DMA buffer consumer
    |
    +--> OSRAM parser: osram_frame_feed()
    |       320 x 80 pixels, 25608-byte UART frame, CRC32
    |
    +--> NICHIA parser: rxmon_feed()
            64 rows, 260-byte row, CRC16 per row
    |
    v
Frame Ethernet TX, EtherType 0x88B5
    |
    +--> OS frames -> OsramEthCapture -> Pane B
    +--> NI frames -> NichiaEthCapture -> Pane B
```

The feature must preserve normal LVDS operation when disabled, must not alter baud rates or frame dimensions, and must not compromise the ASCLIN1/DMA monitoring path. The selector transition and defined P02.2 local level are the actual fault mechanism.

## 2. Confirmed Current Configuration

### 2.1 Physical and hardware path

The attached schematic shows two distinct paths:

- U2 `NBA3N012C` receives the ECU differential pair `ECU_LVDS_IN_H/L` and produces `TTL_FROM_ECU_3V3`.
- U5 `74LVC1G3157` selects either `TTL_FROM_ECU_3V3` or `TTL_FROM_LOCAL`.
- U5 output `TTL_TO_LSM` feeds U6 `NBA3N011S`, which drives the LSM differential pair `LSM_LVDS_OUT_H/L`.
- `TTL_SEL` is the selector control. The existing firmware and pinout define LOW as ECU path and HIGH as local SmartVisio path.
- `TTL_FROM_ECU_3V3` is also tapped to AURIX ASCLIN1 RX P14.8 for monitoring.
- AURIX P02.2 is connected to `TTL_FROM_LOCAL` and is the available local source for active injection. In Phase 1 it is configured as a push-pull GPIO and held HIGH, which is the UART idle state.

Therefore, ASCLIN1 RX can measure the ECU signal but cannot create the fault. A fault becomes physical only when U5 is switched away from the ECU receiver output, or when a locally generated invalid/absent stream is selected.

`adapter_ctrl.c` must own the selector GPIO. The fault policy must call a dedicated adapter-control API and must not access selector registers directly.

### 2.2 AURIX monitoring pipeline

`asclin1_dma.c/h` owns ASCLIN1 configuration, DMA ping-pong buffers and DMA health telemetry. The DMA buffer size is 8192 bytes and the DMA completion ISR only swaps buffers and publishes the completed buffer.

`Cpu0_Main.c` consumes completed buffers and dispatches them according to `device_mode_get()`:

- OSRAM -> `osram_frame_feed(data, len)`
- Nichia -> `rxmon_feed(data, len)`

This pipeline is measurement only. It is useful for confirming that the ECU stream is present and for recording the effect of the physical fault, but transforming a completed DMA buffer would only create a monitor-side artifact and would not affect the LSM or the ECU.

### 2.3 OSRAM protocol

Confirmed from the local parser:

```text
[0..3]       80 A5 AA 55 header
[4..25603]   25600 pixel bytes, 320 x 80
[25604..]    4 CRC32 bytes
```

`osram_frame.c` hunts the header, collects the pixel payload, verifies the CRC32 and emits the frame to `frame_eth` even when the CRC is bad. This existing behavior is important: an LVDS fault may produce a CRC-bad frame, a lost frame, parser resynchronization, or a frame with altered pixels depending on the selected fault.

### 2.4 NICHIA protocol

Confirmed from `rxmon.c/h`:

```text
[0]          5D sync
[1]          row address plus parity
[2..257]     256 pixel bytes
[258..259]   CRC16, MSB then LSB
```

Each valid row is pushed to the Nichia Ethernet frame assembler. A byte fault can therefore cause row CRC failure, row parity failure, loss of parser lock, row continuity errors or an incomplete Ethernet frame.

### 2.5 Ethernet observability path

`frame_eth.c/h` sends completed LVDS frames using EtherType `0x88B5` and magic `OS` or `NI`. The WPF application reassembles these fragments in `OsramEthCapture` and `NichiaEthCapture`.

This Ethernet path is the required observability path and must remain active and unmodified during LVDS fault injection. The C# application must continue to receive the AURIX LVDS monitoring data so that the physical ECU-to-LSM fault can be correlated with the observed frames. No fault is injected between AURIX and C# on this path.

## 3. Fault Types

### 3.1 SELECT_LOCAL_IDLE

Set `TTL_SEL = HIGH` and keep the local P02.2 source HIGH in UART idle state. The ECU stream is physically disconnected from the U5 output and the LSM no longer receives the ECU frame stream.

This is the recommended first fault because it is deterministic, electrically simple and does not require generating a replacement frame. It tests the ECU reaction to a real missing LVDS communication path.

Recommended initial variants:

- activate while the bus is idle;
- activate during an ECU frame;
- hold for a configured duration;
- clear by returning `TTL_SEL = LOW`.

The first phase should implement a bounded selector fault with a duration and explicit clear action. A permanent fault is allowed only through an explicit command and must have a clear path.

Expected effects:

- LSM: stops receiving the ECU LVDS stream while the fault is active.
- ECU: must react according to its own LVDS timeout, retry, diagnostic or failsafe logic.
- AURIX monitor: may continue to show the ECU stream on ASCLIN1 RX because the monitor tap is before U5; this is expected and must not be interpreted as proof that the LSM receives it.

### 3.2 SELECT_LOCAL_INVALID_STREAM

Generate a controlled local waveform on ASCLIN1 TX/P02.2, select it with `TTL_SEL = HIGH`, and send either UART idle, a malformed header/row sequence, or another explicitly defined invalid stream. This mode is deferred; Phase 1 does not enable ASCLIN1 TX and only holds P02.2 HIGH as a GPIO idle level.

An arbitrary byte mutation in the AURIX monitor is not equivalent to this fault because it never reaches U5, U6 or the LSM.

### 3.3 SELECT_LOCAL_CORRUPTED_FRAME

Transmit a locally generated frame or row sequence with deliberately altered payload/header/CRC, select it through U5, and observe the ECU/LSM reaction. This is a true physical replacement of the ECU source at the adapter output, but it is more complex and should follow the idle/disconnect fault.

## 4. Recommended Architecture

Add a dedicated policy module:

```text
Aurix_Firmware/lvds_fault_inject.h
Aurix_Firmware/lvds_fault_inject.c
```

The module owns fault state and counters only. It must not access ASCLIN or DMA registers, and it must not own parser state.

Suggested API:

```c
typedef enum
{
    LVDS_FAULT_OFF = 0u,
    LVDS_FAULT_SELECT_LOCAL_IDLE = 1u,
    LVDS_FAULT_SELECT_LOCAL_INVALID = 2u,
    LVDS_FAULT_SELECT_LOCAL_FRAME = 3u
} LvdsFaultMode;

boolean lvds_fault_set(LvdsFaultMode mode, uint16 durationUnits100Ms,
                       uint8 deviceProfile);
void lvds_fault_clear(void);
boolean lvds_fault_is_active(void);
void lvds_fault_tick(void);
```

The exact API may change after the first probe. The ownership boundary is the important part: the module requests a selector transition through `adapter_ctrl`, and a separate local TX generator owns any replacement waveform.

### 4.1 Physical integration point

The preferred first integration is in the adapter-control path, coordinated by `Cpu0_Main.c` and the Ethernet command owner:

```text
fault command
    -> stop/prepare local LVDS TX source
    -> adapter_ctrl_set_ttl_source(LOCAL)
    -> U5 selects TTL_FROM_LOCAL
    -> U6 drives the selected local signal to LSM
```

For `SELECT_LOCAL_IDLE`, no local frame generator is needed: configure P02.2 as a push-pull GPIO HIGH and switch U5 to local. For replacement-stream faults, ASCLIN1 TX must be configured and prepared before switching the selector. The first implementation should avoid modifying the DMA ISR and avoid dynamic allocation.

### 4.2 State and telemetry

The state should be a small volatile structure with a single command writer:

- enabled and mode;
- active device/profile;
- duration or expiration timestamp;
- selector state requested and applied;
- local source state;
- active device/profile;
- duration and expiration;
- selector transitions and rejected transitions;
- local bytes/frames transmitted, if a replacement source is active;
- monitor frames/rows observed while the physical fault is active;
- rejected and applied commands;
- last fault reason.

Intentional fault counters must remain separate from ASCLIN hardware errors, DMA missed buffers and parser errors.

### 4.3 Command ownership

The existing `frame_eth` command RX path is the natural transport because it already receives PC commands with magic `CM` and EtherType `0x88B5`. Add a new command ID after the CAN-UART fault command, keeping the command parser responsible for validation and the fault module responsible for policy.

The initial command must support:

- START and CLEAR actions;
- `SELECT_LOCAL_IDLE`;
- duration;
- current OSRAM/NICHIA profile, if required by the surrounding control flow;

No command should directly change ASCLIN baud, parity, DMA channel, pin mapping or frame geometry.

Command `FE_CMD_LVDS_FAULT = 0x07` payload:

```text
[16] command ID = 0x07
[17] mode       = 0x01 SELECT_LOCAL_IDLE
[18] profile    = 0 current, 1 NICHIA, 2 OSRAM
[19..20] duration in 100 ms units, big-endian; 0 = permanent until CLEAR
[21] action    = 0 CLEAR, non-zero START
```

`SELECT_LOCAL_INVALID_STREAM` and `SELECT_LOCAL_CORRUPTED_FRAME` remain future extensions and are intentionally excluded from the first command contract.

### 4.4 WPF integration

The WPF UI should follow the CAN-UART pattern only after the firmware command contract is stable:

- a dedicated `LvdsFaultCommand.cs` sender;
- `SELECT_LOCAL_IDLE` and duration controls;
- Inject and Stop/Clear actions;
- local remaining-time display only as a convenience, not as firmware confirmation;
- status based on received/observed LVDS frames and parser telemetry where available;
- continuous C# reception of the AURIX LVDS monitoring Ethernet stream.

The existing `CommunicationFaultState.LvdsFaultEnabled` is reserved for this connection. It must not be treated as proof that the selector changed or that the firmware accepted the command until an acknowledgment or telemetry channel exists. Pane B and ASCLIN1 RX telemetry are monitoring signals only; the ECU reaction is the primary test result. The C# Ethernet monitoring path must not be stopped, filtered or faulted by this feature.

## 5. Safety and Constraints

1. Do not access `PIN_TTL_SEL` directly outside `adapter_ctrl.c`.
2. Do not switch `TTL_SEL` while P02.2/local source is uncontrolled.
3. Do not reset ASCLIN1 or DMA when starting or clearing a fault.
4. Do not modify the established RX baud rates, parity, stop bits, DMA channel, pin mappings, Ethernet format or frame dimensions.
5. Do not add dynamic allocation to DMA, parser or Ethernet hot paths.
6. Keep the DMA completion ISR unchanged unless a measured requirement proves otherwise.
7. Preserve monitor parser, CRC and diagnostic telemetry; they are evidence, not the fault mechanism.
8. Serialize fault commands with device-mode changes, adapter mode changes and LVDS recovery.
9. CLEAR must be idempotent and must restore ECU source selection (`TTL_SEL = LOW`) only after the local source is safe.
10. Never make a fault permanent without a documented explicit clear path.

### 5.1 Command and timeout robustness

The PC sends three identical START Ethernet frames for reliability. Firmware
treats subsequent START copies received while `SELECT_LOCAL_IDLE` is already
active as idempotent: they do not toggle `TTL_SEL` and do not rearm the expiry.

The application also performs periodic device/adapter synchronization. A
repeated `SET_DEVICE_MODE` for the current profile and a repeated
`SET_ADAPTER_MODE` for the current routing must not clear an active LVDS fault.
A real profile or control-mode change still clears the fault before applying
the new hardware state.

Fault expiry uses an extended STM timebase instead of a 32-bit absolute STM
timestamp. This keeps the complete UI-supported duration range valid even when
the lower STM counter wraps.

## 6. Validation Matrix

### Normal operation

- OSRAM valid frames continue with unchanged FPS and CRC counters.
- NICHIA valid rows continue with unchanged row continuity and CRC counters.
- No fault counters increase while injection is OFF.
- Camera trigger and `OS`/`NI` Ethernet frame output remain unchanged.

### Physical selector fault

- Confirm `TTL_SEL = LOW` routes ECU data to LSM.
- Confirm `TTL_SEL = HIGH` routes `TTL_FROM_LOCAL` to LSM.
- Activate `SELECT_LOCAL_IDLE` while idle and during an ECU frame.
- Observe ECU timeout/retry/failsafe behavior.
- Confirm ASCLIN1 RX may still show ECU traffic while LSM communication is interrupted.
- Verify automatic expiration and explicit CLEAR return the selector to ECU source.
- Verify no ASCLIN/DMA reinitialization is performed.

### Future local replacement modes (deferred)

- `SELECT_LOCAL_INVALID_STREAM` and `SELECT_LOCAL_CORRUPTED_FRAME` are deferred.
- Their implementation must continue to preserve the unmodified C# Ethernet monitoring path.
- No validation work for these modes is part of the initial `SELECT_LOCAL_IDLE` implementation.

### Robustness

- Repeated START/CLEAR sequences while idle and during traffic.
- START after device-mode switch.
- Fault expiration near parser frame completion.
- Command flood while LVDS is running.
- Confirm CPU0 loop keeps draining DMA buffers and `missedBuffers` does not rise due to the fault control path.
- Verify duplicate START packets do not rearm the timeout or toggle `TTL_SEL`.
- Verify periodic same-profile/device-mode synchronization does not clear the fault.
- Verify a real adapter/control-mode change clears the fault intentionally.

## 7. Open Technical Questions

- What P02.2 idle level and selector transition timing are required on the target hardware?
- Is the ECU reaction to `SELECT_LOCAL_IDLE` a timeout, retry, diagnostic or failsafe transition?
- Should a future ASCLIN1 TX peripheral be continuously held idle or explicitly armed before each selector transition?
- Is a firmware ACK required before enabling the WPF control as an operational feature?
- What exact fault duration is acceptable for the first hardware test?

These questions should be resolved with the first hardware test plan before adding a broad UI surface.
