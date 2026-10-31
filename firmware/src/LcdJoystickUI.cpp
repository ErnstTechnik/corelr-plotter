/*
 * LcdJoystickUI.cpp
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
 * @file LcdJoystickUI.cpp
 * @brief See LcdJoystickUI.h.
 *
 * Menu state machine (@req{REQ-CoreLR-D-LcdJoystickUI-0003}..@req{REQ-CoreLR-D-LcdJoystickUI-0008}): SW
 * short/long press is detected once per press-release cycle (timed via
 * SysTick), then dispatched to the current UiMode_t's input
 * handler; a second, separate switch always redraws whatever mode is
 * current *after* any transition the handler made, so every mode has
 * exactly one redraw path regardless of whether this tick changed mode.
 * Fixed menu-item labels are kept in flash (PROGMEM), not RAM -- see
 * the kMenuLabels table below.
 */

#include <Arduino.h> /* defines A1/A2/A3, pinMode/analogRead/INPUT_PULLUP -- not auto-included here like it is for the .ino (confirmed 2026-09-07, first real compile) */
#include <avr/pgmspace.h>
#include <string.h>
#include "LcdJoystickUI.h"
#include "LcdI2c.h" /* our own driver for the display -- deliberately NOT Wire + LiquidCrystal_PCF8574, see that header */
#include "LcdGlyphs.h"
#include "HwConfig.h"
#include "SwConfig.h"
#include "JogCurve.h"
#include "Motion.h"
#include "Calibration.h"
#include "PenControl.h"
#include "Stepper.h"

/* The SD file browser, the "Load file" menu item and everything they pull in exist only in the SD variant: in the serial one
 * there is no card, and jobs come over the link (FW-08 of docs/protocol/SerialProtocol.md, REQ-CoreLR-D-LcdJoystickUI-0019). */
/* Two plain values rather than one parenthesised comparison: Doxygen 1.9.1 (the CI's) does not expand the nested macro in the `#if` below. */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
#define LCDJOYSTICKUI_HAS_SD 1   /**< 1: this is the SD image (browser, "Load file", card errors) */
#else
#define LCDJOYSTICKUI_HAS_SD 0   /**< 0: this is the serial image (jobs come over the link, no card) */
#endif
#if LCDJOYSTICKUI_HAS_SD
#include "SDReader.h"
#endif
#include "SysTick.h"
#include "TextFmt.h" /* our own bounded text composition -- deliberately NOT snprintf, see that header */

/** @brief Which top-level UI mode is active. */
typedef enum
{
   LCDJOYSTICKUI_MODE_STATUS = 0,
   LCDJOYSTICKUI_MODE_MENU,
   LCDJOYSTICKUI_MODE_JOG_CONTROL,              /**< @req{REQ-CoreLR-D-LcdJoystickUI-0004} */
   LCDJOYSTICKUI_MODE_CAL_PARAMS, /**< @req{REQ-CoreLR-D-LcdJoystickUI-0010}, steps 1-2 of "Calibrate machine" */
   LCDJOYSTICKUI_MODE_JOG_ADJUST,           /**< @req{REQ-CoreLR-D-LcdJoystickUI-0005}, step 3 of "Calibrate machine" */
   LCDJOYSTICKUI_MODE_CAL_PEN,            /**< @req{REQ-CoreLR-D-LcdJoystickUI-0007} */
   LCDJOYSTICKUI_MODE_SD_BROWSER,               /**< @req{REQ-CoreLR-D-LcdJoystickUI-0008} */
   LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM,           /**< @req{REQ-CoreLR-D-LcdJoystickUI-0031}: "Set origin" / "Origin = calib." asking for its confirmation */
   LCDJOYSTICKUI_MODE_HOMING                    /**< @req{REQ-CoreLR-D-LcdJoystickUI-0024} (present, unused, when CORELR_MENU_HOME is 0) */
} UiMode_t;

/** Main menu items (@req{REQ-CoreLR-F-UserInterface-0003}), in display order. */
typedef enum
{
   MENU_JOG = 0,             /**< hidden/disabled while uncalibrated, @req{REQ-CoreLR-D-LcdJoystickUI-0006} */
#if CORELR_MENU_HOME
   MENU_HOME,                /**< same availability as MENU_JOG, @req{REQ-CoreLR-D-LcdJoystickUI-0024} */
#endif
   MENU_SET_ORIGIN,          /**< the pen's position becomes (0, 0): same availability as the jog, @req{REQ-CoreLR-D-LcdJoystickUI-0031} */
   MENU_ORIGIN_CAL,          /**< the origin goes back to the calibration reference point */
   MENU_CAL_MACHINE,
   MENU_CAL_PEN,
#if LCDJOYSTICKUI_HAS_SD
   MENU_LOAD_FILE,
#endif
   MENU_HALT,                /**< job running: pause it (@req{REQ-CoreLR-D-LcdJoystickUI-0012}) */
   MENU_RESUME,              /**< job paused: carry on */
   MENU_CANCEL,              /**< job running or paused: abandon it */
   MENU_BACK,                /**< always offered, always last: back to the status screen (@req{REQ-CoreLR-D-LcdJoystickUI-0026}) */
   MENU_ITEM_COUNT
} MenuItem_t;

/** Labels kept in flash, not RAM (PROGMEM) -- read back into a small
 * stack buffer only at the moment they're printed. Kept under 16
 * characters so nothing is lost to the LCD's column count. */
static const char kMenuLabelJog[]         PROGMEM = "Jog";
#if CORELR_MENU_HOME
static const char kMenuLabelHome[]        PROGMEM = "Return home";   /**< menu label of MENU_HOME */
#endif
static const char kMenuLabelSetOrigin[]   PROGMEM = "Set origin";   /**< menu label of MENU_SET_ORIGIN */
static const char kMenuLabelOriginCal[]   PROGMEM = "Origin = calib.";   /**< menu label of MENU_ORIGIN_CAL */
static const char kMenuLabelCalMachine[]  PROGMEM = "Calibrate mach.";   /**< menu label of MENU_CAL_MACHINE */
static const char kMenuLabelCalPen[]      PROGMEM = "Calibrate pen";   /**< menu label of MENU_CAL_PEN */
#if LCDJOYSTICKUI_HAS_SD
static const char kMenuLabelLoadFile[]    PROGMEM = "Load file";   /**< menu label of MENU_LOAD_FILE (SD image) */
#endif
static const char kMenuLabelHalt[]        PROGMEM = "Pause job";   /**< menu label of MENU_HALT */
static const char kMenuLabelResume[]      PROGMEM = "Resume job";   /**< menu label of MENU_RESUME */
static const char kMenuLabelCancel[]      PROGMEM = "Cancel job";   /**< menu label of MENU_CANCEL */
static const char kMenuLabelBack[]        PROGMEM = { LCDJOYSTICKUI_BACK_ARROW, ' ', 'B', 'a', 'c', 'k', '\0' };   /**< menu label of MENU_BACK: the arrow glyph, then "Back" */
/** The labels in the order of MenuItem_t (flash): RedrawMainMenu() reads the selected item's pointer with pgm_read_word(), then the text with strncpy_P(). */
static const char * const kMenuLabels[MENU_ITEM_COUNT] PROGMEM =
{
   kMenuLabelJog,
#if CORELR_MENU_HOME
   kMenuLabelHome,
#endif
   kMenuLabelSetOrigin, kMenuLabelOriginCal,
   kMenuLabelCalMachine, kMenuLabelCalPen,
#if LCDJOYSTICKUI_HAS_SD
   kMenuLabelLoadFile,
#endif
   kMenuLabelHalt, kMenuLabelResume, kMenuLabelCancel, kMenuLabelBack
};

/** "Calibrate machine" steps 1-2 numeric entry (@req{REQ-CoreLR-D-LcdJoystickUI-0010}):
 * coarse/fine step-size table (values) and their on-screen labels,
 * both in flash -- same PROGMEM pattern as kMenuLabels above. */
static const int32_t kCalStepSizesUm[LCDJOYSTICKUI_CAL_STEP_COUNT] PROGMEM =
{
   LCDJOYSTICKUI_CAL_STEP_UM_0, LCDJOYSTICKUI_CAL_STEP_UM_1, LCDJOYSTICKUI_CAL_STEP_UM_2
};
static const char kCalStepLabel0[] PROGMEM = "1mm";   /**< label of the finest step size (kCalStepSizesUm[0]) */
static const char kCalStepLabel1[] PROGMEM = "10mm";   /**< label of the middle step size (kCalStepSizesUm[1]) */
static const char kCalStepLabel2[] PROGMEM = "100mm";   /**< label of the coarsest step size (kCalStepSizesUm[2]) */
/** The labels of kCalStepSizesUm, same order (flash): shown on the "Calibrate machine" entry screen for the current step size. */
static const char * const kCalStepLabels[LCDJOYSTICKUI_CAL_STEP_COUNT] PROGMEM =
{
   kCalStepLabel0, kCalStepLabel1, kCalStepLabel2
};

/**
 * Forward declarations for every file-local helper, right after the
 * macros/types -- confirmed 2026-09-07 as a standing convention:
 * function bodies can then be ordered however reads best.
 */
static void PrintPadded(uint8_t row, const char *text);
static void PrintPadded_P(uint8_t row, const char *flashText);
static uint8_t FrameIsClean(void);
static void FlushStep(void);
static char StatusTagFor(LcdJoystickUI_MachineState_t state);
static void ClearCompose(void);
static void PutMm(uint8_t pointColumn, int32_t valueUm, uint8_t withSign);
static void RedrawStatusScreen(void);
static void RedrawMainMenu(void);
static void RedrawJogAdjustment(void);
static void RedrawPenWizard(void);
#if LCDJOYSTICKUI_HAS_SD
static void RedrawSdBrowser(void);
#endif
static void RedrawCalMachineParams(void);
static void EnterMainMenu(void);
static uint8_t MenuItemAvailable(MenuItem_t item);
static uint8_t FirstAvailableMenuItem(void);
static void AdvanceMenuSelection(int8_t direction);
static void EnterMenuItem(MenuItem_t item);
static void EnterJogAdjustment(void);
static void ConfirmCalibration(void);
static uint8_t RepeatGate(uint8_t nowTick);
static void HandleMainMenuInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
static void HandleJogControlInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
#if CORELR_MENU_HOME
static void HandleHomingInput(uint8_t shortPress);
#endif
static void HandleOriginConfirmInput(uint8_t shortPress, uint8_t longPress);
static void RedrawOriginConfirm(void);
static void HandleCalMachineParamsInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
static uint16_t AdjustTargetRate(int16_t deflection);
static void AdjustRampToward(uint16_t target, uint8_t nowTick);
static uint32_t AdjustNextHold(void);
static uint8_t AdjustQueueDepth(uint16_t rate);
static void HandleJogAdjustmentInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
static void HandlePenWizardInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
#if LCDJOYSTICKUI_HAS_SD
static void HandleSdBrowserInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick);
static void RefreshBrowserEntry(void);
#endif

/** The machine state the job state machine last reported (LcdJoystickUI_SetMachineState()): the status tag, and which menu items are offered. */
static LcdJoystickUI_MachineState_t s_machineState = LCDJOYSTICKUI_STATE_UNCAL;
/** The screen / mode being shown, which is also the input handler that runs: Task() dispatches on it. */
static UiMode_t s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
#if CORELR_MENU_HOME
static uint8_t s_homeQueued;    /**< the move to the origin has been accepted by the planner */
static uint8_t s_homeStopping;  /**< SW pressed: ramping down, the rest of the move is dropped when done */
#endif

/** SW press timing (@req{REQ-CoreLR-D-LcdJoystickUI-0003}). */
static uint8_t s_swWasDown;
static uint8_t s_swPressStartTick;   /**< SysTick of the press's start: its age tells a short press from a long one */

/** Shared repeat-gate timestamp for whichever mode's Y/X-driven repeat
 * stepping is currently active -- safe to share since only one mode is
 * ever active at a time (reset to 0 on every mode transition, below). */
static uint8_t s_lastRepeatTick;

static uint8_t s_menuSelectedIndex;   /**< the selected item of the main menu, a MenuItem_t */
static uint8_t s_originToReference;    /**< the origin confirmation screen is for "Origin = calib." (1) or "Set origin" (0) */

/** Pending one-shot request for the job state machine
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0012}). Main-loop only, no ISR access,
 * hence no critical section -- see LcdJoystickUI_TakeEvent(). */
static uint8_t s_pendingEvent = (uint8_t)LCDJOYSTICKUI_EVENT_NONE;

static uint8_t s_lastJogTick;    /**< "Control" jog mode, elapsed-time base for speed scaling */

/** "Calibrate machine" steps 1-2, numeric parameter entry
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0010}). s_xWasDeflected (below) is shared
 * with "Adjustment" mode's motor-select toggle -- both are a single
 * edge-triggered X-axis choice, never active at the same time. */
static uint8_t s_calMachineStep;         /**< 0 = motor distance, 1 = reference cable length */
static int32_t s_calMachineValueUm;      /**< live-edited value for the current step */
static int32_t s_calMachineMotorDistanceUm; /**< step 0's result, held until step 1 persists both */
static uint8_t s_calMachineStepIndex;    /**< index into kCalStepSizesUm/kCalStepLabels */
static uint8_t s_calMachineRefused;      /**< 1 after Calibration_SetParams() refused the values (@req{REQ-CoreLR-D-LcdJoystickUI-0029}) until one changes */

static uint16_t s_adjustRate;                  /**< commanded jog rate, steps/s, ramped; 0 = stick centred */
static uint8_t s_adjustLastRateTick;           /**< SysTick reference for the ramp */
static Stepper_Motor_t s_adjustSelectedMotor;   /**< "Adjustment" jog mode, @req{REQ-CoreLR-D-LcdJoystickUI-0005} */
static int32_t s_adjustStepCountL;             /**< raw step counters, session-local: only used to keep the
                                                     *non-moving* motor's coil bits holding steady via the same
                                                     phase table, never a position (meaningless pre-calibration) */
static int32_t s_adjustStepCountR;   /**< raw step counter of motor R, as s_adjustStepCountL */
static uint8_t s_xWasDeflected;                /**< edge-detect for the X-axis step-size change in "Calibrate machine" steps 1-2
                                                     (once per gesture); the motor select of "Adjustment" is not edge-triggered any more */

#if LCDJOYSTICKUI_HAS_SD
/** SD browser (@req{REQ-CoreLR-D-LcdJoystickUI-0008}). s_browserTotal/
 * s_browserCurrentName/s_browserCurrentIsDirectory are a cache, refreshed
 * only by RefreshBrowserEntry() -- on entering the browser and after
 * every navigation/enter/up action -- never on every Task() tick: each
 * SDReader_Browser*() call does real SD I/O (SDReader.c's
 * FindEntryAtRank() rescans the directory), so recomputing them
 * unconditionally every loop() iteration while merely idling on this
 * screen would hammer the card for no reason. */
static uint8_t s_browserIndex;
static uint8_t s_browserTotal;   /**< entries in the current directory (0 = empty, or the card is unusable: SDReader says why) */
static char s_browserCurrentName[13];   /**< 8.3 name of the entry at s_browserIndex */
static uint8_t s_browserCurrentIsDirectory;   /**< 1 if that entry is a directory */
static uint8_t s_browserRefused;   /**< the last descent was refused for want of room in the path: row 1 says so until the next move (@req{REQ-CoreLR-D-SDReader-0011}) */
#endif

static uint8_t s_penWizardStep;                /**< 0 = "Set PEN UP", 1 = "Set PEN DOWN" */
static uint8_t s_penWizardAngle;               /**< live-edited angle for the current step */
static uint8_t s_penWizardUpAngle;             /**< step 0's result, held until step 1 persists both */

/**
 * Display frame buffer and its transmission state
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0011}).
 *
 * Rendering writes only into s_frame -- no I2C -- and marks a dirty bit
 * for each character that actually *changed*. A bounded slice of the
 * dirty characters is then transmitted per LcdJoystickUI_Task() call.
 * Motivation: one character costs ~0.36ms of blocking I2C (as first
 * measured; protocol arithmetic at 100 kHz gives 0.47ms, to be re-measured
 * on the bench with LcdI2c) against ~11.5ms (~15ms) for a full 32-character
 * redraw, while the step queue
 * holds only ~20ms of motion -- so an unconditional full redraw per
 * loop() iteration starves the queue mid-plot.
 *
 * The per-character diff doubles as the change detector: if a re-render
 * produces identical content, no dirty bit is set and nothing is sent.
 * That is why no separate "last shown values" tracking is needed.
 */
#define LCDJOYSTICKUI_CELLS (LCDJOYSTICKUI_ROWS * LCDJOYSTICKUI_COLS)

/** Input timings in SysTick ticks, derived -- never written by hand. */
#define LCDJOYSTICKUI_LONGPRESS_TICKS (LCDJOYSTICKUI_LONGPRESS_MS / SYSTICK_PERIOD_MS)
#define LCDJOYSTICKUI_REPEAT_TICKS    (LCDJOYSTICKUI_REPEAT_MS / SYSTICK_PERIOD_MS)   /**< the minimum interval of RepeatGate(), SysTick ticks */

/** Refresh period in SysTick ticks, derived -- never written by hand. */
#define LCDJOYSTICKUI_REFRESH_TICKS (LCDJOYSTICKUI_REFRESH_MS / SYSTICK_PERIOD_MS)
#if (LCDJOYSTICKUI_REFRESH_MS % SYSTICK_PERIOD_MS) != 0
#error "LCDJOYSTICKUI_REFRESH_MS must be a whole number of SysTick periods"
#endif
#if LCDJOYSTICKUI_REFRESH_TICKS > SYSTICK_TICKS_MAX
#error "LCDJOYSTICKUI_REFRESH_TICKS exceeds SYSTICK_TICKS_MAX -- the wraparound-safe comparison would be ambiguous (REQ-CoreLR-D-SysTick-0003)"
#endif
#if LCDJOYSTICKUI_LONGPRESS_TICKS > SYSTICK_TICKS_MAX
#error "LCDJOYSTICKUI_LONGPRESS_TICKS exceeds SYSTICK_TICKS_MAX"
#endif

/** Characters transmitted per Task() call. 1 keeps the blocking I2C
 * cost at ~0.36ms per loop() iteration; a full frame then takes 32
 * iterations, comfortably inside the 50ms refresh period. */
#define LCDJOYSTICKUI_TX_PER_TASK 1u

static char s_frame[LCDJOYSTICKUI_CELLS];             /**< what the display should show */
static uint8_t s_dirty[(LCDJOYSTICKUI_CELLS + 7u) / 8u]; /**< one bit per cell, set on change */
static uint8_t s_lcdCursor;                           /**< physical cursor position, 0..CELLS; CELLS = unknown */
static uint8_t s_lastRefreshTick;                     /**< SysTick reference for the rate limit */

/** Line composed by a Redraw*() before PrintPadded() diffs it into the
 * frame buffer. One buffer suffices: each Redraw*() fills row 0 then
 * row 1, sequentially. */
static char s_composeLine[LCDJOYSTICKUI_COLS + 1];

/** @brief Render `text` into row `row` of the frame buffer, left-aligned at column 0 and space-padded to
 * the full 16 columns (so nothing is left over from a longer text a previous screen put on the same row),
 * marking dirty each character that differs from what the buffer already held
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0011}). No I2C traffic: FlushStep() sends the dirty cells, and an identical
 * re-render sets no bit and costs the bus nothing. Longer text is clipped. */
static void PrintPadded(uint8_t row, const char *text)
{
   uint8_t pos = (uint8_t)(row * LCDJOYSTICKUI_COLS);
   uint8_t i;
   uint8_t ended = 0;

   for (i = 0; i < (uint8_t)LCDJOYSTICKUI_COLS; i++, pos++)
   {
      char c = ended ? ' ' : text[i];

      if (c == '\0')
      {
         ended = 1;
         c = ' ';
      }
      if (s_frame[pos] != c)
      {
         s_frame[pos] = c;
         s_dirty[pos >> 3] |= (uint8_t)(1u << (pos & 7u));
      }
   }
}

/**
 * @brief Same as PrintPadded(), for a literal kept in flash -- pass
 * PSTR("...") at the call site. Every *fixed* screen string in this
 * file should go through this, not PrintPadded() with a plain literal:
 * on classic AVR a plain `const char*` string literal is still copied
 * into RAM at boot (same class of issue already fixed for
 * Motion's coil-phase table, raised 2026-09-07 during the
 * memory-optimization pass).
 */
/** @return 1 if every dirty character has been transmitted. */
static uint8_t FrameIsClean(void)
{
   uint8_t i;

   for (i = 0; i < (uint8_t)sizeof(s_dirty); i++)
   {
      if (s_dirty[i] != 0u)
      {
         return 0;
      }
   }
   return 1;
}

/**
 * @brief Transmit up to LCDJOYSTICKUI_TX_PER_TASK dirty characters.
 *
 * A positioning command costs about as much as a character (~4 I2C
 * bytes), so one is emitted only when the target cell is not already
 * under the physical cursor -- the LCD controller auto-increments its
 * own address counter, which makes a run of consecutive dirty cells
 * free to send. Changes are naturally contiguous in practice (the
 * digits of a position value), which this exploits.
 */
static void FlushStep(void)
{
   uint8_t sent;

   for (sent = 0; sent < LCDJOYSTICKUI_TX_PER_TASK; sent++)
   {
      uint8_t pos;

      for (pos = 0; pos < (uint8_t)LCDJOYSTICKUI_CELLS; pos++)
      {
         if ((s_dirty[pos >> 3] & (uint8_t)(1u << (pos & 7u))) != 0u)
         {
            break;
         }
      }
      if (pos >= (uint8_t)LCDJOYSTICKUI_CELLS)
      {
         return; /* nothing dirty: the display already matches the frame */
      }

      if (s_lcdCursor != pos)
      {
         if (!LcdI2c_SetCursor((uint8_t)(pos % LCDJOYSTICKUI_COLS), (uint8_t)(pos / LCDJOYSTICKUI_COLS)))
         {
            s_lcdCursor = (uint8_t)LCDJOYSTICKUI_CELLS;
            return; /* not sent: the cell stays dirty and is offered again on the next call */
         }
         s_lcdCursor = pos;
      }
      if (!LcdI2c_PutChar((uint8_t)s_frame[pos]))
      {
         s_lcdCursor = (uint8_t)LCDJOYSTICKUI_CELLS; /* did the character land? unknown, so reposition and resend */
         return;
      }
      /* The controller's address counter runs on past the last visible column into memory that is not
       * displayed, NOT to the next row: after the last column of a row the cursor is unknown, and the
       * first cell of the next row must be positioned. (Latent until the dirty bits were really set.) */
      s_lcdCursor = (uint8_t)(((pos + 1u) % LCDJOYSTICKUI_COLS) != 0u ? (pos + 1u) : LCDJOYSTICKUI_CELLS);
      s_dirty[pos >> 3] &= (uint8_t)~(1u << (pos & 7u));
   }
}

/** PrintPadded() for a text in flash: copied into a 16-character buffer (the text is cut there), then shown padded with blanks to the row's width.
 *  @param row        0 or 1.
 *  @param flashText  the text, in flash (PSTR() or a PROGMEM array). */
static void PrintPadded_P(uint8_t row, const char *flashText)
{
   static char buffer[LCDJOYSTICKUI_COLS + 1]; /* static: see RedrawStatusScreen()'s comment -- not re-entrant, never ISR-called */

   strncpy_P(buffer, flashText, sizeof(buffer) - 1u);
   buffer[sizeof(buffer) - 1u] = '\0';
   PrintPadded(row, buffer);
}

/** The character at the left of the status screen's first row for a machine state (the LCDJOYSTICKUI_TAG_* of LcdJoystickUI.h); idle for any other value.
 *  @param state  the machine state. */
static char StatusTagFor(LcdJoystickUI_MachineState_t state)
{
   switch (state)
   {
      case LCDJOYSTICKUI_STATE_RUNNING:      return LCDJOYSTICKUI_TAG_RUNNING;
      case LCDJOYSTICKUI_STATE_PAUSED:       return LCDJOYSTICKUI_TAG_PAUSED;
      case LCDJOYSTICKUI_STATE_ERROR:        return LCDJOYSTICKUI_TAG_ERROR;
      case LCDJOYSTICKUI_STATE_UNCAL: return LCDJOYSTICKUI_TAG_UNCALIB;
      case LCDJOYSTICKUI_STATE_IDLE:
      default:                        return LCDJOYSTICKUI_TAG_IDLE;
   }
}

/*
 * How a length is shown on the LCD (REQ-CoreLR-D-LcdJoystickUI-0023): millimetres with ONE decimal, the decimal point always at the same column, so that nothing
 * moves when a number gets a digit more or less and only the digits that change are sent to the display. The number is right-aligned on the point, the minus
 * sign (a position only: a length has none) stuck to its first digit. Rounded to the nearest tenth, half away from zero -- not truncated -- and a result of 0.0 never
 * carries a sign.
 *
 * What can be shown: a position is held within +-2^MOTION_REACH_SHIFT um (the parser's reach rule), a numeric entry within
 * LCDJOYSTICKUI_CAL_MAX_UM (SwConfig.h), a cable length within the arithmetic's bound (CORELR_MATH_48_LIMIT_UM; any int32 with CORELR_MATH_64). The field before the point is 5
 * characters (a sign and four digits, up to 9999.9): when no configuration can exceed that, no code limits the value. When one can, LCDJOYSTICKUI_DISPLAY_CAP
 * compiles in the limit, and the number is shown as 9999.9.
 */
/** The largest position magnitude, um, that the parser's reach rule lets Motion hold. */
#define LCDJOYSTICKUI_JOG_LIMIT_UM ((1L << MOTION_REACH_SHIFT) - 1L)
#if CORELR_MATH == CORELR_MATH_64
/** The largest cable length, um, that the arithmetic can produce: any int32 in the 64-bit build. */
#define LCDJOYSTICKUI_LENGTH_BOUND_UM 2147483647L
#else
/** The largest cable length, um, that the arithmetic can produce: the saturation limit of the 48-bit build. */
#define LCDJOYSTICKUI_LENGTH_BOUND_UM CORELR_MATH_48_LIMIT_UM
#endif
#if (LCDJOYSTICKUI_LENGTH_BOUND_UM >= 9999950L) || (LCDJOYSTICKUI_JOG_LIMIT_UM >= 9999950L) || (LCDJOYSTICKUI_CAL_MAX_UM >= 9999950L)
/** 1 if a value can exceed what the field holds (9999.9 mm): PutMm() then limits it. */
#define LCDJOYSTICKUI_DISPLAY_CAP 1
#else
/** 0: no configuration can exceed the field, so no code limits the value. */
#define LCDJOYSTICKUI_DISPLAY_CAP 0
#endif

/** Blank the compose line: 16 spaces, then the terminator. */
static void ClearCompose(void)
{
   memset(s_composeLine, ' ', LCDJOYSTICKUI_COLS);
   s_composeLine[LCDJOYSTICKUI_COLS] = '\0';
}

/** Write a length into the compose line: the decimal point at `pointColumn`, one decimal after it, the integer part and the sign to its left. */
static void PutMm(uint8_t pointColumn, int32_t valueUm, uint8_t withSign)
{
   uint32_t magnitude = (valueUm < 0) ? (0u - (uint32_t)valueUm) : (uint32_t)valueUm;   /* also right for INT32_MIN */
   uint32_t tenths = (magnitude + 50u) / 100u;                      /* to the nearest tenth of a millimetre */
   uint16_t whole;
   uint8_t at = pointColumn;

#if LCDJOYSTICKUI_DISPLAY_CAP
   if (tenths > 99999u)
   {
      tenths = 99999u;                                             /* 9999.9: the most the field holds */
   }
#endif
   whole = (uint16_t)(tenths / 10u);
   s_composeLine[pointColumn] = '.';
   s_composeLine[pointColumn + 1u] = (char)('0' + (uint8_t)(tenths % 10u));
   do
   {
      at--;
      s_composeLine[at] = (char)('0' + (uint8_t)(whole % 10u));
      whole = (uint16_t)(whole / 10u);
   } while (whole != 0u);
   if (withSign && (valueUm < 0) && (tenths != 0u))
   {
      s_composeLine[at - 1u] = '-';
   }
}

/**
 * @brief Redraw the status screen (@req{REQ-CoreLR-D-LcdJoystickUI-0002}).
 * Line 1: \<tag\>\<X\>,\<Y\> in mm. Line 2: L,R in mm. Also reused, as-is, by
 * "Control" jog mode -- the same readout is exactly what's useful while
 * jogging.
 * @todo Line 2 currently derives L,R from Motion_XYToLR()
 * rather than the motors' actual tracked cable length -- revisit once
 * step-count tracking exists.
 */
static void RedrawStatusScreen(void)
{
   /**
    * Uses the shared s_composeLine instead of locals -- this function
    * sits at the bottom of the single most frequently executed call
    * chain in the firmware (loop->Task->here->XYToLR->ISqrt, run every
    * tick while idle/jogging); measured via avr-gcc -fstack-usage at
    * 380 bytes deep against 344 available before the buffer was shared.
    *
    * Layout (@req{REQ-CoreLR-D-LcdJoystickUI-0023}): the decimal points sit at
    * columns 6 and 14 on both rows, the status tag in column 0, "L" in
    * column 1 and "R" in column 9. Row 0 must be handed to PrintPadded()
    * before row 1 overwrites the single compose buffer.
    */
   Motion_Position_t pos = Motion_GetComputedPosition();
   Motion_CableLengths_t lr = Motion_XYToLR(pos);

   ClearCompose();
   /* The tag says what the screen is doing: J while the Control jog is active, H while "Return home" is (REQ-CoreLR-D-LcdJoystickUI-0025), else the machine's state */
   s_composeLine[0] = (s_uiMode == LCDJOYSTICKUI_MODE_JOG_CONTROL) ? LCDJOYSTICKUI_TAG_JOG
#if CORELR_MENU_HOME
                    : (s_uiMode == LCDJOYSTICKUI_MODE_HOMING)      ? LCDJOYSTICKUI_TAG_HOMING
#endif
                    : StatusTagFor(s_machineState);
   PutMm(LCDJOYSTICKUI_X_POINT_COLUMN, pos.X, 1);
   PutMm(LCDJOYSTICKUI_Y_POINT_COLUMN, pos.Y, 1);
   PrintPadded(0, s_composeLine);

   /* Row 1 carries the cable lengths on the status screen, but the
    * starvation counter while jogging: this screen is reused for
    * "Control" jog, and that is the one mode where the question is
    * whether waypoint generation keeps up, not where the head is.
    * Cable lengths are read standing still, on the status screen.
    *
    * Bring-up aid for the open jog-speed regression -- see
    * docs/bringup/Bringup-Status.md. The adjustment jog has carried the same
    * counter since REQ-CoreLR-D-LcdJoystickUI-0016; it was missing
    * here, which made that note's first test unperformable. */
#if CORELR_DEBUG_MISS_COUNT
   if (s_uiMode == LCDJOYSTICKUI_MODE_JOG_CONTROL)
   {
      uint8_t at = TextFmt_StrP(s_composeLine, 0, sizeof(s_composeLine), PSTR("jog    miss:"));

      (void)TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), Stepper_StarvedCount());
   }
   else
#endif
   {
      ClearCompose();
      s_composeLine[0] = PenControl_IsDown() ? LCDJOYSTICKUI_PEN_DOWN_CHAR : LCDJOYSTICKUI_PEN_UP_CHAR;   /* the pen, column 0 of row 1 (-0030) */
      s_composeLine[LCDJOYSTICKUI_L_LABEL_COLUMN] = 'L';
      s_composeLine[LCDJOYSTICKUI_R_LABEL_COLUMN] = 'R';
      PutMm(LCDJOYSTICKUI_X_POINT_COLUMN, lr.L, 0);
      PutMm(LCDJOYSTICKUI_Y_POINT_COLUMN, lr.R, 0);
   }
   PrintPadded(1, s_composeLine);
}

/** @brief Redraw the main menu: selected item's label + "Item i/N". */
static void RedrawMainMenu(void)
{
   const char *flashLabel = (const char *)pgm_read_word(&kMenuLabels[s_menuSelectedIndex]);

   strncpy_P(s_composeLine, flashLabel, sizeof(s_composeLine) - 1u);
   s_composeLine[sizeof(s_composeLine) - 1u] = '\0';
   PrintPadded(0, s_composeLine);

   /* Counts only the items actually offered in this state: the raw
    * index over MENU_ITEM_COUNT would read "Item 5/7" for the first of
    * two job-control entries. */
   {
      uint8_t i;
      uint8_t shown = 0;
      uint8_t position = 0;

      for (i = 0; i < (uint8_t)MENU_ITEM_COUNT; i++)
      {
         if (MenuItemAvailable((MenuItem_t)i))
         {
            shown++;
            if (i <= s_menuSelectedIndex)
            {
               position = shown;
            }
         }
      }
      {
         uint8_t at = TextFmt_StrP(s_composeLine, 0, sizeof(s_composeLine), PSTR("Item "));

         at = TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), position);
         at = TextFmt_Char(s_composeLine, at, sizeof(s_composeLine), '/');
         (void)TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), shown);
      }
   }
   PrintPadded(1, s_composeLine);
}

/** @brief Redraw "Adjustment" jog mode: which motor is selected, and the confirm hint. */
static void RedrawJogAdjustment(void)
{
   /* Row 0 also carries the live step rate: the speed now follows the
    * stick (REQ-CoreLR-D-LcdJoystickUI-0014), and showing it is what
    * makes the curve legible while positioning -- and tunable. */
   {
      uint8_t at = TextFmt_StrP(s_composeLine, 0, sizeof(s_composeLine),
                                (s_adjustSelectedMotor == STEPPER_MOTOR_L) ? PSTR("[L] R  ") : PSTR("L [R]  "));

      if (s_adjustRate == 0u)
      {
         at = TextFmt_StrP(s_composeLine, at, sizeof(s_composeLine), PSTR("   --"));
      }
      else
      {
         /* The *current* rate, mid-ramp included, not the target: what
          * the motor is doing is the useful thing to see. */
         at = TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), s_adjustRate);
      }
      (void)TextFmt_StrP(s_composeLine, at, sizeof(s_composeLine), PSTR("/s"));
   }
   PrintPadded(0, s_composeLine);

   /* Row 1 carries the starvation count while positioning: it is the
    * one number that separates "the motor cannot keep up" from "we
    * stopped feeding it", and the two look identical on the bench. */
#if CORELR_DEBUG_MISS_COUNT
   {
      uint8_t at = TextFmt_StrP(s_composeLine, 0, sizeof(s_composeLine), PSTR("SW=ok   miss:"));
      (void)TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), Stepper_StarvedCount());
   }
   PrintPadded(1, s_composeLine);
#else
   PrintPadded_P(1, PSTR("SW=ok"));
#endif
}

/** @brief Redraw "Calibrate machine" steps 1-2 (@req{REQ-CoreLR-D-LcdJoystickUI-0010}). */
static void RedrawCalMachineParams(void)
{
   PrintPadded_P(0, s_calMachineRefused ? PSTR("Ref out of zone!")   /* the machine refused them: the travel zone (REQ-CoreLR-D-Calibration-0011) */
                  : ((s_calMachineStep == 0u) ? PSTR("Motor dist (mm)") : PSTR("Ref length (mm)")));

   /* Row 1: the value with its point where the status screen has its X point (column 6), then the step the Y axis
    * adds, "+-" and the flash label, from column 9 -- "+-100mm" fills 9..15 exactly, one blank after the value's
    * last digit. The label is appended last, and TextFmt terminates there, so the line ends with it. */
   ClearCompose();
   PutMm(LCDJOYSTICKUI_X_POINT_COLUMN, s_calMachineValueUm, 0);
   {
      uint8_t at = TextFmt_StrP(s_composeLine, LCDJOYSTICKUI_CAL_STEP_COLUMN, sizeof(s_composeLine), PSTR("+-"));

      (void)TextFmt_StrP(s_composeLine, at, sizeof(s_composeLine),
                         (const char *)pgm_read_word(&kCalStepLabels[s_calMachineStepIndex]));
   }
   PrintPadded(1, s_composeLine);
}

/** @brief Redraw the pen calibration wizard screen (@req{REQ-CoreLR-D-LcdJoystickUI-0007}). */
static void RedrawPenWizard(void)
{
   PrintPadded_P(0, (s_penWizardStep == 0u) ? PSTR("Set PEN UP") : PSTR("Set PEN DOWN"));
   {
      uint8_t at = TextFmt_StrP(s_composeLine, 0, sizeof(s_composeLine), PSTR("Angle: "));

      at = TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), s_penWizardAngle);
      (void)TextFmt_StrP(s_composeLine, at, sizeof(s_composeLine), PSTR(" [Y]"));
   }
   PrintPadded(1, s_composeLine);
}

#if LCDJOYSTICKUI_HAS_SD
/** @brief Redraw the SD file browser (@req{REQ-CoreLR-D-LcdJoystickUI-0008}) from the cache -- no SD I/O here. */
static void RedrawSdBrowser(void)
{
   if (s_browserTotal == 0u)
   {
      /* Say *what* is wrong (REQ-CoreLR-D-SDReader-0007). These three
       * used to be one indistinguishable "No files found", which is
       * what made an absent card look like an empty one on real
       * hardware (2026-09-10).
       *
       * Row 1 always states the way out: with nothing selectable, the
       * only exit is a long SW press, and nothing on screen said so. */
      switch (SDReader_CardStatus())
      {
         case SDREADER_CARD_ABSENT:
            PrintPadded_P(0, PSTR("No SD card"));
            PrintPadded_P(1, PSTR("SW retry/Hold b"));
            break;
         case SDREADER_CARD_NO_VOLUME:
            PrintPadded_P(0, PSTR("Card unreadable"));
            PrintPadded_P(1, PSTR("Need FAT16/32"));
            break;
         case SDREADER_CARD_NO_ROOT:
            PrintPadded_P(0, PSTR("No root dir"));
            PrintPadded_P(1, PSTR("SW retry/Hold b"));
            break;
         case SDREADER_CARD_OK:
         default:
            PrintPadded_P(0, PSTR("No files found"));
            PrintPadded_P(1, PSTR("Hold SW = back"));
            break;
      }
      return;
   }
   if (s_browserRefused)
   {
      PrintPadded(0, s_browserCurrentName);
      PrintPadded_P(1, PSTR("Path too long"));
      return;
   }
   PrintPadded(0, s_browserCurrentName); /* not a literal -- the cached name is already a RAM buffer, PrintPadded() is correct here */
   {
      uint8_t at = TextFmt_Uint(s_composeLine, 0, sizeof(s_composeLine), (uint16_t)(s_browserIndex + 1u));

      at = TextFmt_Char(s_composeLine, at, sizeof(s_composeLine), '/');
      (void)TextFmt_Uint(s_composeLine, at, sizeof(s_composeLine), s_browserTotal);
   }
   PrintPadded(1, s_composeLine);
}

/** @brief Refresh the SD browser cache: called on entering the browser and after every navigation/enter/up action, never unconditionally per tick. */
static void RefreshBrowserEntry(void)
{
   uint8_t pass;

   s_browserRefused = 0;      /* any move of the list, or a change of directory, ends the message */
   /* A card pulled (and put back) during a navigation makes the reads fail: the SD layer notes it, and one remount later the browser is at the root again
    * (REQ-CoreLR-D-SDReader-0009). The index is reset, since it belonged to a directory that is no longer the one shown. */
   for (pass = 0; pass < 2u; pass++)
   {
      s_browserTotal = SDReader_BrowserEntryCount();
      if (s_browserTotal > 0u)
      {
         SDReader_BrowserGetEntryName(s_browserIndex, s_browserCurrentName, sizeof(s_browserCurrentName));
         s_browserCurrentIsDirectory = SDReader_BrowserIsDirectory(s_browserIndex);
      }
      else
      {
         s_browserCurrentName[0] = '\0';
         s_browserCurrentIsDirectory = 0;
      }
      if (!SDReader_BrowserRecover())
      {
         break;
      }
      s_browserIndex = 0;
   }
}

#endif /* LCDJOYSTICKUI_HAS_SD */

/** Open the main menu on its first available item (@req{REQ-CoreLR-D-LcdJoystickUI-0012}); if the state offers nothing, stay on the status screen. */
static void EnterMainMenu(void)
{
   uint8_t first = FirstAvailableMenuItem();

   if (first >= (uint8_t)MENU_ITEM_COUNT)
   {
      return; /* nothing offered in this state -- stay on the status screen */
   }
   s_uiMode = LCDJOYSTICKUI_MODE_MENU;
   s_menuSelectedIndex = first;
   s_lastRepeatTick = 0;
}

/**
 * @brief Is `item` offered in the current machine state
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0012})?
 *
 * This is what keeps a running job from being interfered with: while a
 * job is active the menu offers only the job controls, so entering a
 * jog mode, the pen wizard or the SD browser is impossible *by
 * construction* rather than by a separate guard at each entry point.
 * @req{REQ-CoreLR-F-UserInterface-0002}'s "the menu stays reachable" still
 * holds -- it just holds different items.
 *
 * The uncalibrated rule (@req{REQ-CoreLR-D-LcdJoystickUI-0006}) folds in as
 * one more case rather than staying a special case inside the
 * selection loop.
 */
static uint8_t MenuItemAvailable(MenuItem_t item)
{
   if (item == MENU_BACK)
   {
      return 1;   /* the way out, in every state */
   }
   switch (s_machineState)
   {
      case LCDJOYSTICKUI_STATE_RUNNING:
         return (uint8_t)((item == MENU_HALT) || (item == MENU_CANCEL));
      case LCDJOYSTICKUI_STATE_PAUSED:
         return (uint8_t)((item == MENU_RESUME) || (item == MENU_CANCEL));
      case LCDJOYSTICKUI_STATE_ERROR:
         return 0; /* acknowledged from the status screen (REQ-CoreLR-D-GCodeParser-0006), not here */
      case LCDJOYSTICKUI_STATE_IDLE:
      case LCDJOYSTICKUI_STATE_UNCAL:
      default:
         if ((item == MENU_HALT) || (item == MENU_RESUME) || (item == MENU_CANCEL))
         {
            return 0; /* no job to act on */
         }
#if CORELR_MENU_HOME
         if (item == MENU_HOME)
         {
            return Calibration_IsCalibrated();   /* (0,0) means nothing until the position is known: as for the jog */
         }
#endif
         return (uint8_t)(((item != MENU_JOG) && (item != MENU_SET_ORIGIN) && (item != MENU_ORIGIN_CAL)) || Calibration_IsCalibrated());   /* the origin means nothing until the position is known: as for the jog */
   }
}

/** @return the first available item, or MENU_ITEM_COUNT if none is. */
static uint8_t FirstAvailableMenuItem(void)
{
   uint8_t i;

   for (i = 0; i < (uint8_t)MENU_ITEM_COUNT; i++)
   {
      if (MenuItemAvailable((MenuItem_t)i))
      {
         return i;
      }
   }
   return (uint8_t)MENU_ITEM_COUNT;
}

/**
 * @brief Move the main menu selection by one item, skipping the items MenuItemAvailable() refuses (MENU_JOG while uncalibrated, @req{REQ-CoreLR-D-LcdJoystickUI-0006}).
 * No wrap-around (@req{REQ-CoreLR-D-LcdJoystickUI-0022}): at the first or last available item the selection stays.
 * @param direction  +1 for the next item, -1 for the previous one.
 */
static void AdvanceMenuSelection(int8_t direction)
{
   int16_t next = (int16_t)s_menuSelectedIndex;

   /* No wrap-around (REQ-CoreLR-D-LcdJoystickUI-0022): at the first or the last available item the selection stays where it is. */
   for (;;)
   {
      next = (int16_t)(next + direction);
      if ((next < 0) || (next >= (int16_t)MENU_ITEM_COUNT))
      {
         return;
      }
      if (MenuItemAvailable((MenuItem_t)next))
      {
         s_menuSelectedIndex = (uint8_t)next;
         return;
      }
   }
}

/**
 * @brief Confirm the machine is physically at the calibration reference
 * position (@req{REQ-CoreLR-D-Calibration-0001}) and seed tracked
 * state accordingly.
 *
 * Calibration_MarkCalibrated() sets WorkOriginOffset :=
 * ReferencePosition (@req{REQ-CoreLR-D-Calibration-0002}). Given
 * Motion_XYToLR()'s relation `internal = WorkOriginOffset +/- drawing`
 * (@req{REQ-CoreLR-D-Motion-0025}: minus for Y), the drawing-frame coordinate of the reference
 * position is therefore *always* exactly (0,0) the instant that
 * assignment happens -- true for any ReferencePosition value, not just
 * this machine's -- so nothing needs to be fetched or computed here.
 */
static void ConfirmCalibration(void)
{
   Motion_Position_t origin = { 0, 0 };

   Calibration_MarkCalibrated();
   Motion_SetCurrentPosition(origin);
   /* The machine is standing exactly where the user has just declared it
    * to be: store that now rather than leave a window in which a power cut
    * costs the calibration just made (REQ-CoreLR-D-Calibration-0008). */
   Calibration_StoreCurrentPosition();
   LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_IDLE);
}

/** @brief Step 3 of "Calibrate machine" (@req{REQ-CoreLR-D-LcdJoystickUI-0005}): reach the reference position manually. */
static void EnterJogAdjustment(void)
{
   s_uiMode = LCDJOYSTICKUI_MODE_JOG_ADJUST;
   s_adjustSelectedMotor = STEPPER_MOTOR_L;
   s_adjustStepCountL = 0;
   s_adjustStepCountR = 0;
   s_xWasDeflected = 0;
   s_lastRepeatTick = 0;
}

/**
 * @brief Act on the selected item: enter its mode (and initialise that mode's state), or for the job controls post the event for the job state machine
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0012}) and return to the status screen. The modes that need a calibrated machine check it again (defensive: the menu
 * does not offer them otherwise).
 * @param item  the item chosen.
 */
static void EnterMenuItem(MenuItem_t item)
{
   switch (item)
   {
      case MENU_JOG:
         if (Calibration_IsCalibrated()) /* defensive: AdvanceMenuSelection() already keeps this unreachable otherwise */
         {
            s_uiMode = LCDJOYSTICKUI_MODE_JOG_CONTROL;
#if CORELR_DEBUG_MISS_COUNT
            Stepper_ResetStarvedCount(); /* count this jog, not the session */
#endif
            s_lastJogTick = SysTick_Get();
            JogCurve_Reset();
         }
         break;
#if CORELR_MENU_HOME
      case MENU_HOME:
         if (Calibration_IsCalibrated())     /* defensive, like MENU_JOG */
         {
            PenControl_RequestUp();          /* a no-op if it is up; the request goes ahead of every move queued below */
            s_homeQueued = 0;
            s_homeStopping = 0;
            s_uiMode = LCDJOYSTICKUI_MODE_HOMING;
         }
         break;
#endif
      case MENU_SET_ORIGIN:
      case MENU_ORIGIN_CAL:
         if (Calibration_IsCalibrated())     /* defensive, like MENU_JOG */
         {
            s_originToReference = (uint8_t)(item == MENU_ORIGIN_CAL);
            s_uiMode = LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM;
         }
         break;
      case MENU_CAL_MACHINE:
         /* Steps 1-2: numeric entry (REQ-CoreLR-D-LcdJoystickUI-0010); step 3 (EnterJogAdjustment())
          * only starts once both values are confirmed, from HandleCalMachineParamsInput(). */
         s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS;
         s_calMachineStep = 0;
         s_calMachineRefused = 0;
         s_calMachineValueUm = Calibration_GetMotorDistanceUm();
         s_calMachineStepIndex = 0;
         s_xWasDeflected = 0;
         s_lastRepeatTick = 0;
         break;
      case MENU_CAL_PEN:
         s_uiMode = LCDJOYSTICKUI_MODE_CAL_PEN;
         s_penWizardStep = 0;
         {
            uint8_t up, down;

            Calibration_GetPenAngles(&up, &down);
            s_penWizardAngle = up;
         }
         PenControl_PreviewAngle(s_penWizardAngle);
         s_lastRepeatTick = 0;
         break;
      case MENU_HALT:
      case MENU_RESUME:
      case MENU_CANCEL:
         /* Job control is not the UI's decision: report it and let the
          * state machine in grbl_corelr.ino act
          * (REQ-CoreLR-D-LcdJoystickUI-0012). */
         s_pendingEvent = (uint8_t)((item == MENU_HALT)   ? LCDJOYSTICKUI_EVENT_JOB_HALT
                                  : (item == MENU_RESUME) ? LCDJOYSTICKUI_EVENT_JOB_RESUME
                                                          : LCDJOYSTICKUI_EVENT_JOB_CANCEL);
         s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
         break;
      case MENU_BACK:
         s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
         break;
#if LCDJOYSTICKUI_HAS_SD
      case MENU_LOAD_FILE:
         s_uiMode = LCDJOYSTICKUI_MODE_SD_BROWSER;
         s_browserIndex = 0;
         s_lastRepeatTick = 0;
         RefreshBrowserEntry();
         break;
#endif
      default:
         break;
   }
}

/** @brief Minimum-interval gate shared by every mode's repeat-stepped
 * input. Uses the skip-missed pattern of @req{REQ-CoreLR-D-SysTick-0004}:
 * the reference is reassigned to *now*, never advanced by the period,
 * so a long blocked loop() cannot produce a burst of catch-up steps. */
static uint8_t RepeatGate(uint8_t nowTick)
{
   if (!SysTick_Elapsed(s_lastRepeatTick, LCDJOYSTICKUI_REPEAT_TICKS))
   {
      return 0;
   }
   s_lastRepeatTick = nowTick;
   return 1;
}

/**
 * @brief Main menu input: a short press enters the selected item, a long press leaves for the status screen, otherwise the stick (Y) moves the selection,
 * one item per RepeatGate() period.
 * @param shortPress  the button was pressed and released.
 * @param longPress   the button was held past the long-press time.
 * @param nowTick     SysTick_Get() of this pass.
 */
static void HandleMainMenuInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   if (shortPress)
   {
      EnterMenuItem((MenuItem_t)s_menuSelectedIndex);
   }
   else if (longPress)
   {
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }
   else
   {
      int8_t dy = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);

      if (dy != 0 && RepeatGate(nowTick))
      {
         AdvanceMenuSelection((dy > 0) ? 1 : -1);
      }
   }
}

/**
 * @brief "Control" jog mode (@req{REQ-CoreLR-D-LcdJoystickUI-0004}). Deliberately
 * checks Motion_IsIdle() rather than calling
 * Motion_Task() itself -- grbl_corelr.ino's loop() already
 * calls Task() once, unconditionally, every iteration; calling it again
 * here would advance a second Bresenham grid step per iteration while
 * jogging. s_lastJogTick is only advanced while idle, so elapsedMs
 * measures time since the *previous idle check*, not since the last
 * tick -- this correctly accumulates the full deflection-time spent
 * while a segment was in progress, instead of silently discarding it.
 */
static void HandleJogControlInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   if (shortPress || longPress)
   {
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; /* either press leaves the jog for the home screen, not the menu that opened it (REQ-CoreLR-D-LcdJoystickUI-0025) */
      return;
   }

   /* Keep the planner fed rather than waiting for it to drain: it needs
    * several blocks in hand for the junction look-ahead to carry speed
    * across them, and a one-block-at-a-time jog decelerates to a stop
    * between every block (REQ-CoreLR-D-LcdJoystickUI-0017). */
   if (Motion_PlannerFree() > (uint8_t)(MOTION_PLANNER_DEPTH - LCDJOYSTICKUI_JOG_BLOCKS))
   {
      /* Ticks -> ms. Clamped to SYSTICK_TICKS_MAX first because an
       * 8-bit tick difference is only unambiguous below half the
       * counter range (REQ-CoreLR-D-SysTick-0003), then to
       * LCDJOYSTICKUI_JOG_SLICE_MAX_MS so that time spent with a full
       * planner cannot be banked and spent as one long block. */
      uint8_t elapsedTicks = (uint8_t)(nowTick - s_lastJogTick);
      uint16_t elapsedMs;
      int16_t dx;
      int16_t dy;

      if (elapsedTicks > (uint8_t)SYSTICK_TICKS_MAX)
      {
         elapsedTicks = (uint8_t)SYSTICK_TICKS_MAX;
      }
      elapsedMs = (uint16_t)((uint16_t)elapsedTicks * SYSTICK_PERIOD_MS);

      /* Below one slice there is nothing worth committing: blocks that
       * short would leave the look-ahead with too little horizon to
       * reach VMAX. Return and let the time accumulate. */
      if (elapsedMs < (uint16_t)LCDJOYSTICKUI_JOG_SLICE_MS)
      {
         return;
      }
      if (elapsedMs > (uint16_t)LCDJOYSTICKUI_JOG_SLICE_MAX_MS)
      {
         elapsedMs = (uint16_t)LCDJOYSTICKUI_JOG_SLICE_MAX_MS;
      }
      s_lastJogTick = nowTick;

      dx = LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_X_PIN);
      dy = LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_Y_PIN);
      if (dx != 0 || dy != 0)
      {
         /* From where the queue will END, not from where the head is now: with
          * blocks already queued the head is behind them, and a slice measured
          * from it would overlap what is already accepted. And through the
          * planner, which is what this requirement says and what the code did
          * not do until 2026-10-06 -- Motion_RequestLinearMove() bypasses it, starts
          * and stops at rest, and was silently ignored whenever a move was in
          * progress, so the jog never chained a block (found while porting Motion). */
         Motion_Position_t pos = Motion_GetTargetPosition();

         int32_t moveX;
         int32_t moveY;

         /* The speed follows the NORM of the stick, its direction the stick's (REQ-CoreLR-D-LcdJoystickUI-0021); moveY is DOWN the wall */
         JogCurve_Displacement(dx, dy, elapsedMs, &moveX, &moveY);
         pos.X += moveX;
#if CORELR_Y_UP
         pos.Y -= moveY;     /* dy > 0 is the stick pushed down (HwConfig.h makes it so); the drawing frame has Y up
                              * (REQ-CoreLR-D-Motion-0025), so the pen goes down the wall */
#else
         pos.Y += moveY;     /* Y down: the same motion on the wall, the opposite sign */
#endif
         (void)Motion_QueueJog(pos);      /* the travel zone: the pen stops at its edge and slides along it (REQ-CoreLR-D-LcdJoystickUI-0029) */
      }
   }
}

#if CORELR_MENU_HOME
/**
 * @brief "Return home" (@req{REQ-CoreLR-D-LcdJoystickUI-0024}): one straight move to the drawing origin, SW stops it.
 *
 * The move goes through the planner like every other (a jog's blocks may still be draining), so it is queued as soon as
 * there is room rather than at entry. SW does not just drop it: Motion_Halt() bends the profile into a deceleration
 * ramp, and only when that ramp has been enqueued is the rest of the move dropped (Motion_Abort()) -- the machine
 * never stops dead, and its tracked position stays what was really commanded. Done when the generator is idle and
 * the planner empty (Motion_IsIdle() alone would say so while a block is still queued).
 */
static void HandleHomingInput(uint8_t shortPress)
{
   Motion_Position_t origin;

   if (shortPress && !s_homeStopping)
   {
      Motion_Halt();
      s_homeStopping = 1;
   }
   if (!s_homeQueued && !s_homeStopping && Motion_PlannerHasRoom())
   {
      origin.X = 0;
      origin.Y = 0;
      s_homeQueued = Motion_QueueMove(origin);
   }
   if (s_homeStopping ? Motion_IsHaltRampDone() : (s_homeQueued && Motion_IsIdle() && Motion_PlannerIsEmpty()))
   {
      if (s_homeStopping)
      {
         Motion_Abort();
      }
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }
}
#endif

/**
 * @brief The confirmation of "Set origin" / "Origin = calib." (@req{REQ-CoreLR-D-LcdJoystickUI-0031}): a short press does it and shows the status screen, where the new
 * coordinates can be read; a long press leaves without doing anything. If Motion is not idle yet, or the origin would lie outside the travel zone, nothing is done and the
 * screen stays (a second press tries again).
 */
static void HandleOriginConfirmInput(uint8_t shortPress, uint8_t longPress)
{
   if (longPress)
   {
      s_uiMode = LCDJOYSTICKUI_MODE_MENU;
   }
   else if (shortPress && Motion_SetOrigin(s_originToReference ? MOTION_ORIGIN_REFERENCE : (MOTION_ORIGIN_X | MOTION_ORIGIN_Y), 0, 0))
   {
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }
}

/** @brief Redraw the origin confirmation (@req{REQ-CoreLR-D-LcdJoystickUI-0031}). */
static void RedrawOriginConfirm(void)
{
   PrintPadded_P(0, s_originToReference ? PSTR("Origin=calib.?") : PSTR("Origin = here?"));
   PrintPadded_P(1, PSTR("SW=ok  hold=no"));
}

/**
 * @brief "Calibrate machine" steps 1-2 (@req{REQ-CoreLR-D-LcdJoystickUI-0010}):
 * X axis cycles the coarse/fine step size (edge-triggered, same pattern
 * as Adjustment mode's motor-select toggle), Y axis adjusts the live
 * value by that step (repeat-gated, same pattern as the pen wizard).
 */
static void HandleCalMachineParamsInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   int8_t dx;
   int8_t dy;
   uint8_t xDeflected;

   if (shortPress)
   {
      if (s_calMachineStep == 0u)
      {
         s_calMachineMotorDistanceUm = s_calMachineValueUm;
         s_calMachineStep = 1;
         s_calMachineValueUm = Calibration_GetReferenceCableLengthUm();
         s_calMachineStepIndex = 0;
      }
      else
      {
         Calibration_Params_t params;

         Calibration_GetParams(&params);
         params.MotorDistanceUm = s_calMachineMotorDistanceUm;
         params.ReferenceCableLengthUm = s_calMachineValueUm;
         /* recomputes ReferencePositionX/Y, REQ-CoreLR-D-Calibration-0001; refused when the reference would be outside the travel zone:
          * stay on the step, say so, and let the value be changed (REQ-CoreLR-D-LcdJoystickUI-0029) */
         if (Calibration_SetParams(&params))
         {
            EnterJogAdjustment(); /* step 3 */
         }
         else
         {
            s_calMachineRefused = 1;
         }
      }
      return;
   }
   if (longPress)
   {
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; /* cancel the whole wizard, discard both values */
      return;
   }

   dx = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN);
   xDeflected = (uint8_t)(dx != 0);
   if (xDeflected && !s_xWasDeflected)
   {
      /* right = a coarser step, left = a finer one, once per gesture, and no wrap: the ends are ends (REQ-CoreLR-D-LcdJoystickUI-0022) */
      if ((dx > 0) && (s_calMachineStepIndex < (uint8_t)(LCDJOYSTICKUI_CAL_STEP_COUNT - 1u)))
      {
         s_calMachineStepIndex++;
      }
      else if ((dx < 0) && (s_calMachineStepIndex > 0u))
      {
         s_calMachineStepIndex--;
      }
   }
   s_xWasDeflected = xDeflected;

   dy = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);
   if (dy != 0 && RepeatGate(nowTick))
   {
      int32_t stepUm = (int32_t)pgm_read_dword(&kCalStepSizesUm[s_calMachineStepIndex]);

      s_calMachineRefused = 0;
      s_calMachineValueUm += (dy < 0) ? stepUm : -stepUm;   /* stick UP increases the value (the maintainer's bench, 2026-10-10: it used to be the other way) */
      if (s_calMachineValueUm < 0)
      {
         s_calMachineValueUm = 0; /* a physical distance can't be negative */
      }
      if (s_calMachineValueUm > LCDJOYSTICKUI_CAL_MAX_UM)
      {
         s_calMachineValueUm = LCDJOYSTICKUI_CAL_MAX_UM;
      }
   }
}

/**
 * @brief Target step rate, in steps per second, for a Y deflection
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0014}).
 *
 * The Control jog's response (-0027: it was a second copy of the same quadratic, now there is one shape, JogCurve_Fraction()), so the rate rises
 * slowly at first -- a large stick movement near the dead zone barely changes it, which is what fine positioning needs, while full deflection
 * still reaches MAX.
 *
 *   rate = MIN + (MAX - MIN) * shape(|deflection|)        shape: 0 at the edge of the dead zone, 1.0 at full scale
 *
 * Integer throughout; the widest intermediate is 390 * 65535, inside uint32.
 */
static uint16_t AdjustTargetRate(int16_t deflection)
{
   uint16_t magnitude = (uint16_t)((deflection < 0) ? -deflection : deflection);

   /* The shape is the Control jog's (JogCurve_Fraction(), REQ-CoreLR-D-LcdJoystickUI-0027): one table, so that retuning it retunes both jogs. */
   return (uint16_t)((uint32_t)LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S
                   + (((uint32_t)(LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S - LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S)
                       * (uint32_t)JogCurve_Fraction(magnitude)) >> 16));
}

/**
 * @brief Move the commanded rate towards `target`, once per elapsed
 * SysTick (@req{REQ-CoreLR-D-LcdJoystickUI-0015}).
 *
 * Accelerating gradually is what lets the motor exceed its pull-in
 * rate. Decelerating is immediate: slowing down never loses sync, and
 * making the stick feel laggy on the way back would be a poor trade.
 */
static void AdjustRampToward(uint16_t target, uint8_t nowTick)
{
   uint8_t elapsed = (uint8_t)(nowTick - s_adjustLastRateTick);

   s_adjustLastRateTick = nowTick;

   if (s_adjustRate == 0u)
   {
      /* Starting from rest: begin inside the pull-in region, unless the
       * request is already slower than that. */
      s_adjustRate = (target < (uint16_t)LCDJOYSTICKUI_ADJUST_START_STEPS_PER_S)
                   ? target : (uint16_t)LCDJOYSTICKUI_ADJUST_START_STEPS_PER_S;
      return;
   }
   if (target <= s_adjustRate)
   {
      s_adjustRate = target; /* slowing down needs no ramp */
      return;
   }
   {
      /* SYSTICK_PERIOD_MS per tick, so delta = accel * ms / 1000. */
      uint32_t delta = ((uint32_t)LCDJOYSTICKUI_ADJUST_ACCEL_STEPS_PER_S2
                        * elapsed * SYSTICK_PERIOD_MS) / 1000u;

      if ((uint32_t)s_adjustRate + delta >= (uint32_t)target)
      {
         s_adjustRate = target;
      }
      else
      {
         s_adjustRate = (uint16_t)(s_adjustRate + delta);
      }
   }
}

/**
 * Entries to keep queued ahead of the ISR, from the current step rate
 * (LCDJOYSTICKUI_ADJUST_BUFFER_MS).
 *
 * Every entry this mode emits carries the same hold, so a duration
 * converts to a count with one multiply -- no running total of queued
 * ticks to maintain, and nothing to keep in step with the ISR.
 *
 *      rate (steps/s) x buffer (ms)
 *      ----------------------------  , rounded up, clamped to the ring
 *                1000
 *
 * The floor of 2 keeps a successor queued behind the entry being
 * executed at any rate, which is the whole point; the ceiling is what
 * the ring can hold (one slot always stays free to distinguish full
 * from empty).
 */
static uint8_t AdjustQueueDepth(uint16_t rate)
{
   uint16_t depth = (uint16_t)(((uint32_t)rate * LCDJOYSTICKUI_ADJUST_BUFFER_MS + 999u) / 1000u);

   if (depth < 2u)
   {
      depth = 2u;
   }
   if (depth > (uint16_t)(STEPPER_QUEUE_SIZE - 1u))
   {
      depth = (uint16_t)(STEPPER_QUEUE_SIZE - 1u);
   }
   return (uint8_t)depth;
}

/**
 * @brief Hold, in Timer1 counts, for the next queue entry
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0015}, simplified 2026-10-06).
 *
 * This used to be **dithered**: an integer tick count of 250 us could not
 * express a smooth rate at the top of the range (only 400, 444, 500, 571, 667
 * and 800 steps/s between 400 and 800), so the period was carried in 1/256
 * tick and accumulated. The hold is now in 0.5 us counts, so the rounding
 * error of a plain division is at most 0.25 us -- 0.01 % of a 2.5 ms step --
 * and the accumulator, its RAM and its code are gone.
 */
static uint32_t AdjustNextHold(void)
{
   return (STEPPER_COUNTS_PER_S + (uint32_t)(s_adjustRate / 2u)) / s_adjustRate;
}

/**
 * @brief "Adjustment" jog mode, step 3 of "Calibrate machine" (@req{REQ-CoreLR-D-LcdJoystickUI-0005}): X selects the motor, Y pays its cable out or in at a rate
 * that follows the stick (@req{REQ-CoreLR-D-LcdJoystickUI-0014}), through the step queue topped up to a depth (@req{REQ-CoreLR-D-LcdJoystickUI-0016}).
 * A short press confirms that the head is at the reference position (ConfirmCalibration()); a long press goes back to the menu. From the first raw step the
 * machine is uncalibrated (@req{REQ-CoreLR-D-Calibration-0009}).
 * @param shortPress  the button was pressed and released.
 * @param longPress   the button was held past the long-press time.
 * @param nowTick     SysTick_Get() of this pass.
 */
static void HandleJogAdjustmentInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   int8_t dx;
   int16_t dy;


   if (shortPress)
   {
      ConfirmCalibration();
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
      return;
   }
   if (longPress)
   {
      s_uiMode = LCDJOYSTICKUI_MODE_MENU;
      return;
   }

   /* X axis SELECTS the motor: left = L, right = R, centred = unchanged (REQ-CoreLR-D-LcdJoystickUI-0005, -0022). It used to toggle, so pushing right while R was
    * selected went back to L; now the ends are ends. The stick must reach the ternary threshold (LCDJOYSTICKUI_TERNARY_ON). */
   dx = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN);
   if (dx < 0)
   {
      s_adjustSelectedMotor = STEPPER_MOTOR_L;
   }
   else if (dx > 0)
   {
      s_adjustSelectedMotor = STEPPER_MOTOR_R;
   }

   /*
    * Y axis jogs the selected motor, at a rate set by how far the
    * stick is pushed (REQ-CoreLR-D-LcdJoystickUI-0014). It used to be
    * one step per repeat interval -- 5 steps/s, seven minutes per
    * revolution -- which is why reaching the reference position was
    * unusable.
    *
    * The repeat gate is deliberately not used here: it is a *UI*
    * rate-limit, meant for scrolling a list one item at a time, and
    * its 200ms granularity cannot express a step rate. The rate lives
    * in the queue entry's hold count instead, which is what the
    * Stepper queue is for.
    *
    * The non-moving motor's coil bits are recomputed from its own
    * unchanged step count every time, so it keeps holding its phase
    * correctly instead of drifting or de-energising.
    */
   dy = LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_Y_PIN);
   if (dy != 0)
   {
      AdjustRampToward(AdjustTargetRate(dy), nowTick);

      /* Top the queue up to a fixed depth, rather than refilling only
       * once it has run dry (REQ-CoreLR-D-LcdJoystickUI-0016). The
       * old rule kept the ring empty for nearly the whole of every
       * step, leaving one step period between a slow loop() pass and a
       * frozen coil pattern -- see LCDJOYSTICKUI_ADJUST_QUEUE_DEPTH for
       * the measurement and why the resulting stall was mistaken for
       * the motor losing torque. */
      {
         int32_t direction = (dy > 0) ? 1 : -1;
         uint8_t queued = Stepper_QueuedCount();
         uint8_t depth = AdjustQueueDepth(s_adjustRate);

         while (queued < depth)
         {
            /* Advance a *copy* of the step counters and commit it only
             * once the queue has accepted the entry. Incrementing first
             * would skip a phase on every refusal, which is the same
             * defect that made an earlier bench test manufacture the
             * stutter it was supposed to be diagnosing. */
            int32_t nextL = s_adjustStepCountL;
            int32_t nextR = s_adjustStepCountR;
            uint8_t coilL;
            uint8_t coilR;

            if (s_adjustSelectedMotor == STEPPER_MOTOR_L)
            {
               nextL += direction;
            }
            else
            {
               nextR += direction;
            }
            coilL = Motion_StepToCoilBits(nextL, STEPPER_MOTOR_L_REVERSED);
            coilR = Motion_StepToCoilBits(nextR, STEPPER_MOTOR_R_REVERSED);
            if (!Stepper_RequestMotion(coilL, coilR, AdjustNextHold(), STEPPER_PEN_NONE))
            {
               break; /* halted or full: no step taken, counters untouched */
            }
            s_adjustStepCountL = nextL;
            s_adjustStepCountR = nextR;
            queued++;
            if (Calibration_IsCalibrated())
            {
               /* The motors have moved and Motion has not followed: the position it tracks -- and that the position keeper would store after 2 s --
                * is no longer where the head is. From the first raw step the machine is uncalibrated, whatever way this mode is left
                * (REQ-CoreLR-D-Calibration-0009); only the confirmation brings it back. */
               Calibration_Invalidate();
            }
         }
      }
   }
   else
   {
      /* Stick centred: drop the rate so the next deflection starts its
       * ramp from rest again. Also reset the
       * starvation counter, so the figure on screen always describes
       * the movement about to start rather than every attempt since
       * boot -- including the one unavoidable count each motion ends
       * with, which made the raw total unreadable. */
#if CORELR_DEBUG_MISS_COUNT
      Stepper_ResetStarvedCount();
#endif
      s_adjustRate = 0;
      s_adjustLastRateTick = nowTick;
   }
}

/**
 * @brief "Calibrate pen" wizard (@req{REQ-CoreLR-D-LcdJoystickUI-0007}): Y changes the servo angle, shown live on the pen; a short press confirms step 0 (pen up)
 * then step 1 (pen down), which stores both angles; a long press cancels and parks the pen at its stored angle.
 * @param shortPress  the button was pressed and released.
 * @param longPress   the button was held past the long-press time.
 * @param nowTick     SysTick_Get() of this pass.
 */
static void HandlePenWizardInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   int8_t dy;

   if (shortPress)
   {
      if (s_penWizardStep == 0u)
      {
         s_penWizardUpAngle = s_penWizardAngle;
         s_penWizardStep = 1;
         {
            uint8_t up, down;

            Calibration_GetPenAngles(&up, &down);
            s_penWizardAngle = down;
         }
         PenControl_PreviewAngle(s_penWizardAngle);
      }
      else
      {
         Calibration_Params_t params;

         Calibration_GetParams(&params);
         params.PenUpAngle = s_penWizardUpAngle;
         params.PenDownAngle = s_penWizardAngle;
         Calibration_SetParams(&params);
         PenControl_PreviewAngle(params.PenUpAngle); /* park the pen physically up before leaving the wizard */
         s_uiMode = LCDJOYSTICKUI_MODE_MENU;
      }
      return;
   }
   if (longPress)
   {
      /* Cancel: discard the live preview, park the pen back at its real
       * setpoint. PenControl's tracked up/down flags are untouched by
       * PreviewAngle() either way -- harmless here since this wizard is
       * only reachable from the idle main menu, never mid-job. */
      {
         uint8_t up, down;

         Calibration_GetPenAngles(&up, &down);
         PenControl_PreviewAngle(up);
      }
      s_uiMode = LCDJOYSTICKUI_MODE_MENU;
      return;
   }

   dy = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);
   if (dy != 0 && RepeatGate(nowTick))
   {
      int16_t next = (int16_t)s_penWizardAngle + ((dy > 0) ? (int16_t)LCDJOYSTICKUI_PEN_ANGLE_STEP : -(int16_t)LCDJOYSTICKUI_PEN_ANGLE_STEP);

      if (next < 0)
      {
         next = 0;
      }
      if (next > 180)
      {
         next = 180;
      }
      s_penWizardAngle = (uint8_t)next;
      PenControl_PreviewAngle(s_penWizardAngle);
   }
}

#if LCDJOYSTICKUI_HAS_SD
/**
 * @brief The SD file browser's input (@req{REQ-CoreLR-D-LcdJoystickUI-0008}, @req{REQ-CoreLR-D-LcdJoystickUI-0022}): what each press and each stick movement does.
 *
 * \dot
 * digraph SdBrowser {
 *    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
 *    edge [fontname="Helvetica", fontsize=9];
 *    MENU   [label="main menu"];
 *    LIST   [label="LCDJOYSTICKUI_MODE_SD_BROWSER:\none entry of the directory shown\n(directories first)"];
 *    STATUS [label="status screen"];
 *    MENU   -> LIST   [label="Load file"];
 *    LIST   -> LIST   [label="stick up / down: previous / next entry\n(no wrap-around)"];
 *    LIST   -> LIST   [label="short press on a directory: enter it (first entry);\nrefused, with a message, if the path would not fit"];
 *    LIST   -> LIST   [label="long press below the root: up one level"];
 *    LIST   -> LIST   [label="short press with nothing listed: retry the mount\n(blocks up to about 2 s)"];
 *    LIST   -> LIST   [label="short press on a file that does not open: stay"];
 *    LIST   -> STATUS [label="short press on a file that opens:\nJOB_START"];
 *    LIST   -> MENU   [label="long press at the root"];
 * }
 * \enddot
 */
static void HandleSdBrowserInput(uint8_t shortPress, uint8_t longPress, uint8_t nowTick)
{
   if (shortPress)
   {
      if (s_browserTotal == 0u)
      {
         /* Nothing to select, so the short press is free for something
          * useful: retry the mount, which is how a card inserted after
          * power-on becomes usable without a reset. Blocks up to ~2s
          * when there is still no card (sdfatlib's SD_INIT_TIMEOUT) --
          * acceptable only because this is an explicit user action and
          * no job can be running here (REQ-CoreLR-D-LcdJoystickUI-0012). */
         if (SDReader_CardStatus() != SDREADER_CARD_OK)
         {
            (void)SDReader_Mount();
            s_browserIndex = 0;
            RefreshBrowserEntry();
         }
         return;
      }
      if (s_browserTotal > 0u)
      {
         uint8_t wasDirectory = s_browserCurrentIsDirectory;
         uint8_t jobOpened = SDReader_BrowserEnter(s_browserIndex);

         if (wasDirectory && SDReader_BrowserEnterRefused())
         {
            s_browserRefused = 1; /* the path would not fit: nothing changed, the entry stays selected, and the screen says why */
         }
         else if (wasDirectory)
         {
            s_browserIndex = 0; /* "reset the list to its first entry", REQ-CoreLR-D-LcdJoystickUI-0008 */
            RefreshBrowserEntry();
         }
         else if (jobOpened)
         {
            /* The UI no longer declares the machine RUNNING -- it says
             * "the user picked this file and it opened", and the state
             * machine decides (REQ-CoreLR-D-LcdJoystickUI-0012). */
            s_pendingEvent = (uint8_t)LCDJOYSTICKUI_EVENT_JOB_START;
            s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
         }
         /* else: open failed -- stay in the browser rather than
          * pretending a job started. */
      }
      return;
   }
   if (longPress)
   {
      if (SDReader_BrowserAtRoot())
      {
         s_uiMode = LCDJOYSTICKUI_MODE_MENU;
      }
      else
      {
         SDReader_BrowserGoUp();
         s_browserIndex = 0;
         RefreshBrowserEntry();
      }
      return;
   }

   if (s_browserTotal > 0u)
   {
      int8_t dy = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);

      if (dy != 0 && RepeatGate(nowTick))
      {
         if ((dy > 0) && (s_browserIndex + 1u < s_browserTotal))
         {
            s_browserIndex++;                      /* no wrap-around: the last file is the end of the list (REQ-CoreLR-D-LcdJoystickUI-0022) */
         }
         else if ((dy < 0) && (s_browserIndex > 0u))
         {
            s_browserIndex--;
         }
         RefreshBrowserEntry();
      }
   }
}

#endif /* LCDJOYSTICKUI_HAS_SD */

void LcdJoystickUI_Init(void)
{
   pinMode(LCDJOYSTICKUI_SW_PIN, INPUT_PULLUP);

   (void)LcdI2c_Init(); /* a display that does not answer is not fatal: the frame stays dirty and Task() keeps offering it */
   LcdGlyphs_Load();   /* the pen's pictograms, into the display's CGRAM, if they are built in (REQ-CoreLR-D-LcdJoystickUI-0032) */

   s_machineState = Calibration_IsCalibrated() ? LCDJOYSTICKUI_STATE_IDLE : LCDJOYSTICKUI_STATE_UNCAL;
   s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   s_swWasDown = 0;
   s_menuSelectedIndex = 0;

   /* Frame buffer starts blank and fully dirty, so the first frames
    * push a known state onto a display whose contents we cannot read
    * back. s_lcdCursor = CELLS means "unknown", forcing the first
    * positioning command. */
   memset(s_frame, ' ', sizeof(s_frame));
   memset(s_dirty, 0xFF, sizeof(s_dirty));
   s_lcdCursor = (uint8_t)LCDJOYSTICKUI_CELLS;
   s_lastRefreshTick = SysTick_Get();

   RedrawStatusScreen();
}

uint8_t LcdJoystickUI_IsBusy(void)
{
   return (uint8_t)((s_uiMode != LCDJOYSTICKUI_MODE_STATUS) && (s_uiMode != LCDJOYSTICKUI_MODE_MENU));
}

int16_t LcdJoystickUI_ReadAxis(uint8_t analogPin)
{
   int16_t raw = (int16_t)analogRead(analogPin);
   int16_t deflection = (int16_t)(raw - LCDJOYSTICKUI_ADC_CENTER);

   if (analogPin == LCDJOYSTICKUI_X_PIN ? LCDJOYSTICKUI_X_INVERT : LCDJOYSTICKUI_Y_INVERT)
   {
      deflection = (int16_t)-deflection;   /* this unit reads the other way round (HwConfig.h): positive = right / down, always */
   }

   if (deflection > -LCDJOYSTICKUI_DEADZONE && deflection < LCDJOYSTICKUI_DEADZONE)
   {
      return 0;
   }
   return deflection;
}

int8_t LcdJoystickUI_ReadAxisTernary(uint8_t analogPin)
{
   static int8_t s_state[2];                              /* per axis: X, Y */
   int8_t *state = &s_state[(analogPin == LCDJOYSTICKUI_X_PIN) ? 0 : 1];
   int16_t d = LcdJoystickUI_ReadAxis(analogPin);
   int16_t magnitude = (d < 0) ? -d : d;

   if (magnitude >= LCDJOYSTICKUI_TERNARY_ON)
   {
      *state = (int8_t)((d > 0) ? 1 : -1);
   }
   else if ((magnitude < LCDJOYSTICKUI_TERNARY_OFF) || ((d > 0) != (*state > 0)))
   {
      *state = 0;                                         /* released, or the stick went through the middle to the other side */
   }
   else
   {
      /* between OFF and ON, on the same side: hold what it was (the hysteresis) */
   }
   return *state;
}

LcdJoystickUI_Event_t LcdJoystickUI_TakeEvent(void)
{
   LcdJoystickUI_Event_t event = (LcdJoystickUI_Event_t)s_pendingEvent;

   s_pendingEvent = (uint8_t)LCDJOYSTICKUI_EVENT_NONE;
   return event;
}

void LcdJoystickUI_SetMachineState(LcdJoystickUI_MachineState_t state)
{
   /* When the machine's state changes under an open menu -- a job started, paused or ended from the serial link -- the menu is stale (what it
    * offers depends on the state), so the screen goes back to the status view (REQ-CoreLR-D-LcdJoystickUI-0026). Other modes are left alone:
    * they keep the serial link out (IsBusy), so the state cannot change under them. */
   if ((state != s_machineState) && (s_uiMode == LCDJOYSTICKUI_MODE_MENU))
   {
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }
   s_machineState = state;
}

/* CORELR_NOINLINE: keeps this out of main()'s permanent frame -- see SwConfig.h */
CORELR_NOINLINE
void LcdJoystickUI_Task(void)
{
   uint8_t nowTick = SysTick_Get();
   uint8_t swRawDown = (uint8_t)(digitalRead(LCDJOYSTICKUI_SW_PIN) == LOW);
   uint8_t shortPress = 0;
   uint8_t longPress = 0;

   /* SW short/long press: detected once per press-release cycle, timed
    * via SysTick (REQ-CoreLR-D-LcdJoystickUI-0003). */
   if (swRawDown && !s_swWasDown)
   {
      s_swPressStartTick = nowTick;
   }
   else if (!swRawDown && s_swWasDown)
   {
      if (SysTick_Elapsed(s_swPressStartTick, LCDJOYSTICKUI_LONGPRESS_TICKS))
      {
         longPress = 1;
      }
      else
      {
         shortPress = 1;
      }
   }
   s_swWasDown = swRawDown;

   switch (s_uiMode)
   {
      case LCDJOYSTICKUI_MODE_STATUS:
         if (shortPress)
         {
            if (s_machineState == LCDJOYSTICKUI_STATE_ERROR)
            {
               /* REQ-CoreLR-D-GCodeParser-0006: in the error state a
                * short press from the status screen acknowledges, and
                * does not open the menu -- the user has to clear the
                * error before anything else. */
               s_pendingEvent = (uint8_t)LCDJOYSTICKUI_EVENT_ERROR_ACK;
            }
            else
            {
               EnterMainMenu();
            }
         }
         break;
      case LCDJOYSTICKUI_MODE_MENU:
         HandleMainMenuInput(shortPress, longPress, nowTick);
         break;
      case LCDJOYSTICKUI_MODE_JOG_CONTROL:
         HandleJogControlInput(shortPress, longPress, nowTick);
         break;
#if CORELR_MENU_HOME
      case LCDJOYSTICKUI_MODE_HOMING:
         HandleHomingInput((uint8_t)(shortPress | longPress));
         break;
#endif
      case LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM:
         HandleOriginConfirmInput(shortPress, longPress);
         break;
      case LCDJOYSTICKUI_MODE_CAL_PARAMS:
         HandleCalMachineParamsInput(shortPress, longPress, nowTick);
         break;
      case LCDJOYSTICKUI_MODE_JOG_ADJUST:
         HandleJogAdjustmentInput(shortPress, longPress, nowTick);
         break;
      case LCDJOYSTICKUI_MODE_CAL_PEN:
         HandlePenWizardInput(shortPress, longPress, nowTick);
         break;
#if LCDJOYSTICKUI_HAS_SD
      case LCDJOYSTICKUI_MODE_SD_BROWSER:
         HandleSdBrowserInput(shortPress, longPress, nowTick);
         break;
#endif
      default:
         break;
   }

   /* Transmit a bounded slice of the pending frame on every call --
    * this is what keeps the LCD from monopolising loop()
    * (REQ-CoreLR-D-LcdJoystickUI-0011). */
   FlushStep();

   /* Re-render only when all three conditions hold:
    *   - the refresh period has elapsed        (rate limit)
    *   - the previous frame is fully sent      (no tearing, self-limiting)
    *   - content actually changed              (checked by PrintPadded's
    *                                            per-character diff: an
    *                                            identical re-render sets
    *                                            no dirty bit, so nothing
    *                                            is transmitted)
    * The middle condition is what makes tearing structurally
    * impossible: a frame is never re-rendered while still part-way
    * onto the wire, so the display can never mix two frames. It also
    * makes the refresh self-limiting -- it cannot outrun the I2C bus,
    * whatever LCDJOYSTICKUI_REFRESH_MS is set to. */
   if (!FrameIsClean() || !SysTick_Elapsed(s_lastRefreshTick, LCDJOYSTICKUI_REFRESH_TICKS))
   {
      return;
   }
   s_lastRefreshTick = nowTick;

   switch (s_uiMode)
   {
      case LCDJOYSTICKUI_MODE_STATUS:  RedrawStatusScreen();  break;
      case LCDJOYSTICKUI_MODE_MENU:      RedrawMainMenu();      break;
#if CORELR_MENU_HOME
      case LCDJOYSTICKUI_MODE_HOMING:         RedrawStatusScreen();  break; /* same readout, tag H */
#endif
      case LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM: RedrawOriginConfirm(); break;
      case LCDJOYSTICKUI_MODE_JOG_CONTROL:    RedrawStatusScreen();  break; /* reuses the status layout: live position/L,R feedback while jogging */
      case LCDJOYSTICKUI_MODE_CAL_PARAMS: RedrawCalMachineParams(); break;
      case LCDJOYSTICKUI_MODE_JOG_ADJUST: RedrawJogAdjustment(); break;
      case LCDJOYSTICKUI_MODE_CAL_PEN:  RedrawPenWizard();     break;
#if LCDJOYSTICKUI_HAS_SD
      case LCDJOYSTICKUI_MODE_SD_BROWSER:     RedrawSdBrowser();     break;
#endif
      default:                       break;
   }
}
