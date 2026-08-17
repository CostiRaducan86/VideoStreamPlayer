# Comparison Synchronization Concept

## Purpose

The comparison pane must compare corresponding image content from the AVTP input and the LVDS output. The two streams do not run at the same rate:

- AVTP input on pane A: approximately 100 frames per second.
- LVDS output on pane B: approximately 50 frames per second.
- The ECU introduces a variable round-trip latency between the AVTP input and the LVDS output.

A simple latest-frame comparison would therefore compare different animation positions. For a moving vertical bar, this produces false colored regions even when the ECU output is correct.

The synchronization layer selects the AVTP frame that best corresponds to each received LVDS frame before the comparison colors are calculated.

The selected LVDS frame and its matched AVTP frame must also be consumed as one atomic pair. Rendering one value before a capture callback updates the pair and the other value afterward can mix `B(n)` with `A(n+1)`, creating a false transient mismatch during animation.

At the beginning of a capture session, the first four valid LVDS frames are used to prime the synchronization state. Comparison output becomes valid after this short startup window, preventing the AVTP-to-LVDS pipeline transition from being reported as a device mismatch.

## Data Flow

```text
AVTP/RVF frame
    -> Frame A
    -> synchronization ring
                         LVDS frame B
                              -> candidate matching
                              -> matched Frame A
                              -> comparison(A, B)
                              -> pane D
```

The color algorithm is deliberately separate from synchronization. Synchronization chooses the operands; `DiffRenderer` classifies their pixel differences.

At render time, the live LVDS frame and `_matchedAForDiff` are snapshotted under the same frame lock. This preserves the pair selected by `HandleLvdsFrameReady`.

During the warm-up interval, pane D is cleared and the comparison status shows `Comparison warming up...`. The gate is reset after Start, Stop or LVDS signal loss. At 50 LVDS fps, four frames represent approximately 80 ms.

## Sync Ring

Recent AVTP frames are stored in a fixed-size ring:

- Constant: `SyncRingSize = 128`.
- Each entry contains a safe-copy `Frame`.
- The ring provides enough history for the expected ECU latency and short timing variations.
- Frames are not dynamically allocated by the matching calculation itself, apart from the normal `Frame` ownership model.

In AVTP live mode, `HandleLiveFrameReady` is the sole owner that pushes frames into the ring. `GeneratorLoopAsync` must not push a second polling copy of the same live stream, because duplicate entries reduce the quality of the candidate history.

## Candidate Matching

For every received LVDS frame, the matcher evaluates the AVTP candidates in the ring.

### 1. Flat-frame handling

NCC becomes unstable when the LVDS frame has very little spatial variation. The matcher calculates the per-pixel variance of B. If the standard deviation is below the configured flat-frame threshold, it selects the most recent AVTP frame that is also flat.

This avoids selecting an older animated frame containing a bar when the current LVDS frame is already uniform.

### 2. Normalized Cross-Correlation

For structured frames, the primary metric is normalized cross-correlation:

$$
NCC(A,B) =
\frac{N\sum AB - \sum A\sum B}
{\sqrt{(N\sum A^2-(\sum A)^2)(N\sum B^2-(\sum B)^2)}}
$$

NCC is useful because it remains effective when the ECU changes brightness through an approximately linear transformation:

$$
B \approx kA + c
$$

SAD is calculated for every candidate, including nearly uniform AVTP frames whose NCC is undefined or low. The lowest-SAD candidate is selected because it represents the closest pixel content. NCC remains a structural diagnostic and tie-breaker; it must not exclude a flat temporal predecessor during a moving-bar transition.

### 3. SAD selection window

Animation frames can have almost identical NCC values, especially when only a narrow vertical bar moves. For every candidate that passes the NCC validity gate, the matcher calculates the sum of absolute differences:

$$
SAD(A,B) = \sum_i |A_i - B_i|
$$

The candidate with the smallest SAD is preferred among all ring candidates. SAD is more sensitive to the exact spatial position of the bar than NCC. If SAD values are equal, the candidate with higher NCC wins, followed by the newer candidate. A minimum NCC quality gate remains available for diagnostics and fallback, but a low-NCC candidate with the best SAD is retained when it is the best temporal/content match.

### 4. Recency tie-breaker

Candidates are scanned from newest to oldest. If NCC and SAD are equal, the newer candidate wins. This makes selection deterministic and avoids depending on the physical array index of the ring.

## Quality Gate

If the best NCC is below the existing quality threshold, the match is considered unreliable and the most recent valid AVTP frame is used as a fallback. The actual NCC value remains available for diagnostics.

## Comparison Contract

After synchronization:

- `A` is the selected AVTP reference frame.
- `B` is the corresponding LVDS output frame.
- The comparison calculates `B - A` per pixel.
- The color rules in `DiffRenderer` are unchanged by this synchronization logic.
- With `Flip (0=0=white)` enabled, a pixel is white only when both operands are zero, subject to the configured zero threshold.

Therefore, an orange or red bar in pane D indicates a real pixel mismatch between the selected operands. It should not be corrected by changing the color mapping.

## Timing Interpretation

The AVTP frame identifier can advance by approximately two frames between two consecutive LVDS frames because AVTP is approximately twice as fast. This is expected. The relevant requirement is not equal frame counters, but selecting the AVTP content that produced the observed LVDS frame after ECU latency.

For a moving bar:

- Correctly synchronized black-on-black regions become white when Flip is enabled.
- A bar shifted between A and B appears as a colored mismatch region.
- A persistent colored edge during motion is a synchronization diagnostic signal.

## Diagnostics and Validation

Use a recorded animation with a single vertical bar and inspect frames in chronological order. Record for each LVDS frame:

- AVTP `frameId`.
- LVDS frame number.
- Selected candidate NCC.
- Candidate age in the ring, when exposed by diagnostics.
- Comparison statistics: maximum positive deviation, maximum negative deviation, mean deviation and dark-pixel count.

Expected behavior after synchronization improvements:

1. Stable black regions remain white with Flip enabled.
2. The comparison bar follows only the genuine spatial or intensity difference.
3. Consecutive LVDS frames do not alternate unpredictably between an old and a new AVTP candidate.
4. CRC, parity and synchronization errors remain zero during a clean capture.
