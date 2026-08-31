"""Analyse an OSRAM CAN-UART trace and split it into start-up and cyclic phases.

The trace is the ECU<->LSM diagnostic conversation captured by the AURIX bridge.
For Direct Control Mode the AURIX has to replay the ECU side of it, so this tool
extracts the request sequence, detects where the cyclic run phase begins, and
reports the register writes that configure the LSM.

Read requests are 4 bytes on the wire (header only); writes carry data plus
CRC-16. The RawHex column holds the full frame seen on the bus, which for a read
is the LSM response, so only its first four bytes belong to the request.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from dataclasses import dataclass


@dataclass
class Record:
    seq: int
    timestamp: str
    op: str
    address: int
    value: int
    if_delay_us: int
    raw: str

    @property
    def request_hex(self) -> str:
        """Bytes the ECU puts on the bus for this transaction."""
        return self.raw[:8] if self.op == "R" else self.raw


def load(path: str) -> list[Record]:
    records: list[Record] = []
    with open(path, encoding="utf-8") as handle:
        rows = [line for line in handle if not line.startswith("//")]
    for row in csv.reader(rows, delimiter=";"):
        if len(row) < 10:
            continue
        try:
            records.append(
                Record(
                    seq=int(row[0]),
                    timestamp=row[1],
                    op=row[3],
                    address=int(row[4], 16),
                    value=int(row[5], 16),
                    if_delay_us=int(row[8]),
                    raw=row[9].strip(),
                )
            )
        except ValueError:
            continue
    return records


def find_cycle(records: list[Record], min_period: int = 4, max_period: int = 64) -> tuple[int, int]:
    """Return (start_index, period) of the repeating tail pattern."""
    keys = [(r.op, r.address) for r in records]
    n = len(keys)

    for period in range(min_period, max_period + 1):
        # Verify the pattern holds over the last stretch of the trace.
        tail = min(period * 20, n - period)
        if tail < period * 4:
            continue
        if all(keys[n - 1 - i] == keys[n - 1 - i - period] for i in range(tail)):
            # Walk backwards to the first index where the pattern still holds.
            start = n - tail - period
            while start > 0 and keys[start - 1] == keys[start - 1 + period]:
                start -= 1
            return start, period

    return n, 0


def emit_header(path: str, startup: list[Record], cycle: list[Record], source: str) -> None:
    """Write the C table the AURIX CAN-UART master replays."""
    max_len = max(len(r.request_hex) // 2 for r in startup + cycle)

    def steps(records: list[Record], name: str) -> list[str]:
        out = [f"static const CanUartMasterStep {name}[] =", "{"]
        for r in records:
            data = r.request_hex
            payload = ", ".join(f"0x{data[i:i + 2]}u" for i in range(0, len(data), 2))
            gap = min(r.if_delay_us, 0xFFFF)
            expect = 1 if r.op == "R" else 0
            out.append(
                f"    {{ {gap:5}u, {len(data) // 2}u, {expect}u, {{ {payload} }} }},"
                f"   /* {r.op} 0x{r.address:04X} */"
            )
        out.append("};")
        return out

    lines = [
        "#ifndef CAN_UART_OSRAM_SEQUENCE_H",
        "#define CAN_UART_OSRAM_SEQUENCE_H",
        "",
        "/******************************************************************************",
        " * can_uart_osram_sequence.h - OSRAM CAN-UART request sequence for Direct Mode",
        " *",
        " * GENERATED FILE - do not edit by hand.",
        " * Produced by scripts/analyze_can_uart_trace.py from a captured ECU trace:",
        f" *   {source}",
        " *",
        " * The AURIX replays the ECU side of the diagnostic conversation so the LSM",
        " * leaves failsafe and accepts pixel data.  Read requests are four bytes on the",
        " * wire (header only); writes carry data plus CRC-16.  gapUs is the idle time",
        " * observed before that request in the original trace.",
        " ******************************************************************************/",
        "",
        '#include "Ifx_Types.h"',
        "",
        f"#define CAN_UART_MASTER_MAX_REQUEST_LEN   {max_len}u",
        "",
        "typedef struct",
        "{",
        "    uint32 gapUs;          /* idle time before this request            */",
        "    uint8  len;            /* request length in bytes                  */",
        "    uint8  expectResponse; /* 1 for reads, 0 for writes                */",
        f"    uint8  data[{max_len}];       /* request bytes, MSB first on the wire     */",
        "} CanUartMasterStep;",
        "",
    ]
    lines += steps(startup, "s_osramStartup")
    lines.append("")
    lines += steps(cycle, "s_osramCycle")
    lines += [
        "",
        f"#define CAN_UART_OSRAM_STARTUP_STEPS   {len(startup)}u",
        f"#define CAN_UART_OSRAM_CYCLE_STEPS     {len(cycle)}u",
        "",
        "#endif /* CAN_UART_OSRAM_SEQUENCE_H */",
        "",
    ]

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    total = sum(4 + len(r.request_hex) // 2 for r in startup + cycle)
    print(f"\nwrote {path}: {len(startup)} start-up steps, {len(cycle)} cycle steps, "
          f"about {total} bytes of payload")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_path")
    parser.add_argument("--show-startup", type=int, default=0,
                        help="print the first N start-up requests")
    parser.add_argument("--emit-header", help="write the generated C sequence table here")
    args = parser.parse_args()

    records = load(args.trace_path)
    print(f"records: {len(records)}")
    print(f"span: {records[0].timestamp} .. {records[-1].timestamp}")

    writes = [r for r in records if r.op == "W"]
    reads = [r for r in records if r.op == "R"]
    print(f"writes: {len(writes)}  reads: {len(reads)}")

    start, period = find_cycle(records)
    print(f"\ncyclic phase starts at record {start}, period {period} transactions")
    print(f"start-up phase: {start} transactions")

    # The first occurrence of the pattern still carries the transition timing,
    # where the ECU had not yet settled on its keep-alive cadence.  Replay the
    # last complete period instead, which is the real steady state.
    cycle = records[len(records) - period:] if period else []

    if period:
        print("\ncyclic pattern:")
        for r in cycle:
            print(f"  {r.op} 0x{r.address:04X}  ifDelay={r.if_delay_us} us  req={r.request_hex}")

        cycle_us = sum(r.if_delay_us for r in cycle)
        print(f"  cycle time: about {cycle_us} us")

    startup = records[:start]
    startup_writes = [r for r in startup if r.op == "W"]
    print(f"\nstart-up writes: {len(startup_writes)} to {len(set(r.address for r in startup_writes))} registers")

    print("\nmost written registers during start-up:")
    for addr, count in Counter(r.address for r in startup_writes).most_common(15):
        last = [r for r in startup_writes if r.address == addr][-1]
        print(f"  0x{addr:04X}  x{count:<4}  last value 0x{last.value:08X}")

    if args.show_startup:
        print(f"\nfirst {args.show_startup} start-up requests:")
        for r in startup[:args.show_startup]:
            print(f"  {r.seq:5} {r.op} 0x{r.address:04X} = 0x{r.value:08X}  "
                  f"gap={r.if_delay_us} us  req={r.request_hex}")

    if args.emit_header:
        emit_header(args.emit_header, startup, cycle, args.trace_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
