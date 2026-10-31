#!/usr/bin/env python3
#
# test_check_version.py
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
"""test_check_version.py -- tools/analysis/check_version.py on fabricated repositories, and on this one (REQ-CoreLR-D-Build-0020)

Part 1 runs the tool on small trees, one defect at a time (the part tests/mutation/mutants/check_version.txt breaks); part 2 checks the repository: the version of
firmware/config/Version.h is well formed, written nowhere else, and equal to the one that CMake read (--cmake VERSION, given by the CTest registration).

Usage:   test_check_version.py [-h] [--cmake-version VERSION]
Exit status: the number of failed checks (0 = all pass).
"""
import argparse, importlib.util, io, os, sys, tempfile
from contextlib import redirect_stdout, redirect_stderr

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))


def load_tool():
    spec = importlib.util.spec_from_file_location("check_version", os.path.join(REPO, "tools", "analysis", "check_version.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def header(major=1, minor=2, patch=3, suffix="", extra=""):
    return ('#define CORELR_VERSION_MAJOR  %s\n#define CORELR_VERSION_MINOR  %s\n#define CORELR_VERSION_PATCH  %s\n#define CORELR_VERSION_SUFFIX "%s"\n%s'
            % (major, minor, patch, suffix, extra))


def tree(files):
    d = tempfile.TemporaryDirectory()
    for rel, text in files.items():
        os.makedirs(os.path.dirname(os.path.join(d.name, rel)), exist_ok=True)
        with open(os.path.join(d.name, rel), "w") as f:
            f.write(text)
    return d


def selftest(tool):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append("%s: got %r, expected %r" % (label, got, want))

    def run(files, *argv):
        out, err = io.StringIO(), io.StringIO()
        with tree(files) as root, redirect_stdout(out), redirect_stderr(err):
            status = tool.main(["--root", root] + list(argv))
        return status, out.getvalue().strip(), [x for x in err.getvalue().splitlines() if x]

    H = "firmware/config/Version.h"
    expect("a release version", run({H: header()}), (0, "1.2.3", []))
    expect("a development version", run({H: header(suffix="-dev")}), (0, "1.2.3-dev", []))
    expect("numbers of several digits", run({H: header(10, 20, 305)}), (0, "10.20.305", []))
    expect("a tag from GitHub's variable", run({H: header()}, "--tag", "v1.2.3"), (0, "1.2.3", []))
    expect("a tag with its ref", run({H: header()}, "--tag", "refs/tags/v1.2.3"), (0, "1.2.3", []))
    status, out, err = run({H: header()}, "--tag", "v1.2.4")
    expect("another tag", (status, out, len(err)), (1, "1.2.3", 1))
    expect("another tag, told", "expected the tag v1.2.3" in err[0], True)
    status, out, err = run({H: header(suffix="-dev")}, "--tag", "v1.2.3")
    expect("a tag on a development version", (status, len(err), "must be empty" in err[0]), (1, 1, True))
    status, out, err = run({H: header(suffix="-dev")}, "--tag", "v1.2.3-dev")
    expect("a tag that carries the suffix is wrong twice", (status, len(err)), (2, 2))
    expect("--release on a release", run({H: header()}, "--release")[0], 0)
    status, out, err = run({H: header(suffix="-dev")}, "--release")
    expect("--release on a development version", (status, len(err)), (1, 1))
    expect("CMake agrees", run({H: header(suffix="-dev")}, "--cmake", "1.2.3-dev")[0], 0)
    status, out, err = run({H: header(suffix="-dev")}, "--cmake", "1.2.3")
    expect("CMake read another version", (status, "CMake read the version 1.2.3" in err[0]), (1, True))
    status, out, err = run({H: header(major=7)}, "--cmake", "1.2.3")
    expect("CMake read the old number", status, 1)

    # unreadable Version.h
    expect("no file", run({})[0], 2)
    expect("a number missing", run({H: "#define CORELR_VERSION_MAJOR 1\n#define CORELR_VERSION_MINOR 2\n#define CORELR_VERSION_SUFFIX \"\"\n"})[0], 2)
    expect("a number written twice", run({H: header(extra="#define CORELR_VERSION_PATCH 4\n")})[0], 2)
    expect("a suffix not quoted", run({H: "#define CORELR_VERSION_MAJOR 1\n#define CORELR_VERSION_MINOR 2\n#define CORELR_VERSION_PATCH 3\n#define CORELR_VERSION_SUFFIX -dev\n"})[0], 2)
    expect("a suffix with a space", run({H: header(suffix=" dev")})[0], 2)
    expect("a suffix without a hyphen", run({H: header(suffix="dev")})[0], 2)
    expect("a number that is not one", run({H: header(major="x")})[0], 2)
    expect("the error goes to the error output", run({})[1], "")
    expect("a mention in a comment is not a definition", run({H: "/* #define CORELR_VERSION_MAJOR 9 */\n" + header()}), (0, "1.2.3", []))
    expect("a mention of the suffix in a comment is not a definition", run({H: '/* #define CORELR_VERSION_SUFFIX "-x" */\n' + header()}), (0, "1.2.3", []))
    expect("an indented define is not one either", run({H: "  #define CORELR_VERSION_MAJOR 9\n" + header()}), (0, "1.2.3", []))

    # the version written elsewhere
    ok = {H: header(), "CMakeLists.txt": 'set(CORELR_VERSION "${CORELR_VERSION_NUMBER}")\n', "firmware/src/A.c": 'static const char k[] = "1.1h";\n'}
    expect("the sources are clean", run(ok, "--sources")[0], 0)
    status, out, err = run(dict(ok, **{"CMakeLists.txt": 'set(CORELR_VERSION "0.1.0")\n'}), "--sources")
    expect("a literal in CMakeLists.txt", (status, ("CMakeLists.txt:1:" in err[0])), (1, True))
    status, out, err = run(dict(ok, **{"CMakeLists.txt": '  set(CORELR_VERSION_NUMBER "0.1.0")\n'}), "--sources")
    expect("a literal number in CMakeLists.txt", status, 1)
    status, out, err = run(dict(ok, **{"cmake/Other.cmake": 'set(CORELR_VERSION "2.0.0")\n'}), "--sources")
    expect("a literal in a .cmake file", (status, ("cmake/Other.cmake:1:" in err[0])), (1, True))
    status, out, err = run(dict(ok, **{"firmware/src/A.c": 'x = "0.1.0";\n'}), "--sources")
    expect("a version-like literal in the firmware", (status, ("firmware/src/A.c:1:" in err[0])), (1, True))
    status, out, err = run(dict(ok, **{"firmware/app/s.ino": 'x = "v1.2.3-dev";\n'}), "--sources")
    expect("... in the sketch too", (status, ("firmware/app/s.ino:1:" in err[0])), (1, True))
    status, out, err = run(dict(ok, **{"firmware/config/SwConfig.h": '#define V "4.5.6"\n'}), "--sources")
    expect("... and in the configuration", (status, ("firmware/config/SwConfig.h:1:" in err[0])), (1, True))
    expect("a comment that names a version is fine", run(dict(ok, **{"firmware/src/A.c": '/* Candle 1.1.9 */\n * grbl 1.1.9\n'}), "--sources")[0], 0)
    expect("a version in quotes inside a comment is fine", run(dict(ok, **{"firmware/src/A.c": '/* says "1.2.3" */\n * and "4.5.6"\n// or "7.8.9"\n'}), "--sources")[0], 0)
    expect("a number glued to a word is not a version", run(dict(ok, **{"firmware/src/A.c": 'x = "build5.6.7";\n'}), "--sources")[0], 0)
    expect("other file types are not read", run(dict(ok, **{"firmware/src/notes.txt": '"1.2.3"\n'}), "--sources")[0], 0)
    expect("without --sources the places are not read", run(dict(ok, **{"CMakeLists.txt": 'set(CORELR_VERSION "0.1.0")\n'}))[0], 0)
    expect("Version.h itself may spell it", run(dict(ok, **{H: header(extra='#define X "1.2.3"\n')}), "--sources")[0], 0)
    expect("a repository without a firmware folder", run({H: header()}, "--sources")[0], 0)
    return failures


def check_repository(tool, cmake_version):
    failures = []
    try:
        version = tool.read_version(REPO)
    except tool.VersionError as error:
        return [str(error)]
    failures += tool.source_problems(REPO)
    if cmake_version is not None and cmake_version != tool.version_string(version):
        failures.append("CMake read %s, Version.h says %s" % (cmake_version, tool.version_string(version)))
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--cmake-version", help="the CORELR_VERSION that CMake read: compared with Version.h")
    args = parser.parse_args(argv)
    tool = load_tool()
    failures = ["selftest: " + x for x in selftest(tool)] + ["repository: " + x for x in check_repository(tool, args.cmake_version)]
    for failure in failures:
        print("FAIL " + failure)
    if not failures:
        print("ok: check_version.py notices each defect, and the version of this repository is written once and read alike by CMake")
    return len(failures)


if __name__ == "__main__":
    sys.exit(main())
