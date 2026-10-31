# Design Requirements — LCD / Joystick UI

Module: **LcdJoystickUI** — display driver, joystick reading, and menu
state machine realizing the User Interface functions.

Realizes: [`REQ-CoreLR-F-UserInterface-0001`..`-0008`](../functional/REQ-CoreLR-F-UserInterface.md)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Implemented (2026-09-07) — status screen, main menu, both jog
modes, pen calibration wizard, SD browser navigation. Remaining open
items are deliberate empirical-tuning deferrals and one genuine gap
found during implementation (see below), not undecided architecture
for what's listed above.
`-0011` (frame buffer, incremental transmission, rate-limited refresh)
was specified 2026-09-09 and **implemented 2026-09-10**, replacing the
unconditional two-line redraw on every `_Task()` call. Measured cost:
**+14 bytes of RAM**, not the +5 first estimated — that estimate had
overlooked the compose buffer still needed for `snprintf` output.

## Hardware

## REQ-CoreLR-D-LcdJoystickUI-0001 — Display and input hardware
<a id="REQ-CoreLR-D-LcdJoystickUI-0001"></a>
- Display: 2×16 character LCD on an I²C backpack (PCF8574), on the fixed
  hardware I²C pins (`A4`=SDA, `A5`=SCL), driven by our own `LcdI2c`
  (`REQ-CoreLR-D-LcdI2c-0001`) -- not by `LiquidCrystal_PCF8574`, which this
  requirement named until 2026-10-07.
- Input: a 2-axis analog joystick (potentiometers on `A1`=`ANA_X`,
  `A2`=`ANA_Y`) with an integrated push button (`A3`=`SW`) — **not** a
  rotary encoder ("knob" in `concept.md` refers to this joystick
  module). See `../../docs/hardware/PinMapping.md` for the full board pin
  allocation.

**Rationale:** corrects the initial "knob" wording in `concept.md`; also
matches an earlier local prototype found in `Walldraw/Walldraw.ino`
(pins `BUTTON_SW`, `BUTTON_ANA_X`, `BUTTON_ANA_Y`), which is a useful
reference for wiring even though its code content was discarded.

## Status screen

## REQ-CoreLR-D-LcdJoystickUI-0002 — Status screen content
<a id="REQ-CoreLR-D-LcdJoystickUI-0002"></a>
Adapted from an earlier working prototype (`Walldraw/Walldraw.ino`,
`where_lcd()`), extended with a status tag to satisfy
`REQ-CoreLR-F-UserInterface-0002`'s "current action" requirement within
2×16 characters:
- Line 1: one status-tag character in column 0, then the Cartesian
  position X and Y in millimetres, one decimal place, **in fixed
  columns** (`-0023`; e.g. `I  123.4  -123.4`).
  Tags: `R`=running, `I`=idle, `P`=paused, `E`=error, `U`=uncalibrated.
- Line 2: ` L 761.7 R 535.5` — the cable **lengths L and R in millimetres,
  one decimal place**, computed from the position by the inverse
  kinematics (see the `@todo` in `RedrawStatusScreen`), in the same
  columns as line 1 — not the raw step count. The labels `L` and `R` sit
  in columns 1 and 9 (*amended 2026-10-10: it was `L,R=<l>,<r>`*).

**Rationale (layout of 2026-09, superseded in its details by `-0023`):** a single-character prefix fits status and position on one
line without needing to cycle pages; the plotter's small work area keeps
`±999.9` mm well within the remaining 12 characters. Showing L,R in mm
(confirmed 2026-09-06) keeps both lines visually and unit-consistent for
the user, at the cost of one small conversion (steps → mm) on every
display refresh.

## Menu navigation model

## REQ-CoreLR-D-LcdJoystickUI-0003 — Joystick roles in menu navigation
<a id="REQ-CoreLR-D-LcdJoystickUI-0003"></a>
While inside the menu (not the status screen, not a jog mode):
- **Y axis**: moves up/down through the current list of menu items.
- **X axis**: adjusts the currently-edited value, or moves in/out of a
  submenu (file-browser style).
- **`SW` short press**: validate / enter selected item.
- **`SW` long press**: back / cancel (go up one menu level; from the
  top-level menu, returns to the status screen).

## Jog modes

## REQ-CoreLR-D-LcdJoystickUI-0004 — "Control" jog mode mapping
<a id="REQ-CoreLR-D-LcdJoystickUI-0004"></a>
`REQ-CoreLR-F-UserInterface-0004` is realized by mapping joystick
deflection to jog speed proportionally, independently on X and Y,
reusing the normal motion pipeline (`REQ-CoreLR-D-Motion`).

## REQ-CoreLR-D-LcdJoystickUI-0005 — "Adjustment" jog mode UX
<a id="REQ-CoreLR-D-LcdJoystickUI-0005"></a>
`REQ-CoreLR-F-UserInterface-0005` (select a motor, L or R, then jog it
directly) is realized as: the X axis toggles which motor is currently
selected (shown highlighted/bracketed on the display, e.g. `[L] R` vs.
`L [R]`); the Y axis then jogs the selected motor. Consistent with the
general X=select/Y=act convention of `REQ-CoreLR-D-LcdJoystickUI-0003`.

## REQ-CoreLR-D-LcdJoystickUI-0006 — Jog-mode gating implementation
<a id="REQ-CoreLR-D-LcdJoystickUI-0006"></a>
`REQ-CoreLR-F-UserInterface-0006` (access gating) is realized by
hiding/disabling the "Manual displacement" main-menu entry
(`REQ-CoreLR-F-UserInterface-0003`) whenever the machine is not
calibrated.

## Other workflows

## REQ-CoreLR-D-LcdJoystickUI-0007 — Pen calibration screen flow
<a id="REQ-CoreLR-D-LcdJoystickUI-0007"></a>
Realizes `REQ-CoreLR-F-UserInterface-0007` as a sequential 2-step
wizard:
1. `Set PEN UP` — Y axis adjusts the servo angle live (pen visibly
   moves), `SW` short press validates and advances to step 2.
2. `Set PEN DOWN` — same principle; `SW` short press validates **and**
   persists both setpoints (`REQ-CoreLR-F-Calibration-0002`) to
   non-volatile storage, then returns to the main menu.

```
Screen 1:               Screen 2:
Set PEN UP               Set PEN DOWN
Angle: 60 [Y=+/-]         Angle: 95 [Y=+/-]
```

## REQ-CoreLR-D-LcdJoystickUI-0008 — SD file browser: listing and directory navigation
<a id="REQ-CoreLR-D-LcdJoystickUI-0008"></a>
*Applies to: sd image only*

Realizes `REQ-CoreLR-F-UserInterface-0008`, including navigating into
subdirectories and back up to the parent:
- One entry shown at a time — line 1: entry name (truncated to 16
  characters if needed; directories shown with a trailing `/`); line 2:
  position in the current directory, e.g. `3/27`.
- Entries within a directory are sorted directories-first, then files,
  each group alphabetically.
- When not at the SD card root, a `..` entry is listed first, to go up
  one level.
- **Y axis**: previous/next entry in the current directory.
- **`SW` short press**: if the entry is a directory (including `..`),
  enter it (or go up, for `..`) and reset the list to its first entry;
  if it's a file, load it and start the job.
- **`SW` long press**: go up to the parent directory; from the SD card
  root, return to the main menu.

```
Line 1: dessin_chat.gc
Line 2: 3/27

Y (next) ->
Line 1: fleur_01.gcode
Line 2: 4/27
```

**Extension filtering (confirmed 2026-09-06):** the listing shows
directories plus only files with a recognized GCode extension
(`.gc`, `.gcode`, `.nc`) — other files are hidden from the browser
entirely (they don't count towards the `index/total` either).

## REQ-CoreLR-D-LcdJoystickUI-0009 — Joystick dead zone (confirmed 2026-09-06)
<a id="REQ-CoreLR-D-LcdJoystickUI-0009"></a>
On the 10-bit ADC reading (0–1023, centre 512), a dead zone of **±32**
around the centre value is the default: readings within
`[512-32, 512+32]` are treated as "centred / no input" on that axis.
Defined as a compile-time macro (see `docs/standards/STD-FileOrganization.md`, "Centralized
configuration headers"), not hard-coded inline, so it can be
retuned without hunting through the code.

## REQ-CoreLR-D-LcdJoystickUI-0010 — "Calibrate machine" numeric parameter entry (confirmed 2026-09-07)
<a id="REQ-CoreLR-D-LcdJoystickUI-0010"></a>
"Calibrate machine" (main menu) is a 3-step wizard, not just the
Adjustment jog step:
1. "Motor dist (mm)" — Y axis adjusts the live value by the current
   step size; X axis cycles the step size (1mm/10mm/100mm, edge-
   triggered — one toggle per deflection gesture, not repeated while
   held, same as `-0005`'s motor-select toggle). SW short press
   advances to step 2.
2. "Ref length (mm)" — same value-entry pattern. SW short press
   persists both values via `Calibration_SetParams()` (which
   recomputes `ReferencePositionX/Y`, `REQ-CoreLR-D-Calibration-0001`)
   and advances to step 3.
3. Adjustment jog mode (`-0005`, unchanged) — physically reach the
   reference position, SW short press confirms calibration.

SW long press at any of steps 1-2 cancels the whole wizard (discards
both values, nothing is persisted) back to the main menu; at step 3,
cancels back to the main menu without confirming (unchanged from
before `-0010`).

**Amended 2026-10-10 (maintainer's bench):** in steps 1-2 the stick **UP increases** the value and DOWN decreases it (it was the other way round); the value still stops at 0.
Left / right now choose the step size without wrapping (`-0022`).

**Layout, amended 2026-10-10 (`-0023`):** row 0 is the label; row 1 shows the value with its decimal point in column 6, as the status screen's X, then
`+-<step>` from column 9 — `  1234.5 +-100mm`, `   700.0 +-1mm`. The value is held between 0 and `LCDJOYSTICKUI_CAL_MAX_UM` (`SwConfig.h`: 4 m in the
48-bit arithmetic, 9999.9 mm in the 64-bit).

**Rationale:** reuses the exact value-adjust-with-live-preview shape
already established by the pen calibration wizard (`-0007`) rather than
introducing a new UI pattern. A digit-by-digit editor (like a digital
watch) was considered and rejected: more precise, but a wholly new
widget class (cursor rendering, per-digit state) for no real precision
need — a tape measure reads to the mm, not finer, and this is a rare,
one-time setup action, not a frequent operation.

## REQ-CoreLR-D-LcdJoystickUI-0011 — Frame buffer with incremental, rate-limited transmission (confirmed 2026-09-09)
<a id="REQ-CoreLR-D-LcdJoystickUI-0011"></a>
Realizes `REQ-CoreLR-F-UserInterface-0009`. The module shall not format
and transmit the display inline. Instead:

**1. Render into a RAM frame buffer.** A 2×16-character frame buffer
holds what the display *should* show. Rendering writes only into RAM —
no I²C traffic. It replaces the current per-screen formatted line
buffers rather than adding to them (`s_screenLine1`/`s_screenLine2`),
so the RAM cost of this whole mechanism is **+5 bytes net** (measured
2026-09-09): 32 bytes of frame buffer against the 34 it replaces, plus
4 bytes of dirty bits and ~3 bytes of transmission state.

**2. Mark changed characters dirty.** One dirty bit per character
position (4 bytes for 32 positions), set by rendering when a character
actually differs from what the buffer already held. Without dirty
tracking, all 32 characters would have to be re-sent continuously even
at a standstill, occupying the I²C bus permanently for nothing.

**3. Transmit incrementally from `LcdJoystickUI_Task()`** — a small
number of dirty characters per call, not the whole frame. Measured
2026-09-09: one character costs ~0.36 ms of blocking I²C, against
~11.5 ms for a full 32-character redraw. This is what keeps a refresh
from starving the step queue (`REQ-CoreLR-F-Actuation-0001`).

**4. Track the physical cursor position** and emit a positioning
command only when the next dirty character is *not* already under the
cursor. The LCD controller auto-increments its own address counter, so a
run of consecutive characters needs no repositioning. This matters: a
positioning command costs about as much as a character (~4 I²C bytes),
so naively positioning before every character would double the cost and
cancel much of the benefit. Changes are naturally contiguous in practice
(the digits of a position value), which this exploits.

**5. Gate re-rendering on three conditions, all required:**
```
   SysTick period elapsed  (>= LCDJOYSTICKUI_REFRESH_MS, rate limit)
     AND previous frame fully transmitted
     AND rendered content differs from the buffer  (change-driven)
```
The middle condition is what makes tearing structurally impossible: a
frame is never re-rendered while it is still partly on the wire, so the
display can never show a mix of two frames (e.g. X moving 199→200
flashing "299"). It also makes the refresh rate self-limiting — it
cannot outrun the I²C bus, whatever `LCDJOYSTICKUI_REFRESH_MS` is set
to.

**Time base:** `SysTick` (`REQ-CoreLR-D-SysTick-0001..0004`), read as an
8-bit counter with no critical section, using the skip-missed semantics
of `-0004`. `millis()` shall no longer be used by this module: the SW
short/long-press detection and the input repeat gate migrate to the same
SysTick base (`LCDJOYSTICKUI_LONGPRESS_MS` = 800 ms and
`_REPEAT_MS` = 200 ms are both exact multiples of the 10 ms tick). This
also removes three `uint32_t` timestamps in favour of three `uint8_t`
ones — **9 bytes of RAM recovered**, and the 32-bit arithmetic on the
busiest loop path along with them.

**Refresh interval:** `LCDJOYSTICKUI_REFRESH_MS = 50` (5 SysTick ticks),
settled 2026-09-09 and already in `SwConfig.h`. 20 Hz is imperceptibly
"live" to a human while costing a quarter of what 20 ms would; 100 ms
would likely also be fine and remains a free tuning step if hardware
bring-up shows the queue margin is tighter than expected.

### Empty-browser screen states its own exit (2026-09-10)

When the SD browser has no entries to list, row 1 shows
`Hold SW = back`. It was blank, which made the screen a dead end in
practice: with nothing selectable, the only way out is the long SW
press, and nothing on screen said so. Found on real hardware with no
card inserted — the exit worked, it was simply undiscoverable. Other
screens already state their control (`SW=confirm pos.` in Adjustment
mode), so this only brings the browser in line.

## REQ-CoreLR-D-LcdJoystickUI-0012 — Job control belongs to the state machine, not the UI (implemented 2026-09-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0012"></a>

The UI shall **report** what the user asked for and be **told** what to
display; it shall not decide or hold job state.

Previously `s_machineState` lived in this module with no getter, and
the SD browser started jobs itself (`SDReader_OpenJob()` followed by
`LcdJoystickUI_SetMachineState(RUNNING)`). That is why nothing could be
gated: the one component that knew a job was running was the display.

- **Reporting**: `LcdJoystickUI_TakeEvent()` returns and clears a
  one-shot request (`JOB_START`, `JOB_HALT`, `JOB_RESUME`,
  `JOB_CANCEL`, `ERROR_ACK`). Polled rather than a callback: it mirrors
  `Stepper_TakePendingPenAction()`, an already-reviewed pattern here,
  costs one byte, and keeps function pointers -- which cost RAM on AVR
  -- out of the module. No critical section: main-loop context only, no
  ISR touches it.
- The file name never crosses the interface. The browser still calls
  `SDReader_OpenJob()` (it alone has the name and the open directory)
  and raises `JOB_START` **only if the open succeeded**. This also
  fixes a real gap: the return value was discarded, so a failed open --
  card pulled between listing and selecting -- silently looked like a
  started job.
- **Error acknowledgment** (`REQ-CoreLR-D-GCodeParser-0006`): while in
  the error state a short press on the status screen raises
  `ERROR_ACK` and does **not** open the menu. The user has to clear the
  error before anything else.

### Menu availability replaces the jog/job interleaving guard

A `MenuItemAvailable()` predicate decides what the menu offers per
state:

| State | Items offered |
|---|---|
| Idle, calibrated | Jog, Calibrate mach., Calibrate pen, Load file |
| Idle, uncalibrated | as above minus Jog (`REQ-CoreLR-D-LcdJoystickUI-0006`) |
| Running | Pause job, Cancel job |
| Paused | Resume job, Cancel job |
| Error | none — the menu is not reachable; a short press acknowledges |

This closes the open item "entering a jog mode, the pen wizard, or the
SD browser while a job is `RUNNING` is not gated" **by construction**
rather than by a guard at each entry point: those items simply are not
offered. `REQ-CoreLR-F-UserInterface-0002`'s "the menu stays reachable"
still holds — it holds different items. The uncalibrated rule folds in
as one more case instead of remaining a special case inside the
selection loop.

The "Item n/m" counter counts only the items offered in the current
state; the raw index over `MENU_ITEM_COUNT` would read "Item 5/7" for
the first of two job-control entries.

## REQ-CoreLR-D-LcdJoystickUI-0013 — Compose lines without printf (implemented 2026-09-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0013"></a>

Screen lines shall be composed with bounded append helpers, not
`snprintf_P`.

`snprintf_P` pulled in avr-libc's `vfprintf` and its integer
conversion support -- **1372 bytes of flash, measured** (`vfprintf`
948, `__ultoa_invert` 188, `snprintf_P` 96, `fputc` 96, `strnlen`
2x22) -- to do nothing but splice a few small integers and short
strings together. Seven call sites, all of the form "a label, a number,
a separator, another number".

That became decisive when the real build target turned out to be an
Arduino **Nano**, whose bootloader leaves 30720 bytes of flash against
the Uno's 32256: the image was 31584 bytes, i.e. 102%. Removing printf
recovered 1090 bytes net of the replacement helpers and brought it to
30494 (99%).

The replacement lives in **its own module, `TextFmt`**, not inside
this one -- a general-purpose utility does not belong bolted onto a UI
module, and the maintainer expects more such libraries (convention:
a general-purpose utility gets its own `.h`/`.c` pair). Four functions, each taking the
current write position and returning the new one, each keeping the
destination NUL-terminated and never writing past the capacity:
`TextFmt_Char`, `TextFmt_Str`, `TextFmt_StrP`, `TextFmt_Uint`.

`TextFmt.h` states outright that callers point at our implementation
and not avr-libc's, and carries the measured 1372 bytes, so that the
next reader does not "simplify" it back to `snprintf` -- one call site
is enough to bring the whole cost back. Verified by compiling the real
`TextFmt.c` on the host: every `TextFmt_Uint` boundary from 0 to 65535,
the chaining contract (returned position equals `strlen`), and the
bounds -- truncation, a call past a full buffer being harmless, and a
capacity of 1 yielding nothing but a terminator.

### A display defect fixed on the way

`FormatMm()` derived the sign from the integer part. Integer division
truncates towards zero, so -500um gave a whole part of 0 and printed
"0.5": **the minus sign vanished for every value between -1mm and 0**.
The sign is now emitted separately. Verified on the host against
`snprintf` for agreement on positives, and against the corrected
expectations on negatives (-500um -> "-0.5", -1um -> "-0.0"), plus a
truncation case proving the bound holds and the string stays
terminated.

## REQ-CoreLR-D-LcdJoystickUI-0014 — "Adjustment" jog speed follows the stick (implemented 2026-09-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0014"></a>

The per-motor jog rate shall vary with the Y deflection, so that one
stick does both coarse travel and fine positioning.

### The defect

Jogging issued one step per `RepeatGate()` interval, i.e. one step
every `LCDJOYSTICKUI_REPEAT_MS` = 200 ms: **5 steps/s**. At 2048 steps (2037.9 since the correction of 2026-10-26)
per revolution that is seven minutes per turn, and reaching the
reference position was unusable in practice (maintainer, on
hardware, 2026-09-11).

The repeat gate is a **UI** rate-limit -- it exists to scroll a list
one item at a time -- and its 200 ms granularity cannot express a step
rate at all. The rate belongs in the queue entry's hold count, which is
what the step queue is for. It is no longer used on this axis.

### The curve

Squared rather than linear, so a large stick movement near the dead
zone changes the rate very little:

```
   n    = (|deflection| - dead zone) / (full scale - dead zone)
   rate = MIN + (MAX - MIN) * n^2        steps per second
   hold = ISR frequency / rate           ticks per queue entry
```

Integer throughout; the widest intermediate is about 1.6e8, inside
uint32. Measured on the host across the full travel, monotonic:

| Deflection | steps/s | cable |
|---|---|---|
| 32 (dead-zone edge) | 10 | 0.5 mm/s |
| 100 | 28 | 1.5 mm/s |
| 200 | 121 | 6.5 mm/s |
| 300 | 308 | 16.5 mm/s |
| 450 and beyond | 800 | 43 mm/s |

Twice as fine as before at the slow end, and **160x faster** at full
deflection. `MAX` stays under the ~1 kHz per-motor ceiling with the
same margin plotting uses, enforced by `#error`.

### Overshoot is bounded by the refill rule

Steps are queued only once the queue has run dry, in bursts of
`LCDJOYSTICKUI_ADJUST_BURST` (2). Releasing the stick therefore
overshoots by at most two steps whatever the speed -- a deeper queue
would keep the motor running for a noticeable fraction of a second
after the stick is centred at the slow end, where one entry alone lasts
100 ms.

### The rate is shown

Row 0 carries the live rate next to the motor selector
(`[L] R  800/s`, `--` when centred). A speed that follows the stick is
hard to judge blind, and showing it also makes the constants tunable by
feel.

## REQ-CoreLR-D-LcdJoystickUI-0015 — The jog ramps, and its rate is dithered (implemented 2026-09-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0015"></a>

The "Adjustment" jog shall accelerate towards the rate the stick asks
for rather than commanding it outright, and shall dither the queue
entry's hold count so the average matches.

### Why a ramp: pull-in, not slew

A 28BYJ-48's **pull-in** rate -- the fastest it can be commanded from
standstill without stalling -- is far below the rate it will hold once
turning. Measured on this machine (`tests/T04_StepRate`, queue kept
full, no ramp) it breaks up above **~400 steps/s**, barely better than
the 360 seen while jogging. The Walldraw reference project puts its own
figure lower still, its source commenting that "above 256 it easily
loses steps", and it never commands a rate outright -- it accelerates
at 200 steps/s².

Three other candidates were eliminated first, which is what made the
diagnosis trustworthy:

| Candidate | Verdict |
|---|---|
| The ISR is not at 4 kHz | **3998 Hz measured** against 4000 -- exact |
| The queue starves between refills | Ruled out: with the queue kept full the limit moved only from 360 to 400 |
| The phase table is wrong | Proved equivalent to AccelStepper's -- see below |

### The phase table is correct, and here is why it looks different

AccelStepper's `step4` reads `0x5, 0x6, 0xA, 0x9`, which is not our
`0x0C, 0x06, 0x03, 0x09`. They are the same sequence expressed in
different pin conventions. The reference sketch constructs its steppers
as `AccelStepper(HALFSTEP, pin1, pin3, pin2, pin4)` -- the classic
28BYJ-48 swap -- so the library's bits 1 and 2 reach the motor's IN3
and IN2. Applying that permutation to `5,6,A,9` yields `3,6,C,9`, which
is our cycle traversed the other way; `STEPPER_MOTOR_*_REVERSED` exists
for exactly that.

**Applying `5,6,A,9` to the connector order would be a real fault.**
IN1/IN3 are the two halves of one bifilar winding and IN2/IN4 the
other, so `0x5` (IN1+IN3) and `0xA` (IN2+IN4) drive one winding against
itself: **two phases in four at zero torque**, half the average torque
and a much earlier break-down. The library puts the permutation in the
constructor call; this project puts it in the table, which keeps
`HwConfig.h` honest -- `PIN0` really is `IN1`.

### The ramp

The commanded rate moves towards the target once per SysTick, at
`LCDJOYSTICKUI_ADJUST_ACCEL_STEPS_PER_S2`. Starting from rest it begins
at `..._START_STEPS_PER_S`, inside the pull-in region -- **unless the
target is already slower**, which is what keeps fine positioning
immediate. Deceleration is applied at once: slowing never loses sync,
and a laggy return would be a poor trade.

Simulated from the shipped constants: a target of 10 or 120 steps/s is
reached immediately, 400 steps/s in 140 ms, 800 steps/s in 340 ms.

### The dither

An integer hold count cannot express a smooth ramp at the top of the
range: at 4 kHz the only rates available between 400 and 800 steps/s
are 400, 444, 500, 571, 667 and 800, so a ramp lands on jumps of 10 to
20% -- audible, and reported from the bench as "your ramp is not very
linear". The period is now carried in 1/256 of a tick and accumulated,
each entry taking the whole part and keeping the remainder. Bresenham
applied to time rather than distance. Measured error against the
requested rate: **0.04% to 0.41%**.

The screen shows the *current* rate, mid-ramp included, rather than the
target: what the motor is doing is the useful thing to see.

## REQ-CoreLR-D-LcdJoystickUI-0016 — The adjustment jog keeps the step queue topped up (implemented 2026-09-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0016"></a>

The "Adjustment" jog shall refill the step queue up to a fixed depth
(`LCDJOYSTICKUI_ADJUST_QUEUE_DEPTH`, 8 of the 16 ring slots) on every
`LcdJoystickUI_Task()` pass, rather than only once the queue has run
dry.

### Why the previous rule failed

`REQ-CoreLR-D-LcdJoystickUI-0014` refilled a burst of 2 entries only
when `Stepper_IsEmpty()` was true. That kept the release overshoot
small, which was the stated goal, but it had a consequence that was
never written down: **the ring is empty for nearly the whole of every
step period.**

`Stepper_IsEmpty()` reports on the ring only — deliberately, see its
comment — and not on the entry the ISR is currently executing. So the
sequence in steady state was:

```
      ISR pops the last queued entry   ->  ring empty, that entry now executing
      loop() sees IsEmpty(), refills 2 ->  ring has 2 again
```

Between those two lines the only thing standing between the motor and a
frozen coil pattern was the *reliquat of the executing entry*: one step
period. At 400 steps/s that is 2.5 ms. One LCD character over I²C costs
about 1 ms (four PCF8574 transactions), plus two `analogRead()` at
104 µs each, plus the rest of the pass. The margin was under 2×, and
every so often a pass overran it.

When it did, `Stepper_Tick()` kept re-applying the same
`s_currentEntry.MotorCoilBits` on every tick. The motor stayed
energised on one phase with the rotor short of its detent, and rang
against it.

### Why it was misdiagnosed

That failure is **indistinguishable by ear and by feel from a motor
driven past its pull-in rate** — buzzing, no advance, apparent loss of
torque above a threshold. It was attributed to the motor for several
bench sessions, and `LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S` was lowered
twice (800 → 400 → 200) to work around it.

Three things together settled it:

- The maintainer's own `tests/stepper_speedControl` sketch drove the
  same motor, on the same wiring, at up to 333 steps/s — smoothly and
  with noticeably more torque. Its `Stepper::step()` busy-waits on
  `micros()`, so it can never starve. The motor was exonerated.
- The LEDs on the ULN2003 board stayed on a **fixed pattern** while the
  motor buzzed. At 200 steps/s each coil should be at 50 % duty at
  50 Hz — all four dim, none static. A static pattern can only mean the
  bits stopped changing.
- The phase table was ruled out independently: the reference sketch's
  `Stepper(200, 6, 8, 7, 9)` — middle pins crossed, the bifilar
  convention — yields `6, 3, 9, C` on `IN1..IN4`, which is our
  `kPhaseTable = { C, 6, 3, 9 }` rotated by one. Same cycle, same
  direction.

### Measurement, not inference

`Stepper_StarvedCount()` counts the ticks where an entry's hold expired
with nothing queued behind it, and the adjustment screen displays it
(`SW=ok   miss:N`). One count per motion end is normal — the last step
has no successor. Starvation shows as tens or hundreds accumulating
*while the axis is still moving*.

This counter is the point of the requirement as much as the deeper
queue is: without it, the next occurrence would be blamed on the motor
again.

### Depth is a duration, not a count (revised 2026-09-11)

The first implementation used a fixed depth of 8 entries. That is
incoherent across this mode's 80:1 speed range — 8 entries are 0.8 s of
buffer at 10 steps/s and 13 ms at 600 — so the depth is now derived
from the current rate to hold a constant
`LCDJOYSTICKUI_ADJUST_BUFFER_MS` (30 ms), on the maintainer's
proposal.

It costs one multiply and no bookkeeping: every entry this mode emits
carries the same hold, so a duration converts straight to a count. The
release overshoot becomes a constant ~30 ms at every speed instead of
varying in the same 80:1 ratio.

| rate | entries | buffer | overshoot |
|---|---|---|---|
| 10 /s | 2 (floor) | 200 ms | 0.11 mm |
| 100 /s | 3 | 30 ms | 0.16 mm |
| 400 /s | 12 | 30 ms | 0.64 mm |
| 600 /s | 15 (ring) | 25 ms | 0.81 mm |

The floor of 2 guarantees a successor behind the entry being executed at
any rate, which is the whole point. The ceiling is what the ring holds,
one slot always staying free to tell full from empty — so above
~500 steps/s the 30 ms target is clamped rather than met. Raising
`STEPPER_QUEUE_SIZE` would cost 3 bytes of RAM per slot.

A rejected alternative, also from the maintainer: cap every entry at
2 ms and repeat the same coil pattern, so that an entry *count* becomes
a duration by construction. It cannot work here, for two independent
reasons. `EnqueueRawEntry`'s adjacent-entry merge
(`REQ-CoreLR-D-Stepper-0009`) absorbs any entry whose coil bits match
the tail, up to 16383 ticks — repeated identical patterns would simply
collapse back into one long entry. And at 10 steps/s a single step
lasts 100 ms, i.e. 50 chunks of 2 ms, against a 16-slot ring.

### Counter semantics (revised 2026-09-11)

`Stepper_ResetStarvedCount()` is called whenever the stick is centred,
so the displayed figure describes the movement about to start. The raw
running total proved unreadable on the bench: it necessarily counts once
at the end of every motion — the last step has no successor by
definition — so it accumulated across attempts and buried the signal it
was added to expose.

A latent defect was fixed in the same change: the step counters were
advanced *before* the queue was asked, so any refusal skipped a phase.
They are now advanced in a copy and committed only on success. This is
the same defect that had previously made a bench test manufacture the
very stutter it was written to diagnose.

## REQ-CoreLR-D-LcdJoystickUI-0017 — "Control" jog feeds the planner (implemented 2026-09-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0017"></a>

The Cartesian jog shall queue a move whenever `Motion_PlannerHasRoom()`,
accumulating `LCDJOYSTICKUI_JOG_SLICE_MS` of deflection time per block,
clamped to `LCDJOYSTICKUI_JOG_SLICE_MAX_MS`.

### Two defects in the previous gate

It queued a move only `if (Motion_IsIdle())`. Neither consequence was
intended, and neither was written down.

**The planner never held more than one block.** The junction look-ahead
added in `REQ-CoreLR-D-Motion-0013` was therefore structurally inert
during jog, and every block decelerated to `VMIN` before the next was
accepted. On the bench the pulleys visibly stopped and restarted
throughout a continuous stick deflection.

**The block length ran away.** `s_lastJogTick` advanced only inside the
idle branch, so elapsed time was measured from the previous *idle
check* — i.e. each block was commanded as long as the previous one had
taken to execute:

```
      L(n+1) = v * T(L(n))  where  T(L) = 2*t_ramp + (L - 2*d_ramp)/v
             = L(n) + 2*(v*t_ramp - d_ramp)
             = L(n) + 2.1 mm        at v = 20 mm/s, a = 192 mm/s²
```

Unbounded, clamped only by `SYSTICK_TICKS_MAX` at 1.27 s — 25 mm of
committed motion. That is why the head kept moving for about half a
second after the stick was released.

### The slice is a compromise with the ramp

30 ms is not a free parameter. At 20 mm/s a block is 0.6 mm, so
`MOTION_PLANNER_DEPTH` of them is 2.4 mm of horizon against the 2.08 mm
a full accelerate-and-stop needs — just enough to reach `VMAX`.

- Halving it starves the look-ahead, and the jog never reaches `VMAX`.
- Doubling it puts 4.8 mm of committed motion behind a released stick.

Below one slice the handler returns and lets time accumulate, rather
than committing blocks too short to plan across. Above
`LCDJOYSTICKUI_JOG_SLICE_MAX_MS` the surplus is discarded, so time spent
with a full planner cannot be banked and spent as one long block — the
runaway above in a different guise.

## REQ-CoreLR-D-LcdJoystickUI-0018 — The frame buffer is actually used (implemented 2026-10-07)
<a id="REQ-CoreLR-D-LcdJoystickUI-0018"></a>
Found while replacing the display driver (`REQ-CoreLR-D-LcdI2c-0001`), and a defect against `-0011`, not a decision.

**The defect.** `-0011` says rendering writes only into the RAM frame buffer and marks dirty each character that
changed, and the module's own comments said the same (and `SwConfig.h`: "-0011's frame buffer is implemented"). It was
not wired. `PrintPadded()` still did `setCursor()` and `print()` of the whole 16-character row **directly on the bus**;
nothing but the initial `memset` ever set a dirty bit, so `FlushStep()` sent the 32 blanks once and then had nothing to
do. Every refresh therefore cost two cursor moves and 32 characters of blocking I²C — about 15 ms at 100 kHz
(0.47 ms a transaction, 34 of them) — **every 50 ms, whether or not anything had changed**: roughly 30 % of the main
loop, in exactly the form `F-UserInterface-0009` forbids ("change-driven", "incremental"). The history was purged
before the cause could be traced; the code at the purge point is what it was.

**The fix.** `PrintPadded()` renders into `s_frame`, clipped and space-padded, and sets the dirty bit of each cell that
**differs** from what the buffer held. An identical re-render sets nothing and costs the bus nothing — which is also
the change detector `-0011` asked for. `PrintPadded_P()` keeps its own small buffer for the flash text; the one in
`PrintPadded()` is gone (17 bytes).

**A latent defect that the fix exposed**, because it had never been exercised: `FlushStep()` assumed the cursor, after a
character, was at the next *cell*. The controller's address counter runs on past the last visible column into memory that
is not displayed (`0x10`), **not** to the first cell of row 1; a change spanning cells 15 and 16 would have written the
second one out of sight. After the last column of a row the cursor is now *unknown*, and the next cell is positioned.

**Failure handling in `FlushStep()`** (with `REQ-CoreLR-D-LcdI2c-0002`): a call that fails leaves the cell dirty, marks
the cursor unknown, and returns, to be offered again on the next call.

**Verified** by `tests/host/test_ui.cpp` (see `REQ-CoreLR-D-LcdI2c.md`, Verification): **0 transactions in 300
calls with nothing changing**; ≤ 2 transactions and 0.94 ms in any one call; the glass equals the frame on every screen;
the row-boundary change; padding and clipping.

**Cost, measured:** the change itself is −26 bytes of flash (`PrintPadded` 128 → 102) and −17 of RAM; the flash and RAM of
the driver replacement are in `REQ-CoreLR-D-LcdI2c-0001`.

**The bench figures need re-measuring.** `-0011`'s "~0.36 ms a character, ~11.5 ms for a full redraw" were taken
before and are not reproduced by the protocol arithmetic (47 clocks = 0.47 ms at 100 kHz; 32 characters and two cursor
moves, 16 ms). Whichever is right, the design holds; the figures should be read on a scope with the new driver.

## REQ-CoreLR-D-LcdJoystickUI-0019 — In the serial variant there is no SD browser, and the UI says when it can move the machine (implemented 2026-10-09)
<a id="REQ-CoreLR-D-LcdJoystickUI-0019"></a>
Request FW-08 of `docs/protocol/SerialProtocol.md`. `LCDJOYSTICKUI_HAS_SD` (= `CORELR_JOB_SOURCE` is the SD one) removes from `LcdJoystickUI.cpp` the "Load file" menu item and its label, the browser's state and its three
functions, and `#include "SDReader.h"`: the serial menu has six entries, the SD one seven (`test_ui.cpp`, both variants; the serial UI is 272 bytes smaller). `LcdJoystickUI_IsBusy()` is 1 while a jog or a calibration
wizard is on the screen (every mode but the status screen and the menu): the serial link then refuses G-code with `error:9` and reports `Jog` (`REQ-CoreLR-D-SerialLink-0007`). The menu already offers only
pause/cancel or resume/cancel while a job is active (`MenuItemAvailable()`), so a jog cannot be entered during a job.

## REQ-CoreLR-D-LcdJoystickUI-0020 — Joystick polarity is a hardware setting (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0020"></a>
Origin: the maintainer's first bench test of the Control jog — **inverted in X and in Y**. What the analog value does when the stick moves is a fact about the module
(how it is turned, how it is wired), not about the firmware, so it is stated where the hardware is: `LCDJOYSTICKUI_X_INVERT` and `LCDJOYSTICKUI_Y_INVERT` in `HwConfig.h`
(`0` or `1`, compile-time, one per axis; set to `1`/`1`). `LcdJoystickUI_ReadAxis()` negates the deflection of an inverted axis, so its value is **positive when the stick is pushed right (X) or
down (Y), whatever the unit** — which is what every user of it already assumed: the Control jog (`+X` right, `Y` down the wall, `REQ-CoreLR-D-Motion-0025`), the menus (down = next item),
the Adjustment jog (down = longer cable), the calibration wizards (Y down = increase, as the code has always read it). One place, so the menu and the jogs cannot disagree.

**Because it acts everywhere the stick is read**, the menu navigation follows too: if the menu already felt right before and only the jog was inverted, this would invert the menu; the
two cannot be told apart in the code, only on the bench (the menu and the jog read the same function). Cost: a compare and a negate, a few bytes.

**Verified** (`tests/host/test_ui.cpp`, section 4a2, **all four combinations of the flags, in both variants**): with the stick pushed right / left / down / up *as the unit reads it* (the raw
deflection's sign depends on the flag), the Control jog's target moves right / left / down / up the wall, and `ReadAxis()` is positive / negative / positive / negative. Five of the six broken copies
in `tests/mutation/mutants/joystick.txt` are caught (polarity never applied, applied always, the X flag read for Y, the Y flag for X, the flags swapped; each in the combinations where it matters, the file says which).

## REQ-CoreLR-D-LcdJoystickUI-0021 — The Control jog's response: quadratic in the norm of the stick, dead zone subtracted, the shape computed, a table kept as an option (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0021"></a>
Origin: the maintainer's bench test: the previous response was "not smooth enough at the centre". Decided with him: 40 mm/s at full stick, **corrected to 21 mm/s** the same day (see the correction below); **quadratic**; the speed is the **norm of the stick's vector**, not
a speed per axis; the dead zone is **subtracted** (so the software full scale is the hardware full scale minus the dead zone); the **hardware full scale is a setting**
(`LCDJOYSTICKUI_STICK_FULL_SCALE`, `HwConfig.h`, start 500), shared with the Adjustment jog, whose own 480 is gone.

**The curve.** With `m` the norm of the deflection `(dx, dy)` (`LcdJoystickUI_ReadAxis()`, positive right/down, `-0020`): `r = m − dead zone` (0 at the edge of the zone: no jump), `n = r / (full scale − dead zone)`
clamped to 1, `speed = Vmax · n²`, in the direction of the stick. With the defaults (32, 500, 40 mm/s): deflection 82 → 0.46 mm/s; 132 → 1.8; 182 → 4.1; 266 → 10; 383 → 22.5; 500 → 40. The previous curve was linear
without subtracting the zone and truncated to whole mm/s (1, 2, 3… steps of 1 mm/s, 1 mm/s at the edge of the zone, 19 at the stop).

**Implementation** (`JogCurve.c`, pure arithmetic: no float, no 64-bit): the norm by `Motion_Hypot` (already linked); `n` in Q12 by a 16 × 16 → 32 multiplication; the shape by **`n²` computed** (`(n·n) >> 8`); the option `JOGCURVE_USE_TABLE=1` replaces it with a linear interpolation in a 33-entry table
(`JogCurveTable.h`, `tools/generators/gen_jogcurve.py -e 2`; 66 bytes of flash); `displacement = Vmax · f · elapsed` in micrometres × 2^16; the components by one multiplication and one division by the norm
(`(scaled · dx) / m`, exact: a unit vector held in Q8 was tried first and put the magnitude up to 0.55 % off); and the part of a block below a micrometre is **carried into the next** (a slow stick moves a block of 0.3 µm: truncating
each block would lose it all). Widest intermediates 1.3·10⁹ (< 2³¹), checked at compile time against `LCDJOYSTICKUI_JOG_MAX_SPEED · LCDJOYSTICKUI_JOG_SLICE_MAX_MS`.

**Table or computation — measured** (the maintainer asked whether a table beats squares, roots and trigonometry). There is **no trigonometry** and the root is `Motion_Hypot`, already in the image; the only thing a table can replace is
`n²`. Measured, same inputs: the **cycles are the same** (2 465 against 2 476 mean per call, 0.15 ms per 30 ms block, 0.5 % of the CPU: `Motion_Hypot` and the two divisions are most of it), and the table costs **+112 bytes of flash**
(66 of table, 46 of interpolation; the SD build is 30 314 with it, 30 202 without). What the table buys is the **shape as data**: another exponent (1.5, a gentler top end) is a one-line change of the generator, where
`n^1.5` computed would need a square root. The table was built by default at first, since the shape was the thing to be tuned on the bench. **Decided 2026-10-10 (maintainer): the computed `n²` is the default**, in both variants (−124 bytes of flash on the SD build, where every byte counts, and the shape is quadratic anyway);
`JOGCURVE_USE_TABLE=1` and the generator stay for the day another exponent is wanted (both are tested). The interpolation's error, from linear segments on a convex curve, is at most 0.0002 of the full speed (0.01 mm/s).

**Cost, measured** (Nano fqbn): SD **+432 bytes** of flash (29 882 → 30 314; **406 free**), serial the same order (25 686 → ~26 118, 4 600 free); RAM +4; one more call in the jog (`LcdJoystickUI_Task` +360 bytes, the compiler inlined it).

**Verified**: `tests/host/test_jogcurve.c` (the real module against the formula in floating point: dead zone, continuity, monotonic, within 2 µm of the quadratic along an axis, 1.35 µm measured; the speed is the norm over 2 760
positions of the disc, within 0.3 % and 0.4 degrees; symmetric; proportional to the time; the remainder carried over 2 000 blocks for both signs; no overflow at the extremes), in both shapes; `tests/mutation/mutants/jogcurve.txt`, 19 broken
copies, all caught (each variant's own mutants survive only the other variant's run); `test_ui` for the wiring, for every combination of the polarity flags.

**Correction, 2026-10-10 (mine).** The 40 mm/s was agreed on my statement that it is Motion's ceiling. It is not: `MOTION_VMAX_V16` is 86/16 µm per tick = **21.5 mm/s**, the 400 steps/s the motors hold under
load (`docs/bringup/Bringup-Status.md`: 600 unloaded, 400 loaded, "everything is tuned against 400"). The planner clips every block to it, so with `LCDJOYSTICKUI_JOG_MAX_SPEED = 40` the top of the stick (from n = 0.73 up, the squared
curve crosses 21.5 mm/s there) is saturated, and the speeds in the table above are 21.5 at most. **Resolved the same day: the setting is now 21 mm/s** (maintainer's decision), under the cap and the measured limit; the squared curve then gives, at 10 / 20 / 30 / 50 / 75 / 100 % of the range beyond the dead zone,
0.22 / 0.86 / 1.9 / 5.4 / 12.1 / 21 mm/s. Next, if it is still too lively at the centre: the exponent 2.5 (`tools/generators/gen_jogcurve.py -e 2.5`, 0.07 / 0.38 / 1.1 / 3.8 / 10.5 / 21 mm/s), decided in that order by the maintainer.

**Not decided here / for the bench**: the shape (exponent 2, or 1.5); the dead zone (32, 6 % of the stick); whether 500 is where the stick really stops; and the **stop distance** at the top of the range — 0.21 s and 4.2 mm from 40 mm/s by the
acceleration alone, plus the up-to-4 blocks of 30–60 ms the jog keeps in the planner (an arithmetic consequence of the code, not a measurement).

## REQ-CoreLR-D-LcdJoystickUI-0022 — The stick as a choice: a threshold with hysteresis, and ends that are ends (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0022"></a>
Origin: the maintainer's bench test: selecting the motor L / R with the stick was "far too sensitive in the all-or-nothing mode" (the dead zone alone, 6 % of the travel, made a choice), and a selection that reached its end
**wrapped round** to the other end. Decided with him: a **threshold parameter** for the three-valued reading (−1, 0, +1), and **no wrap-around**, horizontally (the two motors, the step sizes) and vertically (the menu, the file list).

**The reading.** `LcdJoystickUI_ReadAxisTernary(pin)` returns −1, 0 or +1 (same convention as `ReadAxis`: +1 right / down). The stick must reach `LCDJOYSTICKUI_TERNARY_ON` (`SwConfig.h`, counts of deflection, default
**250**, about half the travel) to count, and it stays counted until it is back below `LCDJOYSTICKUI_TERNARY_OFF` (**170**): a **hysteresis**, so that a stick resting near the threshold, or a noisy ADC reading, cannot toggle a
choice twice. Between OFF and ON on the same side it holds its previous value; on the other side it is 0. Compile-time check: `DEADZONE < OFF <= ON`. One state per axis (2 bytes of RAM). It is used wherever the stick makes a
*choice*: the main menu, the SD file list, the motor select, the step size and the value ± of the calibration wizard, the pen wizard's angle. The **analog** uses (the two jogs' speed) are untouched.

**The ends.** *Menu* (`AdvanceMenuSelection`): the selection moves to the next *available* item in the direction pushed, and **stays** if there is none (it used to wrap modulo the number of items). *SD file list*: stops on the first and the last
file. *Adjustment jog, motor*: the stick **selects**, left = L, right = R, centred = unchanged — it used to toggle, so pushing right while R was selected went back to L. *Calibration, step size* (1 / 10 / 100 mm): once per gesture,
right = coarser, left = finer, clamped at both ends.

**Verified** (`tests/host/test_ui.cpp`, 4d–4f, all four combinations of the polarity flags, in both variants): a 17-step sequence of stick positions through the threshold, the release level, both sides and the extreme, on both axes; the
axes are independent; the menu stops at both ends (idle, uncalibrated with the jog hidden, and while running, where Cancel is the last item of all); L / R selected and never toggled, short pushes ignored; the step size stops at both ends and
changes once per gesture; the file list stops on the last file. `tests/mutation/mutants/ternary.txt`: 14 broken copies, all caught (13 in the serial build, where the file list does not exist, plus its own in the SD build).

**Not decided here:** the two numbers (250 and 170), to be tuned on the bench; the value ± of the numeric wizard still steps at a fixed repeat rate (200 ms), see the discussion of applying the Control jog's curve to it.

## REQ-CoreLR-D-LcdJoystickUI-0023 — Lengths on the LCD: fixed decimal columns, nearest tenth, no "-0.0" (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0023"></a>
Origin: the maintainer's bench, "cosmetic point": the numbers moved about as they changed length, and a position a few micrometres below zero read `-0.0`.

**Layout** (columns counted from 0; `SwConfig.h`: `LCDJOYSTICKUI_X_POINT_COLUMN` 6, `_Y_POINT_COLUMN` 14, `_L_LABEL_COLUMN` 1, `_R_LABEL_COLUMN` 9, `_CAL_STEP_COLUMN` 9):

```
column  0123456789012345
        I  123.4  -123.4      status: tag, X, Y
        ^L 761.7 R 535.5      cable lengths, the pen in column 0: ^ up, v down (-0030)
          1234.5 +-100mm      numeric entry (-0010)
```

The decimal point of the first value is always at column 6, that of the second at column 14: the number is right-aligned on the point, the **minus sign glued to its first digit**, and a length (never negative) has none. The widest
position, `-2097.2` (the reach, `MOTION_REACH_SHIFT`), takes columns 1..7, so the tag in column 0 is never touched and column 8 stays blank; the labels `L` and `R` sit left of the digits they introduce. Because nothing
moves when a number gains or loses a digit, only the digits that really change are re-sent to the display (`-0011`): the column rule is also what keeps the bus traffic small.
The `jog    miss:N` line of the Control jog (`CORELR_DEBUG_MISS_COUNT`) is a diagnostic and keeps its own text.

**Rounding.** The value is rounded to the **nearest tenth of a millimetre, half away from zero** (`tenths = (|µm| + 50) / 100`), not truncated: 49 µm shows `0.0`, 50 µm `0.1`, −50 µm `-0.1`. A result of `0.0` **carries no sign**, whatever
the sign of the value (−40 µm shows `0.0`, not `-0.0`).

**The cap, and when it exists.** The field before the point is five characters, a sign and four digits: at most **9999.9**. A value that cannot reach it needs no code to limit it, so the limit is compiled in only when the
configuration can reach it: `LCDJOYSTICKUI_DISPLAY_CAP` is 1 when the largest position (`2^MOTION_REACH_SHIFT − 1` µm), cable length (`CORELR_MATH_48_LIMIT_UM` in the 48-bit arithmetic, any int32 in the 64-bit) or numeric
entry (`LCDJOYSTICKUI_CAL_MAX_UM`) is **9999.95 mm or more** — the first value that would round to a fifth digit. Then a larger value is shown as `9999.9`, a smaller one as `-9999.9`. In the default 48-bit build nothing can, the
cap is not compiled, and the formatter costs nothing for it; in the 64-bit build it is. (The test works out whether a cap must exist from the configuration itself, not from the macro, so that a broken condition is caught.)

**One formatter.** `PutMm()` writes into the shared compose line at a given point column; `ClearCompose()` blanks it. The status screen's two rows and the two numeric-entry screens all use it (the entry screens used to
render a left-aligned string through a scratch buffer and `FormatMm`, which truncated and printed `-0.0`). The two 8-byte scratch buffers are gone: **−18 bytes of RAM**, and the code is smaller: SD build 30 360 → 30 316 bytes of flash
(404 free), serial build 26 156 → 26 160. The numeric entry also gained an upper bound, `LCDJOYSTICKUI_CAL_MAX_UM`, which it did not have: without it the entry could reach any int32 and the cap would always be needed.

**Verified** (`tests/host/test_ui.cpp`, 4g and 4h, both variants and both arithmetics): 20 exact renderings at both points (zero, ±49/50/99/149/150 µm, the reach, 4 m, with and without a sign); two sweeps against an
independent reference (`llround`, half away from zero) — **every micrometre of −2100..2100 mm** (4 200 002 renderings, at both point columns) and 0..4200 mm every 37 µm for the lengths — with the point always where it belongs and
nothing written outside the field; under the 64-bit arithmetic the cap on 9999.9, 9999.949, 9999.95, 10 000, 123 456.789 mm, `INT32_MAX` and `INT32_MIN`; the screens as the glass shows them (status, negative X, a −5 µm Y, the tag in column 0, both
numeric-entry screens, the entry stopping at its maximum). `tests/mutation/mutants/display_format.txt`: 24 broken copies, all caught; `tests/mutation/mutants/display_cap.txt` (run under the 64-bit arithmetic): 6, all caught. One mutant was
dropped as **equivalent**: the numeric entry shown *with* its sign — the entry is clamped at 0, so it is never negative.

**Not decided here:** the numeric entry's `+-` is plain ASCII (the display's character set has no `±` in the position one would hope); the cable-length term of the cap condition is deliberately generous in the 64-bit
arithmetic (any int32), since a length there is the result of a user-entered distance plus a position.

## REQ-CoreLR-D-LcdJoystickUI-0024 — "Return home": a menu entry that sends the pen to X=0, Y=0 (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0024"></a>
Realizes `REQ-CoreLR-F-UserInterface-0011`. Proposed by the maintainer and settled with him: second entry of the main menu, the pen always lifted, interruptible with SW, and optional at build time.

**The entry.** `MENU_HOME`, label `Return home`, right after "Jog" (`CORELR_MENU_HOME`, `SwConfig.h`, `#ifndef`-guarded, default **1**; 0 removes the entry, its mode, its label and the `H` tag). Offered **exactly as the jog is**: only
when calibrated (before that (0,0) means nothing: the position is not known), and, by the existing `MenuItemAvailable()` rule, never while a job is running, paused or in error — the menu then offers only Pause / Resume / Cancel.

**What it does.** Selecting it enters a screen mode, `LCDJOYSTICKUI_MODE_HOMING`:
1. `PenControl_RequestUp()` — a no-op when the pen is already up (no settle delay for nothing). A pen request goes straight into the step queue, and `PenControl_Task()` re-offers it before `Motion_Task()` every pass, so
   the nib is up before any of the move's steps, whatever happens to the queue. The pen is **left up** at the origin.
2. One `Motion_QueueMove()` to the absolute drawing-frame origin, *through the planner* like a jog block (blocks of a jog just left may still be draining, so it is queued as soon as `Motion_PlannerHasRoom()`, retried if refused, and
   not assumed accepted). One straight line, accelerating and decelerating, at the Cartesian speed cap (21.5 mm/s at the motors' measured limit): a diagonal metre takes about 50 s, hence the next paragraph.
3. The screen is the status layout (live X, Y, L, R) with the tag **`H`** in column 0 while it lasts, and `LcdJoystickUI_IsBusy()` is true — as for every mode that can move the machine, so the serial link refuses
   GCode meanwhile (`-0019`).
4. **Done** when the generator is idle **and** the planner empty **and** the move was accepted (`Motion_IsIdle()` alone would say so while a block is still queued, `Motion_PlannerIsEmpty()` alone while a block is being
   generated): back to the status screen.

**SW stops it** (a short or a long press; a second one changes nothing): `Motion_Halt()` bends the profile into a deceleration ramp, and only when `Motion_IsHaltRampDone()` is the rest of the move dropped with
`Motion_Abort()` — the same two steps as a job's cancel (`grbl_corelr.ino`), so the machine never stops dead, and the tracked position stays what was really commanded. A press before the move was ever queued queues nothing.

**Cost, measured** (Nano, `tools/analysis/build_size.sh`): serial build 26 160 → 26 364 bytes of flash (**+204**, 4 356 free), SD build 30 316 → 30 552 (**+236**, 168 free); RAM +2 bytes (the two flags). Both fit, so the default is on in both variants;
the SD build has little room left, and `-D CORELR_MENU_HOME=0` returns the 236 bytes. Decided with the maintainer: should it ever not fit, off in the SD build and on in the serial one.

**Verified** (`tests/host/test_ui.cpp` 4i, both variants, both arithmetics, and with the option off): second in the menu; the label; offered idle and calibrated, not uncalibrated, running, paused or in error; the pen request before the move; one move
to exactly (0,0), queued once; not finished while a block is still planned, nor while one is generated; arrival; SW ramps then aborts, a second press ignored; a full planner (nothing queued, queued once there is room, a refused move
retried); a press before queueing and a press at the very first call (nothing queued); a short and a long press through `Task()`; the glass (`H`, points at columns 6 and 14, back to `I`). `tests/mutation/mutants/home.txt`: 22 broken copies, all caught.
Not exercised: a real move on the machine (the planner and generator are a model in the host test, `Motion_Halt`/`Motion_Abort` themselves are the job-cancel path's, tested in `test_motion`).

## REQ-CoreLR-D-LcdJoystickUI-0025 — Where the screens lead: a tag per mode, and leaving the jog goes home (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0025"></a>
Origin: the maintainer's ergonomics review, 2026-10-10.

- **The tag says what the screen is doing.** Column 0 of the status layout was `I` whether the machine was idle or being jogged. It is now **`J`** while the Control jog is active (`LCDJOYSTICKUI_TAG_JOG`), **`H`** while "Return home" is (`-0024`),
  and the machine's own state otherwise (`R`, `I`, `P`, `E`, `U`).
- **Leaving the Control jog returns to the status screen**, not to the menu that opened it: either press of SW. (A jog is an action, not a place in the menu; a wizard that is *cancelled* still goes back to the menu, where it was chosen.)

**Verified** (`test_ui.cpp` 4j, both variants and with `CORELR_MENU_HOME=0`): tag `J` in the jog; a short and a long press both reach the status screen, tag `I`; no press, no exit. `tests/mutation/mutants/ergonomics.txt`: 4 broken copies, all caught.

## REQ-CoreLR-D-LcdJoystickUI-0026 — The menu has a way out, and gives way when the machine's state changes under it (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0026"></a>
Origin: the maintainer's ergonomics review, 2026-10-10 ("Back" proposed by the maintainer; the serial symptom confirmed by him: with Candle driving the machine while the LCD sat on the menu, the display stayed on the menu).

- **"Back", the last entry** of the main menu (`MENU_BACK`): offered in **every** state, shown as an arrow and the word (`LCDJOYSTICKUI_BACK_ARROW` + ` Back`), a short press goes to the status screen. The long press keeps doing so. It also fixes the menu that
  offered nothing in the error state. The arrow is a character *code* of the display's ROM, so it is a hardware setting (`HwConfig.h`, default `0x7F`, a left arrow in the common A00 ROM): if the entry shows an odd symbol, `'<'` works on every ROM.
- **A state change closes the menu.** `LcdJoystickUI_SetMachineState()` -- called every pass by `loop()` with the job state -- returns to the status screen when the state *differs* from the last one **and** the menu is open: a job started, paused, resumed,
  failed or finished from the serial link makes what the menu offers stale (it offers Pause / Resume / Cancel according to the state), and the user wants to see the position. Other modes are left alone: they keep the serial link out
  (`LcdJoystickUI_IsBusy()`, `-0019`), so the state cannot change under them. In the SD variant it also applies when a job started from the LCD ends while the menu happens to be open.

**Cost, measured** (with `-0025`): serial 26 364 → 26 432 bytes of flash (+68, 4 288 free), SD 30 552 → 30 624 (+72, **96 free**), RAM unchanged.

**Verified** (`test_ui.cpp` 4k, both variants and with `CORELR_MENU_HOME=0`): Back last and offered in idle, uncalibrated, running, paused and error; its label; chosen by `EnterMenuItem` and by a short press in the menu; drawn with the arrow; the menu closing on a job's start, pause, error and end but not when the state is
repeated; every other mode unchanged by a state change. `tests/mutation/mutants/ergonomics.txt`: 10 broken copies in all (`-0025` and this), all caught.
Not exercised: the arrow on the real display's ROM, and Candle starting a job while the menu is open.

## REQ-CoreLR-D-LcdJoystickUI-0027 — The L/R jog has the Control jog's shape (implemented 2026-10-10)
<a id="REQ-CoreLR-D-LcdJoystickUI-0027"></a>
The maintainer asked (2026-10-10) that the Control jog's response also drive the calibration menus, the L/R wires first.
- **One shape.** `JogCurve_Fraction(magnitude)` is the Control jog's curve alone (dead zone subtracted, scaled to the stick's full scale, the table of `JogCurveTable.h`, 0..65535). `JogCurve_Displacement()` multiplies it by the maximum speed; the Adjustment jog's `AdjustTargetRate()` maps it
  onto its step-rate range, `MIN + (MAX - MIN) * fraction`. It was a second copy of the same quadratic; with one table, `tools/generators/gen_jogcurve.py -e …` retunes both. The feel is therefore **the same shape, not a different one** -- what still differs is physical: the L/R jog starts at 10 steps/s and, from rest, at the motor's pull-in rate
  (120 steps/s) before its ramp (`-0014`, `-0015`). Flash −18 bytes (the duplicate arithmetic is gone). `JogCurve_Fraction` took the clamp that was inline; one more clamp, redundant by construction, is kept because the build is 38 bytes smaller with it.
- **The value editors** of the wizard (the Y ± at a fixed repeat rate) are not changed: not decided.
**Verified:** `test_jogcurve.c` section 7 (fraction: 0 in the dead zone, monotone, 65535 from full scale to 65535 counts, within 25/65535 of the quadratic, the displacement = Vmax × fraction); `test_ui.cpp` 4m (the L/R rate: MIN in the dead zone, MAX at full scale, within 1.5 steps/s of `MIN + (MAX-MIN)·n²`, monotone, sign-independent);
`tests/mutation/mutants/jogshape.txt` (3 of 4 caught; the fourth, `<` for `<=` at the edge of the dead zone, is equivalent: r = 0, Shape(0) = 0) and `adjustment.txt`.

## REQ-CoreLR-D-LcdJoystickUI-0028 — The planner depth is a RAM lever, 8 blocks recommended for every image; the Control jog keeps 4 (implemented 2026-10-10, amended 2026-10-29)
<a id="REQ-CoreLR-D-LcdJoystickUI-0028"></a>
**Amended 2026-10-29 (maintainer).** `MOTION_PLANNER_DEPTH` is a **lever to gain RAM** (each block costs RAM: +84 bytes for the four blocks added here), not a difference between the images: **8 is recommended for all**, and both images have 8 since 2026-10-13 (`REQ-CoreLR-D-Motion-0013`, `SwConfig.h`). What follows is the first decision as it was taken on 2026-10-10, when the SD image still kept 4 for its browser's stack.

The maintainer's decision (2026-10-10, "we have the RAM"): `MOTION_PLANNER_DEPTH` was set to **8 in the serial variant**, 4 in SD (the stack margin that forced 8 → 4 on 2026-09-11 is the SD browser's, and the serial variant has none), for streamed files: short segments (an SVG-derived file, arcs cut into chords) hold their speed from ~1 mm instead of ~2 mm.
RAM +84 bytes (861 used, 1 187 left for the stack); `Bf:` and `$I`'s `[OPT:,8,128]` report it. A jog tuned at depth 4 must not lag with 8 (it keeps up to 4 blocks of 30..60 ms: a released stick stops within ~0.1 s), so `LCDJOYSTICKUI_JOG_BLOCKS` (4) caps how many blocks the Control jog keeps queued: it feeds the planner only while
`Motion_PlannerFree() > DEPTH - JOG_BLOCKS` -- identical to the former `Motion_PlannerHasRoom()` at depth 4; flash +22 bytes (serial), +18 (SD). The host tests that filled "4 blocks" by hand now use the macro (`fill_planner()`, `PD`).
**On the machine** (the 20 cm circle R = 100 mm, 828 mm of path in 40.0 s: **20.7 mm/s on average** for a 21.5 mm/s ceiling; the 200 mm square, 1 081 mm in 51.8 s: 20.9 mm/s; the test card, 12 111 mm in 588 s: 20.6 mm/s): the speed is held through the arc's chords and the card's short segments. `test_ui.cpp` still passes with the jog's `PlannerFree` rule; the host suite in both arithmetics.

## REQ-CoreLR-D-LcdJoystickUI-0029 — The zone in the jog and in the wizard (implemented 2026-10-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0029"></a>
Realizes: `REQ-CoreLR-F-Motion-0008`, `REQ-CoreLR-F-Calibration-0009`. **Control jog:** each slice goes through `Motion_QueueJog()` (`REQ-CoreLR-D-Motion-0028`), which brings the target into the zone: the pen stops at the edge and slides along it, and nothing is queued when the clamped target is where the queue already ends (no zero-length block). The clamping itself is tested with the real Motion in `test_travel.c`; `test_ui.cpp` checks that the jog calls `Motion_QueueJog()`.
**Calibrate machine, step 2:** the confirming press calls `Calibration_SetParams()`; on a refusal (`REQ-CoreLR-D-Calibration-0011`) the wizard stays on the step and **row 0 reads `Ref out of zone!`** (in place of the prompt) until the value is changed; changing it clears the message, and entering the wizard afresh starts clean. Host test: `tests/host/test_ui.cpp`.


## REQ-CoreLR-D-LcdJoystickUI-0030 — The pen's state on the status screen (implemented 2026-10-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0030"></a>
Origin: the maintainer's bench, 2026-10-11, the first SD job: "on the display we do not know where the pen is" -- up or down. Realizes: `REQ-CoreLR-F-UserInterface` (the status screen shows what the machine is doing).
**Column 0 of row 1** of the status screen -- the one cell the layout of `-0023` leaves empty (row 0's column 0 is the state tag; row 1 has `L` in column 1) -- shows the pen: **`v` pen down, `^` pen up** (`LCDJOYSTICKUI_PEN_DOWN_CHAR`, `_PEN_UP_CHAR`, `SwConfig.h`).
Plain ASCII, so that it does not depend on the display's character ROM; letters were avoided because they would read as state tags.
- It is the **physical** state, `PenControl_IsDown()` -- what the servo shows -- not the requested one (`PenControl_IsRequestedDown()`), which is ahead of it by the settle time.
- It is on the status screen only, **including while a job runs and in the Control jog** (where the pen stays up); not on the menus and wizards. The one exception is the Control jog's `jog    miss:N` debug line (`CORELR_DEBUG_MISS_COUNT`), which keeps its own text.
- The frame buffer re-sends only the character that changes (`-0011`): a pen move costs one I2C character.
Cost, measured: see the Status line of `docs/bringup/Bringup-Status.md` (the SD variant had 208 bytes of flash left before). Tests: `tests/host/test_ui.cpp` 4o (both glyphs, the neighbours untouched, during a job and in the jog); 8 mutants in `tests/mutation/mutants/penchar.txt`, all caught.

## REQ-CoreLR-D-LcdJoystickUI-0031 — "Set origin" and "Origin = calib." (implemented 2026-10-11)
<a id="REQ-CoreLR-D-LcdJoystickUI-0031"></a>
Realizes: `REQ-CoreLR-F-Calibration-0010`. Two main-menu items, offered like the jog (the position must be known, no job running or paused), placed after "Return home". Choosing one opens a **confirmation screen** -- row 0 `Origin = here?` or `Origin = calib.?`, row 1 `SW=ok  hold=no` -- a short press of SW does it, a long press leaves without doing anything.
Doing it calls `Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0)` or `Motion_SetOrigin(MOTION_ORIGIN_REFERENCE, 0, 0)` and goes to the status screen, where the new position (0.0, 0.0 after "Set origin") can be seen. If Motion is not idle yet (a jog slice still draining) or the origin would be outside the zone, nothing is done and the screen stays: a second press tries again.
The confirmation keeps the serial link out (`LcdJoystickUI_IsBusy()`). Row 0 reads `Origin = here?` / `Origin=calib.?`. Tests: `tests/host/test_ui.cpp` 4p (labels, availability in every job state, both flows, a long press, a refusal staying on screen, a press needed).


## REQ-CoreLR-D-LcdJoystickUI-0032 — The pen's glyphs are pictograms, switchable (decided 2026-10-13, maintainer's design)
<a id="REQ-CoreLR-D-LcdJoystickUI-0032"></a>
Origin: `-0030` chose `v` and `^` for want of anything better in the ROM; the maintainer asked whether the controller had better characters. It has an **A00 ROM** (T09_LcdRom): no vertical arrows. The maintainer drew two pictograms of 5 x 8 dots, a pen over a line of paper, and decided that they must be **switchable at build time** because of their cost:

```
 pen DOWN (code 1)    pen UP (code 2)
 . . # . .            . . # . .
 . . # . .            . . # . .
 . . # . .            # # # # #
 . . # . .            . # # # .
 # # # # #            . . # . .
 . # # # .            . . . . .
 . . # . .            . . . . .
 # # # # #            # # # # #
```
(rows `04 04 04 04 1F 0E 04 1F` and `04 04 1F 0E 04 00 00 1F`.) **`LCDJOYSTICKUI_PEN_GLYPHS`** (`SwConfig.h`): 1 = the pictograms are loaded into the CGRAM at start-up (`LcdJoystickUI_Init()`, after `LcdI2c_Init()`, by `LcdGlyphs_Load()`, which calls `LcdI2c_DefineCharP()`, `-LcdI2c-0005`) and `LCDJOYSTICKUI_PEN_DOWN_CHAR` / `_UP_CHAR` are the codes 1 and 2; 0 = nothing is loaded and the two macros are `'v'` and `'^'` (the whole `LcdGlyphs` module is compiled out). **The shapes and the codes live in their own module, `LcdGlyphs.h/.c`** (2026-10-13, the maintainer's decision: the convention is one small `.h/.c` pair per general resource, the UI file is 1 900 lines, and a later icon is added there without touching the screens); `SwConfig.h` cannot include its header (the header includes `SwConfig.h`), so it states the two codes itself and `LcdGlyphs.c` refuses to compile if they disagree. No cost over the version inside the UI file (same size to the byte). **Kept in both variants**, the SD one included (196 bytes of flash left). Default: see the Status line of `docs/bringup/Bringup-Status.md` (per variant, the flash decides). Everything else of `-0030` is unchanged (column 0 of row 1, the physical state, one character per pen move).

## Open items
- Jog speed-vs-deflection curve — **settled for "Adjustment" mode** 2026-09-11 (`-0014`) and **implemented for "Control" mode 2026-10-10** (`-0021`: quadratic in the norm of
  the stick, dead zone subtracted, 40 mm/s, a tabulated shape). Its shape, dead zone and full scale are still to be tuned on the bench.
- The ±32 dead-zone value is a starting default; may need retuning per
  physical joystick unit during bring-up.
- **Entering a jog mode, the pen wizard, or the SD browser while a job
  is `RUNNING` — resolved 2026-09-10** by
  `REQ-CoreLR-D-LcdJoystickUI-0012`: while a job is active the menu
  offers only the job controls, so the interleaving is impossible by
  construction rather than blocked by a guard.

- **Affected by the 2026-10-06 event-driven change** (`REQ-CoreLR-D-Stepper-0010`…`-0014`, to be revised at implementation, not before):
  `-0014`/`-0015` — the adjustment jog's **dithered hold is gone** (done 2026-10-06: the hold is computed directly in 0.5 µs counts, rounding error ≤ 0.25 µs, and the accumulator and its RAM are removed); its rate ramp is unchanged;
  `-0016` — the queue top-up policy is stated in entries, whose duration is now 100× longer at speed;
  `-0017` — **fixed 2026-10-06**: the Cartesian jog called `Motion_RequestLinearMove()`, which bypasses the planner and is ignored while a move is in progress, although this requirement says it feeds it. It now adds each slice to `Motion_GetTargetPosition()` and queues it with `Motion_QueueMove()`.

**Convention (2026-09-07, memory-optimization pass):** every *fixed*
string this module prints to the LCD goes through `PrintPadded_P()`
with a `PSTR("...")` literal (or an indexed `PROGMEM` table, like
`kMenuLabels`, for a value chosen at runtime) -- never `PrintPadded()`
with a plain literal, which is RAM-resident on classic AVR. A gap here
(`Serial.begin()` left unconditional, several Redraw functions'
literals/`snprintf` formats not yet converted) was found and fixed
after the first real compile came in at 182 bytes free RAM ("stability
problems may occur") -- ~169 bytes recovered. `PrintPadded()` itself
stays correct for already-RAM-resident, runtime-built content (a
formatted buffer, the SD browser's cached entry name).
