#!/usr/bin/env python3
#
# gen_tension_chart.py
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
"""gen_tension_chart.py -- how hard does each cable pull? Charts of the tension of the two cables, in multiples of the pen's weight, for a geometry.

Why it exists: the pen hangs from two cables that can only PULL. Near the line joining the motors the cables are almost horizontal and their tension grows
without bound (REQ-CoreLR-F-Motion-0008); this shows how fast, so that the top margin (MOTION_MARGIN_TOP_UM in SwConfig.h) can be chosen knowing what it costs.

The statics (frame: origin at the left motor, x towards the right motor, y DOWN, D the distance between the motors, W the weight of the pen assembly; the pen
at (x, y); L and R the two cable lengths, L = hypot(x, y), R = hypot(D - x, y)):
    horizontal:  T_L * x / L  =  T_R * (D - x) / R
    vertical:    T_L * y / L  +  T_R * y / R  =  W
which solve to      T_L = W * L * (D - x) / (D * y)         T_R = W * R * x / (D * y)
At mid-span (x = D/2) both are W * L / (2 y) = W / (2 sin(angle of a cable below the horizontal)). The tensions depend on the ratio of the geometry only, and are
proportional to W: the charts give them in multiples of W ("x W"); --mass-g turns them into newtons for a pen assembly of that mass. Cable weight, friction at the
exit holes and the motors' holding torque are not modelled: this is the static load the pen puts on the cables.

Usage:   gen_tension_chart.py [-h] [-D MM] [--side MM] [--top MM] [--height MM] [--step MM] [--mass-g G] [--svg FILE] [--csv FILE] [--quiet]
Output:  text on stdout (the tables); --svg draws the maps and the curves; --csv writes the grid (x, y, T_L, T_R in x W) for any other tool.
Exit status: 0 = written, 2 = bad arguments (a geometry with no zone in it).
Examples:
  tools/generators/gen_tension_chart.py -D 1000 --svg /tmp/tension.svg          the 1 m machine, margins 20 / 50 mm (the firmware's defaults)
  tools/generators/gen_tension_chart.py -D 1200 --top 80 --mass-g 15 --height 800
"""
import argparse, math, os, sys


def tensions(d, x, y):
    """(T_L, T_R) in multiples of the pen's weight, pen at (x, y) below the left motor's level, motors d apart. Needs 0 < y; x may be anywhere (outside [0, d] one
    of them goes negative: the cable would have to push)."""
    left = math.hypot(x, y)
    right = math.hypot(d - x, y)
    return left * (d - x) / (d * y), right * x / (d * y)


def worst_on_top_edge(d, side, y, steps=200):
    """The larger cable tension anywhere on the line `y` below the motors, between the side margins, and where (x, which cable)."""
    best = (-1.0, 0.0, "L")
    for i in range(steps + 1):
        x = side + (d - 2 * side) * i / steps
        tl, tr = tensions(d, x, y)
        for t, name in ((tl, "L"), (tr, "R")):
            if t > best[0]:
                best = (t, x, name)
    return best


def grid(d, side, top, height, step):
    """[(x, y, T_L, T_R)] over the travel zone down to `height` below the motors, `step` mm apart (the zone's own edges are always included)."""
    xs = []
    n = max(1, int(round((d - 2 * side) / step)))
    for i in range(n + 1):
        xs.append(side + (d - 2 * side) * i / n)
    ys = []
    m = max(1, int(round((height - top) / step)))
    for j in range(m + 1):
        ys.append(top + (height - top) * j / m)
    return xs, ys, [[(x, y) + tensions(d, x, y) for x in xs] for y in ys]


# ------------------------------------------------------------------ text
def text_report(a):
    d = a.distance
    out = []
    w = out.append
    w("Cable tension, in multiples of the pen's weight (x W)%s" % ("; W = %.3f N for a %.1f g pen assembly" % (a.mass_g * 9.80665 / 1000, a.mass_g) if a.mass_g else ""))
    w("Geometry: motors %.0f mm apart; travel zone: %.0f mm from each motor's vertical, %.0f mm below the line joining them, down to %.0f mm" % (d, a.side, a.top, a.height))
    w("")
    tl, tr = tensions(d, d / 2, a.top)
    w("At the top edge, mid-span: %.2f x W per cable (the two are equal)" % tl)
    t, x, name = worst_on_top_edge(d, a.side, a.top)
    w("Worst on the top edge: %.2f x W, cable %s, at x = %.0f mm from the left motor" % (t, name, x))
    xs, ys, rows = grid(d, a.side, a.top, a.height, a.step)
    worst = max(((max(c[2], c[3]), c[0], c[1]) for r in rows for c in r))
    w("Worst in the whole zone: %.2f x W at x = %.0f mm, y = %.0f mm" % worst)
    w("")
    w("Abaque 1 -- the larger of the two tensions (x W) as the pen goes down, for a few positions across the span")
    cols = [("x = %.0f (edge)" % a.side, a.side), ("x = D/8", d / 8), ("x = D/4", d / 4), ("x = D/2", d / 2)]
    w("  %10s" % "y (mm)" + "".join("%16s" % c[0] for c in cols))
    ys_tab = [y for y in (10, 20, 30, 40, 50, 60, 80, 100, 150, 200, 300, 400, 600, 800, 1000) if y >= a.top and y <= a.height]
    for y in ys_tab:
        w("  %10.0f" % y + "".join("%16.2f" % max(tensions(d, c[1], y)) for c in cols))
    w("")
    w("Abaque 2 -- the top margin: the worst tension (x W) on the line `margin` below the motors, between the side margins")
    w("  %10s %12s %10s %14s" % ("margin (mm)", "worst x W", "cable", "at x (mm)"))
    for m in (10, 20, 30, 40, 50, 60, 80, 100, 150, 200):
        t, x, name = worst_on_top_edge(d, a.side, m)
        w("  %10.0f %12.2f %10s %14.0f%s" % (m, t, name, x, "   <- the firmware's margin" if abs(m - a.top) < 1e-9 else ""))
    if a.mass_g:
        w("")
        w("In newtons (%.1f g): top edge mid-span %.2f N per cable; worst on the top edge %.2f N" % (a.mass_g, tl * a.mass_g * 9.80665 / 1000, worst_on_top_edge(d, a.side, a.top)[0] * a.mass_g * 9.80665 / 1000))
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------ svg
STOPS = [(0.00, (49, 54, 149)), (0.25, (69, 117, 180)), (0.45, (116, 173, 209)), (0.60, (254, 224, 144)), (0.80, (244, 109, 67)), (1.00, (165, 0, 38))]


def colour(t, lo=0.5, hi=20.0):
    """A colour for a tension (x W) on a log scale between lo and hi."""
    f = (math.log(max(t, lo)) - math.log(lo)) / (math.log(hi) - math.log(lo))
    f = min(max(f, 0.0), 1.0)
    for (f0, c0), (f1, c1) in zip(STOPS, STOPS[1:]):
        if f <= f1:
            u = (f - f0) / (f1 - f0)
            return "#%02x%02x%02x" % tuple(int(round(c0[k] + (c1[k] - c0[k]) * u)) for k in range(3))
    return "#%02x%02x%02x" % STOPS[-1][1]


def svg_report(a):
    d = a.distance
    xs, ys, rows = grid(d, a.side, a.top, a.height, a.svg_step)
    pw = 380.0
    ph = pw * (a.height - a.top + 40) / (d + 40)                 # same scale on both axes: the picture is the wall
    sx = pw / (d + 40)
    mx, my = 50.0, 90.0
    o = []
    W = 3 * (pw + 90) + 60
    H = ph + 130
    o.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %.0f %.0f" width="%.0f" height="%.0f" font-family="DejaVu Sans, Verdana, Arial, sans-serif" font-size="11">' % (W, H, W, H))
    o.append('<rect width="%.0f" height="%.0f" fill="#fff"/>' % (W, H))
    o.append('<text x="20" y="26" font-size="16" font-weight="bold">CoreLR -- cable tension, in multiples of the pen\'s weight</text>')
    o.append('<text x="20" y="46">motors %.0f mm apart; zone: %.0f mm from each motor\'s vertical, %.0f mm below the line joining them. Tension T_L = W L (D-x) / (D y), T_R = W R x / (D y).%s</text>'
             % (d, a.side, a.top, " W = %.3f N (%.1f g)." % (a.mass_g * 9.80665 / 1000, a.mass_g) if a.mass_g else ""))

    def panel(px, which, title):
        o.append('<text x="%.1f" y="%.1f" font-weight="bold">%s</text>' % (px, my - 14, title))
        for j, y in enumerate(ys[:-1]):
            for i, x in enumerate(xs[:-1]):
                c = rows[j][i]
                cx = (rows[j][i][2 + which] + rows[j][i + 1][2 + which] + rows[j + 1][i][2 + which] + rows[j + 1][i + 1][2 + which]) / 4
                o.append('<rect shape-rendering="crispEdges" x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="%s"/>' % (
                    px + (x + 20) * sx, my + (y - a.top + 20) * sx, (xs[i + 1] - x) * sx + 0.3, (ys[j + 1] - y) * sx + 0.3, colour(cx)))
        # the motors and the line joining them, the zone's outline
        o.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="none" stroke="#000" stroke-width="1"/>' % (
            px + (a.side + 20) * sx, my + 20 * sx, (d - 2 * a.side) * sx, (a.height - a.top) * sx))
        o.append('<text x="%.1f" y="%.1f" text-anchor="middle">motor L</text><text x="%.1f" y="%.1f" text-anchor="middle">motor R</text>' % (
            px + 20 * sx, my - 2, px + (d + 20) * sx, my - 2))
        for k in range(0, int((a.height) // 100) + 1):
            yy = k * 100.0
            if a.top <= yy <= a.height:
                o.append('<text x="%.1f" y="%.1f" text-anchor="end" fill="#444">%d</text>' % (px - 4, my + (yy - a.top + 20) * sx + 4, yy))
        for k in range(0, int(d // 200) + 1):
            xx = k * 200.0
            o.append('<text x="%.1f" y="%.1f" text-anchor="middle" fill="#444">%d</text>' % (px + (xx + 20) * sx, my + (a.height - a.top + 20) * sx + 16, xx))

    panel(mx, 0, "left cable T_L (x W)")
    panel(mx + pw + 90, 1, "right cable T_R (x W)")
    # colour bar
    bx = mx + 2 * (pw + 90) - 10
    o.append('<text x="%.1f" y="%.1f" font-weight="bold">scale (log)</text>' % (bx, my - 14))
    for k in range(60):
        f = k / 59.0
        t = 0.5 * (20.0 / 0.5) ** f
        o.append('<rect x="%.1f" y="%.2f" width="18" height="%.2f" fill="%s"/>' % (bx, my + 20 + (1 - f) * 180, 180 / 60.0 + 0.3, colour(t)))
    for t in (0.5, 1, 2, 5, 10, 20):
        f = (math.log(t) - math.log(0.5)) / (math.log(20.0) - math.log(0.5))
        o.append('<text x="%.1f" y="%.2f">%g x W</text>' % (bx + 24, my + 20 + (1 - f) * 180 + 4, t))
    # the curves: the larger tension against y, for a few x
    cx0 = mx + 2 * (pw + 90) + 130
    cw, ch = pw - 130, 220.0
    o.append('<text x="%.1f" y="%.1f" font-weight="bold">larger tension vs depth y</text>' % (cx0, my - 14))
    ymax = a.height
    def px_(y): return cx0 + 36 + (y - a.top) / (ymax - a.top) * cw
    def py_(t): return my + 20 + ch - (math.log(max(t, 0.5)) - math.log(0.5)) / (math.log(40.0) - math.log(0.5)) * ch
    o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="#999"/>' % (px_(a.top), py_(40.0), cw, ch))
    for t in (1, 2, 5, 10, 20, 40):
        o.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ddd"/><text x="%.1f" y="%.1f" text-anchor="end">%g</text>' % (px_(a.top), py_(t), px_(ymax), py_(t), px_(a.top) - 3, py_(t) + 4, t))
    for yy in (100, 200, 400, 600, 800, 1000):
        if a.top <= yy <= ymax:
            o.append('<text x="%.1f" y="%.1f" text-anchor="middle" fill="#444">%d</text>' % (px_(yy), py_(0.5) + 14, yy))
    for name, xpos, col in (("x = D/2", d / 2, "#1a9850"), ("x = D/4", d / 4, "#4575b4"), ("x = %.0f (edge)" % a.side, a.side, "#d73027")):
        pts = []
        for k in range(61):
            y = a.top + (ymax - a.top) * k / 60.0
            pts.append("%.1f,%.1f" % (px_(y), py_(max(tensions(d, xpos, y)))))
        o.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="1.6"/>' % (" ".join(pts), col))
    for i, (name, col) in enumerate((("x = D/2", "#1a9850"), ("x = D/4", "#4575b4"), ("x = %.0f (edge)" % a.side, "#d73027"))):
        o.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="2"/><text x="%.1f" y="%.1f">%s</text>' % (cx0 + 40, my + ch + 70 + 14 * i, cx0 + 64, my + ch + 70 + 14 * i, col, cx0 + 70, my + ch + 74 + 14 * i, name))
    o.append('<text x="%.1f" y="%.1f" text-anchor="middle">y, mm below the line joining the motors</text>' % (px_(a.top) + cw / 2, my + ch + 54))
    o.append("</svg>")
    return "\n".join(o) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-D", "--distance", type=float, default=1000.0, metavar="MM", help="distance between the two motors' cable exit points, mm (default 1000)")
    ap.add_argument("--side", type=float, default=20.0, metavar="MM", help="side margin from each motor's vertical, mm (default 20: MOTION_MARGIN_SIDE_UM)")
    ap.add_argument("--top", type=float, default=50.0, metavar="MM", help="top margin below the line joining the motors, mm (default 50: MOTION_MARGIN_TOP_UM)")
    ap.add_argument("--height", type=float, default=None, metavar="MM", help="how far down to chart, mm (default: the motor distance)")
    ap.add_argument("--step", type=float, default=10.0, metavar="MM", help="grid step of the maps and of the worst-case search, mm (default 10)")
    ap.add_argument("--svg-step", type=float, default=None, metavar="MM", help="grid step of the SVG maps (default: twice --step)")
    ap.add_argument("--mass-g", type=float, default=0.0, metavar="G", help="mass of the pen assembly in grams, to add newtons to the report (default: not given)")
    ap.add_argument("--svg", metavar="FILE", help="write the maps and the curves as an SVG")
    ap.add_argument("--csv", metavar="FILE", help="write the grid as CSV: x_mm, y_mm, T_L, T_R (x W)")
    ap.add_argument("--quiet", action="store_true", help="do not print the tables")
    a = ap.parse_args(argv)
    if a.height is None:
        a.height = a.distance
    if a.svg_step is None:
        a.svg_step = 2 * a.step
    if a.distance <= 0 or a.side < 0 or a.top <= 0 or a.step <= 0 or a.svg_step <= 0 or a.distance <= 2 * a.side or a.height <= a.top:
        print("gen_tension_chart.py: no zone: the distance must exceed twice the side margin, and the height the top margin", file=sys.stderr)
        return 2
    if not a.quiet:
        sys.stdout.write(text_report(a))
    if a.csv:
        xs, ys, rows = grid(a.distance, a.side, a.top, a.height, a.step)
        with open(a.csv, "w") as f:
            f.write("x_mm,y_mm,T_L_xW,T_R_xW\n")
            for r in rows:
                for c in r:
                    f.write("%.3f,%.3f,%.6f,%.6f\n" % c)
    if a.svg:
        os.makedirs(os.path.dirname(os.path.abspath(a.svg)), exist_ok=True)
        with open(a.svg, "w") as f:
            f.write(svg_report(a))
        print("wrote %s" % a.svg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
