"""Decode CAN-UART traffic from Saleae digital CSV exports.

The SmartVisio "CAN" bus is a half-duplex UART through CAN transceivers.
This tool reconstructs the byte stream from a 4-channel digital capture,
groups the bytes into KEWGBXXD1U frames and reports the timing that the
AURIX master has to reproduce: request-to-response turnaround and the
inter-frame gap between consecutive transactions.

Usage:
    python scripts/analyze_saleae_can_uart.py <capture.csv> [--baud 2000000]
    python scripts/analyze_saleae_can_uart.py "docs/CAN_UART_Communication/<fisier>.csv" --only CAN_RX_LSM
"""

from __future__ import annotations

import argparse
import bisect
import csv
from dataclasses import dataclass, field
from pathlib import Path

# ─── Wire parameters (confirmed by WinIDEA runtime + XML config) ───
DEFAULT_BAUD = 2_000_000
DATA_BITS = 8
PARITY = "odd"
STOP_BITS = 2

# LVDS pixel transport towards the LSM: 20 Mbaud, 8O1, LSB first.
LVDS_BAUD = 20_000_000
LVDS_STOP_BITS = 1

# A new frame starts when the line has been idle for longer than this many
# byte times.  One byte is 11 bit times at 8O2.
FRAME_GAP_BYTES = 3.0

SYNC0 = 0x80
SYNC1 = 0xA5

# Osram LVDS frame: header, 320x80 pixels, CRC-32.
OSRAM_HEADER = bytes((0x80, 0xA5, 0xAA, 0x55))
OSRAM_PIXELS = 25600
OSRAM_STREAM_BYTES = len(OSRAM_HEADER) + OSRAM_PIXELS + 4


@dataclass(frozen=True)
class UartProfile:
    """Line coding for one probe."""

    baud: int
    stop_bits: int
    gap_bytes: float = FRAME_GAP_BYTES

    @property
    def bit(self) -> float:
        return 1.0 / self.baud

    @property
    def frame_bits(self) -> int:
        return 1 + DATA_BITS + 1 + self.stop_bits


@dataclass
class Byte:
    """One decoded UART character."""

    t_start: float
    t_end: float
    value: int
    parity_ok: bool
    framing_ok: bool


@dataclass
class Frame:
    """A burst of bytes separated from its neighbours by an idle gap."""

    channel: str
    t_start: float
    t_end: float
    data: bytes
    errors: int = 0
    items: list[Byte] = field(default_factory=list)

    def gap_after(self, index: int) -> float:
        """Idle time in microseconds between byte index and the next one."""
        if index + 1 >= len(self.items):
            return 0.0
        return (self.items[index + 1].t_start - self.items[index].t_end) * 1e6

    @property
    def is_protocol(self) -> bool:
        return len(self.data) >= 4 and self.data[0] == SYNC0 and self.data[1] == SYNC1

    @property
    def hctrl(self) -> int:
        return self.data[2] if len(self.data) > 2 else 0

    @property
    def is_read(self) -> bool:
        return bool(self.hctrl & 0x80)

    @property
    def n_regs(self) -> int:
        return ((self.hctrl >> 1) & 0x0F) + 1

    @property
    def address(self) -> int:
        if len(self.data) < 4:
            return 0
        return ((self.hctrl & 0x01) << 8) | self.data[3]

    def describe(self) -> str:
        if not self.is_protocol:
            return f"raw[{len(self.data)}] {self.data[:8].hex().upper()}"
        kind = "R" if self.is_read else "W"
        return f"{kind} 0x{self.address:04X} n={self.n_regs} len={len(self.data)}"


@dataclass
class ChannelTrace:
    """Transition list for one probe."""

    name: str
    times: list[float] = field(default_factory=list)
    levels: list[int] = field(default_factory=list)

    def level_at(self, t: float) -> int:
        idx = bisect.bisect_right(self.times, t) - 1
        if idx < 0:
            return 1
        return self.levels[idx]


def load_capture(path: Path) -> tuple[list[str], dict[str, ChannelTrace]]:
    """Read a Saleae digital CSV export into per-channel transition lists."""
    if not path.is_file():
        raise FileNotFoundError(f"capture not found: {path}")

    with path.open("r", newline="", encoding="utf-8-sig") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        names = [col.strip() for col in header[1:]]
        traces = {name: ChannelTrace(name) for name in names}
        previous: dict[str, int] = {}

        for row in reader:
            if len(row) < len(names) + 1:
                continue
            try:
                stamp = float(row[0])
            except ValueError:
                continue
            for offset, name in enumerate(names):
                value = int(row[offset + 1])
                if previous.get(name) != value:
                    traces[name].times.append(stamp)
                    traces[name].levels.append(value)
                    previous[name] = value

    return names, traces


def decode_uart(trace: ChannelTrace, profile: UartProfile) -> list[Byte]:
    """Sample a transition list as an asynchronous 8-bit odd-parity stream."""
    bit = profile.bit
    total_bits = profile.frame_bits
    result: list[Byte] = []

    if not trace.times:
        return result

    cursor = trace.times[0]
    end_of_capture = trace.times[-1]

    while cursor < end_of_capture:
        # Locate the next falling edge, which is a start bit.
        idx = bisect.bisect_right(trace.times, cursor)
        while idx < len(trace.times) and trace.levels[idx] != 0:
            idx += 1
        if idx >= len(trace.times):
            break

        t_start = trace.times[idx]

        value = 0
        for position in range(DATA_BITS):
            sample = t_start + (1.5 + position) * bit
            if trace.level_at(sample):
                value |= 1 << position

        ones = bin(value).count("1")
        parity_bit = trace.level_at(t_start + (1.5 + DATA_BITS) * bit)
        expected = 0 if (ones % 2) else 1  # odd parity
        parity_ok = parity_bit == expected

        framing_ok = True
        for position in range(profile.stop_bits):
            sample = t_start + (1.5 + DATA_BITS + 1 + position) * bit
            if not trace.level_at(sample):
                framing_ok = False

        t_end = t_start + total_bits * bit
        result.append(Byte(t_start, t_end, value, parity_ok, framing_ok))
        cursor = t_end - 0.25 * bit

    return result


def group_frames(name: str, stream: list[Byte], profile: UartProfile) -> list[Frame]:
    """Split a byte stream into bursts separated by an idle gap."""
    gap_limit = profile.gap_bytes * profile.frame_bits * profile.bit
    frames: list[Frame] = []
    current: list[Byte] = []

    for item in stream:
        if current and (item.t_start - current[-1].t_end) > gap_limit:
            frames.append(_build_frame(name, current))
            current = []
        current.append(item)

    if current:
        frames.append(_build_frame(name, current))

    return frames


def _build_frame(name: str, items: list[Byte]) -> Frame:
    errors = sum(0 if (b.parity_ok and b.framing_ok) else 1 for b in items)
    return Frame(
        channel=name,
        t_start=items[0].t_start,
        t_end=items[-1].t_end,
        data=bytes(b.value for b in items),
        errors=errors,
        items=list(items),
    )


def merge_bus(frames_by_channel: dict[str, list[Frame]]) -> list[Frame]:
    """Interleave the per-channel frames into one chronological bus view."""
    merged: list[Frame] = []
    for items in frames_by_channel.values():
        merged.extend(items)
    merged.sort(key=lambda item: item.t_start)
    return merged


def osram_crc32(data: bytes) -> int:
    """CRC-32 as implemented in osram_crc32.c: MSB-first, seed 0xDEADAFFE."""
    crc = 0xDEADAFFE
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) else (crc << 1) & 0xFFFFFFFF
    return int.from_bytes(crc.to_bytes(4, "big"), "little")


def report_lvds(name: str, stream: list[Byte]) -> None:
    """Locate Osram pixel frames in a 20 Mbaud stream and check them."""
    data = bytes(b.value for b in stream)
    starts: list[int] = []
    at = data.find(OSRAM_HEADER)
    while at >= 0:
        starts.append(at)
        at = data.find(OSRAM_HEADER, at + 1)

    if not starts:
        print(f"{name}: no Osram frame header found")
        return

    print(f"\n-- {name}: {len(starts)} frame headers --")
    periods: list[float] = []
    good = 0
    shown = 0

    for index, offset in enumerate(starts):
        if index:
            periods.append((stream[offset].t_start - stream[starts[index - 1]].t_start) * 1e3)
        end = offset + OSRAM_STREAM_BYTES
        if end > len(data):
            continue
        payload = data[offset + 4 : offset + 4 + OSRAM_PIXELS]
        stored = int.from_bytes(data[offset + 4 + OSRAM_PIXELS : end], "little")
        ok = osram_crc32(payload) == stored
        good += 1 if ok else 0
        if shown < 6:
            print(
                f"  t={stream[offset].t_start:10.6f} len={OSRAM_STREAM_BYTES} "
                f"crc={'OK ' if ok else 'BAD'} stored=0x{stored:08X} "
                f"first16={payload[:16].hex().upper()}"
            )
            shown += 1

    print(f"  complete frames checked, CRC ok: {good}")
    if periods:
        ordered = sorted(periods)
        print(
            f"  frame period [ms] min={ordered[0]:.3f} "
            f"median={ordered[len(ordered) // 2]:.3f} max={ordered[-1]:.3f}"
        )


def summarise(path: Path, baud: int, top: int, only: str | None, lvds: set[str]) -> None:
    names, traces = load_capture(path)
    print(f"\n=== {path.name} ===")

    can_profile = UartProfile(baud, STOP_BITS)
    lvds_profile = UartProfile(LVDS_BAUD, LVDS_STOP_BITS)

    frames_by_channel: dict[str, list[Frame]] = {}
    for name in names:
        is_lvds = name in lvds
        profile = lvds_profile if is_lvds else can_profile
        stream = decode_uart(traces[name], profile)
        if not stream:
            print(f"{name:20s} idle (no start bits)")
            continue

        bad = sum(0 if (b.parity_ok and b.framing_ok) else 1 for b in stream)
        span = stream[-1].t_end - stream[0].t_start
        kind = "LVDS" if is_lvds else "CAN "
        print(
            f"{name:20s} {kind} bytes={len(stream):8d} "
            f"errors={bad:5d} span={span:.3f}s"
        )

        if is_lvds:
            report_lvds(name, stream)
        else:
            frames_by_channel[name] = group_frames(name, stream, profile)

    if only:
        frames_by_channel = {k: v for k, v in frames_by_channel.items() if k == only}
        print(f"\n(analysing only {only})")

    bus = merge_bus(frames_by_channel)
    if not bus:
        return

    if top:
        print(f"\n-- first {top} bus frames --")
        previous_end: float | None = None
        for frame in bus[:top]:
            gap = (
                ""
                if previous_end is None
                else f"gap={1e6 * (frame.t_start - previous_end):9.1f}us"
            )
            print(
                f"{frame.t_start:12.6f}  {frame.channel:12s} {frame.describe():28s} "
                f"{gap}  {frame.data[:20].hex().upper()}"
            )
            previous_end = frame.t_end

    _report_timing(bus)


def stats(label: str, values: list[float]) -> None:
    if not values:
        print(f"{label}: none")
        return
    ordered = sorted(values)
    median = ordered[len(ordered) // 2]
    print(
        f"{label}: n={len(values)} min={ordered[0]:.1f} "
        f"median={median:.1f} max={ordered[-1]:.1f}"
    )


def _report_timing(bus: list[Frame]) -> None:
    """Measure turnaround inside a read burst and the gap between bursts."""
    turnarounds: list[float] = []
    inter_frame: list[float] = []
    write_gaps: list[float] = []
    length_tally: dict[tuple[int, int, int], int] = {}

    for index, current in enumerate(bus):
        if index + 1 < len(bus):
            gap = (bus[index + 1].t_start - current.t_end) * 1e6
            if 0.0 <= gap < 200000.0:
                inter_frame.append(gap)

        if not current.is_protocol:
            continue

        if current.is_read:
            # The answer follows the 4-byte header inside the same burst.
            turnarounds.append(current.gap_after(3))
            key = (current.hctrl, current.n_regs, len(current.data))
            length_tally[key] = length_tally.get(key, 0) + 1
        else:
            write_gaps.append(current.gap_after(3))

    print("\n-- timing (microseconds) --")
    stats("read turnaround (byte3 -> byte4)", turnarounds)
    stats("write intra-frame gap           ", write_gaps)
    stats("inter-frame gap                 ", inter_frame)

    if length_tally:
        print("\n-- read burst lengths (request header + answer) --")
        print(f"{'HCTRL':>6} {'nRegs':>6} {'burst':>6} {'expected':>9} {'count':>7}")
        for (hctrl, n_regs, burst), count in sorted(length_tally.items()):
            expected = 4 + n_regs * 2 + 2
            flag = "" if burst == expected else "  <-- mismatch"
            print(f"  0x{hctrl:02X} {n_regs:6d} {burst:6d} {expected:9d} {count:7d}{flag}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, nargs="+", help="Saleae digital CSV export")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--top", type=int, default=40, help="frames to list")
    parser.add_argument("--only", type=str, default=None, help="restrict to one channel")
    parser.add_argument(
        "--lvds",
        type=str,
        default="TTL_FROM_ECU_3V3,TTL_FROM_LOCAL",
        help="comma separated channels carrying the 20 Mbaud pixel stream",
    )
    args = parser.parse_args()

    lvds = {name.strip() for name in args.lvds.split(",") if name.strip()}

    for path in args.capture:
        summarise(path, args.baud, args.top, args.only, lvds)


if __name__ == "__main__":
    main()
