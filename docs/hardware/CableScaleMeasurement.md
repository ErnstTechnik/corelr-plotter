# Measuring the cable scale (steps -> real cable length)

*Written 2026-10-16; the gear ratio was corrected in the firmware on 2026-10-26 (see the last section), the measurement below is still the check of what remains.*

*Original text, 2026-10-16. Why: on a calibrated machine the length of cable read with a tape measure does not match the length the firmware believes it has paid out.*

## The observation that prompted it (maintainer, 2026-10-16)
- Near the **left / right limits** the pen seems to go farther than commanded.
- Before the **top limit** the cables are over-tight.
- The cable is a sewing thread (very thin). The maintainer's hypothesis: it winds more than the firmware thinks.

Both are the direction a scale `k > 1` produces (more cable per step than the model): going up the real cables shorten more than planned, going sideways the near cable shortens and the far one lengthens more. And the top limit **amplifies a tiny error enormously**, which is why it shows first. At the top limit (50 mm below the motor line, `D` = 1065, `Lref` = 800, `H` = 597 mm) the cables are nearly horizontal (5.4 degrees); the pen is commanded 265.2 mm of cable shorter than at the origin, and the real gap to the motor line, computed from the geometry, is:

| k | real gap below the motor line | angle of the cables | tension, relative |
|---|---|---|---|
| 1.000 | 50.0 mm | 5.4 deg | 1.0 |
| 1.005 (the gear ratio alone) | 33 mm | 3.6 deg | 1.5 |
| 1.0075 | 19 mm | 2.1 deg | 2.6 |
| 1.012 | 0 mm: the pen is on the motor line | 0 deg | unbounded |

So a 0.5 % scale error, already explained by the gear ratio, takes 17 mm off the top margin and raises the tension by half; the thread's thickness (a sewing thread of about 0.25 mm on 35 mm: +0.7 % for a single layer) adds to it. The cable does not need to be "winding more than expected" by much. (These are computed, not measured; the lateral effect is milder, a few mm, and the same sign.)

## What is suspected
The firmware winds a constant `pi * 35 mm / 2048 = 53.7 um` of cable per step (`HwConfig.h`, `Motion.c`). Checked in the code:
- **pi is not the cause**: 355/113 is off by 8.5e-8 (0.09 um per metre); the circumference is truncated to 109 955 um instead of 109 955.75 (7e-6); the um -> step conversion is Q24 fixed point.
- **The 28BYJ-48's gear ratio is 63.68395:1, not 64:1**: 2037.886 full steps a turn, not 2048. The real output turns **0.496 % more** than the firmware thinks.
- **The cable's own thickness**: the cable winds on the spool's diameter *plus its own thickness* (35 mm + d, +1.4 % for a 0.5 mm cord), and each further layer adds 2 d.

All three are a **scale on the length of cable paid out since the calibration reference point** (where the firmware and the machine agree by construction). They do not change what is stored in the EEPROM.

## The measurement does not touch the calibration
Everything below moves the pen with ordinary moves, which the firmware tracks and stores like any plot (the position is written after 2 s without motion). **Do not** use, in this procedure:
- *Calibrate machine* / the Adjustment jog (that is the calibration workflow), *Set origin*, *Origin = calib.*;
- in Candle: *Zero XY*, *Restore origin*, or any `G92` / `G10`;
- power-off, unplugging or the Nano's reset button **while the pen is moving** (a power cut during a move costs a recalibration, `REQ-CoreLR-F-Calibration-0007`).

Opening the serial port resets the Nano. That is harmless **if the pen was still for at least 2 s before**: the position is restored from the EEPROM ring (verified on the machine).
Do not move the motors by hand while the machine is off.

## What you need
The serial variant flashed, Candle (its console), a tape measure (mm), a pencil mark or tape flag on each cable if you want to read the length at the exit hole without moving the tape. The calibration's `D` and `Lref` (menu *Calibrate machine* shows them; yours: 1065 and 800 mm).

## Procedure
1. Pen at the reference point (drawing origin), machine **still for 2 s**. Connect Candle. In Candle's console: `G90`. The position shown must be `X0 Y0` (if not, the origin was moved: stop, that is another question).
2. **Point 0** (0, 0): with the cables taut, read with the tape the length of **each cable from its exit hole to the pen** (the point where the two cables meet). Write down `L0`, `R0`. It should be about `Lref` (800), within the pen offset (a few mm): only *differences* are used, so a constant offset in the way you read does not matter. Keep the same way of reading for every point.
3. **Point 1**: console `G1 X0 Y-300` (the feed word is ignored; the pen moves at the machine's speed, about 14 s). Wait for `Idle`, then 2 s. Read `L1`, `R1`. The firmware predicts about 1043.2 mm for both.
4. **Point 2**: `G1 X0 Y-600`, wait, read `L2`, `R2` (predicted about 1310.1 mm). Choose the points to suit your wall: the farther, the better the resolution (1 mm of tape on 500 mm is 0.2 %); the pen must stay inside the travel zone, and the cables must keep their tension.
5. **Closure**: `G1 X0 Y0`, wait, read again. It must give `L0`, `R0` back to within about 1 mm; if not, the cable slips, or has slack/hysteresis, and the other readings are not trustworthy: say so.
6. Wait 2 s still. Close Candle (or leave it).

Always approach the points in the same direction (downwards, cables lengthening), and keep the same tension.

Optional points in the direction of the observation (the tool takes any `X,Y`; both cables are read; each one is far enough from the limits):
- **Up**: `G1 X0 Y400` (predicted 567.8 mm for both): the cables **shorten**, which is what you see before the top limit. Approach it from below.
- **Sideways**: `G1 X-400 Y0` (predicted L = 611.6, R = 1107.2 mm): the near cable shortens, the far one lengthens.
Do not go near a limit with this: the tension rises fast (table above).

## Reading the result
```
tools/analysis/cable_scale.py --D 1065 --Lref 800 \
    --point 0,0,L0,R0 --point 0,-300,L1,R1 --point 0,-600,L2,R2
```
(The first point is the reference; `-h` explains the output.) For each motor it gives `k = measured change / predicted change`:

| What you see | Meaning |
|---|---|
| `k` about 1.005 for both, residuals under 1-2 mm | the gear ratio explains everything; the cable's thickness is negligible |
| `k` clearly above 1.005, both motors, residuals small | the cable's thickness adds a constant amount to the spool diameter (the tool gives it) |
| residuals that grow with the length | the scale is not constant: the cable builds up in layers (a single layer, or a groove, would avoid it) |
| `k(L)` and `k(R)` differ by more than 0.2 % | the two spools do not wind alike (different layers, a different cable path); the tape is accurate to about 0.1 % over 1.3 m |

## First readings (2026-10-16): inconclusive
Only L, read to 5 mm: 800 at the origin, 1045 at Y = -300 (predicted 1043.2), 1305 at Y = -600 (predicted 1310.1). The residuals (+1.8 and -5.1 mm) have opposite signs, a scale error would not do that, and 5 mm of rounding is as large as the effect. Nothing can be concluded, and nothing is excluded: the gear ratio alone would give +1.2 and +2.5 mm. To redo with both cables read to the millimetre, twice, with the return to the origin and, if the wall allows it, a longer move.

## Done on 2026-10-26: the gear ratio
`MOTION_STEPS_PER_REV` (2048) became `MOTION_STEPS_PER_REV_X1000` = 2037886 in `HwConfig.h` (63.68395:1 x 32 steps), `Motion.c` follows; the step is now 53.96 µm. Host tests green, the geometry picture regenerated. **Readings taken before that date assumed 2048: give `cable_scale.py --steps 2048` for them.** What the tape then shows is only what remains (the thread's thickness, the geometry). Tried on the machine: not yet.
**The stored step counts are stale after flashing this change** (they were computed with 2048): bring the pen to the reference point and confirm it again with *Calibrate machine*.

## The correction for what remains, once `k` is known (not done yet)
A constant in `HwConfig.h`: `MOTION_SPOOL_DIAMETER_UM` = 35 000 x k (the tool prints it per motor; one value for both unless they differ). It then stays true for any plot, since the error is proportional to the cable paid out.
**It makes the stored step counts stale** (they were computed with the old scale): after flashing, bring the pen to the reference point and confirm it again with *Calibrate machine* (once). I have not checked whether `D`, `Lref`, the drawing-origin offset and the pen angles survive that workflow unchanged; `REQ-CoreLR-F-Calibration-0007` says the installation parameters are kept. Decide when the measurements are in.
