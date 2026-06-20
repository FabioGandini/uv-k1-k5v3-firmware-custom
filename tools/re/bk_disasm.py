#!/usr/bin/env python3
# Robust Thumb linear sweep of the stock UV-K1 firmware (PY32F071xB, Cortex-M0+,
# base 0x08000000). Locates the BK SPI register-write routine and dumps the
# register init (addr,value) sequence.
#
# capstone's disasm() stops at the first undecodable halfword (data in the
# code). We sweep manually: on a gap, advance 2 bytes and resume.

import sys
from collections import Counter
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

BASE = 0x08000000
path = sys.argv[1] if len(sys.argv) > 1 else "archive/quansheng.k1.stock.firmware.v7.03.01.bin"
data = open(path, "rb").read()

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_MCLASS)
md.detail = False

def sweep(buf, base):
    insns = []
    off = 0
    n = len(buf)
    while off < n - 1:
        got = False
        for ins in md.disasm(buf[off:off + 4], base + off):
            insns.append(ins)
            off += ins.size
            got = True
            break  # one at a time so we stay aligned to the stream
        if not got:
            off += 2  # undecodable halfword -> skip
    return insns

insns = sweep(data, BASE)
by_addr = {i.address: i for i in insns}
order = sorted(by_addr)
idx_of = {a: k for k, a in enumerate(order)}
print(f"loaded {path}: {len(data)} bytes; {len(insns)} thumb insns")

def read32(addr):
    o = addr - BASE
    if 0 <= o <= len(data) - 4:
        return int.from_bytes(data[o:o + 4], "little")
    return None

# --- find bl call sites preceded by r0(+r1) immediate setup ---
bl_targets = Counter()
bl_setup = Counter()
for a in order:
    ins = by_addr[a]
    if ins.mnemonic != "bl":
        continue
    try:
        target = int(ins.op_str.lstrip("#"), 0)
    except ValueError:
        continue
    bl_targets[target] += 1
    k = idx_of[a]
    win = [by_addr[order[j]] for j in range(max(0, k - 5), k)]
    sets_r0 = any(w.mnemonic.startswith("mov") and w.op_str.startswith("r0,") for w in win)
    sets_r1 = any(w.mnemonic in ("ldr", "movs", "mov") and w.op_str.startswith("r1,") for w in win)
    if sets_r0 and sets_r1:
        bl_setup[target] += 1

print("\n=== writeReg candidates (bl preceded by r0+r1 setup) ===")
for t, c in bl_setup.most_common(10):
    print(f"  0x{t:08x}: {c} calls")

if not bl_setup:
    print("  (none) -- top bl targets overall:")
    for t, c in bl_targets.most_common(10):
        print(f"  0x{t:08x}: {c} calls")
    sys.exit(0)

writereg = bl_setup.most_common(1)[0][0]
print(f"\n>>> assuming writeReg = 0x{writereg:08x}")

# --- walk the stream, resolve (r0=addr, r1=value) at each writeReg call ---
def resolve_ldr_pc(ins):
    # ldr rX, [pc, #imm]  -> literal at ((pc+4)&~3)+imm
    s = ins.op_str
    if "[pc, #" not in s:
        return None
    imm = int(s.split("[pc, #")[1].rstrip("]"), 0)
    lit = ((ins.address + 4) & ~3) + imm
    return read32(lit)

print("\n=== register writes (addr <- value) in program order ===")
print("    (tracks movs/ldr/adds/subs/lsls/orrs on r0 and r1; '?' = unresolved)")
pairs = []
r0 = r1 = None

def imm(ops):
    return int(ops.split("#")[1], 0)

for a in order:
    ins = by_addr[a]
    m, ops = ins.mnemonic, ins.op_str
    # --- r0 (register address) tracking ---
    if m in ("mov", "movs") and ops.startswith("r0, #"):
        r0 = imm(ops)
    elif m == "adds" and ops.startswith("r0, #"):
        r0 = (r0 + imm(ops)) & 0xFF if r0 is not None else None
    # --- r1 (value) tracking ---
    elif m in ("mov", "movs") and ops.startswith("r1, #"):
        r1 = imm(ops)
    elif m == "ldr" and ops.startswith("r1,"):
        r1 = resolve_ldr_pc(ins)
    elif m == "adds" and ops.startswith("r1, #"):
        r1 = (r1 + imm(ops)) & 0xFFFFFFFF if r1 is not None else None
    elif m == "subs" and ops.startswith("r1, #"):
        r1 = (r1 - imm(ops)) & 0xFFFFFFFF if r1 is not None else None
    elif m == "lsls" and ops.startswith("r1, r1, #"):
        r1 = (r1 << imm(ops)) & 0xFFFFFFFF if r1 is not None else None
    elif m == "orrs" and ops.startswith("r1, r1"):
        pass  # leave as-is; can't resolve without other reg
    elif m in ("mov", "movs", "ldr", "adds", "subs", "lsls", "asrs", "lsrs") and ops.startswith("r1"):
        r1 = None  # some r1 mutation we don't model -> mark unknown
    elif m == "bl":
        try:
            target = int(ops.lstrip("#"), 0)
        except ValueError:
            target = None
        if target == writereg and r0 is not None:
            pairs.append((a, r0, r1))
            vs = f"0x{r1 & 0xFFFF:04X}" if r1 is not None else "????"
            wide = " (WIDE!)" if (r1 is not None and r1 > 0xFFFF) else ""
            print(f"  @0x{a:08x}  REG_{r0:02X} <- {vs}{wide}")
            r1 = None  # value consumed; addr may persist

print(f"\ntotal writeReg calls resolved: {len(pairs)}")
