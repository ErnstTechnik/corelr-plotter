# Design Requirements — SerialLink

Module: **SerialLink** — the protocol of the serial variant: lines in, one reply per line, the real-time commands, the `?` status
report, the `$` commands, the welcome line. **Serial variant only**: in the SD build `SerialLink.c` and `SerialLink.h` are empty.

Realizes: [`REQ-CoreLR-F-SerialControl-0004`…`-0011`](../functional/REQ-CoreLR-F-SerialControl.md); requests **FW-03, FW-05, FW-06, FW-07, FW-08, FW-09**
of [`docs/protocol/SerialProtocol.md`](../../docs/protocol/SerialProtocol.md) (the contract; this file records how the firmware side realized it and what it measured).
Uses [`Uart`](REQ-CoreLR-D-Uart.md), [`GCodeParser`](REQ-CoreLR-D-GCodeParser.md), `TextFmt`.

Origin: the contract, approved 2026-10-08; implemented 2026-10-09.

## REQ-CoreLR-D-SerialLink-0001 — What the module owns, and what it does not (implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0001"></a>
*Applies to: serial image only*

It owns the **link**: assembling a line from the receive ring, submitting it to the parser, the reply, the real-time bytes, the report, the `$`
commands and the welcome line. It does **not** own the job state machine, which stays in `grbl_corelr.ino`: `SerialLink_Task(state, lcdBusy)` is *told* the state
and *answers* with at most one request (`SERIALLINK_REQ_START`, `_HALT`, `_RESUME`, `_CANCEL`, `_ACK_ERROR`, `_FAIL`). The reason is testability: the rules
of the link (what is answered, when, what waits) are checked on the host against the real `Uart`, parser and planner, with the sketch's state
machine modelled in thirty lines of the test (`apply()` / `step()`, one case per request: the table of `-0006`). The state names live in `JobState.h`
(types only, no code).

## REQ-CoreLR-D-SerialLink-0002 — Line assembly (FW-03, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0002"></a>
*Applies to: serial image only*

Bytes are taken from the ring one at a time until a line is complete (a line is **held**, and no more bytes are read, until it has been answered: the held
line has left the ring, which is what the host's character counting assumes). The rules, in the order they are applied:

- the end of a line is **LF, CR or CRLF**, and a LF immediately after a CR is dropped, or a CRLF sender would get two replies per line (grbl itself has the
  weakness). The skip survives the consumption of the line (a bug found by the test: clearing it when the line was dropped re-created the double reply);
- inside `;` (to the end of the line) and `( … )` comments everything is dropped; an unclosed `(` ends with the line;
- spaces, tabs and every other control character are dropped; letters are made **upper case** here, so the parser and the `$` commands need not care;
- **80 significant characters** (`SERIALLINK_LINE_MAX`, grbl's `LINE_BUFFER_SIZE`; the 47 of the SD path is too short once a generator writes 6 decimals); a longer line
  is read to its end and answered `error:11`, **never truncated** — a truncated coordinate is a different, legal-looking one;
- a line that follows lost bytes (`REQ-CoreLR-D-Uart-0004`) is treated as an over-long one: `error:11`.

The 81-byte line buffer replaces `GCodeParser`'s own 48-byte copy, which is **gone** from both variants (`REQ-CoreLR-D-GCodeParser-0013`): the tokenizer upper-cases on the fly, so a
line of any length is read to its end, and the SD build is 154 bytes smaller and its stack 33 bytes shallower (measured).

## REQ-CoreLR-D-SerialLink-0003 — A reply is written whole or not started (FW-02/03, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0003"></a>
*Applies to: serial image only*

Nothing blocks. Before a line is run, the link checks the transmit ring has room for its **reply**: 24 bytes for a G-code line (the longest, `[MSG:Pgm End]\r\nok\r\n`,
is 19; `error:36\r\n` is 10), the whole 64-byte ring (empty) for a `$` line, 60 for a status report (the longest is 57 characters, tested). If there is not, the line stays held and
is tried again on the next pass; a `?` stays flagged until it fits. The ring drains at 11 765 bytes/s, so the wait is a fraction of a millisecond. The one thing that can
exceed the ring is the welcome sequence (30 + 40 bytes), which is sent in two steps.

## REQ-CoreLR-D-SerialLink-0004 — Which line does what (FW-05, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0004"></a>
*Applies to: serial image only*

Exactly one reply per line, in order (contract §4.4):

| State | A G-code line | A `$` line |
|---|---|---|
| alarm (`ERRORSTOP`, `ERROR`), uncalibrated, an LCD mode that can move the machine | `error:9`; an empty line is `ok` | answered |
| hold (`HALTING`, `PAUSED`, `CANCELLING`) | **held**, no reply until the resume; the ring behind it fills | answered |
| `IDLE`, `RUNNING` | to the parser; its result is the reply | answered |

Parser results: `OK` → `ok`; `END` (M2/M30) → `[MSG:Pgm End]` then `ok` (Candle relies on it); `BUSY` → nothing yet, the line is kept and offered again; an error →
`error:N` with grbl's number (`REQ-CoreLR-D-GCodeParser-0011`). During a job (`RUNNING`) a refusal also **stops it** (`SERIALLINK_REQ_FAIL`: ramped stop, pen up, alarm, as on SD, `-0007`) and the lines
already in flight are each answered `error:9`, which keeps a counting host's arithmetic right. **Outside a job** (`IDLE`) it is only reported, the state stays `Idle` (P12). The first line
accepted while `IDLE` starts the job (`SERIALLINK_REQ_START`). **A `$` line is never held**: `$G` must be answered at any time, mid-stream and while paused (Candle polls it every second).

## REQ-CoreLR-D-SerialLink-0005 — The status report and the `$` commands (FW-06, FW-07, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0005"></a>
*Applies to: serial image only*

`<Run|MPos:12.345,-67.890,0.000|Bf:2,87|FS:0,0>` (contract §5): the executed position (`Motion_GetComputedPosition()`), drawing frame, Y up, **three axes**, `Bf:` = free planner
blocks (`Motion_PlannerFree()`), free ring bytes (`Uart_RxFree()`), `FS:0,0`, no `Ov:`. The state words: `Run`, `Hold:1` (halting, cancelling), `Hold:0` (paused), `Alarm` (error states, uncalibrated),
`Jog` (an LCD mode that moves the machine), `Idle`. Millimetres with three decimals come from the new `TextFmt_Micrometres()` (not `snprintf`: 1 372 bytes), tested against `printf` on
14 707 values and the clipping at the buffer's end. `$`, `$$` (`$10=3 $13=0 $20=0 $21=0 $22=0 $32=0`), `$G` (the parser's modal state through the new `GCodeParser_GetModal()`), `$I`
(`[VER:1.1h.20261011:CoreLR <version>]`, the version being `CORELR_VERSION_STRING` of `Version.h` (`REQ-CoreLR-D-Build-0020`), `[OPT:,4,128]` with the planner depth and the ring size taken from the macros), `$X` (acknowledges the error state; `error:8` while running or paused; refused with the
"not calibrated" message and `error:9` when uncalibrated); everything else of grbl's family is `error:3`. Texts that are not replies contain neither `ok` nor `error` (tested).

## REQ-CoreLR-D-SerialLink-0006 — The job state machine, from the sketch's side (FW-05, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0006"></a>
*Applies to: serial image only*

`grbl_corelr.ino` runs `SerialLink_Task(s_jobState, LcdJoystickUI_IsBusy())` once per `loop()` pass, after the LCD's own requests, and executes what it asks. The job transitions became functions
(`JobHalt`, `JobResume`, `JobCancel`, `JobAckError`) so the LCD and the link do exactly the same thing; in the SD build each has one caller and the compiler folds it back (**the SD image did not change**).

| Request | Effect (the model in `test_seriallink.c` implements this table) |
|---|---|
| `START` | `IDLE` → `RUNNING`; `Stepper_ResumeRequests()`. **No `GCodeParser_Init()`**: a user typing `G91`, then `G1 X10`, in two jobs keeps `G91`. Only `M2`/`M30` and Ctrl-X reset the modal state |
| `HALT` (`!` while `RUN`) | ramped stop, pen up, then `PAUSED` |
| `RESUME` (`~` while `Hold:0`) | pen back down if it was, then motion |
| `CANCEL` (Ctrl-X while `RUN`/`HOLD`) | ramped stop, pen up, `IDLE` |
| `ACK_ERROR` (`$X`, Ctrl-X in the error state) | `ERROR` → `IDLE` |
| `FAIL` | ramped stop, pen up, `ERROR` |

The job **ends** (`RUNNING` → `IDLE`) when the link holds nothing (`SerialLink_HasWork()`: no line, no partial line, nothing in the ring, no welcome pending) and the planner, the generator and the
step queue are empty — grbl's own Run → Idle rule. It does **not** lift the pen (P1) and does not store the position at once: the position keeper does, after its stillness delay.
The link is also told if a Ctrl-X is in progress: it flushes the ring and the held line, resets the parser's modal state, drops what arrives until the job is `IDLE` (or the error is
acknowledged), then sends the welcome line again (contract §4.8). A real-time byte is acted on whatever line is held.

## REQ-CoreLR-D-SerialLink-0007 — The LCD during a serial job (FW-08, implemented 2026-10-09; the Cancel point **confirmed by the maintainer 2026-10-11, option A: it stays `Alarm`**)
<a id="REQ-CoreLR-D-SerialLink-0007"></a>
*Applies to: serial image only*

`LCDJOYSTICKUI_HAS_SD` removes the "Load file" menu item, the SD browser and `#include "SDReader.h"` from `LcdJoystickUI.cpp` in the serial variant (the menu has six entries, tested). `LcdJoystickUI_IsBusy()`
is 1 while a jog or a calibration wizard is on the screen: G-code is then refused (`error:9`) and the report says `Jog`. The menu already offers only pause/cancel (running) or resume/cancel (paused)
while a job is active, so a jog cannot be *entered* during one.

**Decision (confirmed 2026-10-11, `docs/protocol/SerialProtocol.md` P13).** The LCD's **Cancel** during a serial job ends in the **error state** (`ERROR`, reported `Alarm`), not in `IDLE` as for an SD job: the host does not know the cancel happened and keeps
streaming, and in `IDLE` its next line would simply start drawing the rest of the file from the middle. In the error state the lines in flight are answered `error:9`, the sender stops (UGS, CNCjs, gSender
pause; bCNC stops; Candle sends `!` and asks), and `$X`, Ctrl-X or the LCD's `SW` acknowledges. The contract says only that cancel "stays available from the joystick, and the host sees its effect in the
status report"; this is the conservative reading, one `#if` in the sketch's LCD event handler. Pause and resume from the LCD need no such care (a pause just holds the host's lines).

## REQ-CoreLR-D-SerialLink-0008 — Welcome, and the build (FW-09, FW-11, implemented 2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0008"></a>
*Applies to: serial image only*

`setup()` ends with `Uart_Init()` and `SerialLink_Init()`; the first `SerialLink_Task()` passes send `\r\nGrbl 1.1h ['$' for help]\r\n` and, if the machine is not calibrated,
`[MSG:Not calibrated: use the LCD menu]\r\n`. Nothing else is sent before. The variant is chosen by `CORELR_JOB_SOURCE` in `SwConfig.h`, now `#ifndef`-guarded so a build can say
`-D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL` (`tools/analysis/build_size.sh -D …`); in the Arduino IDE, edit the `#define` there. `Uart.c`, `SerialLink.c` and `LcdJoystickUI`'s SD part are empty or
absent in the other variant: **the SD image is the same size with and without them** (FW-11).

## REQ-CoreLR-D-SerialLink-0009 — Measurements, the ones that can be made without the bench (2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0009"></a>
*Applies to: serial image only*

Nano fqbn, `tools/analysis/build_size.sh -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL`:

| | Serial variant | SD variant (for comparison) | Contract's estimate |
|---|---|---|---|
| **M-1** flash | **25 680 / 30 720** (5 040 free) | 29 882 (838 free) | ≈ 5–6 KB free |
| static RAM | **785** (1 263 left) | 1 349 | |
| worst-case stack (`tools/analysis/stack_depth.py`) | **300** (+13 for an interrupt) → margin **≈ 950 bytes** | 276 | |
| **M-2** by source file (`tools/analysis/line_sizes.py`) | `Uart.c` **596**, `SerialLink.c` **1 484**, `TextFmt.c` 282 → 582 (**+300**, the formatter); the sketch 766 against 860 for SD (its serial glue is smaller than the SD line hand-off it replaces); `LcdJoystickUI.cpp` 3 462 against 3 734 (the SD browser gone) | | link 1.3–1.8 KB (A) |

So the link itself costs about **2.4 KB** (596 + 1 484 + 300), against the contract's estimate of 1.3–1.8 KB for A (UART 250–350, assembly 150, replies 60, report 250–300, `$` strings 400–700, job 150
= 1 260–1 710): the UART is 596 against 250–350, and the report and the `$` texts are larger. Still **5 KB free**. About 100 bytes more are linked that no one calls (`Print::write`, `SdFile` stubs, from the vendored sdfat compiled in every build: not chased). **M-6, the CPU part** (`tools/analysis/cyclesim/t_link.c`, `run_link.py`, the real code): one `SerialLink_Task()` pass costs
0.01 ms with nothing to do, **0.44 ms** for a 15-character `G1`, 0.60 ms for a 38-character one with six decimals, **0.73 ms** for an arc, 0.46 ms for a status report, 0.10–0.25 ms for `$$`/`$G`/`$I`, 0.15 ms for a refusal;
an 80-character line made artificially of digits is 1.08 ms (the number parser). The rest of the pass (planner, generator, LCD) is as before. **M-7 (the interrupts)** is in `REQ-CoreLR-D-Uart-0007`.
**Not measurable here and still to do on the bench**: M-3 (DTR to the welcome line), M-4 (latency of `!` and `?`), M-5 (sustained lines/s with 0.2 mm segments, does `Stepper` starve), and the *measured* part of M-6/M-7.

## REQ-CoreLR-D-SerialLink-0010 — Verification (2026-10-09)
<a id="REQ-CoreLR-D-SerialLink-0010"></a>
*Applies to: serial image only*

`tests/host/test_seriallink.c`, the whole stack but the hardware (the real `Uart.c` with its interrupts called by the test, `SerialLink.c`, the parser, the planner and generator): the welcome
(calibrated or not, first, nothing answered before it); line assembly on 24 inputs, CR/LF/CRLF in separate bursts, 80 and 81 characters, 700 blanks, 2 400 characters of comment; lost bytes
(a framing error in a line, a full ring); which line starts a job and the persistence of `G91` across implicit jobs; an error during a job (alarm, `error:9` for the lines in flight, `$X`) and outside one; `BUSY`
(the reply comes with the acceptance and the line behind is not read; a pen word waits for the drain); hold, resume, `!`/`~` in the wrong state; Ctrl-X from running, paused, in alarm, idle, uncalibrated, with bytes behind it;
the report in every state, three `?` giving one, waiting for room; every `$` command and every refusal (`$J=`, `$N`, `$#`, …); no `ok`/`error` in other lines; and a **torture**: 3 000 random lines through a host
that counts to 128 bytes, random real-time bytes, a transmit rate of 3 bytes per pass — one reply per line, in order, nothing malformed. `tests/mutation/mutants/seriallink.txt`: **91 broken copies, all caught** — after four rounds: the first left 25 alive, and what they showed was real: the bytes received between a Ctrl-X and the welcome line were not dropped (the contract asks for it), a reply waited for an empty transmit ring, the welcome did not wait for the end of the ramp, a line held at the Ctrl-X survived it, a half-received line did not keep the job open, a parenthesis comment's end was never tested. Four more were **equivalent** and removed (a redundant reset of the comment state and a redundant flush, deleted from the code; `{` upper-cased, which no parser can tell from `[`; a `$` test on a stale buffer, impossible because the line's terminator overwrites it; the welcome block not returning, since it also empties the ring and the flags). `tools/mutate.py` learned a `\\` escape on the way (a batch could not name C's own `\r\n`).

## REQ-CoreLR-D-SerialLink-0011 — `$J=` and 0x85 (implemented 2026-10-10)
<a id="REQ-CoreLR-D-SerialLink-0011"></a>
*Applies to: serial image only*

Realizes `REQ-CoreLR-F-SerialControl-0012` (docs/protocol/SerialProtocol.md P2, P6, increment 3). Not yet on the bench: the serial line was in use when this was written.
- **`$J=…`** is recognised before the other `$` commands (`IsJogLine()`) and run by `HandleJog()`: *locked* (`error:9`) when uncalibrated, in alarm (`ERRORSTOP`/`ERROR`) or with the LCD in a moving mode; *not idle* (`error:8`) during a job (`Run`, `Hold`, `Cancel`); otherwise
  `GCodeParser_ExecuteJog()` (`REQ-CoreLR-D-GCodeParser-0015`): `ok`, `error:N`, or -- planner full -- the line is **kept and its reply comes with its acceptance**, exactly as for G-code. A jog starts **no job**: no `SERIALLINK_REQ_START`.
  `$J=` with nothing after it stays `error:3`.
- **`Jog` in the status report** whenever the machine is Idle but Motion is not (`!(Motion_IsIdle() && Motion_PlannerIsEmpty())`), as it already was for an LCD mode: Candle's states include `Jog`.
- **0x85** is a real-time byte (`UART_RT_JOGCANCEL`, set in the receive interrupt, `REQ-CoreLR-D-Uart-0009`). Acted on only when Idle **and** moving: `SERIALLINK_REQ_JOG_CANCEL`; at rest, or during a job (where `!` and Ctrl-X are the way), it is ignored.
  The sketch answers it as it stops "Return home": `Motion_Halt()` (a ramp, not a dead stop), and when `Motion_IsHaltRampDone()` the rest -- the blocks still in the planner -- is dropped with `Motion_Abort()`. It is not a job state.
**Cost:** serial 26 392 → 27 202 bytes of flash for the Z word, `$J=`, 0x85 and the jog status together (+810; 3 518 free); RAM +1; the SD build is unchanged by the link (it grew 22 bytes for the tokenizer's new parameter).
**Verified:** `test_seriallink.c` section 14 -- a relative jog (ok, no job, `Jog` while queued, `Idle` after, the head where it should be); absolute, lower case, comment; chained jogs and a full planner (kept, then answered); each refusal (`9` uncalibrated / LCD / alarm, `8` running / paused, `20`, `33`, `2`, `3`, `11`);
Z-only; and 0x85 in flight (a bounded step queue holds the jog "in flight": status `Jog`, the ramp, the rest dropped, the head between 0 and 30 mm, ring empty), at rest, and during a job (nothing). `tests/mutation/mutants/joglink.txt`: 8, all caught.

## REQ-CoreLR-D-SerialLink-0012 — MPos in the machine frame, WCO, `$#`, `$H` (implemented 2026-10-11)
<a id="REQ-CoreLR-D-SerialLink-0012"></a>
*Applies to: serial image only*

Realizes `REQ-CoreLR-F-SerialControl-0013`/`-0014`. The status report's `MPos` is the drawing position plus `Motion_GetMachineOffset()`; `|WCO:x,y,0.000` follows `FS` when it is the first report, the tenth since the last, or the offset differs from the one last sent (8 bytes of RAM). `$#` answers `[G54:0.000,0.000,0.000]`, `[G92:<WCO>]`, `[TLO:0.000]`. `$H` is gated like `$J=` (`HandleHome()`) and calls `GCodeParser_ExecuteHome()`; its reply comes with its acceptance (BUSY: kept and offered again). The help line lists `$# $H`.

**Ctrl-X (FW-14, `REQ-CoreLR-F-SerialControl-0015`, 2026-10-12):** the link calls `GCodeParser_ResetByLink()` instead of `GCodeParser_Init()`: the job-start values, but the motion mode is `G0`, so Candle's post-Abort `G21G53G90X..Y..Z..` (no G word) is accepted and moves the head back. Only the link's Ctrl-X: an SD job's start and `M2`/`M30` keep "no motion mode". **`$I` (FW-13):** the date is that of the protocol, `20261011`. Tests: `test_seriallink` section 9; `tests/mutation/mutants/ctrlx_g0.txt`, 6 mutants, all caught.
