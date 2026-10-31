#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""cable_scale.py -- the scale between motor steps and real cable length, from tape-measure readings.

Usage:   cable_scale.py --D MM --Lref MM --point X,Y,L,R --point X,Y,L,R [--point ...] [--spool MM] [--steps N]

The firmware winds a constant  pi * spool / steps  of cable per step (53.96 um for 35 mm and 2037.886 steps; until 2026-10-26 it assumed
2048, which was 0.496 % too many: pass --steps 2048 to read readings taken before). The real cable still differs: it winds on the spool's
diameter PLUS its own thickness. Both are a pure scale on the cable length, which this tool measures.

Each --point is a place the pen was driven to (X,Y in mm, drawing frame, +Y up, the calibration reference point is 0,0) and the
two cable lengths L,R in mm read with a tape measure from each motor's exit hole to the pen. The FIRST point is the reference:
only DIFFERENCES from it are used, so an offset in the way you read the tape (hole to pen tip) cancels.

For each motor it prints what the firmware's model predicts, what was measured, the scale k = measured / predicted change of
length (least squares through the first point), the residuals (a growing residual = the scale changes with the length, i.e. the
cable builds up in layers), the spool diameter that would give k = 1, and the part of k that the gear ratio explains.

Options:
  --D MM       distance between the two exit holes (the calibration's D)
  --Lref MM    the calibration's reference cable length
  --spool MM   spool diameter the firmware uses (default 35)
  --steps N    steps per turn the firmware uses (default 2037.886, HwConfig.h)
  -h, --help   this text

Example:
  cable_scale.py --D 1065 --Lref 800 --point 0,0,800,800 --point 0,-300,1046,1046 --point 0,-600,1316,1316
  (the firmware predicts 1043.2 and 1310.1 mm there; a reading of 1046 and 1316 is k = 1.012)

Exit status: 0 done, 2 bad arguments.
"""
import argparse, math, sys

GEAR_STEPS = 32 * 63.68395   # full steps of the 28BYJ-48 output shaft per turn, as measured by users of the motor


def lengths(d, lref, x, y):
    """The firmware's cable lengths (mm) of drawing point (x, y): Geometry.svg, REQ-CoreLR-F-Calibration-0008."""
    h = math.sqrt(lref * lref - d * d / 4.0)
    return (math.hypot(x + d / 2.0, h - y), math.hypot(x - d / 2.0, h - y))


def analyse(d, lref, points, spool=35.0, steps=2037.886):
    """Returns {'L': {...}, 'R': {...}} -- k, residuals (mm), new spool diameter (mm), the gear factor."""
    if len(points) < 2:
        raise ValueError("at least two points are needed (the reference and one more)")
    if lref <= d / 2.0:
        raise ValueError("Lref must exceed D/2")
    pred = [lengths(d, lref, p[0], p[1]) for p in points]
    gear = steps / GEAR_STEPS
    out = {}
    for i, name in enumerate("LR"):
        dp = [pred[j][i] - pred[0][i] for j in range(1, len(points))]
        dm = [points[j][2 + i] - points[0][2 + i] for j in range(1, len(points))]
        den = sum(a * a for a in dp)
        if den == 0.0:
            raise ValueError("the points do not change the length of cable %s" % name)
        k = sum(a * b for a, b in zip(dp, dm)) / den
        out[name] = {
            "pred": [pred[j][i] for j in range(len(points))],
            "dpred": dp, "dmeas": dm, "k": k,
            "resid": [b - k * a for a, b in zip(dp, dm)],
            "spool": spool * k, "gear": gear,
            "wire": spool * (k / gear - 1.0),
        }
    return out


def main(argv):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--D", type=float)
    ap.add_argument("--Lref", type=float)
    ap.add_argument("--spool", type=float, default=35.0)
    ap.add_argument("--steps", type=float, default=2037.886)
    ap.add_argument("--point", action="append", default=[])
    try:
        a = ap.parse_args(argv)
    except SystemExit:
        return 2
    if a.help:
        print(__doc__); return 0
    try:
        if a.D is None or a.Lref is None:
            raise ValueError("--D and --Lref are required")
        pts = []
        for s in a.point:
            v = [float(t) for t in s.split(",")]
            if len(v) != 4:
                raise ValueError("--point wants X,Y,L,R, got %r" % s)
            pts.append(v)
        res = analyse(a.D, a.Lref, pts, a.spool, a.steps)
    except ValueError as e:
        print("cable_scale: %s (try -h)" % e, file=sys.stderr); return 2
    for name in "LR":
        r = res[name]
        print("Motor %s" % name)
        print("   point   predicted L (mm)   change predicted   change measured   residual")
        for j in range(len(pts)):
            if j == 0:
                print("   %d       %8.2f           (reference)" % (j, r["pred"][j]))
            else:
                print("   %d       %8.2f           %8.2f          %8.2f         %+6.2f" %
                      (j, r["pred"][j], r["dpred"][j - 1], r["dmeas"][j - 1], r["resid"][j - 1]))
        print("   k = %.5f  (%+.2f %%); above 1 the real cable is longer than the firmware thinks, below 1 shorter" % (r["k"], (r["k"] - 1) * 100))
        print("   spool diameter giving k = 1: %.3f mm (MOTION_SPOOL_DIAMETER_UM %d)" % (r["spool"], round(r["spool"] * 1000)))
        print("   gear ratio alone explains %.5f; the rest, k / gear = %.5f, is a spool diameter %+.3f mm larger (the cable's thickness)"
              % (r["gear"], r["k"] / r["gear"], r["wire"]))
    kl, kr = res["L"]["k"], res["R"]["k"]
    print("k(L) / k(R) = %.5f  (the two spools should agree within the tape's accuracy)" % (kl / kr))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
