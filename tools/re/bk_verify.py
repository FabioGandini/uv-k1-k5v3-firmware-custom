#!/usr/bin/env python3
# Verify specific BK register writes by dumping raw instructions + the exact
# literal-pool address each value is read from. Catches disasm artifacts and
# literal-pool dedup. Usage: bk_verify.py <bin> <start_hex> <end_hex>

import sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

BASE = 0x08000000
path, start, end = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
data = open(path, "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_MCLASS)

def read32(addr):
    o = addr - BASE
    return int.from_bytes(data[o:o + 4], "little") if 0 <= o <= len(data) - 4 else None

off = start - BASE
while (BASE + off) < end and off < len(data) - 1:
    chunk = data[off:off + 4]
    got = False
    for ins in md.disasm(chunk, BASE + off):
        extra = ""
        if ins.mnemonic == "ldr" and "[pc, #" in ins.op_str:
            imm = int(ins.op_str.split("[pc, #")[1].rstrip("]"), 0)
            lit = ((ins.address + 4) & ~3) + imm
            val = read32(lit)
            extra = f"   ; lit@0x{lit:08x} = 0x{val:08x}" if val is not None else ""
        raw = chunk[:ins.size].hex()
        print(f"0x{ins.address:08x}: {raw:<8} {ins.mnemonic:<6} {ins.op_str}{extra}")
        off += ins.size
        got = True
        break
    if not got:
        print(f"0x{BASE+off:08x}: {data[off:off+2].hex()}  <data>")
        off += 2
