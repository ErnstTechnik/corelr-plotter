#!/usr/bin/env python3
#
# make_test_images.py
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
"""make_test_images.py -- writes synthetic test images for the img2gcode tools into samples/ (black on white PNG, A4 portrait ratio).

  logo.png      a logo: ring, triangle, bold letters, thick and thin strokes
  lineart.png   a line drawing: spiral, rose curve, a star, hatching -- constant-width curves
  text.png      a paragraph of text in a normal and a bold face (needs a TrueType font; falls back to PIL's default)
  sketch.png    a hand-made look: wobbling strokes with varying width and open ends (a house, a tree, a sun)

Everything is drawn from fixed numbers and a fixed random seed, so the images are the same on every run and every machine.
Run:  .venv/bin/python make_test_images.py [-o DIR] [--width PX]
Exit status: 0 on success.
"""
import argparse, math, os, sys
import numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont

FONTS = ["DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "arial.ttf", "arialbd.ttf", "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"]

def font(size, bold=False):
    names = [n for n in FONTS if (("Bold" in n or "bd" in n) == bold)]
    for n in names:
        try: return ImageFont.truetype(n, size)
        except OSError: pass
    return ImageFont.load_default(size)

def canvas(w, h): return np.full((h, w), 255, np.uint8)

def poly(img, pts, t):
    cv2.polylines(img, [np.round(np.asarray(pts) * 16).astype(np.int32).reshape(-1, 1, 2)], False, 0, max(1, int(round(t))), cv2.LINE_AA, shift=4)

def logo(w, h):
    img = canvas(w, h); c = (w // 2, h // 3); r = w * 0.30
    cv2.circle(img, c, int(r), 0, int(w * 0.035), cv2.LINE_AA)
    cv2.circle(img, c, int(r * 0.80), 0, int(w * 0.008), cv2.LINE_AA)
    tri = np.array([[c[0], c[1] - r * 0.55], [c[0] + r * 0.5, c[1] + r * 0.4], [c[0] - r * 0.5, c[1] + r * 0.4]], np.int32)
    cv2.fillPoly(img, [tri], 0); cv2.fillPoly(img, [(c + (tri - c) * 0.5).astype(np.int32)], 255)
    p = Image.fromarray(img); d = ImageDraw.Draw(p); f = font(int(w * 0.16), True)
    d.text((w // 2, int(h * 0.68)), "CoreLR", font=f, fill=0, anchor="mm")
    d.text((w // 2, int(h * 0.78)), "wall plotter", font=font(int(w * 0.06)), fill=0, anchor="mm")
    img = np.array(p); cv2.line(img, (int(w * .15), int(h * .84)), (int(w * .85), int(h * .84)), 0, int(w * 0.004), cv2.LINE_AA)
    return img

def lineart(w, h):
    img = canvas(w, h); t = w * 0.006
    th = np.linspace(0, 6 * math.pi, 900); s = w * 0.0125 * th / (2 * math.pi) * 3.1  # spiral
    poly(img, np.c_[w * 0.5 + s * np.cos(th) * 1.0, h * 0.25 + s * np.sin(th)], t)
    th = np.linspace(0, 2 * math.pi, 800); r = w * 0.30 * np.cos(4 * th)                # rose
    poly(img, np.c_[w * 0.5 + r * np.cos(th), h * 0.64 + r * np.sin(th)], t)
    k = np.array([(math.sin(2 * math.pi * i * 2 / 5), -math.cos(2 * math.pi * i * 2 / 5)) for i in range(6)]) * w * 0.12  # star
    poly(img, k + (w * 0.2, h * 0.9), t * 0.7)
    for i in range(12): poly(img, [(w * 0.5 + i * w * 0.03, h * 0.86), (w * 0.5 + i * w * 0.03 + w * 0.1, h * 0.95)], t * 0.4)   # hatching
    cv2.rectangle(img, (int(w * .05), int(h * .03)), (int(w * .95), int(h * .975)), 0, int(t * 1.3), cv2.LINE_AA)
    return img

def text(w, h):
    p = Image.fromarray(canvas(w, h)); d = ImageDraw.Draw(p)
    lines = ["Wall plotter test", "The quick brown fox jumps", "over the lazy dog. 0123456789", "", "Two cables, two stepper", "motors and one pen.", "", "AaBbCcDdEeFfGgHhIiJjKk", "LlMmNnOoPpQqRrSsTtUuVv"]
    y = int(h * 0.06)
    for i, ln in enumerate(lines):
        big = i == 0
        d.text((int(w * 0.07), y), ln, font=font(int(w * (0.075 if big else 0.052)), big), fill=0); y += int(h * (0.075 if big else 0.062))
    return np.array(p)

def sketch(w, h):
    rng = np.random.default_rng(7); img = canvas(w, h)
    def stroke(a, b, t, wob=0.004):
        a, b = np.asarray(a, float) * (w, h), np.asarray(b, float) * (w, h); n = max(8, int(np.hypot(*(b - a)) / 12))
        pts = a + np.outer(np.linspace(0, 1, n), b - a) + np.cumsum(rng.normal(0, w * wob / 3, (n, 2)), 0) * 0.6
        poly(img, pts, t * w * (1 + 0.3 * rng.random()))
    for a, b in [((.2, .75), (.2, .45)), ((.2, .45), (.5, .30)), ((.5, .30), (.8, .45)), ((.8, .45), (.8, .75)), ((.2, .75), (.8, .75)), ((.2, .45), (.8, .45))]: stroke(a, b, .007)
    for a, b in [((.45, .75), (.45, .58)), ((.45, .58), (.58, .58)), ((.58, .58), (.58, .75)), ((.28, .52), (.36, .52)), ((.28, .52), (.28, .62)), ((.36, .52), (.36, .62)), ((.28, .62), (.36, .62))]: stroke(a, b, .005)
    for i in range(14):
        a = 2 * math.pi * i / 14; stroke((.85 + .04 * math.cos(a), .12 + .03 * math.sin(a)), (.85 + .08 * math.cos(a), .12 + .06 * math.sin(a)), .004)
    th = np.linspace(0, 2 * math.pi, 60); poly(img, np.c_[w * (.85 + .03 * np.cos(th)), h * (.12 + .02 * np.sin(th))] + rng.normal(0, 1.2, (60, 2)), w * .005)
    stroke((.1, .9), (.9, .9), .004, .008); stroke((.12, .93), (.6, .93), .003, .008)
    for i in range(8): stroke((.6 + .03 * i, .86), (.58 + .03 * i, .76), .004, .01)
    return img

def main():
    ap = argparse.ArgumentParser(description="Write synthetic test images (black on white, A4 portrait ratio) for the img2gcode tools.")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples"), help="output directory (default: samples/ next to this script)")
    ap.add_argument("--width", type=int, default=1240, help="width in pixels (default 1240 = A4 at 150 dpi); the height follows the A4 ratio")
    a = ap.parse_args()
    w = a.width; h = int(round(w * 297 / 210)); os.makedirs(a.out, exist_ok=True)
    for name, fn in (("logo", logo), ("lineart", lineart), ("text", text), ("sketch", sketch)):
        p = os.path.join(a.out, name + ".png"); Image.fromarray(fn(w, h)).save(p); print(p)
if __name__ == "__main__": sys.exit(main())
