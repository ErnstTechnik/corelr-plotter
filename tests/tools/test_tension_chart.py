#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""test_tension_chart.py -- is tools/generators/gen_tension_chart.py right? The statics against an independent solution of the force balance, the symmetries,
the values worked out by hand, the grid, and the text, SVG and CSV outputs. Prints 'all checks passed' or the failing case; exit status = the number of failures.
Proved able to fail by tests/mutation/mutants/tension_chart.txt (tools/mutate.py --script tests/tools/test_tension_chart.py)."""
import importlib.util, math, os, random, subprocess, sys, tempfile, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "generators", "gen_tension_chart.py")
if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__); sys.exit(0)
spec = importlib.util.spec_from_file_location("gtc", TOOL)
gtc = importlib.util.module_from_spec(spec); spec.loader.exec_module(gtc)

bad = 0
def check(ok, what):
    global bad
    if not ok:
        bad += 1; print("FAIL:", what)

def near(a, b, tol=1e-9): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))

# --- 1. against an independent solution: the two cable directions as unit vectors, the 2x2 force balance solved by Cramer's rule
def balance(d, x, y):
    L, R = math.hypot(x, y), math.hypot(d - x, y)
    ul = (-x / L, -y / L)               # from the pen towards the left motor (y is down: the motors are above)
    ur = ((d - x) / R, -y / R)          # towards the right motor
    # T_L * ul + T_R * ur + (0, +1) = 0  (the weight pulls down, +y)
    det = ul[0] * ur[1] - ur[0] * ul[1]
    tl = (0 * ur[1] - ur[0] * (-1.0)) / det
    tr = (ul[0] * (-1.0) - 0 * ul[1]) / det
    return tl, tr
rnd = random.Random(7)
for _ in range(300):
    d = rnd.uniform(300, 3000); x = rnd.uniform(0.02 * d, 0.98 * d); y = rnd.uniform(0.02 * d, 2 * d)
    a = gtc.tensions(d, x, y); b = balance(d, x, y)
    check(near(a[0], b[0], 1e-9) and near(a[1], b[1], 1e-9), "tensions(%g, %g, %g) = %s, the force balance gives %s" % (d, x, y, a, b))

# --- 2. values worked out by hand
tl, tr = gtc.tensions(1000, 500, 20)
check(near(tl, tr) and abs(tl - 12.50999) < 1e-4, "D = 1000 mm, mid-span, 20 mm below the line: %g x W each (expected 12.510)" % tl)
tl, tr = gtc.tensions(1000, 500, 50)
check(abs(tl - 5.02494) < 1e-4, "mid-span, 50 mm: %g (expected 5.025)" % tl)
tl, tr = gtc.tensions(1000, 500, 500)
check(abs(tl - math.sqrt(0.5)) < 1e-12 and abs(tr - math.sqrt(0.5)) < 1e-12, "mid-span, 500 mm below (45 degrees): %g (expected 0.7071)" % tl)
tl, tr = gtc.tensions(1000, 20, 50)
check(abs(tl - 1.0546) < 1e-3 and abs(tr - 0.3925) < 1e-3, "at the side margin, 50 mm: T_L %g (1.055), T_R %g (0.3925)" % (tl, tr))
tl, tr = gtc.tensions(1000, 0, 100)
check(near(tr, 0.0) and near(tl, math.hypot(0, 100) * 1000 / (1000 * 100)), "on the left motor's vertical the right cable carries nothing: T_R = %g, T_L = %g (the weight)" % (tr, tl))
tl, tr = gtc.tensions(1000, -50, 100)
check(tr < 0, "beyond the left motor's vertical the right cable would have to PUSH: T_R = %g" % tr)
tl, tr = gtc.tensions(1000, 1050, 100)
check(tl < 0, "beyond the right motor's vertical the left cable would have to push: T_L = %g" % tl)

# --- 3. symmetry and scale invariance, monotonicity
for _ in range(100):
    d = rnd.uniform(300, 3000); x = rnd.uniform(0.05 * d, 0.95 * d); y = rnd.uniform(0.02 * d, 2 * d); k = rnd.uniform(0.3, 4)
    a = gtc.tensions(d, x, y); b = gtc.tensions(d, d - x, y); c = gtc.tensions(k * d, k * x, k * y)
    check(near(a[0], b[1]) and near(a[1], b[0]), "mirror symmetry broken at (%g, %g)" % (x, y))
    check(near(a[0], c[0]) and near(a[1], c[1]), "scaling the whole geometry by %g changed the tensions (they depend on the shape only)" % k)
prev = None
for y in range(20, 1000, 10):
    t = max(gtc.tensions(1000, 500, y))
    check(prev is None or t < prev, "mid-span tension does not fall as the pen goes down (y = %d)" % y)
    prev = t
# vertical equilibrium, horizontal balance, written out
for _ in range(100):
    d = 1000.0; x = rnd.uniform(20, 980); y = rnd.uniform(50, 900)
    tl, tr = gtc.tensions(d, x, y); L, R = math.hypot(x, y), math.hypot(d - x, y)
    check(near(tl * y / L + tr * y / R, 1.0, 1e-9), "the vertical components do not carry exactly one weight at (%g, %g)" % (x, y))
    check(near(tl * x / L, tr * (d - x) / R, 1e-9), "the horizontal components do not balance at (%g, %g)" % (x, y))

# --- 4. the worst case on the top edge
t, x, name = gtc.worst_on_top_edge(1000, 20, 20)
check(abs(t - 12.50999) < 2e-3 and abs(x - 500) <= 5, "worst on the 20 mm line: %g at x = %g (expected 12.51 near the middle)" % (t, x))
t, x, name = gtc.worst_on_top_edge(1000, 20, 50)
check(abs(t - 5.025) < 5e-3 and name == "L" and abs(x - 495) <= 5, "worst on the 50 mm line: %g, cable %s at x = %g (expected 5.025, L, near 495: left of mid-span the left cable pulls harder)" % (t, name, x))
t0 = gtc.worst_on_top_edge(1000, 20, 10)[0]; t1 = gtc.worst_on_top_edge(1000, 20, 100)[0]
check(t0 > t1 * 5, "a 10 mm margin should be far worse than a 100 mm one (%g vs %g)" % (t0, t1))
# a lopsided case: the worst is NOT mid-span when the margin is large against the span
t, x, name = gtc.worst_on_top_edge(400, 20, 300)
check(0 < x < 400 and t > 0, "worst on a short span: %g at %g" % (t, x))

# --- 5. the grid covers the zone exactly, edges included
xs, ys, rows = gtc.grid(1000, 20, 50, 1000, 10)
check(abs(xs[0] - 20) < 1e-9 and abs(xs[-1] - 980) < 1e-9, "the grid's X range is %g .. %g, expected 20 .. 980" % (xs[0], xs[-1]))
check(abs(ys[0] - 50) < 1e-9 and abs(ys[-1] - 1000) < 1e-9, "the grid's Y range is %g .. %g, expected 50 .. 1000" % (ys[0], ys[-1]))
check(len(rows) == len(ys) and all(len(r) == len(xs) for r in rows), "the grid is not rectangular")
c = rows[0][len(xs) // 2]
check(abs(c[2] - gtc.tensions(1000, c[0], c[1])[0]) < 1e-12 and abs(c[3] - gtc.tensions(1000, c[0], c[1])[1]) < 1e-12, "a grid cell does not hold the tensions of its own point")
check(len(xs) == 97 and len(ys) == 96, "10 mm grid of a 1000 mm zone: %d x %d points, expected 97 x 96" % (len(xs), len(ys)))

# --- 6. the colour scale
check(gtc.colour(0.5) == "#313695" and gtc.colour(20) == "#a50026" and gtc.colour(0.1) == gtc.colour(0.5) and gtc.colour(1000) == gtc.colour(20), "the colour scale's ends or its clamping are wrong")
check(gtc.colour(1) != gtc.colour(2) != gtc.colour(5), "the colour scale does not distinguish 1, 2 and 5")
check(gtc.colour(math.sqrt(0.5 * 20.0)) == "#a2bebb", "the geometric middle of the scale (3.16) is %s, expected #a2bebb (a third of the way from stop 0.45 to 0.60: the scale is logarithmic)" % gtc.colour(math.sqrt(10.0)))

# --- 7. the program: text, svg, csv, refusal of a geometry with no zone
def run(*args):
    return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True)
with tempfile.TemporaryDirectory() as t:
    svg, csv = os.path.join(t, "x.svg"), os.path.join(t, "x.csv")
    r = run("-D", "1000", "--svg", svg, "--csv", csv, "--mass-g", "15")
    check(r.returncode == 0, "a normal run failed: %s" % r.stderr)
    check("At the top edge, mid-span: 5.02 x W per cable" in r.stdout, "the text lacks the 5.02 x W headline: %s" % r.stdout[:300])
    check("<- the firmware's margin" in r.stdout and "12.51" in r.stdout, "the margin abaque lacks the firmware's line or the 20 mm figure")
    check("In newtons (15.0 g): top edge mid-span 0.74 N" in r.stdout, "the newtons line is missing or wrong: %s" % r.stdout[-200:])
    try:
        root = ET.parse(svg).getroot()
        ns = "{http://www.w3.org/2000/svg}"
        rects = root.findall(".//" + ns + "rect")
        check(root.tag == ns + "svg" and len(rects) > 1000 and len(root.findall(".//" + ns + "polyline")) == 3, "the SVG has %d rects and %d curves" % (len(rects), len(root.findall(".//" + ns + "polyline"))))
    except ET.ParseError as e:
        check(False, "the SVG is not well-formed XML: %s" % e)
    lines = open(csv).read().splitlines()
    check(lines[0] == "x_mm,y_mm,T_L_xW,T_R_xW" and len(lines) == 97 * 96 + 1, "the CSV has %d lines, expected %d" % (len(lines), 97 * 96 + 1))
    f = lines[1].split(","); check(abs(float(f[0]) - 20) < 1e-6 and abs(float(f[1]) - 50) < 1e-6 and abs(float(f[2]) - gtc.tensions(1000, 20, 50)[0]) < 1e-5, "the CSV's first row is wrong: %s" % lines[1])
r = run("-D", "30", "--side", "20", "--top", "5", "--height", "100", "--quiet"); check(r.returncode == 2 and "no zone" in r.stderr, "a distance under twice the side margin must be refused (exit %d)" % r.returncode)
r = run("-D", "40", "--side", "20", "--top", "5", "--height", "100", "--quiet"); check(r.returncode == 2, "a distance of exactly twice the side margin leaves a zone of zero width: refused (exit %d)" % r.returncode)
r = run("-D", "1000", "--top", "2000", "--quiet"); check(r.returncode == 2, "a top margin below the chart's height must be refused")
r = run("-D", "1000", "--top", "0", "--quiet"); check(r.returncode == 2, "a zero top margin (infinite tension) must be refused")
r = run("-D", "1000", "--quiet"); check(r.returncode == 0 and r.stdout == "", "--quiet must print nothing")
r = run("-h"); check(r.returncode == 0 and "Usage" in r.stdout, "-h must print the usage")

print("%d check(s) failed" % bad if bad else "all checks passed")
sys.exit(bad)
