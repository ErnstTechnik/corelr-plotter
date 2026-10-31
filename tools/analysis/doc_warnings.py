#!/usr/bin/env python3
#
# doc_warnings.py
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
"""doc_warnings.py -- what did Doxygen complain about, module by module? (REQ-CoreLR-D-Build-0013)

Reads the warning log of one documentation of the `doc` target (build/doxygen/serial/doxygen-warnings.log, or sd/) and sorts every warning into one of five classes:

  undocumented  "Member X (function) of file F is not documented." -- a gap in the documentation, not an error; the inventory of what is still to write
  incomplete    "The following parameter(s) of f(..) are not documented:" -- a function with some @param but not all; a gap too
  markup        everything else: a comment that Doxygen could not read (an unknown tag, a '#word' taken for a link, an unresolved \\ref, an @param that names
                nothing, a misplaced @return, an obsolete Doxyfile tag...). THESE are errors: the `doc` check fails on them.
  external      any warning located in third_party/ (vendored code, read by Doxygen as it is: its comments and its macros are not ours to fix). Counted in one row,
                "(third_party)", whatever the kind; never an error.
  nodot         "ignoring \\dot command because HAVE_DOT is not set": the diagrams of the comments were skipped because `dot` (Graphviz) is missing. The documentation is
                complete without them; the table ends with a line saying so. Not an error.

A line that does not start with "file:line:" or "warning:" continues the warning before it (Doxygen wraps and lists candidates). The module of a warning is the name of its file without the extension (Motion.h and Motion.c are one module, Motion). Warnings that name no file (Doxygen's
own configuration messages) belong to "(doxygen)".

The inventory of the gaps is kept in git, one file per image (docs/doxygen/gaps_serial.txt, gaps_sd.txt): the undocumented items and the incomplete @param lists, one line each
("file: what Doxygen says", no line number, so that a line of code added above does not change it), sorted, behind a first line "# doxygen <version>". It is a ratchet: --baseline
FILE fails on a gap that the file does not list (a new undocumented item) and on a listed gap that is gone (remove its line: the file can only shrink). Another Doxygen version
than the file's does not report the same gaps (it reads macros and enum values its own way): then the differences are only printed, with the two versions, and do not fail.
--gaps prints the file to write: tools/analysis/doc_warnings.py --gaps build/doxygen/serial/doxygen-warnings.log > docs/doxygen/gaps_serial.txt

Usage:   doc_warnings.py [-h] [--list CLASS] [--check] [--gaps] [--baseline FILE] [--doxygen-version V] LOG
         --list CLASS  print the warnings of that class (undocumented, incomplete, markup, nodot, external, or all) instead of the table
         --check       exit with the number of markup warnings (0 = clean); without it the exit status is 0
         --gaps        print the inventory of the gaps (the baseline file's content) instead of the table
         --baseline F  compare the gaps with that file; print the new ones ("new gap: ...") and the vanished ones ("gap fixed, remove it from F: ...")
         --doxygen-version V  the version that wrote LOG (default: asked to `doxygen --version`); written by --gaps, compared by --baseline
Exit status: see --check; with --baseline, the number of differences (0 when the versions differ); 2 for a usage error or an unreadable log or file.
Example: tools/analysis/doc_warnings.py build/doxygen/serial/doxygen-warnings.log
         tools/analysis/doc_warnings.py --list markup --check build/doxygen/sd/doxygen-warnings.log
         tools/analysis/doc_warnings.py --baseline docs/doxygen/gaps_sd.txt build/doxygen/sd/doxygen-warnings.log
"""
import argparse, collections, os, re, shutil, subprocess, sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CLASSES = ("undocumented", "incomplete", "markup", "nodot", "external")
LOCATION = re.compile(r"^(?P<file>[^\s:][^:]*):(?P<line>\d+): (?P<text>.*)$")
UNDOCUMENTED = re.compile(r"^warning: .* is not documented\.$")
INCOMPLETE = re.compile(r"^warning: The following parameters? of .* (is|are) not documented:$")
EXTERNAL = re.compile(r"(^|/)third_party/")
NODOT = re.compile(r"^warning: ignoring \\(dot|dotfile|msc|mscfile|diafile|plantuml) command because HAVE_DOT is not set")
CONTINUATION = re.compile(r"^\s+parameter '")


def classify(text):
    """The class of one warning's first line (the text after 'file:line: ')."""
    if UNDOCUMENTED.match(text):
        return "undocumented"
    if INCOMPLETE.match(text):
        return "incomplete"
    if NODOT.match(text):
        return "nodot"
    return "markup"


def module_of(path):
    return os.path.splitext(os.path.basename(path))[0] if path else "(doxygen)"


def parse(lines):
    """-> a list of (module, class, 'file:line', text). A line that starts with blanks belongs to the warning before it; the parameter names of an
    'incomplete' warning are appended to its text, any other continuation line is kept as part of the warning (Doxygen wraps long messages)."""
    found = []
    for raw in lines:
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        m = LOCATION.match(line)
        if m:
            text = m.group("text")
            file = m.group("file")
            external = EXTERNAL.search(file) is not None
            found.append(("(third_party)" if external else module_of(file), "external" if external else classify(text),
                          "%s:%s" % (file, m.group("line")), text))
        elif found and not line.startswith("warning:"):
            module, cls, where, text = found[-1]                  # no 'file:line:' prefix and not a warning of its own: the rest of the one before
            found[-1] = (module, cls, where, text + "\n" + line)
        else:
            found.append((module_of(None), classify(line), "", line))   # Doxygen's own message
    return found


def table(found):
    counts = collections.defaultdict(lambda: dict.fromkeys(CLASSES, 0))
    for module, cls, _, _ in found:
        counts[module][cls] += 1
    rows = ["%-16s %12s %10s %8s %7s %8s" % ("module", "undocumented", "incomplete", "markup", "no dot", "external")]
    total = dict.fromkeys(CLASSES, 0)
    for module in sorted(counts, key=str.lower):
        c = counts[module]
        rows.append("%-16s %12d %10d %8d %7d %8d" % (module, c["undocumented"], c["incomplete"], c["markup"], c["nodot"], c["external"]))
        for k in CLASSES:
            total[k] += c[k]
    rows.append("%-16s %12d %10d %8d %7d %8d" % ("TOTAL", total["undocumented"], total["incomplete"], total["markup"], total["nodot"], total["external"]))
    if total["nodot"]:
        rows.append("%d diagram(s) skipped: dot (Graphviz) was not found when this was generated; install graphviz (sudo apt install graphviz) and generate again. Nothing else is missing." % total["nodot"])
    return "\n".join(rows), total


def relative(path, root):
    """The path as seen from the repository's root (Doxygen writes absolute ones): the baseline file must not hold a path of the machine that made it."""
    return os.path.relpath(path, root) if root and path.startswith(root.rstrip("/") + "/") else path


def gaps(found, root=None):
    """-> the sorted lines of the inventory: 'file: what Doxygen says' (no line number, the path from the repository's root) for every undocumented item and
    incomplete @param list that is ours."""
    lines = []
    for _, cls, where, text in found:
        if cls in ("undocumented", "incomplete"):
            lines.append("%s: %s" % (relative(where.rsplit(":", 1)[0], root), " ".join(text.split()).replace("warning: ", "", 1)))
    return sorted(lines)


def read_baseline(text):
    """-> (the Doxygen version the file was written with or '', its gap lines)."""
    version, lines = "", []
    for line in text.splitlines():
        if line.startswith("# doxygen "):
            version = line[len("# doxygen "):].strip()
        elif line.strip() and not line.startswith("#"):
            lines.append(line.strip())
    return version, lines


def compare(found, baseline_text, version, root=None):
    """-> (differences, notes): a gap the file does not list, a listed gap that is gone. When `version` is not the file's, the differences become notes."""
    base_version, listed = read_baseline(baseline_text)
    have = collections.Counter(gaps(found, root))
    want = collections.Counter(listed)
    diffs = ["new gap: %s" % k for k in sorted((have - want).elements())]
    diffs += ["gap fixed, remove it from the baseline: %s" % k for k in sorted((want - have).elements())]
    if base_version != version:
        notes = ["the baseline is for Doxygen %s, this log is Doxygen %s: not compared" % (base_version or "?", version or "?")] if diffs else []
        return [], notes + diffs
    return diffs, []


def doxygen_version():
    exe = shutil.which("doxygen")
    if not exe:
        return ""
    try:
        return subprocess.run([exe, "--version"], capture_output=True, text=True, check=True).stdout.split()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        return ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--list", metavar="CLASS", choices=CLASSES + ("all",))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--gaps", action="store_true")
    ap.add_argument("--baseline", metavar="FILE")
    ap.add_argument("--doxygen-version", metavar="V")
    ap.add_argument("log", nargs="?")
    args = ap.parse_args(argv)
    if args.help:
        print(__doc__)
        return 0
    if not args.log:
        print("doc_warnings.py: no log given (try -h)", file=sys.stderr)
        return 2
    try:
        with open(args.log, encoding="utf-8", errors="replace") as f:
            found = parse(f)
    except OSError as e:
        print("doc_warnings.py: cannot read %s: %s" % (args.log, e), file=sys.stderr)
        return 2
    version = args.doxygen_version if args.doxygen_version is not None else doxygen_version()
    if args.gaps:
        print("# doxygen %s" % version)
        print("# The undocumented items and incomplete @param lists of one image's documentation (tools/analysis/doc_warnings.py -h): a ratchet, the list can only shrink.")
        print("\n".join(gaps(found, REPO)))
        return 0
    if args.baseline:
        try:
            with open(args.baseline, encoding="utf-8") as f:
                diffs, notes = compare(found, f.read(), version, REPO)
        except OSError as e:
            print("doc_warnings.py: cannot read %s: %s" % (args.baseline, e), file=sys.stderr)
            return 2
        print("\n".join(notes + diffs) if notes or diffs else "the gaps are those of %s" % args.baseline)
        return len(diffs)
    summary, total = table(found)
    if args.list:
        for module, cls, where, text in found:
            if args.list in ("all", cls):
                print("[%s] %s: %s" % (cls, where or module, text.replace("\n", " ").strip()))
    else:
        print(summary)
    return total["markup"] if args.check else 0


if __name__ == "__main__":
    sys.exit(main())
