# Functional Requirements — Actuation

Sub-function: **Actuation** — drive the two cable motors (L, R) to reach
commanded cable lengths, respecting their physical step-rate limit, with
a defined stop behaviour.

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06)
See also: [`../Design/REQ-CoreLR-D-Stepper.md`](../design/REQ-CoreLR-D-Stepper.md)

## REQ-CoreLR-F-Actuation-0001 — Motor rate limit
<a id="REQ-CoreLR-F-Actuation-0001"></a>

> **Note 2026-10-06:** the "~1.2 kHz" figure is the unloaded electrical limit and is not the operating limit. Measured on the bench 2026-09-11: **600 steps/s unloaded, 400 steps/s under load**. The design is tuned against 400 (`REQ-CoreLR-D-Motion-0011`, 0017).
The system shall never command a motor (L or R) to change step state
faster than its physical limit (measured ~1.2kHz maximum), and shall
keep the two motors' timing independent of each other.

## REQ-CoreLR-F-Actuation-0002 — Stop behaviour (revised 2026-09-10)
<a id="REQ-CoreLR-F-Actuation-0002"></a>
The system shall support stopping motion, and on stop the motor holds
its last commanded position (holding torque).

**Revised 2026-09-10** (maintainer): V1 does distinguish three
actions, which the original wording deferred past V1. The vocabulary is
the maintainer's:

| Action | Behaviour |
|---|---|
| **halt** (pause) | Ramp the motion down, lift the pen, stay resumable. |
| **resume** | Put the pen back down if it was down, ramp back up. |
| **cancel** | Exactly a halt, except the job is not resumed. |

Two properties are required of all three:

- **The stop is ramped, never abrupt** — see
  `REQ-CoreLR-D-Motion-0012`. Stopping dead, or restarting at full
  speed from standstill, makes the gondola swing and loses steps.
- **The position survives** — no queued work is ever discarded, so the
  machine stays calibrated across a pause or a cancel. This is why
  "cancel" is a ramped stop rather than a queue flush: `Motion` commits
  its position model per *enqueued* waypoint, so discarding entries
  would silently lose up to a queue's worth of travel (~0.85 mm) and
  force a recalibration. Raised by the maintainer, 2026-09-10.

## REQ-CoreLR-F-Actuation-0003 — Smooth stepping (proposed 2026-10-06)
<a id="REQ-CoreLR-F-Actuation-0003"></a>
At constant commanded speed, the interval between two consecutive steps
of **one** motor shall not vary by more than **3 %** from step to step
(target — the figure is a proposal for the maintainer to confirm).

**Why this is a requirement and not a tuning detail.** A 28BYJ-48 driven
with uneven step intervals is accelerated and braked on every step. That
is audible as growl and was reported from the bench as vibration on pure
X and pure Y moves. The L/R adjustment jog, whose step intervals are
exact, is silent — the contrast is what located the cause
(`REQ-CoreLR-D-Motion-0016`, now superseded).

**Measured 2026-10-06 on the prototype: 0.2–0.3 % rms** (`REQ-CoreLR-D-Motion-0018`), against 9–10 % for the v1 generator on the same machine and metric.

**How it is verified.** The figure is a *measured* property, not a
design intention: the host prototype of `REQ-CoreLR-D-Motion-0018`
reports the interval spread over X, Y and 45° moves, and the bench
confirms by ear. Earlier jitter figures (13.5 % → 7.2 %) were simulations
of an ideal waypoint stream that the CPU could not actually produce
(`../docs/bringup/Bringup-Status.md`, 2026-10-06), so they do not count.

Realized by `REQ-CoreLR-D-Stepper-0010`…`-0013` and
`REQ-CoreLR-D-Motion-0018`.

## Open items
- Nothing outstanding on stop behaviour. A hardware emergency-stop
  input, which would be the one case wanting an immediate queue flush
  (and would then have to declare the position lost), is not part of
  V1.
