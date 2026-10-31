# Design Requirements — Pen Control

Module: **PenControl** — servo driving mechanism, and requesting
(not directly executing) pen state changes so they stay synchronized
with motion.

Realizes: [`REQ-CoreLR-F-PenControl-0001`, `-0002`, `-0003`, `-0004`](../functional/REQ-CoreLR-F-PenControl.md)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: **Revised 2026-09-06** — the pen is no longer driven directly
from the GCode parser; see rationale below.

## REQ-CoreLR-D-PenControl-0001 — Servo driving mechanism
<a id="REQ-CoreLR-D-PenControl-0001"></a>

> **Superseded 2026-10-06 (implemented the same day)** by `-0003`: the `Servo` library is replaced by a driver on Timer2.
The pen is driven by a single hobby servo, via the Arduino `Servo`
library (`Servo.h`), attached to a dedicated digital pin. Setting the pen
angle is a direct angle write (`Servo::write(angle)`) using the
calibrated "up" or "down" angle setpoint (`REQ-CoreLR-F-Calibration-0002`).

**Servo pin:** defaults to pin `A0`, matching `Walldraw/Walldraw.ino`'s
`pen.attach(A0)`. See `../../docs/hardware/PinMapping.md`.

## REQ-CoreLR-D-PenControl-0002 — Request, don't act directly (revision)
<a id="REQ-CoreLR-D-PenControl-0002"></a>

> **Hand-off superseded 2026-10-06** by `-0003`. The principle — *request, don't act at parse time* — is unchanged.
**Revision:** the first revision had `PenControl_Up()`/`PenControl_Down()`
write the servo immediately when called. Per
`REQ-CoreLR-F-PenControl-0004`, this is wrong — a GCode `M3`/`M5` line is
parsed well ahead of the corresponding physical motion. Instead:
- `PenControl_RequestUp()`/`PenControl_RequestDown()` (called by
  `CoreLR_GCodeParser`) do **not** touch the servo. They call
  `Stepper_RequestPenOnly(action, settleTicks)`
  (`REQ-CoreLR-D-Stepper-0003`), which repeats the last
  commanded motor coil bits (no motion) for the settling duration
  (`REQ-CoreLR-F-PenControl-0003`) and carries the pen action.
- When the ISR later makes that entry current, it raises a pending-flag
  (`REQ-CoreLR-D-Stepper-0004`) — it does not call `Servo`
  itself (the `Servo` library manages its own timer and should not be
  driven from inside another ISR).
- `PenControl_Task()`, polled from `loop()`, checks the pending flag and
  performs the actual `Servo::write()` — this is where the physical pen
  movement really happens, synchronized to within one `loop()` iteration
  of the exact intended point in the motion sequence.

**Rationale:** this also resolves the earlier open item ("no accessor
for the last commanded motor value") — since motion and pen state now
share one queue, the queue module itself already tracks the last
enqueued coil bits and reuses them for a pen-only entry.

## REQ-CoreLR-D-PenControl-0003 — Software servo on Timer2, no `Servo` library (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-PenControl-0003"></a>
Supersedes `-0001`'s use of the `Servo` library and `-0002`'s flag-and-poll
hand-off. Realizes `REQ-CoreLR-F-PenControl-0001`…`-0004`.

**Why.** The `Servo` library owns Timer1, the only 16-bit timer, which
`REQ-CoreLR-D-Stepper-0011` needs. The pen takes two positions and needs
no precision (maintainer, 2026-10-06), so a minimal driver on the 8-bit
Timer2 is enough — and should recover flash, since the library goes.

**Driver.** `IsrDispatch` runs Timer2 at **1 kHz** (`-IsrDispatch-0003`).
A servo frame is 20 of those ticks; the pulse is `544…2400 µs`, the same
angle-to-pulse mapping as the `Servo` library, so the stored pen angles
keep their meaning:

```
tick 0 of the frame : pen pin HIGH
tick n = floor(pulse/1000) : compare-B arms at (pulse mod 1000)/4 counts -> pin LOW
```

Resolution 4 µs (≈ 0.4° of servo travel), far finer than needed. The pin is
`A0`, as in `-0001`, and is driven with the same compile-time per-wire
macros as the motor pins (`PORTC`, bit 0), not `digitalWrite`.

**Hand-off.** The pen state is **one byte** (`UP`/`DOWN`), written by the
step ISR when it pops an entry carrying a pen action, and read by the
servo frame. A single byte is atomic on AVR, so the pending-action flag,
its critical section and the `PenControl_Task()` poll of
`REQ-CoreLR-D-Stepper-0004` all disappear, and the pen moves at the event
rather than "within one `loop()` pass of it". The two pulse widths are
precomputed in the main loop whenever the calibration angles change, so
the ISR does no arithmetic.

`PenControl_PreviewAngle()` (the wizard) stays: it sets a preview pulse,
recomputed under `cli()`.

**Consequence:** `PenControl` no longer needs C++ and can become a plain
`.c` file, in line with the project's language rule.

### Implementation notes, 2026-10-06
- **Saving, measured** (Nano fqbn): removing `Servo` (its Timer1 vector 376
  bytes, `Servo::write` 212, attach and init) and the flag-and-poll hand-off,
  together with the simplified `Stepper`, took flash from 29050 to **28362
  bytes (−688)** and static RAM from 1713 to **1677 (−36)** — `Servo`'s
  `servos[]`, `ServoCount` and the `Servo` object were 40 bytes of RAM on their
  own. The software servo is ~150 bytes of that.
- **Cost, measured** (`tools/analysis/cyclesim/t_isr2.c`): the Timer2 tick, SysTick and
  the servo frame together, 72–90 cycles (mean 74), **0.46 % of the CPU** at
  1 kHz; the compare-B pulse end 45 cycles, once per 20 ms.
- **Verified on the host** (`tests/host/test_pen.c`, Timer2 simulated count
  by count): the pulse on the pin over 0–180°, the 20 ms frame, the hand-off
  from the step ISR, and a changed calibrated angle reaching the servo through
  `PenControl_Task()`. The pulse is **short by about the tick ISR's latency**
  (the pin is raised in the ISR, the end is hardware-timed): worst error 28 µs,
  i.e. under 3°, with a 16 µs latency. Pulses whose fraction of a millisecond is
  under 48 µs are lengthened by up to that (the 12-count floor, a few angles
  around 45° and 142°), under 5°. Neither matters for a two-position pen.
- `PenControl.cpp` is now `PenControl.c`; `HwConfig.h` states the servo pin as
  the number 14 (`A0`) because the plain-C file includes no Arduino header.
- A wizard preview stays selected until the next real pen action, as with the
  `Servo` library, which held its last written angle.

## Open items
- Exact tuned settling-time value (fixed constant, starting point ~300ms
  per `REQ-CoreLR-F-PenControl-0003`) — refined during hardware
  bring-up.
