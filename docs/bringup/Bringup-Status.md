# Hardware bring-up — status and next steps

Written 2026-09-11, at the end of a bench session. Everything here is
measured unless it says "hypothesis".

## Where the machine is

Motors, LCD, joystick and servo are wired. This section is the state of the first bench sessions; the dated sections below record what has run on the machine
since (both variants, plots, arcs, the position ring).

Confirmed working on hardware (first sessions):

- **L/R adjustment jog** — smooth, full torque, `miss:` stays at 0.
  Unchanged by the last commit, as expected.
- **Motor limit, measured**: 600 steps/s unloaded, **400 steps/s under
  load**. Everything is tuned against 400.
- A move tangential to one motor now leaves that motor genuinely still,
  with no phase flicker — the chatter of `REQ-CoreLR-D-Motion-0015`
  is gone.

## Bench observation (first sessions) — the Cartesian jog got SLOWER; resolved by the v2 design

Reported after `VMAX` was raised from 20 to 21.5 mm/s (the jog should
have got marginally *faster*), and **not predicted**.

### Hypothesis (untested): waypoint generation is now CPU-bound

`MOTION_GRID_SUBDIV` went 4 → 8 in the same commit, which **doubled the
queue entries needed per unit distance**:

| grid | entries/s at `VMAX` | `loop()` passes/s needed |
|---|---|---|
| 1/4 | 1796 | 112 |
| 1/8 | **3593** | **225** |

Every waypoint costs `Motion_XYToLR()`, which is **two calls to
`Motion_ISqrt()` on a `uint64_t`** — expensive on an 8-bit AVR, plausibly
a four-figure cycle count each. At 3593 waypoints/s that is ~7200 square
roots per second, which could plausibly exceed what is left of a 16 MHz
ATmega328P after the LCD, the joystick and the planner.

If waypoint production cannot keep up, the achieved speed is set by
throughput rather than by `VMAX` — and it would have been *faster* at
grid 1/4. That matches the report exactly.

### Measured 2026-10-06 (simulator, not yet bench): the hypothesis holds, with a 7× margin

Measured on the real compiled `Motion.c` (Arduino flags, Nano toolchain)
in `tools/analysis/cyclesim/` — a cycle-counting ATmega328P interpreter, validated
by an identical queue-entry stream against an x86 build of the same source
and by `ISqrt` matching `math.isqrt`. Machine dimensions assumed 1 m /
700 mm (no real calibration exists); the 4 kHz ISR and the LCD are **not**
included, so every rate below is an upper bound.

| | cycles |
|---|---|
| **one waypoint, `MotionStepOnce()` as it stands** | **33 300** (2.08 ms) |
| of which `Motion_ISqrt` + its 64-bit helpers (`__lshrdi3`, `__cmpdi2`, …) | ~75 % |
| one `Motion_ISqrt(uint64_t)` on an L/R-sized input | 6 800 |
| one `Motion_ISqrt(uint64_t)` on a v²-sized input | 5 500 |

**Four** square roots per waypoint, not two: `Motion_XYToLR()` calls it
twice and `ComputeHoldTicks()` twice more (`vFromAccel`, `vFromDecel`).
The 64-bit divisions (`/1000` twice, `IdealSubSteps` twice) are most of
the rest.

So at 100 % of the CPU, Motion alone can produce **481 waypoints/s**.

| grid | waypoint | needed at `VMAX` | ceiling (481 wp/s) | needed ÷ ceiling |
|---|---|---|---|---|
| 1/4 (before) | 13 µm | 1650/s | 6.3 mm/s | 3.4× |
| 1/8 (now) | **6 µm** | 3583/s | **2.9 mm/s** | 7.4× |

(`MOTION_STEP_UM / MOTION_GRID_SUBDIV` is an integer division: 53/8 = 6,
not 6.7.) Per-waypoint cost does not depend on the grid — measured 33 279
at 1/4 and 33 259 at 1/8 — so the grid only sets how far each waypoint
goes. Halving it halved the ceiling, from 6.3 to 2.9 mm/s: that is the
regression, and it is independent of `VMAX`, which is why raising `VMAX`
did not help. The machine has been CPU-bound on the Cartesian jog since
the grid first went below one motor step.

**Consequence for the jitter figures.** The 13.5 → 7.2 % jitter numbers
for the finer grids were host simulations of an *ideal* waypoint stream.
The CPU cannot produce that stream, so they were never achievable on the
board.

Square-root alternatives, measured the same way (all exact against
`math.isqrt` on the test inputs):

| | cycles |
|---|---|
| current `ISqrt(uint64)`, L/R-sized | 6 838 |
| same with a lower start bit (result < 2²²) | 6 330 (−7 %) |
| Newton, seeded with the previous root | **2 164** (÷3.2) |
| current `ISqrt(uint64)`, v²-sized (profile) | 5 488 |
| 32-bit `isqrt32`, v²-sized (profile) | **615** (÷9) |

Applying both would bring a waypoint to roughly 14 000 cycles, i.e.
~1100 waypoints/s: still short of 3583 (1/8) and of 1650 (1/4). **No
square-root optimisation reaches the needed rate at a 6–13 µm grid.**
A coarse waypoint (≥ 215 µm, 100/s) costs 21 % of the CPU unchanged,
which is the argument for `NextAlgorithm-StepDecoupling.md`.

### Second finding: the jog does not feed the planner (found while reading)

`HandleJogControlInput()` gates on `Motion_PlannerHasRoom()` but then calls
`Motion_RequestLinearMove()`, which **bypasses the planner**
(`StartSegment(target, 0, 0)`, rest to rest) and returns silently if a
segment is already in progress. A later change replaced the gate
`Motion_IsIdle()` to `Motion_PlannerHasRoom()` and `REQ-CoreLR-D-LcdJoystickUI-0017`
says the jog queues blocks, but the call was never changed to
`Motion_QueueMove()`. Effect: the planner is always empty so the gate is
always true, a slice arriving while a segment runs is dropped and its
deflection time is consumed, and every executed block is rest-to-rest —
a 0.6 mm block cannot exceed ~11 mm/s at 192 mm/s². Independent of
the `VMAX` change; hidden by the CPU limit above. **Fixed 2026-10-06**, when `Motion` was ported to
the decoupled design: `HandleJogControlInput()` now queues its blocks from the end of the queue, through the planner.

### Bench result, 2026-10-06 — and a separate defect it exposed

maintainer on the Cartesian jog: **~3 mm/s on the display, `miss` rises
quickly to 255** — the prediction above (2.9 mm/s) holds. Before that, an
unexplained symptom: *whatever the stick did, the head went straight up,
fast.* Cause, found by reading the code and confirmed in simulation:

`Calibration_Init()` restores the persisted *calibrated* flag, so the jog
menu is available at boot, but `Motion_Init()` sets the position to (0,0)
and the commanded step counts to **0**. Only `ConfirmCalibration()` seeds
them (`Motion_SetCurrentPosition` → `SeedCommandedSteps`). After a reboot
the ideal is ~13 000 steps against a commanded 0, and the hysteresis
closes that gap at **one step per waypoint, on both motors, in the same
direction** — the stick merely triggers waypoint production. Simulation:
599 coil changes out of 599 entries on each motor. After an explicit
recalibration the jog follows the stick (bench-confirmed).

Fixed 2026-10-06, implemented and bench-verified: `REQ-CoreLR-F-Calibration-0003`/`-0007`
and `REQ-CoreLR-D-Calibration-0006`…`-0008` (absolute step counts
persisted, an invalid marker written before any motion).

### How to confirm on the bench, in order

1. **Read `miss:` on the Cartesian jog screen** (compiled out by default since 2026-10-08: build with `CORELR_DEBUG_MISS_COUNT` set to 1, `REQ-CoreLR-D-Stepper-0015`). Row 1 shows
   `jog    miss:N` while that mode is active, and the counter is reset
   on entering it, so the figure describes this jog alone. Climbing =
   the queue is starving = hypothesis confirmed.

   (The counter existed only on the L/R adjustment screen until
   `2026-09-11`; the maintainer pointed out that this made the test
   as first written unperformable. Row 1 of the Control jog now carries
   it in place of the cable lengths, which are read standing still on
   the status screen anyway.)
2. Set `MOTION_GRID_SUBDIV` back to `4` and compare speed directly. If
   the jog speeds up, the hypothesis holds and the grid is the cost.
3. Time `loop()` — toggle a spare pin (A6/A7 are free on the Nano, see
   `PinMapping.md`) and measure the period on a scope.

### Candidate fixes, cheapest first

- **Incremental square root.** Between consecutive waypoints L and R
  change by at most one grid unit, so the previous result is an
  excellent seed: one or two Newton iterations instead of a full
  `ISqrt`. This is the strongest candidate — it attacks the per-waypoint
  cost directly and helps the DDA work below as well.
- **Raise `MOTION_STEPS_PER_TASK_MAX`** (now 16) so one `loop()` pass
  carries more waypoints. Cheap, but it only helps if the limit is pass
  *rate* rather than raw CPU.
- **Back off to grid 1/6 or 1/4**, trading jitter for throughput. The
  jitter numbers per grid are in `REQ-CoreLR-D-Motion-0017`.

## Build outside the IDE (done 2026-10-11): CMake + Ninja
The firmware is built by `cmake --preset avr-default && cmake --build --preset avr-default` (both variants; `ctest --preset host` for the host tests after `cmake --preset host`). The flags the IDE hid
are now in `cmake/Firmware.cmake`, `-mcall-prologues` (−1396 bytes of flash; the SD variant does not link without it: 31 058 against 29 608) is the option `CORELR_CALL_PROLOGUES`, the Arduino core is vendored in
`third_party/arduino-core/`, and the machine's tool paths live in `local.conf` (template `local.conf.example`). **Proof: the `.hex` of both variants is byte for byte the one `arduino-builder` produces**
(`tools/analysis/compare_with_ide.sh`, also with `CORELR_MATH=64`), so every figure in this file still holds; the size limits are enforced at link time (`cmake/CheckSize.cmake`, 15 mutants all caught). Design:
`requirements/design/REQ-CoreLR-D-Build.md`. **The upload targets (`upload_serial`, `upload_sd`) were not tried on the board**; the images they flash are the IDE's, which were.

### First upload from the CMake build (2026-10-11, serial variant): done, calibration kept
`cmake --build --preset avr-default --target upload_serial` (avrdude, 115200 baud, `/dev/ttyUSB0`): 26 474 bytes written and verified. Before it, the flash of the board was compared with this image
(`avrdude -U flash:v`): it **differed** (first mismatch at byte 0x0002, the vector table), i.e. another build was running. **After the upload the machine was still calibrated** (`Idle`, `MPos 0.000,401.493`, `Bf:8`), where the
first flash of the ring format was expected to cost a recalibration: the board therefore already held the ring's EEPROM format (flashed earlier from another build); the prediction made from the notes was wrong, the
measurement is what counts. The EEPROM could not be read back with avrdude (this bootloader returns flash for `eeprom:r`). **Still to check on the machine:** the ring itself (jog, wait 3 s, reopen the port, same place), as in "The position ring" above.

### The position ring on the machine (2026-10-11, serial variant): 10 reopenings, all OK
New probe scenario `tools/bench/serial_probe.py ring` (default 8 cycles): each session opens the port (DTR resets the board), checks `Idle` and the position left by the previous session (tolerance 0.05 mm), moves the head by a relative jog
of at most 10 mm (X and Y), waits 3 s of stillness and closes the port. Result: **every reopening came back `Idle` at exactly the position it was left** (10 sessions, 8 moves of 7 to 14 mm, then the return to the start and a last look: back at
X=0.000 Y=191.365, started there), 0 failures, 78 s. So the ring is read back after a reset, in the serial variant, on the real board. **Not covered here (see the next section for the wrap):** a reset
**before** the 2 s store (expected to lose the position, by design), the SD variant, power-cycling instead of a DTR reset.

### The position ring, 70 distinct positions (2026-10-11, serial variant): 72 reopenings, 0 failures
`serial_probe.py --cycles 70 --span 100 ring` (log: [`ring70-2026-10-11.log`](ring70-2026-10-11.log)): 70 **distinct** absolute targets, pseudo-random (fixed seed) within ±100 mm of the origin and at least 10 mm apart, so that a restored position cannot match by chance; after each, 3 s of
stillness, the port closed (DTR reset) and reopened. At each of the 72 reopenings the machine was `Idle` and **at the position it was left, to 0.05 mm**; each jog reached its target (to 0.3 mm); the machine was found at 70 different positions; it ended at the origin
(X=0.000 Y=0.000). 618 s, 0 checks failed. With about two records written per session (marker at start, position at stop) this goes through well over a hundred slots of the 64-slot ring, i.e. **it has wrapped round**
(not counted from the firmware side: the slot index is not visible over the link). Still not covered: a reset before the 2 s store (loses the position, by design), power-cycling, the SD variant.

## The travel zone (2026-10-11): host-verified; **serial variant: left, right and top limits tested on the machine (maintainer); SD variant: `04_TestPattern.gc` ran; the limits were tried there right after (maintainer)**
Margins: 20 mm from each motor's vertical, 50 mm below the line joining the motors (`REQ-CoreLR-F-Motion-0008`; cable tensions in `docs/hardware/CableTension.md`). Flash: serial 27 390 / 30 720, **SD 30 512 / 30 720 (208 free)**, SD stack 321. **With the pen's state on the LCD (`-0030`, +34 bytes): serial 27 424, SD 30 546 (174 free).**
On the machine, 2026-10-11 (maintainer): the SD variant runs from a card with sub-directories, navigation works, `04_TestPattern.gc` was running -- the first time the SD variant has been tried.
To check on the machine: (1) in the Control jog, push to the left edge: the head must stop 20 mm short of the left motor's vertical and slide along the edge when the stick is also pushed up or down; same on the right and at the top; (2) from Candle, jog towards an edge (`$J=` out of the zone answers `error:33`,
nothing moves); (3) `G1` to a point beyond an edge from a file: `error:33`, job halted; (4) a half circle whose ends are inside but whose bulge crosses an edge: refused; (5) in "Calibrate machine", enter a motor distance under 40 mm or a cable that puts the reference less than 50 mm below the line: the
screen must say `Ref out of zone!` and stay on step 2; (6) the test card (`tests/bench/gcode/04_TestPattern.gc`) must still run whole (it stays within +-100 mm of the origin).

### The pen on the LCD (2026-10-11): host-verified; **maintainer's bench, same day: both variants (SD and serial) flashed from the CMake build with the travel zone and the pen glyph, tested, "it works"** (which of the checks listed above were exercised was not itemised: say so here when it is)
Status screen, row 1 column 0: `v` pen down, `^` pen up (the physical state). To check: the character must follow the pen during a job (`^` while travelling, `v` while drawing), `^` at rest and in the Control jog, and nothing else on the screen may move.

### Testing the limits from the SD card: the zone files (2026-10-11)
**The maintainer's machine: D = 1065 mm, L = 800 mm** (calibration, 2026-10-11) -> origin 532.5 mm from the left motor, 597.029 mm below the line; zone X -512.5 .. +512.5, Y <= +547.029. The files for it are committed: `tests/bench/gcode_machine/` (its README says what each does and what to expect).

The test card stays within +-100 mm of the origin, so it never touches a limit. `tools/generators/gen_gcode.py --limits D,OX,OY` writes files for THE MACHINE'S geometry (D = motor distance, OX = distance from the left motor's vertical to the drawing origin, OY = how far below the line joining the motors
the origin is, all in mm; `--height` how far down to draw, default 600): `10_Limits_Frame.gc` (the zone filled to 5 mm from its edges: frame, diagonals, inscribed circle, three big arcs whose centres are outside the zone, corner marks) and four pen-up refusal files
(`11_Refuse_Left`, `12_Refuse_Right`, `13_Refuse_Top`, `14_Refuse_ArcBulge`: a move 1 mm inside the limit, then one 1 mm beyond it; the machine must halt with `E` on that line, nothing moving). Check them first with `tools/bench/gcode_check.sh --limit 700 --geometry D,OX,OY FILE`;
`tests/host/check_zone_files.sh` does it for a sample geometry in `run_all.sh` and its 18 generator mutants are all caught. **Tried on the machine right after (maintainer).** Arcs whose centre is far outside the zone are judged by their own points (`tests/host/test_travel.c` 8b).

## The size flags (2026-10-11): adopted, host-verified, **tried on the machine right after (maintainer)**
`-mrelax -fno-gcse -fira-algorithm=priority -mstrict-X -fno-inline-small-functions` (`cmake/size_flags.txt`): SD **29 208 / 30 720** (1 512 free), serial **26 626**; Motion +3.4 % cycles, SD worst stack 334 (margin 370). They change the generated code of the whole firmware and the host tests do not run it: **every check
that matters has to be repeated on an image built with them** -- the ring test (`serial_probe.py --cycles 70 ring`), the test card from the SD card and a stream over the serial link (the speed must still be ~20.6 mm/s, the test card ~9 min 48 s), `$J=` and the pause/resume probe, the travel-zone checks, the LCD. If anything differs from
the images before (flash them from `git show 5a74536`'s tree, or build with `-S`, `tools/analysis/build_size.sh -S`), say which flag to suspect: the truth table (`docs/design-notes/SizeFlags-TruthTable-*.md`) lets any subset be built. **Re-evaluate the flags before the first release** (`CONTRIBUTING.md`).

## The user's drawing origin (2026-10-11): host-verified, **tried on the machine right after (maintainer)**
`Set origin` and `Origin = calib.` in the menu (each asks for SW to confirm, a long press leaves), `G92 X Y` and `G10 L20 P1 X Y` in GCode and over the link (`REQ-CoreLR-F-Calibration-0010`, `-GCodeInterpreter-0017`). The origin is stored (also after `G92`), always inside the travel zone, and "Calibrate machine" resets it to the calibration point. Flash: SD 30 152 / 30 720 (568 free), serial 27 582.
To check on the machine: (1) jog the pen to a corner of the paper, Menu -> Set origin -> SW: the status screen must read 0.0 / 0.0, the pen must not have moved, and a power cycle must keep it; (2) jog away, Menu -> Origin = calib. -> SW: the screen reads the position relative to the calibration point again; (3) the same with a long press: nothing changes;
(4) from Candle: "Zero XY" (G92 X0 Y0) at rest: `ok`, the position reads 0,0, and a restored `G92` after an Abort is accepted; (5) `G92 X0 Y0` in the middle of a job file from the SD card: it waits for the end of the moves before it, then the rest of the file is drawn relative to the new origin; (6) the travel-zone checks again with a moved origin: the zone follows it (`gen_gcode.py --limits D,OX,OY` with your new OX, OY);
(7) "Return home" goes to the NEW origin. `docs/protocol/SerialProtocol.md` (the host side's) still lists `G92` as refused: to update there.

## Next substantial piece of work (done 2026-10-06: the v2 design, `REQ-CoreLR-D-Motion-0018`, `REQ-CoreLR-D-Stepper-0011`)

**Decouple motor steps from waypoints.** A motor can currently only step
at a waypoint boundary, which is what sets the residual jitter floor
(7-10 % RMS between consecutive steps). Neither a finer grid nor a
faster tick reaches past it — both were measured, see
`REQ-CoreLR-D-Motion-0017`.

The fix is a per-motor DDA in the ISR spreading each motor's steps
evenly across the interval, as `grbl` does. Agreed with the
maintainer to be done on a separate git branch.

Related, and noted by the maintainer: **Timer1 can be freed.** The
`Servo` library owns it, but the pen only ever takes two positions and
needs no precision. Replacing it gives a 16-bit timer (0.5 µs
resolution, 32 ms range at prescaler 8) instead of the 8-bit Timer2, and
should also recover flash — now the tighter budget.

## Budgets

| | 2026-09-11 | 2026-10-06, v2 Timer1/Timer2 | limit |
|---|---|---|---|
| Flash | 28124 (91 %) | **28252 (92 %)** | 30720 — **the tighter one** |
| Static RAM | 1710 (83 %) | **1537 (75 %)** | 2048 |
| Worst-case stack | 296 | **296**, margin **+215** | 511 free |

Between: +868 flash for the persisted position (`D-Calibration-0006`…`-0008`),
then −688 for the Timer1/Timer2 rework — the `Servo` library alone was ~600
bytes and 40 bytes of RAM. Arcs (`G2`/`G3`) are not written yet and will cost
flash.

**Then the step-event generator in `Motion.c`: +1436 flash (28362 → 29798, 96 %).** See `REQ-CoreLR-D-Motion-0018`'s integration notes. **Optimisation pass 2026-10-06:**
`HardwareSerial` was being linked into the image because the vendored sdfat's unused debug printers mention `Serial` (−732 flash,
**−175 RAM**), and the 64-bit arithmetic in `Motion` was replaced by smaller, faster forms (−814 flash, CPU 17.6 % → 14.6 %).
Free flash: **2468 bytes**.

## Arcs (`G2`/`G3`), 2026-10-07: implemented and tested on the host (history: it did not fit the Nano then; it fits since 2026-10-08 and has run on the machine -- see the end of this section)

Measured with the Nano fqbn: **32 932 bytes of flash (107 % of 30 720)**, 1 619 bytes of RAM (79 %), worst stack 326
(margin **+103** at that point, from +215: the planner blocks grew from 9 to 21 bytes and `Move_t` by 34). The estimate was 1.2–1.5 KB; the
cost is **+4 598 bytes**: `Trig` ~1 260, the arc geometry and queueing ~1 700, the planner's tangents and ceilings, the
parser. Arithmetic on 32-bit values costs 8–10 bytes an operation on this target. Four trims were kept (−700 bytes).

**LCD driver replaced the same day** (`REQ-CoreLR-D-LcdI2c-0001`: polled TWI and our own HD44780 driver for the PCF8574 in
place of `Wire` and `LiquidCrystal_PCF8574`): flash **32 932 → 30 858 (−2 074)**, RAM **1 619 → 1 350 (−269)**, worst stack 326
with **698 bytes free, margin +372** (was +103; the 574-byte TWI interrupt is gone from the latency chain too). The build is
still **138 bytes over the Nano's 30 720** (100 %), so arcs still do not fit; the margin to leave on top of that is the
maintainer's call. Candidates, with the cost of each measured or estimated as stated: the radius blend of an arc (a behaviour
the maintainer confirmed; estimated 120–200 bytes of code and 11 of RAM, not measured); a shared helper for the arc block that
`Motion_ArcCheck` and `Motion_QueueArc` both fill (estimated 60–80); `StartMove`'s line set-up skipped for arcs
(runtime only, saves no flash); the UI's 3 724-byte `LcdJoystickUI_Task`, which has no single large item.

Also found, and fixed in the same change (`REQ-CoreLR-D-LcdJoystickUI-0018`): the display's frame buffer was **never
written** — `PrintPadded()` printed straight to the bus, ~15 ms of blocking I²C every 50 ms whether or not anything had
changed, against the "change-driven, incremental" design. Now: 0 bus transactions when nothing changes, at most one
character per `Task()` call (0.94 ms). Behaviour is verified
on the host (`tests/host/test_arc.c`, `test_parser.c`, `test_trig.c`) and the CPU cost measured (14–17 % while
generating an arc, against 15 % for a line); **nothing of it has run on the bench**.

**Arcs now fit the Nano (2026-10-08), with `CORELR_MATH_48`** (`REQ-CoreLR-D-Motion-0024`, the maintainer's question whether the
64-bit products, divisions and root are needed): no value wider than 32 bits ever exists — the exact residual of the root is
known from the low 32 bits alone. Whole image: **30 588 bytes of flash (99 %, 132 free)**, 1 350 RAM, worst stack 318 (margin
**+380**); `CORELR_MATH_64`, kept side by side behind the same `#define`, is 30 848 and does not fit. `Motion_Hypot` is also
2.8 times faster (1 772 cycles against 5 038). A literal 48-bit type was tried first and was *larger* (+394 bytes): libgcc's
32×32→64 is hand-written assembly. Reach: 2^22 µm (4.19 m) becomes a real limit of the arithmetic; refusing a farther GCode
target is now an invalid line (`REQ-CoreLR-F-GCodeInterpreter-0011`, +118 bytes: 30 706 of 30 720, 14 free — then **−656 bytes of trims in two rounds: 30 050, 670 free**, see `REQ-CoreLR-D-Motion.md`). Both arithmetics pass the whole host suite
(`tests/host/run_all.sh`). *(Written 2026-10-08: not run on the bench. **Since:** `03_Circle_R100.gc`, whose circle is `G2`/`G3`, was streamed on the machine on 2026-10-10 -- 828 mm of path at 20.7 mm/s, see the probe results below -- and arcs are in the travel-zone files; the arcs of the SD image are still to try.)*

## Candle's Home / Safe position / Restore origin (2026-10-11): host-verified, **tried on the machine right after (maintainer)**
Serial variant: **29 052 / 30 720 B of flash (1 668 free)**, static RAM 939 B (measured 2026-10-12). Calibrated and idle, the pen over nothing; Candle 1.1.9.
1. The status line carries `|WCO:` in the first report after connecting, then every tenth, and at once after a `G92`. Candle's position display shows the **drawing** position (WPos) and `MPos` is the machine one: after `G92 X10 Y10` (or *Zero XY* on the pen at the origin) the display reads 0 / 0 and `?` shows `MPos` unchanged.
2. `$#` answers `[G54:0.000,0.000,0.000]`, `[G92:x,y,0.000]`, `[TLO:0.000]`, `ok`; `$` lists `$# $H`.
3. **Safe position** (`G53 G0 Z0`): the pen rises, nothing moves. A `G0 Z0` (no `G53`) lowers it: check both on the servo.
4. **Home** (`$H`): with the pen down and the head away from the origin, it lifts the pen, then goes straight to the **drawing origin**; the reply `ok` comes at once, the state reads `Jog` until it arrives. Refused: uncalibrated / LCD in a moving mode (`error:9`), during a job (`error:8`).
5. **Restore origin** after a *Zero XY* done away from the reference point: `G53 G90 G0 X0 Y0 Z0` goes to the **reference point** (calibration), then `G92 X0 Y0 Z0` puts the origin there. Afterwards `Origin = calib.` on the LCD must read the same.
6. **The Abort (P16, decided (a), FW-14 not yet built):** *captured on the fake machine 2026-10-12, mid-job (`candle_spy.py --ok-delay 1`: 10 lines waiting at the `!`, the three lines after the welcome).* On the **machine**: after the Ctrl-X Candle sends `G21G53G90X..Y..Z..`, `G21G92X..Y..Z..`, `G54 G17 G21 G90 G94 M9 T0` (captured on 1.1.9). **Before FW-14**: `error:20`, `ok`, `error:20`, and the drawing origin shifts by the stop distance (read the position before and after). **After FW-14**: `ok`, `ok`, `error:20`; the head goes back a few mm; the origin does not move.
7. The edges: `G53 G0 X.. Y..` 1 mm beyond the zone is `error:33`; `G53 G1 X..` with `G91` active is still absolute; `G53 X0 Y0` after `M2`/Ctrl-X (no motion word) is `error:20`.

## The serial variant (increment 2, 2026-10-09): implemented, host-verified; first run on the machine on 2026-10-10, see below

`Uart` (the port: rings, real-time bytes), `SerialLink` (lines, replies, report, `$` commands), the sketch's job glue,
`LcdJoystickUI` without the SD browser (`docs/protocol/SerialProtocol.md` FW-02, -03, -05…-09, -11; `REQ-CoreLR-D-Uart`, `REQ-CoreLR-D-SerialLink`).
Nano fqbn: **25 680 / 30 720 bytes of flash (5 040 free), 785 RAM, worst stack 300** (margin ≈ 950); the SD variant is **29 882** and 1 349 RAM, as before
(`FW-11`: not one byte of the link is in it). The SD build lost 154 bytes on the way: the parser no longer copies the line.

**To build it**: in `SwConfig.h` change `CORELR_JOB_SOURCE_SD` to `CORELR_JOB_SOURCE_SERIAL` in the `#define CORELR_JOB_SOURCE` line (or
`tools/analysis/build_size.sh -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL`). Nothing on D0/D1 may be wired to anything but the USB bridge; the SD module can stay plugged in, it is not used.

**What to check on the bench, in this order**, first with a plain terminal (`picocom -b 115200 /dev/ttyUSB0`; type `Ctrl-A Ctrl-Q` to leave), then with Candle:

1. **The welcome line.** Open the port: the Nano resets (DTR), and after a moment `Grbl 1.1h ['$' for help]` arrives, followed by `[MSG:Not calibrated: use the LCD menu]` if the machine is not calibrated.
   *Time it* from the moment the port opens (M-3: the host's start-up wait).
2. **`?`** (no Enter needed: it is a real-time byte) → `<Idle|MPos:0.000,0.000,0.000|Bf:4,128|FS:0,0>`; `$I`, `$G`, `$$`, `$` each answer and end in `ok`.
3. **Uncalibrated**: `G1 X1` → `error:9`; `$X` → the "not calibrated" message and `error:9`. Calibrate on the LCD, then `$X` → `[MSG:Caution: Unlocked]`.
4. **By hand**: `G91`, `G1 X10`, `G1 Y10` (up the wall), `M3`, `M5`. Between two lines the machine goes `Idle` and keeps its pen and its `G91`. `$G` shows both.
5. **Real-time**: stream a few long lines (paste 20 of `G1 X5` / `G1 X-5`), then `!` (the machine ramps down, pen up, `Hold:0`), `~` (it carries on), Ctrl-X (it stops, `Grbl 1.1h …` comes back, `$G` shows the modal state reset).
   Press **Cancel on the LCD** during a stream: the machine must end in `Alarm`, the lines in flight answered `error:9` (this is the one decision of `REQ-CoreLR-D-SerialLink-0007` the maintainer has not yet confirmed).
6. **An error during a job** (`G1 Z5` in the middle of a stream): `error:20`, ramped stop, `Alarm`, then `$X` or Ctrl-X or `SW`.
7. **Candle** (`candle 1.1.9`): connecting sends Ctrl-X, then waits for a line starting `GRBL`; it sends `$G` every second and `?` every 40 ms. Send a small file with arcs. Watch `Bf:`. Its jog buttons send `$J=`: `error:3` for now (increment 3).
8. **The measures the host side asked for**: M-3 above; **M-4** latency of `!` and of `?` while the machine moves (a logic analyser, or Candle's own status refresh); **M-5** a file of 0.2 mm segments at full speed: does the `miss:` counter
   (`-D CORELR_DEBUG_MISS_COUNT=1`) move? **M-7** the step edges' jitter with a stream running (scope on a coil wire).

## Serial-protocol increment 1 (2026-10-08): Y up, the cause of every refusal, `G21`/`G54`/`G94`/`M2`/`M30`

Requests FW-01, FW-04 and FW-10 of `docs/protocol/SerialProtocol.md`, in the code both variants share (`REQ-CoreLR-D-Motion-0025`,
`REQ-CoreLR-D-GCodeParser-0011`/`-0012`). Whole image, Nano fqbn: **30 000 → 30 036 bytes of flash** (FW-01: 0, FW-04 + FW-10: +36), RAM
+1, worst stack −3. **Host-verified; nothing of it has run on the bench.** What to look at, in this order, with a small SD file:

1. **A stored position survives the update.** The EEPROM image holds the internal position, which the flip leaves valid: after flashing, the status
   screen should show the *same point* as before, with the **sign of Y reversed**. If it shows something else, stop.
2. **The Control jog.** Stick down must still lower the pen on the wall, and the `Y` on the status screen now goes *down* (negative) as it does.
   The firmware takes `dy > 0` for "stick down" — never measured on the unit; if the pen goes the wrong way, the sign in `HandleJogControlInput` flips back, and so does
   the menu's. The orientation itself is the build option `CORELR_Y_UP` (`SwConfig.h`; `-D CORELR_Y_UP=0` gives the old +Y-down frame and the old stream, bit for bit).
3. **`G1 Y10`** goes up the wall. **`G2 X20 Y0 I10`** from the origin passes above it, **`G3`** below it.
4. **A file starting with `G21` and ending with `M30`** (what most generators write) runs to the end, lifts the pen, ends in `I`, and ignores whatever follows the `M30`.
5. **A bad line** (`G20`, `Z5`) still stops the job with `E`; which cause it was is invisible on the LCD (it is for the serial link).

Also fixed on the way: `GCodeParser_Init()` was called only from `setup()`, so a second job (or one started after a cancel) inherited `G91` and the pen
state of the one before. It is now called at every job start.

## Defect found 2026-10-07: queued moves started from the executed position

`GCodeParser` computed the start of each move from `Motion_GetComputedPosition()`, which is behind every
block still in the look-ahead planner. Three relative `G1 X10` ended at X = 10 mm instead of 30; an omitted
axis snapped back to the executed position. Fixed (`REQ-CoreLR-D-GCodeParser-0008`), with a regression test
that fails on the old code. **It had never shown because no real job has ever been run** — which is the
single largest thing still unverified (see below).

## "Return home" menu entry (2026-10-10): host-verified, **tried on the machine right after (maintainer)**

Second entry of the menu (`REQ-CoreLR-D-LcdJoystickUI-0024`). To try on the bench, calibrated, idle:
1. Jog the pen a few centimetres off the origin and lower it (a job's Cancel leaves it down too); menu → **Return home**: the pen must rise **before** the head moves, then a straight line to the origin; the screen shows the position
   counting down with an `H` in column 0, then `I` on arrival, and the readout must be `0.0` / `0.0`.
2. Same, but press **SW** half-way: the head must slow down and stop smoothly (not dead), the screen go back to the status layout, the position stay valid (menu → Jog → carry on).
3. The menu entry must be absent when uncalibrated and while a job runs or is paused.
4. Repeat 1 from the far corner and time it (expected ~50 s for a diagonal metre).

## Navigation ergonomics (2026-10-10): host-verified, **tried on the machine right after (maintainer)**
`J` in column 0 during the Control jog, SW leaving it for the status screen; a last menu entry `<arrow> Back` (check the arrow on the real display; if it is a stray symbol, set `LCDJOYSTICKUI_BACK_ARROW` to `'<'` in `HwConfig.h`);
with Candle connected, open the LCD menu and start a job from Candle: the display must go to the status screen (and again on Hold / `~` / end of the job). `REQ-CoreLR-D-LcdJoystickUI-0025`, `-0026`.

## Pause that never finished (bench 2026-10-10): cause found and fixed on the host, **re-run on the machine right after (maintainer)**
Sequence: file sent by Candle, *Pause* in Candle, no *Resume* possible (button greyed; `~` and the LCD's Resume did nothing), LCD *Cancel* → `Alarm`, no menu, reset after > 10 s → calibration lost. One cause:
`BeginRampedStop()` closed Stepper's gate together with `Motion_Halt()`, so the ramp could not be enqueued and the state stayed in HALTING (`REQ-CoreLR-D-Motion-0026`). Fixed, with a host test and a sketch check.
To re-run: pause from Candle (the state must read `Hold:1` for a fraction of a second, then `Hold:0`), resume from Candle with `~` and again from the LCD; cancel from the LCD (→ `Alarm`; a short press of SW on the status screen, or `$X` / Ctrl-X, must acknowledge it);
reset after > 2 s: the machine must come back calibrated. If it does not, the calibration loss has a second cause, and we look at it then.

## The resume that froze the machine (bench 2026-10-10, found with the probe): fixed on the host, **confirmed on the machine right after (maintainer)**
After `-0026` the pause worked and the resume did not: `Run`, position frozen, `Ctrl-X` unanswered. `tools/bench/serial_probe.py pause-resume` reproduced it; its `D:` field (`CORELR_DEBUG_JOBSTATE=1`) showed a stepper armed with a backlog
and 15 entries that did not drain: **the first hold after the resume was 36 minutes** (`REQ-CoreLR-D-Motion-0027`: a stale `PendTime` after the pause's flush). Fixed (`BeginTimeline()` on resume), and a hold check added to the host tests.
To confirm: `tools/bench/serial_probe.py --pause-after 3 --hold 1 pause-resume` must end with "resumed: Run", the rest of the job finishing in `Idle` back at X = 0; then `cancel-after-pause`; then the same from the LCD. Note that **any wedged run followed by a reset loses the calibration**
(the position is stored only in `Idle`, `Paused`, `Error`) -- the probe leaves the machine uncalibrated if it fails.

## Candle after a Cancel from the LCD or an error: the way out is its *Reset* button (bench 2026-10-10, decided to stay as is)
After a Cancel from the LCD (or an error during a job) the machine reports `Alarm`. Candle, which does not know the job is over, stays in its "processing file" state: *Unlock*, *Open*, *Reset* (file) and *Send* are greyed, and *Pause*
and *Abort* do nothing -- Abort waits for the machine to report `Hold` before it sends Ctrl-X, and `Alarm` is not a state its abort handles. **What works** (confirmed on the bench): the **Reset button of the Control panel** (the icon whose tooltip reads "Reset", beside
*Unlock*; not the file group's *Reset*): it sends Ctrl-X, sets Candle's sender to stopped, and our Ctrl-X acknowledges the error and answers the welcome line. Disconnecting and reconnecting has the same effect. The alternative considered -- reporting
`Hold:0` instead of `Alarm` until the error is acknowledged, so that Abort would complete -- was **not** taken (the maintainer: "we stay as it is"): it would change what every other sender sees.
Caveat: this was read in **upstream** Candle's `frmmain.cpp` (the Debian 1.1.9 sources could not be fetched with `apt source` here, no `deb-src`), not in 1.1.9 itself -- only the Reset button's behaviour is confirmed on the real 1.1.9.
For the host side, which owns `docs/protocol/SerialProtocol.md` (not edited here): §4.6 / §11.1 could say that Candle's *Abort* cannot finish from `Alarm`, and what to press instead.

## 2026-10-10: `$J=`, 0x85, the Z pen word, planner depth 8 -- tried on the machine with the probe
`tools/bench/serial_probe.py jog` (20 checks, **all OK**) and `stream`: relative jog ±20 mm (`ok`, `Jog` then `Idle`, to a tenth of a millimetre), absolute jog, ten chained jogs of +2 mm, **0x85 in the middle of a 60 mm jog** (the head stops 4.1 mm after the cancel, stays there, back to the origin by an absolute jog) and at rest (nothing),
Candle's Z-only jog (`ok`, nothing moves), the refusals (`error:20` for `G1`/`M3`, `33` out of reach, `2`, `3`) and **`error:8` for a jog during a job**, `G1 Z0` / `G1 Z5` (`$G` reads `M3` then `M5`: the servo moved). `Bf:8`: the planner is 8 deep.
`stream`: the 20 cm circle R = 100 mm, 828 mm of path in 40.0 s (**20.7 mm/s**, ceiling 21.5); the 200 mm square, 1 081 mm in 51.8 s (**20.9 mm/s**); the **test card** (153 lines, the short-segment case), 12 111 mm of path in **588.3 s = 9 min 48 s** (**20.6 mm/s** on average, every line acknowledged, `Idle` at the origin) -- the maintainer's Candle run takes "a little under 10 minutes", the same.
**Still to try with Candle itself:** the jog buttons (continuous and by step) and the Z buttons; the L/R wires cancel (Calibrate machine, step 3, long press after moving a wire: the machine must read *uncalibrated*, `U` / `Alarm`, and need the calibration again); the jog must feel as before at depth 8. The Z threshold (Z <= 0 = pen down) is this project's convention: check it against a real generator's output.

### A lesson the probe taught: closing the port too soon costs the calibration
A first `jog` run ended 0.7 s after its last move and the machine came back **uncalibrated** at the next open. The position is stored after 2 s of stillness (`CALIBRATION_POSITION_STORE_DELAY_MS`); opening the port resets the board, and *closing* it may too (the DTR line, depending on the USB-serial driver): a reset before the store loses the position. The probe now waits 3 s before it closes
and the `D:` field (`CORELR_DEBUG_JOBSTATE=1`) ends with the keeper's `stored` flag (0 or 1) to watch it. **The same applies to Candle**: disconnecting within 2 s of the last move may cost the calibration. If that bites, `CALIBRATION_POSITION_STORE_DELAY_MS` can come down (the 2 s also limits EEPROM wear: the marker is rewritten at every start and stop).

## The position ring (`Calibration-0010`): host-verified, **tried on the machine right after (maintainer)**
Flashing it costs **one recalibration** (new format, new address). Layout: `docs/protocol/EepromLayout.md` (64 slots of 15 bytes, to the end of the EEPROM). To check afterwards: calibrate; jog; wait 3 s; reopen the port (or power-cycle): the machine must come back calibrated at the same place; do it a few dozen times, and across a pause / resume / cancel. With `CORELR_DEBUG_JOBSTATE=1` the `stored` flag still goes to 1 two seconds after a move.

## SD variant with the position ring (2026-10-10): host-verified, **tried on the machine right after (maintainer)**
`CALIBRATION_POSITION_RING` is now 1 in both variants. The SD build: **29 608 / 30 720 bytes of flash (1 112 free)**, 1 343 RAM, worst stack 314 (margin +391). It **only links with
`-mcall-prologues`** (the CMake build applies it, option `CORELR_CALL_PROLOGUES`). `tools/analysis/stack_depth.py` did not see that flag's prologues (it said 128 for what is
284 without the ring and 314 with): fixed, and checked on the build without the flag, where it gives the figure it always gave (284). Tests: `test_position`, `test_ui`, `test_motion`, `test_parser`, `test_pen`,
`test_jogcurve` pass with the SD define, `test_position` also in `_64` and with the ring off. First start after flashing = one recalibration. Check on the machine as for the serial ring (below).

## 2026-10-12: Candle against the serial variant with FW-12/13/14 -- first bench results (maintainer, `Console` of Candle 1.1.9)
**Works on the machine:** `$I` (`20261011`), `$#` (`[G92:-300.000,0.000,0.000]` = the WCO), Candle shows Machine and Work, **Zero XY** (Work 0,0, Machine unchanged), jog, **Safe position** (`G21G90`, `G53G0Z0`: the pen lifts, nothing moves, `ok`), **Home** (`$H`: pen up, back to the **drawing** origin), the typed pair `G53 G0 X0 Y0` / `G92 X0 Y0` (back to machine zero and work zero).
**Restore origin does not move the head -- and that is Candle, not the firmware:** after a +50 mm jog it sent `G53G90G0X-150Y100Z0` then `G92X50Y0Z0`, i.e. the head's **current** machine and work positions (Machine X -200 -> -150, Work X 0 -> 50). It re-labels where the head already is (its use is after a controller reset); the pen lifts because the line has `Z0` (`G53` Z0 = top). The earlier capture (`G53 G90 G0 X0 Y0 Z0`) only looked like "go to zero" because the head was at 0,0. To put the origin on the calibration point: type `G53 G0 X0 Y0` then `G92 X0 Y0`, or the LCD menu "Origin = calib.".
**Abort (Candle's Cancel) -- a defect found:** during a job, Ctrl-X was followed by Candle's three lines: `G21G53G90X-13.583Y-100Z0` -> `ok`, `G21G92X-13.583Y-100Z0` -> `ok` (FW-14 works: both accepted), then `G54 G17 G21 G90 G94 M9 T0` -> `error:20` and the next line (`S0`) -> **`error:9`**: the machine is left in the **error state** (Alarm), not Idle as `docs/protocol/SerialProtocol.md` P12 assumed. Cause (reproduced on the host, `test_seriallink`, the same four replies `ok`, `ok`, `error:20`, `error:9`, state = error): the first `G53` line is a G-code line, so it starts an implicit job; the third line arrives while that job is still `RUNNING` (the `G92` waits for the move to end, the third line is already in the ring), and an error during a job stops it. Not yet decided how to fix (options in the conversation: accept `M9` and `T<n>` as no-ops, as grbl does; or let an error not stop a job that only holds restoration lines).
**Not established:** whether the head went back to the position Candle had read (-13.583, -100) before the `G92`. The maintainer saw no movement, which a few millimetres at G0 speed would explain; read Machine X after an Abort: -13.583 means it came back (`G92` changes Work, not Machine).

### Same day, later: `M9`/`T` accepted, flashed, and the Abort re-run with the probe
Serial image flashed (`upload_serial`, 29 062 bytes verified). `serial_probe.py abort` (new scenario): a 60 mm move along X, Ctrl-X 0.8 s / 1.5 s / 2.5 s into it, then Candle's three lines and `S0` in one burst 47 ms after the welcome line, from the last position read. **Three runs, the same outcome:** the four replies are `ok`, the machine ends `Idle`, the head is exactly on the position Candle had read (difference 0.000, 0.000 mm), and the head and the work origin were put back to 0,0. The error state after an Abort is gone. **Not reproduced:** the maintainer's Machine Y of -99.572 against Work Y -100 after a Cancel in the middle of the border; with the old image the `error:20` stopped the job right after the restoration move, which is the likely cause, but only a Cancel in Candle on the new image will tell (read Machine and Work, X and Y, before and after).

**Confirmed with Candle itself (maintainer, same day):** a Cancel during the drawing, Machine and Work read before and after: **identical positions, no error in the console**. The Y gap of -99.572 is not seen any more; FW-12, FW-13, FW-14 and the `M9`/`T` fix are verified on the machine with Candle 1.1.9 (Zero XY, jog, Safe position, Home, `$#`, Abort).

### 2026-10-12 (evening): size flags per variant, `-fshort-enums` on -- flashed (serial) and tried
Serial image **28 750 B** (was 29 062), RAM 936. `serial_probe.py`: `jog` 22 checks OK, `ring --cycles 4 --span 60` OK (four different positions found after four resets, the head ends at the drawing origin), `abort` (all `ok`, Idle, 0.000 mm), `pause-resume` (resumed, finished, Idle). The probe's absolute jogs now use the drawing frame (it sent machine coordinates before, `error:33` once WCO was not zero). Not yet on the machine at that date: the SD image with its new flags (29 778 B), and a plot (both done since, 2026-10-13).

### 2026-10-13: `05_Chords.gc` (circles R = 30 mm as chords of 0.2 / 0.5 / 1 / 2 mm) -- the look-ahead on short segments, on the machine
Maintainer: **the four circles take a similar time, the motion is smooth, no visual difference** between the 0.2 mm and the 2 mm chords. Predicted (`tests/host/test_chain.c`, with back-pressure): about 9 s a circle on the serial variant; on the SD variant 12 s for the 0.2 mm one (planner depth 4). **Variant: serial** (maintainer, with Candle); no stopwatch figures, only the comparison between the four circles, which agrees with the prediction (all about 9 s, planner depth 8). **Not tried on the SD variant**: the predicted 0.2 mm shortfall (12 s, depth 4) is unverified.

### 2026-10-13: the SD variant with 8 blocks of look-ahead (was 4) -- flashed. **`05_Chords.gc` from the card (maintainer): no visible difference in speed between the four circles**, as predicted for 8 blocks (about 9 s each; with 4 blocks the 0.2 mm one was predicted at 12 s, never measured). **Still to try**: a plot with the SD browser in a deep directory (the stack margin is 290 bytes).

### 2026-10-13: the SD browser's depth and stack -- the tree to try (`tools/generators/gen_sd_tree.py`, written to the card as `TREE/`)
Procedure (maintainer): (1) enter `TREE`, then `D2345678`, then `D3456789`: three levels, each listed with directories before files, alphabetical; (2) in `D3456789` start `THREE.GC` (a 40 mm square: the size says which file ran); (3) try to enter `D4567890`: the browser must **refuse** (the path would be 32 bytes) and stay in `D3456789`, with no truncated path and no crash; (4) go up to the root and down again twice (the path is re-walked at each "up"); (5) in `TREE/MANY` (40 files and `S1`..`S3`): `S1`..`S3` first, then `F01.GC`..`F40.GC` in order, scroll to the end and back (every step rescans the directory); (6) the same in `TREE/D2345678/D3456789/MANY3` (35 files at depth 3: the deepest stack, 290 bytes of margin by static analysis). Result: *to come*.

### 2026-10-13 (evening): the SD browser: 64-byte path, recovery from a pulled card, rank cache -- host-verified, flashed, **to try**
Maintainer's findings on the machine: with the 32-byte path the tree behaved as predicted; **the display slowed down along a 40-file list** (the rank search rescanned the directory `rank + 1` times: now one scan per step, `REQ-CoreLR-D-SDReader-0010`); **a card pulled during a navigation left the browser stuck** (now: the reads that fail are noted, the UI remounts, back at the root or "No SD card", `-0009`). The limits are in `-0011`. Image: **30 304 B** (416 free), RAM 1 474, worst stack 328. To try with `TREE/`: the same steps as before, plus: (7) the speed along `TREE/MANY` should now be the same at the end of the list as at the start; (8) pull the card during a navigation and put it back: the browser must be at the root, the first entry shown, no freeze; pull it and leave it out: "No SD card", then `SW` after putting it back; (9) `L7.GC` at level 7 (path of 59 characters) must run, and the level 8 (68) must be refused. **Result (maintainer, 2026-10-13): the display is much smoother along the long list (the rank cache works on the machine); the tree goes down to `D8901234`, which is listed but cannot be entered; the deepest readable drawing is `L7.GC`, `L8.GC` is unreachable -- exactly the designed behaviour (level 7 = 59 characters, level 8 = 68 refused).** **Card pulled during a navigation (maintainer): seen as removed; put back, the browser returned to the root.** The silent refusal now says `Path too long` on row 1 (30 374 B, 346 free) -- flashed and **tried by the maintainer: the message shows and stays after SW** (the entry stays selected).

### 2026-10-13 (night): the card pulled DURING a job -- host-verified, flashed, **to try** (`REQ-CoreLR-D-SDReader-0012`)
Start a long file from the card (`05_Chords.gc`, about 40 s), **pull the card in the middle of a circle**: the head must decelerate and stop (not carry on to the end of a circle, not lift the pen as if the job had finished), the screen must show the error tag `E`; `SW` acknowledges; then the browser must find "No SD card" or, with the card put back, the root. 30 422 B, 298 free. **Result (maintainer): the head stops (the deceleration is too quick to see), the screen shows `E`, `SW` acknowledges and the status returns to `I`.** Not reported: what the browser showed afterwards (the card out, or put back).

### 2026-10-13 (late): the LCD's character ROM -- `T09_LcdRom` -- and the pen pictograms
`tests/bench/T09_LcdRom` (the 256-code table, two lines at a time, scrolled by the joystick) shows that the display has the **A00 ROM**: no vertical arrows (`docs/hardware/LcdCharacters.md`). The maintainer drew two 5 x 8 pictograms for the pen (a pen over a line of paper, down and up); they are loaded into the CGRAM at start-up and are **switchable** (`LCDJOYSTICKUI_PEN_GLYPHS`, `v`/`^` when 0). **+102 B of flash** each variant (SD 30 524, **196 free**; serial 28 852). Host-verified (`test_lcdi2c`, `test_ui`, 20 mutants). **Tried by the maintainer on both variants (the SD and the serial images): the display is correct.** The shapes are now in their own module, `LcdGlyphs` (same size to the byte).

## Not exercised when the first sessions ended (since done: the SD card and plots, see 2026-10-13 below)

- **SD card** — no card has been in the slot. This is the deepest stack
  path in the firmware (`REQ-CoreLR-D-SDReader-0005`), so the +42 margin
  above is the number to watch when it is first tried.
- A real plot, end to end. Every speed figure so far comes from jogging.

### 2026-10-16: the cable scale -- a procedure written, a first inconclusive reading (nothing changed in the firmware)
The maintainer sees the pen go **farther than commanded near the left / right limits** and the cables **over-tight before the top limit**; the thread is a sewing thread, the pulley diameter (35 mm) is right. Checked in the code: pi (355/113), the truncated circumference and the Q24 conversion are not the cause (< 1e-5). Suspects: the 28BYJ-48's real ratio (63.68395:1, i.e. 2037.886 steps a turn instead of 2048: +0.496 % of cable per step) and the thread's thickness (+0.7 % for 0.25 mm on 35 mm, single layer). A scale k > 1 gives exactly the two symptoms, and the top limit amplifies it: at 50 mm under the motor line, k = 1.005 leaves 33 mm instead of 50 (tension x1.5), 1.0075 leaves 19 mm (x2.6), 1.012 leaves 0 (computed, not measured). Procedure with a tape measure that touches no calibration: `docs/hardware/CableScaleMeasurement.md`; calculator `tools/analysis/cable_scale.py`. **First readings (L only, 5 mm resolution): 800 / 1045 / 1305 mm at Y = 0 / -300 / -600 against 800 / 1043.2 / 1310.1 predicted: inconclusive (opposite-sign residuals, rounding as large as the effect).** To redo more precisely by the maintainer; then `MOTION_SPOOL_DIAMETER_UM` = 35 000 x k and one *Calibrate machine* after flashing.

### 2026-10-26: the real gear ratio of the 28BYJ-48 -- `MOTION_STEPS_PER_REV` 2048 -> `MOTION_STEPS_PER_REV_X1000` 2037886 (host-verified, then tried on the machine: better, per the maintainer)
At the maintainer's request, after the analysis of 2026-10-16: the output shaft needs 32 x 63.68395 = 2037.886 full steps a turn, not 2048, so a step winds 53.96 um of cable, not 53.69 (+0.5 %). `HwConfig.h` carries the figure with its derivation; `Motion.c`'s three macros (Q24 conversion, waypoint spacing, one-step threshold) follow. Both arithmetics: all host tests pass (the three tests that hard-coded 2048 / 109955 follow), the geometry picture is regenerated and verified against the firmware. Images unchanged to the byte of size: **sd 30 524 (196 free), serial 28 852 (1 868 free)**. **To try on the machine: flash, bring the pen to the reference point and confirm *Calibrate machine* once (the stored step counts were computed with 2048); then the pen should stop farther from the top limit, with less tension, and the lateral overshoot should shrink. **Done: tried on the machine, better (maintainer).** The thread's thickness, if any, remains to be measured (`CableScaleMeasurement.md`).**

### 2026-10-27: the API documentation -- the target `doc` (Doxygen), host-verified; **no image changed**
`cmake --build --preset host --target doc` writes `build/doxygen/html/index.html` (`REQ-CoreLR-D-Build-0013`, template `docs/doxygen/Doxyfile.in`). Nothing was flashed and nothing on the machine was touched. **Firmware unchanged, measured**: the two `.hex` are byte-identical before and after (sha256 `e9b77dcb...` SD, `b046e3e1...` serial), also from a fresh build directory; flash and RAM as at the last reading (SD 30 524 B / 1 477 B, serial 28 852 B / 936 B). Only comments of `HwConfig.h`, `SwConfig.h`, `Calibration.h`, `Stepper.c`, `LcdJoystickUI.cpp` changed (`\#define` in prose, `\<tag\>`, a stray `@return`, one `@ingroup` line per configuration group).
Both variants are documented in one output (an `#if` on `CORELR_JOB_SOURCE` is true for both under `EXPAND_ONLY_PREDEF`; the shipped 48-bit arithmetic and the default selectors are kept through `EXPAND_AS_DEFINED`). First run: 0 markup warnings once the comments above were fixed; **351 undocumented items** (176 variables, 140 macros, 33 functions, 2 typedefs) and **4 incomplete `@param` lists** (`Motion_SetOrigin` x y, `TravelCheck` p, `TextFmt_Char` c, `Trig_Rotate` outX outY) are listed, not forced. By module (undocumented): Motion 96, Stepper 52, LcdJoystickUI 37, GCodeParser 24, Calibration 18, SDReader 18, SerialLink 16, PenControl 14, Uart 11, HwConfig 9, SwConfig 31, grbl_corelr 8, LcdI2c 7, LcdGlyphs 4, JogCurve 3, Trig 2, IsrDispatch 1. Without Doxygen: the configuration succeeds, the target stops with how to install it (tried by hiding it from CMake). Host suite after the change: all 13 tests pass (both arithmetics); `doxygen_selftest`, `doxygen_build`, `doxygen_output` are new; 32 mutants of `tests/mutation/mutants/doxygen.txt` all caught. The CI step is written and never run.

### 2026-10-27 (later): the documentation per image, and the first `\dot` diagrams -- host-verified, **no image changed**
Maintainer's decision: the documentation is generated per image, like the firmware. `doc` now makes `build/doxygen/serial/` and `build/doxygen/sd/` (each preprocessed for its own `CORELR_JOB_SOURCE`; `PREDEFINED` gives the image, `EXPAND_AS_DEFINED` the other selectors) and a landing page; the earlier single output that showed both is gone. Warnings: **markup 0 in both**; undocumented serial 343 / sd 321; incomplete 4 / 4. Each image holds its own code and not the other's (`GCodeParser_ExecuteJog()` only in `serial`, `RedrawSdBrowser()` only in `sd`; `Motion_ISqrt()` in neither), checked by `tests/tools/test_doxygen.py`.
Diagrams added in comments, native `\dot` (Graphviz 14.1 here; no script, no Node): the job state machine (`grbl_corelr.ino`: the ASCII diagram was **deleted**, it was not what the code does: halt/cancel/error paths, `ERRORSTOP` for the LCD cancel of the serial image), the SD browser's input (`LcdJoystickUI.cpp`), the card's mount stages (`SDReader.h`), the serial link's pass and its reply by state (`SerialLink.c`). The other ASCII sketches of the code were left alone. Without `dot` the targets work: the diagrams are skipped, counted as *no dot* and the summary says how to get them (tried by hiding `dot`). Doxygen in use: **1.15.0**, which has no Mermaid (`@mermaid` unknown); the Doxyfile is to be reviewed and the Mermaid test redone on update (`docs/TODO.md`).
Maintainer's second round, done: the modules that belong to one image are now **wholly guarded in the sources** (headers and `.c`/`.cpp`), instead of excluded in CMake. Measured: **SD image identical (`e9b77dcb`), serial image 28 852 -> 28 768 B of flash (1 952 free), RAM 936 unchanged** (hash `1bdad599`): the serial image compiled `SDReader.cpp` (only its pin checks were under `#if`) and kept 84 B of dead code (an `SdFile` constructor, `Print::write`; `fn_sizes.py` on the two ELFs). Same behaviour, **host-verified only, not flashed**: `test_sdreader`, `test_seriallink`, `test_uart` pass, the bring-up sketches build (`bench`); the full host suite was not rerun (maintainer's instruction). Doxygen now reads `third_party/` (the Arduino core in both images, SPI and sdfat in the SD one), grouped by `docs/doxygen/third_party*.dox`; its warnings (serial 948, sd 1 056) are counted apart as *external*.
Maintainer's third round, done: **a module or library that an image does not use is no longer compiled in it** -- `cmake/variant_modules.txt`, read by `cmake/Firmware.cmake`, `cmake/ExportIno.cmake`, `ide_build.sh` and `cmake/Doxygen.cmake`; the `#if` guards stay and `tests/tools/test_variant_modules.py` checks the two agree (26 mutants, 25 caught + 1 equivalent). Measured: sd identical (`e9b77dcb`); serial **28 766 B** (`f11b8a78`, 1 954 free; 28 852 before this work), RAM 936: sdfat's empty static-constructor stub went too (-2 B). **`compare_with_ide.sh`: both images IDENTICAL to the arduino-builder reference build; `check_export_ino.sh`: both exports (now per variant: serial has no `SDReader` nor sdfat, sd has no `Uart`/`SerialLink`) build to the same images and the SD export without the flags still stops at its `#error`;** the bring-up sketches build. Serial image **not flashed**; the full host suite not rerun.
Maintainer's remarks the same evening, done: the image is passed to Doxygen as the firmware build passes it to the compiler (`CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_<SERIAL|SD>`, the numbers read in `SwConfig.h`, no longer copied); the modules of the other image (`SDReader` in `serial`; `Uart`, `SerialLink` in `sd`) are excluded, their headers not being guarded by `#if`; the hand-written module table of the main page is gone (Doxygen's *Files* list, per image, replaces it). Undocumented: serial 328, sd 321 (the modules left out counted for nothing now). The format of the export (one file per image?) is to be discussed.
Firmware: comments only again (the diagrams); both `.hex` byte-identical to the 2026-10-27 baseline (see the numbers above), flash and RAM unchanged. Only the Doxygen tests and the mutants of `doxygen.txt` (48, all caught) were run for this step; the full host suite is left for the maintainer's go.

### 2026-10-28: the documentation in git and in a release -- host-verified, **no image changed**
Maintainer's question: what to put under git? Measured first: `build/doxygen` is 13 MB (909 files) for `serial` and 15 MB (1 048) for `sd`; no LaTeX, `doxybook2` or `moxygen` on the machine. Decided and done (`REQ-CoreLR-D-Build-0016`, `-0017`): the pages are not committed; **`doc_package`** makes `corelr-<version>-<variant>-api.zip` per image and a `SHA256SUMS` (2.3 MB and 2.7 MB; not byte-for-byte reproducible, measured: the zip records the files' access and change times); the **gaps are an inventory in git**, `docs/doxygen/gaps_serial.txt` (332 lines, 324 after the `__attribute__` fix below) and `gaps_sd.txt` (325, then 317), a ratchet checked by `doxygen_output` for the Doxygen that wrote it (1.15.0; another version only prints notes). GitHub Pages was explained and left for later. Only the Doxygen tests and the mutants of `doxygen.txt` (114, all caught) were run; sources and firmware untouched.
Maintainer's remarks on the pages, step 1: `PREDEFINED` now holds `"__attribute__(x)="` (the Doxygen syntax for the idea of `-D__attribute__=`). Measured in a private build tree: the false structure `__attribute__` is gone (`Entry_t` has its page), and **8 functions of `Motion.c` that were listed as undocumented are documented** (the attribute line before the definition had cut them from their comment): `serial` 328 -> 320 undocumented, `sd` 321 -> 313, the 4 incomplete lists unchanged. The baselines lost 8 lines each, none gained. Sources and firmware untouched.
Step 2 of the maintainer's remarks, done: a **Hardware chapter** (`docs/doxygen/hardware.md`, a short text of ours; the pins stay those of the *Hardware configuration* topic): overview, motors, servo, joystick, LCD, the SD module (`sd` only), the geometry and the tension pictures. The wiring pictures are CC BY-SA 3.0 (Fritzing parts): the page credits Fritzing, the licence and each author, and `test_doxygen.py` checks it (and that each wiring SVG keeps its credit). Looked at in a browser: the pictures keep their ratio. Also answered: the target regenerates everything when one file changes (Doxygen has no incremental mode; no clean needed), and the pictures are now dependencies. `__attribute__`: no assembler to document (3 compiler barriers in the C files, one `.S` in the vendored core that Doxygen does not read). Only the Doxygen tests and the mutants (132, all caught) were run; no source, no image touched.
Step 1 of the maintainer's next request (the GCode page), done: `docs/doxygen/gcode.md`, a section per image; every row is a line of GCode and **the answer of the real parser** for that image, checked by `gcode_page` (`tests/host/gcode_probe.c`, built per image; 40 mutants, all caught), which also requires that the words and letters the parser knows are exactly those of the accepted rows. Differences measured between the images: `G53`, `M9`, `T` and `Z` are `error:20` in `sd`. Found while writing it: **a line over 47 characters is cut, not refused, by the SD reader** (the first 47 characters are kept), so a coordinate may be shortened silently -- known (`D-SDReader`), written on the page, reported by `gcode_check.sh`. Only the Doxygen and GCode-page tests and their mutants were run; no firmware source touched.
Maintainer's next step (the specifications in the documentation, explicit aliases), done: a **Requirements** chapter (the 22 requirement files read unchanged, plus `SerialProtocol.md`, which they link to), an invisible anchor `<a id="REQ-...">` on each of the 249 headings, `@req{}` (alias `req{1}`) for the 383 citations of the comments that Doxygen reads (comments only). Measured: **both `.hex` unchanged** (`sd` `e9b77dcb`, 30 524 B; `serial` `f11b8a78`, 28 766 B; built in a private tree before and after), markup warnings 0, and **no citation names a requirement that does not exist**. `tools/analysis/req_refs.py` + `req_refs` (46 mutants) keep it so; the rule is in `STD-FileOrganization.md` and `CONTRIBUTING.md`. Only the documentation tests and their mutants were run (and the two images built); the full host suite was not.
Maintainer's remarks on the Requirements chapter (2026-10-29), done: **three levels** (Requirements -> Functional | Design requirements -> one page per file; each requirement a section below), the requirements that cite each other are **links** (244 in the pages) because the requirement files now go through an **input filter** (`req_refs.py --filter`: page id, heading labels, citations as `@req{}`; the files themselves stay as GitHub shows them), and the headings carry Doxygen labels. Reproduced the "link goes to the top of the page" report? **No**: in the pane's browser (Chromium, over http) the jump to a requirement worked before and after; the labels are the form Doxygen's own scrolling script is written for, to be confirmed in the maintainer's browser. Found on the way: a code span with a backslash and a Doxygen command (the Build requirements' own text) made the labels after it unreadable -- a requirement file must not write one (check `command`). Markup warnings 0 in both documentations; the doc tests and the mutants (`req_refs` 72, `doxygen` 149, all caught) were run; no source touched.
Maintainer's remark (2026-10-29), done: the zipped documentation must be **complete**. Scanning every link of the output found **1 link outside** (`../../docs/hardware/images/Geometry.svg`, from the Calibration and Motion requirements), **3 dead links** (the `.dox` files' own `@file` pages linked to a source listing that does not exist) and **1 `file:` URL** (`file:line` in the protocol's text). Fixed: the filter writes those pictures by name (Doxygen copies them next to the pages), the `.dox` headers are plain comments, `file:line` is defused, the protocol goes through the filter. **0 such link in both documentations now**, no resource loaded from the network (the only http links are the Doxygen credit, the Fritzing and licence links of the hardware chapter), and `doxygen_output` checks all of it (163 mutants of `doxygen.txt` and 80 of `req_refs`, all caught). The maintainer confirmed that the link to a requirement now arrives on the requirement. No source touched.
Maintainer's question on the requirements of one image only (2026-10-29), done: **a banner, not hiding** (hiding would break the links: the shared modules cite requirements of one image, and a requirement of one image often says what the other lacks). Each requirement may carry `*Applies to: serial image only*` / `sd` under its anchor (none = both); the filter (`--image`, passed by the Doxyfile) marks it in the other documentation: heading `(serial image only: not in this image)`, a **Not part of the sd image** banner, and the title of a page whose requirements are all tagged; the protocol is the serial link's (`--applies serial`). **56 tags written, the obvious ones** (`D-SDReader` 12, `D-Uart` 9, `D-SerialLink` 12, `F-SerialControl` but `-0003` 14, `F-JobStorage` 2, SD browser 2, serial-only by title 5); `req_refs` checks the tags against `cmake/variant_modules.txt`, `doxygen_output` the titles (111 + 172 mutants, all caught). The ambiguous ones are left untagged and put to the maintainer. No source touched.
Maintainer's answers on the ambiguous requirements (2026-10-29), done: the UI difference (`D-LcdJoystickUI-0019`), the `Z` word and the program end stay **untagged** (both images). `D-LcdJoystickUI-0028` was out of date (it said 8 blocks in serial and 4 in SD; `SwConfig.h` has 8 for both since 2026-10-13): **amended** -- the depth is a lever to gain RAM, 8 is recommended for all (the 2026-10-10 decision kept as history). **Motion has no difference between the images**, so the tag I had put on `D-Motion-0030` (the machine offset, "serial variant" in its title) was **removed**, and the mentions of one image in the Motion requirements are a **TODO to review** (`docs/TODO.md`), among them a real difference in the code: `Motion_GetMachineOffset()` is compiled under `#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL`. 55 tags now. Also noted for the protocol's owner: `docs/protocol/SerialProtocol.md` line 34 still says "8 blocks in the serial variant, 4 in the SD one". No source touched.
Maintainer's request (2026-10-29): "correct the protocol, and start FW-nn etc." Done. **`docs/protocol/SerialProtocol.md`** (edited at the maintainer's word, the host side owns it): line 34 said "8 blocks in the serial variant, 4 in the SD one"; now 8 in both, a RAM lever, and a row in its revision history. **The protocol is a page of the documentation** (marked as not part of the `sd` image), and its items are links: labels for its numbered sections (31) and invisible anchors for the first table row of each request, decision, point and measurement (40), made by the input filter (the file is otherwise unchanged); aliases `@proto{FW-03}` / `@protosec{4-6,4.6}`; **39 citations in the Doxygen comments and pages rewritten** (`req_refs --convert`), 144 links inside the protocol and 42 from the requirements made by the filter. `req_refs` checks the items (none cited that the protocol does not define), `doxygen_output` the links of a code page. Comments only: **both `.hex` unchanged** (`sd` `e9b77dcb` 30 524 B, `serial` `f11b8a78` 28 766 B). `req_refs` 177 mutants and `doxygen` 178, all caught; markup 0 in both documentations.
Maintainer's request (2026-10-29): for each function the **call graph**, for each variable the list of the functions that **reference it**, and for each function the list of the variables it references. Done in the Doxyfile (`CALL_GRAPH`, `REFERENCED_BY_RELATION`, `REFERENCES_RELATION`; the graph of the callers is left off: the "Referenced by" list says who calls). Measured: 12 s for both images, HTML 20 MB (`serial`) and 23 MB (`sd`), zips 3.9 and 4.4 MB, 170 and 220 call graphs; markup 0; `doxygen_output` checks the page of a module for the three (187 mutants of `doxygen.txt`, all caught). No source touched.
Maintainer's request (2026-10-29): note the gaps of the protocol file for the host side. Done, as `docs/design-notes/SerialProtocol-review-2026-10-29.md` (nothing else changed in the protocol): a **table of requests cut by two blank lines** (`FW-12` to `FW-14` come out as loose paragraphs), `P5` and `P7` cited many times but with **no row** in the table of points (they are bullets under "Still open", `P5` being "decided"), points and `FW-13`/`FW-14` out of order, `file:line` read as a URL; requirement citations in four forms (full 40, short 18, file-level 4, relative on 39 lines; all the ones that name a requirement exist); **ten statements out of date** (the SD and serial flash figures, the transmit ring "64" against 128, "not yet run on the machine" and "Candle's own jog buttons not yet tried" against the bench, "Proposal" for the `$$` list that is built, two dated titles); and what was compared and found right (the error table, the per-variant command set, the welcome line, `$I`, `$$`, the states, the 128-byte ring, the 80-character line, `WCO`). The filter for the short and relative requirement citations of the protocol is planned for 2026-10-30 (`docs/TODO.md`).
Maintainer's follow-up (2026-10-29): **`CALLER_GRAPH = YES` as well** (the graph of the functions that call each function; I had left it off, the "Referenced by" list saying who calls). Measured: the run takes 20 s for both images, HTML 24 MB (`serial`) and 28 MB (`sd`) (it was 20 and 23 MB with the call graph alone), 257 and 299 caller graphs; markup 0; `doxygen_output` checks the caller graph of a module page (190 mutants, all caught). `dot` is installed by `sudo apt install graphviz`. No source touched.
Maintainer's question "and the EEPROM layout?" (2026-10-29): it was **not** integrated; now it is. `docs/protocol/EepromLayout.md` is a page of both documentations (its own filter pattern: no `--applies serial`, the ring exists in both images), linked from the main page, its requirement citations are links, and the code that cites it by path (`SwConfig.h`, `Calibration.c`: 3 citations) and `REQ-CoreLR-D-Calibration` link to it through the new alias `@docref{EepromLayout,docs/protocol/EepromLayout.md}` (`req_refs` checks it: 196 mutants, all caught; `doxygen_output` 195, all caught). Markup 0. Comments only. Its relative citations (`-0006` after a full one) stay plain, like the protocol's.
Maintainer's remarks on the sketch and the options (2026-10-29): (1) "the .ino may not be read correctly": **it is** -- `EXTENSION_MAPPING = ino=C++` was already there, and the source listing of the sketch links its call to `Calibration_Init()`; the hidden `#include <Arduino.h>` is not needed (the sketch calls nothing of the Arduino API); (2) **"pass the compiler's options to Doxygen": done** -- `F_CPU`, `ARDUINO`, `ARDUINO_AVR_NANO`, `ARDUINO_ARCH_AVR`, `__AVR__`, `__AVR_ATmega328P__` from one list, `cmake/TargetDefs.cmake`, read by `Firmware.cmake` and `Doxygen.cmake` (both `.hex` unchanged; our own gaps unchanged; the Arduino core shows 17 more warnings, 14 more pages, 1 more caller graph). `doxygen_output` checks that nothing is spelled out twice (211 mutants of `doxygen.txt`, all caught). **The missing caller graph of `Calibration_Init()` has another cause** (measured by a scratch run with `EXTRACT_ALL = YES`, which makes it appear): Doxygen draws the callers that are themselves *documented*, and its only caller is `setup()`, which has no comment (it is in the gaps). Options put to the maintainer.
Maintainer's choice (2026-10-30): **two Doxygen runs for each image** -- the inventory run (`EXTRACT_ALL = NO`, XML only: an inventory without any output gives no warning, measured) keeps writing `doxygen-warnings.log` that the gap baselines and the tests read; the HTML run (`EXTRACT_ALL = YES`) documents every member, so `Calibration_Init()` now has its caller graph (its only caller is the sketch's `setup()`, which has no comment) and `setup()` its call graph and its references. The files of the other image's modules (`cmake/variant_modules.txt`) are left out of both runs: with `EXTRACT_ALL = YES` they would get an empty page. Measured: 31 s for both images, HTML 33 MB (`serial`) and 40 MB (`sd`); the log of the HTML run has 0 markup warnings; `doxygen_output` checks it (215 mutants, all caught). No source touched.
Maintainer's request (2026-10-30): **comment the callers that belong to us, starting with the sketch.** Done for `grbl_corelr.ino`: `setup()` (the order of the initialisations and why `IsrDispatch_Init()` comes last), `loop()` (the six stages of a pass and the job state machine it runs), `JobHalt()`, `JobResume()`, `StorePositionNow()`, `POSITION_STORE_DELAY_TICKS`, `s_stillLastTick`, and in the SD image `s_haveLine`. **7 gaps closed in `serial` (313 left), 8 in `sd` (305 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). The gap baselines shrank (the ratchet). Most gaps left: `Motion` 88, `Stepper` 52, `LcdJoystickUI` 36, `SwConfig` 30, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial).
Maintainer's choice (2026-10-30): **Motion (88)** first. Every member of `Motion.c` and `Motion.h` that Doxygen listed is now commented, from the code (no comment copied from an older design): the fields of `Move_t` (one `/**< */` per field; the multi-declarator lines were split, the layout is the same), `PlannerBlock_t`, `ArcGeo_t`, `Motion_Position_t`, `Motion_CableLengths_t`, the macros `PLAN_ARC`, `PLAN_CW`, `MOTION_Y_FROM_INTERNAL`, `MOTION_OFFSET_FOR`, `MOTION_ORIGIN_*`, the statics `s_currentPosition`, `s_planHead`, `s_planCount`, and the functions `QuantizeToSteps`, `JunctionVelocity`, `StartMove`, `EndSegment`, `MotionStepOnce`, plus the missing `@param` of `TravelCheck` and `Motion_SetOrigin`. Two comments were on the wrong function and moved (`ReachableV`'s sat above `V2Gain`'s, `StartMove`'s above `BeginTimeline`'s; a duplicated `noinline` attribute left in front of `BeginTimeline`, which already has `CORELR_NOINLINE`, was dropped). **88 gaps closed in both images (`serial` 227 left, `sd` 219 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`, measured in a private AVR tree). `req_refs.py` lint, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches` pass; one mutant of `origin.txt` named the `MOTION_OFFSET_FOR` line with its trailing comment (now a comment above): rewritten, batch re-run, 47 of 47 caught. Most gaps left: `Stepper` 52, `LcdJoystickUI` 36, `SwConfig` 30, `GCodeParser` 23, `Calibration` 18, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial).
Maintainer's choice (2026-10-30/31): **Stepper (52)**. Every member of `Stepper.c` that Doxygen listed is commented: the 48 per-wire macros (`MOTOR_<L|R>_WIRE<n>_PORT/DDR/BIT` and `_SET/_CLR/_DDR_SET`, one trailing `/**< */` each, in both branches of the `#if`), `Queue_t`'s `Indices` and `Entries`, `QueueIndices_t`'s `WriteIndex` and `ReadIndex` (who advances which), `s_queue`, `s_lastCoilBits` (what a pen-only entry repeats, and why a differing value invalidates the stored position), `s_enqueueHalted`, `s_pendingTicksRemaining`. **54 gaps closed in `serial` (173 left) and in `sd` (165 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`, private AVR tree). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Most gaps left: `LcdJoystickUI` 36, `SwConfig` 30, `GCodeParser` 23, `Calibration` 18, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial).
Maintainer's choice (2026-10-31): **LcdJoystickUI (36)**. Every member of `LcdJoystickUI.cpp` that Doxygen listed is commented: the menu and step-size label tables (one trailing `/**< */` per label, a comment on each table), the macros `LCDJOYSTICKUI_HAS_SD`, `_JOG_LIMIT_UM`, `_LENGTH_BOUND_UM`, `_DISPLAY_CAP`, `_REPEAT_TICKS`, the statics `s_machineState`, `s_uiMode`, `s_swPressStartTick`, `s_menuSelectedIndex`, `s_adjustStepCountR` and the three SD-browser cache fields, and the functions `PrintPadded_P`, `StatusTagFor`, `EnterMainMenu`, `AdvanceMenuSelection` (its comment sat above `MenuItemAvailable`'s: moved), `EnterMenuItem`, `HandleMainMenuInput`, `HandleJogAdjustmentInput`, `HandlePenWizardInput`. **36 gaps closed in `sd` (129 left), 32 in `serial` (141 left; the 4 others belong to the SD image), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches` pass. Most gaps left: `SwConfig` 30, `GCodeParser` 23, `Calibration` 18, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial).
Maintainer's choice (2026-10-31): **SwConfig (30)**. The 30 macros of `SwConfig.h` that Doxygen listed are commented, one trailing `/**< */` each, from what reads them: the two `CORELR_JOB_SOURCE_*` values, `CORELR_DEBUG_MISS_COUNT`, `CORELR_GCODE_Z` (both branches), `CORELR_MATH` and its two values, `CORELR_MENU_HOME`, `CORELR_Y_UP`, the status tags, the ternary release threshold, the label and decimal-point columns, `LCDJOYSTICKUI_JOG_SLICE_MAX_MS`, `LCDJOYSTICKUI_ADJUST_ACCEL_STEPS_PER_S2`, the step-size count and column, the pen characters (the plain comments of the glyph branch became doc comments, the ASCII branch got its own), `MOTION_STEPS_PER_TASK_MAX`, `SDREADER_QUEUE_SIZE`, `SDREADER_BROWSER_EXT3`, `STEPPER_COUNTS_PER_S`, `STEPPER_PEN_SHIFT/MASK`. One stale figure corrected in passing: the text of `MOTION_STEPS_PER_TASK_MAX` said "8 steps per pass", the value is 16. **30 gaps closed in both images (`serial` 111 left, `sd` 99 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Most gaps left: `GCodeParser` 23, `Calibration` 18, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial).
Maintainer's choice (2026-10-31): **GCodeParser (23)**. The fields of `ParsedLine_t` (every word of a tokenized line: the `Has*` flags, the values in um, `MotionWord`, `DistWord`, `OriginCmd`, `LValue`/`PValue`, and `Machine` in the serial image) and `GCodeNumber_t` are now `/**< */` comments (the plain `/* */` ones were turned into doc comments, the bare ones written from the tokenizer), and `s_state` has its own comment (who resets it, who changes it: `ApplyParsedLine()` and the return-to-origin request). **23 gaps closed in `sd` (76 left), 24 in `serial` (87 left; `Machine` is the serial image's), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Most gaps left: `Calibration` 18, `SDReader` 18, `PenControl` 14, `Uart` 11 (serial), `HwConfig` 9.
Maintainer's choice (2026-10-31): **Calibration (18)**. Commented, from the code and the EEPROM layout page: `EepromImage_t::Magic` and `::Params`, the twelve `POS_*` macros of the position ring (field, payload and slot sizes, the offsets of Seq, Flags, payload and CRC, the slot count, the valid flag, the magic mask and `POS_FLAGS()`; the two plain comments of `POS_FIELD_BYTES` became doc comments), `s_params`, `s_isCalibrated` (what sets and clears it; not stored) and the three fields of `Calibration_KinematicsParams_t`. **18 gaps closed in both images (`serial` 69 left, `sd` 58 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Most gaps left: `SDReader` 18, `PenControl` 14, `Uart` 11 (serial), `HwConfig` 9, `LcdI2c` 7.
Maintainer's choice (2026-10-31): **SDReader (18)**. In fact 16 gaps in the `sd` image (the module is not in `serial`): `LineQueue_t`'s two fields and `s_queue`, `s_volume`, `s_jobFile`, the listing cache (`s_dirCount`, `s_fileCount`, `s_rcValid`, `s_rcKind`, `s_rcRank`, `s_rcName`: what each holds and when it is valid), `HasParentDirectory()`, `InvalidateListing()`, the pin-guard macro `SDREADER_PIN_IS_SPI` and `SDReader_Init()` (in the header). One stale sentence fixed in `EntryMatches()`: it said `SDREADER_BROWSER_EXT2` (".gcode") could never match; the extension has been ".gco" since 2026-09-10, the text now says why. **16 gaps closed in `sd` (42 left), none added, `serial` untouched (69 left); markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Most gaps left: `PenControl` 14, `Uart` 11 (serial), `HwConfig` 9, `LcdI2c` 7.
Maintainer's choice (2026-10-31): **PenControl (14)**. Commented from the servo-frame code: the pin macros (`PEN_PORT`, `PEN_DDR`, `PEN_BIT`, `PEN_SET()`, `PEN_CLR()`), the frame and pulse constants (`PENCONTROL_FRAME_TICKS`, `PENCONTROL_PULSE_MIN_US`, `PENCONTROL_PULSE_MAX_US`), the three table slots (`SLOT_DOWN`, `SLOT_PREVIEW`; `SLOT_UP` too), `s_angleDown`, the frame state the Timer2 ISRs share (`s_endTick`, `s_endFrac`) and the request/issue pair (`s_requestedPenDown`, `s_issuedPenDown`). **14 gaps closed in both images (`serial` 55 left, `sd` 28 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Gaps left: `Uart` 11 (serial), `HwConfig` 9, `LcdI2c` 7, `Trig` 2, `TextFmt`, `IsrDispatch`, `JogCurve`, `LcdGlyphs`, the sketch.
Maintainer's choice (2026-10-31): **SerialLink (16)**, serial image only. Commented: the transmit-room constants (`SERIALLINK_NEED_REPLY`, `_STATUS` in both branches, `_DOLLAR`), the link's own grbl status numbers (`SERIALLINK_ERR_INVALID_STATEMENT` 3, `_NOT_IDLE` 8, `_LOCKED` 9, checked against section 4.6 and 4.7 of the protocol), `COMMENT_LINE`/`COMMENT_PAREN`, the receive state (`s_line`, `s_len`, `s_comment`, `s_welcome`, `s_wcoSentY`), `kWelcome`, `kNotCalibrated` and `SendError()`. **16 gaps closed in `serial` (39 left), `sd` untouched (28 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `req_refs.py`, `doxygen_*`, `doc_links`, `gcode_page`, `mutant_batches`, `host_math48/64` pass. Gaps left in `serial`: `Uart` 11, `HwConfig` 9, `LcdI2c` 7, `LcdGlyphs` 4, `JogCurve` 3, `Trig` 3, `TextFmt` 1, `IsrDispatch` 1.
Maintainer's choice (2026-10-31): **Uart (11)**, serial image only. Commented: `UART_UBRR` and `UART_ACTUAL_BAUD`, the four ring indices other than the head (`s_rxTail`, `s_txHead`, `s_txTail`; who writes each), `s_realtime`, the gap pair (`s_gapAt`, `s_gapSet`) and the two storage arrays. **11 gaps closed in `serial` (29 left; the file's last undocumented member is a function), `sd` untouched (28 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). Related tests pass (16 of 16). The maintainer's four remarks on the call graphs and the software version were written down in `docs/TODO.md` for a pass after the documentation of the functions.
Maintainer's choice (2026-10-31): **HwConfig (9)**. Commented: the joystick pins (`LCDJOYSTICKUI_X_PIN`, `_Y_PIN`, `_SW_PIN` -- active low, `INPUT_PULLUP`, as `PinMapping.md` says), the two polarity flags, the LCD's I2C address (`0x27` or `0x3F` by the backpack's jumpers, as `PinMapping.md` says), its columns and rows (two rows: what `LcdI2c.c` supports) and `STEPPER_MOTOR_R_REVERSED`. **9 gaps closed in both images (`serial` 20 left, `sd` 19 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). Related tests pass (16 of 16). Owner's decision noted in `docs/TODO.md`: `INTERACTIVE_SVG = YES` goes into the pass after the documentation of the functions.
Maintainer's choice (2026-10-31): **LcdI2c (7)**. The backpack's bits (`LCDI2C_RS`, `LCDI2C_E`, `LCDI2C_BL`) and the four TWI status codes (`TWI_ST_START`, `_SLAW_ACK`, `_DATA_ACK`, `_TIMEOUT` -- the last one is ours, not a TWSR value) are commented; the plain comments they had became doc comments. **7 gaps closed in both images (`serial` 13 left, `sd` 12 left), none added; markup 0 in both runs; comments only: both `.hex` unchanged** (`sd` `e9b77dcb`, `serial` `f11b8a78`). Related tests pass (16 of 16). Gaps left: `LcdGlyphs` 4, `JogCurve` 3, `Trig` 3, `TextFmt` 1, `IsrDispatch` 1, `Uart` 1 (serial).
Maintainer's choice (2026-10-31): **the rest** -- and with it **the inventory is empty**. `TIMER2_OCR2A` (`IsrDispatch.c`); `JOGCURVE_RANGE`, `JOGCURVE_USE_TABLE`, `s_remainderY` (`JogCurve.c`); the two glyph arrays and the two code-agreement checks (`LcdGlyphs.c`); the `@param c` of `TextFmt_Char()`; `kCosCoef` and `kAtanCoef` and the `@param outX`/`outY` of `Trig_Rotate()` (`Trig`); `s_rxHead` (`Uart.c`, the "rings" comment is a plain one and documented nothing). One plain requirement citation in a new doc comment caught by `req_refs.py` and written as `@req{}`. **`gaps_serial.txt` and `gaps_sd.txt` now hold their header only: 0 undocumented members and 0 incomplete `@param` lists in both images, 0 markup warnings; the ratchet stays in force** (a new gap fails `doxygen_output`). Comments only: both `.hex` unchanged (`sd` `e9b77dcb`, `serial` `f11b8a78`); related tests pass (16 of 16). Gaps in this session, by module: Motion 88, Stepper 54, LcdJoystickUI 36, SwConfig 30, GCodeParser 23, Calibration 18, SDReader 16, SerialLink 16, PenControl 14, Uart 12, HwConfig 9, LcdI2c 7, the rest 13. Next: the maintainer's pass (call-graph self-edges, `INTERACTIVE_SVG = YES`, graph limits, one-place version: `docs/TODO.md`) and the protocol file through the filter.
Maintainer's four remarks on the documentation (2026-10-31), done. (1) **Self-calls in the call graphs**: measured in Doxygen's XML, 11 of our functions referenced themselves (`BlockShape`, `BeginSegment`, `BlockLength`, `EndSegment`, `ISqrt32`, `IdealAt`, `IdealSubSteps`, `JunctionVelocity`, `ReachableV`, `UnitDir`, `Hi`), all defined after a line holding `__attribute__((noinline))` alone, which Doxygen's non-preprocessing code browser reads as a call; `BlockShape` had no arrow to `Tangent` or `UnitDir`. The 11 lines are now `CORELR_NOINLINE` (same attribute): **both `.hex` unchanged at that step** (`sd` `e9b77dcb`, `serial` `f11b8a78`). `doxygen_output` fails on a bare attribute line (`check_attribute_lines`). (2) `INTERACTIVE_SVG = YES`; (3) `DOT_GRAPH_MAX_NODES = 250`, `MAX_DOT_GRAPH_DEPTH = 0` (Doxygen's 50 had cut 10 graphs in `serial`, 6 in `sd`, the most important ones; none of ours is cut now); HTML 33 -> 45 MB and 40 -> 54 MB; `doxygen_output` checks the graph settings and the absence of a red frame (237 mutants of `doxygen.txt`, all caught). (4) **The version**: `firmware/config/Version.h` (then 0.1.0-dev), `tools/analysis/check_version.py`, ctest `version`, the `release` workflow, `REQ-CoreLR-F-Build-0007` and `REQ-CoreLR-D-Build-0020`; 30 mutants, all caught. **The serial image changes, by design: 28 766 -> 28 776 B of flash (1 944 free), RAM 936 unchanged, hash `f11b8a78` -> `be7aba74`**; the SD image is unchanged (`e9b77dcb`). Host tests (`host_math48/64`, with the `$I` reply checked), docs 0 markup, `req_refs`, all other related tests pass. **Not flashed.** `$I` and the protocol: see `docs/design-notes/SerialProtocol-review-2026-10-29.md`, section G.
Maintainer's request (2026-10-31): **the protocol file through the filter, for its short, file-level and relative citations**. `tools/analysis/req_refs.py --protocol` now resolves `D-GCodeParser-0013` (short), `REQ-CoreLR-D-LcdI2c` (a link to the page of that requirement file) and `-0006` (the prefix taken from the context: the citation before it in the line or table cell, else the first after it in the same table row, else the one above it in the same column -- the table of section 6.4 cites `F-GCodeInterpreter-0001` once and its rows say `-0003`, `-0012`...). Nothing else is guessed, because a wrong link is worse than none: **9 relative citations have no context** (lines 178, 179, 194, 199, 291, 308, 371, 480) and stay plain text; `req_refs.py --unresolved` lists them and the review note (`docs/design-notes/SerialProtocol-review-2026-10-29.md`, section H) gives the identifier each probably means, for the host side to write in full. The lint reads the protocol: a citation in any form that names no requirement or file is an error. **A defect found on the way:** the first row of each item (`FW-08`, `P13`, `M-1`...) skipped the conversion altogether, its full citations too; fixed. `doxygen_output` fails on a short or a file-level citation that is plain text on the protocol's page. Measured: 81 requirement links on the page of the `serial` documentation, none dead; docs 0 markup; **230 mutants of `req_refs.txt` and 245 of `doxygen.txt`, all caught** (a first run counted them caught while the test itself was failing: re-run once the test was green). All 20 host tests pass. No firmware change: both `.hex` unchanged from the version step (`sd` `e9b77dcb`, `serial` `be7aba74`). `SerialProtocol.md` itself is not edited.
