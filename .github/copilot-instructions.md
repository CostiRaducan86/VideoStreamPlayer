# Copilot instructions — SmartVisioSys / VilsSharpX

## Purpose

SmartVisioSys / VilsSharpX is an automotive lighting investigation and visualization platform.
It combines:

* C# / WPF `.NET 8` desktop visualization and tooling
* AURIX TC397 embedded C firmware
* LVDS acquisition/injection and Ethernet frame transport
* CAN-UART monitoring/injection through CAN transceiver physical layer
* Basler/pylon camera integration and Python tooling

The developer is still building software-development experience. Prefer clear, incremental, well-explained changes.

## Usage-saving behavior

* Do not scan the whole workspace unless explicitly asked.
* Start with the smallest relevant file set.
* Before editing many files, first propose a short plan and list the files to inspect.
* Prefer targeted search over broad semantic search.
* Do not launch subagents or parallel exploration unless the user explicitly asks for a deep audit.
* Do not use web/MCP/memory tools unless local code/docs are insufficient or the user asks for external/current information.
* Keep responses concise and practical.
* Prefer small, reversible patches.
* Avoid large rewrites unless explicitly requested.

## Communication

* Chat with the user in Romanian.
* Code, comments, commit messages, docstrings and technical project documentation should be in English unless the user asks otherwise.
* Explain changes clearly, especially when touching firmware, protocols, threading or SDK integration.

## Current VilsSharpX snapshot

* WPF app targeting `net8.0-windows` with `UseWPF=true`.
* Four panes: A = AVTP/Generator, B = LVDS from AURIX, C = LSM Camera/Basler, D = Comparison.
* Comparison modes: `LVDS-AVTP`, `LSM-LVDS`, `LSM-AVTP`.
* Main runtime areas:

  * AVTP/RVF Ethernet capture and reassembly
  * LVDS Ethernet capture and frame rendering
  * Basler camera acquisition
  * frame comparison and diff rendering
  * adapter/AURIX control commands

Preserve existing architecture, naming and validated constants unless explicitly asked to change them.

## C# / WPF rules

* Keep the UI thread free.
* Do not block the UI thread with socket reads, SDK calls, file I/O, image processing or camera operations.
* Prefer `async` / `await` and `CancellationToken` for long-running work.
* If callbacks come from background threads, use `Dispatcher.Invoke` or `Dispatcher.BeginInvoke` before touching WPF controls.
* Do not silently catch and ignore exceptions.
* Log errors with enough context to diagnose the device, protocol and frame number.
* Keep protocol parsing separate from rendering/UI logic.
* Keep protocol constants centralized and named.
* Prefer small, testable methods.

## C# build rules

* Run `dotnet build` only when a C# source file has been modified or updated.
* Do not build when only firmware/AURIX C files were changed.
* If the build produces errors or warnings, fix them and rebuild until clean when practical.
* Use a generous timeout, at least 120 seconds, for `dotnet build`.
* If running the build is not safe or not requested, suggest the exact command instead.

Useful commands:

* `dotnet build`
* `dotnet run`
* `dotnet run --project VilsSharpX.csproj`

No dedicated test project is currently present; changes are usually validated by running the app, observing panes and checking log files.

## Logging and diagnostics

The app is `WinExe`, so there is no console by default.

Use file-based diagnostics:

* `diagnostic.log` for AVTP/capture diagnostic traces
* `crash.log` for unhandled exceptions

Prefer existing file logging patterns over `Console.WriteLine` for important runtime information.

## AVTP / RVF rules

Ethernet AVTP frame flow:
`ethertype 0x22F0` -> `AvtpLiveCapture` -> `AvtpRvfParser.TryParseAvtpRvfEthernet` -> `RvfReassembler.Push()` -> complete frame -> `OnFrameReady(outFrame, meta)`.

Important assumptions:

* RVF width = `320`
* RVF active height = `80`
* `line` is 1-based
* chunk payload is `numLines * width` bytes
* `RvfReassembler` tracks missing sequences as gaps
* emitted frames must be safe copies, not shared mutable buffers

Preserve protocol compatibility in:

* `RvfProtocol.cs`
* `AvtpRvfParser.cs`
* `RvfReassembler.cs`
* existing Ethernet capture/reassembly modules

## LVDS Ethernet rules

Ethernet LVDS frame:
`ethertype 0x88B5`, magic `OS` or `NI` -> fragment reassembly -> complete OSRAM or NICHIA frame.

Device reminders:

* OSRAM: 320x80 active frame; UART typically 20 Mbaud, 8O1 where applicable
* NICHIA: 256x64 active frame; UART typically 12.5 Mbaud, 8N1 where applicable
* LVDS transport may include extra metadata lines
* Preserve constants such as active height `80` and LVDS height `84` unless explicitly changing protocol handling

Treat project "TTL" signals around LVDS selector/sender as 3.3 V LVCMOS unless a hardware file says otherwise.

## CAN / CAN-UART rules

CAN in this project may be used as CAN-UART physical transport, not standard CAN protocol.

Before changing CAN/CAN-UART code:

* inspect the existing source files
* inspect local protocol/reference material if available
* verify whether the change affects ECU mode, direct mode, bypass mode or injection mode
* do not assume standard CAN protocol behavior unless the code/docs prove it

Reference material may exist in:

* `/docs/LSM_CAN_Docs/`
* `UART_Protocol.csv`
* EEPROM maps
* classic VILS screenshots
* existing protocol source files

## Basler camera rules

Basler camera flow:
`BaslerCameraCapture` via pylon SDK -> `OnFrameReady(bytes, w, h)` -> `FrameDownscaler.DownscaleBlockAverage()` -> LVDS-resolution frame for comparison.

Important:

* Basler frames may arrive on a pylon grab thread.
* Always marshal UI updates through WPF Dispatcher.
* Use `LatestImageOnly` acquisition strategy where the existing code does so.
* Keep ROI, exposure, gain, pixel format and trigger settings explicit.
* Handle missing camera, busy camera and SDK errors with clear messages.
* Release camera resources safely on exit.

Camera trigger from AURIX may be handled by firmware, for example through `camera_trigger.c` driving P23.1 in sync with LVDS frame-complete.

## Comparison rules

Pane D comparison uses `DiffRenderer.ComparePixelToBgr()`.

Current meaning:

* deviation = measured - reference
* Green = within deadband
* Yellow/Red = measured > reference
* Turquoise/Blue = measured < reference
* Magenta = dark pixel case, reference > 0 and measured == 0
* White = both zero, depending on optional flip behavior

Camera modes:

* `zeroThreshold` relaxes the measured side
* reference zero handling should remain strict unless explicitly changed

Downscaling:

* `FrameDownscaler.DownscaleBlockAverage()` reduces camera resolution to LVDS resolution via block averaging before comparison.

## SmartVisio adapter control

* `AdapterModeCommand.cs` sends `SET_ADAPTER_MODE` Ethernet commands to AURIX.
* `adapter_ctrl.c/h` on firmware controls GPIO for ECU mode vs direct mode.
* Do not change adapter mode semantics without checking both PC-side and firmware-side code.
* Preserve failsafe/default hardware assumptions unless explicitly asked to redesign them.

## Firmware rules

Treat these areas as timing-sensitive:

* DMA
* ASCLIN
* LVDS parsing
* Ethernet TX
* TFT rendering
* CAN-UART handling

Rules:

* Avoid dynamic allocation in ISR, DMA, parser and Ethernet hot paths.
* Keep ISR/DMA work minimal.
* Keep `Cpu0_Main.c` clean: the main loop should call APIs/functions, not contain large inline logic.
* Implement functionality in dedicated `.c/.h` modules, not inline in `core0_main`.
* Follow TASKING C constraints and conventions; declare variables before use.
* Validate bounds before all buffer writes.
* Preserve telemetry, debug counters and diagnostic hooks unless explicitly asked to remove them.
* Do not silently change UART baud rates, parity, frame size, DMA channel use, pin mappings or Ethernet frame formats.
* For TFT UI, preserve uppercase `OSRAM` / `NICHIA`.
* Preserve `VIEW_Y = 26u` unless explicitly requested.

At the end of every response that modifies firmware files, list all modified firmware files clearly so the user can update them in ADS for compilation.

Use this format:
`Fișiere firmware modificate: Aurix_Firmware/file1.c, Aurix_Firmware/file2.h`

## Python / SDK rules

* Use `main()` and `if __name__ == "__main__":` for scripts.
* Add type hints for new public functions.
* Avoid hard-coded absolute paths; use arguments or config files.
* Handle missing camera/API/SDK resources with actionable messages.
* Always clean up hardware/SDK resources safely.
* If adding dependencies, update or document the relevant requirements/install step.

## Protocol safety

Before making protocol changes:

* inspect local code and documentation first
* identify exact device mode: OSRAM, NICHIA, AVTP/RVF, CAN-UART or camera
* confirm frame size and packet layout from constants or docs
* trace buffer ownership from input to parser to renderer/transport
* validate bounds before writes
* keep CRC/error telemetry intact
* do not silently drop frames unless existing behavior already does so and the task explicitly preserves it

If unsure about an API, SDK call, register, pin name, protocol rule or hardware behavior:

* do not invent it
* inspect local headers/examples/docs first
* state the uncertainty clearly
* propose a safe verification step

## Git rules

* Do not commit or push after every small change.
* Wait for the user to explicitly request commit/push when the changes have sufficient maturity.
* The commit messages should be in English, clear, concise, and descriptive.
* Do not introduce secrets, tokens, credentials, customer data or proprietary documentation into generated code.

## Repository hygiene

* Follow existing architecture and naming.
* Preserve existing diagnostic hooks and debug counters.
* Prefer small helper functions over duplicated logic.
* Do not remove useful comments or validated constants without a clear reason.
* When editing multiple files, summarize what changed and why.
* Mention risk areas and recommended validation steps after non-trivial changes.
