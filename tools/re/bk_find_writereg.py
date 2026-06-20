#!/usr/bin/env python3
# Recon pass over the stock UV-K1 firmware to locate the BK4829 SPI register
# write routine and the register init sequence.
#
# The K1 MCU is a PY32F071xB (Cortex-M0+, ARMv6-M, Thumb), image loaded at
# 0x08000000 (verified: vector table SP=0x200031a0, reset=0x080028d5).
#
# Strategy: BK4819_WriteRegister(addr, data) is called many times in a row
# during init. On ARMv6-M there is no movw, so the 7-bit addr comes from
# "movs r0,#imm" and the 16-bit data from "ldr r1,[pc,#off]" (literal pool).
# We tally bl targets that are immediately preceded by an r0/r1 setup; the
# hottest one is the write routine.

import sys
from collections import Counter
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

BASE = 0x08000000
path = sys.argv[1] if len(sys.argv) > 1 else "archive/quansheng.k1.stock.firmware.v7.03.01.bin"
data = open(path, "rb").read()
print(f"loaded {path}: {len(data)} bytes, base 0x{BASE:08x}")

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_MCLASS)
md.detail = True

# Linear sweep disassembly (firmware is mostly contiguous thumb code).
insns = list(md.disasm(data, BASE))
print(f"disassembled {len(insns)} instructions")

by_addr = {i.address: i for i in insns}

# Find bl call sites and their targets; record whether r0/r1 were just set.
bl_targets = Counter()
bl_with_setup = Counter()

for idx, ins in enumerate(insns):
    if ins.mnemonic != "bl":
        continue
    # bl target operand
    try:
        target = int(ins.op_str.lstrip("#"), 0)
    except ValueError:
        continue
    bl_targets[target] += 1
    # look back up to 4 insns for movs r0 / ldr r1 pattern
    win = insns[max(0, idx - 4):idx]
    sets_r0 = any(w.mnemonic.startswith("mov") and w.op_str.startswith("r0,") for w in win)
    sets_r1 = any(w.mnemonic in ("ldr", "movs", "mov") and w.op_str.startswith("r1,") for w in win)
    if sets_r0 and sets_r1:
        bl_with_setup[target] += 1

print("\n=== top bl targets overall ===")
for t, c in bl_targets.most_common(12):
    print(f"  0x{t:08x}: {c} calls")

print("\n=== top bl targets preceded by r0+r1 setup (writeReg candidates) ===")
for t, c in bl_with_setup.most_common(12):
    print(f"  0x{t:08x}: {c} calls")
