# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import statistics, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description='Cycle cost of the square-root candidates built from t_cands.c (history of REQ-CoreLR-D-Motion-0005).')
_p.add_argument('bin', metavar='BIN', nargs='?', default='out/t_cands.bin', help='default out/t_cands.bin')
_a = _p.parse_args()
cpu=AVR(load(_a.bin)); cpu.run()
print("all candidates exact:", all(cpu.log), "(%d comparisons)"%len(cpu.log))
m={}
for (c,v) in cpu.marks: m.setdefault(v,[]).append(c)
def cost(a,b): return [y-x for x,y in zip(m[a],m[b])]
for name,(a,b) in {"current ISqrt(uint64), L/R-sized":(10,11),"start bit 4^21 (result<2^22)":(20,21),"Newton seeded by previous root":(30,31),"current ISqrt(uint64), v^2-sized":(40,41),"isqrt32, v^2-sized":(50,51)}.items():
    c=cost(a,b); print("%-38s mean %6.0f  min %5d max %5d cycles"%(name,statistics.mean(c),min(c),max(c)))
