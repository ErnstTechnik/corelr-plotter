#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""gen_branding_png.py -- renders the PNG files of docs/branding/ from their SVG sources (the SVGs are the source, the PNGs are generated).

The renderer is librsvg (through PyGObject, `gi.repository.Rsvg`): the SVG itself is drawn, nothing is redrawn by hand, so a PNG
cannot drift from its SVG. Each PNG is the SVG's viewBox scaled to the width in the TARGETS table below (height follows the aspect ratio),
transparent where the SVG draws nothing.

Text: the SVGs carry their lettering as outlines, except the labels of machine-schematic.svg, which use the generic `sans-serif`; that PNG
therefore shows the machine's sans-serif font (Noto Sans, SIL OFL 1.1, where it is installed). Check the PNG by eye after a change of machine.

  names...      render only these PNG files (default: all)
  -d DIR        directory holding the SVGs and receiving the PNGs (default docs/branding)
  -l, --list    print the table (PNG, source SVG, width) and exit

Needs: python3 and PyGObject with Rsvg 2.0 and GdkPixbuf (Debian/Ubuntu: python3-gi, gir1.2-rsvg-2.0).
Exit status: 0 done, 1 a source SVG is missing or cannot be read, 2 bad usage or no renderer.

Examples:  tools/generators/gen_branding_png.py        tools/generators/gen_branding_png.py machine-schematic.png favicon-32.png
"""
import argparse, os, sys, warnings

warnings.filterwarnings("ignore", category=DeprecationWarning)  # Rsvg.Handle.get_pixbuf: the only call that needs no cairo bridge

HERE = os.path.dirname(os.path.abspath(__file__))
# (png, svg, width in pixels)
TARGETS = [
    ("logo-512.png",          "logo.svg",              512),
    ("favicon-32.png",        "logo.svg",               32),
    ("logo-56.png",           "logo.svg",               56),
    ("logo-wordmark.png",     "logo-wordmark.svg",    1200),
    ("social-preview.png",    "social-preview.svg",   1280),
    ("machine-icon-512.png",  "machine-icon.svg",      512),
    ("machine-icon-32.png",   "machine-icon.svg",       32),
    ("machine-schematic.png", "machine-schematic.svg", 1280),
]

def render(svg, png, width):
    import re
    import gi
    gi.require_version("Rsvg", "2.0")
    from gi.repository import Rsvg
    data = open(svg, "rb").read().decode("utf-8")
    m = re.search(r'<svg\b[^>]*?\bviewBox="\s*[-\d.]+[ ,]+[-\d.]+[ ,]+([\d.]+)[ ,]+([\d.]+)\s*"', data)
    if not m:
        raise SystemExit("%s: no viewBox" % svg)
    height = round(width * float(m.group(2)) / float(m.group(1)))
    # the root element gets the target size; librsvg then rasterises the viewBox at that size (no cairo bridge needed)
    root = re.search(r'<svg\b[^>]*>', data).group(0)
    root_new = re.sub(r'\s(?:width|height)="[^"]*"', '', root)
    root_new = root_new.replace('<svg', '<svg width="%d" height="%d"' % (width, height), 1)
    sized = data.replace(root, root_new, 1)
    pix = Rsvg.Handle.new_from_data(sized.encode("utf-8")).get_pixbuf()
    if pix is None or (pix.get_width(), pix.get_height()) != (width, height):
        raise SystemExit("%s: librsvg gave %s" % (svg, pix and (pix.get_width(), pix.get_height())))
    pix.savev(png, "png", [], [])
    return width, height

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="PNG files to render (default: all)")
    ap.add_argument("-d", "--dir", default=os.path.join(HERE, "..", "..", "docs", "branding"))
    ap.add_argument("-l", "--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for p, s, w in TARGETS:
            print("%-24s %-24s %5d" % (p, s, w))
        return 0
    known = [t[0] for t in TARGETS]
    for n in a.names:
        if n not in known:
            print("gen_branding_png.py: unknown PNG '%s' (try -l)" % n, file=sys.stderr)
            return 2
    try:
        import gi
        gi.require_version("Rsvg", "2.0")
        from gi.repository import Rsvg
    except (ImportError, ValueError) as e:
        print("gen_branding_png.py: no renderer (%s); see -h for what to install" % e, file=sys.stderr)
        return 2
    rc = 0
    for png, svg, width in TARGETS:
        if a.names and png not in a.names:
            continue
        src = os.path.join(a.dir, svg)
        if not os.path.exists(src):
            print("missing %s" % src, file=sys.stderr); rc = 1; continue
        w, h = render(src, os.path.join(a.dir, png), width)
        print("wrote %s (%dx%d) from %s" % (png, w, h, svg))
    return rc

if __name__ == "__main__":
    sys.exit(main())
