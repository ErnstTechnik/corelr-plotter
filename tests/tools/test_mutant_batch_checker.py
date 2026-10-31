#!/usr/bin/env python3
#
# test_mutant_batch_checker.py
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
"""test_mutant_batch_checker.py -- tests of tests/tools/test_mutant_batches.py with batches made by hand.

The checker is run on a folder of batches written here (--dir) and a list of known defects written here (--known), against a real file of the repository
(cmake/CheckSize.cmake, whose text is read at run time so that the test says so if the file moved on): a good mutant, a line with three fields, a file that does
not exist, a text that occurs 0 or 2 times, 'old' equal to 'new', a name used twice, a known defect, a known defect that has been repaired, and the files of
tests/tools/, tests/mutation/ and tools/ that mutate.py can reach.
Put through mutation testing: tests/mutation/mutants/batch_checker.txt.

Usage:   test_mutant_batch_checker.py [-h]
Exit status: the number of failed checks (0 = all passed).
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CHECKER = os.path.join(HERE, "test_mutant_batches.py")
bad = 0


def check(cond, what):
    global bad
    print("%-5s %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        bad += 1


def run(batches, known="", extra=()):
    """batches = {file name: text}; known = text of the list. -> (exit status, output)."""
    with tempfile.TemporaryDirectory() as d:
        for n, t in batches.items():
            open(os.path.join(d, n), "w").write(t)
        k = os.path.join(d, "known.list")
        open(k, "w").write(known)
        bdir = os.path.join(d, "b")
        os.mkdir(bdir)
        for n in batches:
            os.rename(os.path.join(d, n), os.path.join(bdir, n))
        p = subprocess.run([sys.executable, CHECKER, "--dir", bdir, "--known", k, *extra], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


def enc(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("@", "\\@")


def line(name, fname, old, new):
    return "%s @@ %s @@ %s @@ %s\n" % (name, fname, enc(old), enc(new) if new else "(empty)")


def main():
    if len(sys.argv) > 1:
        if sys.argv[1] in ("-h", "--help"):
            print(__doc__)
            return 0
        print("test_mutant_batch_checker.py: unknown option '%s' (try -h)" % sys.argv[1], file=sys.stderr)
        return 2

    src = open(os.path.join(REPO, "cmake", "CheckSize.cmake"), newline="").read()
    ONCE = "foreach(sec text data bootloader)"
    TWICE = "endforeach()"
    ABSENT = "this text is not in CheckSize.cmake at all"
    if src.count(ONCE) != 1 or src.count(TWICE) != 2 or src.count(ABSENT) != 0:
        print("FAIL  cmake/CheckSize.cmake changed: ONCE occurs %d, TWICE %d times (expected 1 and 2); update this test" % (src.count(ONCE), src.count(TWICE)))
        return 1
    i = src.index(ONCE)
    two_lines = src[i:src.index("\n", src.index("\n", i) + 1)]          # this line and the next: an 'old' with a newline in it
    GOOD = line("a good mutant", "CheckSize.cmake", ONCE, "foreach(sec text bootloader)")

    rc, out = run({"good.txt": GOOD})
    check(rc == 0 and "1 mutant(s) read" in out and "0 new" in out, "a good mutant: exit 0, counted")
    rc, out = run({"good.txt": "# a comment\n\n   \n" + GOOD + "  # indented comment\n"})
    check(rc == 0 and "1 mutant(s) read" in out, "comments and blank lines are ignored")
    rc, out = run({"good.txt": line("two lines", "CheckSize.cmake", two_lines, "")})
    check(rc == 0 and "0 new" in out, "an 'old' with a newline in it (\\n in the batch) is found, '(empty)' as the new text is fine")

    # --- the defects
    rc, out = run({"bad.txt": "no new field @@ CheckSize.cmake @@ " + enc(ONCE) + "\n"})
    check(rc == 1 and "FAIL  bad.txt: line 1" in out and "3 field(s) instead of 4" in out, "a line with three fields is a defect, named with its line number")
    rc, out = run({"bad.txt": "x @@ y\n", "good.txt": GOOD})
    check(rc == 1 and "2 field(s)" in out and "good.txt" not in out.split("mutant(s) read")[0] and "1 mutant(s) read" in out,
          "a refused batch does not hide the others: the good one is still read")
    rc, out = run({"bad.txt": line("no such file", "NoSuchFile.c", ONCE, "x")})
    check(rc == 1 and "FAIL  bad.txt: no such file -- no file NoSuchFile.c" in out, "a file that does not exist is a defect")
    rc, out = run({"bad.txt": line("absent", "CheckSize.cmake", ABSENT, "x")})
    check(rc == 1 and "occurs 0 times" in out, "'old' occurring 0 times is a defect")
    rc, out = run({"bad.txt": line("ambiguous", "CheckSize.cmake", TWICE, "x")})
    check(rc == 1 and "occurs 2 times" in out, "'old' occurring twice is a defect")
    rc, out = run({"bad.txt": line("nothing changes", "CheckSize.cmake", ONCE, ONCE)})
    check(rc == 1 and "are the same" in out, "'old' equal to 'new' is a defect")
    rc, out = run({"bad.txt": line("twin", "CheckSize.cmake", ONCE, "a") + line("twin", "CheckSize.cmake", ONCE, "b")})
    check(rc == 1 and "used 2 times" in out, "a name used twice in a batch is a defect")
    rc, out = run({"a.txt": line("twin", "CheckSize.cmake", ONCE, "a"), "b.txt": line("twin", "CheckSize.cmake", ONCE, "b")})
    check(rc == 0, "the same name in two DIFFERENT batches is fine")

    # --- the files mutate.py can reach
    rc, out = run({"g.txt": line("a tests/tools file", "test_mutant_batches.py", "def load_mutate():", "def load_mutate2():")})
    check(rc == 0, "a file of tests/tools/ is found")
    rc, out = run({"g.txt": line("a tests/mutation file", "stale_mutants.txt", "this list can only shrink", "this list can grow")})
    check(rc == 0, "a file of tests/mutation/ is found")
    rc, out = run({"g.txt": line("a tools file", "check_mutants_union.py", "def read_run(path):", "def read_run2(path):")})
    check(rc == 0, "a file of tools/ is found")
    rc, out = run({"g.txt": line("a firmware file", "Motion.c", "Motion_IsIdle(void)", "Motion_IsIdle2(void)")})
    check(rc == 0, "a file of firmware/src/ is found")
    rc, out = run({"g.txt": line("a tests/host file", "run_test.sh", "BUILD FAILED", "BUILD FAILED2")})
    check(rc == 0, "a file of tests/host/ is found")
    rc, out = run({"g.txt": line("one", "CheckSize.cmake", ONCE, "a") + line("two", "CheckSize.cmake", ONCE, "b") + line("three", "CheckSize.cmake", ONCE, "c")})
    check(rc == 0 and "1 batch(es), 3 mutant(s) read" in out, "the mutants of a batch are all counted")

    # --- the list of known defects
    KNOWN = "bad.txt @@ absent @@ the reason\n"
    BAD = {"bad.txt": line("absent", "CheckSize.cmake", ABSENT, "x")}
    rc, out = run(BAD, KNOWN)
    check(rc == 0 and "1 known defect(s) listed, 0 new" in out, "a defect that is in the list passes")
    rc, out = run(BAD, "# a comment\n\n" + KNOWN)
    check(rc == 0, "the list ignores comments and blank lines")
    rc, out = run(BAD, "bad.txt @@ another name @@ the reason\n")
    check(rc == 2 and "FAIL  bad.txt: absent" in out and "FAIL  bad.txt: another name" in out,
          "a defect NOT in the list fails, even if the list has other entries (and the entry that matches nothing is reported too)")
    rc, out = run({"good.txt": GOOD}, "good.txt @@ a good mutant @@ it was broken once\n")
    check(rc == 1 and "repaired or removed" in out and "1 to delete" in out, "a listed mutant that is fine now fails: the list can only shrink")
    rc, out = run({"bad.txt": BAD["bad.txt"] + line("other", "CheckSize.cmake", TWICE, "y")}, KNOWN)
    check(rc == 1 and "FAIL  bad.txt: other" in out and "FAIL  bad.txt: absent" not in out, "a new defect next to a known one fails, and only the new one is named")
    check("1 known defect(s) listed, 1 new, 0 to delete" in out, "the summary counts the known defects and the new ones apart")
    rc, out = run({"bad.txt": line("absent", "CheckSize.cmake", ABSENT, "x")}, "bad.txt @@ absent @@ r\n", extra=("-v",))
    check(rc == 0 and "known bad.txt: absent" in out, "-v lists the known defects")
    rc, out = run(BAD, KNOWN)
    check("known bad.txt" not in out, "without -v the known defects are only counted")
    rc, out = run(BAD, "bad.txt only one field\n")
    check(rc != 0 and "bad line" in out, "a malformed line in the list is an error, not ignored")
    rc, out = run({"a.txt": GOOD, "b.txt": GOOD.replace("a good mutant", "another")})
    check(rc == 0 and "2 batch(es), 2 mutant(s) read" in out, "every batch of the folder is read")
    rc, out = run({})
    check(rc == 0 and "0 batch(es), 0 mutant(s)" in out, "an empty folder is not an error")

    p = subprocess.run([sys.executable, CHECKER, "--dir", REPO + "/tests/mutation/nonexistent", "--known", "/nonexistent/known.list"], capture_output=True, text=True)
    check(p.returncode == 0 and "0 batch(es)" in p.stdout, "no list of known defects and no folder: nothing to check, exit 0")
    p = subprocess.run([sys.executable, CHECKER, "-h"], capture_output=True, text=True)
    check(p.returncode == 0 and "--dir" in p.stdout, "-h prints the usage and exits 0")
    check(subprocess.run([sys.executable, CHECKER, "--nosuch"], capture_output=True, text=True).returncode == 2, "an unknown option is exit 2")

    print("%d check(s) failed" % bad if bad else "all checks passed")
    return bad


if __name__ == "__main__":
    sys.exit(main())
