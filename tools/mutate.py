#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""mutate.py -- mutation testing of the firmware sources against the host tests.

A test that cannot fail proves nothing. This breaks the code on purpose -- one textual replacement at a time, in a COPY of
the tree, never in yours -- builds the chosen host test against the broken copy and says whether the test noticed.

  CAUGHT        the test failed (a FAIL line, a non-zero exit, a crash, a hang past --timeout), or, with --hash, the stream
                hash it prints changed
  NOT CAUGHT    the test passed on the broken code: either a gap in the test, or an EQUIVALENT mutant (a change that cannot
                alter behaviour -- say so in the requirement, and check it really cannot)
  INVALID       the broken copy did not compile, so it proved nothing

A crash counts as caught. (It once did not, and three mutants looked uncaught only because the broken binary segfaulted and
the script was counting FAIL lines.)

One mutant:   mutate.py --test test_arith --file Motion.c --old 'r1--;' --new ''
A batch:      mutate.py --test test_parser --batch mutants.txt
Batch file, one mutant a line, fields separated by ' @@ ' (a token C code does not contain):   name @@ file @@ old @@ new
  \\n is a newline, \\@ a literal '@', \\\\ a literal backslash (C's own \\r\\n is written \\\\r\\\\n in a batch file); lines starting with # and blank lines are ignored. Example line:
  no downward fix-up @@ Motion.c @@ while ((int32_t)(v - r1 * r1) < 0)\\n   {\\n      r1--;\\n   } @@ (empty)
'(empty)' as the new text deletes the old one. Each 'old' must occur EXACTLY ONCE in its file.
Exit status: the number of mutants NOT CAUGHT or INVALID (0 = every mutant was killed).
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))   # tools/mutate.py -> the repository root

def make_tree(dst):
    """A copy of the firmware sources and the host tests, laid out the way the tests expect (../../firmware, ../../third_party)."""
    for d in ("firmware", "third_party", "cmake"):
        shutil.copytree(os.path.join(REPO, d), os.path.join(dst, d), ignore=shutil.ignore_patterns("*.o", "build"))
    shutil.copytree(os.path.join(REPO, "tests", "cmake"), os.path.join(dst, "tests", "cmake"))
    shutil.copytree(os.path.join(REPO, "tests", "tools"), os.path.join(dst, "tests", "tools"))
    # not .venv (a Python environment, 465 MB with scipy and OpenCV: copied for every mutant it filled /tmp when runs were parallel) and not out/ (generated)
    shutil.copytree(os.path.join(REPO, "tools", "generators"), os.path.join(dst, "tools", "generators"), ignore=shutil.ignore_patterns("__pycache__", ".venv", "out"))
    shutil.copytree(os.path.join(REPO, "tools", "bench"), os.path.join(dst, "tools", "bench"), ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(os.path.join(REPO, "tools", "analysis"), os.path.join(dst, "tools", "analysis"), ignore=shutil.ignore_patterns("__pycache__", "cyclesim", "out"))
    shutil.copytree(os.path.join(REPO, "tools", "lib"), os.path.join(dst, "tools", "lib"), ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(os.path.join(REPO, "tests", "host"), os.path.join(dst, "tests", "host"),
                    ignore=shutil.ignore_patterns("out"))
    shutil.copytree(os.path.join(REPO, "tests", "mutation"), os.path.join(dst, "tests", "mutation"))   # the batches and the list of known stale mutants
    for f in ("mutate.py", "check_mutants_union.py"):                                                      # the tools whose tests are mutated too
        shutil.copy(os.path.join(REPO, "tools", f), os.path.join(dst, "tools", f))

SCRIPT = None   # --script: run this script of the tree (path relative to the root) instead of tests/host/run_test.sh

def run_test(tree, test, defines, timeout):
    if SCRIPT: cmd = [os.path.join(tree, SCRIPT)]
    else: cmd = [os.path.join(tree, "tests", "host", "run_test.sh"), "-q"] + [x for d in defines for x in ("-D", d)] + [test]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "hang", ""
    out = p.stdout + p.stderr
    if p.returncode == 2 and "BUILD FAILED" in out: return "build", out
    return ("pass" if p.returncode == 0 else "fail"), out

def stream_hash(tree, test, defines, timeout):
    cmd = [os.path.join(tree, "tests", "host", "out", test)]
    try: p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired: return None
    m = re.search(r"stream hash over \d+ entries: [0-9a-f]+", p.stdout)
    return m.group(0) if m else None

def parse_batch(path):
    rows = []
    for raw in open(path):
        l = raw.rstrip("\n")
        if not l.strip() or l.lstrip().startswith("#"): continue
        parts = l.split(" @@ ")
        if len(parts) != 4: sys.exit("mutate.py: bad batch line (need 'name @@ file @@ old @@ new'): " + l[:80])
        un = lambda s: s.replace("\\\\", "\0").replace("\\n", "\n").replace("\\@", "@").replace("\0", "\\")
        name, f, old, new = parts
        rows.append((name.strip(), f.strip(), un(old), "" if new.strip() == "(empty)" else un(new)))
    return rows

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--test", required=True, help="the host test to run (tests/host/run_test.sh -h lists them)")
    ap.add_argument("--script", help="run this script of the repository (e.g. tests/cmake/test_check_size.sh) instead of the host test; --test is then only a label")
    ap.add_argument("--file", help="source file of the single mutant, by name (e.g. Motion.c), looked up under firmware/, cmake/, tools/, tests/host/, tests/tools/ and tests/mutation/")
    ap.add_argument("--old", help="text to replace; must occur exactly once"); ap.add_argument("--new", default="", help="replacement")
    ap.add_argument("--name", default="mutant", help="label for the single mutant")
    ap.add_argument("--batch", help="a batch file (see above)")
    ap.add_argument("-D", "--define", action="append", default=[], metavar="NAME=V", help="a define for the test build, repeatable")
    ap.add_argument("--hash", action="store_true", help="also count a change of the test's 'stream hash' line as caught (test_motion, test_arc)")
    ap.add_argument("--timeout", type=int, default=300, help="seconds before a run counts as a hang (default 300)")
    a = ap.parse_args()
    global SCRIPT; SCRIPT = a.script
    if a.batch: mutants = parse_batch(a.batch)
    elif a.file and a.old is not None: mutants = [(a.name, a.file, a.old, a.new)]
    else: ap.error("give --file and --old (one mutant) or --batch FILE")

    base_hash = None
    if a.hash:
        with tempfile.TemporaryDirectory() as t:
            make_tree(t); v, out = run_test(t, a.test, a.define, a.timeout)
            if v != "pass": sys.exit("mutate.py: the UNBROKEN code does not pass %s (%s):\n%s" % (a.test, v, out[-500:]))
            base_hash = stream_hash(t, a.test, a.define, a.timeout)
            if not base_hash: sys.exit("mutate.py: --hash given but %s prints no 'stream hash' line" % a.test)
            print("baseline: %s" % base_hash)
    bad = 0
    for name, f, old, new in mutants:
        with tempfile.TemporaryDirectory() as t:
            make_tree(t); path = None
            for root, _, files in [x for d in ("firmware", "cmake", "tools/generators", "tools/analysis", "tests/host", "tests/tools", "tests/mutation", "tools") for x in os.walk(os.path.join(t, d))]:
                if f in files: path = os.path.join(root, f); break
            if not path: print("%-12s %s (no file %s)" % ("INVALID", name, f)); bad += 1; continue
            s = open(path, newline="").read()
            if s.count(old) != 1: print("%-12s %s ('old' occurs %d times in %s, need exactly 1)" % ("INVALID", name, s.count(old), f)); bad += 1; continue
            open(path, "w", newline="").write(s.replace(old, new))
            v, out = run_test(t, a.test, a.define, a.timeout)
            if v == "build": verdict = "INVALID"; bad += 1
            elif v in ("fail", "hang"): verdict = "CAUGHT"
            else:
                verdict = "NOT CAUGHT"
                if a.hash and stream_hash(t, a.test, a.define, a.timeout) != base_hash: verdict = "CAUGHT"
                if verdict != "CAUGHT": bad += 1
            extra = " (hang)" if v == "hang" else (" (stream hash changed)" if verdict == "CAUGHT" and v == "pass" else "")
            print("%-12s %s%s" % (verdict, name, extra))
    print("%d mutant(s), %d not killed" % (len(mutants), bad))
    sys.exit(bad)

if __name__ == "__main__": main()
