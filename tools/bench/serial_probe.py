#!/usr/bin/env python3
#
# serial_probe.py
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
"""Drive the REAL machine over the serial link, and log every byte with its time: what Candle does, reproducibly, and readable.

Why it exists: Candle could not be spied on (bench, 2026-10-10), and a pause that cannot be resumed has to be looked at byte by byte.

THIS MOVES THE MACHINE (except `status`). Read the scenario before running it. The pen is never touched (no M3/M5, no Z).
Opening the port resets the Nano (DTR): the script waits for the welcome line, and the calibration survives only if the position
had been stored before (2 s of stillness, REQ-CoreLR-F-Calibration-0007).

Scenarios
  status         opens the port, reads the welcome line, polls `?` a few times. Sends no G-code. Moves nothing.
  pause-resume   needs a calibrated machine in `Idle`. Sends G91 and six relative moves of 40 mm along X (+40, -40, +40, -40, +40, -40: it
                 ends where it began, about 12 s), then G90. After --pause-after seconds of motion it sends `!`, waits for `Hold:0`,
                 waits --hold seconds, sends `~`, and says whether the state came back to `Run`. A final Ctrl-X ends whatever is left.
  pause-lcd-cancel   the job is paused as above and held for --hold seconds (use 25) so that YOU press Cancel job in the LCD menu; the script reports
                 what the link says (`Alarm` expected), then waits 40 s for you to acknowledge with SW on the status screen and reports `Idle`.
  jog            the `$J=` checks (REQ-CoreLR-D-SerialLink-0011): relative and absolute jogs, ten chained jogs, 0x85 in the middle of a 60 mm jog and at rest,
                 Candle's Z-only jog, the refusals, a jog during a job (error:8), and the Z pen word (`G1 Z0` / `G1 Z5`, read back with `$G`; the pen servo MOVES).
                 The head goes up to 60 mm along X and comes back. Prints OK / BAD per check; exits 1 if any is BAD.
  ring           the position ring (REQ-CoreLR-D-Calibration-0010), --cycles N times (default 8): open the port (DTR resets the board), check the machine is `Idle` and
                 at the position the previous session left (to 0.05 mm), jog to the next of N DISTINCT absolute targets (pseudo-random, fixed seed, within +-`--span` mm
                 of the origin, default 100, at least 10 mm apart, so a position cannot be restored "by chance"), check it got there, wait 3 s of stillness (the position is
                 stored after 2 s) and close the port. Then it returns to the ORIGIN (X0 Y0) and a last session checks it. Needs a calibrated machine in `Idle`; the pen is never
                 touched; the target area is the drawing area (the machine must be able to reach +-span around the origin). A machine that comes back uncalibrated, or elsewhere,
                 fails the run. About 10 s a cycle plus the travel.
  stream         `--file F.gc`: send a GCode file a line at a time (a line, its reply, the next), polling `?`; reports the time, the path the head made, the mean speed.
                 The head moves as the file says: the files of tests/bench/gcode stay inside +-100 mm and start and end pen up at the origin.
  abort          Candle's Abort (bench 2026-10-12): a long move of --dx mm (default 60) along X, Ctrl-X after --pause-after seconds, then, 47 ms after the welcome line and in one burst,
                 Candle's three lines built from the last position it read (`G21G53G90X..Y..Z0`, `G21G92X..Y..Z0`, `G54 G17 G21 G90 G94 M9 T0`) and `S0`. Checks: every reply `ok`, the machine
                 Idle, the head back on the position Candle had read (0.2 mm). Then returns the head and the work origin to where they were. Needs Idle, calibrated; Z0 lifts the pen.
  cancel-after-pause   as pause-resume, but instead of `~` sends Ctrl-X (the host's cancel) and says what the machine reports afterwards.

Usage:   serial_probe.py [-h] [--port DEV] [--pause-after S] [--hold S] [--log FILE] [--cycles N] [--span MM] {status,pause-resume,cancel-after-pause,pause-lcd-cancel,jog,ring,stream,abort}
Exit status: 0 = the scenario ran and the machine behaved (resumed / answered), 1 = it did not (said on the last lines), 2 = could not start
(no port, no welcome, not calibrated, not Idle).
Example: tools/bench/serial_probe.py status
         tools/bench/serial_probe.py --pause-after 3 --hold 1 pause-resume
"""
import argparse
import re
import select
import sys
import time
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import corelr_env as _env

try:
    import serial
except ImportError:
    sys.exit("serial_probe: pyserial is needed (pip install pyserial)")

T0 = time.time()
LOG = None


def say(kind, text):
    line = "%7.3f  %-3s %s" % (time.time() - T0, kind, text)
    print(line, flush=True)
    if LOG:
        LOG.write(line + "\n")
        LOG.flush()


def shown(data):
    return "".join(("<%02X>" % c) if c < 32 or c > 126 else chr(c) for c in data)


class Link:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0, write_timeout=2)
        self.buf = b""
        self.state = None            # last state word of a status report
        self.statuses = []           # (time, state) in order
        self.oks = 0
        self.errors = []
        self.replies = []            # every "ok" / "error:N", in order
        self.pos = None              # (x, y) of the last status report, mm
        self.welcome = False
        self.wco = None              # (x, y) of the last WCO field or [G92:..] line, mm

    def send(self, data, label=None):
        self.ser.write(data)
        say("TX", label or shown(data))

    def pump(self, seconds):
        """Read for `seconds`, split into lines, note statuses, oks and errors."""
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.ser], [], [], 0.02)
            if not r:
                continue
            self.buf += self.ser.read(256)
            while True:
                m = re.search(rb"[\r\n]+", self.buf)
                if not m:
                    break
                line, self.buf = self.buf[:m.start()], self.buf[m.end():]
                if line:
                    self.note(line.decode("ascii", "replace"))

    def note(self, line):
        say("RX", line)
        if line.startswith("Grbl"):
            self.welcome = True
        m = re.match(r"<([A-Za-z]+(?::\d)?)\|", line)
        if m:
            self.state = m.group(1)
            self.statuses.append((time.time() - T0, self.state))
            p = re.search(r"MPos:(-?[\d.]+),(-?[\d.]+)", line)
            if p:
                self.pos = (float(p.group(1)), float(p.group(2)))
            w = re.search(r"WCO:(-?[\d.]+),(-?[\d.]+)", line)
            if w:
                self.wco = (float(w.group(1)), float(w.group(2)))
        elif line.startswith("[G92:"):
            g = re.match(r"\[G92:(-?[\d.]+),(-?[\d.]+)", line)
            if g:
                self.wco = (float(g.group(1)), float(g.group(2)))
        elif line == "ok":
            self.oks += 1
            self.replies.append(line)
        elif line.startswith("error"):
            self.errors.append(line)
            self.replies.append(line)

    def work(self):
        """The last position read, in the drawing frame: MPos - WCO."""
        w = self.wco or (0.0, 0.0)
        return (self.pos[0] - w[0], self.pos[1] - w[1])

    def poll(self, wait=0.25):
        self.send(b"?")
        self.pump(wait)

    def wait_state(self, wanted, timeout):
        end = time.time() + timeout
        while time.time() < end:
            self.poll(0.2)
            if self.state in wanted:
                return True
        return False


def ask(link, text, wait=3.0):
    """Send one line, return its reply ("ok", "error:N") or None if none came within `wait` seconds."""
    n = len(link.replies)
    link.send((text + "\r").encode(), text)
    end = time.time() + wait
    while time.time() < end and len(link.replies) == n:
        link.pump(0.05)
    return link.replies[n] if len(link.replies) > n else None


def settle_idle(link, timeout=15.0):
    """Poll until Idle; return the list of distinct states seen on the way."""
    seen = []
    end = time.time() + timeout
    while time.time() < end:
        link.poll(0.15)
        if link.state not in seen:
            seen.append(link.state)
        if link.state == "Idle":
            return seen
    return seen


def run_jog(link):
    """The `$J=` / 0x85 / Z checks. Returns the number of failed checks. Moves the head by up to 60 mm along X and back."""
    bad = [0]

    def check(ok, what):
        say("OK " if ok else "BAD", what)
        if not ok:
            bad[0] += 1

    def near(v, want, tol):
        return v is not None and abs(v - want) <= tol

    link.poll(0.3)
    x0 = link.pos[0] if link.pos else 0.0
    y0 = link.pos[1] if link.pos else 0.0
    say("--", "start at X=%.3f Y=%.3f" % (x0, y0))
    # A. a relative jog
    r = ask(link, "$J=G21G91X20F600Z0"); seen = settle_idle(link)
    check(r == "ok" and "Jog" in seen and link.state == "Idle", "relative jog +20: reply %s, states %s" % (r, seen))
    check(near(link.pos[0], x0 + 20, 0.3), "  ... ended at X=%.3f (expected %.3f)" % (link.pos[0], x0 + 20))
    r = ask(link, "$J=G21G91X-20F600"); settle_idle(link)
    check(r == "ok" and near(link.pos[0], x0, 0.3), "relative jog -20: reply %s, X=%.3f" % (r, link.pos[0]))
    # B. a continuous jog: ten small jogs chained
    rs = [ask(link, "$J=G21G91X2F600") for _ in range(10)]; settle_idle(link)
    check(all(r == "ok" for r in rs) and near(link.pos[0], x0 + 20, 0.4), "ten chained jogs of +2: replies %s, X=%.3f (expected %.3f)" % ("all ok" if all(r == "ok" for r in rs) else rs, link.pos[0], x0 + 20))
    r = ask(link, "$J=G21G90X%.3fY%.3fF600" % (x0 - (link.wco or (0, 0))[0], y0 - (link.wco or (0, 0))[1])); settle_idle(link)
    check(r == "ok" and near(link.pos[0], x0, 0.3), "absolute jog back to X=%.3f: reply %s, X=%.3f" % (x0, r, link.pos[0]))
    # C. 0x85 in the middle of a long jog
    r = ask(link, "$J=G21G91X60F600"); t = time.time()
    while time.time() - t < 6 and not (link.state == "Jog" and link.pos and link.pos[0] > x0 + 6):
        link.poll(0.1)
    xc = link.pos[0]
    link.send(b"\x85", "0x85 (jog cancel)")
    seen = settle_idle(link, 6.0); xs = link.pos[0]
    link.poll(0.6); link.poll(0.6)
    check(r == "ok" and link.state == "Idle", "0x85: reply %s, final state %s, states %s" % (r, link.state, seen))
    check(xc < xs < x0 + 60 - 5, "  ... the head stopped at X=%.3f (cancel at %.3f, the jog would have ended at %.3f)" % (xs, xc, x0 + 60))
    check(near(link.pos[0], xs, 0.05), "  ... and stayed there (X=%.3f)" % link.pos[0])
    check(xs - xc < 9.0, "  ... it stopped %.1f mm after the cancel (ramp + queued blocks)" % (xs - xc))
    r = ask(link, "$J=G21G90X%.3fY%.3fF600" % (x0 - (link.wco or (0, 0))[0], y0 - (link.wco or (0, 0))[1])); settle_idle(link)
    check(r == "ok" and near(link.pos[0], x0, 0.3), "back to X=%.3f: reply %s, X=%.3f" % (x0, r, link.pos[0]))
    # D. 0x85 at rest does nothing
    link.send(b"\x85", "0x85 at rest"); link.poll(0.4)
    check(link.state == "Idle", "0x85 at rest: state %s" % link.state)
    # E. Candle's Z buttons: accepted, nothing moves
    xp = link.pos[0]
    r = ask(link, "$J=G21G91Z5F100"); link.poll(0.4)
    check(r == "ok" and link.state == "Idle" and near(link.pos[0], xp, 0.01), "Z-only jog: reply %s, state %s" % (r, link.state))
    # F. refusals
    for line, want in (("$J=G1X5", "error:20"), ("$J=G91X3000", "error:33"), ("$J=G91X", "error:2"), ("$J=", "error:3"), ("$J=M3", "error:20")):
        r = ask(link, line); check(r == want, "%s -> %s (expected %s)" % (line, r, want))
    link.poll(0.3); check(link.state == "Idle", "after the refusals the state is %s" % link.state)
    # G. a jog during a job is refused
    r1 = ask(link, "G91"); r2 = ask(link, "G1 X10"); r3 = ask(link, "$J=G91X1")
    seen = settle_idle(link)
    check(r1 == "ok" and r2 == "ok" and r3 == "error:8", "a jog during a job: G91 %s, G1 X10 %s, $J= %s (expected error:8)" % (r1, r2, r3))
    r = ask(link, "G1 X-10"); settle_idle(link)
    check(r == "ok" and near(link.pos[0], x0, 0.3), "back from the job: reply %s, X=%.3f" % (r, link.pos[0]))
    ask(link, "G90"); settle_idle(link)
    # H. the Z pen word (moves the pen servo)
    def modal():
        n = len(link.replies); link.send(b"$G\r", "$G"); link.pump(0.4)
        return link.buf
    r = ask(link, "G90"); r1 = ask(link, "G1 Z0"); settle_idle(link)
    link.pump(0.1)
    out = []
    old = link.note
    def grab(line, _o=old):
        out.append(line); _o(line)
    link.note = grab
    ask(link, "$G", 1.5); link.pump(0.3)
    link.note = old
    check(r1 == "ok" and any("M3" in l for l in out), "G1 Z0 -> pen down: reply %s, $G says %s" % (r1, [l for l in out if l.startswith("[GC")]))
    out[:] = []; link.note = grab
    r2 = ask(link, "G1 Z5"); settle_idle(link); ask(link, "$G", 1.5); link.pump(0.3); link.note = old
    check(r2 == "ok" and any("M5" in l for l in out), "G1 Z5 -> pen up: reply %s, $G says %s" % (r2, [l for l in out if l.startswith("[GC")]))
    r = ask(link, "M5"); settle_idle(link)
    link.poll(0.3)
    say("==", "%d check(s) failed; final state %s, X=%.3f" % (bad[0], link.state, link.pos[0]))
    return bad[0]


def run_stream(link, path):
    """Send a GCode file the simplest way (a line, its reply, the next line), polling `?` meanwhile; report the time and the speed the head really made.
    The planner fills as lines are accepted, so this exercises the look-ahead; it does not count bytes. Returns 0 if every line was acknowledged and the machine ended Idle."""
    lines = []
    for raw in open(path):
        t = re.sub(r"\(.*?\)|;.*", "", raw).strip()
        if t:
            lines.append(t)
    say("--", "%s: %d lines" % (path, len(lines)))
    t0 = time.time(); travelled = 0.0; last = link.pos; maxspeed = 0.0; lastt = t0; err = None
    def track():
        nonlocal travelled, last, maxspeed, lastt
        if link.pos and last:
            d = ((link.pos[0] - last[0]) ** 2 + (link.pos[1] - last[1]) ** 2) ** 0.5
            travelled += d
            now = time.time()
            if now - lastt > 0.05:
                maxspeed = max(maxspeed, d / (now - lastt)) if d < 20 else maxspeed
            lastt = now
        last = link.pos
    for n, t in enumerate(lines):
        r = None
        link.send((t + "\r").encode(), t)
        n0 = len(link.replies)
        end = time.time() + 60
        while time.time() < end and len(link.replies) == n0:
            link.pump(0.03)
            if int((time.time() - t0) * 4) != getattr(run_stream, "_tick", -1):
                run_stream._tick = int((time.time() - t0) * 4)
                link.send(b"?", "?"); link.pump(0.03); track()
        r = link.replies[n0] if len(link.replies) > n0 else None
        if r != "ok":
            err = "line %d '%s' -> %s" % (n + 1, t, r)
            break
    end = time.time() + 120
    while time.time() < end:
        link.poll(0.2); track()
        if link.state == "Idle":
            break
    el = time.time() - t0
    say("==", "%s: %s in %.1f s; path seen %.0f mm, mean %.1f mm/s, final state %s" % (path, "ended" if not err else "STOPPED at " + err, el, travelled, travelled / el if el else 0, link.state))
    return 0 if (err is None and link.state == "Idle") else 1


def start(port):
    link = Link(port)
    say("--", "port open: DTR reset the board, waiting for the welcome line")
    link.ser.reset_input_buffer()
    end = time.time() + 6.0
    while time.time() < end and not link.welcome:
        link.pump(0.2)
    if not link.welcome:
        say("!!", "no welcome line within 6 s")
        return None
    link.pump(0.5)
    link.send(b"$#\r", "$#")        # the work offset: absolute G-code is in the drawing frame, MPos in the machine frame (2026-10-12)
    link.pump(0.6)
    return link


MOVES = ["G91"] + ["G1 X%d F600" % d for d in (40, -40, 40, -40, 40, -40)] + ["G90"]


def ring_targets(cycles, span, start):
    """`cycles` DISTINCT absolute targets (mm), pseudo-random (fixed seed: the run can be repeated) in +-span around the origin, at least 10 mm apart
    and not within 3 mm of the origin or of each other's axis values, ordered nearest-neighbour from `start` to keep the travel short. No two coincide, so a
    restored position cannot match the previous one by chance."""
    import random
    rnd = random.Random(20261011)
    pts = []
    while len(pts) < cycles:
        p = (round(rnd.uniform(-span, span), 1), round(rnd.uniform(-span, span), 1))
        if all(((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) ** 0.5 >= 10.0 and abs(p[0] - q[0]) > 0.5 and abs(p[1] - q[1]) > 0.5 for q in pts) \
                and (p[0] ** 2 + p[1] ** 2) ** 0.5 >= 3.0:
            pts.append(p)
    order, cur = [], start
    while pts:
        n = min(pts, key=lambda q: (q[0] - cur[0]) ** 2 + (q[1] - cur[1]) ** 2)
        pts.remove(n); order.append(n); cur = n
    return order


def run_ring(port, cycles, span):
    """Close the port after each move and check, at every reopening, that the machine is where it was left. Returns the number of failures (2 if it could not start).
    Sessions: `cycles` moves to distinct absolute targets, the return to the origin, a last look."""
    bad = 0
    expect = None
    targets = None
    wco_end = None
    seen = []                                         # positions at which the machine was found after a reset
    for s in range(cycles + 2):
        try:
            link = start(port)
        except serial.SerialException as e:
            say("!!", "cannot open %s: %s" % (port, e))
            return 2
        if link is None:
            return 2
        for _ in range(3):
            link.poll(0.3)
        if link.state != "Idle" or link.pos is None:
            say("BAD", "session %d: after the reset the state is %s (not Idle): the calibration was lost" % (s, link.state))
            link.ser.close()
            return bad + 1
        if expect is not None:
            ok = abs(link.pos[0] - expect[0]) <= 0.05 and abs(link.pos[1] - expect[1]) <= 0.05
            say("OK " if ok else "BAD", "session %d: back at X=%.3f Y=%.3f, left at X=%.3f Y=%.3f" % (s, link.pos[0], link.pos[1], expect[0], expect[1]))
            bad += 0 if ok else 1
            seen.append(link.pos)
        else:
            say("--", "start at X=%.3f Y=%.3f; %d targets within +-%g mm of the origin, then the origin" % (link.pos[0], link.pos[1], cycles, span))
            targets = ring_targets(cycles, span, link.pos)
        if s <= cycles:
            tx, ty = targets[s] if s < cycles else (0.0, 0.0)
            r = ask(link, "$J=G21G90X%.1fY%.1fF1200" % (tx, ty), 5.0); st = settle_idle(link, 60.0)
            wx, wy = link.work() if link.pos else (1e9, 1e9)
            reached = link.pos is not None and abs(wx - tx) <= 0.3 and abs(wy - ty) <= 0.3
            say("OK " if (r == "ok" and reached) else "BAD", "session %d: jog to X=%.1f Y=%.1f: reply %s, now X=%.3f Y=%.3f" % (s, tx, ty, r, link.pos[0], link.pos[1]))
            bad += 0 if (r == "ok" and reached) else 1
            if r != "ok":                             # refused (out of reach? not Idle?): nothing moved, going on would prove nothing
                say("!!", "the jog was refused, stopping here")
                link.ser.close()
                return bad
        finish(link)                                  # 3 s of stillness: the position is stored, then the port may be closed
        expect = link.pos
        wco_end = link.wco
        link.ser.close()
        time.sleep(0.5)
    distinct = len(set((round(p[0], 1), round(p[1], 1)) for p in seen[:-1]))
    say("OK " if distinct == len(seen) - 1 else "BAD", "the machine was found at %d different positions after %d resets" % (distinct, len(seen) - 1))
    bad += 0 if distinct == len(seen) - 1 else 1
    ok = abs(expect[0] - (wco_end or (0, 0))[0]) <= 0.3 and abs(expect[1] - (wco_end or (0, 0))[1]) <= 0.3
    say("OK " if ok else "BAD", "the head ends at the drawing origin (MPos X=%.3f Y=%.3f, WCO %s)" % (expect[0], expect[1], wco_end))
    bad += 0 if ok else 1
    say("==", "%d check(s) failed over %d reopenings" % (bad, cycles + 2))
    return bad


def run_job(link, pause_after, action, hold):
    """Stream MOVES one line at a time (send, wait for ok), polling `?`; pause, then resume or cancel."""
    sent = link.oks
    i, paused_at, resumed_at, result = 0, None, None, None
    t_start, t_job = time.time(), None
    pending = False
    action_done = False
    while time.time() - t_start < 40:
        if not pending and i < len(MOVES) and paused_at is None:
            link.send((MOVES[i] + "\r").encode(), MOVES[i])
            pending, i = True, i + 1
        before = link.oks
        link.poll(0.15)
        if link.oks > before:
            pending = False
        if t_job is None and link.state == "Run":
            t_job = time.time()
            say("--", "state Run: the job is moving")
        if t_job is not None and paused_at is None and time.time() - t_job >= pause_after:
            link.send(b"!", "! (feed hold)")
            paused_at = time.time()
        if paused_at is not None and not action_done:
            if resumed_at is not None and action == "lcdcancel" and link.state == "Alarm":
                result = "cancelled from the LCD %.1f s after Hold:0: state Alarm" % (time.time() - resumed_at)
                action_done = True
                break
            if resumed_at is not None and action == "lcdcancel" and time.time() - resumed_at > hold:
                result = "FAILED: nobody cancelled from the LCD within %.0f s of Hold:0" % hold
                action_done = True
                break
            if resumed_at is not None and action != "lcdcancel" and link.state == "Run":
                result = "resumed by someone else (the LCD?) %.1f s after Hold:0: state Run" % (time.time() - resumed_at)
                action_done = True
                break
            if link.state == "Hold:0":
                if resumed_at is None:
                    resumed_at = time.time()
                    say("--", "Hold:0 after %.2f s" % (resumed_at - paused_at))
                if time.time() - resumed_at >= hold:
                    if action == "resume":
                        link.send(b"~", "~ (cycle start / resume)")
                        link.pump(0.1)
                        ok = link.wait_state(("Run", "Idle"), 3.0)
                        result = "resumed: %s" % link.state if ok else "RESUME FAILED: state is still %s 3 s after ~" % link.state
                    else:
                        link.send(b"\x18", "Ctrl-X (cancel)")
                        link.pump(1.5)
                        link.poll(0.3)
                        result = "after Ctrl-X the state is %s" % link.state
                    action_done = True
                    break
            elif resumed_at is None and time.time() - paused_at > 5.0:
                result = "NO Hold:0 within 5 s of ! (state %s)" % link.state
                break
    return result, pending, i


def run_abort(link, dx, after):
    """Candle's Abort, reproducibly: a long move along X, Ctrl-X `after` seconds into it, then -- 47 ms after the welcome line, in one burst, as Candle does -- its three lines
    built from the last position it had read (G53 back to it, G92 to re-label it, the parser state), then S0. Reports whether the head came back to that position, whether the
    machine is Idle, and the replies. Afterwards it puts the head and the work origin back where they were. Moves the machine along X by up to DX mm; the pen is lifted by Z0."""
    link.send(b"$#\r", "$#")
    link.pump(0.6)
    link.poll(0.3)
    start_m, wco0 = link.pos, link.wco
    if start_m is None or wco0 is None:
        say("!!", "no position or WCO read: nothing sent")
        return 1
    say("--", "start: MPos %s, WCO %s" % (start_m, wco0))
    ask(link, "G90")
    link.send(("G1 X%.3f F600\r" % (start_m[0] - wco0[0] + dx)).encode(), "G1 (the long move)")
    t0 = time.time()
    last = start_m
    while time.time() - t0 < after:
        link.poll(0.15)
        if link.pos:
            last = link.pos
    say("--", "last position read before the Ctrl-X: %s" % (last,))
    link.send(b"\x18", "Ctrl-X")
    link.welcome = False
    n = len(link.replies)
    tc = time.time()
    while not link.welcome and time.time() - tc < 5.0:
        link.pump(0.01)
    time.sleep(0.047)
    burst = ["G21G53G90X%.3fY%.3fZ0" % last, "G21G92X%.3fY%.3fZ0" % (last[0] - wco0[0], last[1] - wco0[1]),
             "G54 G17 G21 G90 G94 M9 T0", "S0"]
    link.send(("\r".join(burst) + "\r").encode(), " | ".join(burst))
    end = time.time() + 15
    while time.time() < end and len(link.replies) < n + len(burst):
        link.pump(0.05)
    seen = settle_idle(link, 15.0)
    link.pump(0.3)
    link.poll(0.3)
    say("==", "replies to the burst: %s" % link.replies[n:n + len(burst)])
    say("==", "final MPos %s against the position Candle had read %s: difference (%.3f, %.3f) mm; state %s (%s)" %
        (link.pos, last, link.pos[0] - last[0], link.pos[1] - last[1], link.state, seen))
    bad = 0 if (link.replies[n:n + len(burst)] == ["ok"] * len(burst) and link.state == "Idle"
                and abs(link.pos[0] - last[0]) < 0.2 and abs(link.pos[1] - last[1]) < 0.2) else 1
    # put things back: the head where it started, the work origin as it was
    ask(link, "G53 G90 G0 X%.3f Y%.3f" % start_m, 10.0)
    settle_idle(link, 20.0)
    ask(link, "G92 X%.3f Y%.3f" % (start_m[0] - wco0[0], start_m[1] - wco0[1]))
    link.poll(0.4)
    say("==", "back: MPos %s, WCO %s (started at %s, %s)" % (link.pos, link.wco, start_m, wco0))
    return bad


def finish(link):
    """Whatever is left: Ctrl-X if not Idle, then G90 and the last state."""
    link.poll(0.3)
    if link.state not in ("Idle",):
        link.send(b"\x18", "Ctrl-X (cleanup)")
        link.pump(2.0)
        link.poll(0.3)
    if link.state == "Idle":
        # The position is stored after CALIBRATION_POSITION_STORE_DELAY_MS of stillness; closing the port may reset the board (the DTR line), and a reset before the
        # store costs the calibration (found 2026-10-10: a probe that exited 0.7 s after its last move left the machine uncalibrated). So wait it out.
        say("--", "waiting 3 s of stillness before the port is closed (the position is stored after 2 s)")
        for _ in range(10):
            link.poll(0.3)
    say("--", "final state: %s" % link.state)


def main():
    global LOG
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="Exit status: 0 behaved, 1 did not, 2 could not start.")
    ap.add_argument("scenario", choices=("status", "pause-resume", "cancel-after-pause", "pause-lcd-cancel", "jog", "ring", "stream", "abort"))
    ap.add_argument("--port", default=_env.get("CORELR_PORT", "/dev/ttyUSB0"), help="serial port (default: CORELR_PORT of local.conf, else /dev/ttyUSB0)")
    ap.add_argument("--file", help="the GCode file for the `stream` scenario")
    ap.add_argument("--cycles", type=int, default=8, help="moves, each followed by a close and a reopening, for the `ring` scenario (default 8)")
    ap.add_argument("--span", type=float, default=100.0, help="half-width in mm of the square around the origin the `ring` targets are drawn from (default 100)")
    ap.add_argument("--dx", type=float, default=60.0, help="length in mm of the long move of the `abort` scenario (default 60)")
    ap.add_argument("--pause-after", type=float, default=3.0, help="seconds of motion before `!` (default 3)")
    ap.add_argument("--hold", type=float, default=1.0, help="seconds to stay in Hold:0 before ~ (default 1)")
    ap.add_argument("--log", default="/tmp/serial_probe.log", help="log file (default /tmp/serial_probe.log)")
    a = ap.parse_args()
    LOG = open(a.log, "w")
    if a.scenario == "ring":
        return 1 if run_ring(a.port, a.cycles, a.span) else 0
    try:
        link = start(a.port)
    except serial.SerialException as e:
        say("!!", "cannot open %s: %s" % (a.port, e))
        return 2
    if link is None:
        return 2
    try:
        for _ in range(3):
            link.poll(0.3)
        if a.scenario == "status":
            say("--", "state: %s" % link.state)
            return 0
        if a.scenario == "stream":
            if link.state != "Idle" or not a.file:
                say("!!", "state %s, file %s: nothing sent (needs Idle and --file)" % (link.state, a.file))
                return 2
            bad = run_stream(link, a.file)
            finish(link)
            return bad
        if a.scenario == "abort":
            if link.state != "Idle":
                say("!!", "state is %s, not Idle: nothing sent" % link.state)
                return 2
            bad = run_abort(link, a.dx, a.pause_after)
            finish(link)
            return bad
        if a.scenario == "jog":
            if link.state != "Idle":
                say("!!", "state is %s, not Idle: nothing sent" % link.state)
                return 2
            bad = run_jog(link)
            say("--", "watching 5 s more, port open: with CORELR_DEBUG_JOBSTATE the last D: field is the position keeper's `stored` flag")
            for _ in range(17):
                link.poll(0.3)
            finish(link)
            return 1 if bad else 0
        if link.state != "Idle":
            say("!!", "state is %s, not Idle: nothing sent (calibrated? error? the LCD in a jog mode?)" % link.state)
            return 2
        result, pending, sent = run_job(link, a.pause_after, {"pause-resume": "resume", "cancel-after-pause": "cancel", "pause-lcd-cancel": "lcdcancel"}[a.scenario], a.hold)
        say("==", result or "the job ended without a pause being exercised")
        if result and a.scenario == "pause-resume" and result.startswith("resumed"):
            # let the rest of the job finish
            end = time.time() + 30
            while time.time() < end and link.state != "Idle":
                if not pending and sent < len(MOVES):
                    link.send((MOVES[sent] + "\r").encode(), MOVES[sent])
                    pending, sent = True, sent + 1
                before = link.oks
                link.poll(0.2)
                if link.oks > before:
                    pending = False
            say("==", "the rest of the job: %s" % ("finished, state Idle" if link.state == "Idle" else "did NOT finish, state %s" % link.state))
        if result and a.scenario == "pause-lcd-cancel" and result.startswith("cancelled"):
            say("--", "now acknowledge with SW on the status screen: waiting up to 40 s for Idle")
            acked = link.wait_state(("Idle",), 40.0)
            result = "acknowledged on the machine: Idle" if acked else "FAILED: still %s 40 s after the cancel" % link.state
            say("==", result)
        finish(link)
        bad = result is None or "FAILED" in result or result.startswith("NO ")
        return 1 if bad else 0
    finally:
        link.ser.close()


if __name__ == "__main__":
    sys.exit(main())
