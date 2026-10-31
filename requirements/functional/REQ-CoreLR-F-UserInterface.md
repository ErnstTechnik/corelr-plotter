# Functional Requirements — User Interface

Sub-function: **UserInterface** — let the user view machine status and
control it (jog, calibrate, load jobs) via LCD + joystick, standalone
(no PC needed).

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06); several details still open (see below and
the linked Design file)
See also: [`../Design/REQ-CoreLR-D-LcdJoystickUI.md`](../design/REQ-CoreLR-D-LcdJoystickUI.md)

## REQ-CoreLR-F-UserInterface-0001 — Standalone status & control
<a id="REQ-CoreLR-F-UserInterface-0001"></a>
The system shall provide a visual status display and a physical input
control, usable without a PC, to view machine state and operate it (jog,
calibrate, load jobs).

## REQ-CoreLR-F-UserInterface-0002 — Default status view
<a id="REQ-CoreLR-F-UserInterface-0002"></a>
By default — at idle and while a job is running — the display shall
show the current status (position, current action). A short press on
the input control opens the main menu.

## REQ-CoreLR-F-UserInterface-0003 — Main menu
<a id="REQ-CoreLR-F-UserInterface-0003"></a>
The system shall expose, via a menu, the following capabilities: manual
displacement (jog), calibrate machine, calibrate pen, load file (SD
browser), and a way back to the status view.
(*Amended 2026-10-10: and, optionally, return to the origin, `-0011`. The way back to the status view is a last entry of the menu, always offered, and the menu gives way to the status view when the machine's state changes under it, e.g. a job driven from the serial link.*)

## REQ-CoreLR-F-UserInterface-0004 — "Control" jog mode
<a id="REQ-CoreLR-F-UserInterface-0004"></a>
Once the machine is calibrated (`REQ-CoreLR-F-Calibration`), the user
shall be able to jog the pen directly in Cartesian X,Y, with input
deflection controlling jog speed proportionally.

## REQ-CoreLR-F-UserInterface-0005 — "Adjustment" jog mode
<a id="REQ-CoreLR-F-UserInterface-0005"></a>
The user shall be able to select and jog each motor (L or R)
independently, to reach the calibration reference position
(`REQ-CoreLR-F-Calibration-0004`) when the machine's position is not yet
known.

## REQ-CoreLR-F-UserInterface-0006 — Jog-mode access gating
<a id="REQ-CoreLR-F-UserInterface-0006"></a>
The "Control" jog mode (`REQ-CoreLR-F-UserInterface-0004`) shall only be
usable once the machine is calibrated; otherwise it is hidden/disabled,
and the user is directed to "Calibrate machine" (Adjustment mode) first.

## REQ-CoreLR-F-UserInterface-0007 — Pen calibration workflow
<a id="REQ-CoreLR-F-UserInterface-0007"></a>
The system shall let the user calibrate the pen's up/down setpoints
(`REQ-CoreLR-F-PenControl-0002`) via the menu.

## REQ-CoreLR-F-UserInterface-0008 — SD file browser
<a id="REQ-CoreLR-F-UserInterface-0008"></a>
*Applies to: sd image only*

The system shall let the user browse files on the SD card — including
navigating into subdirectories and back up to the parent directory — and
select a file to load and run (`REQ-CoreLR-F-JobStorage-0001`). The browser's limits (path length and depth, number of entries, names, speed per step, the card pulled during a navigation) are stated in `REQ-CoreLR-D-SDReader-0011`; the user shall never be left with a frozen browser (`REQ-CoreLR-D-SDReader-0009`).

## REQ-CoreLR-F-UserInterface-0009 — Display freshness must not compete with motion (confirmed 2026-09-09)
<a id="REQ-CoreLR-F-UserInterface-0009"></a>
The display shall reflect a change of machine state within a bounded,
human-imperceptible latency, **without** the act of refreshing it ever
degrading motion quality.

Refreshing must therefore be:
- **rate-limited** — a refresh rate faster than a human can perceive
  buys nothing and costs machine time;
- **change-driven** — nothing is sent to the display when what it would
  show has not changed;
- **incremental** — a single refresh shall not monopolise the main loop
  for long enough to starve the actuation queue
  (`REQ-CoreLR-F-Actuation-0001`).

**Rationale:** the display is driven over I²C, which is *blocking* from
the caller's point of view (verified 2026-09-09: `Wire::endTransmission()`
busy-waits). Measured, a full 32-character redraw costs ~11.5 ms, while
the step queue holds only ~20 ms of motion — so an unconditional redraw
per main-loop iteration consumes over half the queue's margin and can
starve it mid-plot, producing visible stutter in the drawing. Note that
motion *timing* itself is never disturbed (the stepper ISR keeps firing
throughout the blocking wait); what suffers is the main loop's ability
to **refill** the queue. This is a real user-visible quality
requirement, not an optimization.

## REQ-CoreLR-F-UserInterface-0010 — Message during a programmed pause (deferred)
<a id="REQ-CoreLR-F-UserInterface-0010"></a>

When a job pauses itself for a pen change
(`REQ-CoreLR-F-GCodeInterpreter-0008`, deferred), the display shall
tell the user that the machine is waiting on them -- e.g. "change pen
then resume" -- rather than showing only the paused status tag.

Recorded together with that requirement. The open question is what the
message displaces: the status screen's two rows currently carry
position and cable lengths.

## REQ-CoreLR-F-UserInterface-0011 — Return to the origin (2026-10-10)
<a id="REQ-CoreLR-F-UserInterface-0011"></a>

Once the machine is calibrated and no job is running, paused or in error, the user shall be able to send the pen back to the drawing origin (X=0, Y=0) from the menu, without jogging it there by hand: the pen is lifted first,
the move can be stopped at any moment, and the machine stops smoothly when it is. The entry is optional at build time, for the day the flash is needed.

Origin: the maintainer's proposal, 2026-10-10 -- after a drawing, a cancel or a jog the pen is wherever it was left, and the origin is where every job starts. Design: `REQ-CoreLR-D-LcdJoystickUI-0024`.

## Open items
- Analog joystick dead-zone and jog speed-vs-deflection curve —
  deliberately deferred to empirical tuning during hardware bring-up.

All tracked in detail in `../Design/REQ-CoreLR-D-LcdJoystickUI.md`.
