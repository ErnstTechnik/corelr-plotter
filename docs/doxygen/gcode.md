# GCode {#gcode}
<!-- Copyright (C) 2026 ErnstTechnik and the CoreLR contributors -- GPL-3.0-or-later. Read by Doxygen only (docs/doxygen/Doxyfile.in). The tables are CHECKED: tests/tools/test_gcode_page.py runs every line of
     them through the firmware's real parser, compiled for each image, and compares the answer; it also fails if the parser knows a G or M word or a letter that no row shows. -->

What this image's GCode parser (`GCodeParser.c`) accepts and refuses. The pipeline in front of it removes the comments (`;` to the end of the line, and `( ... )`) and cuts the line:
the SD reader does it for a file, the serial link while it assembles the line. The parser then reads **words** (a letter and a number): upper or lower case, blanks optional
(`g1 x10` and `G1X10Y10` are the same line), words of different groups on one line all take effect (`G91 G1 X5` is a relative move and leaves the machine in relative mode, @req{REQ-CoreLR-F-GCodeInterpreter-0010}).
Numbers are decimal and converted to integer micrometres; there is no floating point and no exponent (`1e3` is refused). Units are millimetres (`G21`). The drawing frame has **+Y up the wall** and an origin the
user sets; a target outside the travel zone (20 mm inside the vertical of each motor, 50 mm below the line joining the motors) is refused.

\if serial
**This is the `serial` image**: a sender (Candle 1.1.9) streams the lines. It accepts a few words that the `sd` image refuses (`G53`, `Z`, `M9`, `T`: see *Only in this image*).
\endif
\if sd
**This is the `sd` image**: the lines come from a file on the SD card. It refuses a few words that the `serial` image accepts (`G53`, `Z`, `M9`, `T`: see *Not in this image*), and a program end ends the job.
\endif

**How to read the tables.** Each row is one line of GCode and **the answer of this image's own parser** to it, the head being at the drawing origin, in absolute mode, pen up
(`ok`: accepted and executed; `end`: accepted, and it was a program end; `error:N`: refused with grbl's status number N, which halts a job, see *Status numbers*). The test
`tests/tools/test_gcode_page.py` makes every row true, and fails if the parser knows a G word, an M word or a letter that no row shows.

## Motion
| Line | Answer | What it does |
|---|---|---|
| `G0 X10 Y10` | `ok` | move to (10, 10) mm. `G0` and `G1` run at the same speed: the speed is the machine's own profile, `F` is ignored (@req{REQ-CoreLR-F-GCodeInterpreter-0006}) |
| `G1 X10 Y10` | `ok` | straight line to (10, 10) mm |
| `G1 X10` | `ok` | an axis left out keeps its value |
| `G2 X10 Y0 I5 J0` | `ok` | arc, clockwise (Y up), end point and centre offset from the start (@req{REQ-CoreLR-F-GCodeInterpreter-0003}) |
| `G3 X10 Y0 I5 J0` | `ok` | arc, counter-clockwise |
| `G2 X0 Y0 I5` | `ok` | a full circle: the end is the start; `I` or `J` may be left out, not both |
| `G90 G1 X10 Y10` | `ok` | absolute coordinates (the default at the start of a job) |
| `G91 G1 X5 Y5` | `ok` | relative coordinates: moves by (5, 5) mm |
| `G17` | `ok` | the XY plane, the only one: accepted, no effect |

## Pen and program
| Line | Answer | What it does |
|---|---|---|
| `M3` | `ok` | pen down, once the moves planned before it are done (@req{REQ-CoreLR-F-GCodeInterpreter-0001}) |
| `M5` | `ok` | pen up |
| `M2` | `end` | program end: pen up, the job ends, the modal state returns to its start values (@req{REQ-CoreLR-F-GCodeInterpreter-0012}) |
| `M30` | `end` | the same as `M2` |
| `G1 X10 M30` | `error:20` | a move and a program end on one line: refused, not reordered |
| `M3 G1 X10` | `ok` | the pen word and a move on one line: both take effect |

## Accepted, nothing to do
These are in almost every generated file; refusing them would stop nearly every job.

| Line | Answer | What it does |
|---|---|---|
| `G21` | `ok` | millimetres, the only unit |
| `G54` | `ok` | the only coordinate system |
| `G94` | `ok` | feed per minute (the feed is ignored anyway) |
| `G17 G21 G54 G94` | `ok` | the usual start of a file |
| `G1 X10 F600` | `ok` | `F`, the feed rate, is read and ignored |
| `G1 X10 S1000` | `ok` | `S`, the spindle speed, is read and ignored |
| `N10 G1 X10` | `ok` | `N`, the line number, is read and ignored |

## Origin
| Line | Answer | What it does |
|---|---|---|
| `G92 X0 Y0` | `ok` | the current position becomes (0, 0): the drawing origin moves, and is stored (@req{REQ-CoreLR-F-GCodeInterpreter-0017}) |
| `G92 X10` | `ok` | one axis alone changes that axis only |
| `G10 L20 P1 X0 Y0` | `ok` | the same, in the form of grbl's `G10`; `P1` is `G54`, the only coordinate system |
| `G92` | `error:26` | no axis word |
| `G92 X0 G1` | `error:21` | shares its line with a motion word |
| `G92.1` | `error:20` | a G word with a fraction (`G92.1`, `G38.2`...) is not supported |
| `G10 L2 P1 X0 Y0` | `error:20` | only `L20 P1` |

## Refused
Anything not listed above is an unsupported word: the line is refused with `error:20`, and a job halts in the error state (@req{REQ-CoreLR-F-GCodeInterpreter-0007}).

| Line | Answer | Why |
|---|---|---|
| `G20` | `error:20` | inches: accepting it silently would draw 25.4 times too small |
| `G18` | `error:20` | another plane (`G19` as well) |
| `G2 X10 Y0 R5` | `error:20` | the radius form of an arc is not supported: use `I` and `J` |
| `G4 P1` | `error:20` | no dwell |
| `G28` | `error:20` | no homing from a program |
| `M0` | `error:20` | no programmed pause (`M6`, a tool change, as well) |
| `M7` | `error:20` | no coolant (`M8` as well) |
| `G1 X10 Y10 A5` | `error:20` | an axis the machine does not have (any other letter as well) |
| `G1 X1e3` | `error:20` | no exponent |

## Wrong lines
| Line | Answer | What it means |
|---|---|---|
| `X10` | `error:26` | coordinates with no motion mode yet |
| `G2 X10 Y0` | `error:35` | an arc with neither `I` nor `J` |
| `G2 X10 Y0 J5` | `error:34` | the two radii of the arc disagree by more than the tolerance |
| `G1 X10 I5` | `error:36` | an arc word on a straight move |
| `G1 X` | `error:2` | a letter with no number |
| `G1 X5000` | `error:33` | the target is outside the travel zone (as is `X-5000`) |

\if serial
## Only in this image
| Line | Answer | What it does |
|---|---|---|
| `G53 G0 X0 Y0` | `ok` | one line in the **machine frame**, whose origin is the calibration reference point: goes there wherever the drawing origin is (@req{REQ-CoreLR-F-GCodeInterpreter-0018}) |
| `G53 G1 X0 Y0` | `ok` | the same with `G1` |
| `G53 G0 Z0` | `ok` | "safe position": under `G53` a `Z` of 0 or more lifts the pen; nothing moves |
| `G53 X0 Y0` | `error:20` | `G53` needs `G0` or `G1` |
| `G0 Z5` | `ok` | `Z` is a pen word: `Z` above 0 lifts the pen (@req{REQ-CoreLR-F-GCodeInterpreter-0015}) |
| `G1 Z-1` | `ok` | `Z` of 0 or less puts the pen down; `Z0` alone is also "on the paper" |
| `G91 G1 Z1` | `ok` | in relative mode the sign of the move decides |
| `M3 Z1` | `error:36` | a `Z` word and an `M` word on one line: neither is silently dropped |
| `M9` | `ok` | coolant off: accepted, no effect (there is none) (@req{REQ-CoreLR-F-GCodeInterpreter-0019}) |
| `T1` | `ok` | a tool number: accepted, no effect (there is one pen) |

**Program end.** `M2` and `M30` end the job but the link stays open: the next line starts a new one.

**Not parsed here.** What the serial link answers itself, before the parser, is the contract with the sender (`docs/protocol/SerialProtocol.md`): `?` (status report), `!` and `~` (pause and
resume), Ctrl-X (cancel), the byte 0x85 (cancel a jog), and the `$` commands `$H`, `$J=`, `$G`, `$I`, `$X`, `$#`, `$$`.
\endif
\if sd
## Not in this image
These are accepted by the `serial` image and refused here (`error:20`: an unsupported word), because the SD image has no flash for them.

| Line | Answer | What it would do in the `serial` image |
|---|---|---|
| `G53 G0 X0 Y0` | `error:20` | a move in the machine frame |
| `G0 Z5` | `error:20` | `Z`, a pen word (`Z` is refused whatever the line) |
| `M9` | `error:20` | coolant off |
| `T1` | `error:20` | a tool number |
| `$H` | `error:1` | not GCode: a line that starts with `$` is not a word (the `$` commands exist on the serial link only) |

**Program end.** `M2` and `M30` end the job: the rest of the file is not executed.
\endif

## Comments and the length of a line
`; a comment` and `(a comment)` are removed before the parser sees the line. A line may hold
\if sd
**47 characters** before the comments are removed (`SDREADER_LINE_SIZE` is 48, with the end of the string). **A longer line is not refused: the SD reader keeps its first 47 characters and drops the rest**, and the parser sees the cut line (a coordinate may be shortened without a message): keep the lines short.
\endif
\if serial
**80 significant characters** (blanks and comments do not count). A longer line is read to its end and answered `error:11`, never truncated, and never reaches the parser.
\endif
`tools/bench/gcode_check.sh` runs a file through the real parser and reports a line that is too long or refused.

## Status numbers
The numbers are grbl's, so a sender can show their text. Those that the tables use:

| Number | Macro | Meaning |
|---|---|---|
| `error:1` | @ref GCODEPARSER_ERR_EXPECTED_LETTER | a character that is not a word letter |
| `error:2` | @ref GCODEPARSER_ERR_BAD_NUMBER | a letter with no number, or a malformed one |
| `error:20` | @ref GCODEPARSER_ERR_UNSUPPORTED | a word that is not supported |
| `error:21` | @ref GCODEPARSER_ERR_AXIS_CONFLICT | an origin command on a line with a motion word, an arc offset or a pen word |
| `error:26` | @ref GCODEPARSER_ERR_NO_AXIS_WORDS | coordinates with no motion mode, or an arc with no end point |
| `error:33` | @ref GCODEPARSER_ERR_TARGET_INVALID | a target outside the travel zone |
| `error:34` | @ref GCODEPARSER_ERR_ARC_RADIUS | the radii of an arc disagree |
| `error:35` | @ref GCODEPARSER_ERR_ARC_NO_OFFSET | an arc with neither `I` nor `J` |
| `error:36` | @ref GCODEPARSER_ERR_UNUSED_WORDS | words that this line cannot use (`I`/`J` on a straight move, `Z` with `M`) |
