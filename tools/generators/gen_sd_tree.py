#!/usr/bin/env python3
#
# gen_sd_tree.py
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
"""gen_sd_tree.py -- writes a tree of directories and tiny GCode files to try the SD variant's file browser (REQ-CoreLR-D-SDReader-0005/-0006).

The browser keeps the current directory open, lists it by reading raw 8.3 entries, and bounds the depth by SDREADER_PATH_SIZE (64 bytes since 2026-10-13, 32 before): a path that
would not fit is REFUSED (not truncated). The tree, below the directory given with -o, all names 8.3 upper case so that the short names the firmware sees are the names written here
(the path of level n is 5 + 9 (n - 1) characters, "/TREE" then "/D2345678"...; a path must be shorter than SDREADER_PATH_SIZE):

  TREE/                                                  level 1, path  5    L1.GC           (a 25 mm square)
  TREE/D2345678/ ... down to D7890123/                   levels 2..7, the last with a path of 59 characters; L2.GC .. L7.GC in them (30 .. 55 mm) -- all reachable
  TREE/D2345678/.../D7890123/D8901234/                   level 8: path of 68 characters -- must be REFUSED, the browser stays in level 7; L8.GC would be 60 mm
  TREE/MANY/                                             40 files F01.GC..F40.GC and 3 sub-directories S1, S2, S3: the listing order (directories first, then the files, each alphabetical)
                                                         and how the display speed holds along a long list
  TREE/D2345678/.../D6789012/MANY6/                      35 files G01.GC..G35.GC at level 7 (path 56): the deepest stack of the firmware

Every file draws one small square (the side is in the file's first comment line) around the origin with the pen, and returns to the origin: pen up first, `M5`, `G90 G0 X0 Y0`,
`M30`; lines are at most 47 characters (the SD variant's limit). The files differ in size, so that the one that was started is recognised by what the machine draws.

Usage:   gen_sd_tree.py [-h] [-o DIR]
Example: tools/generators/gen_sd_tree.py -o "$MEDIA/CORELR"        (the mount point of the card; the TREE directory is created inside it, an existing one is left alone)
Exit status: 0 done, 2 the directory exists already or cannot be written.
"""
import argparse, os, sys


def square(side):
    h = side / 2.0
    pts = [(-h, -h), (h, -h), (h, h), (-h, h), (-h, -h)]
    lines = ["; square %d mm around the origin" % side, "M5", "G90 G0 X0 Y0", "G21 G17 G54 G94", "G0 X%g Y%g" % pts[0], "M3"]
    lines += ["G1 X%g Y%g" % p for p in pts[1:]]
    lines += ["M5", "G0 X0 Y0", "M30"]
    assert all(len(l) <= 47 for l in lines)
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=".", help="the directory the TREE directory is created in (default: the current directory)")
    a = ap.parse_args(argv)
    root = os.path.join(a.out, "TREE")
    if os.path.exists(root):
        print("gen_sd_tree.py: %s exists already, nothing written" % root, file=sys.stderr)
        return 2
    names = ["D2345678", "D3456789", "D4567890", "D5678901", "D6789012", "D7890123", "D8901234"]   # levels 2..8 (the last must be refused)
    levels = [root]
    for n in names:
        levels.append(os.path.join(levels[-1], n))
    many, many6 = os.path.join(root, "MANY"), os.path.join(levels[5], "MANY6")      # levels[5] is level 6 (D6789012)
    try:
        for d in levels + [many, many6]:
            os.makedirs(d)
        for d in ("S1", "S2", "S3"):
            os.makedirs(os.path.join(many, d))
        files = [(levels[n - 1], "L%d.GC" % n, 20 + 5 * n) for n in range(1, 9)]
        files += [(many, "F%02d.GC" % i, 10 + i % 5) for i in range(1, 41)]
        files += [(many6, "G%02d.GC" % i, 10 + i % 5) for i in range(1, 36)]
        for d, name, side in files:
            open(os.path.join(d, name), "w", newline="\n").write(square(side))
    except OSError as e:
        print("gen_sd_tree.py: %s" % e, file=sys.stderr)
        return 2
    print("wrote %s: %d files in %d directories" % (root, len(files), len(levels) + 2 + 3))
    return 0


if __name__ == "__main__":
    sys.exit(main())
