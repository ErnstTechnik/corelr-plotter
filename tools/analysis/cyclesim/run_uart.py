# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cycles of the UART interrupts (M-7 of docs/protocol/SerialProtocol.md): python3 run_uart.py out/t_uart.bin
Built by build.sh from t_uart.c with Uart.c; the cost is the handler alone, entry to RETI, as the other t_isr*.c do."""
import sys, argparse
from avrsim import AVR, load
_p = argparse.ArgumentParser(description=__doc__)
_p.add_argument('bin', metavar='BIN', help='the .bin built by build.sh from t_uart.c')
_a = _p.parse_args()
cpu = AVR(load(_a.bin)); cpu.run()
marks = cpu.marks
cases = [(1, 2, 'receive: a data byte'), (3, 4, 'receive: a real-time byte'), (5, 6, 'receive: the ring is full (a gap is noted)'),
         (7, 8, 'receive: a framing error'), (9, 10, 'transmit: a byte, more to send'), (11, 12, 'transmit: the last byte')]
for a, b, name in cases:
    s = [c for c, v in marks if v == a]; e = [c for c, v in marks if v == b]
    d = [y - x for x, y in zip(s, e)]
    print("%-46s %s cycles  (%s us at 16 MHz)" % (name, "/".join(map(str, sorted(set(d)))), "/".join("%.1f" % (x / 16.0) for x in sorted(set(d)))))
