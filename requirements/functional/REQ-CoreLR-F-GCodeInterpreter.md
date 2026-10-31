# Functional Requirements — GCode Interpreter

Sub-function: **GCodeInterpreter** — parse and execute a defined GCode
subset.

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved for V1 scope (2026-09-06); later phases planned but not
yet detailed.
See also: [`../Design/REQ-CoreLR-D-GCodeParser.md`](../design/REQ-CoreLR-D-GCodeParser.md),
[`../Design/REQ-CoreLR-D-SDReader.md`](../design/REQ-CoreLR-D-SDReader.md)
(where comment stripping is actually performed)

## REQ-CoreLR-F-GCodeInterpreter-0001 — V1 command scope
<a id="REQ-CoreLR-F-GCodeInterpreter-0001"></a>
The V1 GCode interpreter shall support:
- `G0` / `G1` — rapid / linear moves
- `G90` / `G91` — absolute / relative positioning mode (modal: stays in
  effect for subsequent lines until changed; `G90` absolute is the
  default at boot/job start)
- `M3` — pen down (mapped to `REQ-CoreLR-F-PenControl-0001`'s "down"
  setpoint); `M5` — pen up (mapped to the "up" setpoint). This is the
  standard convention used by most grbl-derived plotter firmwares
  (tool engaged = `M3`, tool retracted = `M5`), for compatibility with
  common GCode generators.

**Rationale (M3/M5 direction):** confirmed 2026-09-06, chosen for
compatibility with existing GCode generators (e.g. Inkscape gcodetools)
that assume this convention.

## REQ-CoreLR-F-GCodeInterpreter-0002 — Comment syntax
<a id="REQ-CoreLR-F-GCodeInterpreter-0002"></a>
The interpreter shall support `;` end-of-line comments and parenthesised
comments (same convention as grbl). *(Where in the pipeline this is
actually stripped is a design decision — see `REQ-CoreLR-D-SDReader`.)*

## REQ-CoreLR-F-GCodeInterpreter-0003 — Circular arcs: `G2` / `G3` (specified 2026-10-07)
<a id="REQ-CoreLR-F-GCodeInterpreter-0003"></a>
The interpreter shall support `G2` (clockwise) and `G3` (counter-clockwise) circular arcs in the XY plane,
with the centre given as offsets **`I`** (along X) and **`J`** (along Y) from the *start* of the arc — the form
grbl implements and the one common GCode generators emit.

| | |
|---|---|
| plane | `G17` (XY) is accepted and is the only plane; `G18`/`G19` are invalid lines |
| modal | like `G0`/`G1`: a following line with bare `X Y I J` continues in arc mode until another motion word |
| `X`, `Y` | the end point; absolute or relative as `G90`/`G91` says |
| `I`, `J` | **always relative to the start**, whatever the distance mode; a missing one is 0 |
| required | at least one of `X`/`Y`, and at least one of `I`/`J`; otherwise an invalid line |
| full circle | an end point **exactly equal** to the start is a full 360° turn |
| direction | as seen in the GCode coordinate plane: `G2` clockwise with X to the right and Y up |
| `R` (radius form) | **not supported in this increment**: a line with `R` is an invalid line (`-0007`: halt, error state). Deferred by the maintainer, 2026-10-07; to be added as its own increment |

**Why `R` is deferred, not refused for good.** The centre must be computed from the chord and the radius, which
leaves two candidate centres (the short or the long arc, chosen by the sign of `R`), cannot express a full
circle, and is badly conditioned near a half-circle, where a coordinate rounded by the generator moves the centre
a long way. `I`/`J` has none of these problems. A line the machine cannot draw correctly must stop the job rather
than be drawn as something else.

Realized by `REQ-CoreLR-D-GCodeParser-0009` and `REQ-CoreLR-D-Motion-0021`…`-0023`. The radius check is
`REQ-CoreLR-F-GCodeInterpreter-0009`; the accuracy and speed are `REQ-CoreLR-F-Motion-0007`.

## REQ-CoreLR-F-GCodeInterpreter-0004 — Planned: remaining commands (last)
<a id="REQ-CoreLR-F-GCodeInterpreter-0004"></a>
Support for `G4` (dwell), `G28` (return to reference/home, see
`REQ-CoreLR-F-Calibration-0004`), `G92` (set position), `G20` / `G21`
(units) shall be added **last**, once the rest of the system is
functional.

*(2026-10-08: `G21` is now accepted as a no-op by `-0012`; `G20` remains in this item.)*

## REQ-CoreLR-F-GCodeInterpreter-0005 — Degraded-input fallback
<a id="REQ-CoreLR-F-GCodeInterpreter-0005"></a>
If an upstream "maker" GCode source does not emit arcs/curves (only a
list of points), the interpreter shall handle that input gracefully as
plain point-to-point moves, without requiring arc support to function.

## REQ-CoreLR-F-GCodeInterpreter-0006 — Tolerated-but-ignored words
<a id="REQ-CoreLR-F-GCodeInterpreter-0006"></a>
The following words, commonly present in real-world GCode files but not
meaningful to this machine, shall be **accepted and silently ignored**
(parsed but not acted upon) — never treated as an invalid/unsupported
line: `F` (feed rate — motion speed is governed entirely by
`REQ-CoreLR-F-Motion-0003`'s own acceleration profile, not by GCode feed
rate), `S` (spindle speed / power, commonly appended to `M3` by
GCode generators even though this machine has no spindle), `N`
(line/block number).

**Rationale:** without this, `REQ-CoreLR-F-GCodeInterpreter-0007`'s
halt-on-invalid-line behaviour would trigger on nearly every real-world
line, since `F` in particular is close to universal in generated GCode.

## REQ-CoreLR-F-GCodeInterpreter-0007 — Invalid/unsupported line handling
<a id="REQ-CoreLR-F-GCodeInterpreter-0007"></a>
If a line cannot be parsed, or its `G`/`M` command is not one of the
supported (`REQ-CoreLR-F-GCodeInterpreter-0001`) or tolerated
(`REQ-CoreLR-F-GCodeInterpreter-0006`) words, the system shall **halt
the current job and enter the error state** — shown as the `E` tag on
the status screen (`REQ-CoreLR-D-LcdJoystickUI-0002`) — rather than
silently skip the line.

**Rationale:** confirmed 2026-09-06; a silently-skipped line could
produce an incorrect drawing without the user noticing until the job is
already done.

Recovery: the machine stays halted, showing `E`, until the user
explicitly acknowledges it (`REQ-CoreLR-D-GCodeParser-0006`); restarting
the job restarts from the beginning, not from the failing line.

## REQ-CoreLR-F-GCodeInterpreter-0008 — Programmed pause for a pen change (deferred, requested 2026-09-10)
<a id="REQ-CoreLR-F-GCodeInterpreter-0008"></a>

The interpreter shall support a **programmed pause** word, so that a
job can stop itself mid-drawing to let the user change the pen (colour,
line width), and continue on the user's signal.

Requested by the maintainer 2026-09-10. Deferred: not V1 scope, but
recorded now because most of the machinery it needs already exists.

### What it needs

- **A word.** `M0` (unconditional program stop) is the conventional
  choice and the likely one; `M6` (tool change) is arguably the more
  honest semantics for "change the pen" and is what some plotter
  toolchains emit. To be decided when this is taken up -- accepting
  both is also reasonable, since for this machine they would do the
  same thing.
- **A message on the LCD**, e.g. "change pen then resume", shown while
  paused so the user knows the machine is waiting on *them* and not
  merely halted. This is the part with no existing support: the status
  screen currently shows position and cable lengths on its two rows
  (`REQ-CoreLR-D-LcdJoystickUI-0002`), so a message needs a row, and
  therefore a decision about what it displaces while paused.
- **Resuming on the user's signal**, which is the existing "Resume job"
  menu item.

### What already exists (2026-09-10)

The job state machine and its ramped halt/resume
(`REQ-CoreLR-F-Actuation-0002`, `REQ-CoreLR-D-Motion-0012`) do all the
hard parts already, and were built with this in mind:

- a programmed pause maps onto exactly the same transition as a manual
  one -- ramp the motion down, then lift the pen;
- the pen is **restored on resume** if it was down, which is precisely
  the behaviour a pen change wants;
- the position survives the pause, so the drawing continues in
  register -- essential here, since a colour change happens mid-figure.

So the remaining work is the parser word, the message, and having the
parser raise the same halt the UI already raises. Nothing in the motion
or actuation layers should need to change.

## REQ-CoreLR-F-GCodeInterpreter-0009 — Arc radius consistency (specified 2026-10-07)
<a id="REQ-CoreLR-F-GCodeInterpreter-0009"></a>
An arc is described twice over: the start and the centre fix a radius, and the end point and the centre fix
another. In a perfect file they are equal; in a real file they differ by the generator's rounding. The rule is
grbl's (`gcode.c`, lines 804–807), taken as the reference:

> let `Δr` be the difference of the two radii.
> - `Δr ≤ 5 µm`: accepted.
> - otherwise the line is **invalid** if `Δr > 0.5 mm`, or if `Δr > 0.1 %` of the radius.

So the tolerance is relative, 0.1 % of the radius, capped at 0.5 mm for very large arcs and floored at 5 µm
for small ones — 10 µm at a 10 mm radius, 100 µm at 100 mm. (The "0.5 mm" quoted on its own is only the cap.)

Within the tolerance the arc is drawn exactly through both its end points, the radius changing linearly
along it (`REQ-CoreLR-D-Motion-0022`). Beyond it, the line is almost certainly broken — a flipped sign of `I`
or `J`, mixed units, a truncated line — and drawing it would produce a spiral or a wrong shape without any
warning, so the job stops in the error state instead.

Origin: maintainer, 2026-10-07 (defaults confirmed in conversation).

## REQ-CoreLR-F-GCodeInterpreter-0010 — Words of different modal groups may share a line (specified 2026-10-07)
<a id="REQ-CoreLR-F-GCodeInterpreter-0010"></a>
Words of different modal groups on one line — the distance mode (`G90`/`G91`), the motion mode (`G0`…`G3`), the
plane (`G17`), the pen (`M3`/`M5`) — shall **all take effect**: `G91 G1 X10` is a relative move *and* leaves the
machine in relative mode. Within one group the last word wins.

Origin: a defect found while adding arcs, not a decision: the parser held one `G` value per line, so the earlier of
two G words was silently dropped (`G91 G1 X10` made a move in the old distance mode). Realized by
`REQ-CoreLR-D-GCodeParser-0009`.

## REQ-CoreLR-F-GCodeInterpreter-0011 — A target beyond the machine's reach is an invalid line (specified 2026-10-08)
<a id="REQ-CoreLR-F-GCodeInterpreter-0011"></a>
A line whose end point has a drawing-frame coordinate of **2^21 µm (2.097 m) or more in magnitude** — X or Y, after
`G90`/`G91` has been applied to where the queue ends — shall be an invalid line (`-0007`: the job halts into the error
state, and nothing of that line takes effect: no motion queued, no pen action, no change of modal state). 2 097.151 mm is
accepted; 2 097.152 mm is not.

The limit applies to every move (`G0`/`G1`/`G2`/`G3`), and a relative move is judged where it *ends*, so a chain of small
accepted moves can never carry the machine past it. Numbers with so many digits that they once wrapped around the 32-bit
integer back into range (`X8589934.592` is 2^33 µm, which wrapped to 0) are now saturated and refused like any other
distant target.

**Why.** Nothing before this refused a target at all: a typo — `X100000` for `X100.000` — drove the motors for as long as it
took to unwind the cable, and a long digit string could silently become a *different, legal-looking* coordinate. The bound is
also what `CORELR_MATH_48`'s arithmetic stands on (`REQ-CoreLR-D-Motion-0024`: lengths under 2^22 µm; with the
drawing-origin offset, 2^21 µm of drawing coordinate keeps the internal ones inside it), and it equals the reach already
required of an arc (`-0009`, `REQ-CoreLR-D-Motion-0022`).

**Not decided here, and visible:** the limit is a single number for both axes and both signs, measured from the drawing origin
(the reference position after calibration). A machine whose frame is larger than ±2.097 m from that origin cannot draw out to
its edges; the value is `MOTION_REACH_SHIFT` (`SwConfig.h`), and it can only grow with the arithmetic's 2^22 bound.

Origin: maintainer, 2026-10-08 ("add that", translated), following the open item of `REQ-CoreLR-D-Motion-0024`. Realized by
`REQ-CoreLR-D-GCodeParser-0010`.

## REQ-CoreLR-F-GCodeInterpreter-0012 — Program-end and "nothing to do" words (decided 2026-10-08)
<a id="REQ-CoreLR-F-GCodeInterpreter-0012"></a>
The interpreter shall accept, in **every build variant** (SD and serial), since they occur in GCode *files* and not only on
a serial link:
- `G21` (millimetres — the only unit), `G54` (the only coordinate system), `G94` (feed per minute; feed is ignored anyway,
  `-0006`): accepted, **no effect**;
- `M2` and `M30` (program end): the pen is lifted once everything before it has been executed (as `M5`), the job ends, and
  the modal state returns to its job-start values (`G90`, no motion mode, pen up). In the SD variant the rest of the file
  is not executed; in the serial variant the next line starts a new job.

`G20` (inches) stays an invalid line (`-0007`): accepting it silently would draw 25.4 times too small.

**Why.** Most generators start a file with `G21` and end it with `M2` or `M30`; until now each halted the job at the first
line or at the last one. Origin: maintainer, 2026-10-08 (point P4 of `docs/protocol/SerialProtocol.md`). To be realized in
`REQ-CoreLR-D-GCodeParser` (request FW-10 of that document) — **realized 2026-10-08** by `REQ-CoreLR-D-GCodeParser-0011`
(program end, the grbl status number of every refusal) and `-0012`; one addition to the above, a line with a move *and* `M2`/`M30` is refused,
not reordered (the pen lift would come before the move).

## REQ-CoreLR-F-GCodeInterpreter-0013 — What only the serial link uses is not built into the SD variant (decided 2026-10-08)
<a id="REQ-CoreLR-F-GCodeInterpreter-0013"></a>
The command set differs per build variant (`REQ-CoreLR-F-SerialControl-0003`): GCode that can appear in a file is common
to both; the commands that exist only because of the serial link — real-time bytes (`?`, `!`, `~`, Ctrl-X), `$` commands,
and later `$J=` jogging — are part of the serial variant only, and **compiled out** of the SD variant, which shall not grow
by any byte for them. The table of record is §6.4 of `docs/protocol/SerialProtocol.md`.

**Why.** The SD variant is the one short of flash (720 bytes free); it must not pay for a link it does not have. Origin:
maintainer, 2026-10-08 (points P4 and P6 of `docs/protocol/SerialProtocol.md`; "note it well for the implementation", translated).

## REQ-CoreLR-F-GCodeInterpreter-0014 — GCode `+Y` points up the wall (decided 2026-10-08)
<a id="REQ-CoreLR-F-GCodeInterpreter-0014"></a>
In every build variant, a GCode `+Y` shall move the pen **up** the wall, and `G2` shall turn clockwise as seen on the wall
with X to the right and Y up — the convention of common GCode generators, so a file is not drawn mirrored top to bottom.
Everything expressed in the drawing frame follows the same orientation: the position shown on the LCD and reported over the
link, and the joystick's Control jog (stick up, pen up).

Selectable at build time: `CORELR_Y_UP` (`SwConfig.h`), default 1; 0 restores the previous orientation (`REQ-CoreLR-D-Motion-0025`).

Origin: maintainer, 2026-10-08 (decision D1 of `docs/protocol/SerialProtocol.md`), closing the open item "Orientation of the Y
axis" of `REQ-CoreLR-D-Motion.md`. To be realized by the firmware side (request FW-01 of that document).

## REQ-CoreLR-F-GCodeInterpreter-0015 — A `Z` word lifts or lowers the pen (decided 2026-10-10; docs/protocol/SerialProtocol.md P7)
<a id="REQ-CoreLR-F-GCodeInterpreter-0015"></a>
Some plotter generators lift the pen with `Z` moves instead of `M3`/`M5`; such files used to stop at their first pen lift (`Z` was an unsupported word). The interpreter shall accept `Z` as a **pen word**:
in absolute distance mode (`G90`) `Z <= 0` is the pen down on the paper and `Z > 0` lifted; in relative mode (`G91`) the sign of the move decides and `Z0` changes nothing. It behaves like `M3`/`M5` in every other respect
(it waits for the planned moves to finish, it is ordered before the line's own move, it is modal state `$G` reports), and a line carrying both a `Z` and an `M` word is refused (neither is silently dropped).
It is **optional at build time**: on in the serial variant, off in the SD variant, whose flash it would overflow (`CORELR_GCODE_Z`). Design: `REQ-CoreLR-D-GCodeParser-0014`.
The threshold is a convention of this project (the maintainer's go-ahead, 2026-10-10: "yes, implement"); `Z0` = pen on the paper is the common one among pen-plotter generators, but it is the one thing to check against a real generator's output.

## REQ-CoreLR-F-GCodeInterpreter-0016 — A target outside the travel zone is refused (specified 2026-10-11)
<a id="REQ-CoreLR-F-GCodeInterpreter-0016"></a>
A job line or a `$J=` jog whose end point lies outside the travel zone (`REQ-CoreLR-F-Motion-0008`) is refused like a target beyond the machine's reach (`-0011`): the grbl status 33, nothing moved, nothing of the line applied; a job halts into its error state. An arc is refused when any point of it is outside
the zone, not only its end points. A line from a position outside the zone to a point inside it is accepted.


## REQ-CoreLR-F-GCodeInterpreter-0017 — `G92` and `G10 L20 P1` set the origin (specified 2026-10-11)
<a id="REQ-CoreLR-F-GCodeInterpreter-0017"></a>
`G92 X<x> Y<y>` and `G10 L20 P1 X<x> Y<y>` (P1 is G54, the only coordinate system) make the **current position** the point (x, y): the drawing origin of `REQ-CoreLR-F-Calibration-0010` moves so that the pen, where it is, reads (x, y). One axis alone changes that axis only. The values are **absolute whatever G90/G91 says**, as in grbl.
- The line waits (`GCODEPARSER_BUSY`) until everything queued has been executed: coordinates already planned were meant in the old frame.
- It is refused with the status **33** when the new origin would lie outside the travel zone, **26** when it has no axis word, **21** when it shares its line with a motion word, an arc offset, a pen word or a Z word (grbl's "two commands using axis words"), **20** for any other `L` or `P` of `G10`, for `G92.1` and the like, and for `L`/`P` words with no `G10`. The same words are refused by `$J=`.
- It is accepted in a job file as well as over the link; the origin it sets is stored (`-Calibration-0010`).
A sender that restores its work coordinates with `G92` after an abort (Candle does, `docs/protocol/SerialProtocol.md`) is now answered `ok`.


## REQ-CoreLR-F-GCodeInterpreter-0018 — `G53`: a move in the machine frame (specified 2026-10-11, serial variant only; confirmed by the maintainer 2026-10-12)
<a id="REQ-CoreLR-F-GCodeInterpreter-0018"></a>
*Applies to: serial image only*

`G53` makes the **one line** it is on read its X and Y (and Z) in the **machine frame** instead of the drawing frame, absolute whatever G90/G91 says, as in grbl. It needs `G0` or `G1` (on the line or modal), else error **20**.
- The machine frame (`REQ-CoreLR-F-SerialControl-0013`) has its origin at the calibration reference point, +Y up the wall, so `G53 G0 X0 Y0` goes to the reference point wherever the drawing origin is.
- A `Z` word under `G53` is the machine's Z: **Z >= 0 lifts the pen, Z < 0 lowers it** (grbl's machine Z is 0 at the top and negative below). `G53 G0 Z0` alone is "Safe position": the pen is lifted, nothing moves. (Without `G53`, `Z` keeps the rule of `-0015`: Z <= 0 is the pen on the paper.)
- The target must be inside the travel zone (error 33), like any move.
- **The two meanings of `Z0`** (`-0015` against this item) are deliberate: grbl's machine Z is 0 at the top. `G0 Z0` lowers the pen, `G53 G0 Z0` lifts it.
- It exists only in the **serial variant** (a sender asks for it; no job file does): in the SD variant `G53` stays an unsupported word.
Candle's "Safe position" is `G53 G0 Z0` and its "Restore origin" is `G53 G90 G0 X0 Y0 Z0` then `G92 X0 Y0 Z0` (captured 2026-10-11 with `tools/bench/candle_spy.py`).

## REQ-CoreLR-F-GCodeInterpreter-0019 — `M9` and a `T` word are accepted and mean nothing (decided 2026-10-12, serial variant only)
<a id="REQ-CoreLR-F-GCodeInterpreter-0019"></a>
*Applies to: serial image only*

`M9` (coolant off) and `T<n>` (tool select) are accepted and ignored, as in grbl: there is no coolant and one pen. They may share a line with other words (`M9 M3`, `G54 G17 G21 G90 G94 M9 T0`); `M9.5` and the other coolant words (`M7`, `M8`) stay `error:20`. Why: after an Abort Candle sends `G54 G17 G21 G90 G94 M9 T0` (`restoreParserState()`, bench 2026-10-12); refused, it halted the implicit job that the preceding `G53` line had started and left the machine in the error state (`docs/bringup/Bringup-Status.md`). `$G` already reads `M9 T0`. In the SD variant they stay unsupported.

## Open items
- Full dialect/edge-case behaviour beyond what's captured above (e.g.
  exact error-state recovery/acknowledgement flow) is not yet detailed.
