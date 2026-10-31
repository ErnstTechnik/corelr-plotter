#!/usr/bin/env python3
# gen_wiring.py
# Part of the CoreLR wall-plotter firmware project.
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
#
# This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY.
# The DRAWINGS it writes are not covered by this licence: they embed Fritzing artwork and are CC BY-SA 3.0 (third_party/fritzing-parts/PROVENANCE.md).
"""gen_wiring.py -- draws the wiring diagrams of the machine in the Arduino/Fritzing "breadboard" style, as SVG (vector).

A diagram is a scene: parts and wires. Parts are either the vendored Fritzing breadboard drawings (third_party/fritzing-parts/, embedded
unmodified, scaled and rotated) or the project's own drawings in the same style, for what Fritzing does not have (ULN2003 board with its
28BYJ-48 motor, the I2C 2x16 LCD, the external 5 V supply). Wires are orthogonal with rounded corners, from one pin to another by NAME:
a pin is found by its Fritzing connector id (`connectorNpin` / `connectorNleg` in the part's SVG, named from its `.fzp`) or, for the
project's parts, in the table of the class, so a wire follows its part if the part moves.

The pins are NOT written in the scenes: the configurable ones (motors, servo, joystick, SD chip select) are read from firmware/config/HwConfig.h and the
ones the silicon fixes (SPI, TWI) from the Arduino core the firmware is built with (third_party/arduino-core/.../pins_arduino.h); `machine_pins()` refuses a pin used
twice. Change a pin in HwConfig.h, run this tool again: the wires move. `--verify` checks the drawing against the same sources.

Pins of the Arduino Nano are written as on the board: 'D13', 'A0', '5V', 'GND' (the board has two: 'GND' = the one on the D2 side, 'GND#2' = the
one next to VIN), 'VIN'. Every connection is written once, in the scene, next to the HwConfig.h macro it realises.

Every SVG written carries its credit and licence in its <metadata> (CC BY-SA 3.0, "created with parts from Fritzing").

Examples:  tools/generators/gen_wiring.py --list
           tools/generators/gen_wiring.py nano_led                       # -> docs/hardware/images/wiring/nano_led.svg
           tools/generators/gen_wiring.py --all                          # every diagram
           tools/generators/gen_wiring.py parts -o /tmp/parts.svg
           tools/generators/gen_wiring.py --verify                       # every wire against HwConfig.h
Exit status: 0 written / verified, 2 usage or missing part; with --verify, the number of problems.
"""
import argparse, math, os, re, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PARTS_DIR = os.path.join(ROOT, "third_party", "fritzing-parts")
OUT_DIR = os.path.join(ROOT, "docs", "hardware", "images", "wiring")

# part key -> (breadboard svg, part definition). Add a part here when it is vendored.
PARTS = {
    "nano": ("Arduino Nano3_breadboard.svg", "Arduino Nano3(fix).fzp"),
    "led": ("LED-5mm-red-leg.svg", "LED-generic-5mm_6852162_005.fzp"),
    "res220": ("resistor_220.svg", "resistor.fzp"),
    "servo": ("servo.svg", "servo.fzp"),
    "joystick": ("Joystick-Breakout-v11_breadboard.svg", "Joystick-Breakout-v11.fzp"),
    "microsd": ("microsdmodule_breadboard.svg", "microsdmodule.fzp"),
    "diode": ("diode.svg", "diode_schottky_1N5817_300mil.fzp"),
}
# Fritzing's own wire colours (the standard set of its breadboard view)
WIRE = dict(blue="#418dd9", red="#cc1414", black="#404040", yellow="#ffe24d", green="#47cc79", grey="#999999", orange="#ff7033", white="#ffffff",
            brown="#8c3b00", purple="#ab58a2", pink="#ec6fa8", cyan="#2fc4d6")
SCALE = 3.2                    # scene px per 1/72 inch of a Fritzing part: a Nano is 163 px wide, big enough to read its labels
MM = SCALE * 72 / 25.4         # scene px per millimetre: the project's own parts are drawn in mm, at the same true scale
BG = "#f5f7f9"

LICENCE_NOTE = ("Created with parts from Fritzing (fritzing.org, https://github.com/fritzing/fritzing-parts), licensed CC BY-SA 3.0. "
                "This drawing is therefore also CC BY-SA 3.0: https://creativecommons.org/licenses/by-sa/3.0/")


class Placed:
    """Anything with a local box (vx, vy, w, h), placed on the scene at its centre (cx, cy), rotated by a multiple of 90 degrees (clockwise),
    optionally mirrored left-right (before the rotation), scaled by k."""
    count = 0

    def _place(self, cx, cy, rot, flip, k):
        Placed.count += 1
        self.uid, self.cx, self.cy, self.rot, self.flip, self.k = "p%d" % Placed.count, cx, cy, rot, flip, k

    def _map(self, x, y):
        x, y = (x - self.vx - self.w / 2) * self.k, (y - self.vy - self.h / 2) * self.k
        if self.flip:
            x = -x
        a = math.radians(self.rot)
        return self.cx + x * math.cos(a) - y * math.sin(a), self.cy + x * math.sin(a) + y * math.cos(a)

    def pin(self, name):
        """The pin's terminal in scene coordinates."""
        return self._map(*self.local(name))

    def _group(self, name, inner):
        sx = -self.k if self.flip else self.k
        t = "translate(%.2f %.2f) rotate(%g) scale(%.4f %.4f) translate(%.3f %.3f)" % (
            self.cx, self.cy, self.rot, sx, self.k, -(self.vx + self.w / 2), -(self.vy + self.h / 2))
        return '<g id="%s_%s" transform="%s">%s</g>' % (name, self.uid, t, inner)


class Part(Placed):
    """One placed copy of a vendored Fritzing part."""

    def __init__(self, key, cx, cy, rot=0, flip=False, scale=SCALE):
        if key not in PARTS:
            sys.exit("gen_wiring.py: unknown part '%s' (known: %s)" % (key, ", ".join(sorted(PARTS))))
        svg, fzp = PARTS[key]
        path = os.path.join(PARTS_DIR, "svg", svg)
        if not os.path.exists(path):
            sys.exit("gen_wiring.py: missing %s (see third_party/fritzing-parts/PROVENANCE.md)" % path)
        self._place(cx, cy, rot, flip, scale)
        self.key = key
        self.text = open(path, encoding="utf-8").read()
        root_tag = re.search(r"<svg\b[^>]*>", self.text, re.S).group(0)
        self.xmlns = dict(re.findall(r'xmlns:(\w+)="([^"]+)"', root_tag))
        vb = re.search(r'viewBox="([^"]+)"', root_tag)
        if not vb:
            sys.exit("gen_wiring.py: %s has no viewBox" % svg)
        self.vx, self.vy, self.w, self.h = [float(v) for v in vb.group(1).split()]
        self.tree = ET.fromstring(re.sub(r"<!DOCTYPE[^>]*>", "", self.text))
        self.parent = {c: p for p in self.tree.iter() for c in p}
        self.names = self._names(os.path.join(PARTS_DIR, "fzp", fzp))

    @staticmethod
    def _names(fzp):
        """pin name -> connector id; 'D13/SCK' is also 'D13'; a repeated name gets '#2', '#3' (in id order). Where a part has male connectors the female ones (the Nano's ICSP socket) are left out."""
        out, cons = {}, []
        allc = list(ET.parse(fzp).getroot().iter("connector"))
        males = [c for c in allc if c.get("type") != "female"]
        for c in (males or allc):                                    # a part that has only female connectors (a module's header) keeps them all
            cons.append((int(re.sub(r"\D", "", c.get("id"))), c.get("id"), c.get("name")))
        for _, cid, name in sorted(cons):
            for n in {name, name.split("/")[0]}:
                m, i = n, 1
                while m in out:
                    i += 1; m = "%s#%d" % (n, i)
                out[m] = cid
        return out

    @staticmethod
    def _xf(attr):
        """A transform attribute as an affine matrix (a, b, c, d, e, f): translate, scale and matrix are enough for the vendored parts."""
        m = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
        for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", attr or ""):
            v = [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", args)]
            if name == "translate":
                n = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0.0)
            elif name == "scale":
                n = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
            elif name == "matrix":
                n = tuple(v)
            else:
                sys.exit("gen_wiring.py: unsupported transform '%s' in a vendored part" % name)
            a, b, c, d, e, f = m
            m = (a * n[0] + c * n[1], b * n[0] + d * n[1], a * n[2] + c * n[3], b * n[2] + d * n[3], a * n[4] + c * n[5] + e, b * n[4] + d * n[5] + f)
        return m

    def _terminal(self, e):
        """The terminal of connector element e, in the SVG's own units, with the transforms of its ancestors applied."""
        t = e.tag.split("}")[1]
        if t == "circle":
            x, y = float(e.get("cx")), float(e.get("cy"))
        elif t == "rect":
            x, y = float(e.get("x")) + float(e.get("width")) / 2, float(e.get("y")) + float(e.get("height")) / 2
        elif t == "line":                      # a rubber-band leg ends at its tip
            x, y = float(e.get("x2")), float(e.get("y2"))
        elif t == "path":                      # a ring drawn as a path: 'm x y c ...' = the top of the ring, then a quarter turn to its left side
            m = re.match(r"\s*m\s*([-\d.e]+)[ ,]+([-\d.e]+)\s*c\s*([-\d.e ,]+)", e.get("d"))
            n = re.findall(r"-?\d*\.?\d+(?:e-?\d+)?", m.group(3))
            x, y = float(m.group(1)), float(m.group(2)) + float(n[5])
        else:
            sys.exit("gen_wiring.py: connector drawn as <%s>: not supported" % t)
        chain = [e]
        while chain[-1] in self.parent:
            chain.append(self.parent[chain[-1]])
        for a in chain:                        # innermost first: apply its transform, then the enclosing ones
            ma = self._xf(a.get("transform"))
            x, y = ma[0] * x + ma[2] * y + ma[4], ma[1] * x + ma[3] * y + ma[5]
        return x, y

    def local(self, connector):
        """The connector's terminal in the part's own units. A part draws it twice, the pad (`connectorNpin`) and sometimes a leg (`connectorNleg`): the terminal is
        the free end, the one farther from the middle of the part (a rubber-band leg ends at its tip; on a diode the pad is the end and the leg a stub)."""
        cid = self.names.get(connector, connector)
        cand = [self._terminal(e) for suffix in ("leg", "pin") for e in self.tree.iter() if e.get("id") == cid + suffix]
        if not cand:
            sys.exit("gen_wiring.py: part '%s' has no connector '%s'" % (self.key, connector))
        mid = (self.vx + self.w / 2, self.vy + self.h / 2)
        return max(cand, key=lambda q: math.hypot(q[0] - mid[0], q[1] - mid[1]))

    def svg(self):
        body = re.sub(r"^.*?<svg\b[^>]*>", "", re.sub(r"<\?xml[^>]*\?>|<!DOCTYPE[^>]*>|<!--.*?-->", "", self.text, flags=re.S), count=1, flags=re.S)
        body = body.rsplit("</svg>", 1)[0]
        ids = set(re.findall(r'\bid="([^"]+)"', body))
        for i in sorted(ids, key=len, reverse=True):          # longest first: 'a1' before 'a'
            body = body.replace('id="%s"' % i, 'id="%s_%s"' % (self.uid, i))
            body = body.replace("url(#%s)" % i, "url(#%s_%s)" % (self.uid, i))
            body = re.sub(r'(href)="#%s"' % re.escape(i), r'\1="#%s_%s"' % (self.uid, i), body)
        return self._group(self.key, '<svg x="%g" y="%g" width="%g" height="%g" viewBox="%g %g %g %g" overflow="visible">%s</svg>'
                           % (self.vx, self.vy, self.w, self.h, self.vx, self.vy, self.w, self.h, body))


# ---------------------------------------------------------------------------------------------------------------------------------------
# The project's own parts (what Fritzing's repository does not have), drawn in millimetres, origin at the top-left of the part's box.
# ---------------------------------------------------------------------------------------------------------------------------------------
def pad(x, y, r=1.0):
    """A header pin as Fritzing draws it: a gold ring round a dark hole."""
    return ('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#d8b24a" stroke="#8c6d1d" stroke-width=".22"/><circle cx="%.2f" cy="%.2f" r="%.2f" fill="#222"/>'
            % (x, y, r, x, y, r * .45))


def silk(x, y, s, size=1.5, rot=0, anchor="start", col="#fff", weight="normal"):
    t = ' transform="translate(%.2f %.2f) rotate(%g)"' % (x, y, rot) if rot else ' x="%.2f" y="%.2f"' % (x, y)
    return '<text%s font-size="%s" fill="%s" text-anchor="%s" font-weight="%s" font-family="Helvetica, Arial, sans-serif">%s</text>' % (t, size, col, anchor, weight, s)


class Own(Placed):
    """A part drawn by the project. Subclasses give NAME, W, H (mm), PINS {name: (x, y) mm} and art()."""
    NAME, W, H, PINS = "own", 10, 10, {}
    xmlns = {}

    def __init__(self, cx, cy, rot=0, flip=False, k=MM):
        self._place(cx, cy, rot, flip, k)
        self.vx = self.vy = 0
        self.w, self.h = self.W, self.H

    def local(self, name):
        if name not in self.PINS:
            sys.exit("gen_wiring.py: part '%s' has no pin '%s' (pins: %s)" % (self.NAME, name, ", ".join(self.PINS)))
        return self.PINS[name]

    def svg(self):
        return self._group(self.NAME, self.art(self.uid))


class UlnMotor(Own):
    """A ULN2003 driver board (the blue one sold with the 28BYJ-48) with its motor plugged in: header IN1..IN4 and the '-' '+' power pins on the top edge, the
    motor's 5-wire plug in the socket on the bottom edge, the cable going down to the motor. The motor lies below, shaft up out of the page."""
    NAME, W, H = "uln_motor", 42, 75
    BX = 3          # the board's left edge in the box
    PINS = dict([("IN%d" % (i + 1), (3 + 5 + 2.54 * i, 3.0)) for i in range(4)] + [("minus", (3 + 28.5, 3.0)), ("plus", (3 + 28.5 + 2.54, 3.0))])

    def art(self, u):
        bx, g = self.BX, []
        cx, cy = 21, 60
        g.append('<defs><linearGradient id="%s_pcb" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3b7bc4"/><stop offset="1" stop-color="#2a63a8"/></linearGradient>'
                 '<radialGradient id="%s_can" cx=".4" cy=".35" r=".85"><stop offset="0" stop-color="#f7f7f8"/><stop offset=".55" stop-color="#c3c7cc"/><stop offset="1" stop-color="#8a9097"/></radialGradient>'
                 '<linearGradient id="%s_ear" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#d5d8dc"/><stop offset="1" stop-color="#9aa0a6"/></linearGradient>'
                 '<radialGradient id="%s_brass" cx=".4" cy=".35" r=".8"><stop offset="0" stop-color="#f5e08a"/><stop offset="1" stop-color="#b8922b"/></radialGradient></defs>' % (u, u, u, u))
        # motor 28BYJ-48 (top view): ears, can, off-centre shaft, cable box
        g.append('<rect x="0" y="%.1f" width="42" height="7" rx="3.5" fill="url(#%s_ear)" stroke="#7d838a" stroke-width=".25"/>' % (cy - 3.5, u))
        for dx in (-17.5, 17.5):
            g.append('<circle cx="%.1f" cy="%.1f" r="2.1" fill="%s" stroke="#7d838a" stroke-width=".25"/>' % (cx + dx, cy, BG))
        g.append('<circle cx="%d" cy="%d" r="14" fill="url(#%s_can)" stroke="#6f757c" stroke-width=".35"/>' % (cx, cy, u))
        g.append('<circle cx="%d" cy="%d" r="11.6" fill="none" stroke="#fff" stroke-opacity=".5" stroke-width=".3"/>' % (cx, cy))
        g.append('<circle cx="%d" cy="%d" r="4.8" fill="url(#%s_brass)" stroke="#7a5f14" stroke-width=".3"/><circle cx="%d" cy="%d" r="2.5" fill="#d9c25a" stroke="#7a5f14" stroke-width=".25"/>'
                 % (cx, cy + 6, u, cx, cy + 6))
        # the 5 wires (blue, pink, yellow, orange, red) from the plug down to the motor
        for i, col in enumerate(["blue", "pink", "yellow", "orange", "red"]):
            x1, x2 = cx + (i - 2) * 2.5, cx + (i - 2) * 1.8
            d = "M%.2f 35 C%.2f 40 %.2f 40 %.2f 46" % (x1, x1, x2, x2)
            g.append('<path d="%s" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="1.5"/><path d="%s" fill="none" stroke="%s" stroke-width="1.05"/>' % (d, d, WIRE[col]))
        g.append('<rect x="%d" y="44.6" width="12" height="5.5" rx=".8" fill="#e9ecef" stroke="#9aa0a6" stroke-width=".3"/>' % (cx - 6))
        # the board
        g.append('<rect x="%d" y="0" width="36" height="32" rx="1.6" fill="url(#%s_pcb)" stroke="#1a4f8c" stroke-width=".3"/>' % (bx, u))
        for i in range(4):
            x = bx + 5 + 2.54 * i
            g.append(pad(x, 3.0) + silk(x + .5, 5.2, "IN%d" % (i + 1), 1.5, 90))
        for x, s in ((bx + 28.5, "−"), (bx + 28.5 + 2.54, "+")):
            g.append(pad(x, 3.0) + silk(x, 6.6, s, 2.4, anchor="middle", weight="bold"))
        chip_x = bx + 8.4
        g.append('<rect x="%.1f" y="12" width="19.2" height="6.6" rx=".5" fill="#1d1d1f"/><path d="M%.1f 13.8 a1.5 1.5 0 0 1 0 3z" fill="%s" opacity=".25"/>' % (chip_x, chip_x, "#fff"))
        for i in range(8):
            x = chip_x + 1.2 + 2.54 * i
            g.append('<rect x="%.2f" y="10.6" width="1" height="1.5" fill="#a9aeb3"/><rect x="%.2f" y="18.5" width="1" height="1.5" fill="#a9aeb3"/>' % (x, x))
        g.append(silk(chip_x + 9.6, 16.4, "ULN2003A", 1.9, anchor="middle"))
        for i, ch in enumerate("ABCD"):
            x = bx + 10 + 3.7 * i
            g.append('<circle cx="%.2f" cy="21.6" r="1.15" fill="#e53935" stroke="#7a1512" stroke-width=".2"/><circle cx="%.2f" cy="21.2" r=".4" fill="#fff" opacity=".7"/>' % (x, x) + silk(x, 24.4, ch, 1.2, anchor="middle"))
        g.append('<rect x="%.1f" y="27" width="15" height="5" fill="#f1f1ef" stroke="#a9aeb3" stroke-width=".3"/>' % (cx - 7.5))
        g.append('<rect x="%.1f" y="28.2" width="15" height="7.6" rx=".8" fill="#fafafa" stroke="#9aa0a6" stroke-width=".3"/>' % (cx - 7.5))
        for i in range(5):
            g.append('<rect x="%.2f" y="29.6" width="1" height="3" fill="#c9a24b"/>' % (cx - 5 + 2.5 * i - .5))
        return "".join(g)


class LcdI2c(Own):
    """2x16 LCD with its PCF8574 I2C backpack: blue screen, white characters, the four I2C pins on the left edge (GND, VCC, SDA, SCL from the top)."""
    NAME, W, H = "lcd_i2c", 80, 36
    PINS = dict(GND=(2.2, 14.19), VCC=(2.2, 16.73), SDA=(2.2, 19.27), SCL=(2.2, 21.81))

    def art(self, u):
        g = ['<defs><linearGradient id="%s_scr" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3d63d8"/><stop offset="1" stop-color="#2a49b8"/></linearGradient></defs>' % u]
        g.append('<rect x="0" y="0" width="80" height="36" rx="1" fill="#2f9455" stroke="#1d6b3a" stroke-width=".3"/>')
        for x, y in ((2.5, 2.5), (77.5, 2.5), (2.5, 33.5), (77.5, 33.5)):
            g.append('<circle cx="%s" cy="%s" r="1.6" fill="%s" stroke="#d8b24a" stroke-width=".7"/>' % (x, y, BG))
        g.append('<rect x="7.5" y="4.5" width="69" height="27" rx="1.2" fill="#262626"/>')
        g.append('<rect x="10.5" y="8" width="63" height="20" rx="1" fill="url(#%s_scr)" stroke="#162a74" stroke-width=".3"/>' % u)
        for row in (0, 1):
            for c in range(16):
                g.append('<rect x="%.2f" y="%.2f" width="3.3" height="6.7" fill="#fff" fill-opacity=".07"/>' % (11.6 + 3.8 * c, 9.6 + 8.6 * row))
        for row, txt in ((0, "CoreLR"), (1, "plotter")):
            g.append('<text x="11.9" y="%.1f" font-size="6.1" fill="#fff" font-family="DejaVu Sans Mono, Consolas, monospace" textLength="%.1f" lengthAdjust="spacingAndGlyphs">%s</text>'
                     % (15.7 + 8.6 * row, 3.8 * len(txt) - .5, txt))
        for (n, (x, y)) in self.PINS.items():
            g.append(pad(x, y, .95) + silk(x + 1.5, y + .5, n, 1.25))
        return "".join(g)


class Supply5V(Own):
    """The external 5 V supply: a black box with its two flying leads (red +, black -), which end at the pins 'plus' and 'minus'."""
    NAME, W, H = "supply5v", 44, 34
    PINS = dict(minus=(14, 34), plus=(30, 34))

    def art(self, u):
        g = ['<defs><linearGradient id="%s_box" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4a4f57"/><stop offset="1" stop-color="#25282d"/></linearGradient></defs>' % u]
        for x, col in ((14, "black"), (30, "red")):
            g.append('<path d="M%d 26 V32.5" stroke="#000" stroke-opacity=".35" stroke-width="2.6" stroke-linecap="round"/><path d="M%d 26 V32.5" stroke="%s" stroke-width="1.7" stroke-linecap="round"/>'
                     % (x, x, WIRE[col]))
        g.append('<rect x="0" y="0" width="44" height="27" rx="2.2" fill="url(#%s_box)" stroke="#111" stroke-width=".4"/>' % u)
        g.append(silk(22, 14.5, "5 V", 9.5, anchor="middle", weight="bold"))
        g.append(silk(22, 21, "EXTERNAL SUPPLY", 2.1, anchor="middle", col="#c8ccd2"))
        g.append(silk(14, 25.4, "\u2212", 2.6, anchor="middle", col="#c8ccd2", weight="bold") + silk(30, 25.4, "+", 2.6, anchor="middle", col="#ff8a80", weight="bold"))
        return "".join(g)


OWN = {"uln_motor": UlnMotor, "lcd_i2c": LcdI2c, "supply": Supply5V}


class Scene:
    def __init__(self, w, h, title, desc):
        self.w, self.h, self.title, self.desc = w, h, title, desc
        self.parts, self.wires, self.texts, self.ends, self.paths = [], [], [], [], []

    def part(self, key, cx, cy, tag=None, **kw):
        """Place a part. `tag` names this copy in the netlist ('motor_R'); by default the part's key."""
        p = OWN[key](cx, cy, **kw) if key in OWN else Part(key, cx, cy, **kw)
        p.tag = tag or key
        self.parts.append(p)
        return p

    def wire(self, pts, colour, r=14):
        """Orthogonal wire through the corner points `pts`, corners rounded to radius r."""
        pts = [p for i, p in enumerate(pts) if i == 0 or p != pts[i - 1]]
        self.ends.append((colour, pts[0], pts[-1]))
        self.paths.append((colour, list(pts)))
        d = "M%.1f %.1f" % pts[0]
        for i in range(1, len(pts) - 1):
            (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]
            l1, l2 = math.hypot(x1 - x0, y1 - y0), math.hypot(x2 - x1, y2 - y1)
            rr = min(r, l1 / 2, l2 / 2)
            ax, ay = x1 - (x1 - x0) / l1 * rr, y1 - (y1 - y0) / l1 * rr
            bx, by = x1 + (x2 - x1) / l2 * rr, y1 + (y2 - y1) / l2 * rr
            d += " L%.1f %.1f Q%.1f %.1f %.1f %.1f" % (ax, ay, x1, y1, bx, by)
        d += " L%.1f %.1f" % pts[-1]
        c = WIRE[colour]
        self.wires.append('<path d="%s" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="5.6" stroke-linecap="round" stroke-linejoin="round"/>'
                          '<path d="%s" fill="none" stroke="%s" stroke-width="3.6" stroke-linecap="round" stroke-linejoin="round"/>' % (d, d, c))
        for x, y in (pts[0], pts[-1]):
            self.wires.append('<circle cx="%.1f" cy="%.1f" r="3.4" fill="%s" stroke="#000" stroke-opacity=".45" stroke-width="1"/>' % (x, y, c))

    def down(self, a, b, lane, colour, r=14):
        """Wire from pin a to pin b through the horizontal lane y=`lane` (vertical, horizontal, vertical)."""
        self.wire([a, (a[0], lane), (b[0], lane), b], colour, r)

    def netlist(self):
        """The wires as a set of frozenset({'tag.pin', 'tag.pin'}): each end is matched to the pin of a part it lands on (within 1.5 px); an end that
        lands on no pin is an error (a wire in the air)."""
        pins = []
        for p in self.parts:
            names = p.names if isinstance(p, Part) else p.PINS
            seen = {}
            for n in sorted(names, key=lambda n: (len(n), n)):        # the shortest alias of a pin: 'D13' for 'D13/SCK'
                xy = p.pin(n)
                if not any(abs(xy[0] - q[0]) < .01 and abs(xy[1] - q[1]) < .01 for q in seen):
                    seen[xy] = n
            pins += [("%s.%s" % (p.tag, n), xy) for xy, n in seen.items()]

        def at(xy):
            near = [n for n, q in pins if math.hypot(q[0] - xy[0], q[1] - xy[1]) < 1.5]
            if len(near) != 1:
                sys.exit("gen_wiring.py: a wire end at (%.0f, %.0f) lands on %s" % (xy[0], xy[1], near or "no pin"))
            return near[0]
        return {frozenset((at(a), at(b))) for _, a, b in self.ends}

    def legend(self, x, y, items, title=None):
        """Colour key: [(colour, label)...] as short wire samples."""
        if title:
            self.text(x, y, title, 13, "#2e3440", weight="bold"); y += 22
        for c, label in items:
            self.texts.append('<path d="M%.1f %.1f h34" stroke="#000" stroke-opacity=".35" stroke-width="5.6" stroke-linecap="round"/><path d="M%.1f %.1f h34" stroke="%s" stroke-width="3.6" stroke-linecap="round"/>'
                              % (x + 3, y - 4, x + 3, y - 4, WIRE[c]))
            self.text(x + 50, y, label, 12.5); y += 22
        return y

    def text(self, x, y, s, size=13, colour="#3b4252", anchor="start", weight="normal"):
        self.texts.append('<text x="%.1f" y="%.1f" font-size="%s" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, colour, anchor, weight, s))

    def render(self):
        xm = {}
        for p in self.parts:
            xm.update(p.xmlns)
        xm.pop("xlink", None)
        extra = "".join(' xmlns:%s="%s"' % (k, v) for k, v in sorted(xm.items()))
        return ('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"%s viewBox="0 0 %d %d" width="%d" height="%d" '
                'font-family="Helvetica, Arial, sans-serif">\n'
                '<title>%s</title>\n<desc>%s</desc>\n'
                '<metadata><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:cc="http://creativecommons.org/ns#">'
                '<cc:Work rdf:about=""><dc:title>%s</dc:title><dc:rights>Copyright (C) 2026 ErnstTechnik and the CoreLR contributors. %s</dc:rights>'
                '<cc:license rdf:resource="http://creativecommons.org/licenses/by-sa/3.0/"/></cc:Work></rdf:RDF></metadata>\n'
                '<rect width="%d" height="%d" fill="%s"/>\n%s\n%s\n%s\n</svg>\n'
                % (extra, self.w, self.h, self.w, self.h, self.title, self.desc, self.title, LICENCE_NOTE, self.w, self.h, BG,
                   "\n".join(p.svg() for p in self.parts), "\n".join(self.wires), "\n".join(self.texts)))


# ---------------------------------------------------------------------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------------------------------------------------------------------
def nano_led():
    """The smallest case: a LED on D13 through a 220 ohm resistor (the style check)."""
    s = Scene(860, 640, "Arduino Nano with a LED on D13", "LED on D13 through a 220 ohm resistor, cathode to GND (style reference diagram).")
    s.text(24, 34, "Arduino Nano + LED on D13", 17, "#2e3440", weight="bold")
    s.text(24, 54, "LED through 220 Ω, cathode to GND", 12, "#6b7280")
    nano = s.part("nano", 430, 180, rot=90)             # USB to the left; top row D12..TX1, bottom row D13..VIN
    led = s.part("led", 470, 330, flip=True)            # flipped: anode on the left, so the wires do not cross
    ty = led.pin("anode")[1]
    res = s.part("res220", 330, ty)                     # same height as the LED's leg tips
    d13, gnd = nano.pin("D13"), nano.pin("GND#2")
    ra, rb = res.pin("connector0"), res.pin("connector1")
    if ra[0] > rb[0]:
        ra, rb = rb, ra
    s.wire([d13, (d13[0], ty), ra], "green")
    s.wire([rb, led.pin("anode")], "green")
    cat = led.pin("cathode")
    s.wire([cat, (cat[0], ty + 46), (gnd[0], ty + 46), gnd], "black")
    s.text(res.cx, ty + 36, "220 Ω", 12, anchor="middle")
    s.text(led.cx + 40, led.cy - 40, "LED", 12)
    return s


def parts():
    """The project's own parts, side by side (to judge the drawing)."""
    s = Scene(1500, 900, "The project's own parts", "ULN2003 board with 28BYJ-48, I2C LCD, external supply.")
    s.part("uln_motor", 260, 420)
    s.part("lcd_i2c", 900, 200)
    s.part("supply", 1250, 220)
    s.part("uln_motor", 760, 640, rot=180)
    return s


# Colours of the signal wires, by group. IN1..IN4 follow the 28BYJ-48's own wire colours (PinMapping.md lists them); the other groups reuse colours
# because the groups are far apart on the drawing: each diagram has its own key.
IN_COL = ["blue", "pink", "yellow", "orange"]


def nano_at(s, cx, cy):
    """The Nano as every diagram places it: USB to the right, D0..D12 on the bottom edge, D13 and the analog pins and 5V/GND on the top edge."""
    return s.part("nano", cx, cy, rot=270)


def title(s, head, sub):
    s.text(24, 34, head, 17, "#2e3440", weight="bold")
    s.text(24, 54, sub, 12, "#6b7280")


def fmt_pins(pins):
    """['D2', 'D3', 'D4', 'D5'] -> 'D2-D5'; any other list is written out."""
    nums = [p[1:] for p in pins]
    if len({p[0] for p in pins}) == 1 and all(n.isdigit() for n in nums) and [int(n) for n in nums] == list(range(int(nums[0]), int(nums[0]) + len(nums))):
        return "%s\u2013%s" % (pins[0], pins[-1])
    return ", ".join(pins)


def rank(values, reverse=False):
    """The position of each value in the sorted list (ties by index): rank([30, 10, 20]) = [2, 0, 1]."""
    order = sorted(range(len(values)), key=lambda i: (values[i], i), reverse=reverse)
    out = [0] * len(values)
    for r, i in enumerate(order):
        out[i] = r
    return out


def add_motors(s, nano, top, mp):
    """Both motors, IN1..IN4 in order (STEPPER_MOTOR_R_PIN0..3, STEPPER_MOTOR_L_PIN0..3), wherever HwConfig.h puts them. R's board is on the left, L's on the right;
    the lanes follow the pins' positions, so that no wire starts under another. Returns (R, L)."""
    yb = nano.pin("D2")[1]
    mr, ml = s.part("uln_motor", 0, 0, tag="motor_R"), s.part("uln_motor", 0, 0, tag="motor_L")
    cy = top + mr.H / 2 * MM
    xr, xl = [nano.pin(p)[0] for p in mp["R"]], [nano.pin(p)[0] for p in mp["L"]]
    mr.cx, mr.cy = min(xr) - 183 + (mr.w / 2 - 8) * MM, cy                              # R's IN1 183 px left of its leftmost pin
    ml.cx, ml.cy = max(xl) + 190 + (ml.w / 2 - 8) * MM, cy                              # L's IN1 190 px right of its rightmost pin
    for i, (rr, rl) in enumerate(zip(rank(xr), rank(xl, reverse=True))):
        s.down(nano.pin(mp["R"][i]), mr.pin("IN%d" % (i + 1)), yb + 40 + 22 * rr, IN_COL[i])      # leftwards: the rightmost source is the deepest lane
        s.down(nano.pin(mp["L"][i]), ml.pin("IN%d" % (i + 1)), yb + 40 + 22 * rl, IN_COL[i])      # rightwards: the leftmost source is the deepest
    s.text(mr.cx - 20 * MM, cy + mr.H / 2 * MM + 26, "Motor R  (%s)" % fmt_pins(mp["R"]), 15, "#2e3440", weight="bold")
    s.text(ml.cx - 20 * MM, cy + ml.H / 2 * MM + 26, "Motor L  (%s)" % fmt_pins(mp["L"]), 15, "#2e3440", weight="bold")
    return mr, ml


def add_motor_power(s, sup, mr, ml, lane_hi, lane_lo):
    """The external supply to both boards. L is reached from the right, directly; R by the right side, under the modules and up the gap between them."""
    ya, yb_ = lane_hi, lane_lo                         # minus higher than plus: the two wires do not cross
    s.down(sup.pin("minus"), ml.pin("minus"), ya, "black")
    s.down(sup.pin("plus"), ml.pin("plus"), yb_, "red")
    gx = (mr.cx + ml.cx) / 2 + 20
    ybot = ml.cy + ml.H / 2 * MM + 60
    m, p = sup.pin("minus"), sup.pin("plus")
    s.wire([m, (m[0], ybot), (gx + 22, ybot), (gx + 22, ya), (mr.pin("minus")[0], ya), mr.pin("minus")], "black")
    s.wire([p, (p[0], ybot + 40), (gx - 22, ybot + 40), (gx - 22, yb_), (mr.pin("plus")[0], yb_), mr.pin("plus")], "red")


def motors(mp=None):
    mp = mp or machine_pins()
    s = Scene(1950, 1260, "Motors R and L", "Two 28BYJ-48 motors through their ULN2003 boards, powered by the external 5 V supply.")
    title(s, "Motors R and L: ULN2003 boards + 28BYJ-48",
          "Motor R on %s, motor L on %s, IN1 to IN4 in order (HwConfig.h: STEPPER_MOTOR_R_PIN0..3, STEPPER_MOTOR_L_PIN0..3)" % (fmt_pins(mp["R"]), fmt_pins(mp["L"])))
    nano = nano_at(s, 900, 180)
    mr, ml = add_motors(s, nano, 413, mp)
    sup = s.part("supply", 1640, 190)
    yh = mr.pin("plus")[1]
    add_motor_power(s, sup, mr, ml, yh - 45, yh - 30)
    # the common ground: the supply's minus to a Nano GND, over the top
    m, g = sup.pin("minus"), nano.pin("GND#2")
    s.wire([m, (m[0], m[1] + 16), (1235, m[1] + 16), (1235, 40), (g[0], 40), g], "black")
    p, n5 = sup.pin("plus"), nano.pin("5V")                                    # and its plus to the 5V pin: the Nano is fed from it (USB not connected in SD mode)
    s.wire([p, (p[0], p[1] + 22), (1880, p[1] + 22), (1880, 14), (n5[0], 14), n5], "red")
    s.legend(24, 1090, [("blue", "IN1  (motor wire blue)"), ("pink", "IN2  (pink)"), ("yellow", "IN3  (yellow)"), ("orange", "IN4  (orange)"),
                        ("red", "+5 V from the external supply (also to the Nano's 5V pin)"), ("black", "ground, common with the Nano")], "Wires")
    s.text(24, 1250, "The external supply feeds the motors and the Nano's 5V pin (in SD mode the USB port is not connected); its ground is the Nano's.", 12.5, "#2e3440")
    return s


def add_servo(s, nano, cx, cy, mp, lane=None):
    """The pen servo: signal on PENCONTROL_SERVO_PIN. Its +5 V and ground come from the external supply, wired by the caller. Returns the servo."""
    sv = s.part("servo", cx, cy, rot=90)               # its three wires leave at the bottom: gnd, vcc, pulse from left to right
    a0 = nano.pin(mp["servo"])
    if lane is None:                                   # straight down: the servo sits right above its pin
        sv.cx += a0[0] - sv.pin("pulse")[0]
        lane = a0[1] - 30
    tp = sv.pin("pulse")
    s.wire([tp, (tp[0], lane), (a0[0], lane), a0], "white")
    s.text(sv.cx - 90, sv.cy - 205, "Pen servo  (%s)" % mp["servo"], 15, "#2e3440", weight="bold")
    return sv


def pen_servo(mp=None):
    mp = mp or machine_pins()
    s = Scene(1500, 900, "Pen servo", "The pen servo, powered by the external 5 V supply.")
    title(s, "Pen servo", "Signal on %s (HwConfig.h: PENCONTROL_SERVO_PIN); +5 V and ground from the external supply" % mp["servo"])
    nano = nano_at(s, 1000, 560)
    sv = add_servo(s, nano, 0, 235, mp)
    sup = s.part("supply", 400, 330)
    sg, sv5 = sv.pin("gnd"), sv.pin("vcc")
    m, p = sup.pin("minus"), sup.pin("plus")
    s.wire([sg, (sg[0], sg[1] + 16), (m[0], sg[1] + 16), m], "black")                      # leftwards: the rightmost source (vcc) is deeper
    s.wire([sv5, (sv5[0], sv5[1] + 36), (p[0], sv5[1] + 36), p], "red")
    g = nano.pin("GND")
    s.wire([m, (m[0], 690), (g[0], 690), g], "black")                                       # the common ground, to the Nano's bottom GND
    n5, yt = nano.pin("5V"), nano.pin("A0")[1]                                              # and the plus to the Nano's 5V pin (USB not connected in SD mode)
    s.wire([p, (p[0], p[1] + 30), (760, p[1] + 30), (760, yt - 24), (n5[0], yt - 24), n5], "red")
    s.legend(24, 780, [("white", "servo signal"), ("red", "+5 V from the external supply (also to the Nano's 5V pin)"), ("black", "ground, common with the Nano")], "Wires")
    return s


def add_joystick(s, nano, cx, cy, mp, lane0=None):
    """The joystick: VRx (HORZ), VRy (VERT), SW (SEL) on LCDJOYSTICKUI_X_PIN, _Y_PIN, _SW_PIN; +5 V and GND from the Nano."""
    j = s.part("joystick", cx, cy)
    yt = nano.pin("A0")[1] if lane0 is None else lane0 + 30
    plan = [("GND", "GND#2", "black"), ("SEL", mp["SEL"], "grey"), ("HORZ", mp["HORZ"], "green"), ("VERT", mp["VERT"], "brown"), ("VCC", "5V", "red")]
    for (jp, np_, col), k in zip(plan, rank([j.pin(q[0])[0] for q in plan], reverse=True)):   # the rightmost source takes the deepest lane (no wire passes over another's start)
        s.down(j.pin(jp), nano.pin(np_), yt - 30 - 20 * k, col)
    s.text(j.cx - 115, j.cy - 160, "Joystick", 15, "#2e3440", weight="bold")
    return j


def joystick(mp=None):
    mp = mp or machine_pins()
    s = Scene(1500, 760, "Joystick", "The 2-axis joystick with its push button.")
    title(s, "Joystick", "VRx on %s, VRy on %s, SW on %s (HwConfig.h: LCDJOYSTICKUI_X_PIN, _Y_PIN, _SW_PIN); powered from the Nano's 5V" % (mp["HORZ"], mp["VERT"], mp["SEL"]))
    nano = nano_at(s, 900, 560)
    add_joystick(s, nano, 1090, 300, mp)
    s.legend(24, 560, [("green", "VRx (HORZ) \u2192 %s" % mp["HORZ"]), ("brown", "VRy (VERT) \u2192 %s" % mp["VERT"]),
                       ("grey", "SW (SEL) \u2192 %s, pull-up in software" % mp["SEL"]), ("red", "+5 V from the Nano"), ("black", "ground")], "Wires")
    return s


def add_lcd(s, nano, cx, cy, mp, lane0=None):
    """The LCD with its I2C backpack: SDA and SCL on the Nano's TWI pins, powered from the Nano. Address LCDJOYSTICKUI_I2C_ADDRESS."""
    lcd = s.part("lcd_i2c", cx, cy)
    left = lcd.pin("GND")[0] - 2.2 * MM
    yt = nano.pin("A0")[1] if lane0 is None else lane0 + 28
    plan = [("GND", "GND#2", "black"), ("VCC", "5V", "red"), ("SDA", mp["SDA"], "cyan"), ("SCL", mp["SCL"], "purple")]          # in the header's order, top to bottom
    for i, (lp, np_, col), k in zip(range(4), plan, rank([nano.pin(q[1])[0] for q in plan])):                                  # the leftmost source takes the deepest lane
        a, b = nano.pin(np_), lcd.pin(lp)
        lane, xk = yt - 28 - 18 * k, left - 60 + 15 * i
        s.wire([a, (a[0], lane), (xk, lane), (xk, b[1]), b], col)
    s.text(lcd.cx - 40 * MM, lcd.cy - 18 * MM - 16, "LCD 2\u00d716 + I\u00b2C backpack  (%s = SDA, %s = SCL)" % (mp["SDA"], mp["SCL"]), 15, "#2e3440", weight="bold")
    return lcd


def lcd_i2c(mp=None):
    mp = mp or machine_pins()
    s = Scene(1500, 840, "LCD I2C", "The 2x16 LCD with its I2C backpack.")
    title(s, "LCD 2\u00d716 on I\u00b2C", "SDA on %s, SCL on %s (fixed silicon pins, from the Arduino core), address 0x3F (HwConfig.h: LCDJOYSTICKUI_I2C_ADDRESS); powered from the Nano's 5V" % (mp["SDA"], mp["SCL"]))
    nano = nano_at(s, 1000, 640)
    add_lcd(s, nano, 560, 260, mp)
    s.legend(24, 560, [("cyan", "SDA \u2192 %s" % mp["SDA"]), ("purple", "SCL \u2192 %s" % mp["SCL"]), ("red", "+5 V from the Nano"), ("black", "ground")], "Wires")
    return s


def add_sd(s, nano, cx, cy, mp, lane=None):
    """The SD card module on the hardware SPI: CS on SDREADER_CS_PIN, MOSI, MISO, SCK on the SPI pins of the Arduino core; powered from the Nano's 5V.
    Rotated: its pins face the Nano."""
    sd = s.part("microsd", cx, cy, rot=270)            # the header on the left edge; from the top: GND VCC MISO MOSI SCK CS
    plan = [("CS", mp["CS"], "grey"), ("MOSI", mp["MOSI"], "green"), ("MISO", mp["MISO"], "white")]
    xs = [nano.pin(q[1])[0] for q in plan]
    for (sp, np_, col), k in zip(plan, rank(xs)):
        a, b = nano.pin(np_), sd.pin(sp)
        if lane is None:
            s.wire([a, (a[0], b[1]), b], col)
        else:                                          # rightwards, the leftmost source the deepest lane; then up the right of the USB socket, the deepest one the farthest right
            xr = max(xs) + 130 - 22 * k
            s.wire([a, (a[0], lane + 22 * (2 - k)), (xr, lane + 22 * (2 - k)), (xr, b[1]), b], col)
    d13, sck = nano.pin(mp["SCK"]), sd.pin("SCK")
    xr = max(max(xs), d13[0]) + 150                    # round the USB socket
    s.wire([d13, (d13[0], d13[1] - 28), (xr, d13[1] - 28), (xr, sck[1]), sck], "yellow")
    s.text(sd.cx - 110, sd.cy + 140, "SD card module  (SPI)", 15, "#2e3440", weight="bold")
    return sd


def sd_card(mp=None):
    mp = mp or machine_pins()
    s = Scene(1500, 760, "SD card", "The SD card module on the hardware SPI.")
    title(s, "SD card module (SD variant)", "CS on %s (HwConfig.h: SDREADER_CS_PIN), MOSI %s, MISO %s, SCK %s: the SPI pins are fixed silicon (read from the Arduino core)" % (mp["CS"], mp["MOSI"], mp["MISO"], mp["SCK"]))
    nano = nano_at(s, 800, 300)
    sd = add_sd(s, nano, 1320, 440, mp)
    g, v = sd.pin("GND"), sd.pin("VCC")
    n5, ng = nano.pin("5V"), nano.pin("GND#2")
    s.wire([v, (v[0] - 40, v[1]), (v[0] - 40, 70), (n5[0], 70), n5], "red")
    s.wire([g, (g[0] - 70, g[1]), (g[0] - 70, 50), (ng[0], 50), ng], "black")
    s.legend(24, 560, [("grey", "CS \u2192 %s" % mp["CS"]), ("green", "MOSI \u2192 %s" % mp["MOSI"]), ("white", "MISO \u2192 %s" % mp["MISO"]), ("yellow", "SCK \u2192 %s" % mp["SCK"]),
                       ("red", "+5 V from the Nano"), ("black", "ground")], "Wires")
    return s


def overview(mp=None):
    mp = mp or machine_pins()
    s = Scene(2340, 2000, "General wiring", "The whole machine: Nano, two ULN2003 boards with their 28BYJ-48, pen servo, joystick, LCD, SD card module and the external supply, with a Schottky diode on the Nano's 5V.")
    title(s, "CoreLR wiring overview  (variant: a Schottky diode in series with the Nano's 5V)", "Every wire of HwConfig.h in one picture; each subsystem is also drawn alone, as built (no diode). The SD module is the SD variant's (CORELR_JOB_SOURCE = sd).")
    nano = nano_at(s, 1100, 900)
    top, bot = nano.pin("A0")[1], nano.pin("D2")[1]
    mr, ml = add_motors(s, nano, bot + 164, mp)                               # below
    add_lcd(s, nano, 530, 330, mp, lane0=top - 28)                            # upper left
    add_joystick(s, nano, 1230, 470, mp, lane0=top - 28 - 18 * 4 - 8)         # upper right
    sv = add_servo(s, nano, 1620, 470, mp, lane=top - 86)
    sd = add_sd(s, nano, 1640, 880, mp, lane=bot + 18)                        # right
    g, v = sd.pin("GND"), sd.pin("VCC")
    n5, ng = nano.pin("5V"), nano.pin("GND#2")
    xp = 1400
    s.wire([v, (xp, v[1]), (xp, 60), (n5[0], 60), n5], "red")
    s.wire([g, (xp + 25, g[1]), (xp + 25, 40), (ng[0], 40), ng], "black")
    # the external supply: the servo, then both boards (and, in motors(), the common ground)
    sup = s.part("supply", 1980, 640)
    m, p = sup.pin("minus"), sup.pin("plus")
    sg, s5 = sv.pin("gnd"), sv.pin("vcc")
    ylo = sup.cy + 12 * MM
    s.wire([sg, (sg[0], ylo + 24), (m[0], ylo + 24), m], "black")
    s.wire([s5, (s5[0], ylo + 6), (p[0], ylo + 6), p], "red")
    yh = mr.pin("plus")[1]
    add_motor_power(s, sup, mr, ml, yh - 45, yh - 30)
    s.series_diode = True                                                                        # the variant drawn here: a Schottky diode between the supply and the Nano's 5V pin
    dio = s.part("diode", 2200, 330, rot=90, scale=2 * SCALE)                                    # drawn twice the size of the other parts, to be seen; cathode (the band) up, towards the Nano
    an, ca = dio.pin("anode"), dio.pin("cathode")
    s.wire([p, (p[0], p[1] + 22), (an[0], p[1] + 22), an], "red")
    s.wire([ca, (ca[0], 28), (n5[0], 28), n5], "red")
    s.texts.append('<rect x="%.0f" y="%.0f" width="70" height="%.0f" rx="14" fill="#ffe24d" fill-opacity=".28" stroke="#c9a000" stroke-width="2" stroke-dasharray="7 5"/>'
                   % (an[0] - 35, ca[1] - 26, an[1] - ca[1] + 52))
    s.text(an[0] - 48, (an[1] + ca[1]) / 2 - 8, "Schottky diode 1N5817 (proposed)", 14, "#2e3440", anchor="end", weight="bold")
    s.text(an[0] - 48, (an[1] + ca[1]) / 2 + 12, "the USB 5 V and the external 5 V can no longer feed each other", 12, "#6b7280", anchor="end")
    s.wire([m, (m[0], m[1] + 44), (2260, m[1] + 44), (2260, 12), (ng[0], 12), ng], "black")    # the common ground: the supply's minus to the Nano, over the top
    s.legend(24, 1760, [("blue", "IN1  (motor wire blue)"), ("pink", "IN2  (pink)"), ("yellow", "IN3  (yellow)"), ("orange", "IN4  (orange)"),
                        ("red", "+5 V (the external supply; to the Nano's 5V pin through the diode)"), ("black", "ground (common)")], "Wires")
    s.text(24, 1930, "In SD mode the Nano's USB port is not connected: the Nano, the motors and the servo run from the external 5 V; the Nano from the same 5 V less the diode's drop (about 0.3 to 0.45 V); the LCD, the joystick and the SD module from the Nano's 5V pin. With the USB connected, whichever 5 V is higher feeds the Nano.", 12.5, "#2e3440")
    return s


# ---------------------------------------------------------------------------------------------------------------------------------------
# The machine's pins and the verification of the drawn wires
# ---------------------------------------------------------------------------------------------------------------------------------------
CORE_PINS_H = os.path.join(ROOT, "third_party", "arduino-core", "variants", "standard", "pins_arduino.h")      # the Nano's variant includes it
BOTTOM_ROW = {"D%d" % i for i in range(13)}                       # the Nano as the diagrams draw it: D0..D12 on the bottom edge,
TOP_ROW = {"D13"} | {"A%d" % i for i in range(8)}                 # D13 and A0..A7 on the top edge


def hwconfig():
    """The numeric/pin #defines of firmware/config/HwConfig.h."""
    with open(os.path.join(ROOT, "firmware", "config", "HwConfig.h"), encoding="utf-8") as f:
        return dict(re.findall(r"^#define\s+(\w+)\s+(\w+)", f.read(), re.M))


def core_pins():
    """The pins the silicon fixes, from the Arduino core the firmware is built with: PIN_SPI_MOSI/MISO/SCK, PIN_WIRE_SDA/SCL."""
    with open(CORE_PINS_H, encoding="utf-8") as f:
        d = dict(re.findall(r"^#define\s+(PIN_SPI_\w+|PIN_WIRE_\w+)\s+\((\d+)\)", f.read(), re.M))
    need = ("PIN_SPI_MOSI", "PIN_SPI_MISO", "PIN_SPI_SCK", "PIN_WIRE_SDA", "PIN_WIRE_SCL")
    if any(k not in d for k in need):
        sys.exit("gen_wiring.py: %s lacks %s" % (CORE_PINS_H, [k for k in need if k not in d]))
    return d


def nano_pin(v):
    """A pin as the C sources write it ('5', '14', 'A1') -> the Nano's silkscreen name ('D5', 'A0', 'A1')."""
    if v.startswith("A"):
        return v
    n = int(v)
    return "D%d" % n if n < 14 else "A%d" % (n - 14)


def machine_pins(hw=None, core=None):
    """Every pin the diagrams draw, by function, as the Nano's silkscreen names: the configurable ones from HwConfig.h, the fixed ones from the Arduino core.
    Stops, with the reason, if two functions share a pin (the firmware's own guard says the same for a motor wire or the chip select on an SPI pin) or if a pin is on
    the other edge than the diagrams' layout supports."""
    hw, core = hw or hwconfig(), core or core_pins()
    mp = {"R": [nano_pin(hw["STEPPER_MOTOR_R_PIN%d" % i]) for i in range(4)], "L": [nano_pin(hw["STEPPER_MOTOR_L_PIN%d" % i]) for i in range(4)],
          "servo": nano_pin(hw["PENCONTROL_SERVO_PIN"]), "HORZ": nano_pin(hw["LCDJOYSTICKUI_X_PIN"]), "VERT": nano_pin(hw["LCDJOYSTICKUI_Y_PIN"]),
          "SEL": nano_pin(hw["LCDJOYSTICKUI_SW_PIN"]), "CS": nano_pin(hw["SDREADER_CS_PIN"]),
          "MOSI": nano_pin(core["PIN_SPI_MOSI"]), "MISO": nano_pin(core["PIN_SPI_MISO"]), "SCK": nano_pin(core["PIN_SPI_SCK"]),
          "SDA": nano_pin(core["PIN_WIRE_SDA"]), "SCL": nano_pin(core["PIN_WIRE_SCL"])}
    uses = {}
    for k, v in mp.items():
        for i, p in enumerate(v if isinstance(v, list) else [v]):
            uses.setdefault(p, []).append(k + (str(i) if isinstance(v, list) else ""))
    twice = {p: u for p, u in uses.items() if len(u) > 1}
    if twice:
        sys.exit("gen_wiring.py: pin(s) used twice: %s" % "; ".join("%s by %s" % (p, " and ".join(u)) for p, u in sorted(twice.items())))
    bottom = mp["R"] + mp["L"] + [mp["CS"], mp["MOSI"], mp["MISO"]]
    top = [mp["servo"], mp["HORZ"], mp["VERT"], mp["SEL"], mp["SCK"], mp["SDA"], mp["SCL"]]
    bad = [p for p in bottom if p not in BOTTOM_ROW] + [p for p in top if p not in TOP_ROW]
    if bad:
        sys.exit("gen_wiring.py: the diagrams draw the motors and the SD module's CS/MOSI/MISO on D0..D12 and the servo, the joystick, SCK, SDA and SCL on D13 / A0..A7; %s is elsewhere" % bad)
    return mp


def expected_nets(hw=None, diode=False, core=None):
    """The electrical nets the machine needs, as sets of 'tag.pin': one per signal (from HwConfig.h and the Arduino core), the +5 V and the ground.
    The external supply feeds the boards AND the Nano's 5V pin (maintainer, 2026-10-14: in SD mode the USB port is not connected); the Nano's two GND pins are one net."""
    mp = machine_pins(hw, core)
    nets = []
    for m in "RL":
        for i in range(4):
            nets.append({"nano." + mp[m][i], "motor_%s.IN%d" % (m, i + 1)})
    nets.append({"nano." + mp["servo"], "servo.pulse"})
    for k in ("HORZ", "VERT", "SEL"):
        nets.append({"nano." + mp[k], "joystick." + k})
    for k in ("CS", "MOSI", "MISO", "SCK"):
        nets.append({"nano." + mp[k], "microsd." + k})
    nets += [{"nano." + mp["SDA"], "lcd_i2c.SDA"}, {"nano." + mp["SCL"], "lcd_i2c.SCL"}]
    if diode:       # a Schottky diode, anode on the supply and cathode on the Nano's 5V pin: the two 5 V rails are no longer one net, the diode joins them one way
        nets.append({"supply.plus", "motor_R.plus", "motor_L.plus", "servo.vcc", "diode.anode"})
        nets.append({"nano.5V", "joystick.VCC", "lcd_i2c.VCC", "microsd.VCC", "diode.cathode"})
    else:
        nets.append({"supply.plus", "nano.5V", "motor_R.plus", "motor_L.plus", "servo.vcc", "joystick.VCC", "lcd_i2c.VCC", "microsd.VCC"})
    nets.append({"supply.minus", "nano.GND", "nano.GND#2", "motor_R.minus", "motor_L.minus", "servo.gnd", "joystick.GND", "lcd_i2c.GND", "microsd.GND"})
    return nets


def components(links, ties=()):
    """Connected components (a list of sets of pin names) of a set of 2-pin links, plus pins tied on a board."""
    comp = {}
    def find(x):
        while comp.setdefault(x, x) != x:
            comp[x] = comp[comp[x]]; x = comp[x]
        return x
    for l in list(links) + [frozenset(t) for t in ties]:
        a, b = sorted(l)
        comp[find(a)] = find(b)
    groups = {}
    for x in list(comp):
        groups.setdefault(find(x), set()).add(x)
    return list(groups.values())


def verify_scene(scene, hw=None, core=None):
    """Problems found in a scene's wiring (a list of strings; empty = it is what HwConfig.h and PinMapping.md say): a short between two nets, a net not closed,
    a wire to a pin that belongs to no net of the machine."""
    nets = expected_nets(hw, getattr(scene, "series_diode", False), core)
    present = set()
    for p in scene.parts:
        present |= {"%s.%s" % (p.tag, n) for n in (p.names if isinstance(p, Part) else p.PINS)}
    ties = [("nano.GND", "nano.GND#2")] if "nano.GND" in present else []
    comps = components(scene.netlist(), ties)
    out = []
    for c in comps:
        hit = [i for i, n in enumerate(nets) if n & c]
        if not hit:
            out.append("a wire to pin(s) outside the machine's nets: %s" % ", ".join(sorted(c)))
        elif len(hit) > 1:
            out.append("short between %s" % " and ".join("{%s}" % ", ".join(sorted(nets[i] & present)) for i in hit))
    for n in nets:
        pins = n & present
        if len(pins) >= 2 and not any(pins <= c for c in comps):
            parts = [sorted(pins & c) for c in comps if pins & c]
            rest = sorted(pins - set().union(*comps))
            out.append("net not closed: %s" % " | ".join(", ".join(x) for x in parts + ([rest] if rest else [])))
    return out


SCENES = {"nano_led": nano_led, "motors": motors, "pen_servo": pen_servo, "joystick": joystick, "lcd_i2c": lcd_i2c, "sd_card": sd_card, "overview": overview, "parts": parts}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("\n\n", 2)[2])
    ap.add_argument("diagram", nargs="?", help="the scene to draw (see --list)")
    ap.add_argument("-o", "--output", help="output file (default: docs/hardware/images/wiring/<diagram>.svg)")
    ap.add_argument("--all", action="store_true", help="draw every scene that is part of the documentation (not 'parts')")
    ap.add_argument("--list", action="store_true", help="list the scenes and the vendored parts")
    ap.add_argument("--verify", action="store_true", help="check every wire of every scene against firmware/config/HwConfig.h (exit status = the number of problems); writes nothing")
    a = ap.parse_args()
    if a.list:
        print("scenes:", ", ".join(sorted(SCENES)))
        print("parts (Fritzing):", ", ".join(sorted(PARTS)))
        print("parts (own):     ", ", ".join(sorted(OWN)))
        return 0
    if a.verify:
        bad = 0
        for n in [n for n in SCENES if n not in ("parts", "nano_led")]:
            problems = verify_scene(SCENES[n]())
            print("%-10s %s" % (n, "ok" if not problems else "%d problem(s)" % len(problems)))
            for pr in problems:
                print("   ", pr)
            bad += len(problems)
        return bad
    names = [n for n in SCENES if n != "parts"] if a.all else [a.diagram]
    for n in names:
        if n not in SCENES:
            ap.error("give a scene: %s" % ", ".join(sorted(SCENES)))
        out = a.output if (a.output and not a.all) else os.path.join(OUT_DIR, n + ".svg")
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        svg = SCENES[n]().render()
        ET.fromstring(svg.encode("utf-8"))               # well-formed, or we stop here
        with open(out, "w", encoding="utf-8") as f:
            f.write(svg)
        print("wrote", os.path.relpath(out, ROOT) if out.startswith(ROOT) else out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
