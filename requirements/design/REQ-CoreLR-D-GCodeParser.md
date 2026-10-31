# Design Requirements — GCode Parser

Module: **GCodeParser** — internal parsing of a GCode line into an
executable command, and the modal state carried between lines.

Realizes: [`REQ-CoreLR-F-GCodeInterpreter-0001`, `-0002`, `-0006`, `-0007`](../functional/REQ-CoreLR-F-GCodeInterpreter.md)
(and later `-0003`, `-0004` as arcs/remaining commands are implemented)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06), informed by `grbl/grbl/gcode.c` (reference
— a reference repository is read-only: studied for its
tokenization/modal-group approach, not ported as-is — grbl's parser
handles a far larger dialect than our V1 scope needs)

## REQ-CoreLR-D-GCodeParser-0001 — Word tokenization
<a id="REQ-CoreLR-D-GCodeParser-0001"></a>
Each line (already comment-stripped and `\0`-terminated by
`REQ-CoreLR-D-SDReader-0002`/`-0003`) is scanned word by word: a *word*
is one letter (`G`, `M`, `X`, `Y`, plus the tolerated `F`, `S`, `N` from
`REQ-CoreLR-F-GCodeInterpreter-0006`) immediately followed by a numeric
value. The line is uppercased first (letters are case-insensitive),
matching the convention already used in the `Walldraw.ino` reference.

## REQ-CoreLR-D-GCodeParser-0002 — Integer-only decimal-to-µm conversion
<a id="REQ-CoreLR-D-GCodeParser-0002"></a>
Numeric values (e.g. `123.456`, meaning millimetres) are converted
directly to signed micrometre integers **without ever forming a
floating-point value**, consistent with
`REQ-CoreLR-D-Motion-0001`:
1. Parse the integer (pre-decimal-point) part as a plain integer,
   multiply by 1000 (mm → µm).
2. If a decimal point follows, parse up to 3 fractional digits,
   left-padding/truncating so they represent thousandths of a
   millimetre directly (i.e. µm); ignore any further digits beyond the
   3rd (sub-µm precision is meaningless here).
3. Add the two parts (respecting the sign).

```
"123.4"   -> 123000 + 400           = 123400  (µm)
"-7.5"    -> -(7000 + 500)          = -7500   (µm)
"10.12345"-> 10000 + 123 (truncated)= 10123   (µm)
```

**Rationale:** confirmed 2026-09-06 — avoids floating-point arithmetic
entirely (costly on 8-bit AVR) and keeps the parser consistent with the
rest of the motion pipeline's no-float rule.

## REQ-CoreLR-D-GCodeParser-0003 — Tolerated-word handling
<a id="REQ-CoreLR-D-GCodeParser-0003"></a>
`F`, `S` and `N` words (`REQ-CoreLR-F-GCodeInterpreter-0006`) are parsed
(so they don't break tokenization) but their value is discarded — no
state is kept for them.

## REQ-CoreLR-D-GCodeParser-0004 — Modal state
<a id="REQ-CoreLR-D-GCodeParser-0004"></a>
The parser keeps a small persistent state, carried across lines within a
job:
- Current distance mode (absolute/incremental — `G90`/`G91`, default
  absolute at job start).
- Current pen state (down/up — `M3`/`M5`, default up at job start).

A line that omits `G90`/`G91` or `M3`/`M5` keeps the previously-set mode
— standard GCode modal behaviour.

```c
typedef struct
{
   uint8_t DistanceModeIncremental; /* 0 = absolute (G90), 1 = incremental (G91) */
   uint8_t PenDown;                 /* 0 = up (M5), 1 = down (M3) */
} GCodeParser_State_t;
```

## REQ-CoreLR-D-GCodeParser-0005 — Invalid-line handling mechanism
<a id="REQ-CoreLR-D-GCodeParser-0005"></a>
`REQ-CoreLR-F-GCodeInterpreter-0007` (halt job, enter error state) is
realized by setting a shared machine-state flag to "error" and invoking
the same stop mechanism as `REQ-CoreLR-F-Actuation-0002` (halt enqueuing
new jobs) — reusing `REQ-CoreLR-D-Stepper-0007` rather than
introducing a second stop path. The status screen's `E` tag
(`REQ-CoreLR-D-LcdJoystickUI-0002`) reflects this shared flag.

## REQ-CoreLR-D-GCodeParser-0006 — Error acknowledgment (confirmed 2026-09-06)
<a id="REQ-CoreLR-D-GCodeParser-0006"></a>
Once in the error state, the machine stays halted in place, showing `E`
plus the failing line/reason, until the user explicitly acknowledges it
with `SW` (short press) from the status screen. Only after acknowledgment
can a new job be started. Restarting the same job restarts **from the
beginning** — resuming from the failing line is not supported.

**Rationale:** prevents a job from being silently/accidentally restarted
without the user having seen what went wrong.

## REQ-CoreLR-D-GCodeParser-0007 — Acceptance depends on the line, so tokenize first (implemented 2026-09-10)
<a id="REQ-CoreLR-D-GCodeParser-0007"></a>

`GCodeParser_ExecuteLine()` shall tokenize the line **before** deciding
whether it can be accepted, because what the line contains determines
what it needs:

| Line contains | Requires |
|---|---|
| a move (`X`/`Y`) | a free slot in the look-ahead buffer |
| a pen word (`M3`/`M5`) | the whole pipeline drained: `Motion_IsIdle()` **and** `Motion_PlannerIsEmpty()` |

Safe in that order precisely because `TokenizeLine()` is the
side-effect-free half of the two-pass design
(`REQ-CoreLR-D-GCodeParser-0001`): discarding its result and
re-tokenizing on a later pass costs microseconds.

**Why a pen word must drain the pipeline.** A pen action goes straight
into the step queue, whereas moves now sit in the planner buffer ahead
of execution. Without draining, the pen would fire *before* moves that
should precede it -- lifting or dropping at the wrong point on the
drawing, and silently breaking the synchronisation
`REQ-CoreLR-F-PenControl-0004` guarantees. Draining costs nothing in
practice: pen changes are rare and already pay a 300 ms settle.

```
   line:   G1 X10   G1 X20   M5      G1 X30
   plan:   [B0]     [B0 B1]  drain   [B0]
                             wait    (pen lifts at X20, correct)
```

Side effect worth noting: `TokenizeLine()` now also zeroes the value
fields, not just their `Has*` flags. Not a correctness fix -- a value
is only read when its flag is set -- but the compiler could no longer
prove it once the order changed, and an invariant a reader has to
reconstruct is worth two stores.

## REQ-CoreLR-D-GCodeParser-0008 — A queued move starts where the queue ends (implemented 2026-10-07)
<a id="REQ-CoreLR-D-GCodeParser-0008"></a>
The start point of a line — what a relative `X`/`Y` is added to, and what an omitted axis keeps — is the
position the machine will have **once everything already accepted has been executed**
(`Motion_GetTargetPosition()`), not the position it has now (`Motion_GetComputedPosition()`).

**Why.** Moves are queued in the look-ahead planner ahead of their execution
(`REQ-CoreLR-D-Motion-0013`), so the executed position is behind every block still queued. Measured
on the host, through the real parser and the real planner:

| lines | queue ends at | correct |
|---|---|---|
| `G91` `G1 X10` `G1 X10` `G1 X10` | X = **10** mm | 30 mm |
| `G1 X10 Y5` then `G1 X20` (Y omitted, modal) | Y = **0** | 5 mm |
| absolute, relative, absolute | (0, **0**) | (0, 4 mm) |

Every file with relative moves or omitted axes would have been drawn wrongly, collapsed or
rubber-banded back to the executed position. It dates from the introduction of the planner and was
never seen because **no GCode job had ever been run** — every speed and behaviour figure so far comes
from the jog (`../docs/bringup/Bringup-Status.md`). It was found while specifying arcs, which need exactly this
start point.

**Verified** by `tests/host/test_parser.c` (the real `GCodeParser.c` and `Motion.c`): the four cases
above, plus the same chain with the head already moving. The test **fails on the unfixed parser** (four
failures) and passes on the fixed one.

## REQ-CoreLR-D-GCodeParser-0009 — Arc words and their validation (implemented 2026-10-07)
<a id="REQ-CoreLR-D-GCodeParser-0009"></a>
Realizes `REQ-CoreLR-F-GCodeInterpreter-0003`, `-0009`, `-0010`.

**Tokenizing.** `TokenizeLine()` learns the words `I` and `J` (a µm value each, like `X`/`Y`), the motion words
`G2`/`G3` and the plane word `G17` (accepted, means nothing: it is the only plane); `R` stays an unrecognised word,
which is already an invalid line, so the deferral of `-0003` needs no code; `G18`/`G19` and any other `G` value are
rejected as before. `ParsedLine_t` gains `HasI`/`IValueUm`/`HasJ`/`JValueUm`.

**Two groups, two fields.** `ParsedLine_t` held a single `G` value, so on a line carrying two G words — `G91 G1 X10`
— the later overwrote the earlier and **the distance word was silently lost**. It now has `MotionWord` (0…3, or
−1) and `DistWord` (90, 91 or 0), one per modal group (`REQ-CoreLR-F-GCodeInterpreter-0010`). A line's own distance
word decides how *its* target is computed and is also recorded for the lines after it; both are tested.

**State.** `MotionModeSet` became `MotionMode`, the mode itself (0…3, or −1 for none yet), so that a bare `X Y I J`
line repeats the last arc exactly as a bare `X Y` repeats `G1`.

**An invalid arc line, refused before any effect** (`-0007`'s two-pass rule): the tokenizer refuses a line with
`I`/`J` and no `X`/`Y`, or `X`/`Y` and no `I`/`J` in an arc mode, or `I`/`J` under `G0`/`G1` (a word silently
ignored is worse than a halt), or any coordinate word before a motion mode has been set. The radius test needs the
start point, so it is a **pure function in `Motion`**: `Motion_ArcCheck(&target, i, j)` (the start is where the queue ends; it was a parameter until 2026-10-08), called from the
acceptance stage of `GCodeParser_ExecuteLine()` after tokenizing and **before** the BUSY test — its answer does not
depend on the planner, and a line must not be refused for the wrong reason — and before `ApplyParsedLine()`.
`Motion_QueueArc()` then fails only for a full planner (`GCODEPARSER_BUSY`, as for a line).

`ComputeTarget()` (new) is the one place a line's end point is computed, from the queue's end
(`Motion_GetTargetPosition()`, `-0008`), shared by the acceptance stage and `ApplyParsedLine()`. `I`/`J` are always
relative to the start, whatever `G90`/`G91` says; a missing one is 0; `G2` is clockwise, `G3` counter-clockwise.

**Verified** (`tests/host/test_parser.c`, the real parser, planner and generator): `G3`/`G2` end points and
direction (a half circle passes through +Y for `G3`, −Y for `G2`); a modal bare arc line; relative mode; a full
circle; `G17`; `G91 G1 X5` then `G1 X5` ends at 10 mm (the distance word persists); eight invalid arc lines refused
with **no effect** — nothing queued, no pen action, no leaked `G91` — including a mismatched radius carrying
`G91 M3`; a valid arc carrying `M3` accepted with one pen-down; BUSY for a full planner but ERROR for an invalid arc
even then. Six deliberately broken copies of the parser, **all caught** (two only after tests were added for them).

## REQ-CoreLR-D-GCodeParser-0010 — The reach check, and numbers that cannot wrap (implemented 2026-10-08)
<a id="REQ-CoreLR-D-GCodeParser-0010"></a>
Realizes `REQ-CoreLR-F-GCodeInterpreter-0011`.

**The check.** `ComputeTarget()` already produces a line's end point from where the queue ends. `GCodeParser_ExecuteLine()` now
asks `OutOfReach()` of it — one OR of the two magnitudes and one shift by `MOTION_REACH_SHIFT` (21) — and returns
`GCODEPARSER_ERROR` before anything is applied or queued.

**Order of the acceptance stage: validity first, then busyness.** The reach test and the arc's radius test
(`-0009`) now come **before** the pen word's "drain the pipeline first" `BUSY` and the planner-room `BUSY`. Whether a line is
acceptable does not depend on the planner's state, and a line must not be put off only to be refused later: an out-of-reach
move is an `ERROR` even with the planner full, and even when it carries an `M3`/`M5` that would otherwise wait. (The arc test
used to follow the pen-word `BUSY`; moved.)

**Numbers cannot wrap.** `ParseNumber()` accumulated the integer part into an `int32_t` with no bound, so a long digit string
wrapped — `X8589934.592` (2^33 µm after the × 1000) became **0**, a legal coordinate. The integer part now saturates at
10^6 (a kilometre, in mm): after the × 1000 of `GCodeNumberToUm()` it still fits an `int32_t`, and far exceeds the reach, so
the line is refused. The offsets `I` and `J` take the same path, so `I99999999999` is refused by the arc test rather than
wrapping into a plausible centre. A saturated number added to an in-range position (< 2^21) cannot overflow either.

**Cost, measured** (Nano fqbn, whole image): **+118 bytes of flash** (30 588 → 30 706; **14 bytes free**). One trim was kept: one OR and one
shift for both axes instead of two tests (−16). RAM and stack unchanged. The margin is now too thin to take any further feature
without a saving elsewhere.

**Verified** (`tests/host/test_parser.c`, the real parser, planner and generator; both arithmetics): the last micrometre
inside (±2 097.151 mm, X and Y, both signs) and the first outside; `G0` and `G1`; one coordinate in and one out; the digit
strings that used to wrap (2^31, 2^32 and 2^33 µm, 20 digits, negative 20 digits with a fraction); relative moves judged where
they end, including a second one after an accepted one; refused lines leave **no trace** (planner, pen action, position, and a
`G91` they carried is not applied); `ERROR`, not `BUSY`, with the planner full and with a pen word; arcs (end point out of reach;
an absurd centre; an ordinary half circle untouched); a coordinate at the limit is exact. **Nine broken copies** — check removed,
shift 22 and 20, only X or only Y checked, negatives not folded, no saturation, saturation at 10^7 (× 1000 overflows),
saturation to a legal value — **all caught**.

## REQ-CoreLR-D-GCodeParser-0011 — The result says why, and a program end is a result (implemented 2026-10-08)
<a id="REQ-CoreLR-D-GCodeParser-0011"></a>
Realizes `REQ-CoreLR-F-GCodeInterpreter-0007` (the cause), `-0012` (program end); request FW-04 and FW-10 of `docs/protocol/SerialProtocol.md`.

**The result is a byte.** `GCodeParser_Result_t` was an enum (`OK`, `BUSY`, `ERROR`); it is now a `uint8_t`:

| Value | Meaning |
|---|---|
| `0` `GCODEPARSER_OK` | line parsed and executed |
| 1, 2, 20, 26, 33, 34, 35, 36 `GCODEPARSER_ERR_*` | line refused; **the value is grbl's status number** (§4.6 of the protocol document), so a sender can show grbl's text |
| `254` `GCODEPARSER_END` | executed, and it was a program end (M2/M30) |
| `255` `GCODEPARSER_BUSY` | not attempted, retry |

`GCODEPARSER_IS_ERROR(r)` is "neither OK, END nor BUSY". No grbl number is above 38, so nothing collides. Each failure site returns its own constant,
which costs nothing over returning `ERROR`; the SD variant tests `IS_ERROR()` and ignores which one.

| Cause in the code | Constant (grbl N) |
|---|---|
| a character that is not `A`–`Z` where a word starts | `ERR_EXPECTED_LETTER` (1) |
| a letter with no number, or `-`/`.` alone | `ERR_BAD_NUMBER` (2) |
| unknown word letter, unsupported `G`/`M` value, fractional `G`/`M`, `G`/`M` above 254 or negative, a program end with a move on its line | `ERR_UNSUPPORTED` (20) |
| coordinates with no motion mode yet; an arc with no end point | `ERR_NO_AXIS_WORDS` (26) |
| end point beyond the reach (`-0010`) | `ERR_TARGET_INVALID` (33) |
| arc radii inconsistent (`-0009`) | `ERR_ARC_RADIUS` (34) |
| an arc with neither `I` nor `J` | `ERR_ARC_NO_OFFSET` (35) |
| `I`/`J` on a `G0`/`G1` line | `ERR_UNUSED_WORDS` (36) |

The order of detection is grbl's: the letter first (1), then the number (2), then whether the word means anything (20). So an unknown letter
with no number is 2, as in grbl. Causes of the link itself (11 line too long, 3, 8, 9) belong to the serial layer, not here.

**Program end.** `M2` and `M30` are accepted next to `M3`/`M5`. They are acted on like `M5` — they wait for a drained pipeline (`BUSY`
otherwise), then lift the pen — and then call `GCodeParser_Init()`: distance mode back to `G90`, no motion mode, pen up. The parser
returns `GCODEPARSER_END` and leaves the consequence to the caller. A line that has a **move** and a program end is refused
(`ERR_UNSUPPORTED`), not reordered: the pen lift goes into the step queue immediately, a move waits in the planner, so
`M30 G1 X5` would lift the pen *before* the move, which is the opposite of grbl's order (the end comes last). Nobody writes it.

**`G21`, `G54`, `G94`** are accepted with no effect, in the `G17` branch.

**In the SD sketch** (`grbl_corelr.ino`): `GCODEPARSER_END` sets `s_programEnd`; while it is set no further line is dequeued, and
the completion test accepts it in place of "file exhausted" — so the job ends once the planner, the generator and the step
queue are empty, exactly like an end of file, and the rest of the file is not executed. `s_programEnd` and the modal state are reset
at **job start**. (The parser's `GCodeParser_Init()` used to be called once, from `setup()`: a second job inherited `G91` and the
pen state of the first one, and so did a job started after a cancel. A defect found on the way, not a decision; fixed by the call at job start.)

**Not decided here:** what a serial sender does after `GCODEPARSER_END` (the next line starts a new job: increment 2).

## REQ-CoreLR-D-GCodeParser-0012 — A G or M code is one byte after saturating (implemented 2026-10-08)
<a id="REQ-CoreLR-D-GCodeParser-0012"></a>
`GCodeNumberToInt()` returned an `int32_t`, and every comparison of a `G`/`M` value was four bytes wide on the AVR. `GCodeNumberToCode()` returns a
`uint8_t` — **255 for anything negative or above 254**, which is no supported code either way. The saturation is what makes it safe: a plain
cast would turn `G277` into `G21` and `M258` into `M2`; the tests have those (and `G346`, `M286`, 2^32 + 21), and a mutant without the
saturation is caught.

**Measured** (Nano fqbn, whole image, SD variant): the first form of FW-04 + FW-10 with `int32_t` codes cost **+186 bytes**; with byte codes
**+36 bytes in all** (30 000 → 30 036; 684 free), RAM +1 (`s_programEnd`), worst-case stack 312 → 309 (`ParsedLine_t` lost three bytes).

**Verified** (`tests/host/test_parser.c`; `tests/mutation/mutants/parser_errors_end.txt`: **31 broken copies, all caught**; the first run left one
alive, the upper bound of the letter test, and a test was added): every cause of the table; the saturation cases; `G21`/`G54`/`G94` have no effect and
share a line with a move; `M2`, `M30` and `m30` give `END`, lift the pen once, return to `G90` and no motion mode; `BUSY` with moves queued; a
move on the same line refused; `M5` is still not a program end. The stream hash of every entry sent to `Stepper` is **identical**
(`tools/bench/stream_hash.sh`: `3a04d9a6`, `a9cfe556`). The sketch's own handling of `END` (`s_programEnd`) is not covered by a host test — the
sketch is not host-compiled — and is for the bench.

## REQ-CoreLR-D-GCodeParser-0013 — The line is read in place, in any length (implemented 2026-10-09)
<a id="REQ-CoreLR-D-GCodeParser-0013"></a>
`TokenizeLine()` copied the line into a 48-byte upper-case buffer (`upperLine`, sized by the SD reader's line size) before reading it. The copy is **gone**: the tokenizer upper-cases the one
letter it is looking at. Three consequences, all measured: the SD build is **154 bytes smaller** (30 036 → 29 882) and its worst-case stack 33 bytes shallower; a line longer than 47 characters is
no longer **truncated silently** (the parser dropped everything after the 47th: `G1 X10` + 80 blanks + `Y5` lost its `Y5` — the new test fails on the old code); and the serial link, whose lines are up to
80 characters, needs no second buffer. The hash of every entry sent to `Stepper` is unchanged. Realizes request FW-03's "the parser's `upperLine` copy sized by it" in the only way that costs nothing.

## REQ-CoreLR-D-GCodeParser-0014 — The Z word (implemented 2026-10-10)
<a id="REQ-CoreLR-D-GCodeParser-0014"></a>
Realizes `REQ-CoreLR-F-GCodeInterpreter-0015`. The tokenizer keeps a `Z` word (`HasZ`, `ZValueUm`); `PenFromZ()` turns it into a pen action -- 1 down, 0 up, -1 none -- once the line's distance mode is known (a `G90`/`G91` later on the same line counts):
absolute `Z <= 0` down, `Z > 0` up; relative the sign, `Z0` none. `ExecuteLine()` treats it as an `M3`/`M5` for acceptance (BUSY until the pipeline has drained), `ApplyParsedLine()` as one for effect (the modal pen state, `PenControl_RequestDown/Up()`, before the line's move).
`M` and `Z` on one line: error 36 (unused words). Behind `CORELR_GCODE_Z` (`SwConfig.h`): off in the SD variant, where `Z` stays an unsupported word (error 20): **+156 bytes of flash** there would not link (30 728 / 30 720), and the SD build is left at 126 free.
The serial build's flash for the Z word, `$J=` and 0x85 together is in `REQ-CoreLR-D-SerialLink-0011`. **Verified:** `test_parser.c` -- every row of the rule above, the pen counted; a Z with a move; BUSY with moves queued; two pen words; and with the option off. `tests/mutation/mutants/zword.txt`: 8, all caught (the last under `-D CORELR_GCODE_Z=0`).

## REQ-CoreLR-D-GCodeParser-0015 — `GCodeParser_ExecuteJog()`, serial variant (implemented 2026-10-10)
<a id="REQ-CoreLR-D-GCodeParser-0015"></a>
*Applies to: serial image only*

Realizes `REQ-CoreLR-F-SerialControl-0012`. The body of a `$J=` line goes through the **same tokenizer**, told it is a jog (`jog = 1`): it skips the check that bare coordinates have a motion word, and the function then refuses a motion word, an arc offset or a pen word
(error 20), accepts and ignores `F` and `Z`, refuses a target beyond the reach (33), answers BUSY when the planner is full (the link keeps the line, as for a G-code line), and queues **one `Motion_QueueMove()`** from where the queue will end -- the same rule as a line.
It never changes the modal state, and its distance mode is the line's own word, else the parser's current one. A jog with no X or Y (Candle's Z buttons) is accepted and does nothing. Compiled only in the serial variant (`FW-11`).
**Verified:** `test_parser.c`, "`$J=`": relative and absolute jogs, the modal state untouched, a fresh parser, behind a queued move, each refusal and "no trace", Z-only, a full planner. `tests/mutation/mutants/jogparser.txt`: 7, all caught.

## REQ-CoreLR-D-GCodeParser-0016 — The travel zone in the parser (implemented 2026-10-11)
<a id="REQ-CoreLR-D-GCodeParser-0016"></a>
Realizes: `REQ-CoreLR-F-GCodeInterpreter-0016`. In the validity block of `GCodeParser_ExecuteLine()` (before any word is applied, before the busy check -- a line is not put off only to be refused later, `-0010`), after the reach test: `!Motion_TravelAllows(&target)` answers
`GCODEPARSER_ERR_TARGET_INVALID` (status 33). For an arc, `Motion_ArcCheck()` adds the whole-arc test (`REQ-CoreLR-D-Motion-0028`); its refusal keeps the arc-radius error number. `GCodeParser_ExecuteJog()` applies the same zone test to its target. Host tests: `tests/host/test_travel.c` (lines, arcs, `$J=`), mutants `tests/mutation/mutants/travel_zone.txt`; `Motion_ArcCheck()` gained the direction (`clockwise`), which the bulge test needs. `test_parser` and `test_arc`, which are about the reach and the radius rule with coordinates far outside any real zone, are built with the zone widened (`-DMOTION_MARGIN_SIDE_UM=-4000000L -DMOTION_MARGIN_TOP_UM=-4000000L`, `tests/host/run_test.sh`).


## REQ-CoreLR-D-GCodeParser-0017 — `G92` and `G10 L20 P1` (implemented 2026-10-11)
<a id="REQ-CoreLR-D-GCodeParser-0017"></a>
Realizes: `REQ-CoreLR-F-GCodeInterpreter-0017`. The tokenizer reads `G92` and `G10` (with `L` and `P` words, small integers) into `ParsedLine_t.OriginCmd` (1 = `G92`, 2 = `G10`), and validates the line as a whole before anything is applied: `G10` needs `L20` and `P1` (else 20); `L` or `P` without `G10` is 36 (unused words); an axis-word-less origin command is 26; one that shares its line with a motion word, an arc offset, an `M` word or a `Z` is 21 (`GCODEPARSER_ERR_AXIS_CONFLICT`); the "bare coordinates before any motion word" rule does not apply to it (its X and Y are origin values).
`GCodeParser_ExecuteLine()` answers `GCODEPARSER_BUSY` until `Motion_IsIdle() && Motion_PlannerIsEmpty()` (like a pen word), then calls `Motion_SetOrigin()` (axes: those with a word) with the X and Y values **as written** (absolute: G90/G91 are not consulted); a refusal by the zone is 33. `GCodeParser_ExecuteJog()` refuses all of it (20).
A Z word on such a line is **ignored** (Candle restores its work coordinates with `G92 X.. Y.. Z..`), a pen word is not (21). Values beyond the machine's reach are refused (33) before any arithmetic, as for a target. Tests: `tests/host/test_origin.c` (every refusal above, absolute under G91, the modal state untouched, BUSY with a move queued, not a jog); `test_parser.c`.


## REQ-CoreLR-D-GCodeParser-0018 — `G53` and `$H` (implemented 2026-10-11, serial variant)
<a id="REQ-CoreLR-D-GCodeParser-0018"></a>
*Applies to: serial image only*

Realizes `REQ-CoreLR-F-GCodeInterpreter-0018`, `REQ-CoreLR-F-SerialControl-0014`.
- `ParsedLine_t.Machine`: set by `G53` (value 53 is the only G word >= 4 that is not refused, in the serial build). The tokenizer refuses it without a motion word on the line or modal (error 20). `ComputeTarget()` turns a machine target into a drawing one by subtracting the machine offset (`Motion_GetMachineOffset()`, `-Motion-0030`), absolute whatever the distance mode. `PenFromZ()` reads Z as machine Z under `G53` (>= 0 up, < 0 down).
- `GCodeParser_ExecuteHome()`: BUSY until the machine is idle and the planner empty; then lifts the pen if the modal state says it is down and queues one move to the drawing origin (0,0). `$H` is not a G-code line: SerialLink gates it and calls this.

## Open items

### Submit-and-retry instead of caller-side gating (maintainer proposal 2026-09-09, implemented 2026-09-10)
Applied: `GCODEPARSER_BUSY` added, `Motion_IsIdle()` checked inside
`GCodeParser_ExecuteLine()`, the dead `GCodeParser_GetState()` removed,
and `GCodeParser_State_t` moved into the `.c` as file-local
`ParserState_t`. `grbl_corelr.ino` lost its `Motion_Task()`-idle gating
and now holds the pending line in file scope so a refused line is
offered again rather than dropped (+1 byte).

**Today** `grbl_corelr.ino` gates the hand-off itself: it checks
`Motion_IsIdle()` (via `Motion_Task()`'s return), *then* dequeues a line
from `SDReader`, *then* calls `GCodeParser_ExecuteLine()`. The caller
therefore has to know that the parser can only accept a line while
motion is idle — knowledge that belongs to the parser.

**Proposed**, mirroring `Stepper`'s accept-or-refuse contract: the
parser decides. `GCodeParser_ExecuteLine()` gains a third result:

| Result | Meaning |
|---|---|
| `GCODEPARSER_OK` | line parsed **and executed** |
| `GCODEPARSER_BUSY` | not attempted — cannot accept right now; caller retries later |
| `GCODEPARSER_ERROR` | line invalid — job halts (`-0005`) |

The parser checks `Motion_IsIdle()` internally (it already includes
`Motion.h`), and `grbl_corelr.ino` loses the `waypointPipelineIdle`
gating entirely.

**The subtlety that must be handled: a refused line must not be lost.**
`SDReader_Dequeue()` *consumes* the line, so dequeuing before a refusal
would drop it. The caller must therefore only dequeue when it is not
already holding one:

```c
static SDReader_Line_t line;      /* already static today */
static uint8_t         haveLine = 0;   /* +1 byte: must persist across loop() */

if (!haveLine) { haveLine = SDReader_Dequeue(line); }
if (haveLine)
{
   switch (GCodeParser_ExecuteLine(line))
   {
      case GCODEPARSER_BUSY:  break;               /* keep the line, retry next pass */
      case GCODEPARSER_OK:    haveLine = 0; break;
      case GCODEPARSER_ERROR: haveLine = 0; /* halt + error state */ break;
   }
}
```

Cost: **1 byte** of RAM. Deliberately *not* buffering the line inside
the parser: that would need a second 48-byte copy, and the caller's
buffer is already static — the pointer stays valid as long as the caller
does not dequeue again, which the `haveLine` flag guarantees.

**Why `ExecuteLine` keeps its name** (unlike `Stepper`'s
`Enqueue*` → `Request*`): here `OK` genuinely means *executed*, not
merely *accepted for later*. The parser never defers work it has
accepted, so "Execute" remains accurate — the Request+Task rename was
driven by `Stepper`'s backlog, where acceptance and completion are
genuinely separate.

**Consequences for the public surface:**
- `GCodeParser_GetState()` becomes unnecessary and shall be removed. It
  is already **dead code** — verified 2026-09-09: defined, never called
  anywhere. Its persistent-error variant is not needed either: the
  halted-on-error state lives in `Stepper`'s halt flag plus
  `LcdJoystickUI`'s status tag, and the `SW` acknowledgment
  (`REQ-CoreLR-F-GCodeInterpreter-0007`) clears it via
  `Stepper_ResumeRequests()`.
- `GCodeParser_State_t` then has no public use — it holds purely modal
  parsing state (distance mode, pen state, motion-mode-set) that no
  other module reads — and moves into `GCodeParser.c` per
  `docs/standards/STD-FileOrganization.md` ("Expose only what
  callers need"). It is public *today only because* the dead getter
  returns it.


- Internal command representation passed from the parser to the
  Motion/PenControl modules (exact struct) is not yet detailed.
- Exact failing-line/reason detail shown alongside the `E` tag (e.g. line
  number, offending word) is not yet specified.
