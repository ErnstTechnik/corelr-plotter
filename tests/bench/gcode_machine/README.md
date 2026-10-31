# GCode for the maintainer's machine: the travel-zone tests

Geometry (the maintainer's calibration, 2026-10-11): motors **D = 1065 mm** apart, reference cable **L = 800 mm**, so the drawing origin -- where "Calibrate machine" leaves it -- is
**532.5 mm from the left motor and 597.029 mm below the line joining the motors**. The travel zone (20 mm from each motor's vertical, 50 mm below the line) is therefore
**X -512.5 .. +512.5 mm, Y up to +547.029 mm** (Y up the wall), and the frame below fills it 5 mm inside, 600 mm tall (Y -57.971 .. +542.029: the bottom corners need cables of ~1.23 m).

Written by (do not edit by hand; `tests/host/run_all.sh` checks it):
```
tools/generators/gen_gcode.py --limits 1065,800 --only-limits -o tests/bench/gcode_machine        # --height MM for a shorter drawing, --inset MM, --side, --top
tools/bench/gcode_check.sh --limit 800 --geometry 1065,800 tests/bench/gcode_machine/*.gc          # what the firmware's real parser makes of them
```
| file | what it does | expected |
|---|---|---|
| `10_Limits_Frame.gc` | frame, both diagonals, the inscribed circle, three big arcs whose centres are outside the zone, corner marks -- pen DOWN | runs whole, ~9.9 m of ink |
| `11_Refuse_Left.gc` | pen up: a move at X -511.5 (1 mm inside the left limit), then X -513.5 (1 mm beyond) | the second is refused: **error 33**, `E`, nothing moves on that line |
| `12_Refuse_Right.gc` | the same at the right limit, X +511.5 then +513.5 | refused, error 33 |
| `13_Refuse_Top.gc` | the same at the top margin, Y 546.029 then 548.029 | refused, error 33 |
| `14_Refuse_ArcBulge.gc` | pen up: a half circle under its chord (accepted), then the half circle over it, whose top is 10 mm beyond the top margin | refused, **error 34** (the arc's own number) |

Run `10_` first (the machine must be calibrated, the origin where it was calibrated), then the four refusals; after each refusal acknowledge with SW (or Ctrl-X from Candle) and bring the pen back with the jog or "Return home".
