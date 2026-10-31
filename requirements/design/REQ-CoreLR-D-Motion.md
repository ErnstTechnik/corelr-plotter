# Design Requirements — Motion

Module: **Motion** — turns a commanded Cartesian move into a
stream of intermediate waypoints, then into L,R cable-length targets,
then into stepper-queue entries.

Realizes: [`REQ-CoreLR-F-Motion-0001`, `-0002`, `-0003`](../functional/REQ-CoreLR-F-Motion.md),
[`REQ-CoreLR-F-Calibration-0006`](../functional/REQ-CoreLR-F-Calibration.md)
(drawing-origin offset, applied only here)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Implemented. Strategy settled 2026-09-07 (discussed before
implementation, per the project's own governance); the two defects found
by analysis on 2026-09-09 were **fixed 2026-09-10** — the trajectory
generator is now 4-connected (`-0006`'s correction) and the acceleration
unit is re-scaled to nm/tick² (see the open items). Function
decomposition and cross-segment look-ahead remain open by design.

## Two coordinate frames — why, and where the boundary is
*Drawn, with the dimensions and the calibration, in [`../../docs/hardware/images/Geometry.svg`](../../docs/hardware/images/Geometry.svg) (`REQ-CoreLR-F-Calibration-0008`).*


Confirmed 2026-09-07: the machine has two distinct X,Y frames, and
**exactly one function** (`Motion_XYToLR`, `-0005`) is allowed
to know both of them exist.

- **Internal kinematics frame** — origin at Motor L's pivot, X toward
  Motor R, Y downward (X right, Y down; the cables hang from the motors, so `+Y` means longer cables).
  Purely a mathematical convenience (`L = distance to origin`). **Not
  mechanically reachable at its own origin** (`L=0` is impossible) and
  **never shown to the user or referenced by GCode.**
- **Drawing (user/GCode-facing) frame** — what GCode coordinates, the
  status screen, and every other module use. Related to the internal
  frame by the drawing-origin offset (`REQ-CoreLR-F-Calibration-0006`):
  `X_internal = X_drawing + WorkOriginOffsetX`, `Y_internal = WorkOriginOffsetY − Y_drawing` (**Y is flipped**: the drawing frame has `+Y`
  **up** the wall, `REQ-CoreLR-D-Motion-0025`).

Every other function in this module (and every other module) works
purely in the **drawing frame** — `Motion_Position_t`, GCode targets,
`Motion_GetComputedPosition()`'s return value, are all
drawing-frame. The offset is added exactly once, inside
`Motion_XYToLR`, and nowhere else.

## Pipeline overview

```
GCode (target X,Y, drawing frame)
   |
Cartesian planner (accel/decel/cruise, drawing frame) -- REQ-CoreLR-D-Motion-0009
   |
Trajectory generator (integer Bresenham, ~54um grid, drawing frame) -- REQ-CoreLR-D-Motion-0006
   |
Inverse transform: + drawing-origin offset -> internal frame -> isqrt -> (Li,Ri) -- REQ-CoreLR-D-Motion-0005
   |
Step quantization: (Li,Ri) um -> absolute step counts -> per-motor coil bits via phase table -- REQ-CoreLR-D-Motion-0007
   |
Stepper_RequestMotion() -- one combined entry per waypoint
   |
Job queue -> 4kHz ISR -> motor pins
```

## REQ-CoreLR-D-Motion-0001 — Internal coordinate resolution
<a id="REQ-CoreLR-D-Motion-0001"></a>
Internal Cartesian positions (X, Y) shall be stored as signed 32-bit
integers, in **micrometre (µm)** units. No floating-point type shall be
used to store position state.

**Rationale:** a `int32_t` gives a range of ±2147 m, far beyond the
physical work area, while avoiding rounding-error accumulation that a
floating-point representation would introduce over long jobs.

## REQ-CoreLR-D-Motion-0002 — Waypoint-based motion pipeline
<a id="REQ-CoreLR-D-Motion-0002"></a>
The motion pipeline shall generate an explicit stream of intermediate
Cartesian waypoints `(Xi, Yi)`, drawing frame, between the current
position and the commanded target. Timing of each waypoint is derived
from the Cartesian acceleration/deceleration profile. Each waypoint is
then converted **independently**, as an absolute position, into L,R
cable-length targets — never by accumulating per-step deltas.

**Rationale:** converting each waypoint from an absolute position (rather
than integrating deltas) prevents numerical drift — realizes
`REQ-CoreLR-F-Motion-0002`.

## REQ-CoreLR-D-Motion-0003 — Integer trajectory generation algorithm
<a id="REQ-CoreLR-D-Motion-0003"></a>

> **Arc half superseded 2026-10-07** by `-0021`…`-0023`: not a midpoint-circle algorithm but an absolute computation of each waypoint from the circle's definition. The line half was superseded earlier, by `-0018`/`-0019`.
Waypoint generation for straight segments and circular arcs shall use
integer-only incremental algorithms: Bresenham/DDA for line segments, and
a midpoint-circle-style algorithm for arcs. No floating-point arithmetic
shall be used in waypoint generation.

## REQ-CoreLR-D-Motion-0004 — Absolute position recalculation
<a id="REQ-CoreLR-D-Motion-0004"></a>
For any motion command, including curved paths (e.g. arcs) built from
relative steps, the algorithm shall recompute the true absolute target
position at each step and drive towards that absolute target from the
actual current position, rather than accumulating relative deltas.

## REQ-CoreLR-D-Motion-0005 — Inverse kinematics
<a id="REQ-CoreLR-D-Motion-0005"></a>
`Motion_XYToLR()` is the **only** function that knows the
drawing-origin offset exists. Given a drawing-frame position:
```
X_internal = X_drawing + WorkOriginOffsetX
Y_internal = WorkOriginOffsetY - Y_drawing        (the drawing frame has +Y UP the wall: -0025)
L = isqrt(X_internal^2 + Y_internal^2)
R = isqrt((X_internal - MotorDistanceUm)^2 + Y_internal^2)
```
`isqrt()` is an integer square root (no float): the classic bit-by-bit
digit algorithm (shifts, compares, subtracts only — no division). Inputs
are squared in `int64_t`/`uint64_t` before the root, since
`(coordinate)^2` can exceed 32 bits for a multi-metre work area (e.g.
3m ≈ 3,000,000 µm, squared ≈ 9×10^12).

**Shared with `Calibration`:** the reference-position geometry
(`REQ-CoreLR-D-Calibration-0001`) uses the same `isqrt` — exposed
from this module rather than duplicated.

## REQ-CoreLR-D-Motion-0006 — Bresenham grid resolution
<a id="REQ-CoreLR-D-Motion-0006"></a>

> **Superseded 2026-10-06, implemented (the Bresenham generator is gone)** by `-0018`/`-0019`: coarse waypoints exactly on the line; the one-step-per-waypoint invariant that forced the 4-connected staircase no longer applies. Kept for the history of why it existed.
The integer Bresenham/DDA trajectory generator (`-0003`) operates on a
grid sized to the physical motor step resolution
(`≈ MOTION_SPOOL_DIAMETER_UM × π / (MOTION_STEPS_PER_REV_X1000 / 1000) ≈ 54µm`),
not on raw 1µm units.

**Rationale:** at 1µm/step, a 1-metre line would generate ~1,000,000
waypoints — impractical. At the motor's own step resolution, a given
grid step corresponds to at most one physical step of L and/or R,
meaning each Bresenham waypoint maps to exactly one combined stepper
queue entry — no need to merge two independently-paced interpolation
streams (an earlier, more complex approach considered and rejected).

### Correction 2026-09-09, applied 2026-09-10 — the generator is 4-connected (one axis per iteration)
The "at most one physical step" property above holds **only for an
axis-aligned grid step**. The generator was originally *8-connected*: a
single `Motion_Task()` iteration could advance X **and** Y (the classic
diagonal Bresenham step), which broke the invariant this whole design
rests on:

| Grid step | Cartesian travel | In motor steps |
|---|---|---|
| Axial (dX **or** dY) | 53 µm | 0.99 — invariant holds |
| **Diagonal (dX and dY)** | **75 µm** | **1.40 — invariant violated** |

Measured consequence: with a cable-length change of up to 1.40 steps in
one iteration, the rounded absolute step count can differ by **2**
between consecutive waypoints. But `Stepper_RequestMotion()` transmits a
**single** coil-bit state per entry — commanding a phase two positions
ahead is something a stepper physically cannot follow, so the motor
loses steps. That is a genuine position error, and precisely the failure
mode the absolute-recomputation design (`-0004`) exists to prevent: the
*commanded* position stays correct while the *physical* one silently
lags.

**The generator therefore advances exactly one axis per iteration**
(4-connected):

```
   Line at 45 deg, (0,0) -> (3,3), grid = 53um

   8-connected (was)                 4-connected (now)

   3 |. . . X                        3 |. . X X
   2 |. . X .                        2 |. X X .
   1 |. X . .                        1 |X X . .
   0 |X . . .                        0 |X . . .
     +--------                         +--------
      0 1 2 3                           0 1 2 3

   3 iterations                      6 iterations
   each: dX AND dY                   each: dX OR dY
   travel = 53*sqrt(2) = 75um        travel = 53um
   = 1.40 motor steps   VIOLATES     = 0.99 motor steps   OK
   -> may need 2 steps in one        -> never more than 1 step
      queue entry (impossible)          per queue entry
```

Each `Motion_Task()` iteration produces exactly **one** queue entry, and
one entry carries exactly **one** coil-bit state per motor — hence at
most one physical step. That is the whole reason the axes must be
separated: it is a throughput constraint of the queue entry, not a
geometric preference.

The digital line is unchanged — 4-connected Bresenham traces the same
line, it merely visits the intermediate points. The staircase is 53 µm,
below the machine's own step resolution, so it is not observable in the
drawing.

**Implementation note (2026-09-10).** The update is
`if (Err > 0) { Err -= 2*AbsDy; X += dirX; } else { Err += 2*AbsDx; Y += dirY; }`
with `Err` starting at `AbsDx - AbsDy`, so the segment takes exactly
`AbsDx + AbsDy` iterations (`TotalSteps` changed from
`max(AbsDx, AbsDy)` accordingly — it also feeds the velocity profile).
A naive `if / else if` on the original 8-connected conditions was tried
first and **rejected after hand-tracing**: it yields the same step count
but gives the X test fixed priority, so a shallow line runs straight
along X and takes all its Y steps at the end. Measured maximum deviation
from the ideal line, dx=4 dy=1: **0.97 grid units that way versus 0.49**
with the form above.

### Alternative considered and rejected: shrink the grid to one step / sqrt(2)
Raised by the maintainer 2026-09-09: keep the 8-connected generator
and set the grid to `53/sqrt(2) = 37.5 µm`, so that a *diagonal* step
covers `37.5*sqrt(2) = 53 µm = 0.99` motor steps. **This also restores
the invariant** and is a legitimate solution.

It was rejected on a measured basis, and notably *not* on iteration
count — the two options are exactly equal there:

| | Axial line | 45° line | **Mean over all angles** |
|---|---|---|---|
| 4-connected, 53 µm grid | 18.9 it. | 26.7 it. | **24.02** |
| 8-connected, 37.5 µm grid | 26.7 it. | 18.9 it. | **24.02** |

(4-connected costs `(cos+sin)/g` iterations per unit length,
8-connected costs `max(cos,sin)/g'`; with `g' = g/sqrt(2)` the means
coincide to 0.0%. Each option is simply optimal in the directions where
the other is worst.)

The deciding criterion is **motor steps produced per queue entry**:

| | Travel | Motor steps per entry |
|---|---|---|
| 4-connected, axial *and* diagonal | 53 µm | **0.99** |
| 8-connected, diagonal | 53 µm | 0.99 |
| **8-connected, axial** | 37.5 µm | **0.70** |

With the finer grid, an axial move advances only 0.70 motor steps per
entry, so roughly **30% of queue entries command no step change at
all** — consuming a queue slot for no motion.

> **Correction, same day.** The maintainer then pointed out that
> adjacent entries with identical coil bits should be **merged** into
> one entry of summed hold time — now specified as
> [`REQ-CoreLR-D-Stepper-0009`](REQ-CoreLR-D-Stepper.md). That
> mechanism eliminates exactly the entries this argument counted, so
> **the 30%-waste objection above no longer holds.** Measured after
> merging, neither grid choice dominates: entry counts mirror each
> other by angle (at 0°, 1081 for 4-connected vs 1274 for the finer
> grid; at 45°, 1443 vs 1109), and iteration counts were already equal
> on average. Recorded rather than deleted, because the *reasoning* is
> what changed, not the conclusion.

The rejection therefore rests on the remaining grounds, which are
qualitative but unaffected:
- the finer grid needs a rational approximation of `sqrt(2)` inside the
  grid macro, introducing an approximation error to reason about;
- it loses the exact statement "grid = one motor step" that this
  requirement rests on, replacing it with "grid = one motor step /
  sqrt(2)" — correct, but no longer self-evident, and the one-step
  invariant is what makes the coil-phase scheme obviously safe.

Note also that merging is worthwhile **independently** of this choice:
even at exactly one motor step per grid unit, 4.5–12% of waypoints
(depending on line angle) change neither motor's step count, purely
through rounding.

## REQ-CoreLR-D-Motion-0007 — Step quantization and coil phase table
<a id="REQ-CoreLR-D-Motion-0007"></a>
For each waypoint's `(L, R)` (µm, from `-0005`), the target **absolute**
step count per motor is `round(length_um / CORELR_UM_PER_STEP)` —
computed fresh from the absolute length each time, **never** by
accumulating a fractional remainder across waypoints (that would
reintroduce the exact drift `-0002`/`-0004` are designed to prevent).

The delta between consecutive waypoints' step counts (typically 0 or
±1, given the grid resolution in `-0006`) advances or retreats each
motor's own step-phase index, which is looked up in a 4-entry
full-step coil table — validated against the physical hardware via
`Walldraw/Lib/libraries/TinyStepper_28BYJ_48/src/TinyStepper_28BYJ_48.cpp`
(confirmed 2026-09-07 this is a 4-phase full-step drive, matching
`MOTION_STEPS_PER_REV_X1000 = 2037886`: the 28BYJ-48's output shaft needs 2037.886 full steps a turn, not 2048 -- its gear train is 63.68395:1, not 64:1; corrected 2026-10-26, `HwConfig.h`):

| Phase | Wire0 | Wire1 | Wire2 | Wire3 | Bits |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 1 | 1 | `0xC` |
| 1 | 0 | 1 | 1 | 0 | `0x6` |
| 2 | 1 | 1 | 0 | 0 | `0x3` |
| 3 | 1 | 0 | 0 | 1 | `0x9` |

Each motor keeps its own phase index (0-3), advanced/retreated by the
step delta (direction per `STEPPER_MOTOR_L_REVERSED`/`_R_REVERSED`,
`REQ-CoreLR-F-Calibration-0001`), wrapping modulo 4.

## REQ-CoreLR-D-Motion-0008 — Incremental execution (Request+Task)
<a id="REQ-CoreLR-D-Motion-0008"></a>
A line can require far more waypoints (up to ~thousands, `-0006`) than
`Stepper_Queue_t` has entries (128, `REQ-CoreLR-D-Stepper`
open items). `Motion_PlanLinearMove()`/`PlanArcMove()`
(single blocking calls) are replaced by:
```c
void Motion_RequestLinearMove(Motion_Position_t target);
uint8_t Motion_QueueArc(const Motion_Position_t *target, int32_t i, int32_t j, uint8_t clockwise);   /* -0022; replaced the RequestArcMove stub; pointer since 2026-10-08 */
uint8_t Motion_Task(void); /* returns 1 once the current request is fully enqueued (idle) */
```
`Motion_Task()` is called every `loop()` iteration
(same pattern as `PenControl_Task`, confirmed 2026-09-07 for
consistency): it enqueues waypoints only while
`Stepper_RequestMotion()` succeeds, and simply returns (to be
resumed on the next call) once the queue is full — keeping the LCD,
joystick, and stop handling responsive during a long segment.

**Consequence:** `CoreLR_GCodeParser` must not assume a move completes
synchronously — it should only pull/parse the next GCode line once
`Motion_Task()` reports idle.

## REQ-CoreLR-D-Motion-0009 — V1 acceleration profile (simplified)
<a id="REQ-CoreLR-D-Motion-0009"></a>
For V1, confirmed 2026-09-07: each segment starts and ends at
(near-)zero velocity — no cross-segment look-ahead / cornering-speed
optimization (unlike grbl's full planner, which blends consecutive
segments' junction velocities). Each waypoint's `HoldTicks` comes from
the instantaneous velocity of a simple trapezoidal accel→cruise→decel
profile local to the current segment only.

**Rationale:** significantly simpler to implement and test first;
accepted trade-off is visible jerk at direction changes, improvable
later with a real look-ahead planner without changing the pipeline
architecture above it.

## REQ-CoreLR-D-Motion-0010 — Comments and diagrams shall trace the mathematical chain (confirmed 2026-09-09)
<a id="REQ-CoreLR-D-Motion-0010"></a>
Independently of how the code is eventually split into functions, the
comments and ASCII-art diagrams in this module shall make the
**mathematical path** followed by a commanded target explicit and
traceable, stage by stage:

```
   GCode target (X,Y)            drawing frame, signed um
        |
        |  (1) round to Bresenham grid  (~53um, = one motor step)
        v
   grid step (dX,dY)             integer, drawing frame
        |
        |  (2) Bresenham interpolation -> next waypoint, in CARTESIAN space
        v
   waypoint (X,Y)                drawing frame
        |
        |  (3) + work-origin offset -> internal kinematics frame
        v
   internal (X',Y')              origin at Motor L
        |
        |  (4) inverse kinematics: L = sqrt(X'^2 + Y'^2)
        |                          R = sqrt((X'-D)^2 + Y'^2)
        |      via integer sqrt, no floating point
        v
   cable lengths (L,R)           absolute um
        |
        |  (5) quantize to absolute step counts (spool circumference)
        v
   step counts (nL,nR)           absolute, never accumulated
        |
        |  (6) coil phase = step count mod 4  -> 4-phase table
        v
   coil bits -> Stepper_RequestMotion()
```

**Why this is a requirement and not just good practice:** the two
coordinate spaces are the single most confusing thing about this
machine. Interpolation happens in **Cartesian** space (a straight line
must look straight on the wall), while the physical steps happen in
**cable-length** space, and the nonlinear transform sits *between* them,
applied per waypoint. A reader — including a future us — who loses track
of which space a given variable is in will introduce exactly the class
of bug this design exists to avoid (mixing spaces, or accumulating
deltas across the transform). Every stage above shall therefore state
its **frame** and its **unit** where the value is computed.

The function *decomposition* is a separate question, deliberately left
to this module's review (see open items) — this requirement constrains
the documentation, which must hold whatever the split turns out to be.

## REQ-CoreLR-D-Motion-0011 — Acceleration planned in Cartesian space, cable rate guaranteed by construction (confirmed 2026-09-09)
<a id="REQ-CoreLR-D-Motion-0011"></a>
Acceleration and velocity shall be planned in **Cartesian (X,Y) space**,
as grbl does, and converted to cable lengths only at the last stage of
the chain (`-0010`, stage 4). The per-motor step-rate limit
(`REQ-CoreLR-F-Actuation-0001`, ≤1 kHz) shall be guaranteed by a
**single cap on the Cartesian velocity**, not by any per-motor or
per-direction computation.

**Why one Cartesian cap suffices — the identity that makes this work.**
With motor L at the internal origin, `L = sqrt(X'^2 + Y'^2)`, so:

```
   dL/dt = (X'.X' + Y'.Y') / L = v . cos(theta)

   theta = angle between the velocity vector and the cable direction

        cable
      direction
          \  theta
           \ /
            o------>  v          dL/dt = v.cos(theta)
           pen
```

Therefore **|dL/dt| <= |v| always**: radial motion (theta = 0) gives the
worst case `dL/dt = v`, tangential motion (theta = 90 deg) gives zero.
The same holds for R. So capping the *Cartesian* speed at a value whose
equivalent cable step rate is under 1 kHz makes **both** motors
compliant unconditionally — no runtime clamp, no per-motor check, no
direction-dependent logic. This is the velocity-domain twin of the
triangle-inequality argument already used to size the grid in `-0006`.

**The cap.** One motor step is
`spool_circumference / steps_per_rev = 53.69 um`, so 1 kHz corresponds
to a cable speed of 54.0 mm/s = **13.5 um/tick** at 4 kHz. Hence
`MOTION_VMAX_UM_PER_TICK <= 13`. The value of 10 (40 mm/s)
left ~25% margin and was kept in September 2026.
*Update 2026-10-12: `MOTION_VMAX_V16` is 86 (21.5 mm/s = 5.4 um/tick) since 2026-09-11: the motors hold 400 steps/s under load, so the mechanics bind first and the
electrical cap above has a 60 % margin. The argument is unchanged; the 40 mm/s figures in this file that were measured with the old value are marked.*

**Precondition.** This guarantee assumes a constant Cartesian travel per
iteration, which requires the 4-connected generator mandated in
`-0006`'s 2026-09-09 correction. Measured with the current 8-connected
generator: a diagonal step covers 75 um in the same hold time computed
for 53 um, so a ~45 deg line runs 1.41x faster than commanded and the
cable step rate reaches **1117 steps/s — over the 1 kHz limit**. A
secondary effect worth noting even aside from the limit: stroke speed
becomes direction-dependent, which for a pen means visibly uneven ink
deposition between axial and diagonal strokes.

**Distances used by the ramp** shall likewise be true Cartesian
distances. With `-0006`'s correction this is simply
`steps x MOTION_GRID_UM`, as the code already assumes; the assumption
is only valid once every step is axial.

## REQ-CoreLR-D-Motion-0012 — Ramped halt and resume (implemented 2026-09-10)
<a id="REQ-CoreLR-D-Motion-0012"></a>

`Motion_Halt()` shall bring the move in progress to rest along a
**deceleration ramp**, and `Motion_Resume()` shall accelerate away from
standstill along the normal ramp. Neither shall discard queued work.

### Why a ramp is required, not optional

The trapezoidal profile is a function of *position within the segment*
only (`ComputeHoldTicks()`). Halting by merely refusing new requests --
the obvious implementation -- would therefore drain the queue at
whatever speed was already commanded and then **stop dead**, and
resuming would refill the queue at mid-segment speed **from
standstill**. Both bypass the ramp entirely. On a cable plotter that
makes the gondola swing and loses steps, in both directions. Raised by
the maintainer, 2026-09-10.

### Mechanism: bend the existing profile, do not add a second one

Two fields on the segment:

| Field | Meaning |
|---|---|
| `RampStartStep` | the acceleration reference. 0 normally; set to the resume point so speed rises from `VMIN` again instead of jumping back to mid-segment speed. |
| `HaltStopStep` | a **virtual** segment end, or -1 for none. The deceleration term targets whichever end is nearer, so `v² = 2·a·d` produces the pause ramp with no new formula. |

`Motion_Halt()` sizes the ramp from the current speed by the same
relation, `d = v²/(2a)`, clamped to the real segment end (near the end
of a segment the machine is already slow, so the ramp is naturally
short and usually fits). `Motion_IsHaltRampDone()` reports when the
ramp has finished *enqueuing* -- the moment to request the pen lift, so
that the pen entry lands behind the ramp's last motion entry and the
servo moves once the machine is actually at rest. No polling for an
empty queue is needed.

### Measured cost of a halt

| | |
|---|---|
| Ramp from `VMAX` | 78 grid steps = **4.1 mm**, ~210 ms |
| Work already queued at halt time | 16 entries = **0.85 mm**, 21 ms at `VMAX` |
| **Total travel, halt to standstill** | **~5.0 mm, ~230 ms** |

*(Measured in September 2026 at `VMAX` 40 mm/s. At the present 21.5 mm/s and 192 mm/s² the ramp is about 1.2 mm; `test_motion` section 6 prints the current figure: 37 entries, 107 ms.)*

Already-queued entries keep their pre-halt speeds and run first, then
the ramp plays out, so velocity is continuous across the transition.
The queue is 17% of the stopping distance; the ramp dominates.

### Verified

Host-compiled test of the integer arithmetic (`ComputeHoldTicks()` and
the ramp sizing, copied verbatim): acceleration from `VMIN` reaches
`VMAX` in ~120 steps and never decreases; a halt at full speed yields a
78-step ramp that decreases monotonically to `VMIN`; resume restarts at
`VMIN` and rises again; and the stop point never exceeds the segment
end, including a forced-clamp case (40-step segment, halt at full
speed, ramp clamped from step 88 to step 40).

## REQ-CoreLR-D-Motion-0013 — Junction look-ahead (implemented 2026-09-10)
<a id="REQ-CoreLR-D-Motion-0013"></a>

Job moves shall be planned together rather than executed one at a
time, so that each junction is traversed at the highest speed the
corner and the remaining braking distance allow.

### Why, measured

The profile is a function of position within one segment, so every
segment started and finished at `VMIN`. Two collinear moves stopped in
between, and -- the part that made this urgent -- a drawing made of
short chords never came near `VMAX`:

*Measured in September 2026 with `VMAX` = 40 mm/s (it has been 21.5 mm/s since 2026-09-11) and depth 7:*

| Segment length | Without look-ahead | Depth 7, `VMAX` 40 |
|---|---|---|
| 0.2 mm | 5.0 mm/s | 23 mm/s |
| 0.5 mm | 4.7 mm/s | 36 mm/s |
| 1 mm | 6.3 mm/s | 40 mm/s (`VMAX`) |
| 8.4 mm | 19.2 mm/s | 40 mm/s |

**Measured again 2026-10-12** (`tests/host/test_chain.c`: 100 mm of equal segments, with the machine's back-pressure -- a 32-entry step queue drained in real time, 0.19 ms per generator call, a line every 0.6 ms), `VMAX` 21.5 mm/s:

| Segment length | 0.2 mm | 0.5 mm | 1 mm | 2 mm | 5 mm | 20 mm |
|---|---|---|---|---|---|---|
| depth 8 (serial) | 20.7 | 20.7 | 20.8 | 21.1 | 21.1 | 21.1 mm/s |
| depth 4 (SD until 2026-10-13) | 15.4 | 20.7 | 20.8 | 21.1 | 21.1 | 21.1 mm/s |

The speed is `VMAX` less the ramps at the two ends (1.2 mm each). The horizon needed is `VMAX²/(2a)` = 1.2 mm: 6 blocks of 0.2 mm, 3 of 0.5 mm, 2 of 1 mm, which is why depth 8 holds the speed down to 0.2 mm and depth 4 did from 0.5 mm.
**Since 2026-10-13 both variants have depth 8**: on SD it costs +84 bytes of RAM (1 340 -> 1 424) and +4 bytes of flash (29 782), the worst stack (334, the SD browser) is unchanged, the RAM-minus-stack margin goes from 374 to 290 bytes.
(An earlier attempt of the same measurement in `test_motion` printed 6 mm/s for 0.2 mm: its stub accepted every entry at once, so the planner never held more than one block -- no back-pressure, no look-ahead to measure.)

An SVG-derived GCode file is a dense polyline of 0.2-1 mm segments, so
this was costing 5-8x on real files, on plain `G1` with no arcs
involved. It also had to come *before* arcs, which decompose into short
chords and would otherwise have been written against a velocity model
about to change (maintainer, 2026-09-10).

### Mechanism

A ring buffer of `MOTION_PLANNER_DEPTH` moves holds absolute targets
only -- 9 bytes each. Each block's length and direction are derived
from its predecessor's target; the first block's predecessor is the
move being executed, or the current position when idle.

One **reverse pass** (`Replan()`), from the newest block back, gives
each entry speed the lowest of: the corner it turns through, `VMAX`,
and what can still be shed before the following block's entry speed.
That third ceiling is why the pass runs backwards -- a sharp corner far
ahead propagates its low speed back through as many earlier blocks as
braking requires. Verified on the host: four collinear 2 mm blocks
followed by a reversal plan as `10 10 9 7 1`.

No forward pass is needed: `ComputeHoldTicks()` already takes the
minimum of the acceleration and deceleration terms at every step, so a
block that cannot reach its planned entry speed is clamped as it
executes.

The profile formula **generalises** rather than being replaced -- the
`vEnd²` terms were previously implicit zeros:

```
   accel term:  v = sqrt(EntryV² + 2·a·distanceFromStart)
   decel term:  v = sqrt(ExitV²  + 2·a·distanceToEnd)
```

`RampStartStep`/`HaltStopStep` from `REQ-CoreLR-D-Motion-0012` continue
to work on top; a halt forces the deceleration target back to rest,
overriding the planned junction speed.

### Cornering model: axis jerk, not junction deviation

Each direction is a fixed-point vector normalised by its **Manhattan**
length, so `|ux| + |uy| = 256`. That is the natural normalisation here,
not a nicety: `ux/256` is exactly the fraction of grid steps that
advance X, so `v·ux/256` is the pen's X velocity component. The
junction is limited by requiring neither component to jump by more than
`MOTION_JUNCTION_JERK_UM_PER_TICK`.

Chosen over grbl's junction-deviation formula, which needs
trigonometry and divides by a quantity that is zero exactly at the
collinear case that matters most here. The jerk model handles both
degenerate cases with no special-casing: collinear gives a zero
denominator hence no limit at all, and a reversal gives the maximal
denominator hence essentially a stop.

Measured, with `VMAX` = 10 and jerk = 2: collinear 10, 45° 4, 90° 2,
reversal 1. *(With the present units -- `VMAX` 86, jerk 32, `VMIN` 16, in 0.25 mm/s: collinear 21.5 mm/s, 45° 16, 90° 8, reversal 4.)*

### Defect found by the host test, and fixed

The corner limit was first applied to the entry speed of the block
*before* the corner. It belongs on the joint, which is one block's exit
**and** the next block's entry -- an off-by-one that let a right angle,
and even a full reversal, be taken at `VMAX`. Caught only because the
test asserted the physics rather than the code's own output.

### Cost, and the horizon it buys

Flash ~1.7 kB (`Replan` 786 B plus four helpers). RAM 68 B: 63 for
seven blocks, 3 for the ring state, 2 for the segment's entry/exit
speeds. Ring indices wrap by comparison rather than `%`, since the
depth is not a power of two and the modulo would be a real 16-bit
division inside the loop.

Sustaining `VMAX` requires the buffer to span `VMAX²/(2a)` = 4.2 mm at the
then `VMAX` of 40 mm/s: 21 blocks at 0.2 mm, 9 at 0.5 mm, 5 at 1 mm. Depth 7 was what the RAM
budget allowed. **With the present 21.5 mm/s the horizon is 1.2 mm: 6 blocks at 0.2 mm, 3 at 0.5 mm, 2 at 1 mm (depth 8 in both variants since 2026-10-13, 4 on SD before: see the table of 2026-10-12 above).** **This is a RAM ceiling, not an algorithmic one** --
raising the depth is the single most effective speed lever available,
and the natural source is `SDREADER_QUEUE_SIZE`, which spends 48 bytes
a slot on *raw text* where a block holds an already-parsed move.

## REQ-CoreLR-D-Motion-0014 — `Motion_Task()` keeps the step queue deep (implemented 2026-09-11)
<a id="REQ-CoreLR-D-Motion-0014"></a>

> **Superseded 2026-10-06, implemented.** The feeding policy stays — fill the queue to `MOTION_QUEUE_TARGET` entries, bounded per call — but a call now emits step *events*, and the target is stated in entries of ≥ 1.25 ms each (20 ms ahead at full speed, far more when slower).

`Motion_Task()` shall emit steps until the step queue holds
`MOTION_QUEUE_TARGET` entries, the active segment reports idle, or
`MOTION_STEPS_PER_TASK_MAX` steps have been emitted in the call —
whichever comes first.

### The defect

The previous implementation emitted **at most one queue entry per
call**. The achievable step rate was therefore capped by how often
`loop()` happened to run, and any single slow pass left the ISR with
nothing to pop. `Stepper_Tick()` then re-applies the same coil bits on
every tick: the motor stays energised on one phase with the rotor short
of its detent and rings against it.

This is the same defect that was found first in the manual jog
(`REQ-CoreLR-D-LcdJoystickUI-0016`), where it was misread as the motor
losing torque above ~180 steps/s. It was never a motor limit: the
maintainer's reference sketch, which busy-waits on `micros()` and so
cannot starve, drove the same motor on the same wiring at 666 steps/s
(19.5 rev/min at the output shaft) smoothly. The datasheet-derived
"≈15 rev/min" ceiling that had been offered as an explanation was a
catalogue figure standing in for a measurement, and the measurement
refuted it.

### Why plotting fills the queue while the jog meters it

The two consumers reach the queue independently and take deliberately
opposite policies (maintainer, 2026-09-11):

| | manual jog | plotting |
|---|---|---|
| target | a **duration** (~30 ms) | as deep as the ring allows |
| why | a released stick must stop promptly, and queued entries are the overshoot | there is no stick; a halt ramps down *through* the planner, so the queued entries **are** the ramp |

Queued work is never regretted work on the plotting side, which is what
makes the maximal policy safe here and wrong in the jog.

### Bounds

`MOTION_STEPS_PER_TASK_MAX` (8) caps the worst case spent inside one
call so the UI keeps its own cadence. At loop()'s several hundred hertz
that is far more throughput than `MOTION_VMAX_UM_PER_TICK` can ask for,
so the cap never binds in normal operation — it exists to bound the
pass, not to meter the motion.

The loop exits as soon as the segment reports idle, so a finished or
paused move still costs exactly one call.

Measured cost: +112 bytes of flash, no RAM, and 5 bytes of stack for the
wrapper frame (288 bytes worst case for `main()`, +12 for the ISR,
against 350 free — margin +50).

## REQ-CoreLR-D-Motion-0015 — Hysteresis on step quantization (implemented 2026-09-11)
<a id="REQ-CoreLR-D-Motion-0015"></a>

> **Under review 2026-10-06.** maintainer expectation: the dead band becomes unnecessary, because the sawtooth it filters was a product of the 4-connected staircase, which `-0019` removes. **Not removed on an expectation:** the host prototype shall show zero reversals with `H = 0` over 10 mm of travel tangential to a motor (the case that gave 181 reversals in v1); only then is the band deleted. The `H` of `-0018` is that band, and is 0 if the check passes.

The conversion from cable length to commanded step count shall apply a
dead band of `MOTION_HYST_SUBSTEPS` (one whole step) and shall move the
commanded count by **at most one step per waypoint per motor**.

### Defect 1 — the Cartesian staircase makes a stationary cable chatter

The 4-connected generator (`REQ-CoreLR-D-Motion-0006`) advances one axis
per iteration. On a path tangential to one motor — an arc centred on
that pulley — a pure-X step and a pure-Y step move that cable in
**opposite directions** by nearly equal amounts. Its length therefore
sawtooths around a constant value, and round-to-nearest turns the
sawtooth into a stream of forward/backward steps.

Simulated on the real algorithm, 10 mm of tangential travel:

| band | steps emitted | reversals | worst tracking error |
|---|---|---|---|
| 0.5 (round-to-nearest) | 95 fwd + 94 back | 181 | 26 µm |
| **1.0 (adopted)** | 8 fwd + 6 back | 12 | 52 µm |
| 1.5 | 1 fwd + 0 back | 0 | 79 µm |

Net displacement in every case is one step — the motor was being asked
to take 189 steps to move one. It cannot follow reversals at that rate
and simply buzzes, which is how this was first noticed on the bench: the
pulley held its position while its phase LEDs flickered rapidly.

An ordinary 45° line is charged exactly one step out of 185 for the
band, and a radial line nothing at all. The band is not a compromise on
normal work.

`grbl` never meets this because its axes are linear and independent: a
step on X moves X and nothing else. Here the transform is nonlinear and
shared, so a Cartesian staircase is not a cable-space staircase.

### Defect 2 — a two-phase jump, which predates this change

A single waypoint can legitimately move a cable **further than one
step**. The last waypoint of a segment snaps to the exact target rather
than to a grid point (`REQ-CoreLR-D-Motion-0004`), and that jump reaches
**2.01 steps** in simulation.

The code this replaces had no guard, so it emitted a queue entry
advancing the coil table by two phases at once at the end of **10.8 % of
segments**. In a 4-phase table two phases is 180 electrical degrees: the
direction is ambiguous, and the motor may turn either way or merely
buzz. This was a latent defect, found by the maintainer asking
whether the new hysteresis could produce a two-step move — the question
turned out to describe the existing code.

Hence `if`, deliberately not `while`. Falling behind is safe where
jumping is not.

### Why it cannot accumulate

The filter is an **output stage, never an input**. `s_currentPosition`
is written from the Cartesian waypoint in micrometres (`Motion.c`, the
commit point after the queue accepts the entry); `s_cmdStepsL/R` are
derived from it. No quantization decision feeds back into tracked
position, so the question of drift does not arise structurally — and the
ideal is recomputed from absolute position at every waypoint, so the
filter converges rather than integrating.

Measured over 400 chained random segments:

| | reversals | ≥2-phase jumps | worst final error |
|---|---|---|---|
| before | 16165 | 46 | 0.50 step (27 µm) |
| after | 143 | 0 | 1.57 step (84 µm) |

The error is larger and **bounded** — it does not grow with the number
of segments. 84 µm of cable is well inside the mechanical precision of a
cable plotter, and below a pen line width.

One case changes position without the motors moving:
`Motion_SetCurrentPosition()`, which declares where calibration has
established the head. It re-seeds the filter, or it would spend the next
thousand waypoints walking to a position it was never asked to reach.

## REQ-CoreLR-D-Motion-0016 — Sub-divided waypoint grid and dithered holds (implemented 2026-09-11)
<a id="REQ-CoreLR-D-Motion-0016"></a>

> **Superseded 2026-10-06, implemented (removed)** by `-0018`/`-0020`: neither the subdivided grid nor the dither exists in v2. The CPU cost of this subdivision is what the bench regression of 2026-09-11 turned out to be (`../docs/bringup/Bringup-Status.md`).

The waypoint grid shall be `MOTION_GRID_SUBDIV` times finer than one
motor step, and `ComputeHoldTicks()` shall carry the fractional
remainder of each hold rather than truncating it.

### Why the grid was too coarse

The grid was exactly one motor step, which quantized each motor's
**step instants** — not just its positions — to the grid. A motor whose
ideal interval is 1.6 waypoints steps at 1, 2, 1, 2 … waypoints, so its
instantaneous rate alternates between 189 and 377 steps/s. The rotor is
accelerated and braked on every step.

The maintainer reported this from the bench as vibration on **pure X
and pure Y** moves — where the Bresenham staircase is perfectly
regular and `REQ-CoreLR-D-Motion-0015`'s chatter cannot apply — while
the L/R adjustment jog stayed silent. That contrast is what located the
cause: the adjustment jog dithers its holds to 1/256 of a tick, so its
rate ratio is 1.00.

Measured worst-case ratio of instantaneous step rate:

| subdiv | grid | waypoints/s at VMAX | X pure | Y pure | 45° |
|---|---|---|---|---|---|
| 1 | 53 µm | 377 | 2.00× | 2.00× | 2.00× |
| 2 | 26 µm | 769 | 1.33× | 1.50× | 2.00× |
| **4** | **13 µm** | **1538** | **1.17×** | **1.20×** | **1.29×** |

2 does not help at 45° at all, so 4 was chosen.

### The dither is not optional at this grid

`holdTicks = WAYPOINT_GRID_UM / v` is an integer division, so
truncation biases the speed **upward**, and the finer the grid the
worse:

| grid | truncated | dithered | commanded |
|---|---|---|---|
| 53 µm | 21.20 mm/s | 20.00 mm/s | 20.00 mm/s |
| 13 µm | **26.00 mm/s** | 20.02 mm/s | 20.00 mm/s |

At 13 µm the truncated form would have run 30 % over `VMAX` — past the
rate at which this motor stalls — so subdividing the grid without
dithering the hold would have made the machine worse, not better. The
+6 % that the old 53 µm grid was already running is fixed as a side
effect.

The debt is deliberately **not** reset per segment: it is always under
one tick, and carrying it across boundaries is what holds a chain of
short moves at the commanded speed.

### Result, in ISR ticks between consecutive steps

| | before | after |
|---|---|---|
| X pure | 10–22 ticks (2.20×) | 15–19 (1.27×) |
| Y pure | 10–22 ticks (2.20×) | 12–16 (1.33×) |
| 45° | 10–22 ticks (2.20×) | 13–16 (1.23×) |

### Cost and its guards

Four times the queue entries (1538/s at `VMAX`) and two integer square
roots each. `loop()` must average 1538 / `MOTION_STEPS_PER_TASK_MAX`
≈ 96 passes per second — an **average**, since the queue absorbs the
jitter of the other tasks around it (maintainer, 2026-09-11).
`MOTION_STEPS_PER_TASK_MAX` was raised 8 → 16 for margin, and
`Stepper_StarvedCount()` on the adjustment screen is the instrument if
the average is ever not met.

Two compile-time guards, both validated against deliberately broken
configurations: `MOTION_GRID_SUBDIV` ≥ 1, and the grid at least one
tick's travel at `VMAX` — below that the `holdTicks < 1` clamp would
silently cap the speed instead of reporting anything.

## REQ-CoreLR-D-Motion-0017 — Velocity unit, and grid 1/8 (implemented 2026-09-11)
<a id="REQ-CoreLR-D-Motion-0017"></a>

> **Partly superseded 2026-10-06:** the velocity unit stands (restated in physical terms by `-0020`); the 1/8 grid does not.

Velocities shall be expressed in **sixteenths of a micrometre per
tick** (suffix `_V16`), and the waypoint grid shall be subdivided
8 times rather than 4.

### The velocity unit could not express the machine's limit

`MOTION_VMAX_UM_PER_TICK` was a plain integer count of µm/tick. The
measured limit — 400 steps/s, i.e. 21.5 mm/s — is 5.375 µm/tick at the
4 kHz tick, and the only expressible neighbours were 5 (20 mm/s) and
6 (24 mm/s = 447 steps/s, past the stall point). 5 was chosen, and the
maintainer asked why the cap sat below the measured limit. The answer
was granularity, not judgement.

At 1/16 the same figure is **86, exactly**. One unit is 0.25 mm/s, and
255 units is 63.8 mm/s, so every velocity still fits the `uint8_t` the
planner already stores per block — no RAM cost.

This is the same defect fixed once before on the acceleration
(µm/tick² → nm/tick², whose smallest non-zero integer meant 16 m/s²).
Velocities had it too, and it had gone unnoticed because the number it
produced looked plausible.

Consequence throughout `Motion.c`: every `v²` term carries a factor
`MOTION_V_SCALE²` = 256 against distance terms, which stay in
micrometres. Verified against the integer code as written:

| | computed | intended |
|---|---|---|
| speed at `VMAX` | 21.56 mm/s ≈ 401 steps/s | 21.5 mm/s, 400 steps/s |
| halt ramp | 200 grid steps = 1.20 mm | v²/2a = 1.21 mm |
| effective acceleration | 192 mm/s² | 192 mm/s² |
| `ReachableV` from rest over 1204 µm | 86 | `VMAX` |

### Grid 1/8, and why the ISR was left alone

Jitter between consecutive steps of one motor, RMS, as a fraction of
the mean interval:

| | X pure | Y pure | 45° |
|---|---|---|---|
| grid 1/4 (previous) | 13.5 % | 14.2 % | 10.0 % |
| **grid 1/8** | **7.2 %** | **9.6 %** | **8.1 %** |

An 8 kHz tick was costed and **rejected**. Decomposing the jitter showed
the hold quantization contributes barely one point of the 13.5 — the
1/256 dither already makes the *sum* of holds exact, so its error does
not accumulate. The rest is waypoint quantization, which only a finer
grid addresses. 8 kHz would have bought 1–2 further points in exchange
for re-homing `SysTick`, which is derived from that same tick.

Cost: 3593 queue entries/s at `VMAX`, so `loop()` must average 225
passes/s. `Stepper_StarvedCount()` is the instrument.

### What would actually remove the rest

Neither a finer grid nor a faster tick reaches the floor, because a
motor can only step **at a waypoint boundary**. Removing that means
decoupling motor steps from waypoints — a per-motor DDA in the ISR
spreading each motor's steps evenly across the interval, as `grbl` does.
Agreed with the maintainer (2026-09-11) as the next substantial piece
of work, to be done **on its own git branch**.

A variable-period timer was also discussed and set aside: measured at
1 point of the 13.5, it does not justify itself on jitter alone. Should
it be revisited, the maintainer notes that **Timer1 could be freed**
— the pen servo only ever takes two positions and needs no precision,
so the `Servo` library could be replaced by something cheaper. That
would give a 16-bit timer (0.5 µs resolution, 32 ms range at prescaler
8) instead of the 8-bit Timer2, and should also recover flash, which is
now the tighter budget at 91 %.

## v2 architecture — step events computed by `Motion` (2026-10-06)

Companion to `REQ-CoreLR-D-Stepper-0010`…`-0014`; supersedes `-0006`,
`-0014`, `-0016` and the grid half of `-0017`. **Approved in principle by
the maintainer 2026-10-06; values marked *proposed* await
confirmation.**

**Why.** Measured with `tools/analysis/cyclesim/` on the real compiled code: one
waypoint costs **33 300 cycles**, so at 100 % of the CPU `Motion` makes
481 waypoints/s. The 6 µm grid asks for 3583/s at `VMAX`. The machine was
CPU-bound, the Cartesian jog ran at 2.9 mm/s (bench-confirmed:
~3 mm/s on the display, `miss` saturated at 255). No square-root
optimisation closes a 7× gap. Decoupling *where the head goes* (a coarse
waypoint, ~100/s) from *when each motor steps* (an event) removes the
problem rather than shaving it.

> **Names, 2026-10-07:** `Motion_GetCurrentPosition()` is now `Motion_GetComputedPosition()` (the last position *computed*, not the physical one) and `Motion_GetEndPosition()` is `Motion_GetTargetPosition()` (the end of everything accepted). Older notes below use the old names only where they describe what happened under them.

## REQ-CoreLR-D-Motion-0018 — Step events at the crossing instants of a piecewise-linear cable length (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Motion-0018"></a>

```
 Cartesian line ──► coarse waypoints P_k ──► absolute transform ──► ideal S_L(k), S_R(k)
   (-0019)           (~100/s at VMAX)         XYToLR, per waypoint     in 1/256 step

 within segment k, each motor's ideal position is LINEAR in time:

       S(t) = S(k) + (S(k+1) - S(k)) * t / T_k          0 <= t <= T_k

 a step happens when S(t) crosses the decision threshold around the
 commanded count c :        up   at  S >= (c + 1/2) * 256 + H
                            down at  S <= (c - 1/2) * 256 - H
 c changes by exactly +-1, coil bits come from c (-0007), and the two
 motors' events are merged chronologically into queue entries:

       entry = { coil bits after the event,  hold = t_next_event - t_this_event }
```

The thresholds are crossed at times that can be computed exactly —
`t = T_k * (threshold - S(k)) / (S(k+1) - S(k))` — with one division per
motor per segment for the first event and an addition per further event
(events within a segment are equally spaced: `256 * T_k / |ΔS|`).

### Four rules

1. **Absolute, never accumulated.** The only per-motor state carried
   from one segment to the next is the commanded count `c`. The
   fractional position is *not* carried: `S(k)` is recomputed from the
   absolute transform of the waypoint, so a rounding decision in one
   segment can never feed the next
   (`REQ-CoreLR-D-Motion-0002`/`-0004`, same principle).
2. **The jitter is the timer's, because the fraction is kept.** Rounding
   a per-segment step count to an integer would give 3, 4, 4, 3 steps on
   equal segments — a ±25 % swing of the step interval at every waypoint,
   *worse* than v1. Crossing the threshold at the exact instant is what
   makes the step interval depend on the timer resolution (0.5 µs) and the
   ISR latency, and not on the waypoint spacing. This is what
   `REQ-CoreLR-F-Actuation-0003` is measured against.
3. **No event is placed closer than `STEPPER_DELAY_MIN_COUNTS` to the
   previous one: the later is delayed to `previous + MIN`**
   (`REQ-CoreLR-D-Stepper-0012`). Chosen over applying both at the earlier
   instant by the prototype: it guarantees no hold below the minimum by
   construction (0 in 7.7 M entries, against 161 for the other), at the
   same jitter. An event pushed past its segment's end is not lost — its
   threshold is still behind the next segment's start, so it comes out
   there at time 0, clamped the same way.
4. **The rate limit holds by construction.** Between two events of one
   motor the ideal position must advance by 256 sub-steps; its slope never
   exceeds the Cartesian cap (`-0011`, `VMAX ≤ 13 µm/tick`-equivalent), so
   consecutive steps of one motor are never closer than 1/400 s. No per-motor
   logic, as before.

### Generator state
Resumable, one *event* per call (not one waypoint): the next event time of
each motor, their per-segment period, the remaining steps in the segment,
the segment's start time as an absolute counter (differences only, so
wrap-around is harmless), and `c_L`, `c_R`. Of the order of 30 bytes of
static RAM — to be measured at implementation. The entry for an event is
enqueued one event late, since its hold is the distance to the *next*
event.

### What replaces what
| v1 | v2 |
|---|---|
| one entry per 6 µm grid step (`-0006`, `-0016`) | one entry per **step event** |
| hold quantised to a tick, dithered to 1/256 tick (`-0016`) | hold in 0.5 µs counts, **no dither** |
| 4-connected Bresenham staircase (`-0006`) | points exactly on the line (`-0019`) |
| ≤ 1 step per motor per waypoint, enforced by the hysteresis clamp (`-0015`) | the clamp is gone, see `-0015`'s status |

### Measured on the prototype (2026-10-06, `tools/prototypes/evproto/`)

The generator was written as a self-contained integer C file and run on the
PC against the **real v1 stream on the same machine and metric**, and on the
cycle simulator. Machine assumed: 1 m between motors, 700 mm reference cable.

**Step-interval jitter** at constant `VMAX` (rms of the relative change
between consecutive intervals, middle 60 % of each motor's steps):

| move | v1 (6 µm waypoints, 250 µs tick) | v2 |
|---|---|---|
| X pure | 8.9 % | **0.28 %** |
| Y pure | 10.0 % | **0.31 %** |
| 20° | 10.1 % (L) | **0.25 %** |
| 70° | 9.2 % (L) | **0.20 %** |
| 50 chained 600 µm jog blocks | — | **0.25 %** |

Against the 3 % target of `REQ-CoreLR-F-Actuation-0003` that is a factor
of ten of margin. (Earlier v1 figures of 7–10 % were simulations of an
ideal stream; these come from the actual v1 code, and agree with them.)

**Everything else, same run:** drift over 400 random moves — worst deviation
0.56 step, not growing; rest-to-rest 20 mm in 1016 ms against 1021 ms for v1
(the profile is the same); **533 queue entries against 3333** for the same
move.

**Three decisions the prototype settled:**

1. **The hysteresis is *not* needed — but a tie guard is.** The v1 chatter
   (181 reversals on tangential travel) does not occur: v2 gives 0
   reversals with `H = 0` and with any larger `H`, because the sawtooth
   was a product of the staircase. **But `H = 0` fails a different test:**
   fuzzing 3000 random moves found two steps of one motor **32 µs apart**.
   After a step up to `c+1`, the down-threshold of `c+1` is the *same
   number* as the up-threshold of `c`; a reversal that lands exactly on it
   (likely: positions are integers of 1/256 step) steps up and back down at
   once. Any `H ≥ 8` (3 % of a step) removes it — fastest interval then
   2.45 ms over 7.7 M entries. **`H = 16` chosen** (6 % of a step, margin
   of 2× over the waypoint-rounding noise); cost: end-of-move deviation up
   to 0.56 step (30 µm) instead of 0.50. `H = 128` (v1's one-step band)
   buys nothing and costs 1.0 step of accuracy. The 2.45 ms is 408
   steps/s: the Cartesian cap yields 401, and the integer rounding of the
   segment durations accounts for the rest — inside the 600 steps/s
   unloaded limit, 2 % over the loaded one.
2. **Delay, not merge, for events closer than the minimum** (rule 3).
3. **Waypoint spacing 4 steps stays the default; 8 is the CPU option.**
   Jitter does not fall with finer waypoints — it *rises* (1 step: 1.2 %,
   2: 0.7 %, 4: 0.3 %, 8: 0.1 %), since each waypoint contributes its own
   rounding noise (1 µm in position, 1/256 step in cable length). The cost
   of coarser ones is the speed profile, evaluated per segment: at 16
   steps a 20 mm rest-to-rest move runs 3 % short.

**CPU, on the cycle simulator** (30 mm at `VMAX`; `ISR` and LCD not
included):

| | cycles |
|---|---|
| one waypoint (segment start) | **25 600** — two `Motion_ISqrt(uint64)` 13 700, two 64-bit divisions, two 32-bit roots |
| one step event | **2 200** — a 32-bit division and the coil lookups |
| load at `VMAX`, spacing 4 | **22.6 %** of the CPU (45°: 20.1 %) |
| load at `VMAX`, spacing 8 | **15.7 %** |

For comparison v1 needed 33 300 cycles per 6 µm waypoint at 3583 per
second — 7.5× the whole CPU. Not yet optimised: a seeded-Newton root
(2 164 cycles against 6 838) would take about
9 000 cycles off each waypoint, and the per-event divisions can be
replaced by an addition once a segment's period is known. The projected
load is ~14 %.

The prototype is the reference for the port into `Motion.c`; the integer
ranges it relies on (`T` ≤ 107 500 counts, `num·T` < 1.2·10⁸, sub-steps
< 2²⁴) are stated in `evgen.c`.

## REQ-CoreLR-D-Motion-0019 — Coarse waypoints on the exact line (proposed 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Motion-0019"></a>
The Cartesian interpolation produces waypoints every `MOTION_WAYPOINT_STEPS`
motor steps of travel — **4** (*proposed*), i.e. ~215 µm — by integer
proportional interpolation on the commanded line:
`P_k = start + round(Δ * k / N)`, with `N = ceil(length / spacing)`.
It no longer walks a 4-connected staircase. That constraint existed so
that one waypoint could never need more than one motor step
(`-0006`); an event stream has no such limit.

**Accuracy.** The error of joining waypoints with straight lines in
cable-length space is the sagitta of the cable-length curve. At 215 µm
spacing and a 100 mm cable (the shortest realistic) it is
`s²/(8R) = 215²/(8·100000) =` **0.06 µm**; at 700 mm, 0.008 µm. Far below
the machine's own step of 54.0 µm (53.7 before the gear ratio was corrected, 2026-10-26).

**CPU.** At `VMAX` this is 100 waypoints/s; with the measured 33 300
cycles each, **~21 % of the CPU** with no square-root optimisation at all
(two cheaper roots remain available if needed:
`tools/analysis/cyclesim/run_cands.py` — a 32-bit root on the velocity terms costs
615 cycles instead of 5 488). Arcs (`G2`/`G3`) will reuse the same
waypoint-and-event stage.

*(The spacing is a tunable, not a constant of the design: 8 steps would
halve the CPU for 0.03 µm of sagitta; a smaller spacing is not useful
below the point where the step intervals stop depending on it.)*

## REQ-CoreLR-D-Motion-0020 — Time-domain velocity profile (proposed 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Motion-0020"></a>
The velocity profile no longer depends on a tick period.

- **Velocity unit:** 0.25 mm/s (it already is: `-0017`'s `V16` unit was
  1/16 µm per 250 µs tick). The numeric values are **unchanged**
  (`VMAX` = 86 ⇒ 21.5 mm/s), only the unit is now stated in physical
  terms and the `_V16` suffix loses its meaning.
- **Acceleration:** in **mm/s²** (192, from 12 nm/tick²).
- **Segment duration:** `T_k = d_k / v_k` in Timer1 counts, with `v_k`
  evaluated at the **middle** of the segment, not at its end — at
  192 mm/s² and a 10 ms segment, the speed changes 1.9 mm/s (9 % of
  `VMAX`) across one segment, so the end value would bias every ramp.
- The fractional-tick accumulator `s_holdAccumQ8` is **removed**; the
  0.5 µs resolution makes it pointless.
- The junction look-ahead (`-0013`), the halt/resume ramp (`-0012`) and the
  Cartesian rate cap (`-0011`) are **unchanged in behaviour**; they now
  advance per coarse waypoint. A halt ramp of 1.2 mm is ~6 waypoints.

### Integration notes, 2026-10-06 (the port into `Motion.c`)

**Measured on the real code** (`tests/host/test_motion.c`, `tools/analysis/cyclesim/t_motion.c`):

| | v1 (6 µm waypoints) | v2, ported |
|---|---|---|
| step-interval jitter at `VMAX`, X / Y / 20° / 70° | 8.9 / 10.0 / 10.1 / 9.2 % | **0.28 / 0.31 / 0.25 / 0.20 %** |
| 50 chained 600 µm jog blocks | ~3 mm/s (CPU-bound) | **19.6 mm/s** average, `VMAX` = 21.5 |
| 20 mm rest to rest | 1021 ms | 1008 ms |
| CPU while moving at `VMAX`, `Motion` alone | 7.5× the whole CPU | **17.6 %** (X), **16.2 %** (45°) |
| entries for a 30 mm move | ~5800 | **799** |

The CPU figure is below the prototype's 22.6 %: the profile takes its roots in 32
bits (`ISqrt32`, 615 cycles against 5 488) and the `a·32/1000` is a multiply and
a shift (`MOTION_V2_Q10`), not a division.

**Verified** (`test_motion.c`, the real `Motion.c`): the prototype's jitter and zero tangential reversals
reproduce; the move starts *at rest*; **a request Stepper refuses never changes the stream** — the same
five queued moves with 0 % and 40 % of requests refused give an identical entry stream (859 refusals); a
halt ramps to rest in 37 entries / 107 ms from `VMAX`, the motors are within 0.6 step of where `Motion`
believes them, and a resume finishes exactly at the target; abort after a halt leaves the machine
consistent and a fresh move follows; 600 random moves, 811 168 entries, random halt/resume, 25 % of
requests refused: no double-phase jump, no hold below the minimum, every end within 0.56 step, nothing
wedged. Deliberately broken copies are caught (the rest-start rule removed; an outbox entry dropped;
the halt flush removed hangs the halt). The last short entries it reports are final flushes shorter than
the minimum, which Stepper clamps.

**Decisions taken in the port, beyond the prototype:**
1. **The state is one struct** (`Move_t`: the move, the segment in flight, the commanded counts, the timeline, the
   outbox), reached through a register pointer where it is hot. 130 bytes of RAM against v1's ~90 for its
   segment struct plus the dither: static RAM +0 net (the lost `Servo` RAM paid for it).
2. **The outbox.** The generator computes an event (and advances the commanded count) before Stepper has
   accepted it; a refusal must not lose it. A one-entry outbox is offered again before anything new is generated.
3. **Pending entry and flush.** An event's entry is held back until the distance to the *next* event is known; it is
   flushed (hold running to the end of the timeline) when the generator goes idle, at a halt's virtual end, and by
   `Motion_Abort()` — whose pending entry carries a coil change already counted in the commanded step count, and
   dropping it would leave the motors a step short of where `Motion` believes them.
4. **From rest, the entry speed is zero.** The planner's reverse pass knows what a block can brake from, not
   whether the machine is moving; left alone the first block after a stop started at its planned entry speed, up to
   `VMAX`, from standstill. Found while writing the host test. v1 had the same latent defect for planned moves and
   never met it, because the jog bypassed the planner. After a resume the entry speed is zero too.
5. **Halt/resume in segments.** A halt's virtual end is a segment index; the segment in flight finishes first, the
   ramp starts at the next. `Motion_IsHaltRampDone()` is true only once the last entry is *queued*.
6. **The planner works in micrometres**, with an approximate length (±4 %, `BlockLength`: no root per block per
   `Replan()`); the move actually executed uses the exact length, once, in `StartMove()`. Junction normalisation stays
   Manhattan: it needs no root, and the jerk constant is a placeholder to tune on the machine anyway.
7. **`Motion_GetTargetPosition()`** (new): the target of the last queued block. **The jog now queues through the planner**
   (`REQ-CoreLR-D-LcdJoystickUI-0017`): until now it called `Motion_RequestLinearMove()`, which bypasses the planner and
   is ignored while a move is in progress, so look-ahead was inert and slices were dropped. `RequestLinearMove` stays,
   for a standalone rest-to-rest move.
8. **`ComputeCoilBits`** is a two-bit mask: the low bits of a two's-complement integer *are* its value modulo 4, negative or not.

**Cost, measured** (Nano fqbn): flash 28362 → **29798 bytes (96 %), +1436**; static RAM 1677 → 1712; stack margin +51 → +40.
The flash is the generator's own weight — `BeginSegment` 1086, `StartMove` 900, `MotionStepOnce` 1152, `CrossTime` 262, `BlockLength`
312, `IdealAt`/`IdealSubSteps` 228 — less the v1 code it replaces (`MotionStepOnce` 2278, `StartSegment` 706, `GridSteps` 360). AVR
spends 4 bytes on every byte of a 32-bit `lds`/`sts`, so a segment's worth of 32-bit state is expensive. Recovered at the time by:
one shared `IdealAt()` (−56), the root as a shared function (−96), the phase mask (−206), the state in one struct behind
a pointer (−180), shifts instead of divisions (−20).

### Optimisation pass, 2026-10-06 (after the bench confirmed the port)

| Step | Flash | RAM | |
|---|---|---|---|
| the port, as committed | 29798 | 1712 | |
| `SDFAT_NO_SERIAL` in the vendored sdfat | **29066** | **1537** | −732 / −175: `HardwareSerial` was linked for nothing (see `third_party/sdfat/PROVENANCE.md`) |
| Newton root, 32×32→64 squares, multiply-shift for the sub-steps | **28252** | 1537 | −814 |

Total: **28252 bytes (91 %), 1537 bytes of RAM (75 %)** — *below* the v1 firmware's 28362 / 1710, with the persisted position, the Timer1
ISR, the software servo and the event generator all in. Free flash 2468 bytes, stack margin **+215**.

What the third step did (`tests/host/test_arith.c`: exact references, all pass):
- **`Motion_ISqrt`**: Newton from above, seeded with 2^ceil(bits/2), against the bit-by-bit root. Exact on 409 045 values (edges,
  every square ±1, random at every magnitude). 502 → ~120 bytes; it reuses the 64-bit division the firmware links anyway. Cycles are
  *lower* on the inputs that occur: 3 600–7 400, against 7 400.
- **`Motion_XYToLR`**: each square is 32×32→64 on the absolute value, no 64×64 multiply. Exact on 5 840 positions.
- **`IdealSubSteps`**: `length × K >> 24` with K = steps·256/circumference as a 2^24 constant, in place of a 64-bit division of ~1 500
  cycles. Within one sub-step (1/256 of a step) of the exact rounded quotient over 0–4 m.
- **`QuantizeToSteps`** is `IdealSubSteps` and a shift.

Effect on the CPU, `tools/analysis/cyclesim/t_motion.c`: **17.6 % → 14.6 %** while moving at `VMAX`.

**Flash trims of 2026-10-08, −214 bytes (30 706 → 30 492, 228 free), found by attributing every byte of the image to its source line**
(`avr-objdump -d -l` on the linked image, summed per line): `Motion.c` alone is 10.6 KB of the 30. Two changes, both
behaviour-neutral —
- `Mag()` out of line (it was inlined at seven sites, 136 bytes): **−46**;
- `StepMotor()`: the commanded-count update and next-crossing computation of one motor, written out twice in
  `MotionStepOnce` (92 bytes each) for L and for R, is one function taking the motor as a flag: **−168**.
Proof of neutrality: the *hash of every entry sent to `Stepper`* — 820 385 entries over all of `test_motion`'s scenarios and
11 421 789 over `test_arc` — is **identical before and after** (`3a04d9a6`, `a9cfe556`; the tests now print it, so any later
refactor of `Motion` can be held to the same standard); three mutants of the helper (fields swapped, direction inverted, the
wrong motor's count) are caught. Cost: stack 318 → 323 bytes (the extra call frame), margin still ≈ +375; generation CPU load of a
30 mm line **9.91 % → 9.72 %** (cycle simulator, the same 799 entries). **Second round, the same day, −442 bytes more (30 492 → 30 050, 670 free), signature change authorised by the maintainer:**
- the parser's redundant `HasX ? X : 0` / `HasI ? I : 0` (the tokenizer already zeroes the values): **−114**;
- **`Motion_ArcCheck(&target, i, j)` and `Motion_QueueArc(&target, i, j, cw)`** — the target by pointer, and the *start* no
  longer a parameter: an arc starts where the queue ends, `Motion_GetTargetPosition()`, and it was the only start
  there ever was (two 8-byte structures by value had cost 250 bytes in the parser's call): **−194**;
- `V2Gain()`: the expression "v0² plus what accelerating over a distance adds", written for the acceleration, the braking
  and `ReachableV`'s look-ahead: **−88**;
- `memcmp` for "end equals start" (the full circle): **−46**.
Still the same proof: the hash of every entry sent to `Stepper` is **identical** (`3a04d9a6`, `a9cfe556`), and mutants of the new
code change it (wrong operands of `V2Gain`, the clamp, `memcmp` over 4 bytes or inverted). **A try that made things worse was
reverted:** a `ClampEvent()` helper for the two motors' event times cost **+66** bytes (the call and its arguments outweigh
62-byte lines), which is why each trim is *measured* before it is kept. Cost: worst stack **312** (was 323, and 318 before the
trims), generation CPU 9.85 % for a 30 mm line; **an arc is now 11.8 % (quarter, 20 mm), `Motion_ArcCheck` 0.20 ms and
`Motion_QueueArc` 0.90 ms** (17 % / 0.65 / 1.35 ms in the first measurement, under the 64-bit arithmetic).
Candidates measured, not taken: Arduino's `pinMode`/`digitalWrite`/`turnOffPWM` and their pin tables (~360 bytes) are
linked only for the SD card's chip select and the UI's button; replacing them means changing the vendored sdfat's pin calls
on a card that **has never been tried on the bench**, so it waits for that; the calibration bisection's exactness test
(88 bytes, ±1 µm); a 16-bit-argument `Tangent` (~30).

Taken 2026-10-07 (`REQ-CoreLR-D-LcdI2c-0001`): the LCD stack (-2 074 bytes of flash, -269 of RAM, and the TWI interrupt gone from the latency chain of `Stepper-0012`). Still to take, by expected yield: `StartMove` (nine 32-bit divisions); `BlockLength`/`Replan`;
dropping `Motion_RequestLinearMove`. Deriving the position at start-up again (`Calibration-0007`) was retried and measured: +696 bytes still, not adopted — `docs/design-notes/experiments/`.

## Arcs (specified 2026-10-07)

Realizes `REQ-CoreLR-F-GCodeInterpreter-0003`/`-0009` and `REQ-CoreLR-F-Motion-0007`. Decisions confirmed with the
maintainer: `I`/`J` only (`R` deferred), a 2 µm chord tolerance, the centripetal limit at the machine's own
acceleration, the arc as one block of the look-ahead planner, grbl's radius rule.

## REQ-CoreLR-D-Motion-0021 — Trigonometry as its own small library (implemented 2026-10-07)
<a id="REQ-CoreLR-D-Motion-0021"></a>
A new module, **`Trig`** (plain C, no dependencies, `Trig.h`/`Trig.c` — "our own small libraries" convention: a
general-purpose utility gets its own `.h`/`.c` pair). Angles are `uint32_t` **turns, Q32**: 2^32 is one full turn, so a full circle wraps for free and no
angle ever needs normalising. Every function is *absolute*: it computes from its arguments alone, so nothing
accumulates along an arc.

- `Trig_Rotate(r, angle, &x, &y)`: `(r, 0)` rotated by `angle`, in µm. Reduced to one octant, sin and cos as
  Taylor polynomials with Q32 fractions, each product the **high word** of a 32×32→64 multiply — free on AVR,
  where a 64-bit shift is a library call.
- `Trig_Angle(x, y)`: the angle of a vector. **Not** Newton iteration on `Trig_Rotate`, as first specified: that
  was accurate but cost 652 bytes and pulled 64-bit signed multiply and divide (~280 more). It is a **minimax
  odd polynomial** (7 terms, Q30 coefficients fitted for it) in the ratio min/max of the components, evaluated by
  the *same* nested-polynomial loop as sin and cos, with octant and quadrant fixed by comparison: one division
  and no iteration.
- `Trig_ArcLength(r, sweep)`: `r · sweep · 2π`, in µm, two high-word products.

**Measured** (`tests/host/test_trig.c`, long-double reference; cycles on `tools/analysis/cyclesim`):

| | worst error | cycles |
|---|---|---|
| `Trig_Rotate`, 3 M random cases to a 2 m radius, and every octant boundary ±8 units | **0.55 µm** | ~4 300 |
| `Trig_Angle`, 2 M vectors 16 µm … 2 m, error as displacement at the vector's own radius | **0.36 µm** | ~4 000 |
| `Trig_ArcLength`, 1 M cases to 2 m, a full turn included | **0.50 µm** (the rounding) | — |

The two kernels together are ~1 260 bytes with their tables. The prototype comparison (CORDIC: 0.74 µm, ~1 100
bytes for rotate alone and 6 000+ cycles) is in `tools/prototypes/arcproto/README.md`.

## REQ-CoreLR-D-Motion-0022 — Arc geometry and generation (implemented 2026-10-07)
<a id="REQ-CoreLR-D-Motion-0022"></a>
Given the start `S` (`Motion_GetTargetPosition()`), the end `E`, the offsets `(I, J)` and the direction:

```
 C = S + (I, J)          a = S - C         b = E - C
 r_s = |a|   r_e = |b|    theta_s = angle(a)   theta_e = angle(b)        (Trig_Angle)
 sweep = (theta_e - theta_s) mod 2^32   for G3 (counter-clockwise)
         (theta_s - theta_e) mod 2^32   for G2 (clockwise)
 E == S exactly  ->  a full circle, sweep = 0xFFFFFFFF (2^-32 of a turn short: 0.003 µm at 2 m, and the last waypoint is exact anyway)
```

**One function for the geometry**, `ArcSolve()`, used by the check, by the queueing and by the start of the move
— passing three `Motion_Position_t` by value to separate helpers cost more flash than the arithmetic did. With
`full` unset it returns only the two radii (what the check needs).

**Reach.** No vector from the centre to an end point and no radius may reach **2^21 µm (2.097 m)**: a single OR of
the four magnitudes and a shift. That one bound keeps the squares, `Trig`'s 2^22 range and the 16-bit segment count
(≤ 58 720 for a full circle of that radius) safe. (Specified as "2 m"; the power of two is the tested form.)

**Segments.** Uniform angular steps, `N = max( ceil(L / 214 µm), ceil(L / √(8·tol·r)) )` with `L` the arc length
(`Trig_ArcLength` of the mean radius), `r` the start radius and `tol` = `MOTION_ARC_TOLERANCE_UM` (2). The second
term is the chord rule `dθ = √(8·tol/r)` written as a length, which needs one integer root and no angle.
A uniform angular step makes the segments equal in length, so `REQ-CoreLR-D-Motion-0018`'s machinery —
`SegLenQ8`, the profile at the middle of the segment, `CrossTime`, the outbox, halt and resume — is reused unchanged.

**Waypoints.** Waypoint `k` (1 ≤ k < N) is `C + Trig_Rotate( r_k, theta_k )`, both **exact rational shares of
the whole**, not sums of rounded steps: `theta_k = theta_s ± sweep·k/N` and `r_k = r_s + (r_e − r_s)·k/N`, each kept as a
quotient and a remainder spread by an accumulator (`Spread()`, the same device as the line's X/Y), so the value at
`k` is `round(total·k/N)` whatever was or was not computed before it, and at `k = N` it is the end exactly. This
replaces "computed from `k` each time" with an equivalent that needs no 64-bit product or division. The radius
blends linearly, so the arc passes **exactly** through `S` and `E`, and the last waypoint is `E` itself.

**Degenerate cases** (`ArcSolve(.., full = 1)` returns 0, and the block is queued as the straight move it is):
a radius under one motor step (53 µm); no sweep at all (`E` radially in line with `S`, not equal to it).

**State.** `Move_t` gains the centre, the current angle and radius, their steps (quotient, remainder, accumulator)
and a direction: **34 bytes** of RAM.

**Verified** (`tests/host/test_arc.c`, the real `Motion.c` and `Trig.c`, every waypoint sampled by stepping the
generator one iteration at a time, against long-double geometry):

| | result |
|---|---|
| 1 346 arcs, 3 907 511 segments: radii 54 µm … 1.9 m, 11 starts (every octant), 7 sweeps from 0.0006° to a full turn, both directions | worst waypoint **1.78 µm** from the ideal (limit 2) |
| chord, on the ideal points | ≤ 214.0 µm (the waypoint spacing) |
| sagitta, on the ideal points | **1.994 µm** (the tolerance 2) |
| 48 full circles, both directions, 24 starts | closed **exactly**, wound once, the right way round |
| radius blend 10 000 → 10 009 µm | 1.06 µm from the linear blend, ends exact |
| `Motion_ArcCheck`, 13 cases on both sides of grbl's rule and of the reach | all as specified |
| the same lines and arcs with 0 % and 40 % of requests refused | stream **identical** |
| halt in mid-arc, ramp down, resume | stopped 0.34 µm from the circle, ended exactly, 0.11 step |
| fuzz: 500 random lines and arcs, random halts, 25 % refused | 0 double jumps, 0 holds below the minimum, 0 bad ends, worst end deviation 0.559 step |

The test was run against ten deliberately broken copies of the firmware (radius blend sign, tangent direction,
ceiling ignored, junction using the wrong end's tangent, angle remainder dropped, ceiling missing from the exit
speed, full circle not detected, tolerance doubled, last waypoint off by 1 µm, executing arc keeping its start
tangent); **ten caught** — two of them only after tests were added for them, which is why those checks exist.

**Cost, measured on the cycle simulator** (`tools/analysis/cyclesim/t_arc.c`; Motion alone, 1 m motors): `Motion_ArcCheck`
0.40–0.84 ms and `Motion_QueueArc` 0.96–1.55 ms, once per arc; **generation 14–17 % of the CPU** against 15 % for a
straight line of the same length (a quarter turn of 20 mm radius 17.0 %; a 1 mm full circle 16.1 %; 300 mm 16.1 %).

## REQ-CoreLR-D-Motion-0023 — Arcs in the planner and the profile (implemented 2026-10-07)
<a id="REQ-CoreLR-D-Motion-0023"></a>
An arc is **one block** of the look-ahead planner (`-0013`). `PlannerBlock_t` is now `Target`, `Center` (8 bytes,
arcs only), `Len16` (the arc length, saturating at 65 535 µm: the planner only needs it to ~2 m), `EntryV`, `Cap`
and `Flags` (`PLAN_ARC`, `PLAN_CW`): **21 bytes** a block, 84 at the planner depth of 4 (was 36). `Motion_QueueArc()`
solves the geometry once, at queue time, for the length and the ceiling; `StartMove()` solves it again for the
generator rather than carry 16 bytes of it in every block.

- **Tangents, with no trigonometry.** The direction at the start of a counter-clockwise arc is the radius vector
  `a` turned by +90°, `(−a.y, a.x)`; clockwise, `(a.y, −a.x)`; at the end the same from `b` (`Tangent()`).
  `UnitDir()` normalises by the Manhattan length, which a 90° turn preserves. `BlockShape()` returns, per block,
  its length and its *entry* and *exit* directions (equal for a line), and `Replan()` joins block k to block k+1
  with `JunctionVelocity(exit_k, entry_k+1)`. The move in flight keeps its **exit** direction in `Move_t.Ux/Uy`
  for the junction with the next block.
- **Centripetal ceiling.** `v² ≤ a·r` in the units of `-0020`: `v_cap² = r · MOTION_V2_Q10 >> 11`, between `VMIN`
  and `VMAX`; from `r ≥ 2 408 µm` it is `VMAX` and the product is never formed (it would overflow 32 bits). It is
  the block's `Cap` (a line's is `VMAX`), copied to `Move_t.Cap`. It bounds the entry speed `Replan()` assigns
  **and the joint at its exit**, and the speed `BeginSegment()` takes at the middle of every segment.
- **Length.** `ReachableV` uses the arc length `Len16`, not the approximate chord of a line.
- **Halt and resume** are by segment, as for a line (`-0012`); nothing about them depends on the shape.
- **Position bookkeeping.** `Motion_GetTargetPosition()` returns the arc's end point; the start of a block is
  the end of the previous one.

**Verified** (`test_arc.c`, section 5): a 1 mm circle runs at **55 units = 13.8 mm/s, the physical √(a·r)** and no
faster; a 30 mm circle reaches `VMAX`; a line into a tangent arc is entered at `VMAX`, into one at a right angle
at 32 units; an S-bend (two half circles, opposite senses) is not slowed at the join; a 1 mm arc into a tangent
line enters the line at the arc's ceiling (55), not above; an arc in flight leaves a tangent line at `VMAX`.

**Cost, measured on the real build** (Nano fqbn, `arduino-builder`, 2026-10-07) — **far above the estimate of
1.2–1.5 KB**, and the reason is recorded here rather than rounded away: **+4 598 bytes of flash** (28 334 → 32 932,
**107 % of the 30 720 the Nano leaves**), +82 bytes of RAM (1 537 → 1 619), stack 296 → 326. By function: `ArcSolve`
840, `BeginSegment` +672 (of which `Trig_Rotate` is inlined, ~500), `Trig_Angle` 558 + `Nested` 114, `Motion_QueueArc`
496, `StartMove` +432, `Motion_ArcCheck` 396, `BlockShape` 386, `ComputeTarget` 266, `Hypot` 178, `Tangent` 98, and the
parser's own growth. 32-bit arithmetic on AVR costs 8–10 bytes an operation, and an arc has a great deal of it.
Four trims were measured and kept (one shared `Hypot`, a single `ArcSolve` through pointers, a power-of-two reach,
`noinline` on the large shared helpers): −700 bytes. **The build does not fit yet**; the LCD driver replacement
(`REQ-CoreLR-D-LcdI2c`, ~−1.4 KB as estimated then) and further trimming are the way, and are the next work.

## REQ-CoreLR-D-Motion-0024 — Two arithmetics: 64-bit values, or nothing wider than 32 bits (implemented 2026-10-08)
<a id="REQ-CoreLR-D-Motion-0024"></a>
Origin: maintainer's questions of 2026-10-08 — are the 32×32→64 products and the 64-bit divisions necessary; would a
24×24→48 type do; the same for the root — and his instruction to keep **both** implementations behind a `#define`.

**The selector.** `CORELR_MATH` in `SwConfig.h` (`CORELR_MATH_64` or `CORELR_MATH_48`, the default `_48`, overridable with
`-DCORELR_MATH=…`, like `CORELR_JOB_SOURCE`). Both are compiled from the same sources; `tests/host/run_all.sh` runs the
whole suite against either, and **both pass**.

**Answers, with the measurements.**
1. *Are the 64-bit values necessary?* No. The coordinates of this machine stay under 2^22 µm (4.19 m), so x² + y² < 2^45:
   48 bits is the real width, not 64.
2. *Would a 24×24→48 type do?* The range does; the type is not what saves flash. Tried first, literally: a `Wide` library of
   32-bit word pairs (4 × 16×16 partial products in C). **Larger, not smaller** (+394 bytes): `Wide_Mul` 322 bytes against
   116 for libgcc's hand-written `__umulsidi3` + helper. avr-gcc's 32-bit C is 8–10 bytes an operation, and the library's
   assembly beats it. A 48-bit *type* is therefore not worth building; what pays is **never forming the wide value**.
3. *The root?* Yes, and this is where the bytes are. The exact residual v − r0² of a root r0 that is correct to s bits is
   below 2^(s + 24.8), i.e. **below 2^32 for coordinates under 2^22**, so it is known exactly from the **low 32 bits of v
   alone** — plain wrapping 32-bit arithmetic. Hence `Motion_Hypot`: shift both coordinates down until each is under 2^15
   (the sum of squares then fits 32 bits), `ISqrt32` it, shift the root back, then one Newton step (`r0 + (v − r0²) / 2r0`,
   whose floor never lands below the true root) and a downward fix-up. Only 32-bit multiplies and one 32-bit divide, all
   already linked. Measured on adversarial inputs (60 M, worst truncation patterns, s = 7): the true residual is at most
   0.85 × 2^32, and the fix-up loop runs at most once.
4. *Can the top 8 bits be assumed zero?* Yes, and that is what the saturation is for: each component is clamped to
   `CORELR_MATH_48_LIMIT_UM` (2^22 − 1) on entry. Without it a coordinate beyond the bound makes the fix-up loop run for
   billions of iterations; with it the failure is a wrong length, never a hang (tested: `INT32_MIN`, `INT32_MAX`, 5 m).

**What changes under `CORELR_MATH_48`**
| | `_64` | `_48` |
|---|---|---|
| `Motion_Hypot` | `ISqrt` of a 64-bit sum of squares | the residual method above |
| `Motion_ISqrt(uint64_t)` | the 64-bit Newton root, 348 bytes | **absent** (nothing to take the root of) |
| `Trig_Angle`'s ratio b/a | `(uint64_t)b << 32 / a` | `RatioQ32`: 32 steps of restoring division, ~35 bytes |
| calibration height √(L² − h²) | 64-bit squares and `Motion_ISqrt` | bisection on `Motion_Hypot` (22 steps, ~4 ms, once), exact by a final test on the low words |

Unchanged in both: `Trig`'s Q32 products and `IdealSubSteps` keep the 32×32→64 widening multiply — libgcc's is the smallest
implementation there is (see 2). Those two, and `__lshrdi3`, are why some 64-bit helpers stay linked in `_48`.

**Measured** (Nano fqbn, `arduino-builder`, the whole image; cycles on `tools/analysis/cyclesim/t_hypot.c`):
| | `_64` | `_48` |
|---|---|---|
| flash | 30 848 (100 %: **does not fit**) | **30 588** (99 %, 132 bytes free) |
| RAM | 1 350 | 1 350 |
| worst stack | 326 | 318 |
| `Motion_Hypot`, mean of 12 inputs from 40 µm to 4.19 m | 5 038 cycles | **1 772 cycles** (÷ 2.8), 853 … 2 278 |
| `Motion_Hypot` code alone (cycle harness image) | 1 338 bytes | 1 076 bytes |

Neither is slower in the worst case; the arithmetic result is identical wherever both are defined.

**Verified** (`test_arith.c`, `test_position.c`, run by `run_all.sh` under both):
- `Motion_Hypot`: **12.2 M vectors** inside |x|, |y| < 2^22 — edges, every exact square ±1 up to 2^22, 233861 Pythagorean
  triples, random at every magnitude and sign — all exact against a 64-bit reference; `_64` also 200 000 over the whole int32
  range; `_48` the saturation cases.
- The calibration height: 2 015 machines (random, Pythagorean, L = D/2 ± 2, L < D/2), 0 wrong.
- Every arc test (`test_arc`) and the parser, which compute lengths through `Motion_Hypot`.
- **Mutation testing**, 14 broken copies of the 48-bit code (13 caught, 1 equivalent): no fix-up, shift bound 2^16 and 2^13, either saturation missing,
  root not shifted back, the early exit dropping y, a wrong Newton divisor, the scaled v used as the residual, a forgotten
  `abs`, three in the calibration (no exactness step, an off-by-one bisection, L < h unhandled), the division one step short
  — **caught**. (A "carry ignored" mutant of the division is the equivalent one, because the carry cannot occur: `Trig_Angle` passes
  components under 2^22, so 2r < 2^23. The unreachable test was removed instead of kept untestable; the precondition is written
  at `RatioQ32`.) Two mutants first looked uncaught only because the broken binary crashed and the script counted `FAIL`
  lines; a crash is a detection, and the script now says so.

**Closed 2026-10-08.** `_48` makes 2^22 µm (4.19 m) a real limit of the arithmetic, where `_64` accepted any int32, and nothing
refused a GCode target beyond it. The maintainer asked for the parser check: `REQ-CoreLR-F-GCodeInterpreter-0011` makes a drawing
coordinate of 2^21 µm or more an invalid line (`REQ-CoreLR-D-GCodeParser-0010`) — half the arithmetic's bound, since the
drawing-origin offset is added before the lengths are taken. The saturation in `Motion_Hypot` stays as the backstop.

## REQ-CoreLR-D-Motion-0025 — The drawing frame has Y up (implemented 2026-10-08)
<a id="REQ-CoreLR-D-Motion-0025"></a>
Realizes `REQ-CoreLR-F-GCodeInterpreter-0014`; request FW-01 and decision D1 of `docs/protocol/SerialProtocol.md`. Closes the open item "Orientation of the
Y axis" below.

**The flip, once.** `Y_internal = WorkOriginOffsetY − Y_drawing` (it was `+`). It is written in three places, all in `Motion.c`, and they
are the same conversion read in three directions: `Motion_XYToLR()` (drawing → cable lengths), `Motion_GetPersistedState()` (drawing → the
stored *internal* position) and `Motion_Restore()` (stored → drawing). Nothing else knows: the parser, the planner, the generator, the arcs, the
status screen all work in the drawing frame and are untouched.

**Arcs need nothing.** They are generated in the drawing frame and every waypoint is transformed on its own (`-0022`). `G2` is clockwise *in the plane of the
numbers*; with `+Y` up that is clockwise on the wall, as the convention requires. Swapping `G2`/`G3` and negating `J` (what a mirror of the
*internal* frame would have needed) is not done. (The acceptance test of FW-01 in `docs/protocol/SerialProtocol.md` said the opposite — `G3` from (0,0) to (20,0), `I10`,
"above" — and was wrong: the real parser, planner and generator give Y from −9.999 mm to 0 for that line, i.e. **below**, which is what counter-clockwise
means going from the left of the centre to the right. `G2` is the one that goes above. Corrected there.)

**A build option, `CORELR_Y_UP` (`SwConfig.h`, default 1; 2026-10-08, maintainer's request).** `0` gives the previous orientation (`+Y` down the wall, `Y_internal = offset + Y`),
so that the flip can be taken back without touching the code if the bench says so. The macro acts in two places only: `MOTION_Y_TO_INTERNAL` / `MOTION_Y_FROM_INTERNAL`
in `Motion.c`, used by the three conversions, and the sign of the Control jog. A value other than 0 or 1 is an `#error`. It is *not* a runtime setting and not the
joystick's polarity: if the bench shows that `dy > 0` is the stick *up*, the jog sign has to change in **both** settings, which would be a separate option.

**The stored position stays valid.** The EEPROM image holds the *internal* position (`Motion_GetPersistedState()`), and the physical point it names is
unchanged by the flip: an image written by the previous firmware restores to the same place on the wall, now with the opposite sign of Y in the drawing
frame (test: `test_position.c`).

**The joystick.** The Control jog moves the target by `ScaleJogDeflection(dy)`; it is now **subtracted** from Y, so the pen goes where it went
before (`dy > 0`, which the menu takes as "next item", is the stick pushed down). **Settled on the bench, 2026-10-10**: the maintainer's unit read the other way
round in X and in Y (the jog was inverted on both axes). The polarity of each axis is now a hardware setting, `LCDJOYSTICKUI_X_INVERT` / `_Y_INVERT` in `HwConfig.h`
(`REQ-CoreLR-D-LcdJoystickUI-0020`), so that `LcdJoystickUI_ReadAxis()` is positive for a stick pushed right / down whatever the unit does; this requirement's
`dy > 0` = "stick down" is therefore true by construction, and the test (`test_ui.cpp`, 4a2) pins the relation between the *physical* stick and the pen for every combination of the flags.

**Cost, measured** (Nano fqbn): **0 bytes**, in either setting (an operand order; 30 036 before and after, RAM and stack unchanged).

**Verified.**
- `test_arith.c`: `Motion_XYToLR` against the exact formula with the flip, and as a physical fact — `+Y` shortens both cables, `−Y` lengthens both, `+X`
  lengthens L and shortens R. `test_position.c`: the persisted internal position is `offset − Y`; an old-firmware image restores mirrored. `test_ui.cpp`:
  stick right/left/up/down give the four targets. Seven broken copies of the three sites and one of the jog (`tests/mutation/mutants/y_flip.txt`): **all caught**, each by the test
  that owns it. With the option, the batch is run in both settings: 11 mutants (the macros' two branches, each conversion bypassing its macro, both jog signs); every one that is
  not equivalent in the setting it runs under is caught (`tests/mutation/mutants/y_flip.txt` says which combinations to read together). The tests take their expectations from `CORELR_Y_UP`
  and the whole suite passes with `CORELR_Y_UP=0` and `=1`, under both arithmetics.
- **`CORELR_Y_UP=0` reproduces the previous firmware exactly**: `tools/bench/stream_hash.sh -r aec8718 -C CORELR_Y_UP=0` (the commit before the flip, the working tree with the option off):
  `test_motion` **identical** (820 385 entries, `3a04d9a6`) **and `test_arc` identical** (11 421 789 entries, `a9cfe556`) — the arc rounding asymmetry below does not appear, since no
  scenario is mirrored. With the default (1) the same command says DIFFERENT, as it must.
- **The flip changed nothing but the sign**, by the mirror twin of `tools/bench/stream_hash.sh -C MIRROR_Y` (`tests/host/mirror_y.h`: the same scenarios fed to the new firmware
  as their mirror image — `Y → −Y`, `J → −J`, `G2 ↔ G3`, read-backs mirrored — against the old firmware on the plain ones). `test_motion`: **identical**
  (820 385 entries, `3a04d9a6`). `test_arc`: same number of entries (11 421 789), **not** the same hash: 33 278 entries differ (0.3 %), 167 of them in the coil bits (a step one entry
  earlier or later), the total hold time identical to the tick. Cause, found by dumping every waypoint (3 936 741 of them): **10 018 waypoints differ by exactly 1 µm, never more, between an
  arc and its mirror image**, and the same 10 072 bytes differ when the *old, unchanged* generator is run on the plain and on the mirrored scenario — the arc generator's
  own rounding is not mirror-symmetric (negative numbers round the other way). Run on the same mirrored input, **the new firmware's waypoints equal the old firmware's, byte for byte (0 of 31 493 928)**: the flip adds nothing.
  So a file is drawn mirrored top to bottom relative to before, up to this 1 µm of rounding in some arc waypoints.

## REQ-CoreLR-D-Motion-0026 — The halt ramp completes only while Stepper accepts motion (found on the bench 2026-10-10, fixed 2026-10-10)
<a id="REQ-CoreLR-D-Motion-0026"></a>
Amends `-0012`. `Motion_IsHaltRampDone()` is true once the ramp's last entry has been **accepted** by `Stepper_RequestMotion()` (`OutValid` / `HavePending` clear). A refused entry stays in the outbox, so a Stepper that refuses motion from the moment of the
halt makes the ramp unfinishable. `grbl_corelr.ino` used to close the gate (`Stepper_HaltRequests()`) in `BeginRampedStop()`, next to `Motion_Halt()`: the machine stopped dead at the last entry already queued, and the job state stayed in
HALTING / CANCELLING / ERRORSTOP for ever.

**What the bench showed** (Candle sending a file; the maintainer's sequence): pause from Candle (`!`) → the state never reached PAUSED, Candle read `Hold:1` and greyed its pause/resume button; `~` from Candle and *Resume* from the LCD did nothing (both act
only in PAUSED); *Cancel* from the LCD went to ERRORSTOP, which could not complete either (`Alarm` on both displays, no way to acknowledge: the acknowledgment acts only in ERROR, and the menu is closed in error); and since the position keeper stores only in IDLE / PAUSED / ERROR, a
reset after that found the position invalid: **calibration lost**. One cause, four symptoms.

**The rule now:** the gate stays open during the ramp and closes at its end -- in the JOB_HALTING and JOB_ERRORSTOP cases of the ramp-completion switch; a finished cancel (JOB_CANCELLING) *opens* it (it used to stay closed after a cancel from a running job, which would also have killed the jog; `JobStart` and `JobAckError` reopen it as before).
Nothing moves in between: the planner is halted (`s_planHalted`) and the generator stops at the virtual end.

**Verified:** `test_motion.c` section 6b -- with the gate closed together with the halt the ramp is never done (20 000 calls), with the gate closed after it the ramp is done, stays done, no further entry is produced, and `Motion_Resume()` finishes the move;
`tools/analysis/check_halt_order.py` (in `run_all.sh`) fails on the previous sketch and passes on the fixed one, because the sketch cannot be run on the host. **Not verified on the machine** -- see `docs/bringup/Bringup-Status.md`.

## REQ-CoreLR-D-Motion-0027 — A resumed move starts a fresh timeline; no hold is ever negative (found on the bench 2026-10-10, fixed 2026-10-10)
<a id="REQ-CoreLR-D-Motion-0027"></a>
Amends `-0012` (resume) and `-0026`. After `-0026` the pause completed, but *resume* left the job in `Run` with the machine frozen. The bench probe (`tools/bench/serial_probe.py`, with the `CORELR_DEBUG_JOBSTATE` field `|D:…>` in the status report) showed the step queue armed,
a backlog pending and the queue draining one entry in 30 s: **the first entry after the resume had a hold of 4 294 118 081 counts, about 36 minutes.**

**Cause.** The halt ramp ends with `FlushPending()`, which sends the pending entry "held to the end of the timeline" and starts a fresh one: `TSeg = 0`, `AnyEvent = 0`, `HavePending = 0` -- but it leaves **`PendTime` and `LastEvent` from the old timeline**. A *move* initialises all of them in
`StartMove()`; a *resume* did not, so the first event after it computed `hold = TSeg + t - PendTime` with a stale `PendTime`: negative, i.e. ~2^32 as `uint32_t`. A second, silent consequence: `HavePending` stayed clear for the rest of the move, so the move's **last entry was never flushed**.
No host test could see it: they checked the final position, not the durations (the entries were consumed instantly), and the AVR simulator likewise.

**Fix.** `BeginTimeline()` -- the initialisation `StartMove()` always did, now one function -- is also called by `Motion_Resume()`. And `FlushPending()` never produces a negative hold (`TSeg > PendTime ? TSeg - PendTime : 0`): `Motion_Abort()` may call it mid-segment, where `TSeg` is the segment's *start* and the last event is after it (found by the new test, in an abort that no real path makes -- the sketch aborts only after the ramp -- but one the function permits).
Measured side effect: flash **−56 bytes** in both variants (the shared helper), serial 26 442 → 26 386, SD 30 628 → 30 572.

**Verified.** `test_motion.c`: after a resume no hold exceeds 100 000 counts (50 ms; was 4.29e9) and the move produces entries; one check over **every entry of every section** (`g_maxHold` < 400 000, the largest is 99 000 = 49 ms); 6b and 6c as above. Streams: `tools/bench/stream_hash.sh` differs from the previous code in `test_motion` and `test_arc`
only through their halt/resume scenarios (a resume now emits its last entry; the entry counts moved by 0.7 % and 0.4 %); no scenario without a halt changed (`BeginTimeline()` is the old code, moved). **Not yet confirmed on the machine:** see `docs/bringup/Bringup-Status.md`.

## REQ-CoreLR-D-Motion-0028 — The travel zone (implemented 2026-10-11)
<a id="REQ-CoreLR-D-Motion-0028"></a>
Realizes: `REQ-CoreLR-F-Motion-0008`. In the **internal frame** (origin at Motor L, X towards Motor R, Y down, `D` the motor distance), a position is allowed iff
`MOTION_MARGIN_SIDE_UM <= X_int <= D - MOTION_MARGIN_SIDE_UM` and `Y_int >= MOTION_MARGIN_TOP_UM` (`SwConfig.h`: **20 000** and **50 000** um). In the drawing frame, with the drawing-origin offset `(ox, oy)`:
`side - ox <= X <= D - side - ox`, and `Y <= oy - top` with `CORELR_Y_UP` (`Y >= top - oy` without it) -- one function, next to `Motion_XYToLR()`, that knows both frames.
- `Motion_TravelAllows(p)` -- a pure query, 1 if `p` is inside. Without a known geometry (`D <= 0`: uncalibrated) there is no zone to apply and it says 1; the empty zone (`D < 2 * side`) allows nothing.
- `Motion_TravelClamp(p)` -- moves `p` to the nearest point of the zone, axis by axis (the zone is a rectangle open downwards); returns 0, `p` untouched, for an empty zone. Both are one static function, `TravelCheck(p, clamp)` (the limits worked out once; two helpers cost 300 bytes).
- `Motion_QueueJog(target)` -- the joystick's slice: `Motion_QueueMove()` of the clamped target, nothing queued (returns 1: nothing to retry) when the clamped target is where the queue already ends, or the zone is empty; 0 only when the planner is full. The pen stops at the edge and slides along it.
- **Lines:** the zone is convex, so the end point suffices (the start is the previous target, or a position the machine already has). **Arcs:** `Motion_ArcCheck()` also refuses an arc **any** point of which is outside: both end points, and every one of the four extreme points of the circle
  (centre `+-` radius along X and Y, the larger of the two radii) that **lies on the arc** (its angle, relative to the start in the arc's direction, is within the sweep: unsigned 32-bit angle arithmetic of `Trig_Angle()`, exact).
- **A start outside the zone** does not trap the machine: the check is on where the move goes, and a line whose end point is inside is accepted wherever it starts; an arc must lie wholly inside, so none is accepted from outside.
Order of the checks, in `GCodeParser`: reach (`-0011`), then the zone, then the arc radius rule. Callers: `GCodeParser` (lines, arcs, `$J=`), `LcdJoystickUI` (the Control jog, through `Motion_QueueJog()`). Not in the path of the per-motor Adjustment jog (the position is unknown then) nor of "Return home" (the drawing origin is inside the zone by construction, `REQ-CoreLR-D-Calibration-0011`).
The status protocol's side (`docs/protocol/SerialProtocol.md`, owned by the host side) is not edited here: `$J=` and G-code lines outside the zone answer `error:33`, the number already given to an unreachable target.
Verification: `tests/host/test_travel.c` (the rectangle to the micrometre in both Y conventions, clamp, empty and absent zone, a zone that follows the drawing origin, G-code lines, `$J=`, a start outside, arcs -- bulge, direction, sweep, the two radii --, `Motion_QueueJog`) and `tests/host/test_calzone.c`, plus `test_ui`; the 51 mutants of `tests/mutation/mutants/travel_zone.txt` are all caught by at least one of them (`test_travel` in both Y settings, `test_calzone`, `test_ui`).
**Measured cost (2026-10-11, Nano, `build_size.sh`): +916 bytes of flash in the serial variant (26 474 -> 27 390), +904 in the SD variant (29 608 -> 30 512, 208 free), RAM +1 (SD 1 344), worst stack 314 -> 321 (SD)**; where it goes: `TravelCheck` 302, `ArcLeavesZone` 230, `Motion_QueueJog` ~100, `Motion_ArcCheck` +116, `Calibration_SetParams` +84. The 32-bit comparisons dominate (~24 bytes each on AVR). Tried and not kept: the limits in the internal frame (+20), `NOINLINE` on the wrappers (+55). Open: the arc bulge test alone is about 350 bytes; if the SD variant needs the room, a build switch could drop it (a bounding-box refusal of the whole circle would be the cheap, conservative stand-in -- it refuses valid arcs of large radius, which is why it was not chosen).


## REQ-CoreLR-D-Motion-0029 — Moving the origin under the pen (implemented 2026-10-11)
<a id="REQ-CoreLR-D-Motion-0029"></a>
Realizes: `REQ-CoreLR-F-Calibration-0010`. `Motion` owns the drawing frame, so it owns the change: one function, `uint8_t Motion_SetOrigin(uint8_t what, int32_t x, int32_t y)`: `what` is `MOTION_ORIGIN_X` and/or `MOTION_ORIGIN_Y` (the current position reads x / y on those axes; the other keeps its offset) or `MOTION_ORIGIN_REFERENCE` (the origin goes back to the reference point; x and y unused) -- one function rather than
two, which the link-time optimizer showed to be 326 bytes smaller. It (1) requires `Motion_IsIdle() && Motion_PlannerIsEmpty()` -- otherwise 0, nothing done --, (2) take the internal position of the pen from the drawing position and the old offsets (the inverse of `Motion_XYToLR()`'s first step, Y flip included), (3) work out the new offsets (`offsetX' = internalX - x`; `offsetY' = internalY + y` with `CORELR_Y_UP`, `internalY - y` without),
(4) have `Calibration_SetWorkOrigin()` accept them (the zone rule, `REQ-CoreLR-D-Calibration-0012`), and only then (5) rewrite the drawing position of the pen so that the same internal point reads (x, y). The step counts, the commanded steps and the persisted internal position are **not** touched: the pen does not move. Return 0 if refused, 1 if done.
Tests: `tests/host/test_origin.c` (in both Y conventions: the pen's cable lengths and the persisted internal position do not change, the new offsets to the micrometre, each axis alone, the zone limits of the origin to the micrometre, the zone moving with the origin, refused while a move is planned **or being generated**, back to the reference, G92 / G10 lines).
**Measured cost of the whole feature** (menu, G92/G10, Motion, Calibration; Nano, with the size flags): **+944 bytes of flash on the SD variant (29 208 -> 30 152, 568 free) and +956 on the serial one (26 626 -> 27 582)**, SD stack unchanged at 334, serial 317 (+3); one function instead of two saved 326 bytes of the first draft (30 488). 47 mutants (`tests/mutation/mutants/origin.txt`), each caught by at least one of `test_origin` (both Y settings), `test_calzone`, `test_ui`, `test_parser`.


## REQ-CoreLR-D-Motion-0030 — The machine offset (implemented 2026-10-11, serial variant)
<a id="REQ-CoreLR-D-Motion-0030"></a>
Realizes `REQ-CoreLR-F-SerialControl-0013`. `Motion_GetMachineOffset()` returns WCO: with `R` the reference point in the drawing frame (`X: refX - offX`, `Y: MOTION_Y_FROM_INTERNAL(offY, refY)`), **WCO = -R**, so that MPos = drawing + WCO and WPos = MPos - WCO. One place does the Y flip (`MOTION_Y_FROM_INTERNAL`), so `CORELR_Y_UP=0` follows. Computed on demand from the two stored values, nothing kept.

## Open items
- **Function decomposition — to be settled during this module's review**
  (agreed 2026-09-09). This module currently fuses what grbl separates
  into `motion_control` (segment/arc generation), `planner` (velocity
  profile) and part of `stepper` (step generation), because the
  nonlinear inverse transform must be applied *per waypoint*, between
  Cartesian interpolation and step generation — grbl's split assumes
  independent linear axes and does not transfer directly. Splitting it
  grbl-style would push waypoints across a module boundary at ~53µm
  granularity. Whether a finer split is nonetheless worthwhile (and
  along which seam) is a review question, not settled here.
  `-0010` constrains the *documentation* regardless of the outcome.
- **Acceleration: three distinct problems, analysed 2026-09-09.** Raised
  by the maintainer ("in V1 we slow down a lot before changing
  direction"). Verified against the code — it is broader than that, and
  the placeholder constants hide it:

  **(a) Deceleration happens at *every* segment end, not just at
  direction changes.** `ComputeHoldTicks()` derives `distanceToEnd` from
  the **current segment's** remaining steps, so velocity is forced down
  to `MOTION_VMIN_UM_PER_TICK` at the end of every GCode line — even
  between two perfectly collinear `G1` moves, which stop in the middle
  for no geometric reason. This is the documented consequence of having
  no cross-segment look-ahead in V1, but the wording "before changing
  direction" understates it.

  **(b) FIXED 2026-09-10 — the acceleration unit is now nm/tick²**
  (`MOTION_ACCEL_NM_PER_TICK2`, default 12 ≈ 192 mm/s², ramp ≈ 4.2 mm at the then `VMAX` of 40 mm/s; 1.2 mm at the present 21.5).
  The profile formula became `v² = 2·a_nm·d / 1000`, dividing last so
  the integer intermediate keeps its precision. The original problem,
  for the record: `MOTION_ACCEL_UM_PER_TICK2` could not express a
  realistic acceleration. At 4 kHz, `1 µm/tick²` = **16 m/s² (1.63 g)** — and 1
  is the smallest non-zero integer the unit allows. A plausible value
  for a 28BYJ-48 turning a cable spool (100–500 mm/s²) would need
  0.006–0.031 µm/tick², i.e. **zero** in integer arithmetic. This is a
  unit-scaling defect, not a value left to be tuned: no correct setting
  exists in the current unit.

  *Applied:* acceleration in nm/tick² (1000× finer) — 100–500 mm/s²
  becomes 6–31, comfortably representable.

  **(c) (open then; closed by the look-ahead of `-0013`) — now that acceleration is realistic, (a) is the
  dominant problem.** Ramp length to reach `VMAX` (40 mm/s then; at the present 21.5 mm/s: 0.5 mm at 500 mm/s², 1.2 mm at 200, 2.3 mm at 100):

  | Acceleration | Ramp to VMAX |
  |---|---|
  | 1 µm/tick² (current placeholder, 16 m/s²) | 0.05 mm ≈ 0.9 grid step |
  | 500 mm/s² | 1.6 mm |
  | 200 mm/s² | 4.0 mm |
  | 100 mm/s² | 8.0 mm |

  Until 2026-09-10 the placeholder made the ramp shorter than one grid
  step, so the trapezoidal profile was effectively invisible — which is
  why the per-segment stop had not been noticed. With the value now in
  use the
  ramp becomes **longer than a typical plotter GCode segment**
  (0.5–5 mm for a curve approximated by line segments), so most segments
  would never reach `VMAX` at all: accelerate, decelerate, repeat. That
  is the jerky, slow behaviour the maintainer anticipated.

  *V2 direction:* this is exactly what grbl's `planner` solves —
  junction-deviation look-ahead across a buffer of upcoming blocks,
  computing an entry/exit speed per junction from the angle between
  segments instead of forcing zero. Adopting it here needs care: grbl
  computes junction angles in linear axis space, whereas our segments
  are Cartesian but the *actuators* are cable lengths, so the velocity
  limit at a junction is not simply the Cartesian one (see the
  decomposition open item above — the same nonlinearity).
- Exact worst-case timing budget between the Cartesian planner and the
  waypoint generator has not been analysed yet.
- Arc (`G2`/`G3`) midpoint-circle algorithm details — deferred with arc
  support itself (`REQ-CoreLR-F-GCodeInterpreter-0003`).
- **`isqrt` future optimization** (discussed 2026-09-07, deliberately
  not done): the sum-of-squares *input* genuinely needs 64-bit range
  for any realistic work area — not avoidable. But `Motion_ISqrt`'s
  starting bracket (currently `2^62`, correct for the full `uint64_t`
  domain) could skip straight to the largest power of 4 below the
  machine's actual maximum reach once real dimensions
  (`MotorDistanceUm`, work-area limits) are known, cutting loop
  iterations with no precision or convention trade-off. A `float`-based
  sqrt was considered and rejected instead: AVR emulates `float` in
  software too (no inherent speed advantage over integer), the actual
  comparison is library/version-dependent, and it would break the
  no-floating-point convention held everywhere else in this pipeline
  for an unproven gain on a per-waypoint (not per-ISR-tick) computation.
- ~~Orientation of the Y axis~~ — **decided and implemented 2026-10-08**: the drawing frame has `+Y` up the wall, flipped once in
  the drawing-origin transform (`REQ-CoreLR-D-Motion-0025`, decision D1 of `docs/protocol/SerialProtocol.md`).
