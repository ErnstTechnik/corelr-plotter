# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys, subprocess, statistics, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description='Motion generation cost for a move (t_motion.c): entries, duration, cycles per entry and the CPU load while moving, Motion alone.')
_p.add_argument('bin', metavar='BIN', nargs='?', default='out/t_motion.bin', help='the .bin built by build.sh from t_motion.c (default out/t_motion.bin)')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
ents = [tuple(cpu.log[i:i + 5]) for i in range(0, len(cpu.log), 5)]
holds = [e[2] | (e[3] << 8) | (e[4] << 16) for e in ents]
dur = sum(holds) / 2e6
m = [c for c, v in cpu.marks]
total = m[-1] - m[0]
print("%d entries, move lasts %.3f s on the machine; Motion used %d cycles" % (len(ents), dur, total))
print("=> %.0f cycles per entry on average; load while moving: %.1f %% of the CPU (Motion alone, ISRs and LCD not included)" % (total / len(ents), 100.0 * total / (dur * 16e6)))
