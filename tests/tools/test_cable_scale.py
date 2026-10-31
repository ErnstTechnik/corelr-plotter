#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""test_cable_scale.py -- tests of tools/analysis/cable_scale.py (synthetic tape readings, no machine).

Usage:   test_cable_scale.py [-h]
Exit status: 0 if all pass, 1 otherwise.
"""
import os, sys
if "-h" in sys.argv or "--help" in sys.argv:
    print(__doc__); sys.exit(0)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "analysis"))
import cable_scale as c

fails = 0
def check(name, ok, info=""):
    global fails
    if not ok:
        fails += 1; print("FAIL %s %s" % (name, info))

D, LREF = 1065.0, 800.0
def readings(kl, kr, off=3.0, pts=((0, 0), (0, -300), (100, -600))):
    base = c.lengths(D, LREF, 0, 0)
    out = []
    for x, y in pts:
        l, r = c.lengths(D, LREF, x, y)
        out.append([x, y, base[0] + off + kl * (l - base[0]), base[1] - off + kr * (r - base[1])])
    return out

res = c.analyse(D, LREF, readings(1.012, 0.995))
check("k L", abs(res["L"]["k"] - 1.012) < 1e-9, res["L"]["k"])
check("k R", abs(res["R"]["k"] - 0.995) < 1e-9, res["R"]["k"])
check("residual zero", max(abs(v) for v in res["L"]["resid"]) < 1e-9)
check("spool", abs(res["L"]["spool"] - 35.42) < 1e-6, res["L"]["spool"])

# a scale that grows with the length (cable building up in layers) leaves residuals
pts = readings(1.0, 1.0)
pts[2][2] += 4.0
check("layers show in residuals", max(abs(v) for v in c.analyse(D, LREF, pts)["L"]["resid"]) > 1.0)

# the gear ratio alone: k = 2048 / 2037.886 gives a wire thickness of zero
g = 2048.0 / c.GEAR_STEPS      # the firmware used to assume 2048 steps a turn
check("gear explains it", abs(c.analyse(D, LREF, readings(g, g), steps=2048.0)["L"]["wire"]) < 1e-9)
check("corrected firmware: gear factor is 1", abs(c.analyse(D, LREF, readings(1, 1))["L"]["gear"] - 1.0) < 1e-6)

for name, args in (("one point", readings(1, 1)[:1]), ("same point twice", [[0, 0, 800, 800], [0, 0, 800, 800]])):
    try:
        c.analyse(D, LREF, args); check(name + " refused", False)
    except ValueError:
        pass
try:
    c.analyse(500.0, 200.0, readings(1, 1)); check("Lref <= D/2 refused", False)
except ValueError:
    pass
check("cli ok", c.main(["--D", "1065", "--Lref", "800", "--point", "0,0,800,800", "--point", "0,-300,1010,1011"]) == 0)
check("cli bad", c.main(["--D", "1065"]) == 2)
print("ok" if not fails else "%d failed" % fails)
sys.exit(1 if fails else 0)
