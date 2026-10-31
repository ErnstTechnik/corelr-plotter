# hosttest — running the real modules on the PC

Host (x86, `-m32`) builds of firmware modules with a stub for whatever they
call, for logic that does not need the board. Complements `tools/analysis/cyclesim/`,
which measures *time* on an AVR interpreter; this checks *behaviour*.

`-fpack-struct=1` makes structs lay out as on AVR (no padding), so a
`sizeof`-based `_Static_assert` means the same here as on the target.

| Test | What it proves |
|---|---|
| `test_stepper_isr.c` | the Timer1 event ISR (`REQ-CoreLR-D-Stepper-0010`…`-0014`): the real `Stepper.c` `#include`d, Timer1 simulated count by count with random interrupt latency — timing error bounded by the latency and not accumulating, the late guard, idle/arm/idle, the split rule over 399 936 requests, the 300 ms pen settle through the backlog, ports carry the entry's bits, invalidation before publication |
| `test_pen.c` | the software servo (`REQ-CoreLR-D-PenControl-0003`): the real `PenControl.c`, Timer2 simulated; the pulse on the pin over 0–180°, the 20 ms frame, the hand-off from the step ISR |
| `test_parser.c` | the real `GCodeParser.c` driving the real planner: where does each queued move *start*? relative chains, omitted axes, mixed distance modes, with the head already moving (fails on the parser before the 2026-10-07 fix); **the reach limit** (a target of 2^21 µm or more is an invalid line; digit strings that used to wrap the int32; no trace of a refused line; ERROR before BUSY); **arcs** (`REQ-CoreLR-D-GCodeParser-0009`): `G2`/`G3` direction and end points, modal arc lines, relative mode, a full circle, `G17`, a distance word sharing a line with a motion word, invalid arc lines refused with **no effect**, BUSY versus ERROR |
| `test_motion.c` | the real, ported `Motion.c` (`REQ-CoreLR-D-Motion-0018`…`-0020`): jitter at `VMAX`, tangential reversals, rest-to-rest, a chain of jog blocks through the planner, **refusal-invariance** (the same moves with 0 % and 40 % of requests refused give an identical stream), halt/resume, abort, and a fuzz of 600 random moves |
| `test_trig.c` | `Trig` against long-double trigonometry (`REQ-CoreLR-D-Motion-0021`): 3 M rotations to a 2 m radius, the octant boundaries, 2 M vector angles, 1 M arc lengths |
| `test_arc.c` | arcs in the real `Motion.c` + `Trig.c` (`REQ-CoreLR-D-Motion-0022`, `-0023`), `#include`ing `Motion.c` to sample every waypoint: 1 346 arcs (radii 54 µm … 1.9 m, every octant, sweeps from 0.0006° to a full turn, both directions) against long-double geometry — waypoint, chord, sagitta; 48 full circles closing exactly; the radius rule; **refusal-invariance**; the centripetal ceiling against the physical √(a·r); junction speeds; halt/resume in mid-arc; a fuzz of 500 lines and arcs |
| `test_lcdi2c.cpp` | the real `LcdI2c.c` (compiled as C++) against `lcd_model.h`: a TWI unit, a PCF8574 and an HD44780 **with its timing rules** on a virtual clock — init by instruction and its waits, text on both rows, 200 000 random instructions with none sent while the controller was busy, a display that does not answer, a NACK of each data byte, a stuck bus, a stuck STOP, and the measured limitation (a failure that splits a byte garbles the display; `Init` recovers it). 18 mutants, 17 caught |
| `test_ui.cpp` | the real `LcdJoystickUI.cpp` (`#include`d, so its frame buffer is visible) against the same model: the glass equals the frame on all seven screens under random changes; **no bus traffic when nothing changes**; ≤ 1 character per `Task()` call; a change across the row boundary; padding/clipping; transient bus faults. 9 mutants, 8 caught + 1 equivalent; also run with `-D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL` (no "Load file", `LcdJoystickUI_IsBusy()`) |
| *(`test_motion`, `test_arc`)* | both also print a **hash of every entry sent to `Stepper`** (820 385 and 11 421 789 entries): a refactor of `Motion` that must not change behaviour is held to *the same hash before and after* — the strongest check there is on the generator. `mirror_y.h` (`-DMIRROR_Y`) runs both in the mirror image of their own world, to prove `REQ-CoreLR-D-Motion-0025`'s Y flip changed only the sign |
| `test_arith.c` | `Motion`'s integer arithmetic against exact references, **for either `CORELR_MATH`**: the 64-bit root (409 045 values), the inverse kinematics (5 840 positions), the length-to-sub-step conversion |
| `test_position.c` | the persisted position (`REQ-CoreLR-D-Calibration-0006`…`-0007`): a power cut at every byte of *invalidate, store* — clean before, clean after, or leaving the byte half-written — never restores a stale or mixed position; restore seeds `Motion` so the jog does not catch up; geometry change invalidates, pen angles do not; forward kinematics round trip |
| `test_uart.c` | the real `Uart.c` (**serial variant**, `-DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL`; `run_test.sh` adds it) with the USART0 registers as variables and the two interrupt handlers called by the test: UBRR and the frame format, data in order, the four real-time bytes (and inside a line), all 256 byte values, a ring of exactly 128 bytes, the gap marker after a loss (full ring, framing error, overrun), 100 000 bytes through the wrapping indices, the transmit ring (never blocks, never overwrites). `tests/mutation/mutants/uart.txt` |
| *(`test_ui.cpp`, 4d–4f)* | the stick as a choice (`REQ-CoreLR-D-LcdJoystickUI-0022`): the ternary threshold and its hysteresis on both axes for every polarity, the menu / file list / step size stopping at their ends, the motor selected (never toggled) by the stick; `tests/mutation/mutants/ternary.txt` |
| *(`test_ui.cpp`, 4g–4h)* | the length display (`REQ-CoreLR-D-LcdJoystickUI-0023`): decimal points in columns 6 / 14, nearest tenth, no `-0.0`, the conditional cap at 9999.9, a sweep of every micrometre of ±2100 mm against `llround`, the screens as the glass shows them; `tests/mutation/mutants/display_format.txt`, and `display_cap.txt` under `-D CORELR_MATH=CORELR_MATH_64` |
| *(`test_ui.cpp`, 4i)* | "Return home" (`REQ-CoreLR-D-LcdJoystickUI-0024`): availability like the jog, pen first, one move to (0,0), SW ramps down then aborts, a full or refusing planner, short and long press through `Task()`; `tests/mutation/mutants/home.txt` |
| *(`test_parser.c`, `test_seriallink.c` 14, `test_uart.c`)* | the `Z` pen word, `GCodeParser_ExecuteJog()`, `$J=` and 0x85 end to end (a bounded step queue holds a jog "in flight" so it can be cancelled), the L/R jog's rate and the calibration invalidated at its first raw step (`test_ui.cpp` 4l, 4m); `tests/mutation/mutants/zword.txt`, `jogparser.txt`, `joglink.txt`, `jogcancel_uart.txt`, `adjustment.txt`, `jogshape.txt` |
| *(`test_position.c`, the ring)* | the position ring (`REQ-CoreLR-D-Calibration-0010`): 400 cycles with a reboot each (the head and the 6-bit sequence number wrap), the newest record wins, the field extremes, the power-cut sweep at every byte in four torn modes from 14 head positions, for an invalidation + store and for a store over a valid record, the wear counted per cell (0.125 writes per cycle), random garbage; `tests/mutation/mutants/posring.txt`; with `-D CALIBRATION_POSITION_RING=0` it tests the SD variant's single record |
| `test_jogcurve.c` | the Control jog's response (`REQ-CoreLR-D-LcdJoystickUI-0021`): the real `JogCurve.c` (`#include`d) and `Motion_Hypot`, against the quadratic in floating point — nothing moves inside the dead zone and no jump outside it, the curve within 2 µm of the formula along an axis and monotonic, full speed from the full scale and no more, the speed that of the NORM over 2 760 stick positions of the disc, the direction the stick's, symmetric, proportional to the time, **sub-micrometre remainders carried** (2 000 blocks, both signs), no overflow at the extremes. Run it for both shapes (the computed one is the default, `-D JOGCURVE_USE_TABLE=1` the table). `tests/mutation/mutants/jogcurve.txt` |
| `test_chain.c` | the look-ahead at work (`REQ-CoreLR-D-Motion-0013`): the mean speed of 100 mm of equal short segments, with the **back-pressure** of the machine (a 32-entry step queue drained in real time, a generator call = 0.19 ms, a line every 0.6 ms); the table of the planner depth of the build (8 serial, 4 SD with `-D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD`). `test_motion`'s chain has no back-pressure and says nothing about short segments. |
| `test_sdreader.cpp` | the SD browser in the real `SDReader.cpp` against a **fake of sdfatlib** (`shim_sd/sdfat/SdFat.h`: the card is a tree in memory, unsorted, that can be pulled and put back): the listing order, entering and leaving directories, the depth bound to the byte (`SDREADER_PATH_SIZE`), the job's lines without their comments, and the recovery from a card pulled during a navigation (`REQ-CoreLR-D-SDReader-0009`). The fake counts the card reads (`g_readDirCalls`, `g_rewinds`). |
| `geometry_probe.c` | what the firmware's own geometry says for a given D and Lref: the real `Calibration_SetParams`/`MarkCalibrated` and `Motion_XYToLR` (driven by `tools/generators/gen_geometry_svg.py --verify`; `run_all.sh` checks that the committed `docs/hardware/images/Geometry.svg` is still its output and still agrees) |
| `gcode_probe.c` | not a test either: **what does the real parser answer to one line?** Reads lines on standard input, prints `ok`, `end` or `error:N` for each, from a fresh state; built per image (`-D CORELR_JOB_SOURCE=...`) and driven by `tests/tools/test_gcode_page.py`, which makes `docs/doxygen/gcode.md` true. |
| `gcode_check.c` | not a test but a **checker** (driven by `tools/bench/gcode_check.sh`): GCode files through the real parser and planner. `run_all.sh` runs it on `tests/bench/gcode/*.gc`, which must be accepted inside the 200 x 200 mm square, and on `gcode_samples/bad_*.gc`, each of which must be refused (a syntax error, a target out of the box, out of reach, no start at the origin, a pen left down, a line too long): a checker that accepts everything proves nothing. |
| `test_textfmt.c` | `TextFmt_Micrometres` against `printf` (the reference the firmware may not link) on 14 707 values and the extremes, and its clipping at the end of a buffer |
| `test_seriallink.c` | the serial link, **the whole stack but the hardware**: the real `Uart.c`, `SerialLink.c`, `GCodeParser.c`, `Motion.c`; the sketch's job state machine modelled in the test (the table of `REQ-CoreLR-D-SerialLink-0006`). The welcome line; line assembly (CR/LF/CRLF, comments, blanks, 80 characters, lost bytes); one reply per line; start, end and error of a job; `BUSY`; hold/resume/Ctrl-X; the status report in every state; every `$` command; and a torture of 3 000 lines through a host that counts to 128 bytes. `tests/mutation/mutants/seriallink.txt` |

    # one test:   tests/host/run_test.sh [-q] [-D NAME=V] TEST     (run_all.sh and run_test.sh both answer -h)
    # the whole suite, for either arithmetic (REQ-CoreLR-D-Motion-0024) -- both must pass:
    cd tests/host && ./run_all.sh CORELR_MATH_48 && ./run_all.sh CORELR_MATH_64

    # one test by hand (add -DCORELR_MATH=CORELR_MATH_64 to compile the 64-bit arithmetic):
    cd tests/host && mkdir -p out
    gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_position.c \
        ../../firmware/src/Calibration.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_position
    out/test_position

    # the display tests are C++ (the TWI registers are objects with side effects) and have their own shims:
    g++ -m32 -O1 -Wall -Wno-unused-function -Ishim_lcd -I../../firmware/src test_lcdi2c.cpp -o out/test_lcdi2c && out/test_lcdi2c
    g++ -m32 -O1 -Wall -Wno-unused-function -Ishim_lcd -Ishim_ui -I../../firmware/src test_ui.cpp ../../firmware/src/TextFmt.c ../../firmware/src/JogCurve.c -o out/test_ui && out/test_ui

    # every test that links Motion.c now links Trig.c as well; the ones that #include Motion.c (test_arith, test_arc) link Trig.c alone:
    gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function -Ishim -I../../firmware/src test_arc.c ../../firmware/src/Trig.c -lm -o out/test_arc && out/test_arc
    gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_trig.c ../../firmware/src/Trig.c -lm -o out/test_trig && out/test_trig
    gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_parser.c ../../firmware/src/GCodeParser.c \
        ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_parser && out/test_parser

    # the ISR and servo tests #include the .c under test, so its statics are visible:
    gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function -Ishim -I../../firmware/src test_stepper_isr.c -o out/test_stepper_isr && out/test_stepper_isr
    gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function -Ishim -I../../firmware/src test_pen.c -o out/test_pen && out/test_pen

A test that cannot fail proves nothing: `test_position` was run against deliberately broken copies of the firmware (marker written before the payload;
`Calibration_InvalidatePosition` a no-op; the restore in `Motion_Init`
disabled) and each is caught.

`test_stepper_isr` was likewise run against five broken copies of `Stepper.c`
(absolute instead of relative compare; no late guard; greedy split; arm before
publish; invalidate after publish) and each is caught.
