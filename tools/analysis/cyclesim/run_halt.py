# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description='The 2026-10-10 bench scenario (t_halt.c) on the simulated AVR: six chained moves, pause, resume, finish. Prints what Motion did.')
_p.add_argument('bin', metavar='BIN', nargs='?', default='out/t_halt.bin', help='the .bin built by build.sh from t_halt.c (default out/t_halt.bin)')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
l = cpu.log
x = l[3] | (l[4] << 8) | (l[5] << 16) | (l[6] << 24)
x = x - (1 << 32) if x & (1 << 31) else x
print("halted %d, resumed %d; entries after the resume %d; final X %d um; ended idle with an empty planner: %d" % (l[0] & 1, (l[0] >> 1) & 1, l[1] | (l[2] << 8), x, l[7]))
