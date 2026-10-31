/*
 * GCodeParser.c
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
 * @file GCodeParser.c
 * @brief See GCodeParser.h.
 *
 * Two-pass execution (@req{REQ-CoreLR-D-GCodeParser-0001}..@req{REQ-CoreLR-D-GCodeParser-0006}):
 *   1. Tokenize the whole line into a ParsedLine_t, validating every
 *      word as we go (unrecognized letter, malformed number, or an
 *      unsupported G/M value -> a grbl status number immediately, before
 *      touching any state).
 *   2. Only once the whole line is known-valid, apply its effects:
 *      modal state, then the pen request, then the motion request.
 * This ordering means a line that's invalid partway through never
 * leaves its earlier words half-applied.
 */

#include "GCodeParser.h"
#include "SwConfig.h"
#include "Motion.h"
#include "PenControl.h"

/** One already-validated decimal number, split into a non-negative
 * magnitude (integer + fractional-thousandths) and a sign -- kept
 * separate so a value like "-0.5" doesn't lose its sign when the
 * integer part is exactly 0 (@req{REQ-CoreLR-D-GCodeParser-0002}). */
typedef struct
{
   int32_t IntegerPart;   /**< magnitude, always >= 0 */
   int32_t FractionUm;    /**< magnitude, thousandths (0-999), always >= 0 */
   uint8_t HasFraction;   /**< 1 if a '.' was present */
   uint8_t Negative;      /**< 1 if the number carried a minus sign (kept apart so that "-0.5" keeps it) */
   uint8_t Valid;         /**< 0 if no digits were found at all (malformed word) */
} GCodeNumber_t;

/** One line's tokenized words, before any effect is applied. */
typedef struct
{
   int8_t MotionWord;    /**< 0..3 for G0..G3, -1 if the line has none */
   uint8_t DistWord;     /**< 90 or 91, 0 if the line has none -- a separate modal group, so a line
                          * may carry both ("G91 G1 X10"); one shared field lost whichever came first */
   uint8_t HasM;         /**< the line has an M word */
   uint8_t MValue;       /**< its code: 3, 5, 2 or 30 (any other is refused while tokenizing) */
   uint8_t HasX;         /**< the line has an X word */
   int32_t XValueUm;     /**< its value, um */
   uint8_t HasY;         /**< the line has a Y word */
   int32_t YValueUm;     /**< its value, um */
   uint8_t HasI;         /**< the line has an I word (an arc's centre, relative to the start) */
   int32_t IValueUm;     /**< its value, um */
   uint8_t HasJ;         /**< the line has a J word */
   int32_t JValueUm;     /**< its value, um */
   uint8_t HasZ;         /**< a Z word: a pen lift for a job (@req{REQ-CoreLR-D-GCodeParser-0014}, P7), ignored by a jog */
   int32_t ZValueUm;     /**< its value, um: absolute, Z <= 0 pen down and Z > 0 up; relative, the sign of the move */
   uint8_t OriginCmd;    /**< 1 = G92, 2 = G10, 0 = neither (@req{REQ-CoreLR-D-GCodeParser-0017}): the X and Y words are then the new coordinates of the pen, not a target */
   uint8_t LValue;       /**< the L word of G10, 0 if absent */
   uint8_t PValue;       /**< the P word of G10, 0 if absent */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   uint8_t Machine;      /**< G53 on the line: its X, Y and Z are machine-frame, absolute (@req{REQ-CoreLR-D-GCodeParser-0018}, serial variant) */
#endif
} ParsedLine_t;

/**
 * @brief Parser modal state, carried across lines within a job.
 * Realizes @req{REQ-CoreLR-D-GCodeParser-0004}.
 *
 * File-local: no other module reads it, and no public signature
 * mentions it, so it does not belong in the header
 * (docs/standards/STD-FileOrganization.md, "Expose only what
 * callers need"). It was public until 2026-09-10 solely because of the
 * dead GCodeParser_GetState(), now removed. No module prefix either,
 * per the same standard's file-local rule.
 */
typedef struct
{
   uint8_t DistanceModeIncremental; /**< 0 = absolute (G90), 1 = incremental (G91) */
   uint8_t PenDown;                 /**< 0 = up (M5), 1 = down (M3) */
   int8_t MotionMode;               /**< 0..3 = the last G0/G1/G2/G3 seen, -1 = none yet --
                                          confirmed 2026-09-07: the motion words are
                                          modal too, same as G90/G91, matching real-world
                                          GCode where most lines give bare X/Y with no
                                          repeated G word. Without this, such files would
                                          silently produce no motion at all. An arc
                                          mode (2/3) is just as modal: a bare X Y I J
                                          line draws another arc (@req{REQ-CoreLR-D-GCodeParser-0009}). */
} ParserState_t;

/**
 * Forward declarations for every file-local helper, right after the
 * macros/types -- confirmed 2026-09-07 as a standing convention:
 * function bodies can then be ordered however reads best.
 */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
#define MACHINE_WORD(p) ((p)->Machine)
#else
#define MACHINE_WORD(p) 0u
#endif
static const char *ParseNumber(const char *cursor, GCodeNumber_t *outNumber);
static int32_t GCodeNumberToUm(const GCodeNumber_t *number);
static uint8_t GCodeNumberToCode(const GCodeNumber_t *number);
static GCodeParser_Result_t TokenizeLine(const char *line, ParsedLine_t *outParsed, uint8_t jog);
static int8_t PenFromZ(const ParsedLine_t *parsed);
static GCodeParser_Result_t ApplyParsedLine(const ParsedLine_t *parsed);
static int8_t EffectiveMode(const ParsedLine_t *parsed);
static void ComputeTarget(const ParsedLine_t *parsed, Motion_Position_t *outTarget);
static uint8_t OutOfReach(const Motion_Position_t *p);

/** The modal state of the job being parsed: reset by GCodeParser_Init(); changed by ApplyParsedLine() once a whole line has been accepted, and by the return-to-origin request (which lifts the pen). */
static ParserState_t s_state;

/**
 * @brief Reset modal state to the defaults the header documents: G90
 * absolute distance mode, pen up, and no motion mode established yet
 * (@req{REQ-CoreLR-D-GCodeParser-0004}).
 */
void GCodeParser_Init(void)
{
   s_state.DistanceModeIncremental = 0;
   s_state.PenDown = 0;
   s_state.MotionMode = -1;
}

void GCodeParser_GetModal(int8_t *outMotion, uint8_t *outIncremental, uint8_t *outPenDown)
{
   *outMotion = s_state.MotionMode;
   *outIncremental = s_state.DistanceModeIncremental;
   *outPenDown = s_state.PenDown;
}

/**
 * @brief Parse one decimal number (optional sign, digits, optional
 * '.' + digits) starting at cursor. Advances past the number.
 * Fractional digits beyond 3 are consumed (so tokenization of the rest
 * of the line stays correct) but discarded -- sub-micrometre precision
 * is meaningless here (@req{REQ-CoreLR-D-GCodeParser-0002}).
 */
static const char *ParseNumber(const char *cursor, GCodeNumber_t *outNumber)
{
   uint8_t digitsSeen = 0;
   uint8_t fractionDigits = 0;

   outNumber->IntegerPart = 0;
   outNumber->FractionUm = 0;
   outNumber->HasFraction = 0;
   outNumber->Negative = 0;
   outNumber->Valid = 0;

   if (*cursor == '-')
   {
      outNumber->Negative = 1;
      cursor++;
   }
   else if (*cursor == '+')
   {
      cursor++;
   }

   while (*cursor >= '0' && *cursor <= '9')
   {
      /* Saturating: a long digit string used to wrap the int32 and could land back inside the machine's reach.
       * 10^6 (a kilometre, in mm) is far beyond any reach and, times 1000 in GCodeNumberToUm(), still fits an int32
       * (REQ-CoreLR-D-GCodeParser-0010). */
      outNumber->IntegerPart = outNumber->IntegerPart * 10 + (*cursor - '0');
      if (outNumber->IntegerPart > 1000000L)
      {
         outNumber->IntegerPart = 1000000L;
      }
      digitsSeen = 1;
      cursor++;
   }

   if (*cursor == '.')
   {
      outNumber->HasFraction = 1;
      cursor++;
      while (*cursor >= '0' && *cursor <= '9')
      {
         if (fractionDigits < 3u)
         {
            outNumber->FractionUm = outNumber->FractionUm * 10 + (*cursor - '0');
            fractionDigits++;
         }
         digitsSeen = 1;
         cursor++;
      }
      while (fractionDigits < 3u)
      {
         outNumber->FractionUm *= 10;
         fractionDigits++;
      }
   }

   outNumber->Valid = digitsSeen;
   return cursor;
}

/** @brief Combine a parsed number into signed micrometres (mm -> um). */
static int32_t GCodeNumberToUm(const GCodeNumber_t *number)
{
   int32_t um = number->IntegerPart * 1000L + number->FractionUm;
   return number->Negative ? -um : um;
}

/** @brief A G or M value as one byte: 255 for anything negative or above 254, which is no supported code either way.
 * Every comparison on it is then a single 8-bit one, instead of four on an int32 (@req{REQ-CoreLR-D-GCodeParser-0012}). */
static uint8_t GCodeNumberToCode(const GCodeNumber_t *number)
{
   return (number->Negative || (number->IntegerPart > 254L)) ? 255u : (uint8_t)number->IntegerPart;
}

/**
 * @brief Pass 1 (@req{REQ-CoreLR-D-GCodeParser-0001}/@req{REQ-CoreLR-D-GCodeParser-0003}): tokenize the
 * whole line, uppercased first, into outParsed. Validates every word
 * (including that G/M values are ones this V1 interpreter actually
 * supports, @req{REQ-CoreLR-F-GCodeInterpreter-0007}) without applying any
 * state changes.
 */
static GCodeParser_Result_t TokenizeLine(const char *line, ParsedLine_t *outParsed, uint8_t jog)
{
   const char *cursor;

   outParsed->MotionWord = -1;
   outParsed->DistWord = 0;
   outParsed->HasM = 0;
   outParsed->HasX = 0;
   outParsed->HasY = 0;
   outParsed->HasI = 0;
   outParsed->HasJ = 0;
   outParsed->HasZ = 0;
   outParsed->OriginCmd = 0;
   outParsed->LValue = 0;
   outParsed->PValue = 0;
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   outParsed->Machine = 0;
#endif
   /* Values zeroed as well as their flags. A value is only ever read
    * when its flag is set, so this is not a correctness fix -- but
    * leaving them indeterminate made the compiler warn (correctly, on
    * its own terms) once GCodeParser_ExecuteLine() began tokenizing
    * before deciding acceptance, and an invariant a reader has to
    * reconstruct is worth two stores. */
   outParsed->MValue = 0;
   outParsed->XValueUm = 0;
   outParsed->YValueUm = 0;
   outParsed->IValueUm = 0;
   outParsed->JValueUm = 0;
   outParsed->ZValueUm = 0;

   cursor = line;
   while (*cursor != '\0')
   {
      char letter;
      GCodeNumber_t number;

      if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
      {
         cursor++;
         continue;
      }

      letter = *cursor;
      if ((letter >= 'a') && (letter <= 'z'))
      {
         letter = (char)(letter - 'a' + 'A'); /* case-insensitive, on the fly: no copy of the line (it was 48 bytes of stack,
                                               * and it truncated a longer line silently -- REQ-CoreLR-D-GCodeParser-0013) */
      }
      if ((letter < 'A') || (letter > 'Z'))
      {
         return GCODEPARSER_ERR_EXPECTED_LETTER;
      }
      cursor++;
      cursor = ParseNumber(cursor, &number);

      if (!number.Valid)
      {
         return GCODEPARSER_ERR_BAD_NUMBER; /* a letter with no following number */
      }

      switch (letter)
      {
         case 'G':
         {
            uint8_t g = GCodeNumberToCode(&number);

            if (number.HasFraction)
            {
               return GCODEPARSER_ERR_UNSUPPORTED; /* e.g. G92.1 -- outside V1 scope */
            }
            if (g <= 3u)
            {
               outParsed->MotionWord = (int8_t)g;
            }
            else if ((g == 90) || (g == 91))
            {
               outParsed->DistWord = (uint8_t)g;
            }
            else if ((g == 92) || (g == 10))
            {
               outParsed->OriginCmd = (uint8_t)((g == 92) ? 1u : 2u);   /* REQ-CoreLR-F-GCodeInterpreter-0017 */
            }
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
            else if (g == 53)
            {
               outParsed->Machine = 1;                                  /* REQ-CoreLR-F-GCodeInterpreter-0018 */
            }
#endif
            else if ((g != 17) && (g != 21) && (g != 54) && (g != 94))
            {
               return GCODEPARSER_ERR_UNSUPPORTED; /* REQ-CoreLR-F-GCodeInterpreter-0001/-0007; G17 (the XY plane), G21
                                                    * (millimetres), G54 (the coordinate system) and G94 (feed per minute)
                                                    * are the only ones there are, so each is accepted and means nothing
                                                    * (REQ-CoreLR-F-GCodeInterpreter-0012) */
            }
            break;
         }

         case 'M':
            if (number.HasFraction)
            {
               return GCODEPARSER_ERR_UNSUPPORTED;
            }
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
            if (GCodeNumberToCode(&number) == 9u)
            {
               break;                                      /* M9, coolant off: nothing to switch, as in grbl's own parser (REQ-CoreLR-F-GCodeInterpreter-0019) */
            }
#endif
            outParsed->HasM = 1;
            outParsed->MValue = GCodeNumberToCode(&number);
            if (outParsed->MValue != 3 && outParsed->MValue != 5 && outParsed->MValue != 2 && outParsed->MValue != 30)
            {
               return GCODEPARSER_ERR_UNSUPPORTED;
            }
            break;

         case 'X':
            outParsed->HasX = 1;
            outParsed->XValueUm = GCodeNumberToUm(&number);
            break;

         case 'Y':
            outParsed->HasY = 1;
            outParsed->YValueUm = GCodeNumberToUm(&number);
            break;

         case 'I':
            outParsed->HasI = 1;
            outParsed->IValueUm = GCodeNumberToUm(&number);
            break;

         case 'J':
            outParsed->HasJ = 1;
            outParsed->JValueUm = GCodeNumberToUm(&number);
            break;

#if (CORELR_GCODE_Z) || (CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL)
         case 'Z':
            outParsed->HasZ = 1;
            outParsed->ZValueUm = GCodeNumberToUm(&number);
            break;
#endif

         case 'L':
         case 'P':
            if (number.HasFraction)
            {
               return GCODEPARSER_ERR_UNSUPPORTED;
            }
            if (letter == 'L')
            {
               outParsed->LValue = GCodeNumberToCode(&number);
            }
            else
            {
               outParsed->PValue = GCodeNumberToCode(&number);
            }
            break;

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
         case 'T': /* a tool number: there is one pen (REQ-CoreLR-F-GCodeInterpreter-0019) */
#endif
         case 'F': /* tolerated-but-ignored (REQ-CoreLR-F-GCodeInterpreter-0006) */
         case 'S':
         case 'N':
            break;

         default:
            return GCODEPARSER_ERR_UNSUPPORTED; /* unrecognized word */
      }
   }

   /* G92 / G10 (REQ-CoreLR-F-GCodeInterpreter-0017): the line is judged whole here, before anything is applied. Its X and Y are coordinates of the pen, not a target; a Z
    * is ignored (Candle restores its work coordinates with `G92 X.. Y.. Z..`, and has no Z offset to set here). */
   if (outParsed->OriginCmd != 0u)
   {
      outParsed->HasZ = 0;
      if (outParsed->HasM || outParsed->HasI || outParsed->HasJ || (outParsed->MotionWord >= 0) || MACHINE_WORD(outParsed))
      {
         return GCODEPARSER_ERR_AXIS_CONFLICT;
      }
      if ((outParsed->OriginCmd == 2u) ? ((outParsed->LValue != 20u) || (outParsed->PValue != 1u)) : ((outParsed->LValue | outParsed->PValue) != 0u))
      {
         return (outParsed->OriginCmd == 2u) ? GCODEPARSER_ERR_UNSUPPORTED : GCODEPARSER_ERR_UNUSED_WORDS;   /* G10 only as L20 P1; L or P on a G92 */
      }
      if (!(outParsed->HasX || outParsed->HasY))
      {
         return GCODEPARSER_ERR_NO_AXIS_WORDS;
      }
   }
   else if ((outParsed->LValue | outParsed->PValue) != 0u)
   {
      return GCODEPARSER_ERR_UNUSED_WORDS;      /* L or P without a G10: a word silently ignored is worse than a halt */
   }

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   if (outParsed->Machine && !jog && ((EffectiveMode(outParsed) < 0) || (EffectiveMode(outParsed) > 1)))
   {
      return GCODEPARSER_ERR_UNSUPPORTED;          /* G53 only goes with G0 or G1 (REQ-CoreLR-F-GCodeInterpreter-0018) */
   }
#endif

   if (outParsed->HasZ)
   {
      if (outParsed->HasM)
      {
         return GCODEPARSER_ERR_UNUSED_WORDS;      /* two pen words on one line: which one counts? Neither is silently dropped */
      }
      if (!jog && !(CORELR_GCODE_Z))
      {
         return GCODEPARSER_ERR_UNSUPPORTED;       /* Z without CORELR_GCODE_Z: as it always was */
      }
   }

   if (!jog && (outParsed->OriginCmd == 0u) && (outParsed->HasX || outParsed->HasY || outParsed->HasI || outParsed->HasJ))
   {
      int8_t mode = EffectiveMode(outParsed);

      if (mode < 0)
      {
         /* Bare coordinates before any G0..G3 has ever been seen: real
          * GCode has no defined meaning here. Halting (rather than
          * silently doing nothing) matches REQ-CoreLR-F-GCodeInterpreter-0007's
          * spirit -- a silently-dropped move is worse than a halted job. */
         return GCODEPARSER_ERR_NO_AXIS_WORDS;
      }
      if (mode >= 2)
      {
         /* An arc needs an end point and a centre; neither alone is a circle (REQ-CoreLR-D-GCodeParser-0009) */
         if (!(outParsed->HasX || outParsed->HasY))
         {
            return GCODEPARSER_ERR_NO_AXIS_WORDS;
         }
         if (!(outParsed->HasI || outParsed->HasJ))
         {
            return GCODEPARSER_ERR_ARC_NO_OFFSET;
         }
      }
      else if (outParsed->HasI || outParsed->HasJ)
      {
         return GCODEPARSER_ERR_UNUSED_WORDS; /* an offset on a straight move: a word silently ignored is worse than a halt */
      }
      if (outParsed->HasM && (outParsed->MValue != 3) && (outParsed->MValue != 5))
      {
         /* A program end is the last thing a line does, and the pen lift it implies would be queued *before* the move
          * of the same line (REQ-CoreLR-D-GCodeParser-0011); nobody writes "M30 G1 X5", so it is refused, not reordered */
         return GCODEPARSER_ERR_UNSUPPORTED;
      }
   }

   return GCODEPARSER_OK;
}

/**
 * What a Z word means for the pen (P7, @req{REQ-CoreLR-D-GCodeParser-0014}): some plotter generators lift the pen with Z instead of M3/M5.
 * @return 1 pen down, 0 pen up, -1 no pen change (no Z word, or a relative Z0).
 * Absolute (G90): Z <= 0 is the pen on the paper, Z > 0 lifted. Relative (G91): the sign of the move decides, 0 changes nothing.
 */
static int8_t PenFromZ(const ParsedLine_t *parsed)
{
#if !(CORELR_GCODE_Z)
   (void)parsed;
   return -1;
#else
   uint8_t incremental = parsed->DistWord ? (uint8_t)(parsed->DistWord == 91u) : s_state.DistanceModeIncremental;

   if (!parsed->HasZ)
   {
      return -1;
   }
   if (MACHINE_WORD(parsed))
   {
      return (parsed->ZValueUm >= 0) ? (int8_t)0 : (int8_t)1;       /* the machine's Z is 0 at the top, negative below (REQ-CoreLR-F-GCodeInterpreter-0018) */
   }
   if (parsed->ZValueUm > 0)
   {
      return 0;
   }
   if (parsed->ZValueUm < 0)
   {
      return 1;
   }
   return incremental ? (int8_t)-1 : (int8_t)1;
#endif
}

/** The motion mode a line runs under: its own G word, else the modal one. */
static int8_t EffectiveMode(const ParsedLine_t *parsed)
{
   return (parsed->MotionWord >= 0) ? parsed->MotionWord : s_state.MotionMode;
}

/**
 * The end point of a line's move, in the drawing frame.
 *
 * From where the queue will END, not from where the head is now. Moves are queued ahead of
 * their execution (@req{REQ-CoreLR-D-Motion-0013}), so the executed position is behind every
 * block still in the planner: measured on the host, three relative `G1 X10` queued back to
 * back ended at X = 10 mm instead of 30, and `G1 X10 Y5` then `G1 X20` went to Y = 0, the
 * executed position, instead of keeping Y = 5. Present since the look-ahead planner was
 * added, and never met because no job had ever been run (@req{REQ-CoreLR-D-GCodeParser-0008}).
 *
 * The distance mode is the line's own word if it has one, else the modal one, so the same answer
 * comes out before and after ApplyParsedLine() has recorded it.
 */
static void ComputeTarget(const ParsedLine_t *parsed, Motion_Position_t *outTarget)
{
   Motion_Position_t current = Motion_GetTargetPosition();
   uint8_t incremental = parsed->DistWord ? (uint8_t)(parsed->DistWord == 91u) : s_state.DistanceModeIncremental;

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   if (parsed->Machine)
   {
      Motion_Position_t offset = Motion_GetMachineOffset();      /* the machine frame, absolute whatever G90/G91 says: drawing = machine - WCO */

      outTarget->X = parsed->HasX ? (parsed->XValueUm - offset.X) : current.X;
      outTarget->Y = parsed->HasY ? (parsed->YValueUm - offset.Y) : current.Y;
      return;
   }
#endif
   if (incremental)
   {
      outTarget->X = current.X + parsed->XValueUm;   /* a word not on the line is 0: the tokenizer zeroes the values */
      outTarget->Y = current.Y + parsed->YValueUm;
   }
   else
   {
      outTarget->X = parsed->HasX ? parsed->XValueUm : current.X;
      outTarget->Y = parsed->HasY ? parsed->YValueUm : current.Y;
   }
}

/** @return 1 if either drawing coordinate is beyond the machine's reach (@req{REQ-CoreLR-F-GCodeInterpreter-0011}):
 *  one OR of the two magnitudes and one shift. */
static uint8_t OutOfReach(const Motion_Position_t *p)
{
   uint32_t ax = (uint32_t)((p->X < 0) ? -p->X : p->X);
   uint32_t ay = (uint32_t)((p->Y < 0) ? -p->Y : p->Y);

   return (uint8_t)(((ax | ay) >> MOTION_REACH_SHIFT) != 0u);
}

/**
 * @brief Pass 2: apply an already-validated line's effects, in order:
 * distance mode, pen request, motion request (@req{REQ-CoreLR-D-GCodeParser-0004}).
 * @return GCODEPARSER_END for a program end (M2/M30), else GCODEPARSER_OK.
 * Modal motion words (confirmed 2026-09-07, GCodeParser.h) mean a line
 * with bare X/Y after an earlier G0..G3 still triggers motion.
 */
static GCodeParser_Result_t ApplyParsedLine(const ParsedLine_t *parsed)
{
   if (parsed->DistWord)
   {
      s_state.DistanceModeIncremental = (parsed->DistWord == 91u) ? 1u : 0u;
   }

   if (parsed->MotionWord >= 0)
   {
      s_state.MotionMode = parsed->MotionWord;
   }

   if (PenFromZ(parsed) >= 0)
   {
      /* a Z word, as M3 / M5 without the program-end part (P7) */
      s_state.PenDown = (uint8_t)PenFromZ(parsed);
      if (s_state.PenDown)
      {
         PenControl_RequestDown();
      }
      else
      {
         PenControl_RequestUp();
      }
   }

   if (parsed->HasM)
   {
      if (parsed->MValue == 3)
      {
         s_state.PenDown = 1;
         PenControl_RequestDown();
      }
      else /* M5, and the program end M2/M30 (REQ-CoreLR-F-GCodeInterpreter-0012): the pen goes up the same way */
      {
         s_state.PenDown = 0;
         PenControl_RequestUp();
         if (parsed->MValue != 5)
         {
            GCodeParser_Init(); /* back to the job-start values; no motion on this line (refused at tokenizing) */
            return GCODEPARSER_END;
         }
      }
   }

   if (parsed->HasX || parsed->HasY)
   {
      Motion_Position_t target;

      ComputeTarget(parsed, &target);

      /* Queued, not executed immediately: the look-ahead planner needs
       * moves ahead of time to give each junction a speed
       * (REQ-CoreLR-D-Motion-0013). Acceptance was already checked in
       * GCodeParser_ExecuteLine(), so this cannot fail here. */
      if (s_state.MotionMode >= 2)
      {
         /* I and J are always relative to the arc's start, whatever G90/G91 says; a missing one is 0
          * (REQ-CoreLR-D-GCodeParser-0009). G2 is clockwise, G3 counter-clockwise. */
         (void)Motion_QueueArc(&target, parsed->IValueUm, parsed->JValueUm,
                               (uint8_t)(s_state.MotionMode == 2));
      }
      else
      {
         (void)Motion_QueueMove(target);
      }
   }

   return GCODEPARSER_OK;
}

/* CORELR_NOINLINE: keeps this out of main()'s permanent frame -- see SwConfig.h */
CORELR_NOINLINE
GCodeParser_Result_t GCodeParser_ExecuteLine(const char *line)
{
   ParsedLine_t parsed;
   GCodeParser_Result_t result;

   /* Tokenize BEFORE deciding acceptance (REQ-CoreLR-D-GCodeParser-0007):
    * whether the line can be accepted depends on what is in it, and
    * only tokenizing reveals that. Safe to do in this order precisely
    * because TokenizeLine() is the side-effect-free half of the
    * two-pass design -- discarding its result and re-tokenizing on a
    * later pass costs nothing but a few microseconds. */
   result = TokenizeLine(line, &parsed, 0);

   if (result != GCODEPARSER_OK)
   {
      return result;
   }

   /*
    * Acceptance, decided here rather than by the caller so the rule
    * stays inside this module.
    *
    *   a move (X/Y)  needs a free slot in the look-ahead buffer.
    *
    *   a pen word    needs the whole pipeline drained first. A pen
    *   (M3/M5)       action goes straight into the step queue, so if
    *                 planned moves were still waiting it would fire
    *                 *before* them -- the pen would lift or drop at
    *                 the wrong point on the drawing. Draining is the
    *                 cheap fix: pen changes are rare and already cost
    *                 a 300ms settle, so the lost look-ahead does not
    *                 matter. It is also what keeps
    *                 REQ-CoreLR-F-PenControl-0004's guarantee true now
    *                 that moves are parsed ahead of execution.
    *
    *   line:   G1 X10   G1 X20   M5      G1 X30
    *   plan:   [B0]     [B0 B1]  drain   [B0]
    *                             wait    (pen lifted at X20, correct)
    */
   /*
    * Validity first, then busyness (REQ-CoreLR-D-GCodeParser-0010): whether a line is acceptable does not depend on the
    * planner's state, and a line must not be put off for later only to be refused then. A move is invalid if its end
    * point is beyond the machine's reach (REQ-CoreLR-F-GCodeInterpreter-0011), and an arc also by the radius rule, which
    * needs the start point and is a pure query of Motion (REQ-CoreLR-D-GCodeParser-0009).
    */
   if (parsed.OriginCmd != 0u)
   {
      /* G92 / G10 L20 P1 (REQ-CoreLR-D-GCodeParser-0017): the pen's current position becomes (X, Y), absolute whatever G90/G91 says. What it means depends on where the
       * pen IS, so unlike every other line it waits for the end of everything queued; the zone refusal (the origin must stay inside it) can only be answered then. */
      Motion_Position_t at;

      at.X = parsed.HasX ? parsed.XValueUm : 0;
      at.Y = parsed.HasY ? parsed.YValueUm : 0;
      if (OutOfReach(&at))
      {
         return GCODEPARSER_ERR_TARGET_INVALID;
      }
      if (!(Motion_IsIdle() && Motion_PlannerIsEmpty()))
      {
         return GCODEPARSER_BUSY;
      }
      return Motion_SetOrigin((uint8_t)((parsed.HasX ? MOTION_ORIGIN_X : 0u) | (parsed.HasY ? MOTION_ORIGIN_Y : 0u)), parsed.XValueUm, parsed.YValueUm)
             ? GCODEPARSER_OK : GCODEPARSER_ERR_TARGET_INVALID;
   }
   if (parsed.HasX || parsed.HasY)
   {
      Motion_Position_t target;

      ComputeTarget(&parsed, &target);
      if (OutOfReach(&target) || !Motion_TravelAllows(&target))   /* the zone: REQ-CoreLR-F-GCodeInterpreter-0016 */
      {
         return GCODEPARSER_ERR_TARGET_INVALID;
      }
      if ((EffectiveMode(&parsed) >= 2)
       && !Motion_ArcCheck(&target, parsed.IValueUm, parsed.JValueUm, (uint8_t)(EffectiveMode(&parsed) == 2)))
      {
         return GCODEPARSER_ERR_ARC_RADIUS;
      }
   }
   if ((parsed.HasM || (PenFromZ(&parsed) >= 0)) && !(Motion_IsIdle() && Motion_PlannerIsEmpty()))
   {
      return GCODEPARSER_BUSY;
   }
   if ((parsed.HasX || parsed.HasY) && !Motion_PlannerHasRoom())
   {
      return GCODEPARSER_BUSY;
   }

   return ApplyParsedLine(&parsed);
}

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
/** Ctrl-X (@req{REQ-CoreLR-F-SerialControl-0015}): the job-start values, except that the motion mode is G0, as grbl's after a reset -- Candle's restoration after an Abort
 *  is `G53 G90 X.. Y.. Z..` with no G word. */
void GCodeParser_ResetByLink(void)
{
   GCodeParser_Init();
   s_state.MotionMode = 0;
}

/**
 * `$H` (@req{REQ-CoreLR-D-GCodeParser-0018}): lift the pen if it is down, then one move to the drawing origin. Waits (BUSY) for the machine to be idle: the pen word
 * goes straight into the step queue and must not overtake a move still planned.
 */
CORELR_NOINLINE
GCodeParser_Result_t GCodeParser_ExecuteHome(void)
{
   Motion_Position_t origin;

   origin.X = 0;
   origin.Y = 0;
   if (!(Motion_IsIdle() && Motion_PlannerIsEmpty()))
   {
      return GCODEPARSER_BUSY;
   }
   if (!Motion_TravelAllows(&origin))
   {
      return GCODEPARSER_ERR_TARGET_INVALID;
   }
   if (s_state.PenDown)
   {
      s_state.PenDown = 0;
      PenControl_RequestUp();
   }
   (void)Motion_QueueMove(origin);
   return GCODEPARSER_OK;
}

/**
 * `$J=` (docs/protocol/SerialProtocol.md @proto{P2}, @proto{P6}): one jog move from the link. It takes the same words as a line, but only a distance mode, X, Y, F (ignored) and Z (ignored:
 * Candle sends one with every jog); it never touches the modal state, and it needs no motion word -- the tokenizer is told it is a jog and skips the check that bare
 * coordinates have a G word to ride on. A jog with no X or Y has nothing to move: accepted, nothing done.
 */
CORELR_NOINLINE
GCodeParser_Result_t GCodeParser_ExecuteJog(const char *line)
{
   ParsedLine_t parsed;
   Motion_Position_t target;
   GCodeParser_Result_t result = TokenizeLine(line, &parsed, 1);

   if (result != GCODEPARSER_OK)
   {
      return result;
   }
   if (parsed.HasM || parsed.HasI || parsed.HasJ || (parsed.MotionWord >= 0) || (parsed.OriginCmd != 0u))
   {
      return GCODEPARSER_ERR_UNSUPPORTED;     /* a jog is a distance, X, Y and F: a motion word, an arc offset or a pen word is not one */
   }
   if (!(parsed.HasX || parsed.HasY))
   {
      return GCODEPARSER_OK;
   }
   ComputeTarget(&parsed, &target);
   if (OutOfReach(&target) || !Motion_TravelAllows(&target))
   {
      return GCODEPARSER_ERR_TARGET_INVALID;
   }
   if (!Motion_PlannerHasRoom())
   {
      return GCODEPARSER_BUSY;
   }
   (void)Motion_QueueMove(target);
   return GCODEPARSER_OK;
}
#endif
