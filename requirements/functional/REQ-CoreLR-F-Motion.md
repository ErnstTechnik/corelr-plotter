# Functional Requirements — Motion

Sub-function: **Motion** — plan and execute 2D pen paths (segments, arcs)
accurately, without positional drift, within acceleration limits.

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06)
See also: [`../Design/REQ-CoreLR-D-Motion.md`](../design/REQ-CoreLR-D-Motion.md)
(how this sub-function is currently realized)

## REQ-CoreLR-F-Motion-0001 — Path accuracy
<a id="REQ-CoreLR-F-Motion-0001"></a>
The system shall move the pen along commanded 2D paths — straight
segments and circular arcs — matching the commanded geometry.

## REQ-CoreLR-F-Motion-0002 — No positional drift
<a id="REQ-CoreLR-F-Motion-0002"></a>
The system shall not accumulate positional drift over the course of a
job, regardless of path complexity (chained relative moves, arcs, long
jobs).

## REQ-CoreLR-F-Motion-0003 — Controlled acceleration
<a id="REQ-CoreLR-F-Motion-0003"></a>
Motion shall follow a defined acceleration/deceleration profile (no
instantaneous velocity changes), computed in Cartesian coordinates.

## REQ-CoreLR-F-Motion-0006 — Continuous motion across joined moves (2026-09-10)
<a id="REQ-CoreLR-F-Motion-0006"></a>

Consecutive moves that continue in the same or a similar direction
shall be traversed **without coming to a stop between them**. The
system shall slow at a junction only as far as the change of direction
requires.

Confirmed by the maintainer 2026-09-10, after measurement showed the
per-segment stop was not a comfort issue but a 5-8x speed penalty on
realistic files -- an SVG-derived GCode file is a dense polyline of
0.2-1 mm segments, none of which came near the machine's maximum speed.

Realized by [`REQ-CoreLR-D-Motion-0013`](../design/REQ-CoreLR-D-Motion.md).

## REQ-CoreLR-F-Motion-0007 — Arc accuracy and speed (specified 2026-10-07)
<a id="REQ-CoreLR-F-Motion-0007"></a>
- **Accuracy.** The pen path shall stay within **2 µm** of the commanded circle (the chord error, the same as
  grbl's default `ARC_TOLERANCE` of 0.002 mm), plus the radius blend of `REQ-CoreLR-F-GCodeInterpreter-0009`; the
  start and end points shall be exact. The machine's own resolution is 54 µm, so this is a bound the machine
  cannot show, chosen because it is free of cost in speed and cheap in code.
- **No drift.** Every point of an arc is computed from the arc's definition by an absolute method, never by
  adding deltas (`REQ-CoreLR-F-Motion-0002`; the maintainer's rule for circles).
- **Speed.** The pen's speed on an arc of radius `r` shall not exceed `√(a·r)`, `a` being the machine's
  acceleration (`REQ-CoreLR-D-Motion-0020`, 192 mm/s²) — the centripetal limit: 43 mm/s at a 10 mm radius
  (so `VMAX` binds), 13.9 mm/s at 1 mm, 6.2 mm/s at 0.2 mm. The floor `VMIN` still applies.
- **Continuity.** An arc joins its neighbours like any other move: it is traversed without stopping when the
  direction change at the joint allows it (`REQ-CoreLR-F-Motion-0006`), its tangent at each end being the
  direction used.
- **Degenerate arcs.** An arc whose radius is under one motor step (54 µm) is a point to this machine: it is
  replaced by a straight move to the end point.

## REQ-CoreLR-F-Motion-0008 — The pen stays where the cables can hold it (specified 2026-10-11)
<a id="REQ-CoreLR-F-Motion-0008"></a>
The pen hangs from two cables that can only **pull**. Beyond the vertical of a motor both cables pull the same way sideways, so nothing holds the pen on the outer side and it swings back; at the level of the motors the cables are horizontal and cannot carry its weight (the tension of
each grows without bound as the pen nears that line, see the tension charts, `tools/generators/gen_tension_chart.py`). The system shall therefore never command a position outside a **travel zone**:
- **sideways**, between the two motors, at least a **side margin** (default 20 mm) inside the vertical of each motor;
- **upwards**, at least a **top margin** (default 50 mm) **below** the line joining the motors. Downwards the zone is not limited by the geometry (the cable length and the wall do).
Both margins are build-time settings. This applies to every way the pen is moved: job lines and arcs, jogs from the serial link and from the joystick. A move whose end point is outside the zone is refused **before anything moves**; an arc is refused if **any point of it** is outside. A position
that is already outside the zone (a margin raised after calibration) shall not trap the machine: a move **to a point inside the zone** is always accepted. The joystick jog does not refuse: the pen stops at the edge of the zone and slides along it.


## Open items
- Acceptable accuracy/drift tolerance is not yet numerically specified
  (e.g. maximum deviation in mm over a job of a given duration).
