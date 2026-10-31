# arcproto — the geometry kernel for G2/G3, measured before it was chosen

Two candidate ways to turn an angle into a point on a circle, written as integer-only C that
compiles for the PC and for the AVR simulator, and measured. The maintainer's rule for arcs
(the maintainer's notes, 2026-09): *compute the absolute position at every step; never add deltas* — so both
are **absolute**, not recurrences.

Angles are `uint32_t` turns, Q32 (2^32 = one full turn): a full circle wraps for free.

| | `cordic.c` (kept here) | the polynomial (now `firmware/src/Trig.c`) |
|---|---|---|
| Method | 24 iterations of shift-and-add, rotation *and* vectoring (atan2) with one table | sin/cos Taylor on one octant, Q32 fractions, product = high word of 32×32→64; atan2 by a minimax polynomial on the ratio min/max, in the same loop |
| Worst error, 2 M random cases, radius up to 2 m | 0.74 µm (rotate), 1.14 µm (vector angle at the radius) | rotate **0.55 µm**, angle **0.36 µm** |
| Flash | 514 + 504 + 96 (table) = ~1 100 | `Trig_Rotate` 508 + `Trig_Angle` 558 + the shared loop 114 + multiply 20 + tables 60 = **~1 260** |
| Cycles per call | 6 074 (rotate), 6 589 (vector) | ~4 300 (rotate), ~4 000 (angle) |

Both are exact at the cardinal directions and the octant boundaries. At 100 waypoints/s a call is 2.5–4 % of
the CPU, so the constraint is the **flash**, and it drove every change on the way. The polynomial was written
three times:

1. **Newton on the rotation for the angle**: 0.79 ms, accurate, but 652 bytes and it pulled 64-bit signed
   multiply and divide from the library (~280 more). Abandoned.
2. **Data-driven Horner, one loop for sin and cos**: coefficient tables in `PROGMEM`, the loop shared.
3. **A minimax atan polynomial** on `min/max` (7 terms, Q30 coefficients fitted for it), in that same loop:
   4 000 cycles, one division, no iteration, no 64-bit signed arithmetic. This shipped.

The production code and its test are `firmware/src/Trig.c` and `tests/host/test_trig.c`; this directory keeps
the CORDIC comparison. A third approach, octant stepping on the circle equation, was never prototyped:
its segments are not uniform and it yields no angle for the planner's arc length.
