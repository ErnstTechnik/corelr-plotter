# GCode for the serial variant: what the link accepts, refuses, and says

Written by `tools/generators/gen_serial_tests.py` (do not edit by hand; `tests/host/check_serial_files.sh` checks them against the firmware's real parser):
```
tools/generators/gen_serial_tests.py --geometry 1065,800 -o tests/bench/gcode_serial
tools/bench/gcode_check.sh --limit 1000 --geometry 1065,800 tests/bench/gcode_serial/*.gc
```
Machine geometry used: motors **D = 1065 mm** apart, drawing origin 532.500 mm from the left motor and 597.029 mm below the line joining the motors; travel zone
X -512.500 .. +512.500 mm, Y up to +547.029 mm (margins 20 mm sides, 50 mm top). The contract is `docs/protocol/SerialProtocol.md`; the travel-zone files proper
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
| `30_Refuse_01_Letter.gc` | 1 | a character that is not a word letter |
| `31_Refuse_02_NoNumber.gc` | 2 | a letter with no number |
| `32_Refuse_20_G20.gc` | 20 | inches: refused, never taken for millimetres |
| `33_Refuse_20_G4.gc` | 20 | dwell, not supported |
| `34_Refuse_20_M0.gc` | 20 | programmed pause, not supported |
| `35_Refuse_20_Word.gc` | 20 | an unknown word letter (Q) |
| `36_Refuse_20_ArcR.gc` | 20 | the radius form of an arc (R), deferred |
| `37_Refuse_20_Plane.gc` | 20 | another plane (G18) |
| `38_Refuse_26_NoMode.gc` | 26 | coordinates with no motion word (after M2 the modal state is reset) |
| `39_Refuse_26_ArcNoEnd.gc` | 26 | an arc with no end point |
| `40_Refuse_35_ArcNoOffset.gc` | 35 | an arc with neither I nor J |
| `41_Refuse_36_Unused.gc` | 36 | I on a G1 line |
| `42_Refuse_36_PenAndZ.gc` | 36 | an M word and a Z word on one line (serial variant) |
| `43_Refuse_21_AxisConflict.gc` | 21 | G92 on a line with a motion word |
| `44_Refuse_34_ArcRadius.gc` | 34 | an arc whose two radii differ by far more than 0.1 % (start radius 20, end 10) |
| `45_Refuse_33_FarTarget.gc` | 33 | a target 2097.152 mm away: the zone refuses it long before 2^21 um (see README) |
| `46_Refuse_33_Typo.gc` | 33 | X100000 for X100.000: the typo the reach check was written for |
| `47_Refuse_33_Wrap.gc` | 33 | 8589934.592 mm = 2^33 um, which once wrapped to 0 |
| `48_Refuse_33_RelativeChain.gc` | 33 | relative moves: judged where they END; the 9th of 10 small steps crosses the left limit |
| `50_Refuse_20_G53NoMode.gc` | 20 | G53 with no G0/G1 on the line or modal (after M2) |
| `51_Refuse_33_G53Zone.gc` | 33 | G53 to a machine point 1 mm beyond the left limit (the origin being the reference point) |
| `49_Refuse_11_Line81.gc` | 11 | 81 significant characters: never truncated (serial link, not the parser) |

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
