# Functional Requirements — Pen Control

Sub-function: **PenControl** — raise/lower the pen at the correct times
in a job, using calibrated servo setpoints.

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06)
See also: [`../Design/REQ-CoreLR-D-PenControl.md`](../design/REQ-CoreLR-D-PenControl.md)

## REQ-CoreLR-F-PenControl-0001 — Pen up/down control
<a id="REQ-CoreLR-F-PenControl-0001"></a>
The system shall raise and lower the pen at the correct points in a job,
using calibrated ON/OFF servo setpoints (`REQ-CoreLR-F-Calibration-0002`).

## REQ-CoreLR-F-PenControl-0002 — Pen setpoint calibration
<a id="REQ-CoreLR-F-PenControl-0002"></a>
The user shall be able to calibrate (jog and persist) the pen's "up" and
"down" servo setpoints, via the menu
(see `REQ-CoreLR-F-UserInterface-0007`).

## REQ-CoreLR-F-PenControl-0003 — Settling time after a pen state change
<a id="REQ-CoreLR-F-PenControl-0003"></a>
After any pen up/down transition, the system shall pause further motion
for a defined settling time before resuming, to let the physically
suspended pen mechanism stop moving from its own inertia before drawing
continues.

**Rationale:** confirmed 2026-09-06, based on an equivalent mechanism
(`TPD`, ~300ms) found in the `Walldraw/Walldraw.ino` reference prototype
— without it, the pen can smear the start of the next stroke while still
settling from the previous up/down move.

**Confirmed 2026-09-06:** the settling time is a **fixed constant**
(starting value ~300ms, as in the reference prototype), not a
user-configurable calibration parameter, for V1 — kept simple, no extra
menu entry. May become configurable later if real-world testing shows
the fixed value doesn't suit different mechanical builds.

## REQ-CoreLR-F-PenControl-0004 — Synchronization with position (confirmed 2026-09-06)
<a id="REQ-CoreLR-F-PenControl-0004"></a>
Pen up/down actions shall execute synchronized to the machine's actual
physical motion — i.e. exactly when the plotter reaches the point in
the motion sequence at which the action was commanded — and **never**
at the moment the GCode line is parsed. Parsing happens ahead of actual
motion (the motion queue buffers tens of milliseconds of pending
movement, see `REQ-CoreLR-D-Stepper`), so acting immediately at
parse time would raise/lower the pen before the plotter has physically
reached the corresponding position.

**Rationale:** without this, drawings would be silently wrong — the pen
would touch down or lift at the wrong physical location, offset by
however much motion was still queued at parse time. Realized by
enqueuing the pen action into the same time-ordered queue as motion
(`REQ-CoreLR-D-Stepper-0001`, `-0004`), rather than acting on it
directly.

## Open items
None beyond the exact tuned settling-time value, refined during
hardware bring-up.
