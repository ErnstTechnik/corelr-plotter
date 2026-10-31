#!/usr/bin/env python3
#
# test_check_mutants_union.py
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
"""test_check_mutants_union.py -- tests of tools/check_mutants_union.py with run outputs written by hand.

Each case builds the text a `mutate.py --batch` run prints (the same lines, the same suffixes) and checks the verdict and the exit status of the tool:
the union rule (one run that kills a mutant is enough), the gaps (NOT CAUGHT or INVALID everywhere), EQUIVALENT mutants, and the inputs that must never read
as a success (an empty file, a run that did not finish, runs of different batches).
Put through mutation testing: tests/mutation/mutants/union_checker.txt.

Usage:   test_check_mutants_union.py [-h]
Exit status: the number of failed checks (0 = all passed).
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "check_mutants_union.py")
bad = 0


def check(cond, what):
    global bad
    print("%-5s %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        bad += 1


def run_text(*texts, extra=()):
    """Write each text to a file, run the tool on them; -> (exit status, stdout, stderr)."""
    with tempfile.TemporaryDirectory() as d:
        files = []
        for i, t in enumerate(texts):
            p = os.path.join(d, "run%d.txt" % i)
            open(p, "w").write(t)
            files.append(p)
        p = subprocess.run([sys.executable, TOOL, *extra, *files], capture_output=True, text=True)
        return p.returncode, p.stdout, p.stderr


def run(lines, n=None):
    """The text of one run: verdict lines and the summary (n = the number it declares, by default what there is)."""
    n = len(lines) if n is None else n
    return "\n".join(lines) + "\n%d mutant(s), %d not killed\n" % (n, sum(not l.startswith("CAUGHT") for l in lines))


def main():
    if len(sys.argv) > 1:
        if sys.argv[1] in ("-h", "--help"):
            print(__doc__)
            return 0
        print("test_check_mutants_union.py: unknown option '%s' (try -h)" % sys.argv[1], file=sys.stderr)
        return 2

    A_CAUGHT, A_MISS = "CAUGHT       alpha", "NOT CAUGHT   alpha"
    B_CAUGHT, B_MISS = "CAUGHT       beta", "NOT CAUGHT   beta"

    # --- the union rule
    rc, out, _ = run_text(run([A_CAUGHT, B_MISS]), run([A_MISS, B_CAUGHT]))
    check(rc == 0 and "2 caught" in out and "0 gap" in out, "each mutant killed by a different run: union clean (exit 0)")
    rc, out, _ = run_text(run([A_CAUGHT, B_CAUGHT]))
    check(rc == 0, "one run, everything killed: exit 0")
    rc, out, _ = run_text(run([A_CAUGHT, B_MISS]), run([A_CAUGHT, B_MISS]))
    check(rc == 1 and "GAP  beta" in out and "1 gap" in out, "a mutant no run kills is a gap (exit 1), named")
    check("GAP  alpha" not in out, "a killed mutant is not reported as a gap")
    rc, out, _ = run_text(run([A_CAUGHT, "INVALID      beta ('old' occurs 0 times in GCodeParser.c, need exactly 1)"]),
                          run([A_CAUGHT, "INVALID      beta ('old' occurs 0 times in GCodeParser.c, need exactly 1)"]))
    check(rc == 1 and "GAP  beta   (INVALID in every run)" in out, "INVALID in every run is a gap, the explanation stripped from the name")
    rc, out, _ = run_text(run([A_CAUGHT, "INVALID      beta ('old' occurs 0 times in GCodeParser.c, need exactly 1)"]),
                          run([A_CAUGHT, "INVALID      beta ('old' occurs 2 times in GCodeParser.c, need exactly 1)"]))
    check(rc == 1 and "GAP  beta   (INVALID in every run)" in out, "INVALID with a different count in each run is still ONE mutant (a gap, not a mismatch)")
    rc, out, _ = run_text(run([A_CAUGHT, "INVALID      beta (no file Nowhere.c)"]), run([A_CAUGHT, B_CAUGHT]))
    check(rc == 0, "INVALID in one run but killed in another is fine (the test of that run does not build the mutant)")
    rc, out, _ = run_text(run([A_CAUGHT + " (hang)", "CAUGHT       beta (stream hash changed)"]))
    check(rc == 0 and "2 caught" in out, "the (hang) and (stream hash changed) suffixes do not change the name")
    rc, out, _ = run_text(run([A_CAUGHT + " (hang)", B_CAUGHT]), run([A_CAUGHT, B_CAUGHT]))
    check(rc == 0, "the same mutant with and without (hang) in two runs is still one mutant")
    rc, out, _ = run_text(run([A_CAUGHT, "CAUGHT       beta (stream hash changed)"]), run([A_CAUGHT, B_CAUGHT]))
    check(rc == 0, "the same mutant with and without (stream hash changed) in two runs is still one mutant")

    # --- EQUIVALENT
    eq = "NOT CAUGHT   EQUIVALENT (the zone is symmetric) worst-case search stops short"
    rc, out, _ = run_text(run([A_CAUGHT, eq]))
    check(rc == 0 and "1 equivalent" in out, "an EQUIVALENT mutant that nothing kills is accepted and counted")
    rc, out, _ = run_text(run([A_CAUGHT, "NOT CAUGHT   not EQUIVALENT (hm) x"]))
    check(rc == 1, "only a name that STARTS with EQUIVALENT is an equivalent mutant")
    rc, out, _ = run_text(run([A_CAUGHT, "CAUGHT       EQUIVALENT (wrongly labelled) x"]))
    check(rc == 0 and "1 caught" not in out.split("union")[0] and "2 caught" in out, "an EQUIVALENT-labelled mutant that IS killed counts as caught")

    # --- REFUSED: the build itself rejects the change
    ref = "INVALID      REFUSED BY THE BUILD: code 3 (the guard)"
    rc, out, _ = run_text(run([A_CAUGHT, ref]), run([A_CAUGHT, ref]))
    check(rc == 0 and "1 refused by the build" in out and "0 gap" in out, "a REFUSED mutant that is INVALID in every run is accepted and counted")
    rc, out, _ = run_text(run([A_CAUGHT, ref]), run([A_CAUGHT, "NOT CAUGHT   REFUSED BY THE BUILD: code 3 (the guard)"]))
    check(rc == 1 and "GAP  REFUSED BY THE BUILD" in out, "a REFUSED mutant that builds in one run and is not caught there is a gap")
    rc, out, _ = run_text(run([A_CAUGHT, ref]), run([A_CAUGHT, "CAUGHT       REFUSED BY THE BUILD: code 3 (the guard)"]))
    check(rc == 0 and "0 refused" in out and "2 caught" in out, "a REFUSED mutant that one run catches counts as caught, not refused")
    rc, out, _ = run_text(run([A_CAUGHT, "INVALID      a REFUSED change"]), run([A_CAUGHT, "INVALID      a REFUSED change"]))
    check(rc == 1, "only a name that STARTS with REFUSED is a refused mutant")
    rc, out, _ = run_text(run([A_CAUGHT, "NOT CAUGHT   REFUSED BY THE BUILD: x"]), run([A_CAUGHT, "NOT CAUGHT   REFUSED BY THE BUILD: x"]))
    check(rc == 1, "a REFUSED mutant that no run catches and that builds is a gap")

    # --- inputs that are not to be trusted (exit 2, never a success)
    rc, _, err = run_text("")
    check(rc == 2 and "empty" in err, "an empty file (the run died while loading its batch) is exit 2, not 0")
    rc, _, err = run_text("\n  \n")
    check(rc == 2 and "empty" in err, "a file of blank lines is empty too")
    rc, _, err = run_text(A_CAUGHT + "\n" + B_CAUGHT + "\n")
    check(rc == 2 and "did not finish" in err, "no summary line: the run did not finish (exit 2)")
    rc, _, err = run_text(run([A_CAUGHT, B_CAUGHT], n=3))
    check(rc == 2 and "summary says 3" in err, "the summary and the verdict lines disagree (exit 2)")
    rc, _, err = run_text("mutate.py: bad batch line (need 'name @@ file @@ old @@ new'): x\n")
    check(rc == 2, "an error message and nothing else is exit 2")
    rc, _, err = run_text(run([A_CAUGHT, B_CAUGHT]), run([A_CAUGHT, "CAUGHT       gamma"]))
    check(rc == 2 and "same mutants" in err, "runs of different batches are exit 2")
    rc, _, err = run_text(run([A_CAUGHT, A_CAUGHT]))
    check(rc == 2 and "twice" in err, "a mutant listed twice in a run is exit 2")
    rc, _, err = run_text("0 mutant(s), 0 not killed\n")
    check(rc == 2, "no mutant at all is exit 2")
    p = subprocess.run([sys.executable, TOOL, "/nonexistent/run.txt"], capture_output=True, text=True)
    check(p.returncode == 2 and "cannot read" in p.stderr, "a missing file is exit 2")
    p = subprocess.run([sys.executable, TOOL, "-h"], capture_output=True, text=True)
    check(p.returncode == 0 and "AT LEAST ONE" in p.stdout, "-h prints the usage and exits 0")

    # --- the output
    rc, out, _ = run_text(run([A_CAUGHT, B_MISS]), extra=("-q",))
    check("mutants," not in out.split("union")[0] and "union of 1 run(s)" in out, "-q hides the per-run lines and keeps the totals")
    rc, out, _ = run_text(run([A_CAUGHT, B_MISS]), run([A_CAUGHT, B_CAUGHT]))
    per_run = out.split("union")[0].splitlines()
    check(len(per_run) == 2 and re.search(r"\s2 mutants,\s+1 caught$", per_run[0]) and re.search(r"\s2 mutants,\s+2 caught$", per_run[1]),
          "one line per run, each with its own count of the mutants it killed")

    print("%d check(s) failed" % bad if bad else "all checks passed")
    return bad


if __name__ == "__main__":
    sys.exit(main())
