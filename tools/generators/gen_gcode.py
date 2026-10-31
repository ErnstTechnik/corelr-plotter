#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""gen_gcode.py -- writes the GCode files to print on the machine (tests/bench/gcode/*.gc).

Every file starts with a return to the origin with the pen up (`M5`, then `G90 G0 X0 Y0`: absolute mode first, since a serial session may have
left the machine in `G91`) and ends the same way (then `M30`). All lines are at most 47
characters before comment stripping (the SD variant's line buffer is 48), so the files run from the SD card and over the serial link alike, and
everything stays inside a square of 200 x 200 mm centred on the origin (drawing frame: X to the right, Y UP the wall). The dialect is exactly the
firmware's: G0 G1 G2 G3 (I/J), G17 G21 G54 G90 G91 G94, M3 (pen down) M5 (pen up) M30, F S N words ignored, ; and ( ) comments.

  01_Commands.gc     every command once, in short labelled sections, to see how the machine reacts (inside the square)
  02_Square_200.gc   a square 200 x 200 mm, centre on the origin
  03_Circle_R100.gc  a circle R = 100 mm, centre on the origin (two half circles)
  05_Chords.gc       four circles R = 30 mm cut into G1 chords of 0.2, 0.5, 1 and 2 mm (about 1 600 lines): the look-ahead on short segments, time each circle
  04_TestPattern.gc  a test card in the 200 x 200 mm square: border, regular grid (20 mm), diagonals, concentric circles, small circles, centre cross

Check them with tools/bench/gcode_check.sh, which runs them through the firmware's real parser and planner.

THE TRAVEL ZONE (REQ-CoreLR-F-Motion-0008): the pen is never taken within a side margin (20 mm) of a motor's vertical nor within a top margin (50 mm) of the line joining the motors,
so a drawing that fills the machine depends on ITS geometry. With --limits D,OX,OY, or --limits D,L (mm: the distance between the motors, the distance from the left motor's vertical to the drawing
origin, how far below the line joining the motors the origin is -- what "Calibrate machine" was told, and where the origin was put) this also writes, in the same directory:

  10_Limits_Frame.gc          a big drawing that FILLS the zone, `inset` mm inside its edges: the frame, both diagonals, the inscribed circle, and three big arcs whose centres lie OUTSIDE the zone
  11_Refuse_Left.gc           pen up: a move to 1 mm inside the left limit (accepted), then one 1 mm beyond it: the machine must REFUSE it (error 33): the job halts, `E` on the screen
  12_Refuse_Right.gc, 13_Refuse_Top.gc   the same at the right limit and at the top margin
  14_Refuse_ArcBulge.gc       pen up: a half circle UNDER its chord (accepted), then the half circle OVER it, whose ends are inside but whose top is 10 mm beyond the top margin (refused)
Run them in this order; after a refusal acknowledge with SW (or Ctrl-X from Candle) and bring the pen back with the jog or "Return home". The files are specific to the geometry they were
generated for: wrong numbers mean a line the machine accepts that the file called refused, or the reverse -- the check with gcode_check.sh --geometry D,OX,OY tells which.
"""
import argparse, math, os, sys

def num(v):
    """A coordinate in mm with at most 3 decimals (1 um, the firmware's unit), no trailing zeros."""
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s

class File:
    def __init__(self, title, lines_about):
        self.out = []
        for l in title: self.comment(l)
        self.out.append("M5")                     # pen up first,
        self.out.append("G90 G0 X0 Y0")           # then back to the origin, in absolute mode whatever the machine was left in
        self.out.append("G21 G17 G54 G94")        # millimetres, XY plane, the only coordinate system, feed per minute: accepted, no effect
    def comment(self, text):
        assert len(text) + 2 <= 47, text
        self.out.append("; " + text)
    def add(self, line):
        assert len(line) <= 47, line
        self.out.append(line)
    def move(self, x, y): self.add("G0 X%s Y%s" % (num(x), num(y)))
    def line(self, x, y): self.add("G1 X%s Y%s" % (num(x), num(y)))
    def pen(self, down): self.add("M3" if down else "M5")
    def arc(self, ccw, x, y, i, j): self.add("G%d X%s Y%s I%s J%s" % (3 if ccw else 2, num(x), num(y), num(i), num(j)))
    def circle(self, cx, cy, r):
        """A circle of radius r about (cx, cy) as two half circles, counter-clockwise, from its rightmost point; pen is lifted to get there."""
        self.pen(False); self.move(cx + r, cy); self.pen(True)
        self.arc(True, cx - r, cy, -r, 0); self.arc(True, cx + r, cy, r, 0)
        self.pen(False)
    def finish(self):
        self.pen(False); self.add("G0 X0 Y0"); self.add("M30")
        return "\n".join(self.out) + "\n"

def commands():
    f = File(["CoreLR command test: watch the machine react", "Stays inside 200 x 200 mm, centre = origin.",
              "Start: pen up, back to the origin."], None)
    f.comment("-- 1. Pen up/down: listen to the servo --")
    f.add("M3"); f.add("M5"); f.add("M3"); f.add("M5")
    f.comment("-- 2. Axes, pen up (+Y = up the wall) --")
    for x, y in ((50, 0), (-50, 0), (0, 0), (0, 50), (0, -50), (0, 0)): f.move(x, y)
    f.comment("-- 3. Diagonals, pen up, to each corner --")
    for x, y in ((100, 100), (-100, 100), (-100, -100), (100, -100), (0, 0)): f.move(x, y)
    f.comment("-- 4. A plus sign, pen down (G1) --")
    f.move(30, 0); f.pen(True)
    for x, y in ((-30, 0), (0, 0), (0, 30), (0, -30), (0, 0)): f.line(x, y)
    f.pen(False)
    f.comment("-- 5. Relative mode: a 20 mm square --")
    f.move(-90, -20); f.pen(True); f.add("G91")
    f.add("G1 X20"); f.add("G1 Y20"); f.add("G1 X-20"); f.add("G1 Y-20")
    f.add("G90"); f.pen(False)
    f.comment("-- 6. Modal words, no spaces, lower case --")
    f.move(-80, 60); f.pen(True)
    f.add("G1 X-60 Y80"); f.add("X-40 Y60"); f.add("x-20 y80"); f.add("g1x0y60")
    f.add("N50 G1 X20 Y80 F1500 S0")
    f.pen(False)
    f.comment("-- 7. Comments of both kinds --")
    f.move(40, 60)
    f.add("M3 (pen down) ; and a comment")
    f.add("G1 X60 (inline) Y80 ; tail")
    f.add("G1 X80 Y60"); f.add(""); f.add("G1 X100 Y80"); f.pen(False)
    f.comment("-- 8. G2 clockwise circle, R=25 --")
    f.move(-75, -60); f.pen(True)
    f.arc(False, -25, -60, 25, 0)         # left -> right about (-50,-60), clockwise: passes ABOVE the centre
    f.arc(False, -75, -60, -25, 0)        # right -> left, clockwise: passes BELOW
    f.pen(False)
    f.comment("-- 9. G3 counter-clockwise circle --")
    f.move(25, -60); f.pen(True)
    f.arc(True, 75, -60, 25, 0)           # left -> right about (50,-60), counter-clockwise: passes BELOW the centre
    f.arc(True, 25, -60, -25, 0)          # right -> left, counter-clockwise: passes ABOVE
    f.pen(False)
    f.comment("-- 10. Full circles in one line --")
    f.move(10, -60); f.pen(True)
    f.arc(True, 10, -60, -10, 0)          # G3, R=10 about (0,-60)
    f.pen(False)
    f.move(10, -90); f.pen(True)
    f.arc(False, 10, -90, -10, 0)         # G2, R=10 about (0,-90)
    f.pen(False)
    f.comment("-- 11. Back to the origin, program end --")
    return f.finish()

def square(half=100):
    f = File(["Square 200 x 200 mm, centre = origin"], None)
    f.move(-half, -half); f.pen(True)
    for x, y in ((half, -half), (half, half), (-half, half), (-half, -half)): f.line(x, y)
    return f.finish()

def circle(r=100):
    f = File(["Circle R=100 mm, centre = origin"], None)
    f.circle(0, 0, r)
    return f.finish()

def chords(r=30, centres=((-50, 50, 0.2), (50, 50, 0.5), (-50, -50, 1.0), (50, -50, 2.0))):
    """Four circles R = 30 mm drawn as polylines of G1 chords of 0.2, 0.5, 1 and 2 mm: the look-ahead's test (REQ-CoreLR-D-Motion-0013). The
    firmware plans 8 blocks ahead (serial; 4 on the SD variant), the horizon it needs to hold VMAX is 1.2 mm: 6 chords of 0.2 mm, 3 of 0.5 mm, 2 of 1 mm.
    The chord is exact only up to the rounding of n = 2*pi*r/chord to a whole number."""
    f = File(["Circles R=30 as chords: 0.2 0.5 1 2 mm", "top left 0.2, top right 0.5, bottom left 1,", "bottom right 2 mm: time each (mm/s)"], None)
    for cx, cy, chord in centres:
        n = int(round(2 * math.pi * r / chord))
        f.comment("chord %g mm, %d lines" % (chord, n))
        f.pen(False); f.move(cx + r, cy); f.pen(True)
        for k in range(1, n + 1):
            a = 2 * math.pi * k / n
            f.line(cx + r * math.cos(a), cy + r * math.sin(a))
        f.pen(False)
    return f.finish()

def pattern():
    f = File(["Test card, 200 x 200 mm, centre = origin", "border, 20 mm grid, diagonals, circles, cross"], None)
    h = 100
    f.comment("border")
    f.move(-h, -h); f.pen(True)
    for x, y in ((h, -h), (h, h), (-h, h), (-h, -h)): f.line(x, y)
    f.pen(False)
    f.comment("grid every 20 mm: vertical lines")
    for k in range(-4, 5):
        if k == 0: continue                               # the axes are drawn with the cross below
        x = 20 * k
        f.move(x, -h); f.pen(True); f.line(x, h); f.pen(False)
    f.comment("grid every 20 mm: horizontal lines")
    for k in range(-4, 5):
        if k == 0: continue
        y = 20 * k
        f.move(-h, y); f.pen(True); f.line(h, y); f.pen(False)
    f.comment("axes through the centre")
    f.move(-h, 0); f.pen(True); f.line(h, 0); f.pen(False)
    f.move(0, -h); f.pen(True); f.line(0, h); f.pen(False)
    f.comment("diagonals corner to corner")
    f.move(-h, -h); f.pen(True); f.line(h, h); f.pen(False)
    f.move(-h, h); f.pen(True); f.line(h, -h); f.pen(False)
    f.comment("concentric circles R=20,40,60,80,100")
    for r in (20, 40, 60, 80, 100): f.circle(0, 0, r)
    f.comment("a small circle R=10 in each corner cell")
    for cx in (-70, 70):
        for cy in (-70, 70): f.circle(cx, cy, 10)
    f.comment("centre mark R=5")
    f.circle(0, 0, 5)
    return f.finish()

def reference_position(d, lref):
    """The drawing origin that "Calibrate machine" leaves (the default of REQ-CoreLR-F-Calibration-0004): the intersection of two arcs of the reference cable length Lref
    about the motors, D apart: OX = D/2 and OY = sqrt(Lref^2 - (D/2)^2) below the line joining them, in the firmware's own integer micrometres (floors), returned in mm."""
    h = int(round(d * 1000)) // 2
    l = int(round(lref * 1000))
    if l <= h: raise SystemExit("gen_gcode.py: a cable of %g mm cannot reach the middle of %g mm between the motors" % (lref, d))
    return h / 1000.0, math.isqrt(l * l - h * h) / 1000.0

def parse_geometry(text):
    """`D,OX,OY` (mm), or `D,L`: the motor distance and the reference cable length of the calibration, the origin being where calibration leaves it."""
    try: v = [float(x) for x in text.split(",")]
    except ValueError: v = []
    if len(v) == 3: return v[0], v[1], v[2]
    if len(v) == 2:
        ox, oy = reference_position(v[0], v[1])
        return v[0], ox, oy
    raise SystemExit("gen_gcode.py: --limits D,OX,OY or D,L (mm): motor distance, then either the origin's place or the calibration's cable length")

class Zone:
    """The travel zone of a geometry, in the drawing frame (X right, Y UP), in mm: a rectangle down to `height` below its top edge, `inset` mm inside the real limits."""
    def __init__(self, d, ox, oy, side=20.0, top=50.0, inset=5.0, height=600.0):
        self.d, self.ox, self.oy, self.side, self.top, self.inset, self.height = d, ox, oy, side, top, inset, height
        self.edge_l, self.edge_r, self.edge_t = side - ox, d - side - ox, oy - top        # the limits themselves
        self.xl, self.xr, self.yt = self.edge_l + inset, self.edge_r - inset, self.edge_t - inset
        self.yb = self.yt - height
        self.w, self.h = self.xr - self.xl, height
        if self.w <= 0 or height <= 0: raise SystemExit("gen_gcode.py: no zone for this geometry (distance %g, margins %g/%g, inset %g)" % (d, side, top, inset))

def limits_frame(z):
    f = File(["Fills the travel zone, inset %g mm" % z.inset, "X %.1f .. %.1f  Y %.1f .. %.1f mm" % (z.xl, z.xr, z.yb, z.yt),
              "geometry D=%g OX=%g OY=%g" % (z.d, z.ox, z.oy), "Origin where it was calibrated."], None)
    f.comment("frame")
    f.move(z.xl, z.yb); f.pen(True)
    for x, y in ((z.xr, z.yb), (z.xr, z.yt), (z.xl, z.yt), (z.xl, z.yb)): f.line(x, y)
    f.pen(False)
    f.comment("diagonals")
    f.move(z.xl, z.yb); f.pen(True); f.line(z.xr, z.yt); f.pen(False)
    f.move(z.xl, z.yt); f.pen(True); f.line(z.xr, z.yb); f.pen(False)
    cx, cy, r = (z.xl + z.xr) / 2, (z.yb + z.yt) / 2, min(z.w, z.h) / 2
    f.comment("the inscribed circle, R=%.1f" % r)
    f.circle(cx, cy, r)
    # three arcs whose centres are OUTSIDE the zone: along the top edge (centre above it), along the left and right edges (centres beyond them)
    f.comment("big arc, top edge, centre above the zone")
    f.move(z.xl, z.yt); f.pen(True)
    sg = 0.12 * z.h
    L = z.w; k = (L * L / 4 - sg * sg) / (2 * sg)
    if k + sg >= 2000: raise SystemExit("gen_gcode.py: the top arc's radius %.0f mm is beyond the machine's reach (2097 mm)" % (k + sg))
    f.arc(True, z.xr, z.yt, z.w / 2, k)                                                 # CCW under a centre (mid, yt + k): from the left end down and round to the right end
    f.pen(False)
    f.comment("big arc, left edge, centre beyond it")
    sg = 0.10 * z.w; L = z.h; k = (L * L / 4 - sg * sg) / (2 * sg)
    if k + sg >= 2000: raise SystemExit("gen_gcode.py: the left arc's radius %.0f mm is beyond the machine's reach" % (k + sg))
    f.move(z.xl, z.yt); f.pen(True)
    f.arc(False, z.xl, z.yb, -k, -z.h / 2)                                              # CW: centre (xl - k, mid), the arc bulges to the right (inside)
    f.pen(False)
    f.comment("big arc, right edge, centre beyond it")
    f.move(z.xr, z.yb); f.pen(True)
    f.arc(False, z.xr, z.yt, k, z.h / 2)                                                # CW: centre (xr + k, mid), the arc bulges to the left (inside)
    f.pen(False)
    f.comment("corner marks, 10 mm arms, inward")
    for x, y in ((z.xl, z.yb), (z.xl, z.yt), (z.xr, z.yb), (z.xr, z.yt)):
        dx = 10 if x == z.xl else -10; dy = 10 if y == z.yb else -10
        f.move(x, y); f.pen(True); f.line(x + dx, y); f.pen(False)
        f.move(x, y); f.pen(True); f.line(x, y + dy); f.pen(False)
    return f.finish()

def refuse(z, kind):
    t = {"left": "left limit X=%.3f" % z.edge_l, "right": "right limit X=%.3f" % z.edge_r, "top": "top margin Y=%.3f" % z.edge_t, "bulge": "top margin, by an arc's bulge"}[kind]
    f = File(["REFUSAL TEST: %s" % t, "Pen up throughout. The marked line must be", "REFUSED: the job halts (E), nothing moves.", "Then SW to acknowledge, jog or Return home."], None)
    ym = z.yt - 0.5 * z.h                                                               # a height well inside, for the side tests
    if kind == "left":
        f.comment("1 mm inside the left limit: accepted"); f.move(z.edge_l + 1, ym)
        f.comment("1 mm beyond it: REFUSED (error 33)"); f.line(z.edge_l - 1, ym)
    elif kind == "right":
        f.comment("1 mm inside the right limit: accepted"); f.move(z.edge_r - 1, ym)
        f.comment("1 mm beyond it: REFUSED (error 33)"); f.line(z.edge_r + 1, ym)
    elif kind == "top":
        f.comment("1 mm inside the top margin: accepted"); f.move(0 if z.xl < 0 < z.xr else (z.xl + z.xr) / 2, z.edge_t - 1)
        f.comment("1 mm beyond it: REFUSED (error 33)"); f.line(0 if z.xl < 0 < z.xr else (z.xl + z.xr) / 2, z.edge_t + 1)
    else:
        r = min(40.0, z.w / 4); cx = (z.xl + z.xr) / 2
        yc = z.edge_t - 30; top_of_arc = yc + r                                         # the chord 30 mm inside the margin: the top of a 40 mm half circle is 10 mm beyond it
        f.comment("the half circle UNDER its chord: accepted"); f.move(cx - r, yc); f.arc(True, cx + r, yc, r, 0)
        f.comment("half circle OVER it: ends inside, top Y=%.1f" % top_of_arc)
        f.comment("%.0f mm beyond the margin: REFUSED" % (top_of_arc - z.edge_t))
        f.move(cx - r, yc); f.arc(False, cx + r, yc, r, 0)
    return f.finish()

FILES = (("01_Commands.gc", commands), ("02_Square_200.gc", square), ("03_Circle_R100.gc", circle), ("04_TestPattern.gc", pattern), ("05_Chords.gc", chords))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("-o", "--out", default=os.path.join(here, "..", "..", "tests", "bench", "gcode"), help="directory to write (default: tests/bench/gcode)")
    ap.add_argument("--limits", metavar="D,OX,OY | D,L", help="also write the travel-zone files for this geometry, mm: motor distance, origin from the left motor, origin below the line joining the motors; OR the motor distance and the reference cable length of the calibration (the origin is then where calibration leaves it: OX = D/2, OY = sqrt(L^2 - (D/2)^2))")
    ap.add_argument("--only-limits", action="store_true", help="with --limits: write only the zone files")
    ap.add_argument("--side", type=float, default=20.0, help="the side margin, mm (MOTION_MARGIN_SIDE_UM; default 20)")
    ap.add_argument("--top", type=float, default=50.0, help="the top margin, mm (MOTION_MARGIN_TOP_UM; default 50)")
    ap.add_argument("--inset", type=float, default=5.0, help="how far inside the limits the big drawing stays, mm (default 5)")
    ap.add_argument("--height", type=float, default=600.0, help="how far down the big drawing goes, mm (default 600: take what the wall and the cable length allow)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    files = [] if (a.limits and a.only_limits) else list(FILES)
    if a.limits:
        d, ox, oy = parse_geometry(a.limits)
        if oy - a.top <= 0: sys.exit("gen_gcode.py: the origin is %g mm below the line joining the motors, less than the top margin (%g): \"Calibrate machine\" would refuse this geometry" % (oy, a.top))
        z = Zone(d, ox, oy, a.side, a.top, a.inset, a.height)
        files += [("10_Limits_Frame.gc", lambda: limits_frame(z))] + [("%d_Refuse_%s.gc" % (11 + i, k.capitalize() if k != "bulge" else "ArcBulge"), (lambda k=k: refuse(z, k))) for i, k in enumerate(("left", "right", "top", "bulge"))]
    for name, fn in files:
        text = fn()
        open(os.path.join(a.out, name), "w", newline="\n").write(text)
        print("%-24s %4d lines" % (name, text.count("\n")))

if __name__ == "__main__":
    main()
