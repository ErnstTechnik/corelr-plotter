#!/usr/bin/env python3
#
# check_mutants_union.py
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
"""check_mutants_union.py -- is every mutant of a batch killed by AT LEAST ONE of the runs?

A mutant batch is often run once per test (tools/mutate.py --test T --batch B): each mutant has to be killed by one of the tests, not by every test, so no single
run is clean (the batch header says "each must be caught by at least one of ..."). This reads the outputs of those runs and takes the union:

  caught        CAUGHT in at least one run
  equivalent    in no run, but the name starts with EQUIVALENT: a change that cannot alter behaviour, said so in the batch (tools/mutate.py's doc)
  refused       in no run, but the name starts with REFUSED and EVERY run says INVALID: the build itself rejects this change (a guard such as an #error), which is the
                protection being tested. If it compiles in any run and nothing catches it, it is a gap like any other.
  gap           NOT CAUGHT or INVALID in every run (and not equivalent or refused): the tests do not see this fault, or the mutant cannot run (its text is no longer in the source)

Usage:   check_mutants_union.py [-h] [-q] FILE [FILE ...]        FILE = the output of one `mutate.py --batch` run (stdout and stderr), the same batch in every file
Exit status: 0 = no gap;  1 = at least one gap;  2 = the input cannot be trusted (an empty or unfinished file, no summary line `N mutant(s), M not killed`,
             runs that do not list the same mutants, or no mutant at all). A run that died while loading its batch leaves an empty file: that is a 2, never a silent 0.
Example: tools/mutate.py --test test_origin --batch tests/mutation/mutants/origin.txt > o1.txt 2>&1; ...; tools/check_mutants_union.py o1.txt o2.txt o3.txt
"""
import argparse, re, sys

VERDICT = re.compile(r"^(CAUGHT|NOT CAUGHT|INVALID)\s+(.*\S)\s*$")
SUMMARY = re.compile(r"^(\d+) mutant\(s\), (\d+) not killed\s*$")
TAIL_HANG = re.compile(r" \((?:hang|stream hash changed)\)$")                 # what mutate.py appends to a CAUGHT line
TAIL_INVALID = re.compile(r" \((?:'old' occurs \d+ times in .*|no file .*)\)$")   # and to an INVALID one


def read_run(path):
    """-> (verdicts {name: verdict}, declared number of mutants) or raises ValueError with the reason."""
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError as e:
        raise ValueError("cannot read: %s" % e.strerror)
    if not any(l.strip() for l in lines):
        raise ValueError("empty (the run printed nothing: it probably stopped while loading its batch)")
    verdicts, declared = {}, None
    for l in lines:
        m = SUMMARY.match(l)
        if m:
            declared = int(m.group(1)); continue
        m = VERDICT.match(l)
        if not m:
            continue
        name = m.group(2)
        name = TAIL_INVALID.sub("", name) if m.group(1) == "INVALID" else TAIL_HANG.sub("", name)
        if name in verdicts:
            raise ValueError("the mutant '%s' is listed twice" % name)
        verdicts[name] = m.group(1)
    if declared is None:
        raise ValueError("no summary line `N mutant(s), M not killed`: the run did not finish")
    if declared != len(verdicts):
        raise ValueError("the summary says %d mutants, %d verdict lines were read" % (declared, len(verdicts)))
    if declared == 0:
        raise ValueError("no mutant in the run")
    return verdicts, declared


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", metavar="FILE", help="outputs of mutate.py --batch for the same batch (one per test or setting)")
    ap.add_argument("-q", "--quiet", action="store_true", help="print only the gaps and the totals")
    a = ap.parse_args()

    runs = []
    for f in a.files:
        try:
            runs.append((f, read_run(f)[0]))
        except ValueError as e:
            print("check_mutants_union.py: %s: %s" % (f, e), file=sys.stderr)
            return 2
    first = set(runs[0][1])
    for f, r in runs[1:]:
        if set(r) != first:
            missing, extra = sorted(first - set(r)), sorted(set(r) - first)
            print("check_mutants_union.py: %s does not list the same mutants as %s (%d missing, %d extra: %s)"
                  % (f, runs[0][0], len(missing), len(extra), (missing + extra)[0][:60]), file=sys.stderr)
            return 2

    if not a.quiet:
        for f, r in runs:
            print("  %-48s %3d mutants, %3d caught" % (f[-48:], len(r), sum(v == "CAUGHT" for v in r.values())))
    gaps, equivalent, refused, caught = [], 0, 0, 0
    for name in sorted(first):
        if any(r[name] == "CAUGHT" for _, r in runs):
            caught += 1
        elif name.startswith("EQUIVALENT"):
            equivalent += 1
        elif name.startswith("REFUSED") and all(r[name] == "INVALID" for _, r in runs):
            refused += 1
        else:
            gaps.append((name, sorted({r[name] for _, r in runs})))
    for name, why in gaps:
        print("  GAP  %s   (%s in every run)" % (name[:100], " / ".join(why)))
    print("union of %d run(s): %d mutants, %d caught, %d equivalent, %d refused by the build, %d gap(s)" % (len(runs), len(first), caught, equivalent, refused, len(gaps)))
    return 1 if gaps else 0


if __name__ == "__main__":
    sys.exit(main())
