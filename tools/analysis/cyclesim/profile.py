# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys, subprocess, bisect, collections, argparse
from avrsim import AVR, load, Halt
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'lib')); import corelr_env
_p = argparse.ArgumentParser(description='Where the cycles go: per-function share of a run on the simulated AVR (avr-nm for the symbols).')
_p.add_argument('elf', metavar='ELF'); _p.add_argument('bin', metavar='BIN', help='the matching .bin (build.sh makes both)')
_a = _p.parse_args()
_b=corelr_env.avr_bin(); BIN=(_b+'/') if _b else ''
elf, binf = _a.elf, _a.bin
syms=[]
for l in subprocess.run([BIN+'avr-nm','-n','--defined-only',elf],capture_output=True,text=True).stdout.splitlines():
    p=l.split()
    if len(p)==3 and p[1] in 'Tt': syms.append((int(p[0],16),p[2]))
addrs=[a for a,_ in syms]
cpu=AVR(load(binf)); prof=collections.Counter(); calls=collections.Counter()
try:
    while True:
        pc=cpu.pc; c0=cpu.cycles; cpu.step()
        prof[syms[bisect.bisect_right(addrs,pc*2)-1][1]]+=cpu.cycles-c0
except Halt: pass
tot=sum(prof.values())
for n,c in prof.most_common(14): print("%-28s %10d  %5.1f%%"%(n,c,100*c/tot))
