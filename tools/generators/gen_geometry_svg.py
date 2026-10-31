#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""gen_geometry_svg.py -- draws the machine's geometry: the dimensions to measure, the two coordinate frames, the calibration, and the cable lengths in the test square.

Writes docs/hardware/images/Geometry.svg, the picture the specification refers to (REQ-CoreLR-F-Calibration-0008). Every number on it is computed here from the two
installation dimensions you give (default: the example D = 1000 mm, Lref = 700 mm, which are NOT the maintainer's machine: the firmware's factory values are 0).

  --verify   also builds the firmware's own geometry (the real Calibration.c and Motion.c, through tests/host/geometry_probe.c) and checks that the
             reference depth H and every cable length printed on the picture agree with it to 2.5 micrometres (the firmware works in whole micrometres and floors). Exit status 1 if not.

Examples:  tools/generators/gen_geometry_svg.py --verify
           tools/generators/gen_geometry_svg.py --distance 1250 --lref 900 -o /tmp/mine.svg
"""
import argparse, math, os, re, subprocess, sys, textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
C = dict(L="#1c6dd0", R="#d9480f", draw="#2b8a3e", internal="#7048e8", ref="#c2255c", square="#0b7285", ink="#212529", grey="#868e96", wall="#f1f3f5", note="#f08c00")

def fmt(v, d=3):
    s = ("%." + str(d) + "f") % v
    return s.rstrip("0").rstrip(".") if "." in s else s

def lengths(D, H, x, y):
    """Cable lengths for a drawing-frame point (mm): the firmware's Motion_XYToLR with the Y flip of REQ-CoreLR-D-Motion-0025."""
    return math.hypot(x + D / 2, H - y), math.hypot(x - D / 2, H - y)

class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.o = w, h, []
    def add(self, s): self.o.append(s)
    def line(self, x1, y1, x2, y2, col, sw=1.5, dash=None, marker=None, op=1):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        m = ' marker-end="url(#a_%s)"' % marker if marker else ""
        self.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%s"%s%s opacity="%s"/>' % (x1, y1, x2, y2, col, sw, d, m, op))
    def text(self, x, y, s, col="#212529", size=14, anchor="start", weight="normal", rot=None, halo=True, style=""):
        t = ' transform="rotate(%.1f %.1f %.1f)"' % (rot, x, y) if rot is not None else ""
        h = ' paint-order="stroke" stroke="#fff" stroke-width="4" stroke-linejoin="round"' if halo else ""
        self.add('<text x="%.1f" y="%.1f" fill="%s" font-size="%s" text-anchor="%s" font-weight="%s"%s%s%s>%s</text>' % (x, y, col, size, anchor, weight, h, t, style, s))
    def circle(self, x, y, r, fill="none", stroke="#000", sw=1.5):
        self.add('<circle cx="%.1f" cy="%.1f" r="%s" fill="%s" stroke="%s" stroke-width="%s"/>' % (x, y, r, fill, stroke, sw))
    def rect(self, x, y, w, h, fill="none", stroke="none", sw=1.5, rx=0, dash=None, fop=1):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" fill-opacity="%s" stroke="%s" stroke-width="%s" rx="%s"%s/>' % (x, y, w, h, fill, fop, stroke, sw, rx, d))
    def para(self, x, y, text, width, col="#212529", size=13, weight="normal", lead=None, indent="", halo=False):
        """Wrapped text: `width` px at this font size; returns the y after the last line."""
        lead = lead or size * 1.55
        cols = max(10, int(width / (size * 0.58)))
        for i, l in enumerate(textwrap.wrap(text, cols, subsequent_indent=indent) or [""]):
            self.text(x, y, l, col, size, weight=weight, halo=halo); y += lead
        return y
    def dim(self, x1, y1, x2, y2, col, label, off=0, size=14, sw=1.3, side=1, gap=None, anchor="middle"):
        """A dimension line from (x1,y1) to (x2,y2), moved sideways by `off` px, arrowheads at both ends, label along it."""
        dx, dy = x2 - x1, y2 - y1; n = math.hypot(dx, dy); nx, ny = -dy / n, dx / n
        a, b = (x1 + nx * off, y1 + ny * off), (x2 + nx * off, y2 + ny * off)
        self.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%s" marker-start="url(#s_%s)" marker-end="url(#a_%s)"/>' % (a[0], a[1], b[0], b[1], col, sw, col[1:], col[1:]))
        if off:
            for p, q in (((x1, y1), a), ((x2, y2), b)):
                self.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width=".8" stroke-dasharray="3 3"/>' % (p[0], p[1], q[0], q[1], col))
        ang = math.degrees(math.atan2(dy, dx))
        if ang > 90 or ang < -90: ang += 180
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        gap = gap if gap is not None else size * 0.9
        if label: self.text(mx + nx * side * gap, my + ny * side * gap, label, col, size, anchor, "bold", rot=ang)
    def defs(self):
        cols = sorted(set(C.values()))   # sorted: the output must be the same on every run
        s = "<defs>"
        for c in cols:
            k = c[1:]
            s += '<marker id="a_%s" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0 0L10 5L0 10z" fill="%s"/></marker>' % (k, c)
            s += '<marker id="s_%s" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M10 0L0 5L10 10z" fill="%s"/></marker>' % (k, c)
        return s + "</defs>"
    def render(self, title, desc):
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="DejaVu Sans, Verdana, Arial, sans-serif">\n<title>%s</title>\n<desc>%s</desc>\n%s\n<rect width="%d" height="%d" fill="#fff"/>\n%s\n</svg>\n'
                % (self.w, self.h, self.w, self.h, title, desc, self.defs(), self.w, self.h, "\n".join(self.o)))

def hw():
    """The mechanical constants, read from the firmware's HwConfig.h so that the picture cannot go stale."""
    t = open(os.path.join(HERE, "..", "..", "firmware", "config", "HwConfig.h")).read()
    g = lambda n: int(re.search(r"#define\s+%s\s+(-?\d+)" % n, t).group(1))
    return dict(steps=g("MOTION_STEPS_PER_REV_X1000") / 1000.0, spool=g("MOTION_SPOOL_DIAMETER_UM"), revL=g("STEPPER_MOTOR_L_REVERSED"), revR=g("STEPPER_MOTOR_R_REVERSED"))

def build(D, Lref):
    H = math.sqrt(Lref ** 2 - (D / 2) ** 2)
    h = hw()
    S = Svg(1700, 1330)
    s = S.text; ln = S.line
    s(40, 46, "CoreLR geometry: what to measure, what to enter, what the firmware assumes", C["ink"], 26, weight="bold", halo=False)
    S.para(40, 72, "Example machine D = %s mm, Lref = %s mm: NOT the maintainer's machine (the firmware's factory values are 0, so both must be entered). Every figure on this page "
           "is computed from the formulas and checked against the firmware's own code by tools/generators/gen_geometry_svg.py --verify." % (fmt(D), fmt(Lref)), 1620, C["grey"], 13.5)

    # ------------------------------------------------------------------ panel A: the machine, to scale
    k = 0.78                                           # px per mm
    ax0, ay0 = 130, 262                                # motor L pivot on the page
    Lx, Ly = ax0, ay0; Rx, Ry = ax0 + D * k, ay0
    Ox, Oy = ax0 + D / 2 * k, ay0 + H * k              # reference position = drawing origin
    S.rect(40, 110, 1000, 730, C["wall"], "#ced4da", 1)
    s(52, 134, "A. The machine seen from the front, to scale (%s px per mm); the casings are schematic" % fmt(k, 2), C["ink"], 16, weight="bold", halo=False)
    ln(Lx - 30, Ly, Rx + 30, Ry, C["grey"], 1.2, "6 4")                      # the line of the motors
    s(Ox + 8, Ly - 8, "line of the two exit holes", C["grey"], 12, halo=False)
    ln(Ox, Ly, Ox, Oy + 130, C["grey"], 1, "2 5")                              # the vertical bisector
    ln(Lx, Ly, Ox, Oy, C["L"], 3); ln(Rx, Ry, Ox, Oy, C["R"], 3)               # the cables
    for (x, y, col, name) in ((Lx, Ly, C["L"], "L"), (Rx, Ry, C["R"], "R")):   # the casing of the winder (schematic, NOT to scale) and the fixed exit hole = the pivot
        S.rect(x - 22, y - 16, 44, 32, "#fff", col, 2.5, 3); S.circle(x, y, 4, col, col)
        s(x + (-30 if name == "L" else 30), y + 6, "Motor " + name, col, 18, "end" if name == "L" else "start", "bold")
        s(x, y - 22, "exit hole", col, 11, "middle")
    S.dim(Lx, Ly, Rx, Ry, C["ink"], "D = distance between the two cable exit holes = %s mm   (LCD: \"Motor dist (mm)\")" % fmt(D), off=-62, size=15, side=-1)
    S.dim(Lx, Ly, Ox, Ly, C["grey"], "D/2 = %s" % fmt(D / 2), off=-28, size=12, side=-1)
    S.dim(Ox, Ly, Rx, Ry, C["grey"], "D/2 = %s" % fmt(D / 2), off=-28, size=12, side=-1)
    S.dim(Lx, Ly, Ox, Oy, C["L"], "L = Lref = %s mm" % fmt(Lref), off=34, size=15, side=1)
    S.dim(Rx, Ry, Ox, Oy, C["R"], "R = Lref = %s mm" % fmt(Lref), off=-34, size=15, side=-1)
    s(Ox - 12, Ly + 70, "Lref  (LCD: \"Ref length (mm)\")", C["note"], 13, "end", "bold", halo=True)
    s(Ox - 12, Ly + 87, "tape measure, exit hole to pen, on both cables", C["note"], 12, "end", halo=True)
    hx = Ox + 175                                                              # H
    S.dim(hx, Ly, hx, Oy, C["ref"], None, off=0)
    ln(Ox + 4, Oy, hx + 6, Oy, C["ref"], .8, "3 3")
    hy = Oy - 80
    s(hx + 12, hy - 8, "H = sqrt(Lref^2 - (D/2)^2)", C["ref"], 14, weight="bold"); s(hx + 12, hy + 10, "= %s mm" % fmt(H), C["ref"], 14, weight="bold")
    s(hx + 12, hy + 28, "depth of O below the exit holes", C["ref"], 12)
    sq = 100 * k                                                               # the test square and the drawing frame
    S.rect(Ox - sq, Oy - sq, 2 * sq, 2 * sq, C["square"], C["square"], 1.8, dash="6 3", fop=0.10)
    s(Ox - sq, Oy + sq + 17, "test square 200 x 200 mm", C["square"], 12, "start", "bold")
    ln(Ox, Oy, Ox + 150, Oy, C["draw"], 3, marker=C["draw"][1:]); ln(Ox, Oy, Ox, Oy - 150, C["draw"], 3, marker=C["draw"][1:])
    s(Ox + 156, Oy + 24, "+X (right)", C["draw"], 15, "start", "bold"); s(Ox + 10, Oy - 156, "+Y (UP the wall)", C["draw"], 15, "start", "bold")
    S.circle(Ox, Oy, 7, C["ref"], "#fff", 2)
    s(Ox + 14, Oy + 28, "O = (0, 0)", C["ref"], 15, "start", "bold")
    s(Ox + 14, Oy + 46, "= reference position = drawing origin", C["ref"], 12, "start")
    ln(Lx, Ly, Lx, Ly + 175, C["internal"], 2, "7 4", marker=C["internal"][1:]); ln(Lx, Ly, Lx + 110, Ly, C["internal"], 2, "7 4", marker=C["internal"][1:])   # internal frame
    s(Lx + 8, Ly + 192, "Y (down)", C["internal"], 13, "start", "bold"); s(Lx + 116, Ly + 18, "X (right)", C["internal"], 13, "start", "bold")
    ln(Lx, Oy, Ox, Oy, C["internal"], .9, "2 4")
    s(Lx + 8, Oy - 6, "internal Y of O = H", C["internal"], 12, "start")
    S.rect(52, 740, 440, 88, "#fff", C["internal"], 1.2, 4)
    s(62, 760, "INTERNAL frame (violet), used only by the kinematics:", C["internal"], 12.5, weight="bold", halo=False)
    s(62, 778, "origin at the exit hole of motor L, X to the right, Y DOWN.", C["internal"], 12.5, halo=False)
    s(62, 796, "O is at (D/2, H) = (%s, %s).  Never shown." % (fmt(D / 2), fmt(H)), C["internal"], 12.5, halo=False)
    s(62, 814, "DRAWING frame (green): GCode, LCD, status report.", C["draw"], 12.5, weight="bold", halo=False)

    # ------------------------------------------------------------------ panel B: the test square, zoomed
    bx, by, kb = 1075, 110, 1.75                       # panel origin, px per mm
    S.rect(bx - 15, by, 625, 730, "#fff", "#ced4da", 1)
    s(bx, by + 24, "B. The test square 200 x 200 mm, zoomed (%s px per mm)" % fmt(kb, 2), C["ink"], 16, weight="bold", halo=False)
    cx, cy = bx + 300, by + 400
    S.rect(cx - 100 * kb, cy - 100 * kb, 200 * kb, 200 * kb, C["square"], C["square"], 2, fop=0.08)
    ln(cx - 100 * kb - 25, cy, cx + 100 * kb + 25, cy, C["draw"], 2, marker=C["draw"][1:]); ln(cx, cy + 100 * kb + 25, cx, cy - 100 * kb - 25, C["draw"], 2, marker=C["draw"][1:])
    s(cx + 100 * kb + 28, cy + 5, "+X", C["draw"], 15, "start", "bold"); s(cx + 8, cy - 100 * kb - 30, "+Y", C["draw"], 15, "start", "bold")
    for v in (-100, -50, 50, 100):
        s(cx + v * kb, cy + 16, "%d" % v, C["grey"], 11, "middle"); s(cx - 8, cy - v * kb + 4, "%d" % v, C["grey"], 11, "end")
    S.circle(cx, cy, 5, C["ref"], "#fff", 1.5)
    pts = [(0, 0, "start", 12, 34), (-100, 100, "end", -12, -48), (100, 100, "start", 12, -48), (-100, -100, "end", -12, 22), (100, -100, "start", 12, 22)]
    for (x, y, anchor, dxx, dyy) in pts:
        l, r = lengths(D, H, x, y)
        px, py = cx + x * kb, cy - y * kb
        S.circle(px, py, 4, "#fff", C["square"], 2)
        s(px + dxx, py + dyy, "(%d, %d) mm" % (x, y), C["square"], 13, anchor, "bold")
        s(px + dxx, py + dyy + 16, "L = %s" % fmt(l), C["L"], 13, anchor, "bold"); s(px + dxx, py + dyy + 31, "R = %s" % fmt(r), C["R"], 13, anchor, "bold")
    S.para(bx, by + 690, "L and R: cable lengths in mm at each point, exit hole to pen (formulas in C). Top row: both cables shorter than Lref; bottom row: both longer.", 590, C["grey"], 12)

    # ------------------------------------------------------------------ panel C: formulas and numbers
    cx0, cy0 = 40, 870
    S.rect(cx0, cy0, 800, 440, "#fff", "#ced4da", 1)
    s(cx0 + 12, cy0 + 24, "C. Formulas (drawing frame, mm; the firmware computes in whole micrometres)", C["ink"], 16, weight="bold", halo=False)
    rows = [("Given", "D (between the two cable exit holes, the pivots) and Lref (cable length at the reference position), with Lref > D/2.", C["ink"]),
            ("Depth of O", "H = sqrt(Lref^2 - (D/2)^2) = %s mm. The origin is H BELOW the line of the exit holes." % fmt(H), C["ref"]),
            ("Motors", "L at (-D/2, +H) = (%s, %s);  R at (+D/2, +H) = (%s, %s)  in the drawing frame." % (fmt(-D / 2), fmt(H), fmt(D / 2), fmt(H)), C["ink"]),
            ("Cable lengths", "L = sqrt( (x + D/2)^2 + (H - y)^2 )", C["L"]),
            ("", "R = sqrt( (x - D/2)^2 + (H - y)^2 )", C["R"]),
            ("Inverse", "x = (L^2 - R^2) / (2 D);   y = H - sqrt( L^2 - (x + D/2)^2 )", C["ink"]),
            ("Internal frame", "X = x + D/2,  Y = H - y   (so O is (D/2, H)).", C["internal"]),
            ("Steps", "%s steps per turn, spool %s mm: %s mm per turn = %s um per step." % (fmt(h["steps"], 1), fmt(h["spool"] / 1000.0, 0), fmt(math.pi * h["spool"] / 1000.0), fmt(math.pi * h["spool"] / h["steps"], 1)), C["ink"]),
            ("Direction", "Intended: MORE steps = LONGER cable, on both motors. Software direction flags in HwConfig.h: L reversed = %d, R reversed = %d (flipped 2026-10-10 after the bench)." % (h["revL"], h["revR"]), C["ink"]),
            ("Limit", "A GCode target of 2097.152 mm or more in X or Y is refused (error:33).", C["ink"])]
    yy = cy0 + 54
    for (a, b, col) in rows:
        s(cx0 + 14, yy, a, C["grey"], 13, weight="bold", halo=False)
        yy = S.para(cx0 + 150, yy, b, 640, col, 13, "bold" if col not in (C["ink"],) else "normal", lead=22, indent="  ")
    yy += 6
    yy = S.para(cx0 + 14, yy, "At O: L = R = Lref.  Moving up (+y) shortens both cables, down lengthens both; +x lengthens L and shortens R.", 770, C["grey"], 12.5)
    yy = S.para(cx0 + 14, yy, "The LCD's L,R line shows these two lengths in mm; its X,Y line shows x and y.", 770, C["grey"], 12.5)
    yy += 4
    l1, r1 = lengths(D, H, -100, 100)
    S.para(cx0 + 14, yy, "Example: D = %s, Lref = %s, H = %s; at O L = R = %s; at (-100, +100) L = %s, R = %s." % (fmt(D), fmt(Lref), fmt(H), fmt(Lref), fmt(l1), fmt(r1)), 770, C["ink"], 12.5)

    # ------------------------------------------------------------------ panel D: calibration procedure; E: assumptions
    dx0 = 870
    S.rect(dx0, cy0, 790, 218, "#fff", "#ced4da", 1)
    s(dx0 + 12, cy0 + 24, "D. Calibration on the LCD: main menu > Calibrate machine", C["ink"], 16, weight="bold", halo=False)
    steps = ["1. Measure D with a tape, between the two cable exit holes. Enter it: \"Motor dist (mm)\".",
             "2. Choose Lref > D/2 (for instance 700 mm). Enter it: \"Ref length (mm)\". The firmware then knows H.",
             "3. Adjustment jog: X selects [L] or [R], Y winds or unwinds that cable. Bring the pen to the point where BOTH cables measure Lref (tape, exit hole to pen): "
             "on the vertical through the middle, H under the line of the exit holes.",
             "4. SW = confirm. The firmware declares the pen to be at (0, 0) with L = R = Lref. It cannot check it.",
             "Then \"Calibrate pen\" sets the two servo angles (factory 60 and 95). The position survives power cuts; moving the motors by hand while off requires a new calibration."]
    yy = cy0 + 52
    for t in steps: yy = S.para(dx0 + 14, yy, t, 762, C["ink"], 13, indent="    ", lead=20) + 3
    S.rect(dx0, cy0 + 233, 790, 195, "#fff8e1", C["note"], 1.5)
    s(dx0 + 12, cy0 + 257, "E. What the model assumes", C["note"], 16, weight="bold", halo=False)
    ass = ["Each motor is a POINT: the cable leaves it through a FIXED hole in the casing of the winder (confirmed by the maintainer, 2026-10-10), so the exit hole is the pivot and does not move with the cable's direction. "
           "L and R are the straight lengths from that hole to the pen.",
           "The pen is the point where both cables meet (no offset between the cable attachment and the pen tip).",
           "The steps wind a constant %s um of cable per step (the layers building up on the spool are ignored)." % fmt(math.pi * h["spool"] / h["steps"], 1),
           "O is where you calibrate, not necessarily the centre of the paper. Nothing in the firmware measures the wall, the cables or the pen: D, Lref and step 3 are the maintainer's measurements."]
    yy = cy0 + 282
    for t in ass: yy = S.para(dx0 + 14, yy, "- " + t, 762, C["ink"], 12.5, indent="  ", lead=19) + 3
    return S, H

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--distance", type=float, default=1000.0, help="D, the distance between the two motors, mm (default 1000: an example)")
    ap.add_argument("--lref", type=float, default=700.0, help="Lref, the cable length at the reference position, mm (default 700: an example)")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "..", "Doc", "images", "Geometry.svg"), help="output file (default docs/hardware/images/Geometry.svg)")
    ap.add_argument("--verify", action="store_true", help="check H and the cable lengths against the firmware's real geometry (builds tests/host/geometry_probe.c)")
    a = ap.parse_args()
    if a.lref <= a.distance / 2: sys.exit("gen_geometry_svg.py: Lref must exceed D/2 (the cables could not meet)")
    S, H = build(a.distance, a.lref)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "w").write(S.render("CoreLR geometry", "Dimensions, coordinate frames, calibration and cable lengths of the CoreLR wall plotter, D = %s mm, Lref = %s mm" % (fmt(a.distance), fmt(a.lref))))
    print("wrote %s  (D=%s, Lref=%s, H=%s)" % (a.out, fmt(a.distance), fmt(a.lref), fmt(H)))
    if a.verify:
        fw = os.path.join(HERE, "..", "..", "firmware", "src"); cfg = os.path.join(HERE, "..", "..", "firmware", "config"); ht = os.path.join(HERE, "..", "..", "tests", "host"); exe = os.path.join(ht, "out", "geometry_probe"); os.makedirs(os.path.join(ht, "out"), exist_ok=True)
        r = subprocess.run(["gcc", "-m32", "-fpack-struct=1", "-O1", "-w", "-I" + os.path.join(ht, "shim"), "-I" + fw, "-I" + cfg, os.path.join(ht, "geometry_probe.c"),
                            os.path.join(fw, "Calibration.c"), os.path.join(fw, "Motion.c"), os.path.join(fw, "Trig.c"), "-lm", "-o", exe], capture_output=True, text=True)
        if r.returncode: sys.exit("gen_geometry_svg.py: the probe did not build:\n" + r.stderr[:800])
        pts = [(0, 0), (-100, 100), (100, 100), (-100, -100), (100, -100), (60, 40), (-250, -300), (0, 120), (300, -50)]
        p = subprocess.run([exe, fmt(a.distance, 6), fmt(a.lref, 6)], input="".join("%d %d\n" % (x * 1000, y * 1000) for x, y in pts), capture_output=True, text=True).stdout.splitlines()
        bad = 0; got = {}
        for l in p:
            if l.startswith("ref"): got["refY"] = int(l.split("Y=")[1])
            if l.startswith("pt"): f = l.split(); got[(int(f[1]), int(f[2]))] = (int(f[3][2:]), int(f[4][2:]))
        dH = abs(got["refY"] - H * 1000)
        print("  H: picture %.3f mm, firmware %.3f mm (difference %.2f um)" % (H, got["refY"] / 1000.0, dH)); bad += dH > 2.5
        for (x, y) in pts:
            l, r = lengths(a.distance, H, x, y); fl, fr = got[(x * 1000, y * 1000)]
            d = max(abs(fl - l * 1000), abs(fr - r * 1000)); bad += d > 2.5
            print("  (%5d,%5d)  picture L %.3f R %.3f   firmware L %.3f R %.3f   difference %.2f um" % (x, y, l, r, fl / 1000.0, fr / 1000.0, d))
        print("  firmware and picture %s" % ("DISAGREE" if bad else "agree to 2.5 um (the firmware floors H and each length to a micrometre)"))
        sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
