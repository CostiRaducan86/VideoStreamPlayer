def crc8(data):
    c = 0xFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x1D) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c ^ 0xFF


DLC = {7: 64, 6: 32, 5: 24, 4: 16, 3: 8, 2: 4, 1: 2, 0: 1}


def eeprom(path):
    mem = {}
    with open(path, encoding="utf-8") as trace_file:
        for line in trace_file:
            p = line.strip().split(";")
            if len(p) < 10 or line.startswith("//"):
                continue
            try:
                b = bytes.fromhex(p[9])
            except ValueError:
                continue
            if len(b) < 6 or b[0] != 0x55 or (b[2] & 7) != 7:
                continue
            addr = (b[3] << 8) | b[4]
            dlen = DLC[(b[2] >> 3) & 7]
            for i, v in enumerate(b[5:5 + dlen]):
                mem[addr + i] = v
    return mem


bad = eeprom("docs/LSM_CAN_Docs/DefectPixelSimulationConcept_Docs/trace_Nichia_StartUp_RUN_DefectPixels_20260806_132547.txt")
BASE = 0x21AE
defs = sorted(a - BASE for a in bad if bad[a] == 0x00 and BASE <= a <= 0x61AD and 0 <= a - BASE <= 16383)
print("=== Cal Mod defect pixels (base 0x21AE, 0-based) ===")
for pi in defs:
    pair = "0-1" if pi % 256 < 128 else "2-3"
    print(f"  idx0={pi:5d} display={pi+1:5d} row={pi//256:2d} col={pi%256:3d} segpair={pair}")

f = "docs/LSM_CAN_Docs/DefectPixelSimulationConcept_Docs/trace_Nichia_StartUp_RUN_DefectPixels_20260806_132547.txt"
tot = ok = asic = eep = 0
with open(f, encoding="utf-8") as trace_file:
    for line in trace_file:
        p = line.strip().split(";")
        if len(p) < 10 or line.startswith("//"):
            continue
        try:
            b = bytes.fromhex(p[9])
        except ValueError:
            continue
        if len(b) < 6 or b[0] != 0x55:
            continue
        fun = b[2] & 7
        if fun in (4, 5):
            asic += 1
        elif fun in (6, 7):
            eep += 1
        if fun == 7:
            continue
        addrlen = 2 if fun in (6, 7) else 1
        dlen = DLC[(b[2] >> 3) & 7]
        crcidx = 3 + addrlen + dlen
        if len(b) <= crcidx:
            continue
        tot += 1
        if crc8(b[3:3 + addrlen + dlen]) == b[crcidx]:
            ok += 1
print(f"=== CRC8: {ok}/{tot} FUN4/5/6 frames valid ===")
print(f"=== classification: ASIC(fun4/5)={asic}  EEPROM(fun6/7)={eep} ===")
