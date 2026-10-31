# Bring-up tests

> **Ported to CMake, 2026-10-11** (`REQ-CoreLR-D-Build-0011`): `cmake --build build/avr-default --target bench` builds T01, T02 and T05; flash one with `--target upload_bench_T05_PhaseCheck`
> (resets the board). They are built with the SD variant's settings and use the Arduino `Serial`, which the SD firmware leaves free. **Built, flashed and run on the board in this form (maintainer).** The
> sketches below were first run before the restructuring and the v2 `Stepper`. Differences from the originals: T01 no longer uses the `Wire` and `LiquidCrystal_PCF8574` libraries (not in this
> repository, not used by the firmware): it scans the bus with the raw TWI registers and drives the glass with the firmware's own `LcdI2c`; T03 no longer carries a copy of the firmware's loop but
> includes the real `grbl_corelr.ino`; **T04 was removed** (obsolete since the v2 `Stepper`: it measured a 4 kHz tick that no longer exists; its last version is in the git history before the commit that
> ported the bench).

One sketch per step, each proving as little as possible so that a
failure points somewhere specific. Open the Serial Monitor at **115200
baud** for all of them.

These exist because the firmware, on its first power-up, reset roughly
once a second with no way to see why: the SD variant deliberately does
not use `Serial` (`REQ-CoreLR-F-SerialControl-0003`), so the board had
no voice. Here `Serial` is free -- no SD card, no flash pressure -- and
it is the point.

| Test | Contains | A failure here means |
|---|---|---|
| `T01_LcdJoystick` | LCD + joystick, **no CoreLR code at all** | wiring, I²C address, or supply |
| `T02_Startup` | the **real modules**, real order, traced step by step | localises a start-up crash to one call |
| `T03_StackWatch` | **the real firmware** (`grbl_corelr.ino` included, `setup()`/`loop()` wrapped) + a stack painter and a line `S <used>/<room>` (hexadecimal) every 2 s on the UART | whether the stack is reaching the statics. **Does not fit the Nano any more** (SD firmware 30 524 + ~270 bytes: 82 over, 2026-10-26): `STANDALONE`, outside `bench`, until the image or the painter shrinks; the SD stack is measured statically (`stack_depth.py`) meanwhile |
| ~~`T04_StepRate`~~ | *(removed 2026-10-11)* one motor, flat vs ramped rate sweep, plus a real ISR-frequency measurement | it measured the 4 kHz tick of the first Stepper; its question was answered (the queue was starving) |
| `T05_PhaseCheck` | wave drive and the production table at 2 steps/s | whether the coils are wired in the assumed order |
| `T09_LcdRom` | the LCD's **character ROM**: two lines of the 256-code table (`0x00: cccccccc`), the joystick scrolls by a line, SW goes back to the top | which ROM the controller carries (A00 Japanese / A02 European), i.e. whether it has up/down arrows for the pen glyph |
| *(planned)* `T04_Ui` | `LcdJoystickUI` alone, rest stubbed | our UI module |
| *(planned)* `T05_Servo` | the pen servo alone | servo wiring or supply under load |
| *(planned)* `T06_Stepper` | one motor through `Stepper` | wire order, or the ISR under load |
| *(planned)* `T07_Motion` | two motors through `Motion` | the kinematics |
| *(planned)* `T08_Sd` | the SD card alone | card, formatting, or `SDReader` |

## `gcode/` — GCode to run on the machine
(For the **travel zone**, which depends on the machine's own geometry, `tools/generators/gen_gcode.py --limits D,OX,OY` writes more files here or anywhere else: a big drawing that fills the
zone and four refusal tests -- `docs/bringup/Bringup-Status.md`, "Testing the limits from the SD card". They are not committed: they are specific to a geometry.)

Not sketches: GCode files for the SD card or for a sender (Candle) once the firmware is on the machine. Written by `tools/generators/gen_gcode.py` (do not edit
them by hand: change the generator and rerun it), checked by `tools/bench/gcode_check.sh` (also part of `tests/host/run_all.sh`). Each starts with
`M5` then `G90 G0 X0 Y0` (pen up, back to the origin) and stays inside a 200 x 200 mm square centred on the origin; **the machine must be calibrated, and
the drawing origin is where it was calibrated**. Y is up the wall.

| File | What it is | What to look at |
|---|---|---|
| `01_Commands.gc` | every supported command once, in short labelled sections | the pen (listen to the servo), each axis, the corners, a plus sign, relative mode, modal words, comments, `G2`/`G3`, full circles: does the machine do what the comment says? |
| `02_Square_200.gc` | a square, 200 x 200 mm | the sides: straight, equal, closed; the corners; whether the end meets the start |
| `03_Circle_R100.gc` | a circle R = 100 mm, two half circles | round or oval (the kinematics), closed or not |
| `04_TestPattern.gc` | a test card: border, 20 mm grid, axes, diagonals, circles R = 20…100, four corner circles, a centre mark | the grid's regularity and squareness all over the sheet, circles against the grid, small-arc quality, 22 000 waypoints and ~7 m of ink: the speed and the pen lifts |
| `05_Chords.gc` | four circles R = 30 mm cut into `G1` chords of 0.2, 0.5, 1 and 2 mm (1 630 lines, 754 mm of ink) | **the look-ahead on short segments**: time each circle (stopwatch, or Candle's job time): expected **about 9 s each at 20.7 mm/s on the serial variant** (188.5 mm); on the SD variant (planner depth 4) the 0.2 mm circle should take about 12 s (15.4 mm/s), the others 9 s. A circle that takes much longer is the planner starved, or the sender too slow (Candle sends lines as fast as the 127-byte counting allows) |

`preview/*.svg` shows what the firmware's planner will really do with each file (black: drawn, red dashes: pen up; open in a browser); regenerate with
`tools/bench/gcode_check.sh --svg tests/bench/gcode/preview tests/bench/gcode/*.gc`.

## Why T01 contains none of our code

The natural first test would exercise `LcdJoystickUI`, but that module
pulls in `Motion`, `Calibration`, `PenControl`, `Stepper`, `SDReader`,
`SysTick` and `TextFmt` -- very nearly everything. A failure would
isolate nothing. T01 depends only on `Wire` and the LCD library, so if
it fails, the fault is not ours.

## How the later tests will reuse the real modules

By **symlinking** the module files into the test sketch's `src/`
directory: the Arduino IDE compiles `src/` recursively and follows
symlinks, so the tests exercise the real code with no copy to drift
out of date. Whatever a test does not exercise is stubbed inside the
sketch, which has the useful side effect of making each module's true
dependency set explicit.

(Symlinks are a POSIX convenience; a Windows checkout would need real
copies instead.)

## What T01 reports

1. **Why the board last reset**, from `MCUSR` -- brown-out, watchdog,
   external and power-on are four quite different investigations, and
   this distinguishes them. Beware: some bootloaders clear `MCUSR`
   before the sketch runs, so an all-zero read is inconclusive rather
   than meaningful.
2. **An I²C bus scan.** PCF8574 backpacks are `0x27` or `0x3F`
   depending on the chip variant and its jumpers, and a
   blank-but-backlit screen is exactly what the wrong address looks
   like. The scan finds the real one instead of assuming.
3. **A heartbeat counter**, on both the serial line and the LCD. If the
   board is resetting, the counter returns to zero and its last value
   gives the period directly.

## Results so far

### T01 — passed, 2026-09-11

Hardware is sound, so the fault is in our firmware.

- **LCD answers at `0x3F`**, which is what `HwConfig.h` already sets.
  Not the address, then.
- Joystick centred at **X=521, Y=509** against an assumed centre of 512
  (`LCDJOYSTICKUI_ADC_CENTER`). The offset of 9 is well inside the ±32
  dead zone, so no retune is needed.
- The heartbeat counts up steadily: **no reset**.
- `MCUSR` read as 0 — this bootloader clears it before the sketch runs,
  so the reset-cause report is inconclusive on this board. Worth
  knowing: that diagnostic is unavailable to us here.

One observation from T01 that explains the symptom. The backlight is
latched in the PCF8574, and an Arduino reset does **not** reset the
backpack -- so the backlight stays on across a reset and only blinks
when `lcd.begin()` re-initialises it. "Backlight on, with a brief
extinction every second" is therefore a reset roughly every second,
whose `setup()` is dominated by the SD init timeout, and not the
"mostly dark" pattern a naive reading would predict.

### T02 — the symlink arrangement (historical: the CMake build compiles the firmware's modules directly, no link)

`src/corelr` was a symlink to the flat `grbl_corelr` folder of the former repository (now `firmware/src`), so the test compiles the
**real** modules -- all 13 files, including the nested `src/sdfat/`.
The nested include (`SDReader.cpp` asks for `src/sdfat/SdFat.h`)
resolves correctly because the symlink points at the directory, not at
individual files. One symlink, no copies, nothing to drift.

### T02 — passed, 2026-09-11: no crash at all

Every module initialised, the first `loop()` pass completed, and the
heartbeat counted past 36 with no reset. T02 links the same modules in
the same order as the firmware, so what is left to explain the
difference is small and specific:

| | T02 | firmware |
|---|---|---|
| Static RAM | 1686 | **1732** |
| Free | 362 | **316** |
| `loop()` | the five `*_Task()` calls | plus the job state machine, the parser hand-off and the UI events |

46 bytes of RAM, against a *computed* worst-case stack margin of 27 --
a figure with known blind spots, since `tools/analysis/stack_depth.py` follows
direct calls only and a call through a vtable truncates the chain it
measures. That is what T03 exists to settle, by measurement rather than
analysis.

### T03 — what it measures, and how to read it

The free RAM is painted with `0xC5` at start-up. Anything the stack
touches destroys the paint, so the lowest surviving painted byte is the
true deepest point ever reached -- including paths through function
pointers and interrupt handlers that no static call-graph walk can
follow.

It carries the firmware's job state machine and `loop()` **verbatim**,
so the RAM footprint and the per-pass work match: 1746 bytes static
against the firmware's 1732, i.e. 14 bytes tighter, which can only make
a marginal overflow easier to reproduce.

    beat=N up=Nms  stack used=NNN  paint left=NNN

- `paint left` falling towards 0 means the stack is approaching the
  static variables; reaching 0 means it has been writing over them, and
  the once-a-second reset is explained.
- `stack used` well under the free RAM, with the board still resetting,
  clears the stack and points the search elsewhere.
- The board resetting *before* any beat prints would itself be
  informative: the crash would then be in the first half-second, which
  T02 survived.

Note the .ino gotcha met here, worth remembering: the IDE generates
prototypes for a sketch's functions and inserts them after the last
preprocessor directive, which lands *before* a `typedef` written in the
.ino. Any type used in a sketch function signature must therefore
arrive through an `#include` -- hence `JobState.h`.

### T04 — separating three suspects for the ~360 steps/s break-up

Jogging is smooth to about 360 steps/s, then the motor loses sync,
slows and vibrates. Note first what is *not* involved: Adjustment mode
goes UI -> step queue -> ISR and never touches `Motion`'s planner.

| Suspect | How T04 tests it |
|---|---|
| The ISR is not running at 4 kHz | Phase 1 counts real `SysTick` increments against `millis()` |
| The queue starves | Phase 2 keeps the queue **full** instead of refilling only when empty, and reports the rate actually achieved |
| The motor cannot *start* that fast | Phase 3 reaches the same targets through a ramp |

**Reading it.** If phase 2 is smooth where jogging was not, the
firmware's refill rule is at fault. If phase 3 reaches speeds phase 2
cannot, it is pull-in torque, and the fix is a ramp rather than a lower
ceiling. If phase 1 disagrees with 4000 Hz, nothing else matters until
that is settled.

The refill rule is a real suspect and its cost is predictable: the jog
code enqueues two steps only once the queue has run dry, so the motor
pauses while `loop()` notices. Modelled at ~0.4 ms of latency that is
6% low at 360 steps/s and 14% at 800 — and, more to the point, it makes
the motion lumpy (two quick steps, a pause, two more) rather than
merely slow.

On phase 1's scope: `SysTick` and `millis()` both derive from the same
crystal, so it validates the **divider arithmetic** — prescaler bits,
`OCR2A`, the SysTick divisor — which is exactly where a silent factor
of two would hide. It cannot validate the crystal itself.

### T04 — first run, 2026-09-11: two instrumentation bugs, one real answer

**The time base is exact.** Phase 1 printed "990Hz, MISMATCH", and that
verdict was wrong: the test multiplied the SysTick rate by
`SYSTICK_PERIOD_MS` (10) instead of by the ISR-to-SysTick **divider**
(`ISRDISPATCH_FREQ_HZ * SYSTICK_PERIOD_MS / 1000` = 40). The raw
measurement -- 200 SysTick ticks in 2001 ms, i.e. 100/s for a 10 ms
period -- was right all along and gives **3998 Hz against 4000
expected, 0.0% error**. Fixed.

Two further defects made the phase 2 and 3 *numbers* untrustworthy, and
both were mine:

- `StepOnce()` incremented the step counter **before** asking the
  queue, so every refusal dropped a step and jumped the motor's phase.
  The test was manufacturing the very stutter it was meant to diagnose.
  The counter now advances only on acceptance.
- The elapsed window was measured **after** draining the queue, which
  inflated it. Taken before the drain now.

The giveaway that something was wrong: reported rates came out *above*
the commanded value, which is impossible when the ISR is the metronome.
Numbers that break physics are a bug in the instrument, not a
discovery.

What still stands from that run is the **physical observation**, which
is what the test is for: where does the motor break up in phase 2
(queue kept full) versus phase 3 (reached through a ramp)? That
comparison decides between a refill problem, which is ours, and a
pull-in torque limit, which calls for a ramp in the jog code.

### T04 — second run: the queue is not the problem

With the queue kept full, the motor still breaks up **above about 400
steps/s** — barely better than the 360 seen while jogging. The refill
rule was worth fixing but it is not the cause.

Two improvements requested and applied:

- **Direction alternates** between sweeps, so the cable unwinds instead
  of winding fully onto the spool.
- **The ramp is dithered.** It was not the formula that was
  non-linear but the integer hold count: at 4 kHz the only rates
  available between 400 and 800 steps/s are 400, 444, 500, 571, 667 and
  800, so a ramp that is "linear in rate" lands on steps of 10 to 20%.
  The period is now carried in 1/256 of a tick and accumulated, each
  entry taking the whole part and keeping the remainder — Bresenham
  applied to time. Measured error against the requested rate: **0.04%
  to 0.41%** where the jumps had been 10-20%.

### T05 — is the wiring order what the firmware assumes?

The phase table is provably correct: `{0x0C, 0x06, 0x03, 0x09}` is
IN3+IN4, IN2+IN3, IN1+IN2, IN1+IN4, and consecutive pairs always share
exactly one coil and change exactly two — a uniform two-phase-on
rotating field. It runs opposite to the AB-BC-CD-DA convention, which
is precisely what `STEPPER_MOTOR_*_REVERSED` is for.

What reading cannot settle is whether `IN1`..`IN4` reach the motor's
coils **in that physical order**. Two crossed wires still produce
rotation — the field still turns, just unevenly — at reduced torque and
with a much lower break-down speed. That is the remaining candidate for
the ~400 steps/s ceiling.

Phase 2 of the test is the decisive one, because single-coil wave drive
has no ambiguity to hide behind: a steady creep means the order is
right, a stutter or no net motion means two wires are crossed.
