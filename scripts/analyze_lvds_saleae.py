"""Decode and validate an OSRAM LVDS stream captured from TTL_FROM_LOCAL.

The Saleae export is a digital transition list. This tool reconstructs the
asynchronous serial bytes (20 Mbaud, 8O1, LSB first, idle high), locates the
OSRAM frames (header 80 A5 AA 55, 25600 pixels, 4 CRC bytes) and verifies the
frame CRC-32 with the ECU-compatible algorithm implemented in osram_crc32.c.

It also reports frame periods and inter-byte gaps, which is how a DMA underrun
in the transmit path would show up.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import sys
from dataclasses import dataclass, field

OSRAM_HEADER = (0x80, 0xA5, 0xAA, 0x55)
OSRAM_PIXELS = 25600
OSRAM_CRC_LEN = 4
OSRAM_CRC32_RAW_SEED = 0xDEADAFFE
OSRAM_CRC32_POLY = 0x04C11DB7

_CRC32_TABLE: list[int] = []


def _build_crc32_table() -> None:
    if _CRC32_TABLE:
        return
    for i in range(256):
        crc = i << 24
        for _ in range(8):
            crc = ((crc << 1) ^ OSRAM_CRC32_POLY) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
        _CRC32_TABLE.append(crc)


def osram_crc32(data: bytes) -> int:
    """CRC-32 matching osram_crc32_compute(): MSB-first, raw seed, bswap32."""
    _build_crc32_table()
    crc = OSRAM_CRC32_RAW_SEED
    for byte in data:
        idx = ((crc >> 24) ^ byte) & 0xFF
        crc = ((crc << 8) ^ _CRC32_TABLE[idx]) & 0xFFFFFFFF
    return ((crc >> 24) & 0xFF) | ((crc >> 8) & 0xFF00) | ((crc << 8) & 0xFF0000) | ((crc << 24) & 0xFF000000)


@dataclass
class DecodedByte:
    time: float
    value: int
    parity_ok: bool
    stop_ok: bool


@dataclass
class Frame:
    start_time: float
    end_time: float
    pixels: bytes
    crc_wire: int
    crc_calc: int
    parity_errors: int
    stop_errors: int
    max_gap_us: float = 0.0
    gap_count: int = 0

    @property
    def crc_ok(self) -> bool:
        return self.crc_wire == self.crc_calc


@dataclass
class Transitions:
    times: list[float] = field(default_factory=list)
    levels: list[int] = field(default_factory=list)

    def level_at(self, t: float) -> int:
        idx = bisect.bisect_right(self.times, t) - 1
        if idx < 0:
            return 1
        return self.levels[idx]


def load_transitions(path: str, channel: str) -> Transitions:
    result = Transitions()
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None or channel not in reader.fieldnames:
            raise SystemExit(f"channel '{channel}' not found; available: {reader.fieldnames}")
        for row in reader:
            result.times.append(float(row["Time [s]"]))
            result.levels.append(int(row[channel]))
    return result


def decode_uart(tr: Transitions, baud: float, parity: str) -> list[DecodedByte]:
    """Sample-based 8-bit UART decode, LSB first, idle high, one stop bit."""
    bit = 1.0 / baud
    frame_bits = 10 if parity == "none" else 11
    out: list[DecodedByte] = []

    # Candidate start bits are falling edges; the transition list already holds them.
    edge_idx = 0
    n = len(tr.times)
    next_allowed = -1.0

    while edge_idx < n:
        if tr.levels[edge_idx] != 0:
            edge_idx += 1
            continue
        t0 = tr.times[edge_idx]
        if t0 < next_allowed:
            edge_idx += 1
            continue

        value = 0
        for k in range(8):
            if tr.level_at(t0 + (k + 1.5) * bit):
                value |= 1 << k

        pos = 9
        parity_ok = True
        if parity != "none":
            parity_bit = tr.level_at(t0 + (pos + 0.5) * bit)
            ones = bin(value).count("1") + parity_bit
            parity_ok = (ones % 2 == 1) if parity == "odd" else (ones % 2 == 0)
            pos += 1

        stop_ok = tr.level_at(t0 + (pos + 0.5) * bit) == 1
        out.append(DecodedByte(t0, value, parity_ok, stop_ok))

        next_allowed = t0 + (frame_bits - 0.5) * bit
        edge_idx += 1

    return out


def extract_frames(stream: list[DecodedByte], bit_time: float) -> list[Frame]:
    frames: list[Frame] = []
    total = OSRAM_PIXELS + OSRAM_CRC_LEN
    i = 0
    limit = len(stream) - (len(OSRAM_HEADER) + total)

    while i <= limit:
        if tuple(b.value for b in stream[i:i + 4]) != OSRAM_HEADER:
            i += 1
            continue

        body = stream[i + 4:i + 4 + total]
        pixels = bytes(b.value for b in body[:OSRAM_PIXELS])
        crc_bytes = [b.value for b in body[OSRAM_PIXELS:]]
        crc_wire = crc_bytes[0] | (crc_bytes[1] << 8) | (crc_bytes[2] << 16) | (crc_bytes[3] << 24)

        frame = Frame(
            start_time=stream[i].time,
            end_time=body[-1].time,
            pixels=pixels,
            crc_wire=crc_wire,
            crc_calc=osram_crc32(pixels),
            parity_errors=sum(0 if b.parity_ok else 1 for b in stream[i:i + 4 + total]),
            stop_errors=sum(0 if b.stop_ok else 1 for b in stream[i:i + 4 + total]),
        )

        # A healthy transmit path emits back-to-back characters; anything longer
        # than a character time between two bytes is a FIFO underrun.
        nominal = 11 * bit_time
        window = stream[i:i + 4 + total]
        for prev, cur in zip(window, window[1:]):
            gap = cur.time - prev.time - nominal
            if gap > bit_time:
                frame.gap_count += 1
                frame.max_gap_us = max(frame.max_gap_us, gap * 1e6)

        frames.append(frame)
        i += 4 + total

    return frames


def describe_pattern(pixels: bytes, width: int) -> str:
    first_row = pixels[:width]
    ramp_phase = first_row[0]
    is_ramp = all(first_row[c] == (ramp_phase + c) & 0xFF for c in range(width))
    if is_ramp:
        return f"diagonal ramp, phase={ramp_phase}"
    lit = [v for v in pixels if v != 0]
    if not lit:
        return "all black"
    distinct = sorted(set(lit))
    return f"non-ramp, {len(lit)} non-zero pixels, values={distinct[:8]}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", help="Saleae digital export (transition list)")
    parser.add_argument("--channel", default="TTL_FROM_LOCAL")
    parser.add_argument("--baud", type=float, default=20_000_000.0)
    parser.add_argument("--parity", choices=["odd", "even", "none"], default="odd")
    parser.add_argument("--width", type=int, default=320)
    args = parser.parse_args()

    transitions = load_transitions(args.csv_path, args.channel)
    print(f"transitions: {len(transitions.times)}  span: {transitions.times[-1] - transitions.times[0]:.6f} s")

    stream = decode_uart(transitions, args.baud, args.parity)
    bad_parity = sum(0 if b.parity_ok else 1 for b in stream)
    bad_stop = sum(0 if b.stop_ok else 1 for b in stream)
    print(f"decoded bytes: {len(stream)}  parity errors: {bad_parity}  stop errors: {bad_stop}")

    frames = extract_frames(stream, 1.0 / args.baud)
    if not frames:
        print("no complete OSRAM frame found")
        return 1

    print(f"complete frames: {len(frames)}")
    for n, frame in enumerate(frames):
        duration_us = (frame.end_time - frame.start_time) * 1e6
        print(
            f"  frame {n}: start={frame.start_time * 1e3:.3f} ms  "
            f"serialisation={duration_us:.1f} us  "
            f"crc_wire=0x{frame.crc_wire:08X} crc_calc=0x{frame.crc_calc:08X} "
            f"{'OK' if frame.crc_ok else 'MISMATCH'}  "
            f"parity_err={frame.parity_errors} stop_err={frame.stop_errors}  "
            f"gaps={frame.gap_count} max_gap={frame.max_gap_us:.2f} us"
        )
        print(f"           pattern: {describe_pattern(frame.pixels, args.width)}")

    for prev, cur in zip(frames, frames[1:]):
        print(f"  frame period: {(cur.start_time - prev.start_time) * 1e3:.3f} ms")

    return 0 if all(f.crc_ok for f in frames) else 1


if __name__ == "__main__":
    sys.exit(main())
