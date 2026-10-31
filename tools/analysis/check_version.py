#!/usr/bin/env python3
#
# check_version.py
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
"""check_version.py -- what is the software version, and is it the one of the tag? (REQ-CoreLR-D-Build-0020)

The version is written once, in firmware/config/Version.h (CORELR_VERSION_MAJOR, _MINOR, _PATCH and _SUFFIX). This tool reads that file as text and prints the version string:
`<x.y.z>-dev` between two releases, `<x.y.z>` on the commit that carries the tag `v<x.y.z>`. Nothing else is read from git: a hash in the version would make two builds of the same sources
differ, and the .hex files are compared byte for byte.

Usage:   check_version.py [-h] [--root DIR] [--tag TAG] [--release] [--cmake VERSION] [--sources]
         --root DIR       the repository (default: the one this tool is in)
         --tag TAG        the release tag (`v1.0.0`, or `refs/tags/v1.0.0` as GitHub gives it): fails unless it is `v` + the version and the suffix is empty. What the `release` workflow runs.
         --release        fails if the suffix is not empty (this is not a release commit)
         --cmake VERSION  fails unless VERSION (the CORELR_VERSION that CMake read) is the version of Version.h: CMake and this tool read the same file by two parsers, and must agree
         --sources        also fails if the version is written anywhere else: a `set(CORELR_VERSION "...")` in CMakeLists.txt or cmake/, a "1.2.3" string literal in firmware/
Exit status: the number of problems found, 0 = fine; 2 for a usage error or a Version.h that cannot be read (a number missing, a suffix not quoted). The version string goes to the standard output.
Example:  tools/analysis/check_version.py --tag "$GITHUB_REF_NAME" --sources
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
VERSION_H = os.path.join("firmware", "config", "Version.h")


class VersionError(Exception):
    pass


def read_version(root):
    """-> (major, minor, patch, suffix) of root/firmware/config/Version.h; VersionError if it cannot be read."""
    path = os.path.join(root, VERSION_H)
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as error:
        raise VersionError("cannot read %s: %s" % (VERSION_H, error.strerror))
    numbers = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        found = re.findall(r"^#define CORELR_VERSION_%s +([0-9]+)\b" % part, text, re.M)
        if len(found) != 1:
            raise VersionError("%s: need exactly one line '#define CORELR_VERSION_%s <number>' (found %d)" % (VERSION_H, part, len(found)))
        numbers.append(int(found[0]))
    found = re.findall(r'^#define CORELR_VERSION_SUFFIX +"([^"]*)"', text, re.M)
    if len(found) != 1:
        raise VersionError('%s: need exactly one line \'#define CORELR_VERSION_SUFFIX "<text>"\' (found %d)' % (VERSION_H, len(found)))
    if not re.fullmatch(r"(-[0-9A-Za-z.]+)?", found[0]):
        raise VersionError("%s: the suffix '%s' is neither empty nor a hyphen and letters (-dev)" % (VERSION_H, found[0]))
    return numbers[0], numbers[1], numbers[2], found[0]


def version_string(version):
    return "%d.%d.%d%s" % version


def tag_problems(version, tag):
    """-> the problems of the release tag `tag` (v1.2.3, or refs/tags/v1.2.3) for that version."""
    problems = []
    name = tag[len("refs/tags/"):] if tag.startswith("refs/tags/") else tag
    if version[3] != "":
        problems.append("the tag %s, but Version.h says %s: the suffix '%s' must be empty on a release commit" % (name, version_string(version), version[3]))
    expected = "v%d.%d.%d" % version[:3]
    if name != expected:
        problems.append("the tag is %s but the numbers of Version.h are %d.%d.%d (expected the tag %s): change Version.h, then tag again" % ((name,) + version[:3] + (expected,)))
    return problems


def source_problems(root):
    """-> the places other than Version.h that write the version."""
    problems = []
    cmake_files = [os.path.join(root, "CMakeLists.txt")]
    cmake_dir = os.path.join(root, "cmake")
    if os.path.isdir(cmake_dir):
        cmake_files += [os.path.join(cmake_dir, x) for x in sorted(os.listdir(cmake_dir)) if x.endswith(".cmake")]
    for path in cmake_files:
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                for number, line in enumerate(f, 1):
                    if re.match(r'\s*set\(CORELR_VERSION(_NUMBER)?\s+"[0-9]', line):
                        problems.append("%s:%d: the version is written here too: it comes from Version.h" % (os.path.relpath(path, root), number))
    for sub in ("firmware/src", "firmware/app", "firmware/config"):
        folder = os.path.join(root, sub)
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            if not name.endswith((".c", ".cpp", ".h", ".ino")) or os.path.join(sub, name) == VERSION_H.replace(os.sep, "/"):
                continue
            with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
                for number, line in enumerate(f, 1):
                    if re.search(r'"[^"]*(?<![0-9A-Za-z.])v?[0-9]+\.[0-9]+\.[0-9]+', line) and not line.lstrip().startswith(("*", "/")):
                        problems.append("%s/%s:%d: a version-like string literal: the version comes from CORELR_VERSION_STRING (Version.h)" % (sub, name, number))
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description="print the software version of firmware/config/Version.h and check it (see the head of the file)")
    parser.add_argument("--root", default=REPO)
    parser.add_argument("--tag")
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--cmake")
    parser.add_argument("--sources", action="store_true")
    args = parser.parse_args(argv)
    try:
        version = read_version(args.root)
    except VersionError as error:
        print("check_version.py: %s" % error, file=sys.stderr)
        return 2
    problems = []
    if args.tag is not None:
        problems += tag_problems(version, args.tag)
    if args.release and version[3] != "":
        problems.append("not a release commit: the suffix of Version.h is '%s'" % version[3])
    if args.cmake is not None and args.cmake != version_string(version):
        problems.append("CMake read the version %s, Version.h says %s" % (args.cmake, version_string(version)))
    if args.sources:
        problems += source_problems(args.root)
    print(version_string(version))
    for problem in problems:
        print("check_version.py: %s" % problem, file=sys.stderr)
    return len(problems)


if __name__ == "__main__":
    sys.exit(main())
