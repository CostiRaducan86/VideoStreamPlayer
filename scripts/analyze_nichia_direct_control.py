"""Analyze Nichia/TLD816K Direct Control traces and Saleae captures.

The Nichia diagnostic bus is UART 8N1 at 2 Mbit/s with CRC8 on ASIC
register transactions. The LVDS stream is UART 8N1 at 12.5 Mbit/s and
contains one 256-pixel row per packet: 0x5D, row/parity, 256 pixels, CRC16.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import datetime
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

NICHIA_CAN_BAUD = 2_000_000
NICHIA_LVDS_BAUD = 12_500_000
DATA_BITS = 8
NICHIA_SYNC = 0x55
LVDS_SYNC = 0x5D
NICHIA_WIDTH = 256
NICHIA_HEIGHT = 64
NICHIA_ROW_BYTES = 260
MASTER_MAX_REQUEST_LENGTH = 72
DLC_LENGTHS = (1, 2, 4, 8, 16, 24, 32, 64)


def crc8(data: bytes) -> int:
    crc = 0xFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def parity_bits(row: int) -> int:
    return 0x40 if ((row & 0x3F).bit_count() & 1) else 0x80


def crc16_nichia(data: bytes) -> int:
    """CRC16 used by rx_crc.c: poly 0x1021, seed 0x0001, MSB first."""
    crc = 0x0001
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Transaction:
    seq: int
    timestamp: str
    op: str
    address: int
    value: int
    gap_us: int
    raw: bytes

    @property
    def request(self) -> bytes:
        if self.op != "R" or len(self.raw) < 3:
            return self.raw
        address_length = 2 if (self.raw[2] & 0x07) in (6, 7) else 1
        return self.raw[:3 + address_length]

    @property
    def fun(self) -> int:
        return self.request[2] & 0x07 if len(self.request) >= 3 else -1

    @property
    def dlc_length(self) -> int:
        return DLC_LENGTHS[(self.request[2] >> 3) & 0x07] if len(self.request) >= 3 else 0

    def signature(self) -> tuple[object, ...]:
        return (self.op, self.address, self.fun, self.dlc_length, len(self.request))


def load_trace(path: Path) -> list[Transaction]:
    records: list[Transaction] = []
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("//"):
                continue
            fields = line.rstrip("\n").split(";")
            if len(fields) < 10:
                continue
            try:
                records.append(Transaction(
                    seq=int(fields[0]), timestamp=fields[1], op=fields[3],
                    address=int(fields[4], 16), value=int(fields[5], 16),
                    gap_us=int(fields[8]), raw=bytes.fromhex(fields[9]),
                ))
            except ValueError:
                continue
    return records


def trace_crc_status(record: Transaction) -> str:
    if record.op == "R" or record.fun == 7:
        return "n/a"
    address_length = 2 if record.fun in (6, 7) else 1
    data_length = record.dlc_length
    crc_index = 3 + address_length + data_length
    if len(record.raw) <= crc_index:
        return "short"
    return "ok" if crc8(record.raw[3:crc_index]) == record.raw[crc_index] else "BAD"


def find_tail(records: list[Transaction], min_period: int = 4, max_period: int = 64) -> tuple[int, int]:
    """Find the earliest stable repeated transaction signature in the tail."""
    signatures = [record.signature() for record in records]
    for period in range(min_period, max_period + 1):
        start = max(0, len(signatures) - period * 8)
        if len(signatures) - start < period * 4:
            continue
        if all(signatures[index] == signatures[index - period]
               for index in range(start + period, len(signatures))):
            first = start
            while first >= period and signatures[first - period:first] == signatures[first:first + period]:
                first -= period
            return first, period
    return len(records), 0


def find_run_candidate(records: list[Transaction], window: int = 120) -> int:
    """Find the first long window containing the observed Normal-Run shape."""
    run_addresses = {0x0000, 0x0020, 0x0030, 0x0040, 0x0062,
                     0x0080, 0x0090, 0x00A0, 0x00B0, 0x00BC,
                     0x00CC, 0x00DC, 0x00EC, 0x0023}
    for index in range(len(records) - window):
        block = records[index:index + window]
        addresses = {record.address for record in block}
        if not addresses.issubset(run_addresses):
            continue
        if sum(record.address == 0x0062 for record in block) < 2:
            continue
        if sum(record.address == 0x0023 for record in block) < 1:
            continue
        return index
    return len(records)


def print_trace(path: Path, show: int) -> None:
    records = load_trace(path)
    print(f"trace records: {len(records)}")
    if not records:
        return
    bad_crc = sum(trace_crc_status(record) == "BAD" for record in records)
    print(f"CRC8 bad: {bad_crc}")
    start, period = find_tail(records)
    run_candidate = find_run_candidate(records)
    print(f"cyclic tail: start={start} period={period} transactions")
    print(f"Normal-Run shape candidate: record {run_candidate}")
    startup = records[:start]
    cycle = records[start:start + period]
    print(f"startup: {len(startup)} transactions, writes={sum(r.op == 'W' for r in startup)}")
    print(f"cycle timing: {sum(r.gap_us for r in cycle)} us from recorded inter-frame delays")
    print("cycle transactions:")
    for record in cycle:
        print(f"  {record.seq:4d} {record.op} 0x{record.address:04X} "
              f"value=0x{record.value:08X} gap={record.gap_us:5d} us "
              f"fun={record.fun} crc={trace_crc_status(record)}")
    if show:
        print(f"first {show} startup writes:")
        for record in [item for item in startup if item.op == "W"][:show]:
            print(f"  {record.seq:4d} 0x{record.address:04X} 0x{record.value:08X} "
                  f"gap={record.gap_us} us crc={trace_crc_status(record)}")
    print("startup write registers:")
    for (address, value), count in Counter((r.address, r.value) for r in startup if r.op == "W").most_common():
        print(f"  0x{address:04X} = 0x{value:08X}: {count}x")


def emit_header(path: Path, records: list[Transaction]) -> None:
    """Emit the first Normal-Run transition as the Nichia test cycle."""
    boundary = find_run_candidate(records)
    if boundary >= len(records):
        raise ValueError("Normal-Run boundary was not found")

    startup = records[:boundary]
    cycle = records[boundary:boundary + 34]
    if not cycle:
        raise ValueError("Nichia cycle is empty")

    def render(name: str, steps: list[Transaction]) -> list[str]:
        lines = [f"static const CanUartNichiaStep {name}[] =", "{"]
        for record in steps:
            request = record.request
            if len(request) > MASTER_MAX_REQUEST_LENGTH:
                raise ValueError(f"request at record {record.seq} exceeds master buffer")
            payload = ", ".join(f"0x{value:02X}u" for value in request)
            gap_us = record.gap_us
            if gap_us == 0xFFFF and record.seq > 0:
                previous = records[record.seq - 1]
                current_time = datetime.datetime.fromisoformat(record.timestamp)
                previous_time = datetime.datetime.fromisoformat(previous.timestamp)
                timestamp_gap_us = int((current_time - previous_time).total_seconds() * 1_000_000)
                if timestamp_gap_us > 0:
                    gap_us = timestamp_gap_us
            lines.append(
                f"    {{ {gap_us:5}u, {len(request):2}u, "
                f"{1 if record.op == 'R' else 0}u, {{ {payload} }} }},"
                f"   /* {record.op} 0x{record.address:04X} */"
            )
        lines.append("};")
        return lines

    lines = [
        "#ifndef CAN_UART_NICHIA_SEQUENCE_H",
        "#define CAN_UART_NICHIA_SEQUENCE_H",
        "",
        "/******************************************************************************",
        " * can_uart_nichia_sequence.h - Nichia CAN-UART Direct Mode test sequence",
        " *",
        " * GENERATED FILE - do not edit by hand.",
        " * Source: trace_Nichia_StartUp_Run_20260904_143935.txt",
        " ******************************************************************************/",
        "",
        '#include "Ifx_Types.h"',
        "",
        "#define CAN_UART_NICHIA_MAX_REQUEST_LEN  72u",
        "",
        "typedef struct",
        "{",
        "    uint32 gapUs;",
        "    uint8  len;",
        "    uint8  expectResponse;",
        "    uint8  data[CAN_UART_NICHIA_MAX_REQUEST_LEN];",
        "} CanUartNichiaStep;",
        "",
    ]
    lines += render("s_nichiaStartup", startup)
    lines.append("")
    lines += render("s_nichiaCycle", cycle)
    lines += [
        "",
        f"#define CAN_UART_NICHIA_STARTUP_STEPS {len(startup)}u",
        f"#define CAN_UART_NICHIA_CYCLE_STEPS   {len(cycle)}u",
        "",
        "#endif /* CAN_UART_NICHIA_SEQUENCE_H */",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {path}: {len(startup)} startup steps, {len(cycle)} cycle steps")


def saleae_bursts(stream: list[tuple[float, int, bool]],
                  gap_us: float = 30.0) -> list[list[tuple[float, int, bool]]]:
    """Group decoded UART bytes into wire bursts separated by idle time."""
    bursts: list[list[tuple[float, int, bool]]] = []
    current: list[tuple[float, int, bool]] = []
    for item in stream:
        if current and (item[0] - current[-1][0]) * 1_000_000.0 > gap_us:
            bursts.append(current)
            current = []
        current.append(item)
    if current:
        bursts.append(current)
    return bursts


def emit_saleae_header(path: Path, saleae_path: Path) -> None:
    """Generate the replay table from the ECU Saleae transmit stream."""
    traces = load_csv(saleae_path)
    tx_stream = decode_uart(traces["CAN_TX_LSM"], NICHIA_CAN_BAUD)
    rx_stream = decode_uart(traces["CAN_RX_LSM"], NICHIA_CAN_BAUD)
    tx_bursts = saleae_bursts(tx_stream)
    rx_bursts = saleae_bursts(rx_stream)

    def request_length(data: bytes) -> int:
        if len(data) < 3 or data[0] != NICHIA_SYNC:
            return 0
        fun = data[2] & 0x07
        address_length = 2 if fun in (6, 7) else 1
        data_length = DLC_LENGTHS[(data[2] >> 3) & 0x07]
        return (3 + address_length + data_length + 1) if fun == 4 else (3 + address_length)

    # Derive requests from CAN_TX_LSM. CAN_RX_LSM combines the request echo
    # with the LSM response and can therefore collapse repeated requests when
    # a response is appended to only some of the identical bursts.
    transactions: list[tuple[float, bytes, float, bool]] = []
    rx_index = 0
    for burst in tx_bursts:
        raw = bytes(item[1] for item in burst)
        length = request_length(raw)
        if length == 0 or len(raw) < length:
            continue
        request = raw[:length]
        start = burst[0][0]
        while rx_index < len(rx_bursts):
            rx_raw = bytes(item[1] for item in rx_bursts[rx_index])
            rx_index += 1
            if rx_raw[:length] == request:
                transactions.append((start, request, burst[-1][0], len(rx_raw) > length))
                break

    requests = [item[1] for item in transactions]
    if not requests:
        raise ValueError("Saleae LSM RX channel contains no Nichia transactions")

    cycle_start = -1
    cycle_length = 0
    best_repetitions = 0
    for period in range(20, 151):
        for index in range(100, len(requests) - period * 2):
            normal_run_marker = (
                requests[index:index + 3] == [
                    bytes.fromhex("55 11 35 20"),
                    bytes.fromhex("55 11 35 00"),
                    bytes.fromhex("55 11 35 20"),
                ]
            )
            if not normal_run_marker:
                continue
            repetitions = 1
            while (index + (repetitions + 1) * period <= len(requests) and
                   requests[index:index + period] ==
                   requests[index + repetitions * period:index + (repetitions + 1) * period]):
                repetitions += 1
            if repetitions > best_repetitions:
                best_repetitions = repetitions
                cycle_start = index
                cycle_length = period
    if cycle_start < 0:
        raise ValueError("Repeated Nichia cycle was not found")

    steps: list[tuple[int, bytes, int, bool]] = []
    for index, (start, request, _, has_response) in enumerate(
            transactions[:cycle_start + cycle_length]):
        previous_end = transactions[index - 1][2] if index else start
        gap = 65535 if index == 0 else int((start - previous_end) * 1_000_000.0)
        steps.append((gap, request, index, has_response))

    def render(name: str, selected: list[tuple[int, bytes, int, bool]]) -> list[str]:
        lines = [f"static const CanUartNichiaStep {name}[] =", "{"]
        for gap, request, index, has_response in selected:
            if len(request) > MASTER_MAX_REQUEST_LENGTH:
                raise ValueError(f"request at Saleae burst {index} exceeds master buffer")
            fun = request[2] & 0x07 if len(request) >= 3 else 0
            payload = ", ".join(f"0x{value:02X}u" for value in request)
            lines.append(
                f"    {{ {gap:5}u, {len(request):2}u, {1 if has_response else 0}u, "
                f"{{ {payload} }} }},   /* Saleae burst {index} */"
            )
        lines.append("};")
        return lines

    lines = [
        "#ifndef CAN_UART_NICHIA_SEQUENCE_H",
        "#define CAN_UART_NICHIA_SEQUENCE_H",
        "",
        "/******************************************************************************",
        " * can_uart_nichia_sequence.h - Nichia CAN-UART Direct Mode test sequence",
        " *",
        " * GENERATED FILE - do not edit by hand.",
        f" * Source: {saleae_path.name}",
        " ******************************************************************************/",
        "",
        '#include "Ifx_Types.h"',
        "",
        "#define CAN_UART_NICHIA_MAX_REQUEST_LEN  72u",
        "",
        "typedef struct",
        "{",
        "    uint32 gapUs;",
        "    uint8  len;",
        "    uint8  expectResponse;",
        "    uint8  data[CAN_UART_NICHIA_MAX_REQUEST_LEN];",
        "} CanUartNichiaStep;",
        "",
    ]
    lines += render("s_nichiaStartup", steps[:cycle_start])
    lines.append("")
    lines += render("s_nichiaCycle", steps[cycle_start:cycle_start + cycle_length])
    lines += [
        "",
        f"#define CAN_UART_NICHIA_STARTUP_STEPS {cycle_start}u",
        f"#define CAN_UART_NICHIA_CYCLE_STEPS   {cycle_length}u",
        "",
        "#endif /* CAN_UART_NICHIA_SEQUENCE_H */",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {path}: {cycle_start} startup steps, "
          f"{cycle_length} cycle steps from Saleae")


@dataclass
class TransitionTrace:
    times: list[float] = field(default_factory=list)
    levels: list[int] = field(default_factory=list)

    def level_at(self, timestamp: float) -> int:
        index = bisect.bisect_right(self.times, timestamp) - 1
        return self.levels[index] if index >= 0 else 1


def load_csv(path: Path) -> dict[str, TransitionTrace]:
    traces: dict[str, TransitionTrace] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames or "Time [s]" not in reader.fieldnames:
            raise ValueError("CSV must contain a 'Time [s]' column")
        channels = [name for name in reader.fieldnames if name != "Time [s]"]
        traces = {name: TransitionTrace() for name in channels}
        previous: dict[str, int] = {}
        for row in reader:
            timestamp = float(row["Time [s]"])
            for name in channels:
                level = int(row[name])
                if previous.get(name) != level:
                    traces[name].times.append(timestamp)
                    traces[name].levels.append(level)
                    previous[name] = level
    return traces


def decode_uart(trace: TransitionTrace, baud: int, parity: str = "none") -> list[tuple[float, int, bool]]:
    bit = 1.0 / baud
    frame_bits = 10 if parity == "none" else 11
    decoded: list[tuple[float, int, bool]] = []
    index = 0
    next_start = -1.0
    while index < len(trace.times):
        if trace.levels[index] != 0:
            index += 1
            continue
        start = trace.times[index]
        if start < next_start:
            index += 1
            continue
        value = sum((trace.level_at(start + (position + 1.5) * bit) << position)
                    for position in range(DATA_BITS))
        stop_ok = trace.level_at(start + (9.5 if parity == "none" else 10.5) * bit) == 1
        decoded.append((start, value, stop_ok))
        next_start = start + (frame_bits - 0.5) * bit
        index += 1
    return decoded


def print_csv(path: Path, can_channels: list[str], lvds_channel: str) -> None:
    traces = load_csv(path)
    for name in can_channels + [lvds_channel]:
        if name not in traces:
            print(f"{name}: missing")
            continue
        baud = NICHIA_LVDS_BAUD if name == lvds_channel else NICHIA_CAN_BAUD
        stream = decode_uart(traces[name], baud)
        errors = sum(not item[2] for item in stream)
        print(f"{name}: bytes={len(stream)} stop_errors={errors}")
        if name == lvds_channel:
            rows = 0
            crc_bad = 0
            data = bytes(item[1] for item in stream)
            for offset in range(0, len(data) - NICHIA_ROW_BYTES + 1):
                if data[offset] != LVDS_SYNC:
                    continue
                row_header = data[offset + 1]
                row = row_header & 0x3F
                if (row_header & 0xC0) != parity_bits(row):
                    continue
                crc_wire = (data[offset + 258] << 8) | data[offset + 259]
                rows += 1
                crc_bad += crc16_nichia(data[offset + 2:offset + 258]) != crc_wire
            print(f"  Nichia row candidates={rows} crc_bad={crc_bad} bytes_per_row={NICHIA_ROW_BYTES}")
    
def saleae_can_stream(path: Path, channel: str) -> list[tuple[float, int, bool]]:
    traces = load_csv(path)
    if channel not in traces:
        raise ValueError(f"Saleae channel not found: {channel}")
    return decode_uart(traces[channel], NICHIA_CAN_BAUD)

def print_can_comparison(reference: Path, actual: Path, channel: str) -> None:
    reference_stream = saleae_can_stream(reference, channel)
    actual_stream = saleae_can_stream(actual, channel)
    reference_data = bytes(item[1] for item in reference_stream)
    actual_data = bytes(item[1] for item in actual_stream)
    common = min(len(reference_data), len(actual_data))
    first_difference = next(
        (index for index in range(common) if reference_data[index] != actual_data[index]),
        common,
    )
    print(f"comparison channel={channel}")
    print(f"  reference bytes={len(reference_data)} actual bytes={len(actual_data)}")
    if first_difference < common:
        start = max(0, first_difference - 16)
        end = min(common, first_difference + 32)
        print(f"  first byte difference={first_difference}")
        print(f"  reference[{start}:{end}]={reference_data[start:end].hex().upper()}")
        print(f"  actual   [{start}:{end}]={actual_data[start:end].hex().upper()}")
    else:
        print(f"  common prefix={common} bytes")
    for name, stream in (("reference", reference_stream), ("actual", actual_stream)):
        bad = sum(not item[2] for item in stream)
        print(f"  {name}: stop_errors={bad}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--saleae", type=Path)
    parser.add_argument("--show-startup", type=int, default=8)
    parser.add_argument("--emit-header", type=Path)
    parser.add_argument("--emit-saleae-header", type=Path)
    parser.add_argument("--lvds-channel", default="TTL_FROM_ECU_3V3")
    parser.add_argument("--can-channels", default="CAN_RX_LSM,CAN_TX_LSM")
    parser.add_argument("--compare-saleae", nargs=2, type=Path,
                        metavar=("REFERENCE", "ACTUAL"))
    parser.add_argument("--compare-channel", default="CAN_TX_LSM")
    args = parser.parse_args()
    if args.trace:
        print_trace(args.trace, args.show_startup)
        if args.emit_header:
            emit_header(args.emit_header, load_trace(args.trace))
    if args.saleae:
        print_csv(args.saleae, [name for name in args.can_channels.split(",") if name], args.lvds_channel)
        if args.emit_saleae_header:
            emit_saleae_header(args.emit_saleae_header, args.saleae)
    if args.compare_saleae:
        print_can_comparison(args.compare_saleae[0], args.compare_saleae[1], args.compare_channel)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
