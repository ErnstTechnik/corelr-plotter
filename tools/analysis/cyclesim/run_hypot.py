# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cycles per Motion_Hypot call and the results, to compare the two arithmetics: python3 run_hypot.py out/t_hypot_48.bin"""
import sys, math, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description=__doc__)
_p.add_argument('bin', metavar='BIN', help='the .bin built by build.sh from t_hypot.c (-DCORELR_MATH=... picks the arithmetic)')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
a = [c for c, v in cpu.marks if v == 1]; b = [c for c, v in cpu.marks if v == 2]
inp = [(500000, 489898), (700000, 300000), (-123456, 789012), (1000000, 1000000), (4194303, 4194303), (30000, 40000), (214, 0), (5000, 12000), (40, 30), (32767, 1), (32768, 1), (3000000, 17)]
res = [cpu.log[i] | cpu.log[i + 1] << 8 | cpu.log[i + 2] << 16 | cpu.log[i + 3] << 24 for i in range(0, len(cpu.log), 4)]
tot = 0
for (x, y), s, e, r in zip(inp, a, b, res):
    ok = r == math.isqrt(x * x + y * y); tot += e - s
    print("hypot(%8d,%8d) = %8d  %6d cycles  %s" % (x, y, r, e - s, "exact" if ok else "WRONG"))
print("mean %.0f cycles (%.2f ms at 16 MHz)" % (tot / len(a), tot / len(a) / 16000.0))
