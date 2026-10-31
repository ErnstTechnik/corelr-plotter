# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys, statistics
sys.path.insert(0, '../cyclesim')
from avrsim import AVR, load
cpu = AVR(load('out/t_evgen_cyc.bin')); cpu.run()
m = [c for c, v in cpu.marks]
d = [m[i + 1] - m[i] for i in range(0, len(m) - 1, 2)]
d = d[:-1]                       # the last call returns 0 (no event)
tot = sum(d)
heavy = [x for x in d if x > 6000]; light = [x for x in d if x <= 6000]
print("entries: %d   total %d cycles   mean %.0f cycles/entry" % (len(d), tot, tot / len(d)))
print("calls that began a segment (>6000 cycles): %d, mean %.0f ;  other calls: %d, mean %.0f, max %d" %
      (len(heavy), statistics.mean(heavy) if heavy else 0, len(light), statistics.mean(light), max(light)))
secs = 30000 / 21500.0
print("move of 30 mm at 21.5 mm/s lasts %.2f s  ->  generator load %.1f %% of the CPU (Motion alone)" % (secs, 100.0 * tot / (secs * 16e6)))
