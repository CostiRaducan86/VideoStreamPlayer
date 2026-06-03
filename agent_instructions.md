# Agent Instructions — VilsSharpX

These rules apply to every session and every chat. Re-read this file at the start of each session.

## Communication

- **Chat language**: Romanian (conversație în română).
- **Code & documentation language**: English (cod, comentarii, commit messages, docstrings).

## C# GUI Build

- Run `dotnet build` **only** when a C# source file has been modified/updated.
- If the build produces errors or warnings, fix them immediately and re-build until clean (0 errors, 0 warnings).
- Do **not** build when only firmware (Aurix C) files were changed.
- **Build timeout**: use a generous timeout (≥120 s) when running `dotnet build` — the net8.0 toolchain has become slow recently. Do not assume the build has stalled if it takes 2-3 minutes.

## Firmware File Tracking

- At the end of every response that modifies firmware files, list **all modified firmware files** clearly so the user can update them in ADS for compilation.
- Format: `Fișiere firmware modificate: Aurix_Firmware/file1.c, Aurix_Firmware/file2.h`

## Git Commit & Push

- Do **not** commit + push after every small change.
- Wait for the user to explicitly request a commit/push when the changes have sufficient maturity.

## Code Quality — Firmware

- Keep `Cpu0_Main.c` clean: the `while(1)` main loop should call functions/APIs, not contain inline logic.
- Implement functionality in dedicated `.c/.h` modules, not inline in `core0_main`.
- Follow TASKING C conventions: declare all variables before use (no forward references).

## Code Quality — C# GUI

- Follow existing patterns in the codebase.
- Marshal background-thread callbacks to UI thread via `Dispatcher.Invoke/BeginInvoke`.
- Basler camera frames arrive on a Pylon grab thread — always Dispatcher-marshal.
- Comparison mode logic: `_comparisonMode` 0=LVDS-AVTP, 1=LSM-LVDS, 2=LSM-AVTP.

## General

- Read `/docs/LSM_CAN_Docs/` reference material (UART_Protocol.csv, EEPROM maps, classic VILS screenshots) before making protocol changes.
- Preserve protocol compatibility in `RvfProtocol.cs`, `AvtpRvfParser.cs`, `RvfReassembler.cs`.
- Keep frame dimension constants consistent: W=320, active height 80, LVDS height 84.
- Four display panes: A (AVTP), B (LVDS), C (LSM Camera), D (Comparison).
