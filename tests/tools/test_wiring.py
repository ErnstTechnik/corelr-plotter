#!/usr/bin/env python3
# test_wiring.py
# Part of the CoreLR wall-plotter firmware project.
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# GPLv3 (see the repository's LICENSE); the drawings the tool writes are CC BY-SA 3.0, see third_party/fritzing-parts/PROVENANCE.md.
"""test_wiring.py -- is tools/generators/gen_wiring.py right? The wires of every diagram against a table written out by hand (not derived from HwConfig.h),
against PinMapping.md's bench wiring tables and against HwConfig.h itself; that a wrong or a dangling wire is refused; the geometry (rotation, mirror, the SVG
transforms, the ring pads) against values worked out by hand; that the vendored Fritzing parts are byte-identical to upstream; and that every SVG is well formed,
has unique ids, keeps its wires on the canvas and carries its CC BY-SA 3.0 credit. Prints 'all checks passed' or the failing case; exit status = the number of failures.
Proved able to fail by tests/mutation/mutants/wiring.txt (tools/mutate.py --script tests/tools/test_wiring.py)."""
import hashlib, importlib.util, math, os, re, sys, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(ROOT, "tools", "generators", "gen_wiring.py")
if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__); sys.exit(0)
spec = importlib.util.spec_from_file_location("gw", TOOL)
gw = importlib.util.module_from_spec(spec); spec.loader.exec_module(gw)

bad = 0
def check(ok, what):
    global bad
    if not ok:
        bad += 1; print("FAIL:", what)

def near(a, b, tol=1e-6): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))

# --- 1. the machine's nets, from PinMapping.md's tables (the document) and the maintainer's statement of the power; the drawing must say the same.
# The external supply feeds the boards, the servo AND the Nano's 5V pin (maintainer, 2026-10-14: in SD mode the USB port is not connected); the Nano's two GND pins are one net.
doc = open(os.path.join(ROOT, "docs", "hardware", "PinMapping.md"), encoding="utf-8").read()
ref_sig = {}
for m in "RL":
    sec = re.search(r"### Motor %s\b.*?(?=\n### )" % m, doc, re.S).group(0)
    rows = re.findall(r"\|\s*`IN(\d)`\s*\|\s*`D(\d+)`", sec)
    check(len(rows) == 4, "PinMapping.md: motor %s lists %d IN rows, expected 4" % (m, len(rows)))
    for i, d in rows:
        ref_sig["motor_%s.IN%s" % (m, i)] = "D" + d
servo = re.search(r"###\s+Pen servo.*?\|\s*signal[^|]*\|\s*`(A\d)`", doc, re.S)
check(servo is not None, "PinMapping.md: the servo's signal row"); ref_sig["servo.pulse"] = servo.group(1) if servo else "?"
joy = re.findall(r"\|\s*`(VRx|VRy|SW)`\s*\|\s*`(A\d)`", doc)
check(len(joy) == 3, "PinMapping.md: joystick rows %s" % joy)
for pin, a in joy:
    ref_sig["joystick." + {"VRx": "HORZ", "VRy": "VERT", "SW": "SEL"}[pin]] = a
lcd = re.findall(r"\|\s*`(SDA|SCL)`\s*\|\s*`(A\d)`", doc)
check(len(lcd) == 2, "PinMapping.md: LCD rows %s" % lcd)
for pin, a in lcd:
    ref_sig["lcd_i2c." + pin] = a
sd = re.findall(r"\|\s*`(CS|MOSI|MISO|SCK)`[^|]*\|\s*`(D\d+)`", doc)
check(len(sd) == 4, "PinMapping.md: SD rows %s" % sd)
for pin, d in sd:
    ref_sig["microsd." + pin] = d
check(len(ref_sig) == 8 + 1 + 3 + 2 + 4, "PinMapping.md: %d signal rows, expected 18" % len(ref_sig))
NET5_EXT = {"supply.plus", "motor_R.plus", "motor_L.plus", "servo.vcc", "diode.anode"}                                                  # with the series diode:
NET5_NANO = {"nano.5V", "joystick.VCC", "lcd_i2c.VCC", "microsd.VCC", "diode.cathode"}                                                  # two rails, one way through the diode
NET5 = {"supply.plus", "nano.5V", "motor_R.plus", "motor_L.plus", "servo.vcc", "joystick.VCC", "lcd_i2c.VCC", "microsd.VCC"}            # as built: the supply straight on the 5V pin
NETG = {"supply.minus", "nano.GND", "nano.GND#2", "motor_R.minus", "motor_L.minus", "servo.gnd", "joystick.GND", "lcd_i2c.GND", "microsd.GND"}
REF = [{"nano." + pin, mod} for mod, pin in ref_sig.items()] + [NET5_EXT, NET5_NANO, NETG]       # the overview draws the diode variant

ov = gw.overview()
comps = gw.components(ov.netlist(), [("nano.GND", "nano.GND#2")])
check(sorted(map(sorted, comps)) == sorted(map(sorted, REF)),
      "overview: the nets differ from the reference: extra %s, missing %s" % ([sorted(c) for c in comps if c not in REF], [sorted(n) for n in REF if n not in comps]))
check(len(comps) == 21, "overview: 18 signals + the two 5 V rails + the ground = 21 nets, got %d" % len(comps))
check(getattr(ov, "series_diode", False), "the overview is the series-diode variant")
for n, f in gw.SCENES.items():
    if n in ("parts", "nano_led"):
        continue
    check(gw.verify_scene(f()) == [], "%s: %s" % (n, gw.verify_scene(f())))
check(len(gw.expected_nets()) == 20 and len(gw.expected_nets(diode=True)) == 21, "expected_nets: 20 nets as built, 21 with the diode")

# --- 2. PinMapping.md and HwConfig.h and the core say the same pins (a document out of date is found here)
hw = gw.hwconfig()
mp = gw.machine_pins()
for mod, pin in ref_sig.items():
    key = {"motor_R": "R", "motor_L": "L"}.get(mod.split(".")[0])
    if key:
        check(mp[key][int(mod[-1]) - 1] == pin, "HwConfig.h puts %s on %s, PinMapping.md says %s" % (mod, mp[key][int(mod[-1]) - 1], pin))
    else:
        k = "servo" if mod == "servo.pulse" else mod.split(".")[1]
        check(mp[k] == pin, "HwConfig.h / the Arduino core put %s on %s, PinMapping.md says %s" % (mod, mp[k], pin))
core = gw.core_pins()
check(core["PIN_SPI_MOSI"] == "11" and core["PIN_SPI_MISO"] == "12" and core["PIN_SPI_SCK"] == "13" and core["PIN_WIRE_SDA"] == "18" and core["PIN_WIRE_SCL"] == "19",
      "the Arduino core's fixed pins (SPI 11 12 13, TWI 18 19 = A4 A5): %s" % core)
check([gw.nano_pin(v) for v in ("0", "13", "14", "19", "A1")] == ["D0", "D13", "A0", "A5", "A1"], "nano_pin: %s" % [gw.nano_pin(v) for v in ("0", "13", "14", "19", "A1")])

# --- 3. the drawing FOLLOWS the pins: moved in HwConfig.h or in the core, the wires move and stay right; the old drawing is then wrong
hw2 = dict(hw, STEPPER_MOTOR_R_PIN0="3", STEPPER_MOTOR_R_PIN1="2")                          # two of motor R's pins swapped
mp2 = gw.machine_pins(hw2)
for name in ("motors", "overview"):
    sc = gw.SCENES[name](mp2)
    check(gw.verify_scene(sc, hw2) == [], "%s, R's IN1/IN2 swapped in HwConfig.h: the new drawing must verify: %s" % (name, gw.verify_scene(sc, hw2)))
    check(gw.verify_scene(sc) != [], "%s: the drawing made for the swapped pins must NOT verify against the real ones" % name)
    check(gw.verify_scene(gw.SCENES[name]())[:1] == [] and gw.verify_scene(gw.SCENES[name](), hw2) != [], "%s: the real drawing must NOT verify against the swapped pins" % name)
nl2 = gw.motors(mp2).netlist()
check(frozenset(("nano.D3", "motor_R.IN1")) in nl2 and frozenset(("nano.D2", "motor_R.IN2")) in nl2 and frozenset(("nano.D2", "motor_R.IN1")) not in nl2,
      "the swapped pins are drawn swapped")
hw3 = dict(hw, SDREADER_CS_PIN="9", STEPPER_MOTOR_L_PIN3="10")                              # CS and the last L pin swapped
check(gw.verify_scene(gw.sd_card(gw.machine_pins(hw3)), hw3) == [] and gw.verify_scene(gw.sd_card(gw.machine_pins(hw3))) != [], "SDREADER_CS_PIN moved: the SD diagram follows")
core2 = dict(core, PIN_SPI_MOSI=core["PIN_SPI_MISO"], PIN_SPI_MISO=core["PIN_SPI_MOSI"])    # a core with MOSI and MISO swapped
mp3 = gw.machine_pins(core=core2)
check(mp3["MOSI"] == "D12" and mp3["MISO"] == "D11", "the fixed pins come from the Arduino core, not from the tool: %s" % ((mp3["MOSI"], mp3["MISO"]),))
check(gw.verify_scene(gw.sd_card(mp3), core=core2) == [] and gw.verify_scene(gw.sd_card(mp3)) != [], "the SD diagram follows the core's SPI pins")
core3 = dict(core, PIN_WIRE_SDA="19", PIN_WIRE_SCL="18")
mp4 = gw.machine_pins(core=core3)
check(mp4["SDA"] == "A5" and mp4["SCL"] == "A4" and gw.verify_scene(gw.lcd_i2c(mp4), core=core3) == [], "the LCD diagram follows the core's TWI pins")
for what, h, c in (("a motor pin on the SPI MOSI pin", dict(hw, STEPPER_MOTOR_R_PIN0="11"), None),
                   ("the chip select on a motor pin", dict(hw, SDREADER_CS_PIN="6"), None),
                   ("a motor on an analog pin", dict(hw, STEPPER_MOTOR_R_PIN0="A1", LCDJOYSTICKUI_X_PIN="2"), None),
                   ("the servo on the bottom edge", dict(hw, PENCONTROL_SERVO_PIN="12", STEPPER_MOTOR_L_PIN3="14"), None),
                   ("the core's SDA on a motor pin", hw, dict(core, PIN_WIRE_SDA="5"))):
    try:
        gw.machine_pins(h, c); check(False, "machine_pins must refuse %s" % what)
    except SystemExit:
        pass
check(gw.rank([30, 10, 20]) == [2, 0, 1] and gw.rank([30, 10, 20], reverse=True) == [0, 2, 1] and gw.rank([5, 5, 1]) == [1, 2, 0], "rank: %s %s" % (gw.rank([30, 10, 20]), gw.rank([30, 10, 20], reverse=True)))
check(gw.fmt_pins(["D2", "D3", "D4", "D5"]) == "D2\u2013D5" and gw.fmt_pins(["D3", "D2", "D4", "D5"]) == "D3, D2, D4, D5" and gw.fmt_pins(["A1", "A2"]) == "A1\u2013A2", "fmt_pins")

def crossings(sc, colours):
    """Pairs of wires of the given colours whose orthogonal paths cross (a vertical through the inside of a horizontal)."""
    segs = []
    for i, (col, pts) in enumerate(sc.paths):
        if col in colours:
            segs += [(i, pts[k], pts[k + 1]) for k in range(len(pts) - 1)]
    n = 0
    for a in segs:
        for b in segs:
            if a[0] >= b[0]:
                continue
            (x1, y1), (x2, y2), (u1, v1), (u2, v2) = a[1], a[2], b[1], b[2]
            for (hx1, hy1, hx2, hy2), (vx1, vy1, vx2, vy2) in (((x1, y1, x2, y2), (u1, v1, u2, v2)), ((u1, v1, u2, v2), (x1, y1, x2, y2))):
                if hy1 == hy2 and vx1 == vx2 and min(hx1, hx2) < vx1 < max(hx1, hx2) and min(vy1, vy2) < hy1 < max(vy1, vy2):
                    n += 1
    return n
check(crossings(gw.motors(), {"blue", "pink", "yellow", "orange"}) == 0, "the motors' wires must not cross each other: %d crossings" % crossings(gw.motors(), {"blue", "pink", "yellow", "orange"}))
check(crossings(gw.sd_card(), {"grey", "green", "white"}) == 0, "the SD wires from the bottom row must not cross each other")
check(crossings(gw.overview(), {"blue", "pink", "yellow", "orange"}) == 0, "the overview's motor wires must not cross each other")

# --- 4. a wrong wire, a dangling wire, a short, a net left open
s = gw.Scene(900, 700, "t", "t")
nano = s.part("nano", 450, 150, rot=270)
mr = s.part("uln_motor", 450, 420, tag="motor_R")
s.down(nano.pin("D3"), mr.pin("IN1"), 300, "blue")                          # D3 on IN1: wrong (it is D2)
pb = gw.verify_scene(s)
check(any("short" in p and "nano.D3" in p and "motor_R.IN1" in p for p in pb), "a wire D3 -- motor_R.IN1 must be reported as a short between two nets: %s" % pb)
check(any("not closed" in p and "nano.D2" in p for p in pb), "the absent wire D2 -- motor_R.IN1 must be reported (net not closed): %s" % pb)
s2 = gw.Scene(900, 700, "t", "t")
n2 = s2.part("nano", 450, 150, rot=270)
s2.wire([n2.pin("D2"), (n2.pin("D2")[0], 500)], "blue")                      # ends in the air
try:
    s2.netlist(); check(False, "a wire ending on no pin must stop the tool")
except SystemExit:
    pass
s3 = gw.Scene(900, 700, "t", "t")
n3 = s3.part("nano", 450, 150, rot=270)
m3 = s3.part("uln_motor", 450, 420, tag="motor_R")
su = s3.part("supply", 800, 300)
s3.wire([n3.pin("5V"), m3.pin("minus")], "red")                              # the 5 V on a ground pin: a short
check(any("short" in p for p in gw.verify_scene(s3)), "5V on a motor board's minus must be reported as a short: %s" % gw.verify_scene(s3))
check(any("not closed" in p and "supply.minus" in p for p in gw.verify_scene(s3)), "a supply and a Nano without a common ground must be reported: %s" % gw.verify_scene(s3))
s4 = gw.Scene(900, 700, "t", "t")
n4 = s4.part("nano", 450, 150, rot=270)
s4.part("supply", 800, 300)
s4.wire([n4.pin("GND#2"), s4.parts[1].pin("minus")], "black")
check(not any("supply.minus" in p for p in gw.verify_scene(s4)) and any("supply.plus" in p for p in gw.verify_scene(s4)),
      "the ground tied but the +5 V not: only the 5 V net is open: %s" % gw.verify_scene(s4))

# the series diode: anode on the supply side, cathode on the Nano's 5V; reversed, missing, or the old direct wire kept, are all refused
def diode_scene(wires):
    sc = gw.Scene(1200, 700, "t", "t"); sc.series_diode = True
    nn = sc.part("nano", 600, 400, rot=270); su = sc.part("supply", 300, 150); di = sc.part("diode", 450, 60)
    for a, b in wires(nn, su, di):
        sc.wire([a, b], "red")
    return sc
good = diode_scene(lambda nn, su, di: [(su.pin("plus"), di.pin("anode")), (di.pin("cathode"), nn.pin("5V")), (su.pin("minus"), nn.pin("GND"))])
check(gw.verify_scene(good) == [], "supply -> anode, cathode -> 5V, common ground: nothing to report; got %s" % gw.verify_scene(good))
rev = diode_scene(lambda nn, su, di: [(su.pin("plus"), di.pin("cathode")), (di.pin("anode"), nn.pin("5V")), (su.pin("minus"), nn.pin("GND"))])
check(any("short" in p for p in gw.verify_scene(rev)), "a reversed diode must be refused: %s" % gw.verify_scene(rev))
direct = diode_scene(lambda nn, su, di: [(su.pin("plus"), nn.pin("5V")), (su.pin("minus"), nn.pin("GND"))])
check(any("not closed" in p and "diode" in p for p in gw.verify_scene(direct)) or any("short" in p for p in gw.verify_scene(direct)), "a direct wire in the diode variant must be refused: %s" % gw.verify_scene(direct))
nodiode = diode_scene(lambda nn, su, di: [(su.pin("minus"), nn.pin("GND"))])
check(any("not closed" in p for p in gw.verify_scene(nodiode)), "a diode wired to nothing must be reported: %s" % gw.verify_scene(nodiode))
asbuilt = gw.Scene(1200, 700, "t", "t")
nb = asbuilt.part("nano", 600, 400, rot=270); sb = asbuilt.part("supply", 300, 150)
asbuilt.wire([sb.pin("plus"), nb.pin("5V")], "red"); asbuilt.wire([sb.pin("minus"), nb.pin("GND")], "black")
check(gw.verify_scene(asbuilt) == [], "as built (no diode): the supply straight on 5V is right: %s" % gw.verify_scene(asbuilt))

# --- 5. the geometry, by hand
MM = gw.MM
sup = gw.Supply5V(100, 100)                                                  # plus (30, 34) in a 44 x 34 box: (+8, +17) mm from the centre
for rot, ex in ((0, (8, 17)), (90, (-17, 8)), (180, (-8, -17)), (270, (17, -8))):
    sup.rot = rot
    p = sup.pin("plus")
    check(near(p[0], 100 + ex[0] * MM) and near(p[1], 100 + ex[1] * MM), "rotation %d: the pin is at %s, expected %s" % (rot, p, (100 + ex[0] * MM, 100 + ex[1] * MM)))
sup.rot, sup.flip = 0, True
p = sup.pin("plus")
check(near(p[0], 100 - 8 * MM) and near(p[1], 100 + 17 * MM), "mirrored: %s" % (p,))
check(near(MM, 3.2 * 72 / 25.4) and near(gw.SCALE, 3.2), "the scale: one millimetre = %g px" % MM)
nano = gw.Part("nano", 1000, 500, rot=270)
x2, y2, x3, y3 = *nano.pin("D2"), *nano.pin("D3")
check(near(x3 - x2, 7.2 * 3.2) and near(y3, y2), "D2 -> D3 is one 0.1 inch pitch, to the right (%.3f, %.3f)" % (x3 - x2, y3 - y2))
check(near(nano.pin("D13")[0], nano.pin("D12")[0]) and nano.pin("D13")[1] < nano.pin("D12")[1], "D13 is straight above D12")
check(near(nano.pin("D2")[1] - nano.pin("A0")[1], 0.6 * 72 * 3.2), "the two rows of the Nano are 0.6 inch apart")
check(near(nano.pin("D2")[1] - 500, 21.6 * 3.2) and near(nano.pin("GND#2")[1] - 500, -21.6 * 3.2), "the rows are 21.6 units either side of the centre")
check(nano.pin("GND")[1] > 500 > nano.pin("GND#2")[1], "'GND' is the bottom-row one, 'GND#2' the top-row one")
sv = gw.Part("servo", 0, 0)                                                  # the group has translate(-0.555968, 45.0334): the legs' tips are at x2 + tx, y2 + ty
lx, ly = sv.local("gnd")
check(near(lx, 113.976032 - 0.555968) and near(ly, 39.406 + 45.0334), "servo gnd tip (transform of the ancestor group): %s" % ((lx, ly),))
sd = gw.Part("microsd", 0, 0)                                                # a ring drawn as a path: centre = (x, y + the quarter-turn's dy)
cx, cy = sd.local("CS")
check(near(cx, 15.502) and near(cy, 8.888 + 2.394), "microSD CS pad (path ring): %s" % ((cx, cy),))
check(sd.pin("CS")[0] < sd.pin("SCK")[0] < sd.pin("MOSI")[0] < sd.pin("MISO")[0] < sd.pin("VCC")[0] < sd.pin("GND")[0], "the microSD header reads CS SCK MOSI MISO VCC GND")
dio = gw.Part("diode", 0, 0)
check(dio.local("cathode")[0] < dio.local("anode")[0] and near(dio.local("anode")[0] - dio.local("cathode")[0], 29.879 - 0.5 - 0.5, 0.05), "the diode's terminals are its two ends (cathode on the left): %s %s" % (dio.local("cathode"), dio.local("anode")))
led = gw.Part("led", 0, 0)
check(led.local("cathode")[0] < led.local("anode")[0], "the LED's cathode is the left leg, the anode the right one")
check(near(gw.Part._xf("translate(3, 4) scale(2)")[4], 3) and near(gw.Part._xf("matrix(1 0 0 1 5 6)")[5], 6), "the transform parser")
uln = gw.UlnMotor(0, 0)
check(near(uln.PINS["IN2"][0] - uln.PINS["IN1"][0], 2.54) and near(uln.PINS["plus"][0] - uln.PINS["minus"][0], 2.54), "the ULN2003 board's header is on a 2.54 mm pitch")
lcd = gw.LcdI2c(0, 0)
check(list(lcd.PINS) == ["GND", "VCC", "SDA", "SCL"] and all(near(b[1] - a[1], 2.54) for a, b in zip(list(lcd.PINS.values()), list(lcd.PINS.values())[1:])),
      "the LCD's I2C header: GND VCC SDA SCL from the top, 2.54 mm pitch")

# --- 6. the vendored Fritzing parts are the upstream bytes
man = os.path.join(gw.PARTS_DIR, "MANIFEST.sha256")
rows = [l.split("  ", 1) for l in open(man, encoding="utf-8").read().splitlines() if l.strip()]
check(len(rows) >= 7, "MANIFEST.sha256 lists %d files" % len(rows))
for h, f in rows:
    check(hashlib.sha256(open(os.path.join(gw.PARTS_DIR, f), "rb").read()).hexdigest() == h, "third_party/fritzing-parts/%s differs from its manifest (the parts must stay unmodified)" % f)
for key, (svg, fzp) in gw.PARTS.items():
    check(os.path.exists(os.path.join(gw.PARTS_DIR, "svg", svg)) and os.path.exists(os.path.join(gw.PARTS_DIR, "fzp", fzp)), "part %s: files missing" % key)
    check(any(r[1] == "svg/" + svg for r in rows) and any(r[1] == "fzp/" + fzp for r in rows), "part %s is not in MANIFEST.sha256" % key)
prov = open(os.path.join(gw.PARTS_DIR, "PROVENANCE.md"), encoding="utf-8").read()
for key, (svg, fzp) in gw.PARTS.items():
    check(svg in prov and fzp in prov, "PROVENANCE.md does not list %s / %s" % (svg, fzp))
check("CC BY-SA 3.0" in prov or "Attribution-ShareAlike 3.0" in prov, "PROVENANCE.md states the licence")

# --- 7. every SVG: well formed, unique ids, the wires on the canvas, the credit and licence, no personal path
for n, f in gw.SCENES.items():
    sc = f()
    svg = sc.render()
    try:
        root = ET.fromstring(svg.encode("utf-8"))
    except ET.ParseError as e:
        check(False, "%s: not well formed (%s)" % (n, e)); continue
    ids = re.findall(r'\bid="([^"]+)"', svg)
    dup = sorted({i for i in ids if ids.count(i) > 1})
    check(not dup, "%s: duplicate ids %s" % (n, dup[:5]))
    check("creativecommons.org/licenses/by-sa/3.0" in svg and "Fritzing" in svg and "ErnstTechnik" in svg, "%s: credit, licence or copyright missing from the metadata" % n)
    check("/home/" not in svg and not re.search(r"[\w.+-]+@[\w-]+\.[\w.]+", svg), "%s: a personal path or an e-mail address in the SVG" % n)
    for col, a, b in sc.ends:
        for x, y in (a, b):
            check(0 <= x <= sc.w and 0 <= y <= sc.h, "%s: a wire end (%.0f, %.0f) is outside the %dx%d canvas" % (n, x, y, sc.w, sc.h))
    for d in re.findall(r'<path d="(M[^"]*)"', svg):
        for x, y in re.findall(r"[ML]\s*([-\d.]+)\s+([-\d.]+)", d):
            check(-5 <= float(x) <= sc.w + 5 and -5 <= float(y) <= sc.h + 5, "%s: a wire passes outside the canvas (%s, %s)" % (n, x, y))
    for p in sc.parts:
        for name in (p.names if hasattr(p, "names") else p.PINS):
            x, y = p.pin(name)
            if n not in ("parts", "nano_led"):
                check(-300 <= x <= sc.w + 300 and -300 <= y <= sc.h + 300, "%s: pin %s.%s far off the canvas (%.0f, %.0f)" % (n, p.tag, name, x, y))

print("all checks passed" if not bad else "%d check(s) failed" % bad)
sys.exit(bad)
