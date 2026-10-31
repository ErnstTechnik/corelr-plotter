# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import math, struct, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description="Self-check of the simulator: Motion_ISqrt run on the simulated AVR must match math.isqrt (needs the CORELR_MATH_64 build of t_isqrt.c).")
_p.add_argument('bin', metavar='BIN', nargs='?', default='out/t_isqrt.bin', help='default out/t_isqrt.bin')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
vals=[0,1,2,3,4,15,16,99999,10**12,614000000000*3,2**64-1,123456789012345678]
res=struct.unpack('<%dI'%len(vals), bytes(cpu.log))
cyc=[b[0]-a[0] for a,b in zip(cpu.marks[0::2],cpu.marks[1::2])]
for v,r,c in zip(vals,res,cyc):
    print("%22d  got %10d  want %10d  %s   %6d cycles"%(v,r,math.isqrt(v),'OK' if r==math.isqrt(v) else 'WRONG',c))
