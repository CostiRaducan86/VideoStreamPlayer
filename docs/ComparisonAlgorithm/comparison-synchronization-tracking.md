# Comparison Synchronization Tracking

This document records implementation changes, investigations and validation results for frame synchronization used by the comparison pane.

## Current Status

- Status: Implemented and build-validated.
- Primary code: `MainWindow.xaml.cs`.
- Comparison color calculation: `DiffRenderer.cs`.
- Main use case: AVTP at approximately 100 fps versus LVDS at approximately 50 fps.
- Validation command: `dotnet build VilsSharpX.csproj`.
- Latest build result: 0 warnings, 0 errors.

## Baseline Behavior

The initial synchronization design used a fixed ring of recent AVTP frames and selected the candidate with the highest normalized cross-correlation (NCC) against each LVDS frame.

This design addressed the rate mismatch and ECU latency, but animated content exposed an ambiguity:

- Neighboring AVTP frames containing a moving vertical bar have very similar global structure.
- Multiple candidates can have almost identical NCC values.
- A candidate with a slightly displaced bar can therefore be selected.
- The comparison then reports a colored edge although the intended frames are equivalent.

The color mapping was inspected and intentionally left unchanged. The observed orange/red region is a consequence of comparing different animation positions, not a failure of the `0 + 0 -> white` rule.

## Change 1: Single Sync-Ring Owner

### Single-Owner Root Cause

In AVTP live mode, the same live input could reach the ring through two paths:

1. `HandleLiveFrameReady` received a new AVTP frame and pushed it.
2. `GeneratorLoopAsync` polled the current source and pushed another copy.

Because the generator and capture callbacks run at different cadences, the ring could contain duplicate or stale candidates. This is particularly harmful when LVDS arrives at half the AVTP rate.

### Single-Owner Implementation

`HandleLiveFrameReady` is now the sole sync-ring owner in `ModeOfOperation.AvtpLiveMonitor`. `GeneratorLoopAsync` continues to push frames for non-live modes, such as file or replay workflows, but does not push a second copy in live AVTP mode.

### Single-Owner Expected Effect

- Cleaner candidate history.
- Less duplicate data in the ring.
- More predictable candidate age.
- Better behavior during animation transitions.

## Change 2: Deterministic NCC Selection

### NCC Selection Root Cause

The previous scan did not explicitly prefer recent candidates when NCC values were nearly equal. Selection could depend on ring traversal order rather than temporal relevance.

### NCC Selection Rules

Candidates are now scanned from newest to oldest. A candidate replaces the current best candidate when:

- Its NCC is better by more than `NccTieEpsilon`.
- Its SAD is lower, including when the candidate is nearly uniform and NCC is low.
- Its NCC and SAD are equal and it is newer.

The current NCC diagnostic threshold is:

```text
MinimumCandidateNcc = 0.5
```

## Change 3: SAD Selection Window

### SAD Tie-Breaker Root Cause

NCC is a global structural metric. For a narrow moving bar, it can consider adjacent positions almost equivalent.

### SAD Selection Evaluation

For candidates within the NCC similarity window, the implementation calculates:

$$
SAD(A,B) = \sum_i |A_i - B_i|
$$

All candidates are now evaluated with SAD, including low-variance candidates for which NCC is not meaningful. The lowest-SAD candidate wins, followed by higher NCC and then recency for equal metrics. This prevents the matcher from discarding the correct flat predecessor at the moment a moving bar transitions into LVDS.

## Change 4: Atomic Comparison Pair

### Mixed-Pair Root Cause

The capture callback updates `_latestB` and `_matchedAForDiff` together under `_frameLock`. The render path previously read `_latestB` under the lock but read `_matchedAForDiff` after releasing it. During a moving-bar transition, a callback could update the match between those reads, causing the comparison to combine an older LVDS frame with a newer AVTP match.

### Atomic Pair Implementation

The render path now snapshots the live LVDS frame and its matched AVTP frame under the same `_frameLock` section. The comparison therefore consumes the pair selected for the same LVDS callback.

### Warm-Up Expected Effect

- Prevent transient `B(n)` versus `A(n+1)` comparisons.
- Keep pane D stable while the animation bar moves.
- Preserve the existing NCC and SAD candidate-selection rules.

## Validation History

### Screen-capture investigation

A chronological screen capture was inspected with the following observations:

- AVTP `frameId` advances at approximately twice the rate of LVDS frame numbers.
- LVDS frames remain clean, with zero CRC and parity errors in the shown samples.
- Pane A and pane B show a vertical bar moving through the image.
- Pane D is green or white when the selected frames represent the same content.
- A short orange/red region appears during a bar transition, with comparison statistics showing a large positive deviation.
- The anomaly is consistent with a neighboring animation frame being selected for one operand.

### Build validation

After the synchronization changes:

```text
dotnet build VilsSharpX.csproj
Build succeeded.
0 Warning(s)
0 Error(s)
```

The atomic comparison-pair change was also build-validated successfully with the same command.

## Change 5: LVDS Comparison Warm-Up

### Startup Transition Root Cause

At session start, AVTP can already display the moving bar while the first LVDS frame still represents the previous gray background. Comparing that first LVDS frame immediately produces the expected `17 x 64 = 1088` pixel mismatch, but it is not a valid comparison verdict because synchronization has not yet been primed.

### Warm-Up Gate Implementation

The first four valid LVDS frames now prime synchronization only. Comparison becomes ready after this four-frame window. Until then, pane D is cleared and the status displays `Comparison warming up...`. The gate resets on Start, Stop and LVDS signal loss.

### Expected Effect

- Remove the deterministic startup orange bar.
- Keep pane A and pane B visible during initialization.
- Avoid treating a known startup pipeline transition as a device defect.
- Preserve normal comparison behavior after four consecutive LVDS frames.

## Change 6: Immediate LVDS Pane Rendering

### Pane B Display Timing

Pane C is written immediately from the Basler callback, while pane B was previously written only by the periodic `RenderAll` loop. This allowed pane B to display an older LVDS image while the camera pane had already displayed the corresponding LSM image.

### Direct LVDS Bitmap Update

The LVDS callback now writes the validated frame directly to the pane B bitmap on the Dispatcher thread. `_latestB` remains the source for the comparison snapshot, so this change isolates visual pane timing from synchronization candidate selection.

### Validation Goal

Repeat the moving-bar test and compare pane B and pane C at the same instant. If the red/orange comparison region remains while B now shows the bar together with C, the remaining issue is frame pairing. If B and C become visually aligned, the previous anomaly was caused by pane B render-loop latency.

## Change 7: LVDS-Ordered Camera Display

### Callback Ordering Root Cause

The Basler trigger can produce a camera frame before the complete LVDS Ethernet frame has been reassembled and dispatched to the WPF UI. The hardware sequence can therefore be correct while pane C becomes visually newer than pane B.

### Display Credit Implementation

Camera frames are now held as the newest pending frame once the session display barrier is active. Each validated LVDS frame updates pane B first and releases one pending camera frame to pane C. Pending camera data and display credits are reset on Start, Stop and LVDS signal loss.

This changes display ordering only. It does not fabricate LVDS data, alter the comparison operands or change the Basler acquisition trigger.

### B-to-C Validation Goal

During the moving-bar test, pane C must not show the bar before pane B. If the bar is visible in C but not B after this change, the next investigation target is the LVDS Ethernet capture/reassembly path rather than WPF rendering order.

## Open Validation Work

The implementation still needs hardware or replay validation with the original animation recording. For each consecutive LVDS frame, capture:

- AVTP `frameId`.
- LVDS frame number.
- Comparison statistics.
- Selected NCC and candidate age, if diagnostic fields are enabled.

The key acceptance criterion is that a stable black-on-black region remains white with Flip enabled, while colored pixels correspond only to genuine spatial or intensity differences.

## Future Improvements

These are intentionally not part of the current patch:

1. Add a diagnostic field for selected candidate age.
2. Add a diagnostic field for SAD and best NCC.
3. Add a bounded temporal latency model once the ECU round-trip delay is measured.
4. Add automated unit tests using a synthetic moving-bar sequence.
5. Compare candidate timestamps and LVDS arrival timestamps as a secondary sanity check.

Any future change should preserve the separation between frame synchronization and pixel color classification.
