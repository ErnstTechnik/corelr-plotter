# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cycle costs of arcs: python3 run_arc.py out/t_arc.bin   (marks as in t_arc.c)"""
import sys, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description=__doc__)
_p.add_argument('bin', metavar='BIN', help='the .bin built by build.sh from t_arc.c (marks 4/5 ArcCheck, 6/7 QueueArc, 2/3 generation)')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
m = {}
for c, v in cpu.marks: m.setdefault(v, []).append(c)
ents = [tuple(cpu.log[i:i+5]) for i in range(0, len(cpu.log), 5)]
hold = sum(e[2] | (e[3] << 8) | (e[4] << 16) for e in ents)
if 4 in m: print("Motion_ArcCheck : %6d cycles (%.2f ms)" % (m[5][0] - m[4][0], (m[5][0] - m[4][0]) / 16000.0))
if 6 in m: print("Motion_QueueArc : %6d cycles (%.2f ms)" % (m[7][0] - m[6][0], (m[7][0] - m[6][0]) / 16000.0))
gen = m[3][0] - m[2][0]
print("generation      : %d entries, %d cycles, the move lasts %.1f ms -> CPU load %.1f %%" % (len(ents), gen, hold / 2000.0, 100.0 * gen / (hold * 8.0)))
