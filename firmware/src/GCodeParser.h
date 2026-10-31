/*
 * GCodeParser.h
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
 * @file GCodeParser.h
 * @brief Parses one GCode line into an executable command and carries
 * modal state (distance mode, pen state) between lines.
 *
 * Realizes: @req{REQ-CoreLR-F-GCodeInterpreter-0001}, @req{REQ-CoreLR-F-GCodeInterpreter-0002}, @req{REQ-CoreLR-F-GCodeInterpreter-0006}, @req{REQ-CoreLR-F-GCodeInterpreter-0007}, @req{REQ-CoreLR-F-GCodeInterpreter-0012}
 * Design:   @req{REQ-CoreLR-D-GCodeParser-0001}..@req{REQ-CoreLR-D-GCodeParser-0006}, @req{REQ-CoreLR-D-GCodeParser-0011}, @req{REQ-CoreLR-D-GCodeParser-0012}
 * See:      requirements/design/REQ-CoreLR-D-GCodeParser.md
 */

#ifndef CORELR_GCODEPARSER_H
#define CORELR_GCODEPARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of executing one line: 0 = accepted, a grbl status number = refused, or one of two special values.
 *
 * A refusal carries *why* (@req{REQ-CoreLR-F-GCodeInterpreter-0007}, `docs/protocol/SerialProtocol.md` section @protosec{4-6,4.6}): the number is grbl's,
 * so a sender can show its text. It is a plain byte, not an enum -- the cause is returned at each failure site as a
 * constant, which costs nothing, and the SD variant simply ignores which one it was. A sketch tests for "refused" with
 * GCODEPARSER_IS_ERROR().
 */
typedef uint8_t GCodeParser_Result_t;

#define GCODEPARSER_OK                    0u   /**< line parsed *and executed* */
#define GCODEPARSER_ERR_EXPECTED_LETTER   1u   /**< a character that is not a word letter ("Expected command letter") */
#define GCODEPARSER_ERR_BAD_NUMBER        2u   /**< a letter with no number, or a malformed one ("Bad number format") */
#define GCODEPARSER_ERR_UNSUPPORTED      20u   /**< a G/M value or a word letter outside the supported set ("Unsupported command") */
#define GCODEPARSER_ERR_AXIS_CONFLICT    21u   /**< G92 / G10 on a line with a motion word, an arc offset, a pen word (@req{REQ-CoreLR-F-GCodeInterpreter-0017}) */
#define GCODEPARSER_ERR_NO_AXIS_WORDS    26u   /**< coordinates with no motion mode yet, or an arc with no end point */
#define GCODEPARSER_ERR_TARGET_INVALID   33u   /**< end point beyond the machine's reach (-0011) */
#define GCODEPARSER_ERR_ARC_RADIUS       34u   /**< arc radii inconsistent (@req{REQ-CoreLR-F-GCodeInterpreter-0009}) */
#define GCODEPARSER_ERR_ARC_NO_OFFSET    35u   /**< an arc with neither I nor J */
#define GCODEPARSER_ERR_UNUSED_WORDS     36u   /**< I/J on a G0/G1 line */
#define GCODEPARSER_END                 254u   /**< accepted, and it was a program end (M2/M30): the job is over, @req{REQ-CoreLR-F-GCodeInterpreter-0012} */
#define GCODEPARSER_BUSY                255u   /**< not attempted: cannot accept right now, caller retries later */

/** @return 1 if a result is a refusal of the line (any grbl status number), 0 for OK, END and BUSY. */
#define GCODEPARSER_IS_ERROR(result)   (((result) != GCODEPARSER_OK) && ((result) != GCODEPARSER_END) && ((result) != GCODEPARSER_BUSY))

/** @brief Reset modal state to the job-start values (G90 absolute, no motion mode, pen up). Call at job start;
 *  the parser itself does it at a program end (M2/M30). */
void GCodeParser_Init(void);

/**
 * @brief The parser's modal state, for the `$G` report (serial variant only).
 * @param outMotion       the last motion word, 0..3 for G0..G3, -1 if none yet
 * @param outIncremental  1 for G91, 0 for G90
 * @param outPenDown      1 after M3, 0 after M5
 */
void GCodeParser_GetModal(int8_t *outMotion, uint8_t *outIncremental, uint8_t *outPenDown);

/**
 * @brief Parse and execute one already comment-stripped, NUL-terminated
 * GCode line (@req{REQ-CoreLR-D-SDReader-0002}/@req{REQ-CoreLR-D-SDReader-0003}).
 *
 * The whole line is tokenized and validated *before* any effect is
 * applied (modal state, Motion/PenControl dispatch) -- an
 * error partway through never leaves the line's earlier words
 * half-applied.
 *
 * This module decides for itself whether it can accept a line: it
 * returns GCODEPARSER_BUSY while Motion is still executing the previous
 * move, so the caller does not need to know that rule. A refused line
 * must **not** be discarded -- hold it and offer it again later.
 *
 * @retval GCODEPARSER_OK    parsed and executed.
 * @retval GCODEPARSER_END   executed, and it was M2/M30: the pen is being lifted, modal state is back to its job-start
 *                           values, and the caller ends the job (@req{REQ-CoreLR-D-GCodeParser-0011}).
 * @retval GCODEPARSER_BUSY  not attempted; retry with the same line.
 * @retval otherwise         invalid/unsupported line, the value being the grbl status number
 *                           (GCODEPARSER_IS_ERROR()); nothing of the line took effect -- caller invokes the
 *                           halt/error mechanism (@req{REQ-CoreLR-D-GCodeParser-0005}/@req{REQ-CoreLR-D-GCodeParser-0006}).
 */
GCodeParser_Result_t GCodeParser_ExecuteLine(const char *line);

#include "SwConfig.h"
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
/**
 * @brief Execute the body of a `$J=` line (everything after the `=`), serial variant only (@req{REQ-CoreLR-D-GCodeParser-0015}, docs/protocol/SerialProtocol.md @proto{P2}, @proto{P6}).
 * A relative or absolute move of the head, queued like a line's but without touching the modal state; X, Y, a distance mode, F and Z (ignored) only.
 * @retval GCODEPARSER_OK    queued, or nothing to move (no X or Y)
 * @retval GCODEPARSER_BUSY  the planner is full: retry with the same line
 * @retval otherwise         refused (a grbl status number), nothing took effect
 */
GCodeParser_Result_t GCodeParser_ExecuteJog(const char *line);

/** @brief Ctrl-X over the link: as GCodeParser_Init() but the motion mode is G0 (@req{REQ-CoreLR-F-SerialControl-0015}). Serial variant. */
void GCodeParser_ResetByLink(void);

/**
 * @brief `$H` (@req{REQ-CoreLR-F-SerialControl-0014}): pen up if down, then to the drawing origin. Serial variant.
 * @retval GCODEPARSER_OK    accepted (queued)
 * @retval GCODEPARSER_BUSY  something is still moving or planned: retry
 */
GCodeParser_Result_t GCodeParser_ExecuteHome(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CORELR_GCODEPARSER_H */
