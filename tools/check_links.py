#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""check_links.py -- do the relative links of the Markdown files point at something that exists?

Checks every [text](target) and [text]: target in the tracked *.md files: a relative file or directory target must exist
(a file whose first line starts with '# (archived)' is skipped; an '#anchor' or '?query' is stripped, absolute URLs and mailto: are skipped, anchors are not checked). Also reports
references to the retired layout (grbl_corelr/, Doc/, Requirements/, tools/hosttest), which a link check alone would miss
when they are written as plain code spans.

Usage:   check_links.py [-h] [ROOT]      ROOT defaults to the repository root (one level above this script)
Exit status: the number of broken links or stale layout references (0 = clean).
Example: tools/check_links.py
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|^\s*\[[^\]]+\]:\s*(\S+)", re.M)
STALE = re.compile(r"(?<![\w/.-])(grbl_corelr/|Doc/|Requirements/|tools/hosttest|FilesToPrint)")

def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__); return 0
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, ".."))
    files = subprocess.run(["git", "-C", root, "ls-files", "*.md"], capture_output=True, text=True).stdout.split()
    bad = 0
    for f in files:
        path = os.path.join(root, f); text = open(path, encoding="utf-8", errors="replace").read()
        if text.startswith("# (archived)"):
            continue                                 # a frozen historical copy: its links and old names are what they were
        in_code = False; lines = text.split("\n")
        for n, line in enumerate(lines, 1):
            if line.lstrip().startswith("```"): in_code = not in_code
            if in_code: continue
            for m in LINK.finditer(line):
                t = (m.group(1) or m.group(2) or "").split("#")[0].split("?")[0]
                if not t or re.match(r"^[a-z]+:", t): continue
                if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(path), t))):
                    print("%s:%d: broken link -> %s" % (f, n, t)); bad += 1
            if f != "CHANGELOG.md" and STALE.search(line) and "PROVENANCE" not in f and "design-notes/experiments" not in f:
                print("%s:%d: retired layout name: %s" % (f, n, STALE.search(line).group(1))); bad += 1
    print("%d problem(s) in %d file(s)" % (bad, len(files)))
    return bad

if __name__ == "__main__": sys.exit(main())
