/*
 * grbl_corelr.ino
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
 * @file grbl_corelr.ino
 * @brief Main sketch: wires up every module's Init()/Task() and hosts
 * the top-level state machine (idle / running / paused / error).
 *
 * A from-scratch wall-plotter firmware, using grbl as an algorithmic
 * reference and Walldraw as the hardware reference (see README.md
 * and requirements/ for the full context; neither reference is needed to
 * build this repository).
 *
 * The job/error/pause state machine required by
 * @req{REQ-CoreLR-F-Actuation-0002} and @req{REQ-CoreLR-F-GCodeInterpreter-0007}
 * lives here -- see JobState_t below.
 *
 * Job source (REQ-CoreLR-F-JobStorage vs. REQ-CoreLR-F-SerialControl):
 * selected at compile time via CORELR_JOB_SOURCE in SwConfig.h -- not
 * both at once, deliberately (see that macro's comment for why). Only
 * the SD side exists so far; the SERIAL branch is scaffolding for when
 * REQ-CoreLR-F-SerialControl is actually implemented (explicitly last
 * priority).
 */

#include "HwConfig.h"
#include "SwConfig.h"
#include "Stepper.h"
#include "SysTick.h"
#include "IsrDispatch.h"
#include "Motion.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"
#include "LcdJoystickUI.h"
#include "JobState.h"
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
#include "SDReader.h"
#elif CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
#include "Uart.h"
#include "SerialLink.h"
#else
#error "CORELR_JOB_SOURCE (SwConfig.h) must be CORELR_JOB_SOURCE_SD or CORELR_JOB_SOURCE_SERIAL"
#endif

/* The SD variant fits the Nano only with -mcall-prologues: 29 608 bytes of flash with it, 31 058 without, against 30 720
 * (REQ-CoreLR-D-Build-0003). avr-gcc defines no macro for that flag, so whoever builds says so: the CMake build defines
 * CORELR_CALL_PROLOGUES=1 together with the flag, and so does the platform.local.txt of an exported IDE sketch (REQ-CoreLR-D-Build-0010).
 * Without it the build would end in an anonymous "sketch too big"; this says why. (Host builds are not AVR builds: not checked.) */
#if defined(__AVR__) && CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD && !defined(CORELR_CALL_PROLOGUES)
#error "The SD variant needs -mcall-prologues to fit the Nano (31058 bytes without, 29608 with, limit 30720). CMake: leave CORELR_CALL_PROLOGUES=ON. Arduino IDE: copy platform.local.txt of the exported sketch (cmake --build <dir> --target export_ino) next to the AVR core's platform.txt."
#endif

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
/**
 * The pending GCode line and whether one is held.
 *
 * File scope, not loop() locals: the parser may refuse a line
 * (GCODEPARSER_BUSY) and it must then be offered again on a later
 * pass, so both have to survive across iterations. Keeping the 48-byte
 * buffer out of loop()'s stack frame also matters on its own -- it was
 * measured (2026-09-07, avr-gcc -fstack-usage) as the single biggest
 * contributor to the worst-case call chain, which at the time exceeded
 * the available stack.
 */
static SDReader_Line_t s_line;
static uint8_t s_haveLine;   /**< 1 while `s_line` holds a line that the parser has not accepted yet (it answered busy): it is offered again on the next pass; 0 when there is none */

/** Set when the parser reported a program end (M2/M30): no further line of the file is read or executed, and the job
 *  completes once what is already queued has been drawn (@req{REQ-CoreLR-F-GCodeInterpreter-0012}). */
static uint8_t s_programEnd;
#endif

/**
 * Top-level job state (@req{REQ-CoreLR-F-Actuation-0002},
 * @req{REQ-CoreLR-F-GCodeInterpreter-0007}), owned here.
 *
 * It used to live in LcdJoystickUI, which also started jobs itself --
 * that is why nothing could be gated. The UI is now a display and an
 * input device: it reports what the user asked for
 * (LcdJoystickUI_TakeEvent()) and is told what to show
 * (LcdJoystickUI_SetMachineState()).
 *
 * Three of the seven states are transient ramps. Halting, cancelling
 * and erroring all stop the same way -- Motion_Halt() bends the
 * trapezoidal profile into a deceleration ramp, and the pen is lifted
 * only once that ramp has been enqueued -- so the machine never stops
 * dead and never restarts from standstill at full speed
 * (@req{REQ-CoreLR-D-Motion-0012}).
 *
 * The transitions (the ASCII diagram that was here is replaced by this one, which says what the code does; a ramp is "transient"):
 *
 * \dot
 * digraph JobState {
 *    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
 *    edge [fontname="Helvetica", fontsize=9];
 *    rankdir=LR;
 *    JOB_IDLE      -> JOB_RUNNING    [label="start (LCD file / first accepted line)"];
 *    JOB_RUNNING   -> JOB_IDLE       [label="job complete: nothing left\nanywhere (planner, steps, link)"];
 *    JOB_RUNNING   -> JOB_HALTING    [label="halt (LCD, !)"];
 *    JOB_HALTING   -> JOB_PAUSED     [label="ramp enqueued:\npen up, gate closed"];
 *    JOB_PAUSED    -> JOB_RUNNING    [label="resume (LCD, ~):\npen put back if it was down"];
 *    JOB_RUNNING   -> JOB_CANCELLING [label="cancel (Ctrl-X; LCD, sd variant)"];
 *    JOB_HALTING   -> JOB_CANCELLING [label="cancel"];
 *    JOB_PAUSED    -> JOB_CANCELLING [label="cancel (no ramp needed)"];
 *    JOB_CANCELLING -> JOB_IDLE      [label="ramp enqueued:\npen up, rest dropped"];
 *    JOB_RUNNING   -> JOB_ERRORSTOP  [label="refused line, card pulled,\nlink failure; LCD cancel (serial variant,\nalso from HALTING and PAUSED)"];
 *    JOB_ERRORSTOP -> JOB_ERROR      [label="ramp enqueued:\npen up, gate closed"];
 *    JOB_ERROR     -> JOB_IDLE       [label="acknowledged (LCD SW,\n$X, Ctrl-X)"];
 * }
 * \enddot
 *
 * **Position is preserved across all of it**, which is the point of
 * ramping rather than flushing: nothing already enqueued is ever
 * discarded, and Motion commits its position model per *enqueued*
 * waypoint, so the physical machine converges to it as the queue
 * drains. Neither the halt/cancel path nor Motion_Abort() touches
 * Motion's current position or Calibration's calibrated flag -- so a
 * pause or a cancel never costs a recalibration.
 */
/* JobState_t is declared in JobState.h: the serial link names the states too. */
static JobState_t s_jobState = JOB_IDLE;

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
/** 0x85 (jog cancel, docs/protocol/SerialProtocol.md @proto{P2}) was received while a `$J=` jog moved the machine: Motion is ramping down, and once that ramp is enqueued the rest
 *  of the jog -- the blocks still in the planner -- is dropped (Motion_Abort()), exactly as "Return home" is stopped from the LCD. It is not a job state: no job runs. */
static uint8_t s_jogCancelling;
#endif

/** Whether the pen was down when a halt lifted it, so resume can put
 *  it back (@req{REQ-CoreLR-D-LcdJoystickUI-0012}). */
static uint8_t s_penWasDownAtHalt;

/** @brief Begin the shared ramped stop, then head for `nextState`. */
static void BeginRampedStop(JobState_t nextState)
{
   s_penWasDownAtHalt = PenControl_IsRequestedDown();
   Motion_Halt();             /* decelerate into a virtual segment end */
   /* NOT Stepper_HaltRequests() here: the ramp is itself made of motion entries, and Motion_IsHaltRampDone() only
    * reports "done" once Motion has *enqueued* the last of them. With the gate closed from the start Stepper refuses
    * every one, the ramp never finishes, and the state never leaves HALTING / CANCELLING / ERRORSTOP (bench,
    * 2026-10-10: a pause from Candle that could not be resumed, a cancel that ended in an error nothing could
    * acknowledge, and -- the machine never being stationary in a state the position keeper accepts -- a lost
    * calibration). The gate closes at the end of the ramp, below. */
   s_jobState = nextState;
}

/*
 * The job's transitions, one function each, because there are two sources of requests in the serial variant -- the LCD's menu and
 * the link's real-time bytes and lines -- and they must do exactly the same thing (REQ-CoreLR-D-SerialLink-0006). In the SD
 * variant each has one caller and the compiler folds it in.
 */
/** @req{REQ-CoreLR-D-GCodeParser-0006}: only now may a new job be started, and it will start from the beginning. */
static void JobAckError(void)
{
   if (s_jobState == JOB_ERROR)
   {
      Motion_Abort();
      Stepper_ResumeRequests();
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
      s_haveLine = 0;
#endif
      s_jobState = JOB_IDLE;
   }
}

/**
 * @brief Pause request, from the LCD or from the link (`!`): a running job starts a ramped stop and ends in PAUSED.
 *
 * Only a running job reacts: in any other state (idle, already halting, paused, cancelling, error) the request is ignored, as grbl ignores a hold that has nothing to hold.
 * The stop is a ramp (BeginRampedStop()), not a dead stop, so no step is lost; the pen is lifted behind the ramp (see loop(), "ramp completion").
 */
static void JobHalt(void)
{
   if (s_jobState == JOB_RUNNING)
   {
      BeginRampedStop(JOB_HALTING);
   }
}

/**
 * @brief Resume request, from the LCD or from the link (`~`): a paused job goes on, in any other state nothing happens.
 *
 * The pen goes down first if it was down when the job was halted, then motion restarts from standstill (not from the speed of a segment in flight), then the job is RUNNING again:
 * PenControl_Task() runs before Motion_Task(), so the pen entry is enqueued ahead of the first resumed waypoint and the nib touches the paper before the machine moves.
 */
static void JobResume(void)
{
   if (s_jobState == JOB_PAUSED)
   {
      /* Pen first, then motion: PenControl_Task() runs before
       * Motion_Task(), so the pen-down entry is enqueued ahead of
       * any resumed waypoint and the nib is on the paper before
       * the machine moves again. */
      if (s_penWasDownAtHalt)
      {
         PenControl_RequestDown();
      }
      Motion_Resume();          /* accelerate from here, not from mid-segment speed */
      Stepper_ResumeRequests();
      s_jobState = JOB_RUNNING;
   }
}

/** Stop and abandon the job; `endState` is JOB_CANCELLING (it ends Idle) or, for the LCD's cancel in the serial variant,
 *  JOB_ERRORSTOP (it ends in the error state: see the LCD event handler). */
static void JobCancel(JobState_t endState)
{
   if ((s_jobState == JOB_RUNNING) || (s_jobState == JOB_HALTING))
   {
      BeginRampedStop(endState);
   }
   else if (s_jobState == JOB_PAUSED)
   {
      s_jobState = endState;     /* already stopped and pen already lifted */
   }
}

/* ------------------------------------------------------------------
 * Position persistence (REQ-CoreLR-D-Calibration-0008)
 *
 * The position is stored when the machine has stood still for
 * CALIBRATION_POSITION_STORE_DELAY_MS, and at once when a job ends. It
 * is invalidated, elsewhere, before the first step of any movement
 * (REQ-CoreLR-D-Stepper-0014), so "stored" here only ever has to answer
 * "has anything moved since?".
 * ------------------------------------------------------------------ */
/** The stillness delay after which the position is stored, in SysTick ticks (`CALIBRATION_POSITION_STORE_DELAY_MS` divided by `SYSTICK_PERIOD_MS`: an exact multiple, and small enough for
 *  the 8-bit counter `s_stillTicks`: both are checked below at build time). */
#define POSITION_STORE_DELAY_TICKS (CALIBRATION_POSITION_STORE_DELAY_MS / SYSTICK_PERIOD_MS)
#if (CALIBRATION_POSITION_STORE_DELAY_MS % SYSTICK_PERIOD_MS) != 0
#error "CALIBRATION_POSITION_STORE_DELAY_MS must be a multiple of SYSTICK_PERIOD_MS"
#endif
#if POSITION_STORE_DELAY_TICKS > 255
#error "the store delay does not fit the 8-bit stillness counter"
#endif

static uint8_t s_stillTicks;     /**< ticks of stillness so far, saturating */
static uint8_t s_stillLastTick;   /**< `SysTick_Get()` at the previous pass of PositionKeeper_Task(): the difference with now is the time that has elapsed since */

/**
 * @brief Store the machine's position in the EEPROM now, if there is something to store.
 *
 * Done only for a calibrated machine whose stored copy is stale (nothing has been written since it moved). Called when a job ends, at once
 * (@req{REQ-CoreLR-F-Calibration-0007}), and by PositionKeeper_Task() when the machine has stood still for POSITION_STORE_DELAY_TICKS.
 */
static void StorePositionNow(void)
{
   if (Calibration_IsCalibrated() && !Calibration_IsPositionStored())
   {
      Calibration_StoreCurrentPosition();
   }
}

/**
 * @brief Count how long the machine has stood still, and store its
 * position once that reaches the delay.
 *
 * "Still" means the job is not moving anything -- idle, paused or in
 * error -- the move generator is done or has finished its halt ramp, and
 * the step queue has run dry *including its last hold*. A job that is
 * running is excluded outright: the machine pauses between lines for a
 * few milliseconds, never for seconds, and storing mid-job would only
 * be undone by the next step.
 */
CORELR_NOINLINE
static void PositionKeeper_Task(void)
{
   uint8_t now = SysTick_Get();
   uint8_t elapsed = (uint8_t)(now - s_stillLastTick);
   uint16_t sum;

   s_stillLastTick = now;
   if (!(s_jobState == JOB_IDLE || s_jobState == JOB_PAUSED || s_jobState == JOB_ERROR)
       || !Calibration_IsCalibrated() || Calibration_IsPositionStored()
       || !Motion_IsHaltRampDone() || !Stepper_IsIdle())
   {
      s_stillTicks = 0;
      return;
   }
   sum = (uint16_t)s_stillTicks + elapsed;
   s_stillTicks = (uint8_t)((sum > 255u) ? 255u : sum);
   if (s_stillTicks >= (uint8_t)POSITION_STORE_DELAY_TICKS)
   {
      StorePositionNow();
      s_stillTicks = 0;
   }
}

/** @brief Map job state to what the status screen should display. */
static LcdJoystickUI_MachineState_t DisplayStateFor(JobState_t state)
{
   switch (state)
   {
      case JOB_RUNNING:
      case JOB_CANCELLING:   /* still moving: honestly still 'R' */
         return LCDJOYSTICKUI_STATE_RUNNING;
      case JOB_HALTING:
      case JOB_PAUSED:
         return LCDJOYSTICKUI_STATE_PAUSED;
      case JOB_ERRORSTOP:
      case JOB_ERROR:
         return LCDJOYSTICKUI_STATE_ERROR;
      case JOB_IDLE:
      default:
         return Calibration_IsCalibrated() ? LCDJOYSTICKUI_STATE_IDLE
                                           : LCDJOYSTICKUI_STATE_UNCAL;
   }
}

/**
 * @brief The Arduino entry point, run once after a reset: initialises the modules, in the order of their dependencies.
 *
 * Calibration first (the parameters and the stored position), then the tick source and the step queue, the motion planner, the pen, the parser, the SD reader (SD image) and the LCD
 * and joystick. IsrDispatch_Init() comes **last of the interrupt users**: it arms the periodic interrupt, so every tick consumer must be initialised before it
 * (@req{REQ-CoreLR-D-IsrDispatch-0001}). In the serial image the UART driver and the serial link come after everything else, so that the welcome line, which the first pass of
 * SerialLink_Task() sends, says that the machine is ready (docs/protocol/SerialProtocol.md @protosec{4-1,4.1}, @proto{FW-09}).
 *
 * `Serial` is not used anywhere: the serial image has its own driver (Uart.c), which defines the same interrupt vectors as the Arduino core's `HardwareSerial`; linking both fails
 * (@req{REQ-CoreLR-D-Uart-0001}).
 */
void setup(void)
{
    /* `Serial` is not used anywhere: the serial variant has its own driver (Uart.c), which defines the same interrupt vectors
     * as the Arduino core's HardwareSerial -- linking both fails (REQ-CoreLR-D-Uart-0001). */

   Calibration_Init();
   SysTick_Init();
   Stepper_Init();
   Motion_Init();
   PenControl_Init();
   GCodeParser_Init();
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   SDReader_Init();
#endif
   LcdJoystickUI_Init();

   /* Last: arms the periodic interrupt, so every tick consumer must
    * already be initialised (REQ-CoreLR-D-IsrDispatch-0001). */
   IsrDispatch_Init();

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   /* Last of all, so the welcome line (sent by the first SerialLink_Task()) says the machine is ready
    * (docs/protocol/SerialProtocol.md 4.1, FW-09). */
   Uart_Init();
   SerialLink_Init();
#endif
}

/**
 * @brief The Arduino main loop: one cooperative pass of every task and of the job state machine; it never blocks, and nothing here waits.
 *
 * What a pass does, in this order:
 *  1. **The tasks** that each do a little: the SD reader (SD image), the step queue's backlog, the LCD and joystick, the pen, then the motion generator. The pen comes **before** the
 *     motion: with one free queue slot the pen entry takes it, never a waypoint, so a pen change cannot be overtaken by the move that should follow it.
 *  2. **The user's request** (LcdJoystickUI_TakeEvent()): start, halt, resume, cancel, acknowledge an error. Each is one of the transitions JobHalt(), JobResume(), JobCancel(),
 *     JobAckError() (the link's requests use the same ones: the LCD and the link must do exactly the same thing, @req{REQ-CoreLR-D-SerialLink-0006}).
 *  3. **The end of a ramp**: once the halt ramp is done (Motion_IsHaltRampDone()), HALTING becomes PAUSED, CANCELLING becomes IDLE, ERRORSTOP becomes ERROR, the pen is lifted and the
 *     step queue's gate is closed or reopened accordingly. In the serial image a jog cancelled by `0x85` is dropped here, once its ramp is done.
 *  4. **The hand-off of a line while the job runs**: in the serial image the link answers its lines and real-time bytes and asks for what the job must do (SerialLink_Task()); in
 *     the SD image the line held in `s_line`, or the next one of the file, is offered to the parser, which may answer busy (the line is kept and offered again), refuse it (the job
 *     stops in the error state), or report a program end.
 *  5. **The end of the job**, when nothing is left anywhere (no held line, planner, generator and step queue all empty): the job goes back to IDLE; the SD image also lifts the pen
 *     and stores the position at once, the serial image does not (a user typing `M3` then `G1 X10` by hand keeps the pen down in between).
 *  6. **The position keeper** (PositionKeeper_Task()) and the state shown on the LCD.
 *
 * The state machine is the diagram of @ref s_jobState. Timing: a pass is short and bounded by the tasks it calls; the interrupts (the step generator at 4 kHz, the tick) run beside it.
 */
void loop(void)
{
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   uint8_t haveLine;
#endif
   LcdJoystickUI_Event_t event;

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   SDReader_Task();
#endif
   Stepper_Task();        /* REQ-CoreLR-D-Stepper-0003: drains any pending hold-request backlog */
   LcdJoystickUI_Task();
   PenControl_Task();     /* REQ-CoreLR-F-PenControl-0004: the actual servo write happens here, synchronised to motion, not at parse time.
                           * Deliberately BEFORE Motion_Task(): with one free queue slot the pen entry takes it, never a waypoint, so a pen
                           * change can never be overtaken by the motion that should follow it. */
   (void)Motion_Task();   /* REQ-CoreLR-D-Motion-0008: enqueues waypoints incrementally for the move in progress */

   /* ---- user requests ------------------------------------------- */
   event = LcdJoystickUI_TakeEvent();
   switch (event)
   {
      case LCDJOYSTICKUI_EVENT_JOB_START:
         if (s_jobState == JOB_IDLE)
         {
            Stepper_ResumeRequests();
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
            s_haveLine = 0;
            s_programEnd = 0;
            GCodeParser_Init(); /* every job starts in G90, no motion mode, pen up -- not where the last one left them */
#endif
            s_jobState = JOB_RUNNING;
         }
         break;

      case LCDJOYSTICKUI_EVENT_JOB_HALT:
         JobHalt();
         break;

      case LCDJOYSTICKUI_EVENT_JOB_RESUME:
         JobResume();
         break;

      case LCDJOYSTICKUI_EVENT_JOB_CANCEL:
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
         /* A cancel from the LCD ends in the error state, not in Idle: the host does not know it happened and goes on streaming,
          * and in Idle the next line would start drawing the rest of the file from the middle. In the error state the lines
          * in flight are answered error:9, the sender stops, and `$X`, Ctrl-X or the LCD's SW acknowledges
          * (REQ-CoreLR-D-SerialLink-0007). */
         JobCancel(JOB_ERRORSTOP);
#else
         JobCancel(JOB_CANCELLING);
#endif
         break;

      case LCDJOYSTICKUI_EVENT_ERROR_ACK:
         JobAckError();
         break;

      case LCDJOYSTICKUI_EVENT_NONE:
      default:
         break;
   }

   /* ---- ramp completion ----------------------------------------- */
   if (Motion_IsHaltRampDone())
   {
      switch (s_jobState)
      {
         case JOB_HALTING:
            PenControl_RequestUp(); /* lands behind the ramp: fires once the machine is at rest */
            Stepper_HaltRequests(); /* the ramp is enqueued: from here, no new motion until JobResume() */
            s_jobState = JOB_PAUSED;
            break;
         case JOB_CANCELLING:
            PenControl_RequestUp();
            Motion_Abort();         /* drop the rest of the move; position stays valid */
            Stepper_ResumeRequests(); /* the job is over: a cancel from PAUSED arrives here with the gate still closed, and the jog needs it open */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
            s_haveLine = 0;
#endif
            s_jobState = JOB_IDLE;
            break;
         case JOB_ERRORSTOP:
            PenControl_RequestUp();
            Motion_Abort();
            Stepper_HaltRequests(); /* nothing moves until the error is acknowledged (JobAckError() reopens it) */
            s_jobState = JOB_ERROR;
            break;
         default:
            break;
      }
   }

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   if (s_jogCancelling && Motion_IsHaltRampDone())
   {
      Motion_Abort();           /* the rest of the jog is dropped; the position stays what was commanded */
      s_jogCancelling = 0;
   }
#endif

   /* ---- job line hand-off, only while actually running ---------- */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   /* The link answers lines and real-time bytes, and asks for what the job must do (REQ-CoreLR-D-SerialLink-0006). It was told the
    * state as settled above; what it asks for takes effect before the next pass. */
   switch (SerialLink_Task(s_jobState, LcdJoystickUI_IsBusy()))
   {
      case SERIALLINK_REQ_START:
         if (s_jobState == JOB_IDLE)
         {
            Stepper_ResumeRequests();      /* no GCodeParser_Init(): the modal state carries on from the line before (G91, the pen) */
            s_jobState = JOB_RUNNING;
         }
         break;
      case SERIALLINK_REQ_HALT:
         JobHalt();
         break;
      case SERIALLINK_REQ_RESUME:
         JobResume();
         break;
      case SERIALLINK_REQ_CANCEL:
         JobCancel(JOB_CANCELLING);
         break;
      case SERIALLINK_REQ_ACK_ERROR:
         JobAckError();
         break;
      case SERIALLINK_REQ_FAIL:
         BeginRampedStop(JOB_ERRORSTOP);    /* REQ-CoreLR-D-GCodeParser-0005 */
         break;
      case SERIALLINK_REQ_JOG_CANCEL:
         if (!s_jogCancelling)
         {
            Motion_Halt();                  /* a ramp, not a dead stop */
            s_jogCancelling = 1;
         }
         break;
      case SERIALLINK_REQ_NONE:
      default:
         break;
   }
#else /* CORELR_JOB_SOURCE_SD */
   haveLine = s_haveLine;
   if ((s_jobState == JOB_RUNNING) && SDReader_JobFailed())
   {
      BeginRampedStop(JOB_ERRORSTOP);   /* the card was pulled during the job (REQ-CoreLR-D-SDReader-0012): the same stop as a refused line, acknowledged with SW */
   }
   if ((s_jobState == JOB_RUNNING) && !haveLine && !s_programEnd)
   {
      haveLine = SDReader_Dequeue(s_line);
   }
   if ((s_jobState == JOB_RUNNING) && haveLine)
   {
      /* Submit-and-retry (REQ-CoreLR-D-GCodeParser): the parser decides
       * whether it can accept a line, so a refused one must be held and
       * offered again rather than dropped. */
      GCodeParser_Result_t result = GCodeParser_ExecuteLine(s_line);

      if (result == GCODEPARSER_BUSY)
      {
         /* keep the line, offer it again next pass */
      }
      else
      {
         haveLine = 0;
         if (GCODEPARSER_IS_ERROR(result)) /* which refusal it was matters to the serial link, not here */
         {
            BeginRampedStop(JOB_ERRORSTOP); /* REQ-CoreLR-D-GCodeParser-0005 */
         }
         else if (result == GCODEPARSER_END)
         {
            s_programEnd = 1; /* M2/M30: the rest of the file is not executed (REQ-CoreLR-F-GCodeInterpreter-0012) */
         }
      }
   }
   s_haveLine = haveLine;
#endif

   /* ---- job completion ------------------------------------------ */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   if ((s_jobState == JOB_RUNNING) && !haveLine
       && (s_programEnd || SDReader_JobIsExhausted()) && Motion_PlannerIsEmpty()
       && Motion_IsIdle() && Stepper_IsEmpty())
   {
      /*
       * All five conditions matter, each covering a different stage of
       * the pipeline still holding work:
       *
       *   !haveLine                 no line held back for retry
       *   SDReader_JobIsExhausted   file at EOF and its queue empty -- or a program end (M2/M30) was executed
       *   Motion_PlannerIsEmpty     no move still planned ahead
       *   Motion_IsIdle             no move being generated
       *   Stepper_IsEmpty           no step still queued
       *
       * The planner test is not redundant with Motion_IsIdle(), which
       * reports only on the segment in flight: StartSegment() leaves
       * InProgress at 0 for a move shorter than half a grid unit
       * (~27um), so popping such a block makes Motion "idle" with up to
       * MOTION_PLANNER_DEPTH moves still queued. Without this test the
       * job would be declared finished and the pen lifted while the
       * machine carried on drawing the remainder. Found by review
       * 2026-09-11, before first power-up.
       *
       * No ramp needed here -- a segment already decelerates to VMIN at
       * its own end.
       */
      PenControl_RequestUp();
      s_jobState = JOB_IDLE;
      StorePositionNow(); /* end of file: store at once, not after the stillness delay (REQ-CoreLR-F-Calibration-0007) */
   }
#else /* CORELR_JOB_SOURCE_SERIAL */
   /* The job is over when nothing is left anywhere (docs/protocol/SerialProtocol.md 4.10, grbl's own Run -> Idle rule): the link holds
    * no line and nothing is waiting in its ring, and the planner, the generator and the step queue are empty. The pen is NOT
    * lifted (P1: a user typing M3 then G1 X10 by hand keeps the pen down in between), and the position is not stored at once:
    * the position keeper does it after its stillness delay, as after any other stop. */
   if ((s_jobState == JOB_RUNNING) && !SerialLink_HasWork() && Motion_PlannerIsEmpty()
       && Motion_IsIdle() && Stepper_IsEmpty())
   {
      s_jobState = JOB_IDLE;
   }
#endif

   PositionKeeper_Task();

   LcdJoystickUI_SetMachineState(DisplayStateFor(s_jobState));
}
