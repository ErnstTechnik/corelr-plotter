# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""CPU cost of one SerialLink_Task() pass per kind of line (M-6 of docs/protocol/SerialProtocol.md): python3 run_link.py out/t_link.bin
Built by build.sh from t_link.c; the passes are those bracketed by marks in that file."""
import sys, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description=__doc__)
_p.add_argument('bin', metavar='BIN', help='the .bin built by build.sh from t_link.c')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
m = {}
for c, v in cpu.marks: m.setdefault(v, []).append(c)
names = {0: 'nothing to do (empty ring)', 1: 'G1, 15 characters', 2: 'G1, 38 characters, 6 decimals', 3: 'G1, 80 significant characters',
         4: 'G2 arc, 35 characters (ArcCheck + QueueArc)', 5: 'G1 with a 90-character comment', 6: 'refused line (error:20)', 7: '$G', 8: '$$', 9: '$I', 10: '? (status report)'}
worst = 0
for k in range(0, 11):
    a, b = (10 * k + 1, 10 * k + 2) if k not in (0, 10) else ((1, 2) if k == 0 else (101, 102))
    if a in m and b in m:
        d = m[b][0] - m[a][0]; worst = max(worst, d)
        print("%-46s %7d cycles  %6.2f ms" % (names[k], d, d / 16000.0))
print("longest pass: %d cycles = %.2f ms" % (worst, worst / 16000.0))
