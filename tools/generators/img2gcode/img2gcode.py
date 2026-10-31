#!/usr/bin/env python3
#
# img2gcode.py
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
"""img2gcode.py -- turns a picture into a GCode file for the CoreLR wall plotter, and shows what it did.

  .venv/bin/python img2gcode.py IMAGE [-s STYLE] [--paper a4] [--pen 2] [--geometry D,OX,OY|D,L] [--debug] [-o OUT]
  .venv\\Scripts\\python img2gcode.py IMAGE ...        (Windows)

THE FRAME.  The bottom-left corner of the sheet is X0 Y0 of the GCode (X to the right, Y UP, millimetres): put the drawing origin of the machine there
(calibrate it with the joystick or `G92`).  The picture is scaled to fit the sheet inside --margin, keeping its ratio, and centred.  The sheet is
portrait by default (--landscape turns it); --paper takes A0..A6 or WIDTHxHEIGHT in mm.

THE STYLES.  The same black-on-white picture, three ways to draw it with one pen:
  skeleton  the centre line of every stroke (skimage thinning), short spurs removed, branches rejoined where they meet.  One line per stroke whatever
            its width: the right choice for line art, text and sketches.  A stroke thicker than the pen is drawn as a thin line.
  outline   the edge of every black area (potrace's curves).  A thin stroke gives two lines (one each side).  Looks like the picture; costs more time.
  hatch     the black areas filled with parallel lines (--hatch-spacing apart, at --hatch-angle), zig-zagged where neighbours meet.
  all       the three, in OUT/NAME/skeleton, /outline, /hatch, plus OUT/NAME/compare.png side by side.

THE PATHS.  Strokes are joined where they touch, put in an order that shortens the pen-up travel (nearest next end, either direction, closed shapes
entered at their nearest point), consecutive strokes closer than --bridge are drawn through without lifting the pen, and each stroke is fitted by
lines and ARCS (G2/G3 with I,J) within --tol of the picture.  Every line is at most 47 characters (the SD variant's buffer).

WHAT IT WRITES in OUT/NAME/STYLE/: NAME.gc, NAME.svg (the path, arcs kept as arcs), NAME.png (the ink at the pen's width, pen-up travel in red),
NAME.md (the report: counts, lengths, estimated time), NAME.json (the same numbers), and with --debug a debug/ folder with every intermediate image.
The estimated time follows the firmware's own planner constants (read from firmware/config/SwConfig.h when found): top speed, acceleration, the
junction speed limit, the pen settle time.  It assumes an unlimited look-ahead and the same speed for G0 and G1, so it is a floor, not a promise.

THE TRAVEL ZONE.  With --geometry the points of the path are checked against the zone the firmware allows (REQ-CoreLR-F-Motion-0008; --side and --top
are its margins).  A drawing that leaves it is NOT written (the machine would stop at that line with error 33): the report and the preview still are,
with the zone drawn, so you can see where.  Without --geometry nothing is checked.  --geometry D,OX,OY = motor distance, then the origin's distance
from the left motor's vertical and below the line joining the motors; D,L = motor distance and the calibration's reference cable length (the origin
is then OX = D/2, OY = sqrt(L^2 - (D/2)^2)).  All in mm.

Examples:
  img2gcode.py samples/logo.png -s skeleton --geometry 1065,800 --debug
  img2gcode.py samples/text.png -s all --paper a3 --landscape --pen 0.7
Exit status: 0 = written; 1 = a problem with the input; 3 = the drawing leaves the travel zone (no .gc written).
"""
import argparse, json, math, os, re, sys
import numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SWCONFIG = os.path.normpath(os.path.join(HERE, "..", "..", "..", "firmware", "config", "SwConfig.h"))
PAPERS = {"a0": (841, 1189), "a1": (594, 841), "a2": (420, 594), "a3": (297, 420), "a4": (210, 297), "a5": (148, 210), "a6": (105, 148)}
STYLES = ("skeleton", "outline", "hatch")
ARC_RMAX_MM = 1500.0      # the firmware refuses an arc whose radius or centre offset reaches 2^21 um = 2.097 m (error 34, MOTION_ARC_REACH_SHIFT); flatter than this is a line anyway

# ---------------------------------------------------------------------------------------------------------------- firmware constants
def firmware_constants():
    """The planner's numbers, from SwConfig.h when it is there (never assumed), else the values of 2026-10-12. Speeds in mm/s."""
    k = {"MOTION_VMAX_V16": 86, "MOTION_VMIN_V16": 16, "MOTION_JUNCTION_JERK_V16": 32, "MOTION_ACCEL_MM_S2": 192, "PENCONTROL_SETTLE_TIME_MS": 300}
    src = "built-in defaults (SwConfig.h not found)"
    try:
        txt = open(SWCONFIG, encoding="utf-8", errors="replace").read(); src = "firmware/config/SwConfig.h"
        for n in k:
            m = re.search(r"#define\s+%s\s+(\d+)" % n, txt)
            if m: k[n] = int(m.group(1))
    except OSError: pass
    return {"vmax": k["MOTION_VMAX_V16"] / 4.0, "vmin": k["MOTION_VMIN_V16"] / 4.0, "jerk": k["MOTION_JUNCTION_JERK_V16"] / 4.0,
            "accel": float(k["MOTION_ACCEL_MM_S2"]), "settle": k["PENCONTROL_SETTLE_TIME_MS"] / 1000.0, "source": src}

# ---------------------------------------------------------------------------------------------------------------- geometry of the machine
def parse_geometry(s):
    try: v = [float(x) for x in s.split(",")]
    except ValueError: v = []
    if len(v) == 3: return v[0], v[1], v[2]
    if len(v) == 2:
        d, l = v; h = int(round(d * 500)); ll = int(round(l * 1000))                     # the firmware's integer um, floors
        if ll <= h: sys.exit("img2gcode.py: cable length %g is not longer than half the motor distance %g" % (l, d / 2))
        return d, h / 1000.0, math.isqrt(ll * ll - h * h) / 1000.0
    sys.exit("img2gcode.py: --geometry D,OX,OY or D,L (mm)")

class Zone:
    """The travel zone in the sheet frame (X right, Y up, origin at the sheet's bottom-left = the drawing origin)."""
    def __init__(self, d, ox, oy, side, top):
        self.xl, self.xr, self.yt = side - ox, d - side - ox, oy - top
        if self.xr <= self.xl or self.yt <= 0: sys.exit("img2gcode.py: no travel zone for this geometry (the origin must be more than %g mm below the motors)" % top)
    def outside(self, pts, slack=1e-6):
        p = np.asarray(pts); bad = (p[:, 0] < self.xl - slack) | (p[:, 0] > self.xr + slack) | (p[:, 1] > self.yt + slack)
        return p[bad]

# ---------------------------------------------------------------------------------------------------------------- stages' images (--debug)
class Debug:
    def __init__(self, folder, on): self.dir, self.on, self.n = folder, on, 0
    def save(self, name, img):
        if not self.on: return
        os.makedirs(self.dir, exist_ok=True); self.n += 1
        cv2.imwrite(os.path.join(self.dir, "%02d_%s.png" % (self.n, name)), img)

def mmscale(pts, s, H):
    """sheet mm (Y up) -> pixel (Y down) at s px/mm for a sheet of height H."""
    p = np.asarray(pts, float); return np.c_[p[:, 0] * s, (H - p[:, 1]) * s]

def draw_polys(shape, polys, s, H, colour_fn=None, thick=1, base=None):
    img = base.copy() if base is not None else np.full(shape + (3,), 255, np.uint8)
    for k, pl in enumerate(polys):
        c = colour_fn(k, len(polys)) if colour_fn else (30, 30, 30)
        cv2.polylines(img, [np.round(mmscale(pl, s, H) * 16).astype(np.int32).reshape(-1, 1, 2)], False, c, thick, cv2.LINE_AA, shift=4)
    return img

def rainbow(k, n):
    h = int(170 * k / max(1, n - 1)); px = cv2.cvtColor(np.uint8([[[h, 230, 200]]]), cv2.COLOR_HSV2BGR)[0, 0]
    return int(px[0]), int(px[1]), int(px[2])

def rand_colour(k, n):
    r = np.random.default_rng(k * 7 + 3); return tuple(int(v) for v in r.integers(40, 220, 3))

# ---------------------------------------------------------------------------------------------------------------- input -> binary picture
class Sheet:
    """The picture on the sheet: ppmm pixels per mm; the picture's pixel (col,row) is at mm (x0 + col/ppmm, H - (y0 + row/ppmm))."""
    def __init__(self, W, H, ppmm, x0, y0, ink): self.W, self.H, self.ppmm, self.x0, self.y0, self.ink = W, H, ppmm, x0, y0, ink
    def to_mm(self, rc):                       # rc: (N,2) of (row, col) in pixel CORNER coordinates (centre of pixel (r,c) is r+.5, c+.5)
        a = np.asarray(rc, float); return np.c_[self.x0 + a[:, 1] / self.ppmm, self.H - (self.y0 + a[:, 0] / self.ppmm)]

def load_ink(path, W, H, margin, pen, threshold, invert, speckle_mm2, dbg):
    try: im = Image.open(path)
    except Exception as e: sys.exit("img2gcode.py: cannot read %s: %s" % (path, e))
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA"); bg = Image.new("RGBA", im.size, (255, 255, 255, 255)); im = Image.alpha_composite(bg, im)
    g = np.array(im.convert("L")); dbg.save("gray", g)
    aw, ah = W - 2 * margin, H - 2 * margin
    if aw <= 0 or ah <= 0: sys.exit("img2gcode.py: the margin leaves no room on the sheet")
    native = min(g.shape[1] / aw, g.shape[0] / ah); ppmm = min(native, 6.0)             # px/mm: the picture's own, capped at 6 (0.17 mm)
    w_mm, h_mm = g.shape[1] / native, g.shape[0] / native
    nw, nh = max(1, int(round(w_mm * ppmm))), max(1, int(round(h_mm * ppmm)))
    g = cv2.resize(g, (nw, nh), interpolation=cv2.INTER_AREA if ppmm < native else cv2.INTER_CUBIC)
    if threshold is None: thr, _ = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU); thr = int(thr)
    else: thr = int(threshold)
    ink = (g >= thr) if invert else (g < thr)
    frac = ink.mean()
    if frac > 0.5: print("img2gcode.py: warning: %.0f%% of the picture is ink -- white on black? try --invert" % (100 * frac), file=sys.stderr)
    min_px = int(speckle_mm2 * ppmm * ppmm)
    if min_px > 1:                                                                       # drop specks of dust
        n, lab, st, _ = cv2.connectedComponentsWithStats(ink.astype(np.uint8), connectivity=8)
        keep = np.zeros(n, bool); keep[1:] = st[1:, cv2.CC_STAT_AREA] >= min_px; ink = keep[lab]
    dbg.save("work_grey_%dx%d_px_at_%.2f_per_mm" % (nw, nh, ppmm), g)
    dbg.save("ink_threshold_%d" % thr, np.where(ink, 0, 255).astype(np.uint8))
    return Sheet(W, H, ppmm, margin + (aw - w_mm) / 2, margin + (ah - h_mm) / 2, ink), thr

# ---------------------------------------------------------------------------------------------------------------- stroke makers
class Stroke:
    def __init__(self, pts, closed=False): self.pts, self.closed = np.asarray(pts, float), closed
    def length(self):
        p = self.pts; l = float(np.hypot(*np.diff(p, axis=0).T).sum()) if len(p) > 1 else 0.0
        return l + (float(np.hypot(*(p[0] - p[-1]))) if self.closed else 0.0)

NB8 = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]

def trace_skeleton(sk):
    """Pixel paths of a thin skeleton: [(list of (r,c), degree of the first pixel, degree of the last, closed)]. Isolated pixels come back as 1-pixel paths."""
    ys, xs = np.nonzero(sk); pix = set(zip(ys.tolist(), xs.tolist()))
    def nbs(p): return [(p[0] + dy, p[1] + dx) for dy, dx in NB8 if (p[0] + dy, p[1] + dx) in pix]
    changed = True                                                                       # drop the redundant corner pixels of a staircase
    while changed:
        changed = False
        for p in list(pix):
            if p not in pix: continue
            n = nbs(p)
            if len(n) == 2 and max(abs(n[0][0] - n[1][0]), abs(n[0][1] - n[1][1])) == 1: pix.discard(p); changed = True
    nb = {p: nbs(p) for p in pix}
    out, done_edges, seen = [], set(), set()
    def walk(a, b):
        path, prev, cur = [a, b], a, b; done_edges.add((a, b)); done_edges.add((b, a)); seen.update((a, b))
        while len(nb[cur]) == 2:
            n0, n1 = nb[cur]; nxt = n1 if n0 == prev else n0
            if (cur, nxt) in done_edges: break
            done_edges.add((cur, nxt)); done_edges.add((nxt, cur)); path.append(nxt); seen.add(nxt); prev, cur = cur, nxt
            if cur == a: break
        return path
    for p in pix:
        if len(nb[p]) == 2: continue
        if not nb[p]: out.append(([p], 0, 0, False)); seen.add(p); continue
        for q in nb[p]:
            if (p, q) not in done_edges:
                path = walk(p, q); out.append((path, len(nb[path[0]]), len(nb[path[-1]]), path[0] == path[-1]))
    for p in pix:                                                                        # what is left: closed loops without a junction
        if p in seen or len(nb[p]) != 2: continue
        path = walk(p, nb[p][0]); out.append((path, 2, 2, True))
    return out

def smooth(pts, win, closed):
    if win < 3 or len(pts) < 5: return pts
    win = win | 1; h = win // 2; p = np.asarray(pts)
    if closed: q = np.r_[p[-h:], p, p[:h]]
    else: q = np.r_[np.repeat(p[:1], h, 0), p, np.repeat(p[-1:], h, 0)]
    k = np.ones(win) / win; r = np.c_[np.convolve(q[:, 0], k, "valid"), np.convolve(q[:, 1], k, "valid")]
    if not closed: r[0], r[-1] = p[0], p[-1]
    return r

def tangent(pts, at_end, span):
    p = np.asarray(pts); n = len(p)
    if n < 2: return np.zeros(2)
    a, b = (p[-1], p[max(0, n - 1 - span)]) if at_end else (p[0], p[min(n - 1, span)])
    v = a - b; l = np.hypot(*v); return v / l if l > 1e-9 else np.zeros(2)

def join_strokes(strokes, eps, straight=True):
    """Join open strokes whose ends are within eps mm; where several meet, the straightest continuation first. Returns the new list."""
    op = [s for s in strokes if not s.closed and len(s.pts) >= 1]; cl = [s for s in strokes if s.closed]
    n = len(op)
    if n < 2 or eps <= 0: return strokes
    E = np.array([[s.pts[0], s.pts[-1]] for s in op]).reshape(-1, 2)                    # end 2*i = head, 2*i+1 = tail
    T = np.array([[tangent(s.pts, False, 6), tangent(s.pts, True, 6)] for s in op]).reshape(-1, 2)   # pointing OUT of the stroke
    dmat = np.hypot(E[:, None, 0] - E[None, :, 0], E[:, None, 1] - E[None, :, 1])
    ii, jj = np.nonzero(np.triu(dmat <= eps, 1)); cand = []
    for a, b in zip(ii.tolist(), jj.tolist()):
        if a // 2 == b // 2: continue
        pen = ((1 + float(T[a] @ T[b])) / 2) if straight else 0.0                         # 0 = a straight continuation, 1 = a U-turn
        cand.append((dmat[a, b] / eps + pen, a, b))
    cand.sort(); link = {}
    for _, a, b in cand:
        if a in link or b in link: continue
        link[a] = b; link[b] = a
    used = set(); res = []
    def chain(i, head_first):
        pts = []; e = 2 * i + (0 if head_first else 1)                                   # we enter stroke i at end e
        while True:
            s = e // 2; used.add(s); p = op[s].pts if e % 2 == 0 else op[s].pts[::-1]
            pts.append(p); out_end = e ^ 1
            if out_end in link and link[out_end] // 2 not in used: e = link[out_end]
            else: break
        return np.vstack(pts)
    for i in range(n):                                                                   # chains from a free end
        if i in used: continue
        if (2 * i) not in link: res.append(Stroke(chain(i, True)))
        elif (2 * i + 1) not in link: res.append(Stroke(chain(i, False)))
    for i in range(n):                                                                   # what remains is a loop of strokes
        if i not in used: p = chain(i, True); res.append(Stroke(p, closed=True))
    return cl + res

def strokes_skeleton(sheet, a, dbg):
    from skimage.morphology import skeletonize
    sk = skeletonize(sheet.ink); dbg.save("skeleton", np.where(sk, 0, 255).astype(np.uint8))
    raw = trace_skeleton(sk); ppmm = sheet.ppmm; strokes = []; spurs = 0
    dist = cv2.distanceTransform(sheet.ink.astype(np.uint8), cv2.DIST_L2, 5) / ppmm       # half the local stroke width, mm
    for path, d0, d1, closed in raw:
        if len(path) == 1:                                                               # a dot: a 0.3 mm tick
            c = sheet.to_mm([(path[0][0] + .5, path[0][1] + .5)])[0]; strokes.append(Stroke([c - (.15, 0), c + (.15, 0)])); continue
        if closed and path[0] == path[-1]: path = path[:-1]
        pts = sheet.to_mm(np.array(path, float) + .5); st = Stroke(pts, closed)
        if (d0 == 1) != (d1 == 1):                                                       # a free end on a junction: a hair if shorter than the local half-width x3 + spur
            j = path[-1] if d0 == 1 else path[0]
            if st.length() < a.spur + 3.0 * float(dist[j]): spurs += 1; continue
        strokes.append(st)
    dbg.save("traced_%d_paths_%d_spurs_cut" % (len(strokes), spurs), draw_polys((int(sheet.H * 4), int(sheet.W * 4)), [s.pts for s in strokes], 4, sheet.H, rand_colour))
    for s in strokes:
        if len(s.pts) > 4: s.pts = smooth(s.pts, int(round(a.smooth * ppmm)), s.closed)
    strokes = join_strokes(strokes, a.join)
    dbg.save("joined_%d_strokes" % len(strokes), draw_polys((int(sheet.H * 4), int(sheet.W * 4)), [s.pts for s in strokes], 4, sheet.H, rand_colour))
    return strokes, {"skeleton_paths": len(raw), "spurs_removed": spurs}

def strokes_outline(sheet, a, dbg):
    import potrace
    ink = sheet.ink
    if a.shrink > 0:
        r = int(round(a.shrink * sheet.ppmm)); ink = cv2.erode(ink.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))).astype(bool)
        dbg.save("ink_shrunk", np.where(ink, 0, 255).astype(np.uint8))
    pl = potrace.Bitmap(~ink).trace(turdsize=max(2, int(a.speckle * sheet.ppmm ** 2)), alphamax=1.0, opticurve=True, opttolerance=0.2)
    strokes = []; step = 0.3 * sheet.ppmm                                                # (potracer traces the False pixels, hence ~ink above)
    for c in pl.curves:
        pts = [(c.start_point.y, c.start_point.x)]
        for sg in c.segments:
            p0 = np.array(pts[-1][::-1]); e = np.array([sg.end_point.x, sg.end_point.y])
            if sg.is_corner:
                m = np.array([sg.c.x, sg.c.y]); pts += [(m[1], m[0]), (e[1], e[0])]
            else:
                c1 = np.array([sg.c1.x, sg.c1.y]); c2 = np.array([sg.c2.x, sg.c2.y])
                n = max(2, int(math.ceil((np.hypot(*(c1 - p0)) + np.hypot(*(c2 - c1)) + np.hypot(*(e - c2))) / step)))
                for t in np.linspace(0, 1, n + 1)[1:]:
                    q = (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * c1 + 3 * (1 - t) * t * t * c2 + t ** 3 * e; pts.append((q[1], q[0]))
        p = sheet.to_mm(pts); p = p[:-1] if np.allclose(p[0], p[-1]) else p; strokes.append(Stroke(p, True))
    dbg.save("potrace_%d_outlines" % len(strokes), draw_polys((int(sheet.H * 4), int(sheet.W * 4)), [s.pts for s in strokes], 4, sheet.H, rand_colour))
    return strokes, {"outlines": len(strokes)}

def strokes_hatch(sheet, a, dbg):
    ink = sheet.ink.astype(np.uint8)
    r = int(round(a.pen / 2 * sheet.ppmm))
    if r >= 1: ink = cv2.erode(ink, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1)))   # the pen's own width stays inside the shape
    dbg.save("ink_eroded_by_half_pen", np.where(ink > 0, 0, 255).astype(np.uint8))
    h, w = ink.shape; diag = int(math.hypot(h, w)) + 4
    rot = cv2.getRotationMatrix2D((w / 2, h / 2), a.hatch_angle, 1.0); rot[0, 2] += (diag - w) / 2; rot[1, 2] += (diag - h) / 2
    big = cv2.warpAffine(ink, rot, (diag, diag), flags=cv2.INTER_NEAREST)
    inv = cv2.invertAffineTransform(rot); sp = max(1.0, a.hatch_spacing * sheet.ppmm); strokes = []
    y = sp / 2
    while y < diag:
        row = big[int(y)] > 0; d = np.diff(np.r_[0, row.astype(np.int8), 0]); st = np.nonzero(d == 1)[0]; en = np.nonzero(d == -1)[0]
        for s0, e0 in zip(st, en):
            if e0 - s0 < 2: continue
            p = np.array([[s0, y], [e0 - 1, y]], float); q = p @ inv[:, :2].T + inv[:, 2]      # back to picture pixels (x=col, y=row)
            strokes.append(Stroke(sheet.to_mm(q[:, ::-1])))
        y += sp
    dbg.save("hatch_%d_lines" % len(strokes), draw_polys((int(sheet.H * 4), int(sheet.W * 4)), [s.pts for s in strokes], 4, sheet.H, rand_colour))
    strokes = join_strokes(strokes, a.hatch_spacing * 1.6, straight=False)
    dbg.save("hatch_joined_%d_strokes" % len(strokes), draw_polys((int(sheet.H * 4), int(sheet.W * 4)), [s.pts for s in strokes], 4, sheet.H, rand_colour))
    return strokes, {"hatch_lines": len(strokes)}

# ---------------------------------------------------------------------------------------------------------------- order, bridge
def order_strokes(strokes, start=(0.0, 0.0)):
    """Greedy nearest next stroke (open: either end; closed: nearest of up to 32 vertices). Returns new strokes, each oriented/rotated to be drawn first-to-last."""
    n = len(strokes)
    if n == 0: return []
    K = 32; C = np.full((n, K, 2), np.inf); idx = []
    for i, s in enumerate(strokes):
        if s.closed:
            sel = np.unique(np.linspace(0, len(s.pts) - 1, min(K, len(s.pts))).astype(int)); C[i, :len(sel)] = s.pts[sel]; idx.append(sel)
        else: C[i, 0], C[i, 1] = s.pts[0], s.pts[-1]; idx.append(None)
    alive = np.ones(n, bool); cur = np.array(start, float); out = []
    for _ in range(n):
        d = np.hypot(C[..., 0] - cur[0], C[..., 1] - cur[1]); d[~alive] = np.inf
        k = np.argmin(d, axis=1); best = d[np.arange(n), k]; i = int(np.argmin(best)); kk = int(k[i]); s = strokes[i]; alive[i] = False
        if s.closed: p = np.roll(s.pts, -int(idx[i][kk]), axis=0)
        else: p = s.pts if kk == 0 else s.pts[::-1]
        out.append(Stroke(p, s.closed)); cur = p[0] if s.closed else p[-1]
    return out

def bridge_strokes(strokes, gap):
    """Strokes drawn one after the other whose ends are closer than gap are drawn through (no pen lift). A closed stroke is opened and closed again."""
    if gap <= 0 or not strokes: return strokes
    res = []; cur = None
    def as_open(s): return np.vstack([s.pts, s.pts[:1]]) if s.closed else s.pts
    for s in strokes:
        o = as_open(s)
        if cur is not None and np.hypot(*(o[0] - cur[-1])) <= gap: cur = np.vstack([cur, o])
        else:
            if cur is not None: res.append(Stroke(cur))
            cur = o
    res.append(Stroke(cur))
    return res

def travel_length(strokes, start=(0.0, 0.0)):
    """Pen-up distance when the strokes are drawn in this order and each is left at its last point (a closed one at its first), then back to the origin."""
    cur = np.array(start, float); t = 0.0
    for s in strokes:
        t += float(np.hypot(*(s.pts[0] - cur))); cur = s.pts[0] if s.closed else s.pts[-1]
    return t + float(np.hypot(*cur))

# ---------------------------------------------------------------------------------------------------------------- fit: lines and arcs
def _gallop(ok, lo, hi):
    """The largest j in [lo,hi] with ok(j), ok(lo) being true; doubling then bisecting."""
    good, step = lo, 1
    while good < hi:
        nxt = min(hi, good + step)
        if ok(nxt): good = nxt; step *= 2
        else:
            l, r = good, nxt
            while r - l > 1:
                m = (l + r) // 2
                if ok(m): l = m
                else: r = m
            return l
    return good

def _line_ok(P, i, j, tol):
    a, b = P[i], P[j]; d = b - a; L = float(np.hypot(*d)); seg = P[i + 1:j]
    if len(seg) == 0: return True
    if L < 1e-9: return float(np.hypot(*(seg - a).T).max()) <= tol
    dist = np.abs(d[0] * (seg[:, 1] - a[1]) - d[1] * (seg[:, 0] - a[0])) / L; t = ((seg - a) @ d) / (L * L)
    return float(dist.max()) <= tol and float(t.min()) >= -tol / L and float(t.max()) <= 1 + tol / L

def _arc_fit(P, i, j, tol, rmin, rmax):
    """A circle through P[i] and P[j] (centre on their bisector, nearest to the least-squares one) that the points between stay within tol of; (centre, R, ccw) or None."""
    pts = P[i:j + 1]; a, b = pts[0], pts[-1]; d = b - a; L = float(np.hypot(*d))
    if L < 1e-6: return None
    x, y = pts[:, 0], pts[:, 1]; A = np.c_[2 * x, 2 * y, np.ones(len(x))]
    try: sol = np.linalg.lstsq(A, x * x + y * y, rcond=None)[0]
    except np.linalg.LinAlgError: return None
    m = (a + b) / 2; nrm = np.array([-d[1], d[0]]) / L; t = float(np.dot(sol[:2] - m, nrm)); c = m + t * nrm; R = math.hypot(L / 2, t)
    if not (rmin <= R <= rmax) or L > 2 * R * 0.999: return None
    if float(np.abs(np.hypot(*(pts - c).T) - R).max()) > tol: return None
    ang = np.unwrap(np.arctan2(y - c[1], x - c[0])); da = np.diff(ang)
    if not (np.all(da >= -1e-9) or np.all(da <= 1e-9)) or abs(ang[-1] - ang[0]) > math.pi: return None
    return c, R, bool(ang[-1] > ang[0])

class Path:
    """start, then segments: ('L', end) or ('A', end, centre, ccw)."""
    def __init__(self, start): self.start, self.segs = np.asarray(start, float), []
    def end(self): return self.segs[-1][1] if self.segs else self.start

def fit_stroke(s, tol, arcs, rmin, rmax):
    P = np.vstack([s.pts, s.pts[:1]]) if s.closed else s.pts
    path = Path(P[0]); n = len(P) - 1; i = 0
    while i < n:
        jl = _gallop(lambda j: _line_ok(P, i, j, tol), i + 1, n); seg = None
        if arcs and n - i >= 3:
            ja = _gallop(lambda j: _arc_fit(P, i, j, tol, rmin, rmax) is not None, i + 2, n)
            if ja >= i + 3 and ja > jl + 1:
                f = _arc_fit(P, i, ja, tol, rmin, rmax)
                if f: seg = ("A", P[ja], f[0], f[2]); jl = ja
        path.segs.append(seg if seg else ("L", P[jl])); i = jl
    return path

def sample_arc(p, q, c, ccw, step):
    a0 = math.atan2(p[1] - c[1], p[0] - c[0]); a1 = math.atan2(q[1] - c[1], q[0] - c[0])
    sw = (a1 - a0) % (2 * math.pi) if ccw else -((a0 - a1) % (2 * math.pi))
    r0, r1 = math.hypot(*(p - c)), math.hypot(*(q - c)); n = max(2, int(math.ceil(abs(sw) * max(r0, r1) / step)) + 1)
    t = np.linspace(0, 1, n); a = a0 + sw * t; r = r0 + (r1 - r0) * t
    return np.c_[c[0] + r * np.cos(a), c[1] + r * np.sin(a)], sw

def sample_path(path, step=0.25):
    pts = [path.start]; cur = path.start
    for sg in path.segs:
        if sg[0] == "L": pts.append(sg[1])
        else: pts.extend(sample_arc(cur, sg[1], sg[2], sg[3], step)[0][1:])
        cur = sg[1]
    return np.array(pts)

# ---------------------------------------------------------------------------------------------------------------- GCode
def num(v):
    s = ("%.3f" % v).rstrip("0").rstrip("."); return "0" if s in ("-0", "") else s

def rnd(v): return round(float(v), 3) + 0.0

def write_gcode(paths, title_lines):
    out = ["; " + t for t in title_lines] + ["M5", "G90 G0 X0 Y0", "G21 G17 G54 G94"]; cur = (0.0, 0.0); pen = False; counts = {"G0": 0, "G1": 0, "G2": 0, "G3": 0}
    for p in paths:
        s = (rnd(p.start[0]), rnd(p.start[1]))
        if s != cur:
            if pen: out.append("M5"); pen = False
            out.append("G0 X%s Y%s" % (num(s[0]), num(s[1]))); counts["G0"] += 1; cur = s
        if not pen: out.append("M3"); pen = True
        for sg in p.segs:
            e = (rnd(sg[1][0]), rnd(sg[1][1]))
            if e == cur: continue
            if sg[0] == "L": out.append("G1 X%s Y%s" % (num(e[0]), num(e[1]))); counts["G1"] += 1
            else:
                i, j = rnd(sg[2][0] - cur[0]), rnd(sg[2][1] - cur[1]); g = "G3" if sg[3] else "G2"
                out.append("%s X%s Y%s I%s J%s" % (g, num(e[0]), num(e[1]), num(i), num(j))); counts[g] += 1
            cur = e
    out += ["M5"] + (["G0 X0 Y0"] if cur != (0.0, 0.0) else []) + ["M30"]
    for l in out: assert len(l) <= 47, l
    return out, counts

# ---------------------------------------------------------------------------------------------------------------- time model (the firmware's planner, unlimited look-ahead)
def estimate_time(paths, k):
    """Seconds: moves planned like the firmware (accel, top speed, a junction limit from the change of direction, arcs limited to sqrt(a*r)), a stop and a settle
    time at every pen change. Returns dict(draw_s, travel_s, pen_s, total_s)."""
    vmax, vmin, jerk, acc = k["vmax"], k["vmin"], k["jerk"], k["accel"]
    def group_time(blocks):
        if not blocks: return 0.0
        L = np.array([b[0] for b in blocks]); U = np.array([b[1] for b in blocks]); V = np.array([b[2] for b in blocks]); n = len(L)
        lim = np.empty(n + 1); lim[0] = lim[n] = vmin
        if n > 1:
            du = np.hypot(*(U[1:] - U[:-1]).T); cap = np.minimum(V[1:], V[:-1])
            with np.errstate(divide="ignore"): j = np.where(du > 1e-9, jerk / np.maximum(du, 1e-9), np.inf)
            lim[1:n] = np.clip(np.minimum(j, cap), vmin, vmax)
        v = lim.copy()
        for i in range(n): v[i + 1] = min(v[i + 1], math.sqrt(v[i] ** 2 + 2 * acc * L[i]))
        for i in range(n - 1, -1, -1): v[i] = min(v[i], math.sqrt(v[i + 1] ** 2 + 2 * acc * L[i]))
        t = 0.0
        for i in range(n):
            v0, v1, l, vm = v[i], v[i + 1], L[i], V[i]
            vp = min(vm, math.sqrt(max(v0 * v0, v1 * v1, (2 * acc * l + v0 * v0 + v1 * v1) / 2)))
            ta, tb = (vp - v0) / acc, (vp - v1) / acc; cruise = l - (vp * vp - v0 * v0) / (2 * acc) - (vp * vp - v1 * v1) / (2 * acc)
            t += ta + tb + (max(0.0, cruise) / vp if vp > 0 else 0)
        return t
    def blocks_of(pts_cur, segs):
        bl = []; cur = pts_cur
        for sg in segs:
            if sg[0] == "L":
                d = sg[1] - cur; l = float(np.hypot(*d))
                if l > 1e-9: bl.append((l, d / l, vmax))
            else:
                pts, sw = sample_arc(cur, sg[1], sg[2], sg[3], 1.0); R = float(np.hypot(*(cur - sg[2]))); vm = min(vmax, math.sqrt(acc * max(R, 1e-3)))
                for a, b in zip(pts[:-1], pts[1:]):
                    d = b - a; l = float(np.hypot(*d))
                    if l > 1e-9: bl.append((l, d / l, vm))
            cur = sg[1]
        return bl
    draw = trav = 0.0; pen_events = 0; cur = np.zeros(2)
    for p in paths:
        trav += group_time(blocks_of(cur, [("L", p.start)])) if np.hypot(*(p.start - cur)) > 1e-6 else 0.0
        draw += group_time(blocks_of(p.start, p.segs)); cur = p.end(); pen_events += 2
    trav += group_time(blocks_of(cur, [("L", np.zeros(2))])) if np.hypot(*cur) > 1e-6 else 0.0
    pen = pen_events * k["settle"]
    return {"draw_s": draw, "travel_s": trav, "pen_s": pen, "total_s": draw + trav + pen}

def fmt_t(s): s = int(round(s)); return "%d h %02d min %02d s" % (s // 3600, s % 3600 // 60, s % 60) if s >= 3600 else "%d min %02d s" % (s // 60, s % 60)

# ---------------------------------------------------------------------------------------------------------------- pictures out
def ink_polys(paths): return [sample_path(p, 0.2) for p in paths]

def travel_polys(paths):
    cur = np.zeros(2); t = []
    for p in paths: t.append(np.array([cur, p.start])); cur = p.end()
    t.append(np.array([cur, np.zeros(2)])); return t

def render_png(paths, W, H, pen, zone, s=4):
    img = np.full((int(H * s), int(W * s), 3), 255, np.uint8)
    if zone is not None:
        ov = img.copy(); x0, x1 = int(max(0, zone.xl) * s), int(min(W, zone.xr) * s); y1 = int(max(0, H - zone.yt) * s)
        cv2.rectangle(ov, (0, 0), (img.shape[1], img.shape[0]), (225, 225, 250), -1); cv2.rectangle(ov, (x0, y1), (x1, img.shape[0]), (255, 255, 255), -1)
        img = cv2.addWeighted(ov, 0.7, img, 0.3, 0) if (zone.xl > 0 or zone.xr < W or zone.yt < H) else img
    for pl in travel_polys(paths): cv2.polylines(img, [np.round(mmscale(pl, s, H)).astype(np.int32).reshape(-1, 1, 2)], False, (90, 90, 240), 1, cv2.LINE_AA)
    for pl in ink_polys(paths):
        cv2.polylines(img, [np.round(mmscale(pl, s, H) * 16).astype(np.int32).reshape(-1, 1, 2)], False, (35, 35, 35), max(1, int(round(pen * s))), cv2.LINE_AA, shift=4)
    cv2.rectangle(img, (0, 0), (img.shape[1] - 1, img.shape[0] - 1), (150, 150, 150), 1); cv2.circle(img, (0, img.shape[0] - 1), 6, (0, 140, 0), -1)
    return img

def render_svg(paths, W, H, pen, zone):
    def P(p): return "%s %s" % (num(p[0]), num(H - p[1]))
    d = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" width="%smm" height="%smm">' % (num(W), num(H), num(W), num(H)),
         '<rect width="%s" height="%s" fill="white" stroke="#999" stroke-width="0.4"/>' % (num(W), num(H))]
    if zone is not None: d.append('<rect x="%s" y="%s" width="%s" height="%s" fill="none" stroke="#c33" stroke-width="0.5" stroke-dasharray="3 2"/>' % (num(zone.xl), num(H - zone.yt), num(zone.xr - zone.xl), num(zone.yt + 10 * H)))
    for pl in travel_polys(paths): d.append('<path d="M%s L%s" stroke="#e55" stroke-width="0.25" stroke-dasharray="1.5 1.5" fill="none"/>' % (P(pl[0]), P(pl[1])))
    for p in paths:
        s = "M" + P(p.start); cur = p.start
        for sg in p.segs:
            if sg[0] == "L": s += " L" + P(sg[1])
            else:
                R = (math.hypot(*(cur - sg[2])) + math.hypot(*(sg[1] - sg[2]))) / 2; sw = sample_arc(cur, sg[1], sg[2], sg[3], 1.0)[1]
                s += " A%s %s 0 %d %d %s" % (num(R), num(R), 1 if abs(sw) > math.pi else 0, 0 if sg[3] else 1, P(sg[1]))
            cur = sg[1]
        d.append('<path d="%s" stroke="#222" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round" fill="none"/>' % (s, num(pen)))
    d.append("</svg>"); return "\n".join(d)

# ---------------------------------------------------------------------------------------------------------------- one style, start to finish
def run_style(style, sheet, a, name, outdir, geom, k):
    folder = os.path.join(outdir, style); os.makedirs(folder, exist_ok=True); dbg = Debug(os.path.join(folder, "debug"), a.debug)
    dbg.save("ink_input", np.where(sheet.ink, 0, 255).astype(np.uint8))
    strokes, info = {"skeleton": strokes_skeleton, "outline": strokes_outline, "hatch": strokes_hatch}[style](sheet, a, dbg)
    strokes = [s for s in strokes if len(s.pts) >= 2 and s.length() >= a.min_stroke]
    naive = travel_length(strokes); n0 = len(strokes)
    ordered = order_strokes(strokes) if not a.no_optimize else strokes
    merged = bridge_strokes(ordered, a.bridge) if not a.no_optimize else ordered
    ordered_travel = travel_length(merged)
    Hs = sheet.H
    dbg.save("order_rainbow_%d_strokes" % len(merged), draw_polys((int(Hs * 4), int(sheet.W * 4)), [s.pts for s in merged], 4, Hs, rainbow, 2))
    paths = [fit_stroke(s, a.tol, not a.no_arcs, 0.3, ARC_RMAX_MM) for s in merged]
    npts_in = sum(len(s.pts) for s in merged)
    dbg.save("fitted_lines_and_arcs", render_png(paths, sheet.W, Hs, a.pen, None))
    zone = Zone(*geom, a.side, a.top) if geom else None
    pts_all = np.vstack([sample_path(p, 0.25) for p in paths] + [np.zeros((1, 2))])           # the origin counts: the pen starts and ends there
    bad = zone.outside(pts_all) if zone else np.zeros((0, 2))
    gl, counts = write_gcode(paths, ["CoreLR img2gcode: %s, style %s" % (name, style), "sheet %gx%g mm, origin bottom-left, pen %g" % (sheet.W, sheet.H, a.pen)])
    ink_len = sum(float(np.hypot(*np.diff(sample_path(p, 0.25), axis=0).T).sum()) for p in paths)
    t = estimate_time(paths, k); png = render_png(paths, sheet.W, Hs, a.pen, zone)
    res = {"image": name, "style": style, "sheet_mm": [sheet.W, sheet.H], "pen_mm": a.pen, "tol_mm": a.tol, "strokes_before_join_order": n0, "strokes": len(paths),
           "points_before_fit": int(npts_in), "gcode_lines": len(gl), "moves": counts, "bytes": sum(len(l) + 1 for l in gl), "ink_mm": round(ink_len, 1),
           "travel_mm": round(ordered_travel, 1), "travel_mm_unordered": round(naive, 1), "pen_lifts": len(paths), "time_s": {x: round(y, 1) for x, y in t.items()},
           "outside_zone_points": int(len(bad)), "firmware_constants": k, **info}
    base = os.path.join(folder, name)
    cv2.imwrite(base + ".png", png); open(base + ".svg", "w").write(render_svg(paths, sheet.W, Hs, a.pen, zone))
    if os.path.exists(base + ".gc"): os.remove(base + ".gc")                            # never leave an older file next to a refusal
    if len(bad) == 0: open(base + ".gc", "w", newline="\n").write("\n".join(gl) + "\n")
    open(base + ".json", "w").write(json.dumps(res, indent=1))
    open(base + ".md", "w").write(report_md(res, a, bad, zone, geom))
    return res, png

def report_md(r, a, bad, zone, geom):
    L = ["# %s -- %s" % (r["image"], r["style"]), "", "Sheet %g x %g mm, origin (X0 Y0) at the bottom-left, pen %g mm, fit tolerance %g mm, arcs %s." % (r["sheet_mm"][0], r["sheet_mm"][1], r["pen_mm"], r["tol_mm"], "off" if a.no_arcs else "on"), ""]
    if zone: L += ["Travel zone (%s): X %.1f .. %.1f mm, Y up to %.1f mm." % (a.geometry, zone.xl, zone.xr, zone.yt)]
    else: L += ["No `--geometry`: the travel zone was **not** checked."]
    if len(bad): L += ["", "**NOT WRITTEN: %d points of the path are outside the travel zone** (first: X%.1f Y%.1f). Move the drawing origin, shrink the sheet or the margin, or change the geometry." % (len(bad), bad[0][0], bad[0][1])]
    m = r["moves"]; t = r["time_s"]
    L += ["", "| | |", "|---|---|", "| strokes (pen down/up pairs) | %d (from %d before joining and bridging) |" % (r["strokes"], r["strokes_before_join_order"]),
          "| GCode lines / bytes | %d / %d |" % (r["gcode_lines"], r["bytes"]), "| moves | %d G0, %d G1, %d G2, %d G3 |" % (m["G0"], m["G1"], m["G2"], m["G3"]),
          "| ink length | %.0f mm |" % r["ink_mm"], "| pen-up travel | %.0f mm (%.0f mm in the order of the picture) |" % (r["travel_mm"], r["travel_mm_unordered"]),
          "| points before fitting | %d |" % r["points_before_fit"],
          "| **estimated time** | **%s** = drawing %s + travel %s + pen %s |" % (fmt_t(t["total_s"]), fmt_t(t["draw_s"]), fmt_t(t["travel_s"]), fmt_t(t["pen_s"])), ""]
    k = r["firmware_constants"]
    L += ["Time model (%s): top speed %.1f mm/s, acceleration %.0f mm/s2, junction limit %.0f mm/s, minimum %.0f mm/s, pen settle %.2f s; unlimited look-ahead, G0 as fast as G1 -- a floor, to be checked on the machine." % (k["source"], k["vmax"], k["accel"], k["jerk"], k["vmin"], k["settle"])]
    return "\n".join(L) + "\n"

def compare_image(results, outdir, name, sheet, pens):
    w = 420; cells = []
    ink = np.where(sheet.ink, 0, 255).astype(np.uint8); ink = cv2.cvtColor(cv2.resize(ink, (w, int(w * ink.shape[0] / ink.shape[1])), interpolation=cv2.INTER_AREA), cv2.COLOR_GRAY2BGR)
    cells.append(("input (black = ink)", ink, ""))
    for (r, png) in results:
        t = r["time_s"]["total_s"]; im = cv2.resize(png, (w, int(w * png.shape[0] / png.shape[1])), interpolation=cv2.INTER_AREA)
        cells.append((r["style"], im, "%d strokes, %d lines, ink %.0f m, %s" % (r["strokes"], r["gcode_lines"], r["ink_mm"] / 1000, fmt_t(t))))
    h = max(c[1].shape[0] for c in cells) + 56; canvas = Image.new("RGB", (len(cells) * (w + 10) + 10, h), (245, 245, 245)); d = ImageDraw.Draw(canvas)
    try: f = ImageFont.truetype("DejaVuSans.ttf", 15)
    except OSError:
        try: f = ImageFont.truetype("arial.ttf", 15)
        except OSError: f = ImageFont.load_default()
    for i, (title, im, cap) in enumerate(cells):
        x = 10 + i * (w + 10); d.text((x, 6), title, fill=(0, 0, 0), font=f); d.text((x, 28), cap, fill=(60, 60, 60), font=f)
        canvas.paste(Image.fromarray(cv2.cvtColor(im, cv2.COLOR_BGR2RGB)), (x, 52))
    p = os.path.join(outdir, "compare.png"); canvas.save(p); return p

# ---------------------------------------------------------------------------------------------------------------- command line
def main():
    ap = argparse.ArgumentParser(description="Turn a picture into a GCode file for the CoreLR wall plotter, with previews and a report. See the top of the file for the details.",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog="Origin of the GCode: the sheet's bottom-left corner. Exit status 0 written, 1 bad input, 3 outside the travel zone.")
    ap.add_argument("image"); ap.add_argument("-s", "--style", default="skeleton", choices=STYLES + ("all",), help="how to draw the picture (default skeleton)")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "out"), help="output folder (default out/ next to this script, ignored by git)")
    ap.add_argument("--paper", default="a4", help="a0..a6 or WIDTHxHEIGHT in mm (default a4)"); ap.add_argument("--landscape", action="store_true", help="landscape instead of portrait")
    ap.add_argument("--margin", type=float, default=10.0, help="blank margin around the picture, mm (default 10)")
    ap.add_argument("--pen", type=float, default=2.0, help="pen width, mm (default 2: the whiteboard marker; 0.7 for the bench pencil)")
    ap.add_argument("--tol", type=float, default=None, help="fit tolerance, mm (default pen/8)")
    ap.add_argument("--no-arcs", action="store_true", help="lines only (no G2/G3)"); ap.add_argument("--no-optimize", action="store_true", help="keep the strokes in the picture's order")
    ap.add_argument("--bridge", type=float, default=None, help="draw through a pen-up gap shorter than this, mm (default = pen width; 0 = always lift)")
    ap.add_argument("--threshold", type=int, default=None, help="0..255, darker than this is ink (default: Otsu's)"); ap.add_argument("--invert", action="store_true", help="light on dark: the light is the ink")
    ap.add_argument("--speckle", type=float, default=0.5, help="drop black specks under this area, mm2 (default 0.5)")
    ap.add_argument("--spur", type=float, default=None, help="skeleton: cut hairs shorter than this, mm (default = pen width)")
    ap.add_argument("--smooth", type=float, default=0.8, help="skeleton: moving-average window, mm (default 0.8)"); ap.add_argument("--join", type=float, default=None, help="skeleton: join ends closer than this, mm (default 1.5 x pen)")
    ap.add_argument("--shrink", type=float, default=0.0, help="outline: shrink the black by this much before tracing, mm (default 0; pen/2 keeps the drawn edge on the original edge)")
    ap.add_argument("--hatch-spacing", type=float, default=None, help="hatch: distance between lines, mm (default 0.8 x pen)"); ap.add_argument("--hatch-angle", type=float, default=45.0, help="hatch: direction, degrees (default 45)")
    ap.add_argument("--min-stroke", type=float, default=0.25, help="drop strokes shorter than this, mm (default 0.25)")
    ap.add_argument("--geometry", metavar="D,OX,OY | D,L", help="the machine: check the travel zone (see above)"); ap.add_argument("--side", type=float, default=20.0, help="side margin of the zone, mm (default 20)")
    ap.add_argument("--top", type=float, default=50.0, help="top margin of the zone, mm (default 50)")
    ap.add_argument("--debug", action="store_true", help="write every intermediate image to OUT/NAME/STYLE/debug/")
    a = ap.parse_args()
    if not os.path.isfile(a.image): sys.exit("img2gcode.py: no such image: %s" % a.image)
    if a.pen <= 0: sys.exit("img2gcode.py: --pen must be positive")
    a.tol = a.tol if a.tol is not None else a.pen / 8; a.bridge = a.pen if a.bridge is None else a.bridge; a.spur = a.pen if a.spur is None else a.spur
    a.join = 1.5 * a.pen if a.join is None else a.join; a.hatch_spacing = 0.8 * a.pen if a.hatch_spacing is None else a.hatch_spacing
    ps = a.paper.lower()
    if ps in PAPERS: W, H = PAPERS[ps]
    else:
        m = re.fullmatch(r"(\d+(?:\.\d+)?)[x,](\d+(?:\.\d+)?)", ps)
        if not m: sys.exit("img2gcode.py: --paper a0..a6 or WIDTHxHEIGHT (mm)")
        W, H = float(m.group(1)), float(m.group(2))
    if a.landscape: W, H = max(W, H), min(W, H)
    else: W, H = min(W, H), max(W, H)
    geom = parse_geometry(a.geometry) if a.geometry else None
    name = os.path.splitext(os.path.basename(a.image))[0]; outdir = os.path.join(a.out, name); os.makedirs(outdir, exist_ok=True)
    dbg = Debug(os.path.join(outdir, "debug_input"), a.debug); k = firmware_constants()
    sheet, thr = load_ink(a.image, W, H, a.margin, a.pen, a.threshold, a.invert, a.speckle, dbg)
    styles = list(STYLES) if a.style == "all" else [a.style]; results = []; rc = 0
    for st in styles:
        r, png = run_style(st, sheet, a, name, outdir, geom, k); results.append((r, png)); t = r["time_s"]["total_s"]
        print("%-8s %4d strokes %6d lines  ink %6.0f mm  travel %6.0f mm  est. %s%s" % (st, r["strokes"], r["gcode_lines"], r["ink_mm"], r["travel_mm"], fmt_t(t),
              "   ** OUTSIDE THE TRAVEL ZONE (%d points): no .gc written **" % r["outside_zone_points"] if r["outside_zone_points"] else ""))
        if r["outside_zone_points"]: rc = 3
        print("         -> %s" % os.path.join(outdir, st))
    if len(results) > 1: print("compare  -> %s" % compare_image(results, outdir, name, sheet, a.pen))
    return rc

if __name__ == "__main__": sys.exit(main())
