#!/usr/bin/env python3
#
# candle_spy.py
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
"""candle_spy.py -- a FAKE grbl machine on a fake serial port, to see exactly what a sender (Candle) sends when a button is pressed. Touches no real machine.

Why: what Candle 1.1.9 sends for "Home", "Safe position", "Restore origin", "Zero XY" ... was read off its binary's strings and its settings, not off its source (not installed), and
the meaning of "Restore origin" depends on what `$#` answers. Instead of guessing, point Candle here, press the buttons, read the log.

It behaves as a PERMISSIVE grbl 1.1 with the machine/work split a real grbl has: the machine position `MPos`, a work offset `WCO` (what G92 / G10 L20 sets), `$#` answering `[G92:..]`,
`$H` a short `Home` state, `G53`, `G91`, `$J=` moving at once (no timing). Every line it does not know is answered `ok` AND logged as UNKNOWN: the point is to see it. Status polls (`?`) and the `$G` Candle sends every second are
not logged unless -s. Every line received is logged with its time, and every reply too.

  1. start it:                         tools/bench/candle_spy.py                     (creates /tmp/corelr_spy, a pseudo-terminal; Ctrl-C to stop; the log is also written to /tmp/candle_spy.log)
  2. point Candle at it WITHOUT touching your own settings -- a private configuration directory with the port changed:
         mkdir -p /tmp/candle_spy_cfg/Candle && cp ~/.config/Candle/candle.conf /tmp/candle_spy_cfg/Candle/ && sed -i 's#^port=.*#port=/tmp/corelr_spy#' /tmp/candle_spy_cfg/Candle/candle.conf
         XDG_CONFIG_HOME=/tmp/candle_spy_cfg candle
     (Qt takes a port name that begins with `/` as a path.) If Candle shows the old port anyway, type `/tmp/corelr_spy` in its Port box if it is editable, or use step 2b.
  2b. A port Candle lists by itself needs a device named like a serial port, which takes root:
         sudo socat PTY,link=/dev/ttyUSB9,raw,echo=0,mode=666 PTY,link=/tmp/corelr_spy_far,raw,echo=0      (leave it running)
         tools/bench/candle_spy.py --device /tmp/corelr_spy_far                                           then choose ttyUSB9 in Candle
  3. press Zero XY, Home, Safe position, Restore origin, jog, ...; read the lines the spy prints.

Cancelling in the middle of a job: by default every `ok` comes at once, so the whole file is "done" a few milliseconds after Candle sends it, and a later Abort interrupts nothing. With
`--ok-delay S` each line that moves the machine or changes the pen is acknowledged S seconds after the previous one (a queue, as a real machine's), the state reads `Run` while any
line waits, `!` holds the queue (`Hold:0`) and `~` resumes it, and Ctrl-X flushes it and sends the welcome line again. The log says, at `!` and at Ctrl-X, how many lines were still waiting:
that is the proof that the cancel took effect in the middle of the job.

Usage:   candle_spy.py [-h] [--link PATH | --device PATH] [--g92 X,Y] [--home-seconds S] [--ok-delay S] [-s] [--log FILE]
Options: --link PATH     where to put the pseudo-terminal (default /tmp/corelr_spy)
         --device PATH   use an existing serial device or pty end instead of making one (a `socat` pair, see 2b); needs pyserial
         --g92 X,Y       start with this work offset (mm), so that `$#` answers a non-zero `[G92:..]` and what "Restore origin" re-sends can be seen
         --home-seconds  how long `$H` reports the `Home` state (default 1.0)
         --ok-delay S    seconds between two acknowledged G-code lines (default 0: all at once); try 1 with a file, then Abort
         -s              also log the `?` polls and the `$G` Candle sends every second
Exit status: 0 after Ctrl-C, 2 if the port could not be set up.
Example: tools/bench/candle_spy.py --g92 12.5,-3
"""
import argparse, os, re, select, sys, time

PROMPT = "Grbl 1.1h ['$' for help]"
WORD = re.compile(r"([A-Z])(-?\d*\.?\d+)")


def fmt(v):
    """A coordinate as grbl prints it: three decimals, never '-0.000'."""
    s = "%.3f" % v
    return "0.000" if s == "-0.000" else s


class FakeGrbl:
    """The machine: a position, a work offset, a few modal words. No timing but `$H`'s `Home` state (`clock` is injectable for the test)."""

    def __init__(self, g92=(0.0, 0.0), home_seconds=1.0, clock=time.time):
        self.m = [0.0, 0.0]                 # machine position, mm
        self.wco = [g92[0], g92[1]]         # work coordinate offset: WPos = MPos - WCO
        self.relative = False
        self.pen = "M5"
        self.hold = False
        self.home_until = 0.0
        self.home_seconds = home_seconds
        self.clock = clock
        self.reports = 0
        self.unknown = []                   # lines it did not know, in order
        self.motion = "G0"
        self.busy = False                   # set by the Spy while acknowledgements are still waiting: the machine is running a job

    def welcome(self):
        self.__init__(g92=tuple(self.wco), home_seconds=self.home_seconds, clock=self.clock)   # a reset keeps the offset (grbl keeps G92 in RAM only; here it is the test's convenience)
        return "\r\n" + PROMPT + "\r\n"

    def state(self):
        if self.hold: return "Hold:0"
        if self.busy: return "Run"
        if self.clock() < self.home_until: return "Home"
        return "Idle"

    def status(self):
        """`<Idle|MPos:x,y,0.000|Bf:8,128|FS:0,0>`, with `|WCO:x,y,0.000` on the first report and every tenth one, as grbl does."""
        self.reports += 1
        wco = "|WCO:%s,%s,0.000" % (fmt(self.wco[0]), fmt(self.wco[1])) if (self.reports % 10) == 1 else ""
        return "<%s|MPos:%s,%s,0.000|Bf:8,128|FS:0,0%s>" % (self.state(), fmt(self.m[0]), fmt(self.m[1]), wco)

    def realtime(self, b):
        """A real-time byte; returns the text to send back ('' for none)."""
        if b == ord("?"): return self.status() + "\r\n"
        if b == ord("!"): self.hold = True; return ""
        if b == ord("~"): self.hold = False; return ""
        if b == 0x18: return self.welcome()
        return ""                            # 0x85 (jog cancel) and the 0x90.. overrides: nothing to do

    def line(self, text):
        """One line from the sender -> the list of reply lines (without line ends)."""
        t = text.strip().upper().replace(" ", "")
        if not t: return []
        if t == "$G": return ["[GC:%s G54 G17 G21 %s G94 %s M9 T0 F0 S0]" % (self.motion, "G91" if self.relative else "G90", self.pen), "ok"]
        if t == "$I": return ["[VER:1.1h.20201011:CoreLR spy]", "[OPT:V,8,128]", "ok"]
        if t == "$$": return ["$110=2000.000", "$111=2000.000", "$112=2000.000", "$120=200.000", "$121=200.000", "$122=200.000", "$130=1000.000", "$131=1000.000", "$132=1000.000", "ok"]
        if t == "$#":
            z = "0.000,0.000,0.000"
            return ["[G54:%s]" % z, "[G55:%s]" % z, "[G56:%s]" % z, "[G57:%s]" % z, "[G58:%s]" % z, "[G59:%s]" % z, "[G28:%s]" % z, "[G30:%s]" % z,
                    "[G92:%s,%s,0.000]" % (fmt(self.wco[0]), fmt(self.wco[1])), "[TLO:0.000]", "[PRB:0.000,0.000,0.000:0]", "ok"]
        if t == "$H":
            self.home_until = self.clock() + self.home_seconds; self.m = [0.0, 0.0]
            return ["ok"]
        if t == "$X": return ["[MSG:Caution: Unlocked]", "ok"]
        if t.startswith("$J="):
            self._move(t[3:], jog=True); return ["ok"]
        if t.startswith("$"):
            self.unknown.append(text.strip()); return ["ok"]
        self._gcode(t, text.strip())
        return ["ok"]

    def _words(self, t):
        return [(l, float(v)) for l, v in WORD.findall(t)]

    def _gcode(self, t, raw):
        ws = self._words(t)
        gs = [int(v) for l, v in ws if l == "G"]
        known = bool(ws)
        for l, v in ws:
            if l == "G" and int(v) not in (0, 1, 2, 3, 10, 17, 20, 21, 53, 54, 90, 91, 92, 94): known = False
            if l == "M" and int(v) not in (3, 5, 9, 2, 30): known = False
        if 92 in gs or 10 in gs:
            vals = {l: v for l, v in ws if l in "XY"}
            for i, a in enumerate("XY"):
                if a in vals: self.wco[i] = self.m[i] - vals[a]           # the pen is HERE and reads this
            return
        if 90 in gs: self.relative = False
        if 91 in gs: self.relative = True
        for l, v in ws:
            if l == "M" and int(v) in (3, 5): self.pen = "M%d" % int(v)
        mot = [g for g in gs if g in (0, 1, 2, 3)]
        if mot: self.motion = "G%d" % mot[-1]
        if any(l in "XY" for l, _ in ws) and (mot or self.motion):
            self._move(t, machine=(53 in gs))
        if not known:
            self.unknown.append(raw)

    def _move(self, t, jog=False, machine=False):
        rel = self.relative
        for l, v in self._words(t):
            if l == "G" and int(v) == 91: rel = True
            if l == "G" and int(v) == 90: rel = False
        for l, v in self._words(t):
            if l in "XY":
                i = "XY".index(l)
                if rel: self.m[i] += v
                elif machine: self.m[i] = v
                else: self.m[i] = self.wco[i] + v


class Spy:
    """The port side: reads bytes, handles real-time bytes at once, lines at their end, logs, replies."""

    def __init__(self, machine, write, log, show_status=False, clock=time.time, ok_delay=0.0):
        self.mc, self.write, self.log, self.show_status, self.clock = machine, write, log, show_status, clock
        self.ok_delay = ok_delay
        self.pending = []                   # (text, quiet) lines whose acknowledgement is still to come, in order
        self.next_due = 0.0
        self.buf = b""
        self.t0 = clock()

    def say(self, kind, text):
        self.log("%7.3f  %-3s %s" % (self.clock() - self.t0, kind, text))

    def reply(self, text):
        self.write(text.encode("ascii", "replace"))

    def delayed(self, text):
        """Does this line wait for its ok? A line that moves the machine or changes the pen: not a `$` command, not a comment or a blank."""
        t = re.sub(r"\(.*?\)|;.*", "", text).strip().upper()
        return self.ok_delay > 0 and bool(t) and not t.startswith("$") and bool(re.search(r"[GMXYZ]", t))

    def run_line(self, text, quiet):
        n = len(self.mc.unknown)
        for r in self.mc.line(text):
            if not quiet: self.say("TX", r)
            self.reply(r + "\r\n")
        if len(self.mc.unknown) > n: self.say("!!", "UNKNOWN to the spy (answered ok): %s" % text)

    def tick(self):
        """Time passes: acknowledge the waiting lines whose turn has come (not while held)."""
        now = self.clock()
        if self.mc.hold:
            self.next_due = now + self.ok_delay
        while self.pending and now >= self.next_due:
            text, quiet = self.pending.pop(0)
            self.run_line(text, quiet)
            self.next_due = now + self.ok_delay
        self.mc.busy = bool(self.pending)

    def feed(self, data):
        for b in data:
            if b in (ord("?"), ord("!"), ord("~"), 0x18) or b >= 0x80:
                waiting = len(self.pending)
                if b == 0x18: self.pending = []; self.mc.busy = False
                out = self.mc.realtime(b)
                if b != ord("?") or self.show_status: self.say("RX", "<%02X>%s" % (b, " ?" if b == ord("?") else ""))
                if b in (ord("!"), 0x18): self.say("--", "%s: %d line(s) were still waiting for their ok%s" % ("hold" if b == ord("!") else "reset", waiting, " -- NOT in the middle of a job" if waiting == 0 else " -- the job was interrupted"))
                if out:
                    if b != ord("?") or self.show_status: self.say("TX", out.strip())
                    self.reply(out)
            elif b in (10, 13):
                if self.buf:
                    text = self.buf.decode("ascii", "replace"); self.buf = b""
                    quiet = (text.strip().upper() == "$G") and not self.show_status      # Candle asks every second: noise
                    if not quiet: self.say("RX", text)
                    if self.delayed(text):
                        if not self.pending: self.next_due = max(self.next_due, self.clock() + self.ok_delay)
                        self.pending.append((text, quiet)); self.mc.busy = True
                    else:
                        self.run_line(text, quiet)
            else:
                self.buf += bytes([b])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--link", default="/tmp/corelr_spy", help="where to put the pseudo-terminal (default /tmp/corelr_spy)")
    ap.add_argument("--device", help="an existing serial device or pty end instead of a new pty (needs pyserial)")
    ap.add_argument("--g92", default="0,0", metavar="X,Y", help="the initial work offset, mm (default 0,0)")
    ap.add_argument("--home-seconds", type=float, default=1.0, help="how long $H reports Home (default 1.0)")
    ap.add_argument("--ok-delay", type=float, default=0.0, metavar="S", help="seconds between two acknowledged G-code lines (default 0: all at once)")
    ap.add_argument("-s", action="store_true", help="also log the ? polls")
    ap.add_argument("--log", default="/tmp/candle_spy.log", help="log file (default /tmp/candle_spy.log)")
    a = ap.parse_args(argv)
    try:
        g92 = tuple(float(v) for v in a.g92.split(","))
        assert len(g92) == 2
    except (ValueError, AssertionError):
        print("candle_spy.py: --g92 X,Y in mm", file=sys.stderr); return 2
    logf = open(a.log, "w")
    def log(line):
        print(line, flush=True); logf.write(line + "\n"); logf.flush()
    try:
        if a.device:
            import serial
            ser = serial.Serial(a.device, 115200, timeout=0)
            fd = ser.fileno(); write = ser.write; where = a.device
        else:
            import tty
            master, slave = os.openpty(); tty.setraw(master); tty.setraw(slave)
            if os.path.lexists(a.link): os.remove(a.link)
            os.symlink(os.ttyname(slave), a.link)
            fd = master; write = lambda b: os.write(master, b); where = a.link + " -> " + os.ttyname(slave)
    except Exception as e:
        print("candle_spy.py: cannot set up the port: %s" % e, file=sys.stderr); return 2
    spy = Spy(FakeGrbl(g92=g92, home_seconds=a.home_seconds), write, log, show_status=a.s, ok_delay=a.ok_delay)
    log("candle_spy: a fake grbl is listening on %s  (Ctrl-C to stop; log: %s)" % (where, a.log))
    announced = False
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.02)
            spy.tick()
            if r:
                try: data = os.read(fd, 256)
                except OSError: time.sleep(0.2); continue          # nobody has the other end open yet
                if data:
                    if not announced:
                        announced = True; spy.say("--", "the sender is talking")
                    spy.feed(data)
    except KeyboardInterrupt:
        log("candle_spy: stopped")
    if not a.device and os.path.lexists(a.link): os.remove(a.link)
    return 0


if __name__ == "__main__":
    sys.exit(main())
