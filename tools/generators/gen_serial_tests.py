#!/usr/bin/env python3
#
# gen_serial_tests.py
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
"""
gen_serial_tests.py -- writes the GCode files to stream from a sender (Candle 1.1.9) to the serial variant: what the protocol accepts, what it refuses and
with which `error:N`, the arcs and their direction, the origin words, the pen words, the program end, comments, long lines, and a real drawing.

The contract is docs/protocol/SerialProtocol.md (6.4: the command set; 4.6: the error numbers). The files are GEOMETRY-SPECIFIC only where the travel zone
decides (REQ-CoreLR-F-Motion-0008): the machine is given as `--geometry D,L` (the motor distance and the reference cable length "Calibrate machine" was told,
the drawing origin being where calibration leaves it) or `D,OX,OY` (mm). Everything else is written around the origin, inside 110 mm of it.

Every file starts with `M5` then `G90 G0 X0 Y0` (a serial session may have left the machine in G91, with the pen down) and ends pen up at the origin, then `M30`,
except where the file tests the opposite and says so. A REFUSAL file holds ONE refused line: a refusal during a job halts it (`Alarm`), and Candle then
offers its dialog. The file's own header says which `error:N` to expect; the same table is in README.md, written next to the files.

  20_Accept_Words.gc      the words the machine accepts, lower case, no spaces, `N` `F` `S` words, modal bare coordinates, G90/G91 on a line with a move
  21_Accept_Arcs.gc       G2/G3 and their direction (Y up the wall): from (0,0) to (40,0) with I20, G2 passes ABOVE, G3 BELOW; a full circle; relative I/J under G91
  22_Accept_Origin.gc     G92 and G10 L20 P1 move the origin; the file puts it back where it was (REQ-CoreLR-F-GCodeInterpreter-0017)
  23_Accept_G53.gc        G53: a move in the machine frame; Candle's Restore origin and Safe position
  24_Accept_PenWords.gc   M3/M5 and the Z word (serial variant only; the Z threshold is still OPEN, P7)
  25_Accept_ProgramEnd.gc M2 ends a job, resets the modal state; the next line starts a new job
  26_Accept_Comments.gc   `;` and `( )` comments, tabs, blanks, text that looks like replies
  27_Accept_LongLines.gc  80 significant characters exactly, six decimals, arcs, comments making the raw line long (serial variant only)
  30..51_Refuse_*.gc      one refused line each (see README.md for the table)
  60_Drawing_M3M5.gc      a drawing (house, sun, tree) in the style of a CAM export: M3/M5
  61_Drawing_ZPen.gc      the same drawing, the pen lifted with Z words

Check them with: tools/bench/gcode_check.sh --limit 1000 --geometry D,L FILES (the firmware's real parser and planner), or tests/host/check_serial_files.sh.
"""
import argparse, math, os, sys, textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gcode as g   # the number format, the geometry parser, the zone

num = g.num

def fmt6(v):
    s = ("%.6f" % v)
    return "0.000000" if s in ("-0.000000",) else s

class F:
    """One file. `raw=True` lets a line exceed the 47 characters of the SD variant (these files are for the serial link)."""
    def __init__(self, title, start=True):
        self.out = []
        for l in title:
            self.c(l)
        if start:
            self.out += ["M5", "G90 G0 X0 Y0", "G21 G17 G54 G94"]
    def add(self, *lines):
        self.out += list(lines)
        return self
    def c(self, text):
        for piece in (textwrap.wrap(text, 45) or [""]):     # a comment stays within 45 characters: the file also fits the SD variant's line buffer
            self.out.append("; " + piece)
        return self
    def finish(self, tail=True):
        if tail:
            self.out += ["M5", "G90 G0 X0 Y0", "M30"]
        return "\n".join(self.out) + "\n"

# ----------------------------------------------------------------------------------------------------------------------------------------------------------
def accept_words():
    f = F(["ACCEPTED: the words of the command set (contract 6.4). Pen up", "throughout; the head draws nothing. Every line must be", "answered ok; the file ends at the origin, Idle."])
    f.c("G0 and G1, spaced and not, upper and lower case")
    f.add("G0 X10 Y10", "g1 x20 y10 f600", "G1X20Y20", "g1x10y20")
    f.c("modal: bare coordinates continue the last motion word")
    f.add("X0 Y0", "X5", "Y5")
    f.c("N, F and S words are read and ignored")
    f.add("N10 G1 X10 Y10 F300", "N20 G1 X0 Y0 F1200 S500")
    f.c("G91 on a line with a move: both take effect, in order")
    f.add("G91 G1 X10 Y10", "G1 X-10", "G1 Y-10", "G90")
    f.c("G17 G21 G54 G94: accepted, no effect")
    f.add("G17", "G21", "G54", "G94", "G21 G54 G94 G17 G90")
    f.c("a line with only tolerated words (Candle sends S alone)")
    f.add("S100", "F600", "N30")
    f.c("an empty line is answered ok")
    f.add("", "   ")
    return f.finish()

def accept_arcs():
    f = F(["ACCEPTED: arcs and their direction, Y UP the wall (D1).", "Pen DOWN, so the paper shows it: from (0,0) to (40,0) with", "I20, G2 (clockwise) must pass ABOVE the start, G3 BELOW.", "Expected ink: an S-like figure, upper half then lower half."])
    f.add("M3")
    f.c("G2: clockwise, passes above (+Y)")
    f.add("G2 X40 Y0 I20 J0")
    f.c("G3 back to the start: counter-clockwise from the right,")
    f.c("it goes above again; a full lens")
    f.add("G3 X0 Y0 I-20 J0")
    f.c("G3 away to the right: below")
    f.add("G3 X40 Y0 I20 J0")
    f.c("G2 back: below again")
    f.add("G2 X0 Y0 I-20 J0")
    f.add("M5", "G0 X0 Y-60", "M3")
    f.c("full circle R20: end point = start point, centre at (0,-80)")
    f.add("G2 X0 Y-60 I0 J-20", "M5", "G0 X60 Y-60", "M3")
    f.c("G3 full circle, centre (60,-80)")
    f.add("G3 X60 Y-60 I0 J-20", "M5", "G0 X-60 Y-60", "M3")
    f.c("I/J are relative to the start whatever the distance mode:")
    f.add("G91", "G2 X30 Y0 I15 J0", "G3 X30 Y0 I15 J0", "G90", "M5")
    f.c("an arc by bare coordinates after G2: modal")
    f.add("G0 X0 Y40", "G2", "G2 X20 Y40 I10")
    return f.finish()

def accept_origin():
    f = F(["ACCEPTED: G92 and G10 L20 P1 make the CURRENT position (x,y)", "(REQ-CoreLR-F-GCodeInterpreter-0017). Pen up. The pen never leaves", "its place on the wall for the set; each ends by putting the origin", "back where it was, so the file leaves the machine as found.", "Afterwards `G0 X0 Y0` must be a no-move, and Idle."])
    f.c("G92: the pen, where it is, now reads (50,50)")
    f.add("G92 X50 Y50", "G0 X70 Y50", "G1 X70 Y70", "G0 X50 Y50", "G92 X0 Y0")
    f.c("G10 L20 P1: the same, other spelling")
    f.add("G10 L20 P1 X-30 Y30", "G0 X-10 Y30", "G0 X-30 Y30", "G10 L20 P1 X0 Y0")
    f.c("one axis alone changes that axis only")
    f.add("G92 X5", "G0 X5 Y0", "G92 X0")
    f.c("absolute whatever G91 says")
    f.add("G91", "G92 X7 Y7", "G90", "G0 X7 Y7", "G92 X0 Y0")
    f.c("a Z word on it is ignored (Candle sends one)")
    f.add("G92 X0 Y0 Z0")
    return f.finish()

def accept_g53():
    f = F(["ACCEPTED (serial variant): G53, a move in the MACHINE frame", "(REQ-CoreLR-F-GCodeInterpreter-0018): its X Y Z are read from the", "calibration reference point, +Y up, absolute whatever G90/G91", "says, for that one line. It needs G0 or G1. Z >= 0 lifts the pen,", "Z < 0 lowers it. START with the origin where it was calibrated", "(menu: Origin = calib.), the pen over nothing."])
    f.c("shift the origin: the pen, at the reference point, reads (30,-30)")
    f.add("G92 X30 Y-30", "G0 X50 Y-30")
    f.c("Candle's Restore origin: G53 to the machine origin, then G92")
    f.add("G53 G90 G0 X0 Y0 Z0", "G92 X0 Y0 Z0")
    f.c("G53 is absolute even under G91, and does not change it")
    f.add("G91", "G53 G0 X10 Y10", "G1 X5", "G53 G1 X0 Y0", "G90")
    f.c("Safe position: Z0 alone lifts the pen, nothing moves")
    f.add("M3", "G53 G0 Z0")
    return f.finish()

def accept_penwords():
    f = F(["ACCEPTED (serial variant): the pen by M3/M5 and by the Z word.", "Z <= 0 = pen down, Z > 0 = pen up under G90; under G91 the sign", "of the move decides, Z0 changes nothing. THE Z THRESHOLD IS A", "PROJECT CONVENTION, STILL OPEN (P7): check it on a real generator.", "Watch the servo, not the paper (the head draws a short line)."])
    f.add("M3", "G1 X10", "M5", "G0 X0")
    f.c("Z words: down, up")
    f.add("G1 Z0", "G1 X10", "G0 Z5", "G0 X0", "G1 Z-1 F100", "G1 X10", "G0 Z2", "G0 X0")
    f.c("a Z-only line, and G0 with Z")
    f.add("G0 Z-0.5", "G0 Z0.5")
    f.c("relative: the sign of the move")
    f.add("G91", "G1 Z-2", "G1 Z3", "G1 Z0", "G90")
    f.c("a Z with a move on the same line: the pen first")
    f.add("G1 Z-1 X10 F100", "G1 Z3 X0")
    f.c("Candle's Z buttons send $J= with Z: see README, console")
    return f.finish()

def accept_programend():
    f = F(["ACCEPTED: M2 ends the job, lifts the pen and resets the modal state", "(G90, no motion word, pen up); in the serial variant the next line", "starts a new job. Part 2 begins with a motion word, as it must:", "bare coordinates there would be error:26 (see 33_Refuse_26)."])
    f.c("part 1: a short line with the pen down")
    f.add("G91", "M3", "G1 X20", "M2")
    f.c("part 2: a new job. $G must now read G0 G90 M5")
    f.add("G1 X10 Y10", "G1 X0 Y0", "M30")
    f.c("part 3: M30 does the same")
    f.add("G0 X5 Y5", "M30")
    f.out += ["G0 X0 Y0"]
    return f.finish(tail=False)

def accept_comments():
    f = F(["ACCEPTED: comments and blanks. Candle sends comments unstripped;", "the firmware strips them. Nothing moves far: a small square."])
    f.add("; a whole-line comment", "(a whole-line comment in parentheses)")
    f.add("G0 X10 Y0 ; to the right", "G1 X10 (up) Y10 (and across) ", "\tG1\tX0\tY10\t", "G1 X0 Y0 (ends the square)")
    f.c("text that looks like replies, in comments only:")
    f.add("G0 X0 Y0 ; ok", "G0 X0 Y0 (error:9 is only a comment)", "G0 X0 Y0 ; [MSG:Pgm End] ok error:20 ALARM:1")
    f.c("a comment with a real-time byte in it is still a real-time")
    f.c("command in grbl; the status request below is harmless:")
    f.add("G0 X0 Y0 ; ?")
    f.c("a parenthesis that is never closed ends with the line")
    f.add("G0 X5 Y5 (never closed")
    f.add("G0 X0 Y0")
    return f.finish()

def accept_longlines():
    f = F(["ACCEPTED (serial variant only: the SD variant's line is shorter):", "lines of up to 80 SIGNIFICANT characters (after blanks and comments are", "removed), six decimals, the way a generator writes them."])
    # exactly 80 significant characters: G1 X then 77 characters of number (a leading-zero run is a valid number)
    f.c("80 significant characters: G1X and 77 zeros (X0)")
    f.add("G1X" + "0" * 77)
    f.c("the same idea with blanks and a comment: 166 raw characters, 5 significant")
    f.add(" " * 50 + "G1 X0 Y0" + " " * 30 + "; " + "padding " * 8)
    # arcs written with six decimals, geometrically consistent (centre, radius, end on the circle)
    cx, cy, r = 123.456789, 80.5, 20.123456
    sx, sy = cx + r, cy
    f.c("six-decimal arcs: a circle in two half arcs, far from the origin")
    f.add("G0 X%s Y%s" % (fmt6(sx), fmt6(sy)))
    ex, ey = cx - r, cy
    f.add("G3X%sY%sI%sJ%sF1000" % (fmt6(ex), fmt6(ey), fmt6(-r), fmt6(0.0)))
    f.add("G3X%sY%sI%sJ%sF1000" % (fmt6(sx), fmt6(sy), fmt6(r), fmt6(0.0)))
    f.c("a long chord with an odd digit count")
    f.add("G1 X-12.345678 Y-23.456789 F1000.123456 S1000.123456 N123456")
    f.add("G1 X-12.345678 Y23.456789", "G1 X12.345678 Y23.456789", "G1 X12.345678 Y-23.456789")
    return f.finish()

# ---------------------------------------------------------------------------------------------------------------------------------------------------------
def refusals(z):
    """(file name, expected error, one-line meaning, setup lines before, the refused line)."""
    far = "2097.152"
    return [
        ("30_Refuse_01_Letter",       1,  "a character that is not a word letter", [], "5 G1 X10"),
        ("31_Refuse_02_NoNumber",     2,  "a letter with no number", [], "G1 X"),
        ("32_Refuse_20_G20",          20, "inches: refused, never taken for millimetres", [], "G20"),
        ("33_Refuse_20_G4",           20, "dwell, not supported", [], "G4 P1"),
        ("34_Refuse_20_M0",           20, "programmed pause, not supported", [], "M0"),
        ("35_Refuse_20_Word",         20, "an unknown word letter (Q)", [], "G1 X10 Q1"),
        ("36_Refuse_20_ArcR",         20, "the radius form of an arc (R), deferred", [], "G2 X10 Y0 R5"),
        ("37_Refuse_20_Plane",        20, "another plane (G18)", [], "G18"),
        ("38_Refuse_26_NoMode",       26, "coordinates with no motion word (after M2 the modal state is reset)", ["M2"], "X10 Y10"),
        ("39_Refuse_26_ArcNoEnd",     26, "an arc with no end point", [], "G2 I10"),
        ("40_Refuse_35_ArcNoOffset",  35, "an arc with neither I nor J", [], "G2 X10 Y0"),
        ("41_Refuse_36_Unused",       36, "I on a G1 line", [], "G1 X10 I5"),
        ("42_Refuse_36_PenAndZ",      36, "an M word and a Z word on one line (serial variant)", [], "M3 Z1"),
        ("43_Refuse_21_AxisConflict", 21, "G92 on a line with a motion word", [], "G92 G1 X5 Y5"),
        ("44_Refuse_34_ArcRadius",    34, "an arc whose two radii differ by far more than 0.1 % (start radius 20, end 10)", [], "G2 X30 Y0 I20 J0"),
        ("45_Refuse_33_FarTarget",    33, "a target %s mm away: the zone refuses it long before 2^21 um (see README)" % far, [], "G1 X%s" % far),
        ("46_Refuse_33_Typo",         33, "X100000 for X100.000: the typo the reach check was written for", [], "G1 X100000"),
        ("47_Refuse_33_Wrap",         33, "8589934.592 mm = 2^33 um, which once wrapped to 0", [], "G1 X8589934.592"),
        ("48_Refuse_33_RelativeChain", 33, "relative moves: judged where they END; the 9th of 10 small steps crosses the left limit", None, None),
        ("50_Refuse_20_G53NoMode",    20, "G53 with no G0/G1 on the line or modal (after M2)", ["M2"], "G53 X0 Y0"),
        ("51_Refuse_33_G53Zone",      33, "G53 to a machine point 1 mm beyond the left limit (the origin being the reference point)", [], "G53 G0 X%s Y0" % num(z.edge_l - 1)),
        ("49_Refuse_11_Line81",       11, "81 significant characters: never truncated (serial link, not the parser)", [], "G1X" + "0" * 78),
    ]

def refusal_file(z, name, err, why, setup, line):
    t = ["REFUSAL TEST: error:%d -- %s." % (err, why), "Pen up throughout. The marked line must be REFUSED: the job halts", "(Alarm, E on the LCD), nothing moves on that line. Then Ctrl-X / SW /", "Candle's Reset (control panel) to acknowledge."]
    f = F(t)
    if name.startswith("48_"):
        step = 10.0
        n = int(math.floor((-z.edge_l) / step)) + 1     # steps of -10 from the origin: the n-th one ends strictly beyond the left limit
        f.add("G91", "M5")
        for i in range(1, n):
            f.add("G0 X-%s" % num(step))
        f.c("the previous step ended %.1f mm inside the limit; this one ends beyond: REFUSED" % (-z.edge_l - (n - 1) * step))
        f.add("G0 X-%s" % num(step))
        f.add("G90")
        return f.finish()
    f.c("accepted baseline move")
    f.add("G0 X10 Y10")
    for s in setup:
        f.add(s)
    f.c("REFUSED: error:%d" % err)
    f.add(line)
    return f.finish()

# ---------------------------------------------------------------------------------------------------------------------------------------------------------
class Pen:
    def __init__(self, f, style):
        self.f, self.style, self.down = f, style, False
    def up(self):
        if self.down or self.down is None:
            self.f.add("M5" if self.style == "m" else "G0 Z5")
        self.down = False
    def dn(self):
        if not self.down:
            self.f.add("M3 S1000" if self.style == "m" else "G1 Z-1 F100")
        self.down = True

def drawing(style):
    title = ["A drawing in the style of a CAM export, %s: a house, a sun, a tree," % ("pen by M3/M5" if style == "m" else "pen lifted by Z words (Z <= 0 down)"),
             "inside 100 mm of the origin (x -100..100, y -50..65), with arcs.",
             "Expected: runs whole, ends pen up at the origin, Idle."]
    f = F(title)
    p = Pen(f, style)
    def go(x, y):
        p.up(); f.add("G0 X%s Y%s F3000" % (num(x), num(y)))
    def ln(x, y):
        p.dn(); f.add("G1 X%s Y%s F600" % (num(x), num(y)))
    def arc(ccw, x, y, i, j):
        p.dn(); f.add("G%d X%s Y%s I%s J%s F600" % (3 if ccw else 2, num(x), num(y), num(i), num(j)))
    def circle(cx, cy, r):
        go(cx + r, cy); arc(True, cx - r, cy, -r, 0); arc(True, cx + r, cy, r, 0)
    f.add("(ground)")
    go(-100, -50); ln(100, -50)
    f.add("(house: walls, roof, arched door, window)")
    go(-75, -50); ln(-75, 0); ln(5, 0); ln(5, -50)
    go(-85, 0); ln(-35, 40); ln(15, 0); ln(-85, 0)
    go(-50, -50); ln(-50, -20); arc(False, -30, -20, 10, 0); ln(-30, -50)
    go(-14, -20); ln(-4, -20); ln(-4, -10); ln(-14, -10); ln(-14, -20)
    f.add("(chimney)")
    go(-15, 22); ln(-15, 38); ln(-5, 38); ln(-5, 12)
    f.add("(sun with eight rays)")
    circle(65, 45, 12)
    for k in range(8):
        a = k * math.pi / 4
        go(65 + 16 * math.cos(a), 45 + 16 * math.sin(a)); ln(65 + 24 * math.cos(a), 45 + 24 * math.sin(a))
    f.add("(tree: trunk and a crown of two arcs)")
    go(55, -50); ln(55, -20); go(65, -50); ln(65, -20)
    go(45, -20); arc(False, 75, -20, 15, 0)
    go(47, -20); arc(True, 73, -20, 13, 0)
    f.add("(smoke: two arcs, G2 then G3)")
    go(-10, 38); arc(False, -10, 50, 0, 6); arc(True, -10, 62, 0, 6)
    p.up()
    return f.finish()

# ---------------------------------------------------------------------------------------------------------------------------------------------------------
README_HEAD = """# GCode for the serial variant: what the link accepts, refuses, and says

Written by `tools/generators/gen_serial_tests.py` (do not edit by hand; `tests/host/check_serial_files.sh` checks them against the firmware's real parser):
```
tools/generators/gen_serial_tests.py --geometry %(geom)s -o tests/bench/gcode_serial
tools/bench/gcode_check.sh --limit 1000 --geometry %(geom)s tests/bench/gcode_serial/*.gc
```
Machine geometry used: motors **D = %(d)g mm** apart, drawing origin %(ox).3f mm from the left motor and %(oy).3f mm below the line joining the motors; travel zone
X %(xl).3f .. +%(xr).3f mm, Y up to +%(yt).3f mm (margins 20 mm sides, 50 mm top). The contract is `docs/protocol/SerialProtocol.md`; the travel-zone files proper
are in `../gcode_machine/`.

**How to use with Candle 1.1.9.** The machine calibrated and the origin where it was calibrated (or "Origin = calib."), the pen over paper for the drawing files and
over nothing for the others. Open a file, send it. A refusal file stops at its one refused line: Candle sends `!`, shows its dialog; the machine reports `Alarm`.
The way out is the **Reset button of the Control panel** (not the file group's Reset): it sends Ctrl-X, which acknowledges the error and brings the welcome line again.
Do not close the port within 2 s of a move (the position is stored after 2 s of stillness).

## Accepted
| file | what it shows | expected |
|---|---|---|
| `20_Accept_Words.gc` | the command set, modal words, tolerated words, empty lines | every line `ok`, Idle at the origin, the head drew nothing (pen up) |
| `21_Accept_Arcs.gc` | **direction**: `G2 X40 Y0 I20` passes ABOVE, `G3` below; full circles; I/J under G91 | ink: an upper lens, a lower lens, two circles, two half circles, a short arc |
| `22_Accept_Origin.gc` | `G92`, `G10 L20 P1`, one axis, G91, a Z word | every line `ok`; the origin is back where it was (`G0 X0 Y0` makes no move; the LCD still reads 0.0 / 0.0) |
| `23_Accept_G53.gc` | `G53` in the machine frame (FW-12): Candle's "Restore origin", absolute under G91, "Safe position" | every line `ok`; the origin is back on the reference point; the pen rises at the last line (start with "Origin = calib.") |
| `24_Accept_PenWords.gc` | `M3`/`M5` and `Z` words (serial variant; **threshold still open**) | the servo goes down/up at each pen line; short strokes only |
| `25_Accept_ProgramEnd.gc` | `M2`, `M30`: pen up, modal reset, next line = new job | three parts; `$G` after part 1 reads `G0 ... G90 ... M5` |
| `26_Accept_Comments.gc` | `;` and `( )` comments, tabs, reply-like text in comments | every line `ok`; a small square |
| `27_Accept_LongLines.gc` | 80 significant characters, six decimals, arcs far from the origin | every line `ok` (SD variant: not for it) |
| `60_Drawing_M3M5.gc` | a house, a sun and a tree, M3/M5 | the same ink as `61_` (`gcode_check` reports its length) |
| `61_Drawing_ZPen.gc` | the same, the pen by Z words | the same ink; **Z threshold = convention, open** |

## Refused: one line each (the job halts; `Alarm`)
| file | `error:N` | cause |
|---|---|---|
"""
README_TAIL = """
Notes.
- `45_`, `46_`, `47_`: on THIS machine the travel zone refuses these long before the machine's reach (2^21 um = 2 097.152 mm, `REQ-CoreLR-F-GCodeInterpreter-0011`): the
  answer is the same `error:33`, so the bench **cannot tell the 2.097 m boundary from the zone**. The boundary itself (2 097.151 accepted, 2 097.152 not) is a host test
  (`tests/host/test_parser.c`, zone widened).
- `48_` is the relative-chain case: each step ends inside the zone until the last.
- `42_` is a serial-variant refusal; in the SD variant a `Z` is `error:20` (an unsupported word).
- `38_` and `50_` (no motion word after `M2`) rely on `M2`/`M30` leaving no motion mode, as today; P16 (a) of `docs/protocol/SerialProtocol.md` gives a motion mode (`G0`) to Ctrl-X only, so they do not change. After a Ctrl-X, a bare `X10 Y10` becomes legal (`G0`).
- `Z0` means pen DOWN in `24_` (`G1 Z0`) and pen UP under `G53` (`23_`, `G53 G0 Z0`).
- `49_` is refused by the LINK (`error:11`, line over 80 significant characters), not by the parser.
- Outside a job (machine `Idle`, the line typed in Candle's console), the same lines are only answered `error:N`; the machine stays `Idle` (P12).

## To type in Candle's console (the `$` commands, real-time bytes: not files)
| type | expected |
|---|---|
| `$G` | `[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]`, `ok` (the motion word and M3/M5 are the real state) |
| `$I` | `[VER:1.1h.<protocol date>:CoreLR]`, `[OPT:,8,128]`, `ok` |
| `$$` | `$10=3 $13=0 $20=0 $21=0 $22=0 $32=0`, `ok` |
| `$` | `[HLP:$$ $G $I $X ~ ! ? ctrl-x]`, `ok` |
| `$X` | calibrated and in `Alarm` from a refusal: `[MSG:Caution: Unlocked]`, `ok`; uncalibrated: `error:9` and the "not calibrated" message |
| `$#`, `$N`, `$C`, `$H`, `$SLP`, `$RST`, `$10=3` | `error:3` each |
| `$J=G21G91X10Y5F600Z0` | `ok`, state `Jog` then `Idle`, the head moved 10, 5 (Z ignored) |
| `$J=G21G90X0Y0F600` | `ok`, back to the origin; during a job: `error:8` |
| `$J=G1X10` | `error:20` (a motion word is not a jog) |
| `$J=` | `error:3` |
| a jog button held, then released | chained small `$J=`; stop with 0x85 (Candle's Stop) |
| `G1 X10` while uncalibrated or in `Alarm` | `error:9` |
"""

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--geometry", required=True, metavar="D,L | D,OX,OY", help="the machine, mm: motor distance and the calibration's reference cable length (the origin where calibration leaves it), or the motor distance and the origin's place")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "..", "..", "tests", "bench", "gcode_serial"), help="directory to write (default: tests/bench/gcode_serial)")
    ap.add_argument("--side", type=float, default=20.0, help="the side margin, mm (default 20)")
    ap.add_argument("--top", type=float, default=50.0, help="the top margin, mm (default 50)")
    a = ap.parse_args()
    d, ox, oy = g.parse_geometry(a.geometry)
    if oy - a.top <= 0:
        sys.exit("gen_serial_tests.py: the origin is %g mm below the line joining the motors, less than the top margin (%g)" % (oy, a.top))
    z = g.Zone(d, ox, oy, a.side, a.top, 0.0, 600.0)
    if z.edge_l > -120 or z.edge_r < 120 or z.edge_t < 70:
        sys.exit("gen_serial_tests.py: the travel zone of this geometry (X %.1f..%.1f, Y up to %.1f) is too small for files written inside 110 mm of the origin" % (z.edge_l, z.edge_r, z.edge_t))
    os.makedirs(a.out, exist_ok=True)
    files = [("20_Accept_Words.gc", accept_words()), ("21_Accept_Arcs.gc", accept_arcs()), ("22_Accept_Origin.gc", accept_origin()), ("23_Accept_G53.gc", accept_g53()),
             ("24_Accept_PenWords.gc", accept_penwords()), ("25_Accept_ProgramEnd.gc", accept_programend()), ("26_Accept_Comments.gc", accept_comments()), ("27_Accept_LongLines.gc", accept_longlines())]
    rows = []
    for name, err, why, setup, line in refusals(z):
        files.append((name + ".gc", refusal_file(z, name, err, why, setup, line)))
        rows.append("| `%s.gc` | %d | %s |" % (name, err, why))
    files += [("60_Drawing_M3M5.gc", drawing("m")), ("61_Drawing_ZPen.gc", drawing("z"))]
    for name, text in files:
        open(os.path.join(a.out, name), "w", newline="\n").write(text)
        print("%-30s %4d lines" % (name, text.count("\n")))
    readme = README_HEAD % dict(geom=a.geometry, d=d, ox=ox, oy=oy, xl=z.edge_l, xr=z.edge_r, yt=z.edge_t) + "\n".join(rows) + "\n" + README_TAIL
    open(os.path.join(a.out, "README.md"), "w", newline="\n").write(readme)

if __name__ == "__main__":
    main()
