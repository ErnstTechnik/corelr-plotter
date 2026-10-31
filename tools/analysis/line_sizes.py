#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""line_sizes.py -- attribute every byte of a linked firmware to its SOURCE LINE (avr-objdump -d -l), then rank.

Why it exists: function sizes (fn_sizes.py) say WHERE a function is big; this says which LINES made it so, including
code the optimiser inlined from elsewhere (it is charged to the line it came from). It is how the duplicated pairs
(the same expression written for L and for R, a helper inlined seven times) were found: they show up as several lines
with nearly the same cost. Needs the ELF to carry debug line info (the Arduino build does) and avr-objdump.

  line_sizes.py ELF                      the bytes per source file
  line_sizes.py ELF --file Motion.c      the heaviest lines of that file, with the source text
"""
import argparse, collections, glob, os, re, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib")); import corelr_env

def find_objdump():
    return corelr_env.avr_tool("avr-objdump")   # AVR_BIN: environment, local.conf, auto-detection

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", metavar="ELF")
    ap.add_argument("--file", action="append", default=[], metavar="NAME", help="a source file name (repeatable); lists its heaviest lines")
    ap.add_argument("--top", type=int, default=25, help="rows per file (default 25)")
    ap.add_argument("--src", default=os.path.join(here, "..", "..", "firmware", "src"), help="where to find the source text of --file (default: ../../firmware/src)")
    a = ap.parse_args()
    out = subprocess.run([find_objdump(), "-d", "-l", "--no-show-raw-insn", a.elf], capture_output=True, text=True).stdout
    ins = re.compile(r"^\s+([0-9a-f]+):\s+(\S+)"); loc = re.compile(r"^(/\S+):(\d+)")
    cur = None; addrs = []
    for l in out.splitlines():
        m = loc.match(l)
        if m: cur = (m.group(1).split("/")[-1], int(m.group(2))); continue
        m = ins.match(l)
        if m and cur: addrs.append((int(m.group(1), 16), cur))
    byline = collections.Counter(); byfile = collections.Counter()
    for (x, c), (y, _) in zip(addrs, addrs[1:]):
        if 0 < y - x <= 4: byline[c] += y - x; byfile[c[0]] += y - x
    if not a.file:
        print("bytes by source file (inlined code is charged to the file it came from):")
        for f, v in byfile.most_common(a.top): print("%7d  %s" % (v, f))
        return
    for f in a.file:
        path = None
        for root, _, files in os.walk(a.src):
            if f in files: path = os.path.join(root, f); break
        src = open(path).read().splitlines() if path else []
        print("===== %s: %d bytes" % (f, byfile.get(f, 0)))
        for (ff, ln), v in sorted(((k, v) for k, v in byline.items() if k[0] == f), key=lambda x: -x[1])[:a.top]:
            print("%6d  %5d  %s" % (v, ln, src[ln - 1].strip()[:100] if 0 < ln <= len(src) else ""))

if __name__ == "__main__": main()
