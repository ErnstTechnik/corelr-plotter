#!/usr/bin/env python3
#
# stack_depth.py
# Part of the CoreLR wall-plotter firmware project.
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
"""Worst-case stack depth of the linked firmware, per call chain.

Why not avr-gcc -fstack-usage: that only reports frames for functions
compiled from our own sources. The two deepest frames in this firmware
are inside the Arduino SD library (SDClass::open, File::openNextFile),
which -fstack-usage never sees -- and it produces nothing at all under
-flto, which the Arduino build uses. Working from the linked image
instead covers every function that actually ends up in the binary.

Usage:
    avr-objdump -d firmware.elf > dis.txt
    ./stack_depth.py dis.txt main __vector_7
    SKIP=FindEntryAtRank,openNextFile ./stack_depth.py dis.txt main

Per function: frame = 2 (the call's return address) + prologue pushes
+ any prologue-time SP adjustment (sbiw/subi on r28). Only the prologue
counts -- sbiw on r28 also appears inside loops as ordinary pointer
arithmetic, and counting those inflates the result wildly (main
measured 3480 bytes before this was restricted to the prologue).

SKIP takes a comma-separated list of substrings; any function whose
name contains one is not entered. Use it to isolate a regime, e.g. to
get the steady-state depth without the SD browser.

With -mcall-prologues (the project's build setting, cmake/Firmware.cmake) a function does not push in its own body: it loads the frame size
into r27:r26 and jumps into libgcc's __prologue_saves__, whose entry point says how many of its 18 pushes (r2..r17, r28, r29) to run. That is
read too: pushes = 18 - offset/2, frame = r27:r26. (Before 2026-10-10 such a function counted 2 bytes: the tool said 128 for main where it is 284.)

Recursion is detected and reported rather than followed, so the number
printed for a recursive chain is a lower bound.
"""
import re,sys,argparse
_ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
_ap.add_argument("disassembly",metavar="DIS",help="avr-objdump -d output of the linked .elf (see above)")
_ap.add_argument("roots",metavar="ROOT",nargs="+",help="entry points to measure, e.g. main __vector_11 __vector_7 __vector_8 (the interrupts add to main's depth)")
_ap.add_argument("--skip",default="",help="comma-separated substrings of function names not to enter (same as the SKIP environment variable)")
_args=_ap.parse_args()
dis=open(_args.disassembly).read().splitlines()
fn=None; frames={}; callees={}; prologue=False; ldi={}
hdr=re.compile(r'^[0-9a-f]+ <([^>]+)>:$')
ins=re.compile(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*\t(\S+)\s*(.*)$')
for L in dis:
    m=hdr.match(L)
    if m:
        fn=m.group(1); frames[fn]=2; callees[fn]=set(); prologue=True; ldi={}; continue
    if fn is None: continue
    m=ins.match(L)
    if not m: continue
    op,arg=m.group(1),m.group(2)
    if op=='ldi' and prologue and arg.startswith(('r26','r27')):
        v=re.split(r'[,\t;]',arg)[1].strip(); ldi[arg[:3]]=int(v,16) if v.startswith('0x') else int(v)
    if op in ('jmp','rjmp') and '<__prologue_saves__' in arg:
        o=re.search(r'<__prologue_saves__(?:\+(0x[0-9a-f]+))?>',arg); off=int(o.group(1),16) if o and o.group(1) else 0
        frames[fn]+=(36-off)//2+ldi.get('r26',0)+256*ldi.get('r27',0)
        prologue=False; continue
    if op in ('jmp','rjmp') and '<__epilogue_restores__' in arg:
        continue
    if op in ('call','rcall','jmp','rjmp','icall','ijmp') or op.startswith('br'):
        if op in ('call','rcall'):
            t=re.search(r'<([^>+]+)',arg)
            if t: callees[fn].add(t.group(1))
        elif op in ('jmp','rjmp'):
            t=re.search(r'<([^>+]+)',arg)      # tail-call
            if t and t.group(1)!=fn: callees[fn].add(t.group(1))
        prologue=False; continue
    if prologue:
        if op=='push': frames[fn]+=1
        elif op in ('sbiw','subi') and arg.startswith('r28'):
            v=re.split(r'[,\t;]',arg)[1].strip()
            v=int(v,16) if v.startswith('0x') else int(v)
            # "subi r28, K" subtracts K from the frame pointer, i.e. it
            # allocates K bytes -- for every K, including K > 127.
            # An earlier version folded large K as 256-K, on the mistaken
            # assumption that it encoded a negative immediate. That turned
            # main()'s real 202-byte frame into 54 and made every margin
            # this tool reported far too optimistic, right up to a
            # firmware that overflowed its stack on real hardware
            # (2026-09-11). Do not "simplify" this back.
            frames[fn]+=v
import os
SKIP=(set(os.environ.get("SKIP","").split(","))|set(_args.skip.split(",")))-{""}
memo={}
def deep(f,stack):
    if f in stack: return (frames.get(f,2),[f+' (recursion!)'])
    if f in memo: return memo[f]
    best=(0,[])
    for c in callees.get(f,()):
        if c not in frames or any(s in c for s in SKIP): continue
        d=deep(c,stack|{f})
        if d[0]>best[0]: best=d
    r=(frames.get(f,2)+best[0],[f]+best[1])
    memo[f]=r; return r
for root in _args.roots:
    tot,path=deep(root,set())
    print(f"--- {root}: {tot} octets")
    acc=0
    for p in path:
        acc+=frames.get(p,2); print(f"      {frames.get(p,2):4d}  (cumul {acc:4d})  {p}")
