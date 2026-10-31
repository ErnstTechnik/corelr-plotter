#!/usr/bin/env python3
#
# flag_truth_table.py
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
"""flag_truth_table.py -- the size flags as a truth table: build EVERY combination, to see what the combination brings and not only each flag alone.

The size flags of cmake/size_flags.txt (REQ-CoreLR-D-Build-0012) were first measured one at a time (tools/analysis/flag_sweep.sh), and their gains do not add up: the
code the compiler leaves after one flag changes what another one can save. This builds the firmware with every subset of the flags (2^N builds, N = the number of
factors; `-j` of them at once), each from scratch with tools/analysis/build_size.sh -S (none of the project's size flags: only the subset), and prints:

  1. the truth table: one row per combination, 1 = the flag is on, with the flash, the RAM and the change against the no-flag build;
  2. per factor: ALONE (only that flag), WITH ALL THE OTHERS (what it still brings when everything else is on: the drop-one cost), and the AVERAGE over every pair
     of combinations that differ in that flag alone (its mean main effect) -- when ALONE and WITH ALL differ much, the flags overlap;
  3. the best combination, the all-on one, and whether any flag HURTS in the presence of the others (dropping it would make the image smaller).

To be REDONE before each release (CONTRIBUTING.md): a compiler, a core or a code change moves these figures; a flag that has stopped paying is dropped from cmake/size_flags.txt.
It measures SIZE only: whether the faster/slower, different code still works is for tools/analysis/cyclesim, tools/analysis/stack_depth.py, the host tests and the machine.

Usage:   flag_truth_table.py [-h] [-v serial|sd] [-j N] [-w DIR] [--flags FILE] [--only NAME[,NAME...]] [--md FILE] [--csv FILE] [--list]
Exit status: 0 = done, 2 = bad arguments or the baseline does not build. A combination that does not build is shown as FAILED and left out of the effects.
Example: tools/analysis/flag_truth_table.py -v sd -j 8 --md docs/design-notes/SizeFlags-TruthTable.md
"""
import argparse, concurrent.futures, itertools, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD_TOOL = os.path.join(HERE, "build_size.sh")   # replaceable: tests/tools/test_flag_truth_table.py


def read_factors(path):
    """[(name, compile flags, link flags, status)] from cmake/size_flags.txt."""
    out = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) != 4:
            raise SystemExit("flag_truth_table.py: %s: not 'name|compile|link|status': %r" % (path, line))
        out.append((parts[0].strip(), parts[1].split(), parts[2].split(), parts[3].strip()))
    return out


def combinations(n):
    """Every subset of n factors as a tuple of 0/1, in binary order (the first is all off, the last all on)."""
    return [tuple((m >> (n - 1 - i)) & 1 for i in range(n)) for m in range(1 << n)]


def analyse(names, results):
    """results: {combination tuple: flash or None}. Returns a dict with the baseline, the all-on and the best combination, and per factor its figures:
       alone (flash with only that flag minus the baseline), with_all (flash with everything minus flash with everything but that flag), mean (the mean of
       flash(on) - flash(off) over every pair of combinations that differ in that flag only), and hurts (dropping it from the all-on set makes the image smaller)."""
    n = len(names)
    zero, full = (0,) * n, (1,) * n
    base, allon = results.get(zero), results.get(full)
    ok = {c: v for c, v in results.items() if v is not None}
    best = min(ok, key=lambda c: (ok[c], sum(c))) if ok else None
    factors = {}
    for i, name in enumerate(names):
        alone = tuple(1 if j == i else 0 for j in range(n))
        without = tuple(0 if j == i else 1 for j in range(n))
        diffs = []
        for c, v in ok.items():
            if c[i] == 0:
                c1 = c[:i] + (1,) + c[i + 1:]
                if c1 in ok:
                    diffs.append(ok[c1] - v)
        f = {"alone": (results[alone] - base) if results.get(alone) is not None and base is not None else None,
             "with_all": (allon - results[without]) if allon is not None and results.get(without) is not None else None,
             "mean": (sum(diffs) / len(diffs)) if diffs else None}
        f["hurts"] = bool(f["with_all"] is not None and f["with_all"] > 0)
        factors[name] = f
    return {"baseline": base, "all": allon, "best": best, "best_flash": ok[best] if best else None, "factors": factors}


def build(variant, workdir, combo, factors, tag):
    """Flash and RAM of the variant built with exactly the flags of this combination (None, None if it does not build)."""
    args = [BUILD_TOOL, "-v", variant, "-S", "-s", ROOT, "-b", os.path.join(workdir, tag)]
    for on, (name, cf, lf, st) in zip(combo, factors):
        if on:
            for f in cf: args += ["-F", f]
            for f in lf: args += ["-L", f]
    r = subprocess.run(args, capture_output=True, text=True)
    fl = re.search(r"^flash   : (\d+) ", r.stdout, re.M)
    rm = re.search(r"^RAM     : (\d+) ", r.stdout, re.M)
    return (int(fl.group(1)), int(rm.group(1))) if fl and rm else (None, None)


def render(variant, names, results, rams, summary):
    n = len(names)
    L = []
    w = max(len(x) for x in names)
    L.append("Size flags, truth table -- variant %s, %d factors, %d builds (1 = the flag is on)\n" % (variant, n, 1 << n))
    L.append("| " + " | ".join("%-*s" % (w, x) for x in names) + " | flash | vs none | RAM |")
    L.append("|" + "|".join("-" * (w + 2) for _ in names) + "|------:|-------:|----:|")
    base = summary["baseline"]
    for c in combinations(n):
        v = results.get(c)
        L.append("| " + " | ".join("%-*s" % (w, "1" if b else "0") for b in c) + " | " + ("%5d | %+6d | %4d |" % (v, v - base, rams[c]) if v is not None and base is not None else "FAILED |"))
    L.append("")
    L.append("none: %s bytes;  all on: %s (%+d);  best: %s (%+d) with %s" % (base, summary["all"], (summary["all"] - base) if summary["all"] is not None and base is not None else 0,
             summary["best_flash"], summary["best_flash"] - base if summary["best_flash"] is not None and base is not None else 0,
             ", ".join(x for x, b in zip(names, summary["best"]) if b) or "no flag"))
    L.append("")
    L.append("| factor | alone | with all the others | mean effect | |")
    L.append("|---|---:|---:|---:|---|")
    for x in names:
        f = summary["factors"][x]
        fmt = lambda v: "%+d" % v if isinstance(v, int) else ("%+.1f" % v if v is not None else "-")
        L.append("| %s | %s | %s | %s | %s |" % (x, fmt(f["alone"]), fmt(f["with_all"]), fmt(f["mean"]), "HURTS: dropping it saves %d bytes" % f["with_all"] if f["hurts"] else ""))
    L.append("")
    L.append("alone = flash(only this flag) - flash(none); with all the others = flash(all on) - flash(all on but this one), i.e. what it still brings when everything else is on;")
    L.append("mean effect = the average of flash(on) - flash(off) over every pair of combinations that differ in this flag only. Negative = smaller.")
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--variant", choices=("serial", "sd"), default="sd", help="the firmware variant (default sd: the one with the least room)")
    ap.add_argument("-j", "--jobs", type=int, default=4, help="builds at once (default 4; each needs a few hundred MB of RAM at most)")
    ap.add_argument("-w", "--work", default=os.path.join(tempfile.gettempdir(), "flag_truth_table"), help="work directory, one build directory per combination (default $TMPDIR/flag_truth_table)")
    ap.add_argument("--flags", default=os.path.join(ROOT, "cmake", "size_flags.txt"), help="the list of factors (default cmake/size_flags.txt)")
    ap.add_argument("--only", help="comma-separated factor names: restrict the table to these (default: all of the file's, `off` ones included)")
    ap.add_argument("--md", metavar="FILE", help="also write the report to FILE (Markdown)")
    ap.add_argument("--csv", metavar="FILE", help="also write the table as CSV")
    ap.add_argument("--list", action="store_true", help="list the factors and exit")
    a = ap.parse_args(argv)
    factors = read_factors(a.flags)
    if a.only:
        want = [x.strip() for x in a.only.split(",")]
        missing = [x for x in want if x not in [f[0] for f in factors]]
        if missing:
            print("flag_truth_table.py: unknown factor(s): %s" % ", ".join(missing), file=sys.stderr); return 2
        factors = [f for f in factors if f[0] in want]
    names = [f[0] for f in factors]
    if a.list:
        for name, cf, lf, st in factors:
            print("  %-18s compile: %-40s link: %-10s %s" % (name, " ".join(cf), " ".join(lf), st))
        return 0
    if not factors or len(factors) > 10:
        print("flag_truth_table.py: %d factors (1..10 wanted)" % len(factors), file=sys.stderr); return 2
    os.makedirs(a.work, exist_ok=True)
    combos = combinations(len(factors))
    results, rams = {}, {}
    print("%d builds of the %s variant, %d at a time..." % (len(combos), a.variant, a.jobs), file=sys.stderr, flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futs = {pool.submit(build, a.variant, a.work, c, factors, "c" + "".join(map(str, c))): c for c in combos}
        for fu in concurrent.futures.as_completed(futs):
            c = futs[fu]; results[c], rams[c] = fu.result()
    if results[(0,) * len(factors)] is None:
        print("flag_truth_table.py: the build with no flag does not build", file=sys.stderr); return 2
    summary = analyse(names, results)
    text = render(a.variant, names, results, rams, summary)
    sys.stdout.write(text)
    if a.md:
        os.makedirs(os.path.dirname(os.path.abspath(a.md)), exist_ok=True)
        open(a.md, "w").write("# Size flags: the truth table\n\n" + text)
    if a.csv:
        with open(a.csv, "w") as f:
            f.write(",".join(names) + ",flash,ram\n")
            for c in combos:
                f.write(",".join(map(str, c)) + ",%s,%s\n" % (results[c], rams[c]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
