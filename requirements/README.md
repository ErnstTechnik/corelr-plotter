# Requirements index — CoreLR

Requirements are split in two, per decision on 2026-09-06:

- **`Functional/`** — `REQ-CoreLR-F-<SubFunction>-nnnn`. *What* the
  system must do, independent of implementation. Should stay stable even
  if the implementation changes.
- **`Design/`** — `REQ-CoreLR-D-<Module>-nnnn`. *How* a function is
  realized: algorithms, data structures, timing, interfaces. Each Design
  requirement/file states which Functional requirement(s) it realizes.

Numbering restarts at `0001` within each sub-function/module (the name
already disambiguates, so no global range reservation is needed).

## Which image a requirement applies to
The project builds two firmware images, `serial` and `sd` (`CORELR_JOB_SOURCE`). **A requirement applies to both unless it says otherwise**: one that concerns a single image carries, on the line after its
anchor (`<a id="..."></a>`), the line `*Applies to: serial image only*` or `*Applies to: sd image only*`. The API documentation of the other image marks it as not part of that image
(`docs/standards/STD-FileOrganization.md`, `REQ-CoreLR-D-Build-0019`).

## Functional (what)

| Sub-function | File | Realized by (Design) |
|---|---|---|
| `Motion` | [F-Motion.md](functional/REQ-CoreLR-F-Motion.md) | [D-Motion.md](design/REQ-CoreLR-D-Motion.md) |
| `Actuation` | [F-Actuation.md](functional/REQ-CoreLR-F-Actuation.md) | [D-Stepper.md](design/REQ-CoreLR-D-Stepper.md) |
| `Calibration` | [F-Calibration.md](functional/REQ-CoreLR-F-Calibration.md) | [D-Calibration.md](design/REQ-CoreLR-D-Calibration.md) |
| `PenControl` | [F-PenControl.md](functional/REQ-CoreLR-F-PenControl.md) | [D-PenControl.md](design/REQ-CoreLR-D-PenControl.md) |
| `GCodeInterpreter` | [F-GCodeInterpreter.md](functional/REQ-CoreLR-F-GCodeInterpreter.md) | [D-GCodeParser.md](design/REQ-CoreLR-D-GCodeParser.md), [D-SDReader.md](design/REQ-CoreLR-D-SDReader.md) *(comments)* |
| `JobStorage` | [F-JobStorage.md](functional/REQ-CoreLR-F-JobStorage.md) | [D-SDReader.md](design/REQ-CoreLR-D-SDReader.md) |
| `UserInterface` | [F-UserInterface.md](functional/REQ-CoreLR-F-UserInterface.md) | [D-LcdJoystickUI.md](design/REQ-CoreLR-D-LcdJoystickUI.md) |
| `Build` | [F-Build.md](functional/REQ-CoreLR-F-Build.md) | [D-Build.md](design/REQ-CoreLR-D-Build.md) |
| `SerialControl` | [F-SerialControl.md](functional/REQ-CoreLR-F-SerialControl.md) | [D-Uart.md](design/REQ-CoreLR-D-Uart.md), [D-SerialLink.md](design/REQ-CoreLR-D-SerialLink.md) *(protocol in [`../docs/protocol/SerialProtocol.md`](../docs/protocol/SerialProtocol.md))* |

## Design (how)

| Module | File | Realizes (Functional) |
|---|---|---|
| `Motion` | [D-Motion.md](design/REQ-CoreLR-D-Motion.md) | Motion |
| `Stepper` | [D-Stepper.md](design/REQ-CoreLR-D-Stepper.md) | Actuation, PenControl (sync, -0004) |
| `Calibration` | [D-Calibration.md](design/REQ-CoreLR-D-Calibration.md) | Calibration |
| `PenControl` | [D-PenControl.md](design/REQ-CoreLR-D-PenControl.md) | PenControl |
| `GCodeParser` | [D-GCodeParser.md](design/REQ-CoreLR-D-GCodeParser.md) | GCodeInterpreter |
| `SDReader` | [D-SDReader.md](design/REQ-CoreLR-D-SDReader.md) | JobStorage, GCodeInterpreter (comments) |
| `LcdJoystickUI` | [D-LcdJoystickUI.md](design/REQ-CoreLR-D-LcdJoystickUI.md) | UserInterface |
| `LcdI2c` | [D-LcdI2c.md](design/REQ-CoreLR-D-LcdI2c.md) | UserInterface (the display), Actuation (frees the TWI interrupt) |
| `Uart` | [D-Uart.md](design/REQ-CoreLR-D-Uart.md) | SerialControl (serial variant: the port, the rings, the real-time bytes) |
| `SerialLink` | [D-SerialLink.md](design/REQ-CoreLR-D-SerialLink.md) | SerialControl (serial variant: lines, replies, report, `$` commands) |
| `IsrDispatch` | [D-IsrDispatch.md](design/REQ-CoreLR-D-IsrDispatch.md) | Actuation (timer), UserInterface (-0009 time base) |
| `SysTick` | [D-SysTick.md](design/REQ-CoreLR-D-SysTick.md) | UserInterface (-0009 time base) |
| `Build` | [D-Build.md](design/REQ-CoreLR-D-Build.md) | Build (the CMake + Ninja build, size limits, local settings) |

`IsrDispatch` and `SysTick` were split out 2026-09-09 and implemented
2026-09-10: the timer/ISR previously lived inside
`Stepper`, which made it the firmware's de-facto clock provider.
`IsrDispatch` now owns the timer and vector and dispatches each tick to
`Stepper_Tick()` then `SysTick_Tick()`, so both are ordinary
tick consumers.

## v2 architecture, 2026-10-06

The step/waypoint decoupling is specified, not yet implemented. Where to
read it, in order:

1. `docs/bringup/Bringup-Status.md` — the measurements that motivated it (the
   per-waypoint cost, the CPU ceiling, the boot defect).
2. [`F-Actuation-0003`](functional/REQ-CoreLR-F-Actuation.md) (smooth
   stepping) and [`F-Calibration-0003`/`-0007`](functional/REQ-CoreLR-F-Calibration.md)
   (position persistence and validity).
3. [`D-Stepper-0010`…`-0014`](design/REQ-CoreLR-D-Stepper.md) — the
   event queue, the Timer1 ISR, the delay limits, arming.
4. [`D-Motion-0018`…`-0020`](design/REQ-CoreLR-D-Motion.md) — how `Motion`
   computes the step events.
5. [`D-Calibration-0006`…`-0008`](design/REQ-CoreLR-D-Calibration.md) — the
   position record.
6. [`D-PenControl-0003`](design/REQ-CoreLR-D-PenControl.md),
   [`D-IsrDispatch`](design/REQ-CoreLR-D-IsrDispatch.md),
   [`D-SysTick`](design/REQ-CoreLR-D-SysTick.md) — the timer move.

Older requirements that this supersedes carry a status note directly
under their heading ("Superseded … implemented") and are kept
for the history of why they existed. The code implements the new ones.

## Arcs, 2026-10-07

`G2`/`G3` with `I`/`J`, specified; implementation follows. In order:

1. [`F-GCodeInterpreter-0003`](functional/REQ-CoreLR-F-GCodeInterpreter.md) (syntax and rules; `R` deferred),
   [`-0009`](functional/REQ-CoreLR-F-GCodeInterpreter.md) (radius consistency, grbl's rule),
   [`F-Motion-0007`](functional/REQ-CoreLR-F-Motion.md) (accuracy, speed).
2. [`D-GCodeParser-0008`](design/REQ-CoreLR-D-GCodeParser.md) (a queued move starts where the queue ends — a defect
   fixed on the way) and [`-0009`](design/REQ-CoreLR-D-GCodeParser.md) (arc words and validation).
3. [`D-Motion-0021`…`-0023`](design/REQ-CoreLR-D-Motion.md) — the `Trig` library, the arc geometry and generation, the
   arc in the planner and profile. `tools/prototypes/arcproto` holds the measurements behind the choice of kernel.

Coding and documentation conventions (language, style, doxygen, module
prefixing, copyright, where docs live) are in
[`docs/standards/`](../docs/standards/README.md) and the way changes are made in
[`CONTRIBUTING.md`](../CONTRIBUTING.md) — they are process rules, not numbered
requirements. The **L/R motor-naming convention** is likewise not a numbered
requirement (it is neither a function nor a design decision, just a naming
convention): the two motors are **L** (left) and **R** (right) everywhere in the code,
the comments, the documentation and the requirements, never A/B.

Origin tracking (which informal maintainer note prompted which
requirement) is kept out of this repository entirely, in the maintainer's own
working notes — see each requirement file's "Origin" line.

*(Closed 2026-09-10: the header-scope cleanup is applied. Six internal
types moved out of public headers into their module's `.c` and lost
their module prefix per the file-local rule — `Stepper`'s entry/indices/
queue, `SDReader`'s indices/line-queue, and `LcdJoystickUI`'s UI-mode
enumeration; `GCodeParser`'s modal-state struct followed once the dead
`GCodeParser_GetState()` was removed. No `_priv.h` was needed anywhere:
every module is a single `.c`/`.cpp`. Verified byte-identical firmware
size before/after, as a pure scope change should be.)*

## Status legend
- **Approved** — decision confirmed with the maintainer, ready to
  design/implement against.
- **Open item** — noted inside a requirement file; not yet decided,
  do not implement against it without resolving first.
- **Placeholder** — a Design file exists only to reserve the module name
  and record what's still to be decided; no approved content yet.

## Traceability rule
Every `Design/` requirement or file states, near its top, which
`Functional/` requirement(s) it realizes. When a design changes, check
whether the functional requirement it realizes still holds — and when a
functional requirement changes, check every design file that claims to
realize it.

## Cross-cutting design note: `CORELR_NOINLINE` (2026-09-11)

Every function `loop()` calls carries `CORELR_NOINLINE`
(`SwConfig.h`). This is a **RAM** measure, not a speed one, and it is
the fix for a real stack overflow found on hardware.

The Arduino core's `main()` calls `setup()` then `loop()`. With
`-flto`, which the IDE always passes, GCC sees the whole program at
link time and inlines **across translation units** -- so both, and
everything they call, were folded into `main()`. The compiler then
allocates one frame at `main()`'s entry, the union of every inlined
local, and it stays allocated for the life of the program.

The cost is not the size of any single function but that mutually
exclusive paths stop sharing stack: the SD browser and the GCode parser
never run together, yet their buffers coexisted permanently.

| | `main()`'s own frame | Worst-case stack | Free RAM | |
|---|---|---|---|---|
| Inlined | 206 | 428 | 316 | **overflows** |
| `CORELR_NOINLINE` | 36 | 320 | 352 | +32 margin |

The board reset about once a second until this was applied. Flash fell
by 728 bytes too, inlining having duplicated code.

Two things worth knowing, both learned the hard way:

- **Separate `.c` files do not achieve this.** The modules already are
  separate translation units and were inlined anyway. Only the
  attribute stops LTO.
- **`tools/analysis/stack_depth.py` reported 43 bytes for a `main()` frame of
  206**, because it folded `subi r28, 0xCA` as a two's-complement
  negative instead of an allocation of 202. Every margin reported
  before 2026-09-11 was wrong, and always too optimistic. Fixed, with
  the trap spelled out in the tool.

The lesson for this project: a stack figure from static analysis is a
hypothesis. `tests/T03_StackWatch` paints free RAM and reports the true
high-water mark on the target, and that is what settled it.
