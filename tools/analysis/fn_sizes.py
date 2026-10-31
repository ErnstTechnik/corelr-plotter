#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""fn_sizes.py -- code size per function of a linked firmware ELF, or the difference between two.

One ELF : the biggest functions, and the total.
Two ELFs: what grew and what shrank (second minus first), biggest changes first -- the way to see where a
          refactor's bytes went, or to attribute a whole-image delta to functions.

Names are normalised (".constprop.N", ".part.N", ".isra.N", ".lto_priv.N" suffixes folded in, static constructors
merged), so a function the compiler cloned counts once. Needs avr-nm (AVR_BIN of the environment or of local.conf, else auto-detected: local.conf.example).
"""
import argparse, glob, os, re, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib")); import corelr_env

def find_nm():
    return corelr_env.avr_tool("avr-nm")   # AVR_BIN: environment, local.conf, auto-detection

def load(elf):
    out = subprocess.check_output([find_nm(), "-S", "-t", "d", elf], text=True)
    r = {}
    for l in out.splitlines():
        p = l.split()
        if len(p) == 4 and p[2] in "tTwW":
            n = re.sub(r"\.(part|constprop|isra|cold|lto_priv)\.\d+", "", p[3])
            n = re.sub(r"_GLOBAL__I.*", "_GLOBAL_I", n)
            r[n] = r.get(n, 0) + int(p[1])
    return r

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="examples:\n  fn_sizes.py build/grbl_corelr.ino.elf --top 30\n  fn_sizes.py before.elf after.elf          # delta\n  fn_sizes.py a.elf b.elf --filter Motion  # only names containing 'Motion'")
    ap.add_argument("elf", nargs="+", metavar="ELF", help="one ELF to list, or two to compare (second minus first)")
    ap.add_argument("--top", type=int, default=25, help="how many rows (default 25)")
    ap.add_argument("--filter", default="", help="only function names containing this substring")
    a = ap.parse_args()
    if len(a.elf) > 2: ap.error("give one ELF, or two to compare")
    f = lambda d: {k: v for k, v in d.items() if a.filter in k}
    if len(a.elf) == 1:
        d = load(a.elf[0]); print("total of all functions: %d bytes (%d functions)" % (sum(d.values()), len(d)))
        for k, v in sorted(f(d).items(), key=lambda x: -x[1])[:a.top]: print("%6d  %s" % (v, k))
    else:
        x, y = load(a.elf[0]), load(a.elf[1])
        rows = sorted(((y.get(k, 0) - x.get(k, 0), k, x.get(k, "-"), y.get(k, "-")) for k in set(x) | set(y)
                       if y.get(k, 0) != x.get(k, 0) and a.filter in k), key=lambda r: -abs(r[0]))
        print("total change of all functions: %+d bytes" % (sum(y.values()) - sum(x.values())))
        print("%7s  %-40s %8s %8s" % ("delta", "function", "before", "after"))
        for dlt, k, b, c in rows[:a.top]: print("%+7d  %-40s %8s %8s" % (dlt, k, b, c))

if __name__ == "__main__": main()
