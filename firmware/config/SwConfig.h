/*
 * SwConfig.h
 * Part of the CoreLR wall-plotter firmware project.
 *
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file SwConfig.h
 * @brief Software tunables: queue/buffer sizes, timing constants, UI
 * tuning. Never a pin assignment or physical fact (see HwConfig.h
 * for that).
 */

#ifndef CORELR_SWCONFIG_H
#define CORELR_SWCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SwConfig_JobSource GCode job source: SD card vs. live serial
 * @ingroup CoreLR_SwConfig
 * Realizes: REQ-CoreLR-F-JobStorage, @req{REQ-CoreLR-F-SerialControl-0003}
 *
 * Confirmed 2026-09-07: on an ATmega328P's 2KB of RAM, supporting both
 * an SD-fed job and a live serial-fed job *at the same time* in one
 * firmware image was judged illusory -- each input path's queues/library
 * overhead are a meaningful fraction of the whole budget on their own
 * (see the RAM-overflow history on STEPPER_QUEUE_SIZE/
 * SDREADER_QUEUE_SIZE above/below). Selected here, at compile time,
 * rather than at runtime: a runtime switch would still need both code
 * paths (and their RAM) linked in simultaneously, defeating the point.
 * Change CORELR_JOB_SOURCE and rebuild to produce the other variant (in
 * the Arduino IDE edit the \#define below; a command-line build passes
 * -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL, see tools/analysis/build_size.sh).
 * The serial variant (Uart, SerialLink, docs/protocol/SerialProtocol.md) is
 * implemented since 2026-10-09; what only it uses is compiled out of
 * the SD build, and the other way round (@req{REQ-CoreLR-F-GCodeInterpreter-0013}).
 * @{
 */
#define CORELR_JOB_SOURCE_SD     1   /**< value of CORELR_JOB_SOURCE: jobs are read from an SD card */
#define CORELR_JOB_SOURCE_SERIAL 2   /**< value of CORELR_JOB_SOURCE: jobs arrive over the serial link (Candle, a grbl 1.1 subset) */
#ifndef CORELR_JOB_SOURCE
#define CORELR_JOB_SOURCE CORELR_JOB_SOURCE_SERIAL
#endif
/** @} */

/**
 * @defgroup SwConfig_Serial The serial link (serial variant only)
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-Uart-0001}.., @req{REQ-CoreLR-D-SerialLink-0001}.., docs/protocol/SerialProtocol.md
 * @{
 */
/** Baud rate. 115 200, 8N1: what every grbl sender defaults to (docs/protocol/SerialProtocol.md @protosec{4-1,4.1}). */
#define UART_BAUD            115200UL
/** Receive ring, bytes. **Must be 128**: UGS, bCNC and Candle hard-code that figure for their character counting, and CNCjs and gSender
 *  can only raise theirs from `Bf:` (docs/protocol/SerialProtocol.md @protosec{4-5,4.5}), so a smaller ring would be overrun by a correctly counting host.
 *  A power of two (the indices are masked). */
#define UART_RX_SIZE         128u
/**
 * CORELR_DEBUG_JOBSTATE (bench, 2026-10-10: a resume that leaves the job in Run with the machine frozen): 1 appends the internals of the job, the
 * generator and the step queue to every status report -- `|D:job,motion,stepper,queued,K,N,stored>` -- so that a probe (tools/bench/serial_probe.py) can say *what* is
 * stuck. Off by default: it needs the larger transmit ring below, 64 more bytes of RAM, and some flash. Not for use with a real sender (it would not
 * understand the extra field, though grbl senders ignore unknown fields).
 */
#ifndef CORELR_DEBUG_JOBSTATE
#define CORELR_DEBUG_JOBSTATE 0
#endif
/** Transmit ring, bytes. A power of two. The largest single reply is a status report of about 90 characters (with the WCO field, @req{REQ-CoreLR-D-SerialLink-0012}; 115 with
 *  the debug field), and a reply is only composed when it fits whole, so the ring is never written when full (@req{REQ-CoreLR-D-SerialLink-0003}). It was 64 before the WCO. */
#define UART_TX_SIZE         128u
/** Significant characters of a line, once spaces, comments and control characters are stripped: grbl's LINE_BUFFER_SIZE
 *  (docs/protocol/SerialProtocol.md @protosec{4-3,4.3}, @proto{FW-03}). A longer line is refused with error:11, never truncated. */
#define SERIALLINK_LINE_MAX  80u
/** @} */

/**
 * @defgroup SwConfig_Debug Diagnostics compiled in or out
 * @ingroup CoreLR_SwConfig
 *
 * CORELR_DEBUG_MISS_COUNT (@req{REQ-CoreLR-D-Stepper-0013}, -LcdJoystickUI-0016): the *step starvation* counter -- how often the
 * producer found the step timer idle while a move it was still feeding was in progress (the queue ran dry mid-move) --
 * and its display, `miss:N`, on row 1 of the Control jog screen (`jog    miss:N`) and of the Adjustment jog screen (`SW=ok   miss:N`).
 * It was the bring-up instrument for the "jog got slower" regression, which the v2 event generator cured; on the bench it
 * has read 0 ever since. 0 (the default) compiles all of it out: the counter, the detection in `Motion_Task()`, the
 * display, and the resets. The Control jog's row 1 then shows the cable lengths like the status screen, and the Adjustment
 * jog's a plain `SW=ok`. Set it to 1 to bring the instrument back (e.g. after retuning VMAX/ACCEL or the queue depth).
 * Measured saving: see @req{REQ-CoreLR-D-Stepper-0015}.
 * @{
 */
#ifndef CORELR_DEBUG_MISS_COUNT
#define CORELR_DEBUG_MISS_COUNT 0   /**< 1 compiles in the step starvation counter and its display; 0 (default) compiles them out */
#endif
/** @} */

/**
 * @defgroup SwConfig_GCode Optional GCode words
 * @ingroup CoreLR_SwConfig
 *
 * CORELR_GCODE_Z (@req{REQ-CoreLR-D-GCodeParser-0014}, docs/protocol/SerialProtocol.md P7): a `Z` word in a job line is a pen lift (some plotter generators use it instead of
 * M3/M5): absolute, Z <= 0 pen down and Z > 0 up; relative, the sign of the move. 1 compiles it in; 0 leaves `Z` an unsupported word (error 20), as it was.
 * Default: on in the serial variant (flash to spare, and Candle sends a Z with every jog), **off in the SD variant**, which has about 100 bytes of flash left and
 * would not link with it (measured: +156 bytes). The serial variant's `$J=` always accepts and ignores a Z, whatever this says.
 * @{
 */
#ifndef CORELR_GCODE_Z
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
#define CORELR_GCODE_Z 1   /**< 1: a Z word is a pen lift (the serial image's default) */
#else
#define CORELR_GCODE_Z 0   /**< 0: Z is an unsupported word, error 20 (the SD image's default) */
#endif
#endif
/** @} */

/**
 * @defgroup SwConfig_Menu Optional menu entries
 * @ingroup CoreLR_SwConfig
 *
 * CORELR_MENU_HOME (@req{REQ-CoreLR-D-LcdJoystickUI-0024}): the main menu's second entry, "Return home": lift the pen, then
 * move in a straight line to the drawing origin (X=0, Y=0), the SW key stopping it on the way. Offered like "Jog" -- only
 * once calibrated, and only with no job running, paused or in error. 1 (the default) compiles it in; 0 removes the entry,
 * the screen mode behind it and its label, for the day the flash is needed (measured cost: @req{REQ-CoreLR-D-LcdJoystickUI-0024}).
 * @{
 */
#ifndef CORELR_MENU_HOME
#define CORELR_MENU_HOME 1   /**< 1 (default): the main menu offers "Return home"; 0 removes the entry and its mode */
#endif
/** @} */

/**
 * @defgroup SwConfig_Frame Orientation of the drawing frame
 * @ingroup CoreLR_SwConfig
 *
 * CORELR_Y_UP (@req{REQ-CoreLR-D-Motion-0025}, decision @proto{D1} of docs/protocol/SerialProtocol.md):
 *  - 1 (the default): a GCode +Y moves the pen UP the wall -- the convention of common generators -- and `G2` is clockwise
 *    as seen on the wall. The internal Y (cable length, down) is `offset - Y`.
 *  - 0: the previous behaviour, +Y DOWN the wall (`offset + Y`), so a file is drawn mirrored top to bottom and `G2` is
 *    counter-clockwise on the wall.
 * It changes four things together and nothing else: the three conversions in `Motion.c` (`Motion_XYToLR`,
 * `Motion_GetPersistedState`, `Motion_Restore`) and the sign of the Control jog's Y, which follows so that the pen goes where the
 * stick points in either setting. A stored position stays valid across both (it is kept in the internal frame). Costs no flash.
 * With 0, the stream sent to `Stepper` is identical to the one before the flip, to the bit (tools/bench/stream_hash.sh).
 * @{
 */
#ifndef CORELR_Y_UP
#define CORELR_Y_UP 1   /**< 1 (default): +Y goes UP the wall and G2 is clockwise on the wall; 0: +Y goes down */
#endif
#if (CORELR_Y_UP != 0) && (CORELR_Y_UP != 1)
#error "CORELR_Y_UP (SwConfig.h) must be 0 or 1"
#endif
/** @} */

/**
 * @defgroup SwConfig_Math Width of the arithmetic: 64-bit values, or nothing wider than 32 bits
 * @ingroup CoreLR_SwConfig
 * (@req{REQ-CoreLR-D-Motion-0024})
 *
 * Two implementations of the same functions, kept side by side and chosen here:
 *
 *  - CORELR_MATH_64: the original. Lengths come from a 64-bit sum of squares and a 64-bit Newton root
 *    (`Motion_ISqrt`), the arc's angle ratio from a 64-bit division, the calibration geometry from 64-bit
 *    squares. Valid for any int32 coordinates. Costs ~340 bytes of libgcc's 64-bit helpers and 350 of root.
 *  - CORELR_MATH_48: no value wider than 32 bits ever exists. A length is the root of x^2 + y^2 < 2^45
 *    (|x|, |y| < 2^22 um = 4.19 m): the root of the coordinates shifted down so the sum fits 32 bits, then the
 *    EXACT residual from the LOW 32 bits alone (it is below 2^32, so arithmetic mod 2^32 knows it), one Newton
 *    step and a fix-up; the angle ratio by a 32-step restoring division; the calibration height by bisection.
 *    Bit-identical results inside its range (tested against exact references); coordinates beyond it are
 *    SATURATED, not wrapped, so a move to a silly place is wrong rather than a hang.
 *
 * Measured (Nano fqbn, whole image, 2026-10-08): _48 is 260 bytes smaller (30 848 -> 30 588: the only one
 * that fits the Nano's 30 720 with the arcs), the worst stack 8 bytes lower, and Motion_Hypot ~2.8 times
 * faster (5 038 -> 1 772 cycles on average, cycle simulator). Select with `#define CORELR_MATH
 * CORELR_MATH_64` here, or -DCORELR_MATH=... on the compiler line; tests/host/run_all.sh tests either.
 * @{
 */
#define CORELR_MATH_64 1   /**< value of CORELR_MATH: 64-bit lengths, valid for any int32 coordinates */
#define CORELR_MATH_48 2   /**< value of CORELR_MATH: nothing wider than 32 bits; coordinates saturate at CORELR_MATH_48_LIMIT_UM */
#ifndef CORELR_MATH
#define CORELR_MATH CORELR_MATH_48   /**< the arithmetic in use: CORELR_MATH_48 (default) or CORELR_MATH_64 */
#endif
/** The bound of CORELR_MATH_48: every coordinate component and every length below this (um, 2^22 = 4.19 m). */
#define CORELR_MATH_48_LIMIT_UM 0x3FFFFFL
/** @} */

/**
 * @brief Forbid the optimiser from inlining a function into its caller.
 *
 * Needed on every function `loop()` calls, and it is a RAM fix, not a
 * speed one.
 *
 * The Arduino core's `main()` calls `setup()` then `loop()`, and with
 * `-flto` -- which the IDE always passes -- GCC sees the whole program
 * at link time and inlines across translation units. Both ended up
 * folded into `main()`, together with everything they call. The
 * compiler then allocates **one** frame at `main()`'s entry, the union
 * of every inlined local, and that frame stays for the life of the
 * program.
 *
 * The cost is not the size of any one function but the fact that
 * mutually exclusive paths stop sharing stack: the SD browser and the
 * GCode parser never run at the same time, yet their buffers coexisted
 * permanently. Measured on this firmware, 2026-09-11:
 *
 *              main()'s own frame   worst-case stack   free RAM
 *   inlined            206                 428            316   OVERFLOW
 *   noinline            36                 320            316
 *
 * The board reset roughly once a second until this was applied, and
 * flash dropped by 728 bytes as well, inlining having duplicated code.
 *
 * Note that putting the functions in separate .c files does **not**
 * achieve this -- they already are, and were inlined anyway. Only the
 * attribute stops it.
 */
#define CORELR_NOINLINE __attribute__((noinline))

/**
 * @defgroup SwConfig_IsrDispatch Periodic timer and coarse time base
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-IsrDispatch-0003}, @req{REQ-CoreLR-D-SysTick-0002}
 * @{
 */
/** Timer2 interrupt rate. IsrDispatch derives the prescaler and compare
 * value from this by macro, and fails the build if the value is not
 * exactly reachable (@req{REQ-CoreLR-D-IsrDispatch-0003}). 1 kHz since
 * 2026-10-06: Timer2 no longer drives the motors (Timer1 does,
 * @req{REQ-CoreLR-D-Stepper-0011}) -- it is the coarse time base and the pen
 * servo's frame, and PenControl.c assumes exactly 1 ms per tick. */
#define ISRDISPATCH_FREQ_HZ 1000u

/** SysTick period in milliseconds. The ISR-tick divider is derived from
 * it against ISRDISPATCH_FREQ_HZ, with \#error guards
 * (@req{REQ-CoreLR-D-SysTick-0002}). At 4kHz the divider is exactly 40.
 * 10ms gives a 2.56s counter wrap and a 1.27s usable maximum period
 * (SYSTICK_TICKS_MAX) -- more than every period this project needs. */
#define SYSTICK_PERIOD_MS 10u
/** @} */

/**
 * @defgroup SwConfig_Stepper Stepper step queue
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-Stepper-0002}
 * @{
 */
/**
 * Sized 2026-09-07, revised same day: first cut to 32 (from 128) fixed
 * a 2570/2048-byte overflow, but a real-hardware stack-depth
 * investigation (avr-gcc -fstack-usage, prompted by a persisting "low
 * memory" warning at 344 bytes free) found the busiest call chain in
 * the firmware needed ~228 bytes on its own. 16 entries is still
 * several tens of ms of lookahead at any realistic hold-tick count.
 * @todo Empirically re-check against real hardware timing, same as
 * every other placeholder timing constant in this project.
 */
#define STEPPER_QUEUE_SIZE      16u
/** Entry timer+pen field layout (@req{REQ-CoreLR-D-Stepper-0001}):
 * bits 0-13 = hold ticks, bits 14-15 = a Stepper_PenAction_t value
 * (NONE=0/UP=1/DOWN=2) stored directly -- the *actual* target state,
 * not a toggle, so a skipped/reordered entry can never leave the
 * tracked state silently wrong (confirmed 2026-09-06, defensive
 * programming: the same absolute-over-relative principle already
 * applied to position, @req{REQ-CoreLR-D-Motion-0002}/@req{REQ-CoreLR-D-Motion-0004}). */
#define STEPPER_MAX_HOLD_COUNTS 0x3FFFu /**< 14-bit field: 16383 Timer1 counts = 8.19 ms (@req{REQ-CoreLR-D-Stepper-0010}) */
#define STEPPER_PEN_SHIFT      14u   /**< position of the pen action in an entry's hold-and-pen word */
#define STEPPER_PEN_MASK       (0x3u << STEPPER_PEN_SHIFT)   /**< mask of the pen action in that word */

/**
 * Timer1 and the hold unit (@req{REQ-CoreLR-D-Stepper-0010}..@req{REQ-CoreLR-D-Stepper-0012}, v2).
 * The motors are driven by a compare-match timer, not a periodic tick: an
 * entry's hold is a number of Timer1 counts, 0.5 us each at prescaler 8.
 * Stepper.c checks at compile time that F_CPU / prescaler is this.
 */
#define STEPPER_TIMER_PRESCALER 8u
#define STEPPER_COUNTS_PER_S    2000000UL   /**< Timer1 counts per second: 0.5 us each (F_CPU / STEPPER_TIMER_PRESCALER) */
/** Shortest hold the ISR can honour: the next compare must still lie in
 * the future when the ISR has finished. 32 us, against ~23 us worst case
 * (a queued Timer0 ISR, a queued TWI ISR, then our own ~8 us) -- an
 * estimate from reading the code, to be checked on the bench. */
#define STEPPER_DELAY_MIN_COUNTS   64u
/** Longest hold put in ONE entry; a longer request is split, never
 * leaving a remainder below the minimum (@req{REQ-CoreLR-D-Stepper-0012}). */
#define STEPPER_DELAY_MAX_COUNTS   16000u
/** First compare after the timer is armed from idle. */
#define STEPPER_START_DELAY_COUNTS 64u
/** If the ISR finds the next compare point already (almost) behind the
 * counter, it fires again after this many counts instead of waiting a whole
 * counter wrap (@req{REQ-CoreLR-D-Stepper-0011}). */
#define STEPPER_LATE_MARGIN_COUNTS 24u
#if STEPPER_DELAY_MAX_COUNTS < (2u * STEPPER_DELAY_MIN_COUNTS)
#error "STEPPER_DELAY_MAX_COUNTS must be at least twice STEPPER_DELAY_MIN_COUNTS, or a split cannot always leave a usable remainder"
#endif
#if STEPPER_DELAY_MAX_COUNTS > STEPPER_MAX_HOLD_COUNTS
#error "STEPPER_DELAY_MAX_COUNTS does not fit the 14-bit hold field"
#endif
/** @} */

/**
 * @defgroup SwConfig_SDReader SD reader: line queue and file browser
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-SDReader-0003}, @req{REQ-CoreLR-D-LcdJoystickUI-0008}
 * @{
 */
/**
 * Sized 2026-09-07, queue depth revised same day (see
 * STEPPER_QUEUE_SIZE's comment for why -- same stack-depth
 * investigation, this is one of the few real, unconditional-reduction
 * levers left): the V1 GCode dialect (G0/G1/G90/G91/M3/M5, F/S/N
 * tolerated-ignored) longest realistic line is something like
 * "N100 G1 X-1234.567 Y-1234.567 F1500 S255" (~41 chars incl. '\0') --
 * 48 leaves margin without the old 64. A 3-entry queue is still
 * lookahead for streaming (@req{REQ-CoreLR-D-SDReader-0001}): SD reads are
 * fast relative to how long one line takes to execute.
 * @todo Empirically re-check against real GCode files once some exist.
 */
#define SDREADER_LINE_SIZE  48u
/* 3 -> 2 on 2026-09-11, to fund MOTION_PLANNER_DEPTH: a queued line is
 * 48 bytes of raw text, a planner block 9 bytes of parsed move, so the
 * trade buys five times the look-ahead per byte. Two slots still cover
 * the latency of one SD block read while a line is being consumed;
 * going to 1 would remove that cushion entirely and has NOT been
 * measured on real hardware -- see REQ-CoreLR-D-SDReader's open items. */
#define SDREADER_QUEUE_SIZE 2u   /**< lines of raw text held ahead of the parser (SDREADER_LINE_SIZE bytes each) */

/** File browser: recognized GCode extensions, and the current-directory
 * path buffer (@req{REQ-CoreLR-D-LcdJoystickUI-0008}). Grouped here with the
 * rest of SDReader's tunables rather than under the UI section: they are
 * consumed by SDReader.cpp, which is what the naming now says. */
#define SDREADER_BROWSER_EXT1 ".gc"
/**
 * ".gco", not ".gcode" (changed 2026-09-10): only FAT 8.3 short names
 * are visible at the SdFile layer (@req{REQ-CoreLR-D-SDReader-0005}), and a
 * short name's extension field is 3 characters wide -- so ".gcode"
 * could never match anything, silently hiding the very files it was
 * meant to accept. A file saved as "PLOT.GCODE" appears under its
 * generated short name, typically "PLOT~1.GCO", which this matches.
 */
#define SDREADER_BROWSER_EXT2 ".gco"
#define SDREADER_BROWSER_EXT3 ".nc"   /**< third accepted extension (the browser also accepts .gc and .gco) */
/**
 * @brief Current SD browser directory path, including the '\0'.
 *
 * This is what bounds **how deep the browser can descend**
 * (@req{REQ-CoreLR-D-SDReader-0006}). Measured on the host at 32 bytes:
 * 3 levels of 8-character directory names
 * ("/DIRNAME8/DIRNAME8/DIRNAME8", 28 bytes used), or 2 levels of
 * maximal 12-character 8.3 names. Deeper than that,
 * SDReader_BrowserEnter() simply refuses to descend -- it never
 * truncates the path, because a truncated path re-walked on "go up"
 * would silently land in the wrong directory.
 *
 * Raising it costs static RAM 1:1 (s_currentPath is a static buffer,
 * not a stack one). It was 32 until 2026-10-13 (3 levels of 8-character names); the maintainer asked for 64 once the stack margin had grown to
 * 290 bytes: 6 levels of 8-character names below the root (a path of 54 characters), 7 counting a directory
 * named "TREE" first -- measured: see @req{REQ-CoreLR-D-SDReader-0006}.
 */
#define SDREADER_PATH_SIZE    64u
/** @} */

/**
 * @defgroup SwConfig_Pen Pen control timing
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-F-PenControl-0003}
 * @{
 */
#define PENCONTROL_SETTLE_TIME_MS 300u /**< fixed constant for V1, not user-configurable */
/** @} */

/**
 * @defgroup SwConfig_Ui Joystick / status-screen tuning
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-LcdJoystickUI-0002}, @req{REQ-CoreLR-D-LcdJoystickUI-0009}
 * @{
 */
#define LCDJOYSTICKUI_ADC_CENTER 512 /**< 10-bit ADC, 0-1023 */
#define LCDJOYSTICKUI_DEADZONE   32  /**< +/- around center; @todo retune per hardware */

/**
 * Discrete ("ternary": -1, 0, +1) use of the stick (@req{REQ-CoreLR-D-LcdJoystickUI-0022}): choosing the motor L / R, cycling a step size, moving in a menu, stepping a value.
 * The dead zone alone (above) made these far too sensitive -- 6 % of the stick selected a motor. The stick must now be pushed to ON (counts of deflection, as
 * LcdJoystickUI_ReadAxis() gives them, 0..LCDJOYSTICKUI_STICK_FULL_SCALE) to count as -1 or +1, and it stays there until it is back below OFF: a HYSTERESIS, so that a stick
 * resting near the threshold, or a noisy reading, cannot toggle a selection twice. DEADZONE < OFF <= ON <= STICK_FULL_SCALE. The analog uses (the two jogs' speed) are not affected.
 */
#define LCDJOYSTICKUI_TERNARY_ON   250
#define LCDJOYSTICKUI_TERNARY_OFF  170   /**< deflection below which a pushed stick is released again (the hysteresis; DEADZONE < OFF <= ON) */
#if !((LCDJOYSTICKUI_DEADZONE < LCDJOYSTICKUI_TERNARY_OFF) && (LCDJOYSTICKUI_TERNARY_OFF <= LCDJOYSTICKUI_TERNARY_ON))
#error "LCDJOYSTICKUI_TERNARY_*: need DEADZONE < OFF <= ON"
#endif

#define LCDJOYSTICKUI_TAG_RUNNING 'R'   /**< status tag: a job is running */
#define LCDJOYSTICKUI_TAG_IDLE    'I'   /**< status tag: idle */
#define LCDJOYSTICKUI_TAG_PAUSED  'P'   /**< status tag: the job is paused */
#define LCDJOYSTICKUI_TAG_ERROR   'E'   /**< status tag: the job stopped on an error */
#define LCDJOYSTICKUI_TAG_UNCALIB 'U'   /**< status tag: not calibrated */
#define LCDJOYSTICKUI_TAG_HOMING  'H'  /**< status screen while "Return home" is under way (CORELR_MENU_HOME) */
#define LCDJOYSTICKUI_TAG_JOG     'J'  /**< status screen while the Control jog is active (@req{REQ-CoreLR-D-LcdJoystickUI-0025}) */

/** Menu/jog/SD-browser input timing (@req{REQ-CoreLR-D-LcdJoystickUI-0003}). */
#define LCDJOYSTICKUI_LONGPRESS_MS  800u /**< SW held at least this long = "back/cancel" */
#define LCDJOYSTICKUI_REPEAT_MS     200u /**< min. interval between repeated menu/list/jog steps while deflected */

/** Minimum interval between two display re-renders
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0011}, @req{REQ-CoreLR-F-UserInterface-0009}).
 * 50ms = 20 Hz: imperceptibly "live" to a human, while a full 32-char
 * redraw costs ~11.5ms of blocking I2C against only ~20ms of motion
 * buffered in the step queue. 100ms would likely also be fine; 50 keeps
 * jog feedback feeling immediate. The refresh is self-limiting anyway --
 * -0011 also requires the previous frame to be fully transmitted before
 * re-rendering, so this is a floor on the interval, not a guarantee.
 * Used since 2026-09-10: -0011's frame buffer is implemented, so this
 * gates re-rendering, not transmission. */
#define LCDJOYSTICKUI_REFRESH_MS    50u

/** LcdI2c's bound on any one wait for the bus, in polls (@req{REQ-CoreLR-D-LcdI2c-0002}). A byte takes ~90 us
 * at 100 kHz and a poll 8 cycles = 0.5 us (counted in the disassembly), so this is ~1.2 ms: 13 byte-times, enough for anything the bus does,
 * and short enough that a stuck bus costs the main loop ~1 ms a call rather than for ever. */
#define LCDI2C_TIMEOUT_LOOPS 2400u

/**
 * "Control" (Cartesian) jog: the speed at full stick, um/ms = mm/s (@req{REQ-CoreLR-D-LcdJoystickUI-0021}).
 *
 * 21 mm/s, just under what the machine can do: Motion caps every block at MOTION_VMAX_V16 = 86/16 um/tick = 21.5 mm/s, the 400 steps/s the motors hold under load
 * (measured, docs/bringup/Bringup-Status.md; 600 unloaded). It was set to 40 on 2026-10-10 on my wrong statement that 40 mm/s was Motion's ceiling, which saturated the top
 * quarter of the stick and made the pen coast after the release; lowered to 21 the same day at the maintainer's decision. It was 20 (2026-09-11) and 5 before that.
 * If it ever has to follow Motion's cap, 21.5 mm/s is 21 um/ms here (whole units).
 * as a fraction of what is left up to the stick's full scale (LCDJOYSTICKUI_STICK_FULL_SCALE, HwConfig.h): 0 at the edge of the dead zone, Vmax at full scale.
 * The shape is the table of JogCurveTable.h (tools/generators/gen_jogcurve.py -e EXPONENT rewrites it: 1.5 for a gentler top end), and the speed is that of the NORM of the
 * stick, not of each axis.
 *
 * Mind the stop: from 21 mm/s the machine needs 0.11 s and 1.2 mm to stop (192 mm/s^2; it was 0.21 s and 4.2 mm from the 40 mm/s of the first setting), and the jog keeps up to 4 blocks of 30..60 ms in the planner.
 */
#define LCDJOYSTICKUI_JOG_MAX_SPEED 21L

/**
 * How many blocks the Control jog keeps in the planner (@req{REQ-CoreLR-D-LcdJoystickUI-0017}, @req{REQ-CoreLR-D-LcdJoystickUI-0028}). The jog tuned at depth 4 -- the planner then held at most 4 blocks of
 * 30..60 ms, so a released stick stopped within ~0.1 s -- and a deeper planner (the serial variant's 8, for streamed files) must not make it lag: the jog feeds
 * the planner only while fewer than this many blocks are waiting. It cannot exceed the planner's depth.
 */
#define LCDJOYSTICKUI_JOG_BLOCKS 4u

/**
 * Deflection time accumulated into one "Control" jog block, in ms
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0017}).
 *
 * The jog used to queue a block only while Motion was idle, which had
 * two consequences neither intended nor written down. The planner never
 * held more than one block, so the junction look-ahead had nothing to
 * look ahead at and every block decelerated to a stop -- the pulleys
 * visibly stopped and restarted throughout a move. And the elapsed time
 * was measured from the previous *idle check*, so each block was as
 * long as the previous one took to run:
 *
 *     L(n+1) = L(n) + 2*(v*t_ramp - d_ramp) = L(n) + 2.1mm
 *
 * which grows without bound until clamped by SYSTICK_TICKS_MAX at
 * 1.27s, i.e. 25mm. That is why the head kept moving for about half a
 * second after the stick was released.
 *
 * Blocks are now queued whenever the planner has room, and this sets
 * their size. It is a compromise with the ramp, not a free parameter:
 * at 20mm/s a block is 0.6mm, so MOTION_PLANNER_DEPTH of them is 2.4mm
 * of horizon against the 2.08mm a full accelerate-and-stop needs --
 * just enough to reach VMAX. Halving it would starve the look-ahead and
 * cap the jog below VMAX; doubling it would put 4.8mm of committed
 * motion behind a released stick.
 */
#define LCDJOYSTICKUI_JOG_SLICE_MS  30u
#define LCDJOYSTICKUI_JOG_SLICE_MAX_MS (2u * LCDJOYSTICKUI_JOG_SLICE_MS)   /**< the longest time one Control-jog slice may cover, ms: the elapsed time is capped there, so a slow pass cannot queue a long move */

/** "Adjustment" jog mode (@req{REQ-CoreLR-D-LcdJoystickUI-0005}): fixed
 * hold-tick count per manually-issued single step (not speed-profiled
 * like normal motion -- there is no path to plan, just one raw step
 * per repeat interval while the joystick is held deflected). */
/**
 * @defgroup SwConfig_AdjustJog "Adjustment" jog speed curve
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0014})
 *
 * Per-motor jogging used to issue one step per repeat interval, i.e.
 * one step every LCDJOYSTICKUI_REPEAT_MS = 200ms: **5 steps/s**. With
 * 2037.9 steps per revolution that is seven minutes per turn, which is
 * what made reaching the reference position unusable.
 *
 * The step rate now follows the Y deflection, so the same stick does
 * coarse travel and fine positioning:
 *
 *   steps/s
 *   MAX |                                       _/
 *       |                                   _,-/
 *       |                             _,,-/
 *       |                     _,,--/
 *   MIN |______,,,,----/
 *       +--------------------------------------- |deflection|
 *      dead zone                            full scale
 *
 * The curve is **squared**, not linear: near the dead zone a large
 * stick movement changes the rate very little, which is what fine
 * positioning needs, while full deflection still reaches MAX.
 * @{
 */

/** Rate just past the dead zone. Low enough to place a single step. */
#define LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S  10u

/**
 * Rate at full deflection. **Measured on the machine, 2026-09-11**, not
 * derived from the ~1kHz electrical ceiling: torque holds to about 180
 * full steps/s, falls off above that, and past ~360 the motor vibrates
 * without advancing.
 *
 * Comparison with the Walldraw reference needs the step modes lined up
 * first -- it drives the same motor in HALF steps, 4096 per revolution
 * against our 2037.9 (the true ratio of the 28BYJ-48, HwConfig.h). Its setMaxSpeed(800) is 400 of our steps, and its
 * source comment "above 256 it easily loses steps" is 128 of ours. This
 * machine holds 180, i.e. 40% better than that warning, so the figures
 * are reassuring rather than alarming once compared like for like.
 *
 * Raise it only against a measurement, and note which way the error
 * runs: a stalled motor does not turn slowly, it stops. Setting this
 * too high makes the machine *slower*.
 */
#define LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S 400u

/* The deflection treated as full scale is LCDJOYSTICKUI_STICK_FULL_SCALE (HwConfig.h), shared with the Control jog: there is one stick and
 * one mechanical stop. (It was a constant of this jog alone, 480, until 2026-10-10.) */

/** Steps queued per refill. Bounds how far the motor overshoots after
 *  the stick is released -- at most this many steps, whatever the
 *  speed -- while keeping the queue from running dry between passes. */
#define LCDJOYSTICKUI_ADJUST_BURST            2u

/**
 * How many step entries the adjustment jog keeps queued ahead of the
 * ISR (@req{REQ-CoreLR-D-LcdJoystickUI-0016}, 2026-09-11).
 *
 * This replaces a refill-only-when-empty rule that made the step
 * cadence hostage to the loop() period. The old rule left the ring
 * empty for nearly the whole of every step, so the only thing standing
 * between the motor and a frozen coil pattern was the reliquat of the
 * entry already executing -- one step period. At 400 steps/s that is
 * 2.5ms, and a single LCD character over I2C costs about 1ms, so any
 * pass that did a little more than usual overran it. The motor then
 * held one energised phase while the rotor was still short of its
 * detent, and rang against it.
 *
 * That failure is indistinguishable by ear from a motor driven past its
 * pull-in rate, which is what it was blamed on -- see
 * Stepper_StarvedCount(), added to tell the two apart by measurement
 * rather than by ear.
 *
 * Expressed as a **duration, not an entry count** (maintainer,
 * 2026-09-11). A fixed count is incoherent across this mode's 80:1
 * speed range: 8 entries are 0.8s of buffer at 10 steps/s and 13ms at
 * 600. A duration equalises both ends -- and it equalises the release
 * overshoot too, which becomes a constant ~30ms at every speed instead
 * of varying in the same 80:1 ratio.
 *
 * No running total is needed to honour it: every entry this mode emits
 * carries the same hold, so the depth follows from the rate alone
 * (see AdjustQueueDepth()).
 *
 * Note the ring bounds what this can actually deliver. 15 usable slots
 * are 25ms at 600 steps/s, so above ~500 steps/s the target is clamped
 * rather than met. Raising STEPPER_QUEUE_SIZE costs 3 bytes of RAM per
 * slot if that ever proves to matter.
 */
#define LCDJOYSTICKUI_ADJUST_BUFFER_MS       30u

/**
 * Rate the ramp starts from, and the acceleration it uses
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0016}).
 *
 * A 28BYJ-48's **pull-in** rate -- the fastest it can be commanded from
 * a standstill without stalling -- is far below its slew rate.
 * Measured on this machine with the queue kept full and no ramp, it
 * breaks up above ~400 steps/s; the Walldraw reference project puts
 * its own limit even lower, its source commenting that "above 256 it
 * easily loses steps", and it accelerates at 200 steps/s^2 rather than
 * commanding a rate outright.
 *
 * So the jog starts inside the pull-in region and accelerates. The
 * ramp only engages when it is needed: a target at or below START is
 * commanded directly, which is what keeps fine positioning immediate.
 * Deceleration is immediate too -- slowing down never loses sync.
 */
#define LCDJOYSTICKUI_ADJUST_START_STEPS_PER_S 120u
#define LCDJOYSTICKUI_ADJUST_ACCEL_STEPS_PER_S2 2000u   /**< acceleration of the Adjustment jog's ramp, steps/s^2 */

#if LCDJOYSTICKUI_ADJUST_START_STEPS_PER_S < LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S
#error "ADJUST_START must be at least ADJUST_MIN, or the ramp would start below the slowest commandable rate"
#endif

#if LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S >= 1000u
#error "LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S exceeds the ~1kHz per-motor limit"
#endif
/** @} */

/** Pen calibration wizard (@req{REQ-CoreLR-D-LcdJoystickUI-0007}). */
#define LCDJOYSTICKUI_PEN_ANGLE_STEP 1u /**< degrees per repeat interval while Y deflected */

/** "Calibrate machine" steps 1-2, numeric parameter entry
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0010}): coarse/fine step sizes the X axis
 * cycles through, Y axis then adjusts the live value by the selected
 * step. A tape measure reads to the mm, not finer, so mm-level steps
 * are enough -- no need for a digit-by-digit editor. */
#define LCDJOYSTICKUI_CAL_STEP_UM_0 1000L   /**< 1mm, fine */
#define LCDJOYSTICKUI_CAL_STEP_UM_1 10000L  /**< 10mm */
#define LCDJOYSTICKUI_CAL_STEP_UM_2 100000L /**< 100mm, coarse */
#define LCDJOYSTICKUI_CAL_STEP_COUNT 3u   /**< number of step sizes the X axis cycles through (LCDJOYSTICKUI_CAL_STEP_UM_0..2) */
/** Upper bound of the numbers the two entries above accept (um). Under CORELR_MATH_48 every length must stay below 2^22 um
 * (CORELR_MATH_48_LIMIT_UM), so 4 m; under CORELR_MATH_64 it is what the display's field holds, 9999.9 mm
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0023}). */
#if CORELR_MATH == CORELR_MATH_64
#define LCDJOYSTICKUI_CAL_MAX_UM 9999900L
#else
#define LCDJOYSTICKUI_CAL_MAX_UM 4000000L
#endif

/** Fixed columns of the position readout (0-based), shared by the status screen, the Control jog and the numeric entries
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0023}): the decimal point of the first (X, or L) and second (Y, or R) value, the "L" and "R"
 * labels of the second row, and where the entries put their step. */
#define LCDJOYSTICKUI_X_POINT_COLUMN 6u
#define LCDJOYSTICKUI_Y_POINT_COLUMN 14u   /**< column (0-based) of the decimal point of the second value (Y, or R) */
#define LCDJOYSTICKUI_L_LABEL_COLUMN 1u   /**< column (0-based) of the "L" label on the second row */
/** The pen's state on the status screen, column 0 of the second row (@req{REQ-CoreLR-D-LcdJoystickUI-0030}): physical, i.e. what the servo shows, not what was
 *  requested.
 *  LCDJOYSTICKUI_PEN_GLYPHS (@req{REQ-CoreLR-D-LcdJoystickUI-0032}, 2026-10-13): 1 = two pictograms of 5 x 8 dots (a pen above a line of paper, down and up) are loaded into the
 *  display's CGRAM at start-up and shown as the codes 1 and 2; 0 = the plain ASCII `v` and `^`, which need nothing from the display and cost no flash. The display of the
 *  maintainer's machine has the A00 ROM (no arrows): the pictograms are drawn, not found. */
#ifndef LCDJOYSTICKUI_PEN_GLYPHS
#define LCDJOYSTICKUI_PEN_GLYPHS 1
#endif
#if LCDJOYSTICKUI_PEN_GLYPHS
#define LCDJOYSTICKUI_PEN_DOWN_CHAR  '\x01'   /**< the pen-down character: LCDGLYPHS_PEN_DOWN, LcdGlyphs.h (the module cannot be included here: this file is included by it) */
#define LCDJOYSTICKUI_PEN_UP_CHAR    '\x02'   /**< the pen-up character: LCDGLYPHS_PEN_UP */
#else
#define LCDJOYSTICKUI_PEN_DOWN_CHAR  'v'   /**< the pen-down character: plain ASCII */
#define LCDJOYSTICKUI_PEN_UP_CHAR    '^'   /**< the pen-up character: plain ASCII */
#endif
#define LCDJOYSTICKUI_R_LABEL_COLUMN 9u   /**< column (0-based) of the "R" label on the second row */
#define LCDJOYSTICKUI_CAL_STEP_COLUMN 9u   /**< column (0-based) where the numeric entries show their step size */
/** @} */

/**
 * @defgroup SwConfig_Motion Internal resolution and motion profile
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-Motion-0001}, @req{REQ-CoreLR-D-Motion-0006}, @req{REQ-CoreLR-D-Motion-0009}
 * @{
 */
#define CORELR_UM_PER_MM 1000L /**< 1 internal position unit = 1 micrometre */

/** Trapezoidal velocity profile constants (@req{REQ-CoreLR-D-Motion-0009}).
 *
 * Velocity is in um/tick: 1 um/tick = 4 mm/s at 4kHz.
 * VMAX is capped by the per-motor 1kHz limit
 * (@req{REQ-CoreLR-F-Actuation-0001}): one motor step is 53.96um, so 1kHz
 * corresponds to a cable speed of 54.0 mm/s = 13.4 um/tick, hence
 * VMAX <= 13. A single *Cartesian* cap suffices for both motors,
 * because dL/dt = v*cos(theta) means cable speed never exceeds
 * Cartesian speed (@req{REQ-CoreLR-D-Motion-0011}). (That was the electrical argument for 10 = 40 mm/s;
 * the mechanical limit now binds at 21.5 mm/s -- see the correction below.)
 *
 * Acceleration is in nm/tick^2, NOT um/tick^2 (rescaled 2026-09-10).
 * The coarser unit could not express a realistic value at all: at 4kHz
 * 1 um/tick^2 = 16 m/s^2 (1.63 g) and 1 was the smallest non-zero
 * integer, while a plausible 100-500 mm/s^2 for a 28BYJ-48 on a cable
 * spool needs 0.006-0.031 um/tick^2 -- i.e. zero. In nm/tick^2 the same
 * range is 6-31, comfortably representable:
 *   6 nm/tick^2 ~= 100 mm/s^2   (ramp to VMAX 2.3 mm; 8.0 mm at the old VMAX of 40 mm/s)
 *  12 nm/tick^2 ~= 200 mm/s^2   (ramp 1.2 mm; 4.0 mm at 40 mm/s)
 *  31 nm/tick^2 ~= 500 mm/s^2   (ramp 0.5 mm; 1.6 mm at 40 mm/s)
 * @todo All three still need empirical tuning on real hardware. Note
 * that a realistic value makes the ramp *longer* than a typical plotter
 * GCode segment, which will expose V1's per-segment deceleration
 * (REQ-CoreLR-D-Motion's open items) -- that needs junction look-ahead,
 * not a different constant here. */
/**
 * **Corrected 2026-09-11 against a measurement: 10 -> 2.**
 *
 * The old value came from the *electrical* ceiling. One step is 53.7um,
 * so ~1kHz per motor is 53.7mm/s, and 10um/tick = 40mm/s = 745 steps/s
 * looked like a comfortable 25% margin. The motor was never asked. It
 * holds torque to about 180 steps/s and vibrates without advancing past
 * ~360, so 745 would have stalled the plot exactly as it stalled the
 * jog. The ceiling that binds is mechanical, not electrical.
 *
 * **Unit changed 2026-09-11: sixteenths of a micrometre per tick**
 * (suffix `_V16`), and with it every other velocity in this module --
 * VMIN, the junction jerk, and the planner's stored entry speeds.
 *
 * The old unit was a plain integer number of um/tick, and it could not
 * express this machine's actual limit. 400 steps/s is 21.5mm/s, which
 * at the 4kHz tick is 5.375um/tick: the only expressible neighbours
 * were 5 (20mm/s) and 6 (24mm/s = 447 steps/s, past the stall point).
 * 5 was chosen, and the maintainer rightly asked why the cap sat
 * below the measured limit -- the answer was granularity, not judgement.
 *
 * At 1/16 the same figure is 86 exactly. One unit is 0.25mm/s, and the
 * range still fits a uint8_t (255 = 63.8mm/s), so the planner's
 * per-block entry speeds cost no extra RAM.
 *
 * This is the same defect already fixed once on the acceleration
 * (um/tick^2 -> nm/tick^2, whose smallest non-zero integer meant
 * 16 m/s^2). Velocities had it too.
 *
 * Every v^2 term in Motion.c therefore carries a factor of 256
 * (= 16^2) against the distance terms, which stay in micrometres.
 *
 * Previously revised 2026-09-11 (2 -> 5) after the software defect behind
 * the earlier measurement was found and fixed: the queue was starving
 * the ISR, and a frozen coil pattern had been read as the motor losing
 * torque (@req{REQ-CoreLR-D-Motion-0014}). With that fixed the motor reaches
 * 600 steps/s unloaded and holds 400 under load, measured.
 *
 * 5um/tick = 20mm/s = 372 steps/s, ~7% under the loaded limit. The cap
 * of 13 from the electrical argument is now irrelevant -- mechanics
 * bind first, at 5.37um/tick.
 *
 * One welcome side effect. The deceleration ramp is v^2/(2a), so
 * dropping VMAX from 40 mm/s to 21.5 shortens it from 4.2mm to **1.2mm**
 * (192 mm/s^2; it was 0.17mm only at the intermediate VMAX of 2 um/tick = 8 mm/s), and the
 * look-ahead horizon MOTION_PLANNER_DEPTH must cover collapses with it:
 * 6 blocks sustain VMAX on 0.2mm segments, 3 on 0.5mm, 2 on 1mm, where 21 would have
 * been needed at 40 mm/s. Measured with the machine's back-pressure (tests/host/test_chain.c): depth 8 holds 20.7 mm/s down to 0.2mm,
 * (depth 4, the SD variant's until 2026-10-13: from 0.5mm, 15.4 mm/s at 0.2mm.)
 */
#define MOTION_VMAX_V16          86u

/**
 * How deep Motion_Task() keeps the step queue, and how many steps it
 * may emit in one call (@req{REQ-CoreLR-D-Motion-0014}, 2026-09-11).
 *
 * Plotting takes the opposite policy to the manual jog above: fill the
 * queue as far as it will go. There is no stick to release, so queued
 * work is never work we might regret -- a halt ramps down through the
 * planner, and the queued entries *are* the ramp.
 *
 * The defect this fixes is the one found in the jog, at a larger scale:
 * Motion_Task() emitted at most ONE entry per call, so the step rate
 * was capped by how often loop() ran, and any slow pass starved the
 * ISR. Feeding to a depth decouples the two.
 *
 * MOTION_STEPS_PER_TASK_MAX bounds the worst-case time spent inside one
 * call so the UI keeps its own cadence; 16 iterations per pass at loop()'s
 * several hundred hertz is far more throughput than VMAX can ask for.
 */
#define MOTION_QUEUE_TARGET       12u
#define MOTION_STEPS_PER_TASK_MAX 16u   /**< most iterations (step events, segment boundaries, flushes) of one Motion_Task() call */

/**
 * The step-event generator (@req{REQ-CoreLR-D-Motion-0018}..@req{REQ-CoreLR-D-Motion-0020}, v2).
 *
 * Cable lengths are carried in 1/MOTION_SUBSTEP of a motor step, so that the
 * instant a motor's ideal position crosses a decision threshold can be placed
 * to a fraction of a step without floating point.
 */
#define MOTION_SUBSTEP            256

/**
 * Tie guard, in sub-steps, each side of the +-1/2 step decision point.
 *
 * Measured on the prototype (tools/prototypes/evproto, 7.7 M entries): with 0, a
 * reversal landing exactly on a threshold steps up and straight back down,
 * two steps of one motor 32 us apart -- after a step up to c+1, the down
 * threshold of c+1 is the SAME number as the up threshold of c. Any value from 8
 * removes it (fastest interval then 2.45 ms); 16 is 6 % of a step and leaves
 * twice the margin over the waypoints' rounding noise. It costs an end-of-move
 * deviation of up to 0.56 step (30 um) instead of 0.50. v1's one-step band
 * (128) bought nothing here and cost a whole step of accuracy.
 */
#define MOTION_TIE_GUARD_SUBSTEPS 16

/**
 * Waypoint spacing, in motor steps of Cartesian travel (~214 um at 4).
 * Measured on the prototype: the step-interval jitter does NOT fall with
 * finer waypoints, it rises (1: 1.2 %, 2: 0.7 %, 4: 0.3 %, 8: 0.1 %), since each
 * waypoint adds its own rounding noise; the cost of coarser ones is the speed
 * profile, which is evaluated per segment (16 steps: a 20 mm rest-to-rest move
 * runs 3 % short). 8 halves the CPU load (22.6 % -> 15.7 % at VMAX).
 */
#define MOTION_WAYPOINT_STEPS     4u

/**
 * Acceleration, in mm/s^2 (192 = the 12 nm/tick^2 of v1, stated physically now
 * that the profile no longer depends on a tick). v^2, in (0.25 mm/s)^2, gains
 * 2*a*d, i.e. a*32/1000 per um of distance.
 */
#define MOTION_ACCEL_MM_S2        192u

/**
 * Arcs (G2/G3, @req{REQ-CoreLR-D-Motion-0022}).
 *
 * Chord tolerance, in um: the most a segment's chord may fall short of the
 * circle (the sagitta). It sets the angular step, dtheta = sqrt(8 * tol / r),
 * i.e. segments of sqrt(8 * tol * r) -- 126 um at a 1 mm radius, so only radii
 * under ~2.9 mm are cut finer than the ordinary 214 um waypoint spacing.
 * 2 um is grbl's default ARC_TOLERANCE (0.002 mm), and a tenth of a motor step.
 */
#define MOTION_ARC_TOLERANCE_UM   2UL

/** The travel zone (@req{REQ-CoreLR-F-Motion-0008}, @req{REQ-CoreLR-D-Motion-0028}). The pen hangs from two cables that can only pull: beyond the vertical of a
 *  motor nothing holds it on the outer side, and at the level of the motors the cables are horizontal and cannot carry its weight (the tension of
 *  each grows without bound as it nears that line: tools/generators/gen_tension_chart.py draws it for a given geometry). So no position is commanded
 *  outside a zone that keeps these margins, in um, from the vertical of each motor and below the line joining them. A target outside is refused
 *  (error:33), the joystick jog stops at the edge, and a calibration whose reference position is outside is refused. */
#ifndef MOTION_MARGIN_SIDE_UM      /* #ifndef: a test may widen the zone with a negative margin (tests/host/run_test.sh, test_arc) */
#define MOTION_MARGIN_SIDE_UM     20000L
#endif
/** The same, from the line joining the two motors, upwards. Larger than the side margin: near that line the tension rises fast
 *  (@req{REQ-CoreLR-D-Motion-0028}: 12.5 times the weight of the pen per cable at 20 mm below it, mid-span, for a 1 m span; 5 times at 50 mm). */
#ifndef MOTION_MARGIN_TOP_UM
#define MOTION_MARGIN_TOP_UM      50000L
#endif

/** The machine's reach, as a power of two so that it is tested with a shift (@req{REQ-CoreLR-F-GCodeInterpreter-0011}):
 *  a drawing coordinate of a GCode target must be below 2^21 um (2.097 m) in magnitude, or the line is invalid.
 *  Behind it: with the drawing-origin offset the internal coordinates stay under 2^22 um, the bound of
 *  CORELR_MATH_48's arithmetic (@req{REQ-CoreLR-D-Motion-0024}). */
#define MOTION_REACH_SHIFT        21
/** The same bound for an arc: no radius, and no vector from the centre to an end point, may reach it. It keeps every
 *  square, every arc length and the segment count inside the integer widths used, and Trig inside its 2^22 range. */
#define MOTION_ARC_REACH_SHIFT    MOTION_REACH_SHIFT

/** Radius consistency (@req{REQ-CoreLR-F-GCodeInterpreter-0009}, grbl's rule): a
 *  difference between the start and end radii of up to this is always accepted
 *  (um) ... */
#define MOTION_ARC_RADIUS_FLOOR_UM 5UL
/** ... otherwise it is refused above this absolute difference (um) ... */
#define MOTION_ARC_RADIUS_CAP_UM  500UL
/** ... or above 1/this of the radius (1000 = 0.1 %). */
#define MOTION_ARC_RADIUS_RATIO   1000UL

#define MOTION_VMIN_V16          16u  /**< 4 mm/s (units of 0.25 mm/s); floor avoids a division by zero and huge segment durations at v=0 */

/**
 * @brief Junction look-ahead depth, in planned moves
 * (@req{REQ-CoreLR-D-Motion-0013}).
 *
 * Without look-ahead every segment starts and ends at VMIN, so a
 * drawing made of short segments -- which is what an SVG-derived GCode
 * file is -- never gets near VMAX. Measured, current profile:
 *
 *   segment length     0.2mm   0.5mm   1mm    8.4mm   50mm
 *   no look-ahead      5.0     4.7     6.3    19.2    35.2  mm/s   (September 2026, VMAX then 40 mm/s)
 *   depth 7            23      36      40     40      40    mm/s   (the same)
 *
 * Now (VMAX 21.5 mm/s, tests/host/test_chain.c, with the machine's back-pressure): depth 8 (both variants) 20.7 mm/s from 0.2mm up;
 * (depth 4, the SD variant's until 2026-10-13: 15.4 at 0.2mm, 20.7 from 0.5mm.)
 *
 * Sustaining VMAX needs an horizon long enough to decelerate to rest
 * inside it, i.e. VMAX^2/(2a) = 1.2mm at 21.5 mm/s (4.2mm at the old 40): 6 blocks at 0.2mm, 3 at 0.5mm,
 * 2 at 1mm. 8 is what the RAM budget allows, which is enough from 0.2mm upwards.
 *
 * Why 8 and not 7 (maintainer's question, 2026-09-11): a power of
 * two lets the ring indices wrap with a bitmask instead of a compare
 * and subtract, worth 38 bytes of flash. That alone would not pay for
 * the extra block's 9 bytes of RAM, but paired with taking one slot
 * back from SDREADER_QUEUE_SIZE it improves every figure at once --
 * flash -58, static RAM -39, stack margin 12 -> 51 -- while adding a
 * block of look-ahead. Measured, all four combinations.
 *
 * Raising it further stays the single most effective speed lever, and
 * the same source applies: SDREADER_QUEUE_SIZE spends 48 bytes a slot
 * on *raw text* where a block holds an already-parsed move.
 */
/* 8 -> 4 on 2026-09-11, to buy back the stack margin the inlining fix
 * left 4 bytes short. Costs speed on short segments: VMAX now holds
 * from ~2mm rather than ~1mm, and a chain of 0.2mm segments runs at
 * ~18mm/s instead of 25. Reversible in one constant once RAM is found
 * elsewhere -- FindEntryAtRank still carries three name buffers in an
 * 81-byte frame, two of which are reducible. */
/* Per variant (2026-10-10, the maintainer's decision: "we have the RAM"): the stack margin that forced 8 -> 4 is the SD browser's, and the serial variant has no
 * SD stack -- 1271 bytes were free with 4. 8 blocks of look-ahead (+84 bytes of RAM) is what a streaming sender wants: short segments (an SVG-derived file,
 * arcs cut into chords) hold their speed from ~1 mm instead of ~2 mm, and `Bf:` / `$I` report it honestly. The Control jog does NOT use the extra
 * depth -- see LCDJOYSTICKUI_JOG_BLOCKS. */
/* SD too since 2026-10-13 (the maintainer's question): the SD stack, 334 bytes at the worst (the browser), left 374 bytes of margin at depth 4 and leaves 290 at 8 (+84 bytes of RAM,
 * +4 bytes of flash, the stack unchanged); depth 4 held the speed only from 0.5mm segments up (15.4 mm/s at 0.2mm), depth 8 holds it from 0.2mm. */
#define MOTION_PLANNER_DEPTH      8u
/* Must be a power of two: Replan()'s ring indices wrap with a bitmask
 * rather than "%", which on this target would be a real 16-bit
 * division performed several times per block. */
#if LCDJOYSTICKUI_JOG_BLOCKS > MOTION_PLANNER_DEPTH
#error "LCDJOYSTICKUI_JOG_BLOCKS cannot exceed MOTION_PLANNER_DEPTH"
#endif
#if (MOTION_PLANNER_DEPTH & (MOTION_PLANNER_DEPTH - 1u)) != 0u
#error "MOTION_PLANNER_DEPTH must be a power of two -- Motion.c's ring indices wrap with a bitmask"
#endif

/**
 * @brief Cornering limit: the largest instantaneous change of an axis
 * velocity component tolerated at a junction, in um/tick
 * (@req{REQ-CoreLR-D-Motion-0013}).
 *
 * An axis-jerk model, chosen over grbl's junction-deviation formula
 * because it needs no trigonometry and has no singularity at the
 * collinear case -- two collinear moves give a zero change, hence no
 * limit at all, which is exactly the defect this replaces.
 *
 * What it yields now (VMAX 21.5 mm/s, jerk 8 mm/s, VMIN 4 mm/s):
 *
 *   junction angle    0 deg    10 deg   45 deg   90 deg   180 deg
 *   allowed speed     21.5     21.5     16       8        4 (VMIN)     mm/s
 *
 * 8 mm/s through a right angle. Placeholder pending
 * hardware tuning, like VMAX and the acceleration: too high and the
 * gondola overshoots the corner, too low and dense polylines crawl.
 */
#define MOTION_JUNCTION_JERK_V16 32u  /**< 8 mm/s, in units of 0.25 mm/s */
/** @} */

/**
 * @defgroup SwConfig_Calibration Calibration parameter persistence
 * @ingroup CoreLR_SwConfig
 * Realizes: @req{REQ-CoreLR-D-Calibration-0003}
 * @{
 */
#define CALIBRATION_EEPROM_ADDRESS 0u /**< internal AVR EEPROM, byte offset */
/**
 * The persisted position (@req{REQ-CoreLR-D-Calibration-0006}, @req{REQ-CoreLR-D-Calibration-0010}) is a ring of slots of its own, from this address, after the bytes reserved for the calibration
 * image -- so that storing a position never rewrites the installation parameters. Calibration.c checks at compile time that the image fits and that the ring fits
 * the EEPROM. Moved from 32 to 64 on 2026-10-10 with the ring: the old single record, left where it was, looks like nothing to the new code (a first start
 * after the change is "uncalibrated": one recalibration).
 */
#define CALIBRATION_POSITION_EEPROM_ADDRESS 64u
/**
 * The number of slots of the position ring (@req{REQ-CoreLR-D-Calibration-0010}, layout in @docref{EepromLayout,docs/protocol/EepromLayout.md}): each EEPROM cell is rewritten at most once every
 * SLOTS/2 motion cycles, so a cell's ~100 000 write cycles last 100 000 * SLOTS / 2 motion cycles. A power of two, 2..128 (the 8-bit sequence number
 * tells the newest of at most 128). The default is the largest the EEPROM holds after the first 64 bytes (the calibration image and a reserve): 64 slots of 15 bytes
 * with the 24-bit fields of CORELR_MATH_48 (960 bytes, to the end of the EEPROM: 3.2 million cycles), 32 of 19 bytes with the 32-bit fields of CORELR_MATH_64.
 */
#ifndef CALIBRATION_POSITION_SLOTS
#if CORELR_MATH == CORELR_MATH_64
#define CALIBRATION_POSITION_SLOTS 32u
#else
#define CALIBRATION_POSITION_SLOTS 64u
#endif
#endif
/**
 * 1: the position is kept in that ring; 0: in one record of 18 bytes, rewritten in place (what it always was), whose marker byte is written twice per motion cycle and
 * wears out first (~50 000 cycles). The ring costs about 590 bytes of flash. **On in both variants** (maintainer, 2026-10-10): the SD build holds it thanks to -mcall-prologues
 * (cmake/Firmware.cmake, option CORELR_CALL_PROLOGUES), which the build must apply -- without it the SD variant no longer fits the Nano. 0 keeps the single record (the code under \#else).
 */
#ifndef CALIBRATION_POSITION_RING
#define CALIBRATION_POSITION_RING 1
#endif
/** How long the machine must stand still before its position is stored
 * (@req{REQ-CoreLR-D-Calibration-0008}; maintainer, 2026-10-06). */
#define CALIBRATION_POSITION_STORE_DELAY_MS 2000u
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CORELR_SWCONFIG_H */
