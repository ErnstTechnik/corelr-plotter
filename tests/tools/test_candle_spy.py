#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""test_candle_spy.py -- tests of the protocol engine of tools/bench/candle_spy.py (no port involved).

Usage:   test_candle_spy.py [-h]
Exit status: 0 if all pass, 1 otherwise.
"""
import os, sys
if "-h" in sys.argv or "--help" in sys.argv:
    print(__doc__); sys.exit(0)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "bench"))
import candle_spy as c

fails = 0
def check(name, got, want):
    global fails
    if got != want:
        fails += 1; print("FAIL %s: got %r want %r" % (name, got, want))

class Clk:
    t = 100.0
    def __call__(self): return self.t

def mk(g92=(0.0, 0.0)):
    clk = Clk(); return c.FakeGrbl(g92=g92, clock=clk, home_seconds=1.0), clk

m, clk = mk()
check("g0", (m.line("G0 X10 Y5"), m.m), (["ok"], [10.0, 5.0]))
m.line("G91"); m.line("G1 X-3"); check("relative", m.m, [7.0, 5.0])
m.line("G90"); m.line("G1 X1")
check("absolute again", m.m, [1.0, 5.0])
m.line("G92 X0 Y0"); check("g92 offset", m.wco, [1.0, 5.0])
check("g92 not a move", m.m, [1.0, 5.0])
check("$# g92", [l for l in m.line("$#") if l.startswith("[G92")], ["[G92:1.000,5.000,0.000]"])
m.line("G0 X2 Y2"); check("work move uses offset", m.m, [3.0, 7.0])
m.line("G53 G0 X0 Y0"); check("g53 machine", m.m, [0.0, 0.0])
m.line("G10 L20 P1 X0 Y0"); check("g10 sets offset", m.wco, [0.0, 0.0])
m.line("$J=G91 X5 Y-2"); check("jog", m.m, [5.0, -2.0])
m.line("G0 X 4 Y 1"); check("spaces", m.m, [4.0, 1.0])
check("neg zero", c.fmt(-0.0001), "0.000")

# real-time bytes and report
m, clk = mk((2.0, 3.0))
check("first report has WCO", "WCO:2.000,3.000,0.000" in m.status(), True)
for i in range(9): check("report %d no WCO" % (i + 2), "WCO" in m.status(), False)
r = [m.status() for _ in range(2)]
check("11th report WCO", "WCO" in r[0], True)
check("idle", m.status().startswith("<Idle|"), True)
m.realtime(ord("!")); check("hold", m.status().startswith("<Hold:0|"), True)
m.realtime(ord("~")); check("resume", m.status().startswith("<Idle|"), True)
m.m=[5.0,5.0]; m.line("$H"); check("home zeroes MPos", m.m, [0.0, 0.0]); check("home state", m.status().startswith("<Home|"), True)
clk.t += 1.5; check("home done", m.status().startswith("<Idle|"), True)
check("reset welcome", c.PROMPT in m.realtime(0x18), True)
check("reset keeps offset", m.wco, [2.0, 3.0])

# unknown lines are answered ok and flagged
m, clk = mk()
check("unknown ok", m.line("FOO"), ["ok"]); check("unknown flagged", m.unknown, ["FOO"])
m.line("G99 X1"); m.line("$21=1"); check("unknown list", m.unknown, ["FOO", "G99 X1", "$21=1"])
m.line("M5"); m.line("G0 X1"); check("known not flagged", len(m.unknown), 3)
check("pen in $G", "M5" in m.line("$G")[0], True)
m.line("M3"); check("pen M3 in $G", "M3" in m.line("$G")[0], True)
check("$G distance", ("G90" in m.line("$G")[0], "G91" in m.line("$G")[0]), (True, False))
m.line("G91"); check("$G distance rel", ("G90" in m.line("$G")[0], "G91" in m.line("$G")[0]), (False, True))
check("blank", m.line("  "), [])

# the Spy: framing, logging, replies
out, logs = [], []
sp = c.Spy(c.FakeGrbl(), out.append, logs.append)
sp.feed(b"G0 X1\r\n$#\n?"); 
check("replies written", b"ok\r\n" in out, True)
check("status reply", any(o.startswith(b"<Idle") for o in out), True)
check("poll not logged", any("?" in l for l in logs), False)
sp2 = c.Spy(c.FakeGrbl(), lambda b: None, logs.append, show_status=True); n = len(logs); sp2.feed(b"?"); check("poll logged with -s", len(logs) > n, True)
logs.clear(); sp.feed(b"$G\n"); check("$G not logged", logs, []); check("$G still answered", any(o.startswith(b"[GC:") for o in out), True)
logs.clear(); sp.feed(b"BAR\n"); check("unknown banner", any("UNKNOWN" in l for l in logs), True)
logs.clear(); sp.feed(b"G0 X"); sp.feed(b"2\n"); check("split line", any("RX  G0 X2" in l for l in logs), True)


# --ok-delay: acknowledgements one at a time, Run while some wait, hold, resume, flush on Ctrl-X
clk = Clk(); out, logs = [], []
sp = c.Spy(c.FakeGrbl(clock=clk), out.append, logs.append, clock=clk, ok_delay=1.0)
sp.feed(b"G0 X1\nG1 X2\n$G\n; comment\n")
check("delay: $G and comments at once", (sum(o.startswith(b"[GC:") for o in out), b"ok\r\n" in out), (1, True))
out.clear(); sp.tick(); check("delay: no ok yet", out, [])
check("delay: Run while lines wait", sp.mc.status().startswith("<Run|"), True)
clk.t += 1.1; sp.tick(); check("delay: first ok after the delay", out.count(b"ok\r\n"), 1)
check("delay: first line executed, second not", sp.mc.m, [1.0, 0.0])
clk.t += 1.0; sp.tick(); check("delay: second ok", out.count(b"ok\r\n"), 2)
check("delay: Idle when the queue is empty", sp.mc.status().startswith("<Idle|"), True)
sp.feed(b"G0 X5\nG0 X6\nG0 X7\n"); logs.clear(); out.clear()
sp.feed(b"!"); check("hold logged with the waiting count", any("3 line(s) were still waiting" in l and "interrupted" in l for l in logs), True)
clk.t += 5.0; sp.tick(); check("held: no ok", out, [])
check("held: state", sp.mc.status().startswith("<Hold:0|"), True)
sp.feed(b"~"); clk.t += 1.1; sp.tick(); check("resumed: acknowledged again", out.count(b"ok\r\n"), 1)
logs.clear(); sp.feed(b"\x18"); check("reset logged: lines flushed", any("2 line(s) were still waiting" in l for l in logs), True)
out.clear(); clk.t += 10.0; sp.tick(); check("flushed lines never answered", [o for o in out if o == b"ok\r\n"], [])
logs.clear(); sp.feed(b"\x18"); check("reset with nothing waiting says so", any("0 line(s)" in l and "NOT in the middle" in l for l in logs), True)
sp0 = c.Spy(c.FakeGrbl(clock=clk), lambda b: None, logs.append, clock=clk)           # default: no delay, nothing ever waits
sp0.feed(b"G0 X1\n"); check("no delay by default", (sp0.pending, sp0.mc.m), ([], [1.0, 0.0]))

print("%s (%d failure%s)" % ("PASS" if not fails else "FAIL", fails, "" if fails == 1 else "s"))
sys.exit(1 if fails else 0)
