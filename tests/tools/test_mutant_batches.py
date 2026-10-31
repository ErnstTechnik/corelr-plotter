#!/usr/bin/env python3
#
# test_mutant_batches.py
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
"""test_mutant_batches.py -- can every mutant of every batch RUN? (a static check, no build, a second or two)

tests/mutation/mutants/*.txt hold the mutants (tools/mutate.py). A mutant that cannot run proves nothing, and nothing says so until the batch is run: the first CI
run found a batch refused whole at load (a mutant written over five lines) and three mutants whose text had moved on since G53 changed the code. This loads
every batch with mutate.py's own parser and looks each mutant up the way mutate.py does:

  - the line has four fields (name @@ file @@ old @@ new) -- else mutate.py exits at load and NO mutant of the batch runs;
  - the file exists under firmware/, cmake/, tools/, tests/host/ (mutate.py's search), and 'old' occurs EXACTLY ONCE in it;
  - 'old' differs from 'new'; no two mutants of a batch share a name (the union checker keys on the name).

Known defects are listed in tests/mutation/stale_mutants.txt (`batch.txt @@ mutant name`), each one a mutant to repair or retire. The test fails for a defect that is
NOT in that list (a new one), and for a listed mutant that has been repaired (remove it from the list): the list can only shrink.

Usage:   test_mutant_batches.py [-h] [-v] [--dir DIR] [--known FILE]
         -v: also list the known defects;  --dir, --known: other batches and another list (used by test_mutant_batch_checker.py)
Exit status: the number of unexpected defects and of stale list entries (0 = all as recorded).
"""
import argparse, glob, importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BATCHES = os.path.join(REPO, "tests", "mutation", "mutants")
KNOWN = os.path.join(REPO, "tests", "mutation", "stale_mutants.txt")
SEARCH = ("firmware", "cmake", "tools", "tests/host", "tests/tools", "tests/mutation")   # mutate.py's search order


def load_mutate():
    spec = importlib.util.spec_from_file_location("mutate", os.path.join(REPO, "tools", "mutate.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def find(fname):
    for d in SEARCH:
        for root, dirs, files in os.walk(os.path.join(REPO, d)):
            dirs[:] = [x for x in dirs if x not in ("out", "__pycache__", "build")]
            if fname in files:
                return os.path.join(root, fname)
    return None


def defects(mutate, batches=BATCHES):
    """-> ({(batch, mutant name): reason} for every mutant that cannot run, the number of mutants read)."""
    found, count = {}, 0
    for path in sorted(glob.glob(os.path.join(batches, "*.txt"))):
        b = os.path.basename(path)
        # the line check first, with the line number (mutate.py's parser reports only the start of the line)
        for n, raw in enumerate(open(path, encoding="utf-8"), 1):
            l = raw.rstrip("\n")
            if l.strip() and not l.lstrip().startswith("#") and len(l.split(" @@ ")) != 4:
                found[(b, "line %d: %s" % (n, l[:50]))] = "%d field(s) instead of 4: mutate.py would refuse the WHOLE batch" % len(l.split(" @@ "))
        try:
            rows = mutate.parse_batch(path)
        except SystemExit:
            continue                                           # reported above; no row of this batch can be checked
        names = [r[0] for r in rows]
        count += len(rows)
        for name, fname, old, new in rows:
            if names.count(name) > 1:
                found[(b, name)] = "the name is used %d times in the batch" % names.count(name)
                continue
            src = find(fname)
            if not src:
                found[(b, name)] = "no file %s" % fname
                continue
            k = open(src, newline="", encoding="utf-8").read().count(old)
            if k != 1:
                found[(b, name)] = "'old' occurs %d times in %s (need exactly 1)" % (k, fname)
            elif old == new:
                found[(b, name)] = "'old' and 'new' are the same"
    return found, count


def known(path=KNOWN):
    out = set()
    if os.path.exists(path):
        for l in open(path, encoding="utf-8"):
            if l.strip() and not l.lstrip().startswith("#"):
                p = l.rstrip("\n").split(" @@ ")
                if len(p) < 2:
                    sys.exit("test_mutant_batches.py: bad line in %s: %s" % (path, l[:60]))
                out.add((p[0].strip(), p[1].strip()))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true", help="also list the known defects")
    ap.add_argument("--dir", default=BATCHES, help="the folder of batches (default tests/mutation/mutants)")
    ap.add_argument("--known", default=KNOWN, help="the list of known defects (default tests/mutation/stale_mutants.txt)")
    a = ap.parse_args()
    (found, total), listed = defects(load_mutate(), a.dir), known(a.known)
    new = sorted(k for k in found if k not in listed)
    fixed = sorted(k for k in listed if k not in found)
    for b, n in new:
        print("FAIL  %s: %s -- %s" % (b, n, found[(b, n)]))
    for b, n in fixed:
        print("FAIL  %s: %s -- repaired or removed: delete it from the list of known defects" % (b, n))
    if a.verbose:
        for (b, n) in sorted(k for k in found if k in listed):
            print("known %s: %s -- %s" % (b, n, found[(b, n)]))
    nb = len(glob.glob(os.path.join(a.dir, "*.txt")))
    print("%d batch(es), %d mutant(s) read; %d known defect(s) listed, %d new, %d to delete from the list"
          % (nb, total, len(found) - len(new), len(new), len(fixed)))
    return len(new) + len(fixed)


if __name__ == "__main__":
    sys.exit(main())
