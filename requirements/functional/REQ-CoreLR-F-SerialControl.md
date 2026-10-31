# Functional Requirements — Serial Control

Sub-function: **SerialControl** — accept and execute GCode sent live
over a serial link (RS232/USB-serial), as with 3D printers or basic
GCode-controlled plotters — a second source of GCode alongside SD job
files (`REQ-CoreLR-F-JobStorage`).

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06) — **explicitly deferred: last
implementation priority**, only after SD-based job execution, Motion,
Actuation, Calibration and Pen Control are functional and validated.
See also: [`../../docs/protocol/SerialProtocol.md`](../../docs/protocol/SerialProtocol.md) (the protocol, contract between host and firmware).
Design: [`D-Uart.md`](../design/REQ-CoreLR-D-Uart.md), [`D-SerialLink.md`](../design/REQ-CoreLR-D-SerialLink.md) (implemented 2026-10-09, host-verified, not yet on the bench).
Started 2026-10-08, the rest of the system being functional (`-0002`).

## REQ-CoreLR-F-SerialControl-0001 — Live serial GCode execution
<a id="REQ-CoreLR-F-SerialControl-0001"></a>
*Applies to: serial image only*

The system shall accept GCode lines sent over a serial connection and
execute them through the same interpreter used for SD-sourced jobs
(`REQ-CoreLR-F-GCodeInterpreter`).

## REQ-CoreLR-F-SerialControl-0002 — Implementation priority
<a id="REQ-CoreLR-F-SerialControl-0002"></a>
*Applies to: serial image only*

This capability shall be implemented **last** in the project sequence —
after the rest of the system (motion, actuation, calibration, pen
control, SD job execution, LCD/joystick UI) is functional.

**Rationale:** confirmed 2026-09-06 by the maintainer; avoids
spending early effort on a second GCode input path before the core
machine works at all.

## REQ-CoreLR-F-SerialControl-0003 — Mutually exclusive with SD job storage (confirmed 2026-09-07)
<a id="REQ-CoreLR-F-SerialControl-0003"></a>
A single firmware build shall support **either** SD-sourced jobs
(`REQ-CoreLR-F-JobStorage`) **or** live-serial-sourced jobs, never both
at once. Selected at **compile time**, not runtime, via `CORELR_JOB_SOURCE`
in `SwConfig.h`.

**Rationale:** on the ATmega328P's 2KB of RAM, each input path's own
queues and library overhead (SD library's internal buffers vs. the
`Serial` object's own fixed RX/TX buffers) are each a meaningful
fraction of the whole budget — confirmed the hard way by a real RAM
overflow while only the SD path existed (`REQ-CoreLR-D-SDReader`/
`REQ-CoreLR-D-Stepper` open items). A *runtime* switch would
still need both code paths, and both sets of RAM, linked into the same
image simultaneously — it would not actually save anything, only a
compile-time selection (excluding the unselected path's code and data
entirely) does. This also resolves this file's former "mutually
exclusive?" open item.

## REQ-CoreLR-F-SerialControl-0004 — Existing free senders drive the machine (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0004"></a>
*Applies to: serial image only*

The serial link shall speak a **subset of the grbl 1.1 protocol**, chosen so that existing free GCode senders on Linux
(UGS, CNCjs, bCNC, Candle, gSender) can drive the machine without a tool of our own. Where CoreLR's behaviour differs from
grbl's, the difference is stated in the protocol document and chosen so that those senders still cope.

The protocol itself — bytes, replies, codes, states — is specified in [`../../docs/protocol/SerialProtocol.md`](../../docs/protocol/SerialProtocol.md),
the contract between the host side and the firmware side; this file keeps only the functional decisions.

## REQ-CoreLR-F-SerialControl-0005 — One reply per line, flow control by character counting (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0005"></a>
*Applies to: serial image only*

Every line received shall get exactly one reply, in order: `ok` once the line is accepted for execution, or an error. A line
the machine cannot accept yet gets its reply later, never a refusal. The machine shall accept **128 bytes** of lines in
flight (the figure the surveyed senders assume, and which some cannot lower).

## REQ-CoreLR-F-SerialControl-0006 — Errors over the link (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0006"></a>
*Applies to: serial image only*

A refused line shall be reported with grbl's `error:N` numbering, so that senders can show its cause. An invalid line stops
the job as on SD (`REQ-CoreLR-F-GCodeInterpreter-0007`), and the machine then refuses G-code until the error is
acknowledged — by `$X` or Ctrl-X over the link, or by `SW` on the machine.

An invalid line received while **no job is running** is only reported: the machine stays idle and no error state is
entered — there is no drawing to protect, and senders send lines of their own that CoreLR cannot accept (decided
2026-10-08, P12 of `docs/protocol/SerialProtocol.md`).

## REQ-CoreLR-F-SerialControl-0007 — Real-time commands (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0007"></a>
*Applies to: serial image only*

Status request, pause, resume and cancel shall be honoured **at any time**, including while lines are waiting to be
accepted — they never queue behind a line. Pause, resume and cancel have the same effect as the LCD's own
(`REQ-CoreLR-F-Actuation-0002`); cancel also acknowledges an error.

## REQ-CoreLR-F-SerialControl-0008 — Implicit job (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0008"></a>
*Applies to: serial image only*

There is no explicit job start: the first line accepted while idle starts a job, and the job ends when everything received
has been executed (or at `M2`/`M30`, `REQ-CoreLR-F-GCodeInterpreter-0012`). Reaching the end this way does **not** lift the
pen: a user typing lines by hand keeps the pen where the last `M3`/`M5` put it.

## REQ-CoreLR-F-SerialControl-0009 — An uncalibrated machine refuses motion (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0009"></a>
*Applies to: serial image only*

While the machine is not calibrated, the link shall refuse every G-code line and refuse to be unlocked; calibration is done
on the LCD only.

## REQ-CoreLR-F-SerialControl-0010 — Restart on connection (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0010"></a>
*Applies to: serial image only*

Opening the port restarts the Nano; this is accepted, as with grbl. The machine announces itself once ready, and a host
shall treat any announcement it did not cause as the loss of the job in progress.

## REQ-CoreLR-F-SerialControl-0011 — The LCD during a serial job (decided 2026-10-08)
<a id="REQ-CoreLR-F-SerialControl-0011"></a>
*Applies to: serial image only*

The status screen of a serial job shall be the same as that of an SD job; pause, resume and cancel stay available from the
joystick. Modes that move the machine from the LCD (jogs, calibration) are not available while a serial job runs.

**Cancel from the LCD ends in the error state** (reported `Alarm`), not in idle (confirmed by the maintainer, 2026-10-11, P13 of `docs/protocol/SerialProtocol.md`): the host does not know the job was cancelled and goes on streaming, and from idle its next line would start drawing the rest of the file from the middle. The lines in flight are answered `error:9`, and `$X`, Ctrl-X or `SW` acknowledges. A sender that cannot finish its own Abort from `Alarm` (Candle 1.1.9) is left with its *Reset* button (`docs/protocol/SerialProtocol.md` 4.6).

## REQ-CoreLR-F-SerialControl-0012 — Jogging from the PC (decided 2026-10-08, P2/P6; implemented 2026-10-10)
<a id="REQ-CoreLR-F-SerialControl-0012"></a>
*Applies to: serial image only*

The sender's jog buttons shall work: `$J=G21G91X10Y5F600Z0` moves the head by a relative (or absolute) distance, `0x85` stops it with a ramp, and the status report says `Jog` while it moves. A jog is accepted only when the machine is
calibrated and idle (or already jogging: a continuous jog is a chain of small `$J=` lines) -- not during a job, in alarm or while the LCD is in a mode that moves the machine. Candle's `Z` word is accepted and ignored. Design:
`REQ-CoreLR-D-GCodeParser-0015`, `REQ-CoreLR-D-SerialLink-0011`.

## REQ-CoreLR-F-SerialControl-0013 — Machine frame and work offset (specified 2026-10-11, **confirmed by the maintainer 2026-10-12**)
<a id="REQ-CoreLR-F-SerialControl-0013"></a>
*Applies to: serial image only*

Over the link the position has two readings, as in grbl: **MPos**, in the machine frame (origin at the calibration reference point, +Y up the wall, millimetres), and the **work coordinate offset WCO** such that the drawing position (WPos) = MPos - WCO. WCO is the drawing origin of `REQ-CoreLR-F-Calibration-0010` seen from the reference point; it is sent in the status report (first report, every tenth, and whenever it changed) and answered by `$#` as `[G92:x,y,0.000]`. A sender (Candle) then shows the drawing position. Candle's "Restore origin" sends the head's **current** machine and work positions (`G53 G90 G0 X<m> Y<m> Z0`, `G92 X<w> Y<w> Z0`, bench 2026-10-12): it moves nothing and re-labels where the head is; the origin goes back on the reference point with `G53 G0 X0 Y0` then `G92 X0 Y0`. *(The `G53` line Candle sends after an Abort has no `G0`: `docs/protocol/SerialProtocol.md` P16, open.)* Serial variant only.

## REQ-CoreLR-F-SerialControl-0014 — `$H`: return to the drawing origin (decided 2026-10-11, confirmed 2026-10-12)
<a id="REQ-CoreLR-F-SerialControl-0014"></a>
*Applies to: serial image only*

`$H` (Candle's "Home") lifts the pen if it is down, then moves to the **drawing origin**, as the LCD's "Return home" does: the machine has no limit switches to home on. It is accepted like `$J=` (calibrated, idle, LCD not in a moving mode; else error 9 or 8) and answered `ok` when accepted. Serial variant only.

## REQ-CoreLR-F-SerialControl-0015 — The motion mode after a Ctrl-X is `G0` (decided 2026-10-12, P16 (a))
<a id="REQ-CoreLR-F-SerialControl-0015"></a>
*Applies to: serial image only*

After a cancel by Ctrl-X the parser shall be in motion mode `G0`, as grbl's is after a reset, so that a sender's restoration of its position after an Abort (Candle: `G21G53G90X..Y..Z..`, then `G21G92X..Y..Z..`) works: the first line moves the head back to where the sender last saw it, the second puts the work offset back exactly. Without a motion mode the first was refused and the second shifted the drawing origin by the stop distance.
Scope: the Ctrl-X of the link only; the start of an SD job, `M2`/`M30` and the SD variant are unchanged (no motion mode: `REQ-CoreLR-F-GCodeInterpreter-0012`). To be realized by the firmware side (request FW-14 of `docs/protocol/SerialProtocol.md`).

## Open items
- Which free sender is used first on the bench (`docs/protocol/SerialProtocol.md`, P5).
- *(Closed 2026-10-08: the acknowledgment protocol, `-0005`; the error format, `-0006`; the LCD during a serial job,
  `-0011`.)*
