#!/usr/bin/env python3
#
# test_variant_modules.py
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
"""test_variant_modules.py -- the list of the modules of ONE image (cmake/variant_modules.txt) against the `#if` guards of the sources (REQ-CoreLR-D-Build-0014)

The build, the IDE export, the reference build and the documentation read cmake/variant_modules.txt; the sources are also guarded, as a second line of defence
(an IDE sketch compiles every file it finds, and Doxygen reads the files itself). The two must say the same thing:

  - a file of the list that lies in firmware/src is WHOLLY under `#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_<its image>`: nothing but `#include "SwConfig.h"`
    outside the condition, no `#else`;
  - a file of firmware/src that is not in the list is not wholly guarded for one image (else the list is missing it);
  - every path of the list exists, and the image is sd or serial.

Part 1 runs the checker on fabricated files, one defect at a time (the part tests/mutation/mutants/variant_modules.txt breaks); part 2 checks the repository.

Usage:   test_variant_modules.py [-h]
Exit status: the number of failed checks (0 = all pass).
"""
import os, re, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
GUARD = re.compile(r"^CORELR_JOB_SOURCE\s*==\s*CORELR_JOB_SOURCE_(SD|SERIAL)\s*$")
SOURCES = (".h", ".c", ".cpp")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def wholly_guarded_by(text):
    """-> 'sd' or 'serial' if the whole file is under `#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_<IMAGE>` (outside it, only `#include "SwConfig.h"` and blank
    lines; no #else/#elif at the top level of the condition), else None."""
    depth = 0
    image = None
    for raw in strip_comments(text).split("\n"):
        line = raw.strip()
        if not line:
            continue
        m = re.match(r"#\s*(\w+)\s*(.*)$", line)
        if m is None:
            if depth == 0:
                return None                                      # code outside any condition
            continue
        word, rest = m.group(1), m.group(2).strip()
        if word in ("if", "ifdef", "ifndef"):
            if depth == 0:
                g = GUARD.match(rest) if word == "if" else None
                if g is None or (image is not None and g.group(1).lower() != image):
                    return None
                image = g.group(1).lower()
            depth += 1
        elif word in ("else", "elif"):
            if depth == 1:
                return None                                      # the other branch of the image condition is code of the other image
        elif word == "endif":
            depth -= 1
            if depth < 0:
                return None
        elif depth == 0:
            if not (word == "include" and rest.replace(" ", "") == '"SwConfig.h"'):
                return None                                      # another include or a #define outside the condition
    return image if depth == 0 else None


def parse_list(text):
    """-> ([(path, image)], [problems]) of the text of cmake/variant_modules.txt."""
    entries, problems = [], []
    for n, line in enumerate(text.split("\n"), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "|" not in line:
            problems.append("line %d: no '|': %r" % (n, line))
            continue
        path, image = [x.strip() for x in line.split("|", 1)]
        if image not in ("sd", "serial"):
            problems.append("line %d: the image of %s must be sd or serial, not %r" % (n, path, image))
            continue
        entries.append((path, image))
    return entries, problems


def check(root):
    """-> the list of problems of the repository (or fabricated one) at `root`."""
    problems = []
    entries, bad = parse_list(read(os.path.join(root, "cmake", "variant_modules.txt")))
    problems += bad
    listed = {}
    for path, image in entries:
        full = os.path.join(root, path)
        if not os.path.exists(full):
            problems.append("%s is in the list but does not exist" % path)
        if path in listed:
            problems.append("%s is in the list twice" % path)
        listed[path] = image
        if path.startswith("firmware/src/") and os.path.isfile(full):
            got = wholly_guarded_by(read(full))
            if got != image:
                problems.append("%s belongs to the %s image (list) but is %s" %
                                (path, image, "not wholly guarded for it" if got is None else "wholly guarded for the %s image" % got))
    src = os.path.join(root, "firmware", "src")
    for name in sorted(os.listdir(src)) if os.path.isdir(src) else ():
        path = "firmware/src/" + name
        if name.endswith(SOURCES) and path not in listed:
            got = wholly_guarded_by(read(os.path.join(src, name)))
            if got is not None:
                problems.append("%s is wholly guarded for the %s image but is not in cmake/variant_modules.txt" % (path, got))
    return problems


# ---------------------------------------------------------------------------------------------------------------- part 1: fabricated cases
HEADER = '/* license */\n#include "SwConfig.h"\n\n/** @file */\n'
GUARDED_SD = HEADER + '#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD\n#ifndef X_H\n#define X_H\nint f(void);\n#endif\n#endif\n'


def selftest():
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append("%s: got %r, want %r" % (label, got, want))

    expect("a header wholly guarded for sd", wholly_guarded_by(GUARDED_SD), "sd")
    expect("the same for serial", wholly_guarded_by(GUARDED_SD.replace("_SD", "_SERIAL")), "serial")
    expect("a file with no guard", wholly_guarded_by("int f(void);\n"), None)
    expect("an empty file", wholly_guarded_by("/* only a comment */\n"), None)
    expect("code before the condition", wholly_guarded_by("int x;\n" + GUARDED_SD), None)
    expect("code after the condition", wholly_guarded_by(GUARDED_SD + "int y;\n"), None)
    expect("another include before the condition", wholly_guarded_by('#include <stdint.h>\n' + GUARDED_SD), None)
    expect("a #define before the condition", wholly_guarded_by("#define A 1\n" + GUARDED_SD), None)
    expect("an include guard around the condition is not a variant guard", wholly_guarded_by("#ifndef X_H\n#define X_H\n" + GUARDED_SD + "#endif\n"), None)
    expect("an #else is code of the other image", wholly_guarded_by(GUARDED_SD.replace("#ifndef X_H", "#else\nint g;\n#ifndef X_H")), None)
    expect("an #elif is too", wholly_guarded_by(GUARDED_SD.replace("#ifndef X_H", "#elif 1\n#ifndef X_H")), None)
    expect("an #else inside the include guard is fine", wholly_guarded_by(GUARDED_SD.replace("int f(void);", "#if A\nint f(void);\n#else\nint g(void);\n#endif")), "sd")
    expect("an unbalanced #endif", wholly_guarded_by(GUARDED_SD + "#endif\n"), None)
    expect("an #endif too many, made good by an #if", wholly_guarded_by(GUARDED_SD + "#endif\n#if A\n"), None)
    expect("a missing #endif", wholly_guarded_by(GUARDED_SD.rsplit("#endif\n", 1)[0]), None)
    expect("a condition on something else", wholly_guarded_by(GUARDED_SD.replace("CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD", "CORELR_MATH == CORELR_MATH_48")), None)
    expect("a negated condition", wholly_guarded_by(GUARDED_SD.replace("CORELR_JOB_SOURCE ==", "CORELR_JOB_SOURCE !=")), None)
    expect("two conditions for different images", wholly_guarded_by(GUARDED_SD + GUARDED_SD.replace("_SD", "_SERIAL")), None)
    expect("two conditions for the same image", wholly_guarded_by(GUARDED_SD + GUARDED_SD), "sd")
    expect("a comment that looks like code", wholly_guarded_by(HEADER + "// int x;\n/* int y; */\n" + GUARDED_SD), "sd")
    expect("spaces in the condition", wholly_guarded_by(GUARDED_SD.replace("CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD", "CORELR_JOB_SOURCE   ==   CORELR_JOB_SOURCE_SD ")), "sd")
    expect("a space after the hash", wholly_guarded_by(GUARDED_SD.replace("#if CORELR", "# if CORELR")), "sd")

    # the list
    entries, bad = parse_list("# c\n\nfirmware/src/A.c|sd\nthird_party/lib | serial\n")
    expect("entries", entries, [("firmware/src/A.c", "sd"), ("third_party/lib", "serial")])
    expect("no problem in a good list", bad, [])
    expect("an unknown image", len(parse_list("a|both\n")[1]), 1)
    expect("an image with a capital", len(parse_list("a|SD\n")[1]), 1)
    expect("a line with no bar", len(parse_list("a\n")[1]), 1)
    expect("a comment line is skipped", parse_list("#a|sd\n")[0], [])

    # the repository checker, on a fabricated tree
    def tree(listing, files):
        d = tempfile.mkdtemp()
        os.makedirs(os.path.join(d, "cmake"))
        os.makedirs(os.path.join(d, "firmware", "src"))
        with open(os.path.join(d, "cmake", "variant_modules.txt"), "w") as f:
            f.write(listing)
        for name, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, name)), exist_ok=True)
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        return d

    base = {"firmware/src/A.h": GUARDED_SD, "firmware/src/B.h": "int b(void);\n"}
    expect("a good tree", check(tree("firmware/src/A.h|sd\n", base)), [])
    expect("a listed file that is not guarded", check(tree("firmware/src/A.h|sd\nfirmware/src/B.h|sd\n", base)),
           ["firmware/src/B.h belongs to the sd image (list) but is not wholly guarded for it"])
    expect("a listed file guarded for the other image", check(tree("firmware/src/A.h|serial\n", base)),
           ["firmware/src/A.h belongs to the serial image (list) but is wholly guarded for the sd image"])
    expect("a guarded file that is not listed", check(tree("", base)),
           ["firmware/src/A.h is wholly guarded for the sd image but is not in cmake/variant_modules.txt"])
    expect("a listed path that does not exist", check(tree("firmware/src/A.h|sd\nthird_party/lib|sd\n", base)), ["third_party/lib is in the list but does not exist"])
    expect("a path listed twice", check(tree("firmware/src/A.h|sd\nfirmware/src/A.h|sd\n", base)), ["firmware/src/A.h is in the list twice"])
    expect("a bad image is reported", len(check(tree("firmware/src/A.h|sd\nfirmware/src/B.h|nope\n", base))), 1)
    expect("a library directory of the list needs no guard", check(tree("firmware/src/A.h|sd\nthird_party/lib|sd\n", dict(base, **{"third_party/lib/x.h": "int x;\n"}))), [])
    expect("a guarded .c file that is not listed", check(tree("firmware/src/A.h|sd\n", dict(base, **{"firmware/src/C.c": GUARDED_SD}))),
           ["firmware/src/C.c is wholly guarded for the sd image but is not in cmake/variant_modules.txt"])
    expect("a file of the list outside firmware/src is not read as a source", check(tree("firmware/src/A.h|sd\nthird_party/f.h|sd\n", dict(base, **{"third_party/f.h": "int f;\n"}))), [])
    expect("a non-source file of firmware/src is not examined", check(tree("firmware/src/A.h|sd\n", dict(base, **{"firmware/src/notes.txt": GUARDED_SD}))), [])
    return failures


def main():
    if len(sys.argv) > 1:
        if sys.argv[1] in ("-h", "--help"):
            print(__doc__)
            return 0
        print("test_variant_modules.py: unknown option '%s' (try -h)" % sys.argv[1], file=sys.stderr)
        return 2
    failures = selftest()
    failures += ["repository: " + p for p in check(REPO)]
    for f in failures:
        print("FAIL", f)
    if not failures:
        print("ok: cmake/variant_modules.txt and the #if guards of firmware/src say the same thing")
    return len(failures)


if __name__ == "__main__":
    sys.exit(main())
