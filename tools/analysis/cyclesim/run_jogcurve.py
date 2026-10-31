# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cycles of JogCurve_Displacement per stick position, and its results (30 ms block): python3 run_jogcurve.py out/t_jog_table.bin"""
import argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description=__doc__)
_p.add_argument('bin', metavar='BIN', help='the .bin built by build.sh from t_jogcurve.c')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
ins = [(500, 0), (33, 0), (200, 150), (-300, 100), (400, 400), (0, -250), (20, 20)]
m = {}
for c, v in cpu.marks: m.setdefault(v, []).append(c)
tot = 0
for i, (x, y) in enumerate(ins):
    d = m[2 * i + 2][0] - m[2 * i + 1][0]; tot += d
    o = cpu.log[4 * i:4 * i + 4]; rx = o[0] | o[1] << 8; ry = o[2] | o[3] << 8
    rx -= 65536 if rx > 32767 else 0; ry -= 65536 if ry > 32767 else 0
    print("stick (%4d,%4d): %5d cycles   moves (%4d,%4d) um in 30 ms" % (x, y, d, rx, ry))
print("mean %.0f cycles (%.3f ms at 16 MHz)" % (tot / len(ins), tot / len(ins) / 16000.0))
