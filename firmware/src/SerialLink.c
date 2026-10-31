/*
 * SerialLink.c
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

#include "SwConfig.h"

/* Serial variant only (the file's own documentation is inside the condition too: the documentation of the SD image does not list an empty file). */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL

/**
 * @file SerialLink.c
 * @brief See SerialLink.h.
 */

#include "SerialLink.h"

#include <avr/pgmspace.h>
#include "Uart.h"
#include "GCodeParser.h"
#include "Motion.h"
#include "Calibration.h"
#if CORELR_DEBUG_JOBSTATE
#include "Stepper.h"   /* Stepper_QueuedCount(), Stepper_DebugFlags(): without it they are implicit int functions and the high byte is garbage */
#endif
#include "TextFmt.h"
#include "Version.h"

/*
 * How much of the transmit ring a reply needs *free* before it is started, so that it is written whole and never blocks.
 * A G-code line's reply is at most "[MSG:Pgm End]\r\nok\r\n" (19) or "error:36\r\n" (10); every `$` reply and the status
 * report are composed in a buffer of the ring's size, and a `$` reply is started only on an empty ring.
 */
#define SERIALLINK_NEED_REPLY   24u   /**< free bytes of the transmit ring that a G-code line's reply needs before it is started */
#if CORELR_DEBUG_JOBSTATE
#define SERIALLINK_NEED_STATUS  128u   /**< same for a status report (with the bench diagnostics) */
#else
#define SERIALLINK_NEED_STATUS  96u   /**< same for a status report: the longest one is about 90 characters, with WCO */
#endif
#define SERIALLINK_NEED_DOLLAR  UART_TX_SIZE   /**< a `$` reply is composed in a buffer of the ring's size and started only on an empty ring */
#if UART_TX_SIZE < 128u
#error "UART_TX_SIZE below 128: the longest status report (about 90 characters, with WCO) and the longest $ reply could not be sent"
#endif

/** grbl status numbers that come from the link, not from the parser (docs/protocol/SerialProtocol.md @protosec{4-6,4.6}). */
#define SERIALLINK_ERR_LINE_OVERFLOW  11u
#define SERIALLINK_ERR_INVALID_STATEMENT 3u   /**< `error:3`: a `$` command that is not known */
#define SERIALLINK_ERR_NOT_IDLE       8u   /**< `error:8`: not allowed during a job (a `$J=` jog or `$H` while the job state is not idle) */
#define SERIALLINK_ERR_LOCKED         9u   /**< `error:9`: locked out - uncalibrated, in the error state, or an LCD mode that moves the machine (grbl's "locked") */

/** What the welcome sequence still has to send. */
#define WELCOME_NONE   0u
#define WELCOME_LINE   1u   /**< the line senders recognise grbl by */
#define WELCOME_UNCAL  2u   /**< the "not calibrated" message that follows it */

/** Where the line being read is in a comment: none, to the end of the line (`;`), or to the closing parenthesis. */
#define COMMENT_NONE   0u
#define COMMENT_LINE   1u   /**< in a `;` comment: to the end of the line */
#define COMMENT_PAREN  2u   /**< in a `( )` comment: to the closing parenthesis */

/** The line being received, then executed: upper-cased, comments and blanks dropped, terminated. */
static char s_line[SERIALLINK_LINE_MAX + 1u];
static uint8_t s_len;            /**< characters of s_line so far */
static uint8_t s_ready;          /**< a complete line waits for its execution and its reply */
static uint8_t s_overflow;       /**< the line is too long, or follows lost bytes: it will be answered error:11 */
static uint8_t s_comment;        /**< COMMENT_NONE, COMMENT_LINE or COMMENT_PAREN: where the line being read is in a comment */
static uint8_t s_skipLf;         /**< the previous byte was a CR: a LF right after it is the same end of line */
static uint8_t s_realtime;       /**< real-time flags received and not yet acted on */
static uint8_t s_welcome;        /**< what the welcome sequence still has to send: WELCOME_NONE, _LINE or _UNCAL */
static uint8_t s_wcoCount;       /**< status reports since the last WCO field (@req{REQ-CoreLR-D-SerialLink-0012}) */
static int32_t s_wcoSentX;       /**< the WCO last sent: a change is sent at once */
static int32_t s_wcoSentY;       /**< same, Y */
static uint8_t s_cancelPending;  /**< Ctrl-X received: the job is being stopped, then the welcome line is sent again */

/** The welcome line, the one that line senders recognise grbl by (docs/protocol/SerialProtocol.md). */
static const char kWelcome[] PROGMEM = "\r\nGrbl 1.1h ['$' for help]\r\n";
/** The message that follows the welcome line when the machine is not calibrated. */
static const char kNotCalibrated[] PROGMEM = "[MSG:Not calibrated: use the LCD menu]\r\n";

void SerialLink_Init(void)
{
   s_len = 0;
   s_ready = 0;
   s_overflow = 0;
   s_comment = COMMENT_NONE;
   s_skipLf = 0;
   s_realtime = 0;
   s_cancelPending = 0;
   s_wcoCount = 0;
   s_welcome = WELCOME_LINE;
}

uint8_t SerialLink_HasWork(void)
{
   return (uint8_t)((s_ready != 0u) || (s_len != 0u) || (s_welcome != WELCOME_NONE) || (Uart_RxCount() != 0u));
}

/** Throw away the line being read or waiting. s_skipLf is NOT cleared: the line is dropped when it has been answered, which is
 *  after its CR was read, and the LF of that CRLF is still to come. */
static void DropLine(void)
{
   s_len = 0;
   s_ready = 0;
   s_overflow = 0;
   s_comment = COMMENT_NONE;
}

/**
 * Read bytes from the ring into the line, until one line is complete or the ring is empty
 * (docs/protocol/SerialProtocol.md @protosec{4-3,4.3}, @proto{FW-03}).
 *  - the end of a line is LF, CR, or CRLF: a LF right after a CR is dropped, or a CRLF sender would get two replies per line;
 *  - spaces, tabs and every other control character are dropped, and so are `;` comments (to the end of the line) and
 *    `( )` comments, so the limit applies to what is left;
 *  - letters are made upper case here, so that neither the parser nor the `$` commands care;
 *  - a line that exceeds SERIALLINK_LINE_MAX is read to its end and marked, never truncated: a truncated coordinate is a
 *    different, legal-looking one. A line that follows lost bytes (Uart_RxAtGap()) is marked the same way.
 * Real-time bytes are not here: the driver took them out in the interrupt.
 */
static void AssembleLine(void)
{
   while ((s_ready == 0u) && (Uart_RxCount() != 0u))
   {
      uint8_t afterLoss = Uart_RxAtGap();
      uint8_t c = Uart_RxPop();

      if (afterLoss != 0u)
      {
         s_overflow = 1u;
      }
      if ((c == (uint8_t)'\n') && (s_skipLf != 0u))
      {
         s_skipLf = 0;
      }
      else
      {
         s_skipLf = 0;
         if ((c == (uint8_t)'\n') || (c == (uint8_t)'\r'))
         {
            s_skipLf = (uint8_t)(c == (uint8_t)'\r');
            s_line[s_len] = '\0';
            s_ready = 1u;                     /* s_comment is reset by DropLine(), once the line has been answered */
         }
         else if (s_comment == COMMENT_PAREN)
         {
            if (c == (uint8_t)')')
            {
               s_comment = COMMENT_NONE;
            }
         }
         else if (s_comment == COMMENT_NONE)
         {
            if (c == (uint8_t)';')
            {
               s_comment = COMMENT_LINE;
            }
            else if (c == (uint8_t)'(')
            {
               s_comment = COMMENT_PAREN;
            }
            else if ((c > (uint8_t)' ') && (c < 0x7Fu))
            {
               if (s_len >= SERIALLINK_LINE_MAX)
               {
                  s_overflow = 1u;
               }
               else
               {
                  s_line[s_len] = (char)(((c >= (uint8_t)'a') && (c <= (uint8_t)'z')) ? (c - 32u) : c);
                  s_len++;
               }
            }
            else
            {
               /* a space, a tab or another control character: dropped */
            }
         }
         else
         {
            /* inside a ';' comment: dropped up to the end of the line */
         }
      }
   }
}

/* ---- replies ------------------------------------------------------------------------------------------------- */

/** Send `error:N` and its line end, whole: the caller has checked that the transmit ring has room (SERIALLINK_NEED_REPLY).
 *  @param number  grbl's status number (the parser's, or one of SERIALLINK_ERR_*). */
static void SendError(uint8_t number)
{
   char text[12];
   uint8_t at = TextFmt_StrP(text, 0, sizeof(text), PSTR("error:"));

   at = TextFmt_Uint(text, at, sizeof(text), number);
   at = TextFmt_StrP(text, at, sizeof(text), PSTR("\r\n"));
   Uart_TxWrite(text, at);
}

/** A `$J=` line (docs/protocol/SerialProtocol.md @proto{P2}, @proto{P6}): `$J=G21G91X10Y5F600`. */
static uint8_t IsJogLine(void)
{
   return (uint8_t)((s_len > 3u) && (s_line[0] == '$') && (s_line[1] == 'J') && (s_line[2] == '='));
}

/**
 * Execute a `$J=` line. Allowed when the machine is calibrated and Idle -- or already jogging: a continuous jog is a chain of small `$J=` lines, each accepted while
 * the one before is still moving. Not during a job (error:8), not in alarm, uncalibrated or with the LCD in a moving mode (error:9, grbl's "locked").
 * @return 1 if the planner was full: the line is kept and its reply comes with its acceptance, exactly as for a G-code line.
 */
static uint8_t HandleJog(JobState_t state, uint8_t calibrated, uint8_t lcdBusy)
{
   GCodeParser_Result_t result;

   if ((calibrated == 0u) || (lcdBusy != 0u) || (state == JOB_ERRORSTOP) || (state == JOB_ERROR))
   {
      SendError(SERIALLINK_ERR_LOCKED);
      return 0;
   }
   if (state != JOB_IDLE)
   {
      SendError(SERIALLINK_ERR_NOT_IDLE);
      return 0;
   }
   result = GCodeParser_ExecuteJog(&s_line[3]);
   if (result == GCODEPARSER_BUSY)
   {
      return 1;
   }
   if (result == GCODEPARSER_OK)
   {
      Uart_TxWriteP(PSTR("ok\r\n"));
   }
   else
   {
      SendError(result);
   }
   return 0;
}

/** `$H` (@req{REQ-CoreLR-F-SerialControl-0014}): gated like a jog; the pen goes up, the head goes to the drawing origin. @return 1 if the machine was busy: kept, offered again. */
static uint8_t HandleHome(JobState_t state, uint8_t calibrated, uint8_t lcdBusy)
{
   GCodeParser_Result_t result;

   if ((calibrated == 0u) || (lcdBusy != 0u) || (state == JOB_ERRORSTOP) || (state == JOB_ERROR))
   {
      SendError(SERIALLINK_ERR_LOCKED);
      return 0;
   }
   if (state != JOB_IDLE)
   {
      SendError(SERIALLINK_ERR_NOT_IDLE);
      return 0;
   }
   result = GCodeParser_ExecuteHome();
   if (result == GCODEPARSER_BUSY)
   {
      return 1;
   }
   if (result == GCODEPARSER_OK)
   {
      Uart_TxWriteP(PSTR("ok\r\n"));
   }
   else
   {
      SendError(result);
   }
   return 0;
}

/** The state as a sender expects to read it (docs/protocol/SerialProtocol.md @protosec{4-7,4.7}). */
static const char *StateTextP(JobState_t state, uint8_t calibrated, uint8_t lcdBusy)
{
   switch (state)
   {
      case JOB_RUNNING:
         return PSTR("Run");
      case JOB_HALTING:
      case JOB_CANCELLING:
         return PSTR("Hold:1");
      case JOB_PAUSED:
         return PSTR("Hold:0");
      case JOB_ERRORSTOP:
      case JOB_ERROR:
         return PSTR("Alarm");
      case JOB_IDLE:
      default:
         if ((lcdBusy != 0u) || !(Motion_IsIdle() && Motion_PlannerIsEmpty()))
         {
            return PSTR("Jog");        /* an LCD mode that moves the machine, or a `$J=` jog under way */
         }
         return (calibrated != 0u) ? PSTR("Idle") : PSTR("Alarm");
   }
}

/** `<Run|MPos:12.345,-67.890,0.000|Bf:2,87|FS:0,0>` (docs/protocol/SerialProtocol.md @protosec{5,5}): the executed position, three axes, always. */
static void SendStatus(JobState_t state, uint8_t calibrated, uint8_t lcdBusy)
{
   char text[SERIALLINK_NEED_STATUS];
   Motion_Position_t position = Motion_GetComputedPosition();
   Motion_Position_t wco = Motion_GetMachineOffset();
   uint8_t at = TextFmt_Char(text, 0, sizeof(text), '<');

   position.X += wco.X;                                 /* MPos is the machine frame: drawing + WCO (REQ-CoreLR-D-Motion-0030) */
   position.Y += wco.Y;

   at = TextFmt_StrP(text, at, sizeof(text), StateTextP(state, calibrated, lcdBusy));
   at = TextFmt_StrP(text, at, sizeof(text), PSTR("|MPos:"));
   at = TextFmt_Micrometres(text, at, sizeof(text), position.X);
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Micrometres(text, at, sizeof(text), position.Y);
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(",0.000|Bf:"));
   at = TextFmt_Uint(text, at, sizeof(text), Motion_PlannerFree());
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Uart_RxFree());
#if CORELR_DEBUG_JOBSTATE
   /* |D:job,motion,stepper,queued,K,N,stored -- job = JobState_t; motion bits: 1 Moving, 2 InSegment, 4 halt ramp planned, 8 planner halted, 16 outbox valid,
    * 32 pending entry; stepper bits: 1 gate closed, 2 timer armed, 4 backlog pending, 8 idle; queued = entries in the step queue; K,N = segment index and count, stored = 1 once the position keeper has stored the position */
   at = TextFmt_StrP(text, at, sizeof(text), PSTR("|D:"));
   at = TextFmt_Uint(text, at, sizeof(text), (uint16_t)state);
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Motion_DebugFlags());
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Stepper_DebugFlags());
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Stepper_QueuedCount());
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Motion_DebugSegment(0));
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Motion_DebugSegment(1));
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), Calibration_IsPositionStored());
#endif
   at = TextFmt_StrP(text, at, sizeof(text), PSTR("|FS:0,0"));
   if ((s_wcoCount == 0u) || (wco.X != s_wcoSentX) || (wco.Y != s_wcoSentY))
   {
      s_wcoSentX = wco.X;
      s_wcoSentY = wco.Y;
      s_wcoCount = 10u;
      at = TextFmt_StrP(text, at, sizeof(text), PSTR("|WCO:"));
      at = TextFmt_Micrometres(text, at, sizeof(text), wco.X);
      at = TextFmt_Char(text, at, sizeof(text), ',');
      at = TextFmt_Micrometres(text, at, sizeof(text), wco.Y);
      at = TextFmt_StrP(text, at, sizeof(text), PSTR(",0.000"));
   }
   s_wcoCount--;
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(">\r\n"));
   Uart_TxWrite(text, at);
}

/** `$G`: the parser's modal state as grbl prints it; what describes this machine (mm, one coordinate system, the XY plane) is fixed. */
static void SendParserState(void)
{
   char text[UART_TX_SIZE];
   int8_t motion;
   uint8_t incremental;
   uint8_t penDown;
   uint8_t at;

   GCodeParser_GetModal(&motion, &incremental, &penDown);
   at = TextFmt_StrP(text, 0, sizeof(text), PSTR("[GC:G"));
   at = TextFmt_Uint(text, at, sizeof(text), (uint16_t)((motion < 0) ? 0 : motion));
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(" G54 G17 G21 G9"));
   at = TextFmt_Char(text, at, sizeof(text), incremental ? '1' : '0');
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(" G94 M"));
   at = TextFmt_Char(text, at, sizeof(text), penDown ? '3' : '5');
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(" M9 T0 F0 S0]\r\nok\r\n"));
   Uart_TxWrite(text, at);
}

/** `$#`: the coordinate offsets as grbl prints them; only the work offset (what G92 sets) is not zero (@req{REQ-CoreLR-D-SerialLink-0012}). */
static void SendOffsets(void)
{
   char text[UART_TX_SIZE];
   Motion_Position_t wco = Motion_GetMachineOffset();
   uint8_t at = TextFmt_StrP(text, 0, sizeof(text), PSTR("[G54:0.000,0.000,0.000]\r\n[G92:"));

   at = TextFmt_Micrometres(text, at, sizeof(text), wco.X);
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Micrometres(text, at, sizeof(text), wco.Y);
   at = TextFmt_StrP(text, at, sizeof(text), PSTR(",0.000]\r\n[TLO:0.000]\r\nok\r\n"));
   Uart_TxWrite(text, at);
}

/** `$I`: the identity, in grbl's format. The date is that of the protocol (docs/protocol/SerialProtocol.md @protosec{6-1,6.1}), not of the build; the free text after the second colon is the
 *  name and the software version (CORELR_VERSION_STRING, Version.h: @req{REQ-CoreLR-D-Build-0020}). */
static void SendIdentity(void)
{
   char text[UART_TX_SIZE];
   uint8_t at = TextFmt_StrP(text, 0, sizeof(text), PSTR("[VER:1.1h.20261011:CoreLR " CORELR_VERSION_STRING "]\r\n[OPT:,"));

   at = TextFmt_Uint(text, at, sizeof(text), MOTION_PLANNER_DEPTH);
   at = TextFmt_Char(text, at, sizeof(text), ',');
   at = TextFmt_Uint(text, at, sizeof(text), UART_RX_SIZE);
   at = TextFmt_StrP(text, at, sizeof(text), PSTR("]\r\nok\r\n"));
   Uart_TxWrite(text, at);
}

/**
 * A `$` line (docs/protocol/SerialProtocol.md @protosec{6,6}): answered whatever the job state, except `$X`. Everything else of grbl's `$` family is
 * "invalid statement": CoreLR has no settings to write, no homing, no check mode, and its jog from the PC is a later increment.
 * @return the request for the job state machine
 */
static SerialLink_Request_t HandleDollar(JobState_t state, uint8_t calibrated)
{
   SerialLink_Request_t request = SERIALLINK_REQ_NONE;
   char second = s_line[1];

   if ((s_line[1] != '\0') && (s_line[2] != '\0'))
   {
      SendError(SERIALLINK_ERR_INVALID_STATEMENT);
   }
   else if (second == '\0')
   {
      Uart_TxWriteP(PSTR("[HLP:$$ $# $G $H $I $X ~ ! ? ctrl-x]\r\nok\r\n"));
   }
   else if (second == '$')
   {
      /* the settings a sender asks for at connection, every one fixed and true of the machine (docs/protocol/SerialProtocol.md 6.3):
       * $13=0 (millimetres) is the one UGS needs */
      Uart_TxWriteP(PSTR("$10=3\r\n$13=0\r\n$20=0\r\n$21=0\r\n$22=0\r\n$32=0\r\nok\r\n"));
   }
   else if (second == 'G')
   {
      SendParserState();
   }
   else if (second == 'I')
   {
      SendIdentity();
   }
   else if (second == '#')
   {
      SendOffsets();
   }
   else if (second == 'X')
   {
      if (calibrated == 0u)
      {
         /* unlocking would let the machine move from a position it does not know (docs/protocol/SerialProtocol.md 4.11) */
         Uart_TxWriteP(PSTR("[MSG:Not calibrated: use the LCD menu]\r\n"));
         SendError(SERIALLINK_ERR_LOCKED);
      }
      else if ((state == JOB_RUNNING) || (state == JOB_HALTING) || (state == JOB_PAUSED) || (state == JOB_CANCELLING)
            || (state == JOB_ERRORSTOP))
      {
         SendError(SERIALLINK_ERR_NOT_IDLE);
      }
      else
      {
         Uart_TxWriteP(PSTR("[MSG:Caution: Unlocked]\r\nok\r\n"));
         if (state == JOB_ERROR)
         {
            request = SERIALLINK_REQ_ACK_ERROR;
         }
      }
   }
   else
   {
      SendError(SERIALLINK_ERR_INVALID_STATEMENT);
   }
   return request;
}

/**
 * Run the line that is complete, if the state allows and the reply fits (docs/protocol/SerialProtocol.md @protosec{4-4,4.4}, @protosec{4-6,4.6}, @protosec{4-7,4.7}, @protosec{4-10,4.10}).
 *
 *   state                        a G-code line                              a `$` line
 *   ---------------------------  -----------------------------------------  ----------
 *   alarm, uncalibrated, LCD jog error:9                                    answered
 *   hold (halting/paused/...)    kept, no reply yet: runs after the resume  answered
 *   idle, running                to the parser; its result is the reply      answered
 *
 * A line the parser refuses is answered `error:N`. During a job that also stops it (the request FAIL, as before:
 * @req{REQ-CoreLR-F-GCodeInterpreter-0007}); outside one the machine stays Idle (@proto{P12}, grbl's own behaviour).
 */
static SerialLink_Request_t ExecuteLine(JobState_t state, uint8_t calibrated, uint8_t lcdBusy)
{
   SerialLink_Request_t request = SERIALLINK_REQ_NONE;
   uint8_t dollar = (uint8_t)((s_len != 0u) && (s_line[0] == '$'));
   uint8_t result = GCODEPARSER_OK;

   if (Uart_TxFree() < (dollar ? SERIALLINK_NEED_DOLLAR : SERIALLINK_NEED_REPLY))
   {
      return SERIALLINK_REQ_NONE;                        /* the reply would not fit whole: try again next pass */
   }
   if (dollar)
   {
      if (s_overflow != 0u)
      {
         SendError(SERIALLINK_ERR_LINE_OVERFLOW);
      }
      else if ((s_len == 2u) && (s_line[1] == 'H'))
      {
         if (HandleHome(state, calibrated, lcdBusy) != 0u)
         {
            return SERIALLINK_REQ_NONE;                  /* machine busy: kept, offered again */
         }
      }
      else if (IsJogLine())
      {
         if (HandleJog(state, calibrated, lcdBusy) != 0u)
         {
            return SERIALLINK_REQ_NONE;                  /* planner full: kept, offered again */
         }
      }
      else
      {
         request = HandleDollar(state, calibrated);
      }
   }
   else if ((s_len == 0u) && (s_overflow == 0u))
   {
      Uart_TxWriteP(PSTR("ok\r\n"));
   }
   else if ((lcdBusy != 0u) || (state == JOB_ERRORSTOP) || (state == JOB_ERROR) || ((state == JOB_IDLE) && (calibrated == 0u)))
   {
      SendError(SERIALLINK_ERR_LOCKED);
   }
   else if ((state == JOB_HALTING) || (state == JOB_PAUSED) || (state == JOB_CANCELLING))
   {
      return SERIALLINK_REQ_NONE;                        /* received, queued: nothing runs until the resume */
   }
   else
   {
      result = (s_overflow != 0u) ? SERIALLINK_ERR_LINE_OVERFLOW : GCodeParser_ExecuteLine(s_line);
      if (result == GCODEPARSER_BUSY)
      {
         return SERIALLINK_REQ_NONE;                     /* the line is kept and offered again: its reply comes with its acceptance */
      }
      if (result == GCODEPARSER_END)
      {
         Uart_TxWriteP(PSTR("[MSG:Pgm End]\r\nok\r\n"));
      }
      else if (result == GCODEPARSER_OK)
      {
         Uart_TxWriteP(PSTR("ok\r\n"));
      }
      else
      {
         SendError(result);
      }
      if (GCODEPARSER_IS_ERROR(result))
      {
         request = (state == JOB_RUNNING) ? SERIALLINK_REQ_FAIL : SERIALLINK_REQ_NONE;
      }
      else
      {
         request = (state == JOB_IDLE) ? SERIALLINK_REQ_START : SERIALLINK_REQ_NONE;
      }
   }
   DropLine();
   return request;
}

/** Ctrl-X: what has to happen, state by state, before the welcome line can be sent again (docs/protocol/SerialProtocol.md @protosec{4-8,4.8}). */
static SerialLink_Request_t ContinueCancel(JobState_t state)
{
   switch (state)
   {
      case JOB_RUNNING:
      case JOB_HALTING:
      case JOB_PAUSED:
         return SERIALLINK_REQ_CANCEL;
      case JOB_ERROR:
         return SERIALLINK_REQ_ACK_ERROR;               /* Ctrl-X also acknowledges the error (P8) */
      case JOB_IDLE:
         s_cancelPending = 0;
         s_welcome = WELCOME_LINE;
         return SERIALLINK_REQ_NONE;
      case JOB_CANCELLING:
      case JOB_ERRORSTOP:
      default:
         return SERIALLINK_REQ_NONE;                    /* the ramp is still running: wait for its end */
   }
}

/**
 * One pass of the link, in the order the code takes it (the first branch that applies returns; docs/protocol/SerialProtocol.md @protosec{4-1,4.1}, @protosec{4-8,4.8}):
 *
 * \dot
 * digraph LinkPass {
 *    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
 *    edge [fontname="Helvetica", fontsize=9];
 *    start   [label="SerialLink_Task(state, lcdBusy)\nreal-time bytes collected from the UART", shape=ellipse, style=solid];
 *    welcome [label="welcome line still to send?\nsend it when it fits whole;\nflush what arrives meanwhile", shape=diamond, style=solid];
 *    cancel  [label="Ctrl-X received, or a cancel pending?\ndrop the line, parser to G0;\nrequest per state (cancel / ack error / wait)", shape=diamond, style=solid];
 *    rt      [label="0x85 jog cancel, ! hold, ~ resume:\none request each, if the state allows it"];
 *    status  [label="? status report,\nif the ring has room"];
 *    line    [label="assemble one line from the ring"];
 *    exec    [label="a complete line waits?\nExecuteLine(): one reply, maybe a request", shape=diamond, style=solid];
 *    none    [label="SERIALLINK_REQ_NONE", style="rounded,dashed"];
 *    req     [label="SerialLink_Request_t\nto the job state machine", style="rounded,bold"];
 *    start   -> welcome;
 *    welcome -> none   [label="yes: nothing else this pass"];
 *    welcome -> cancel [label="no"];
 *    cancel  -> req    [label="yes"];
 *    cancel  -> rt     [label="no"];
 *    rt      -> req    [label="request"];
 *    rt      -> status [label="none"];
 *    status  -> line;
 *    line    -> exec;
 *    exec    -> req    [label="yes"];
 *    exec    -> none   [label="no"];
 * }
 * \enddot
 *
 * What ExecuteLine() answers to a complete line, by state (the first condition that holds):
 *
 * \dot
 * digraph LinkReply {
 *    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
 *    edge [fontname="Helvetica", fontsize=9];
 *    line   [label="a complete line", shape=ellipse, style=solid];
 *    room   [label="room in the transmit ring\nfor the whole reply?", shape=diamond, style=solid];
 *    kind   [label="first character", shape=diamond, style=solid];
 *    dollar [label="$ command: $H, $J=, or HandleDollar()\n(a request such as $X: ack error)"];
 *    empty  [label="empty line: ok"];
 *    locked [label="error: locked\n(LCD busy, error state, not calibrated)"];
 *    queued [label="kept, no reply yet:\nhalting, paused or cancelling"];
 *    parser [label="GCodeParser_ExecuteLine()", style="rounded,bold"];
 *    busy   [label="planner full: line kept,\nreply when accepted"];
 *    reply  [label="ok / [MSG:Pgm End] ok / error:n"];
 *    wait   [label="try again next pass", style="rounded,dashed"];
 *    line   -> room;
 *    room   -> wait   [label="no"];
 *    room   -> kind   [label="yes"];
 *    kind   -> dollar [label="$"];
 *    kind   -> empty  [label="nothing"];
 *    kind   -> locked [label="G-code, state locks it"];
 *    kind   -> queued [label="G-code, machine stopping or paused"];
 *    kind   -> parser [label="G-code, otherwise"];
 *    parser -> busy   [label="GCODEPARSER_BUSY"];
 *    parser -> reply  [label="accepted or refused:\nstart request if Idle,\nfail request if Running and refused"];
 * }
 * \enddot
 */
SerialLink_Request_t SerialLink_Task(JobState_t state, uint8_t lcdBusy)
{
   uint8_t calibrated = Calibration_IsCalibrated();
   SerialLink_Request_t request;

   s_realtime = (uint8_t)(s_realtime | Uart_TakeRealtime());

   /* the welcome line first: nothing else is sent until it has gone */
   if (s_welcome != WELCOME_NONE)
   {
      if ((s_welcome == WELCOME_LINE) && (Uart_TxFree() >= (uint8_t)(sizeof(kWelcome) - 1u)))
      {
         Uart_TxWriteP(kWelcome);
         s_welcome = (calibrated != 0u) ? WELCOME_NONE : WELCOME_UNCAL;
      }
      else if ((s_welcome == WELCOME_UNCAL) && (Uart_TxFree() >= (uint8_t)(sizeof(kNotCalibrated) - 1u)))
      {
         Uart_TxWriteP(kNotCalibrated);
         s_welcome = WELCOME_NONE;
      }
      else
      {
         /* waiting for room */
      }
      Uart_RxFlush();                                   /* what arrives before the welcome line is dropped (docs/protocol/SerialProtocol.md 4.1, 4.8) */
      s_realtime = 0;
      return SERIALLINK_REQ_NONE;
   }

   /* Ctrl-X: everything received and not yet read is discarded, the parser forgets its modal state, and whatever arrives until
    * the welcome line is dropped (docs/protocol/SerialProtocol.md 4.8) */
   if ((s_realtime & UART_RT_CANCEL) != 0u)
   {
      s_realtime = 0;
      DropLine();
      s_skipLf = 0;
      GCodeParser_ResetByLink();                         /* motion mode G0, as after a grbl reset (FW-14) */
      s_cancelPending = 1u;
   }
   if (s_cancelPending != 0u)
   {
      Uart_RxFlush();                                   /* the ring, now and on every pass until the welcome line */
      s_realtime = 0;
      return ContinueCancel(state);
   }

   if ((s_realtime & UART_RT_JOGCANCEL) != 0u)
   {
      s_realtime = (uint8_t)(s_realtime & (uint8_t)~UART_RT_JOGCANCEL);
      if ((state == JOB_IDLE) && !(Motion_IsIdle() && Motion_PlannerIsEmpty()))
      {
         return SERIALLINK_REQ_JOG_CANCEL;
      }
   }
   if ((s_realtime & UART_RT_HOLD) != 0u)
   {
      s_realtime = (uint8_t)(s_realtime & (uint8_t)~UART_RT_HOLD);
      if (state == JOB_RUNNING)
      {
         return SERIALLINK_REQ_HALT;
      }
   }
   if ((s_realtime & UART_RT_RESUME) != 0u)
   {
      s_realtime = (uint8_t)(s_realtime & (uint8_t)~UART_RT_RESUME);
      if (state == JOB_PAUSED)
      {
         return SERIALLINK_REQ_RESUME;
      }
   }
   if (((s_realtime & UART_RT_STATUS) != 0u) && (Uart_TxFree() >= SERIALLINK_NEED_STATUS))
   {
      s_realtime = (uint8_t)(s_realtime & (uint8_t)~UART_RT_STATUS);
      SendStatus(state, calibrated, lcdBusy);
   }

   AssembleLine();
   request = SERIALLINK_REQ_NONE;
   if (s_ready != 0u)
   {
      request = ExecuteLine(state, calibrated, lcdBusy);
   }
   return request;
}

#endif /* CORELR_JOB_SOURCE_SERIAL */
