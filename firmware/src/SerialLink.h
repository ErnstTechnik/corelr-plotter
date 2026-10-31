/*
 * SerialLink.h
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

/* This module belongs to the serial image only (CORELR_JOB_SOURCE_SERIAL): in the other image this header is empty, so nothing can use the module by mistake,
 * and the documentation of that image does not list it. */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL

/**
 * @file SerialLink.h
 * @brief The serial link's protocol: lines in, replies out, real-time commands, `?` status reports and `$` commands
 * (docs/protocol/SerialProtocol.md, a grbl 1.1 subset). **Serial variant only** (`CORELR_JOB_SOURCE_SERIAL`): in the SD build this
 * module is empty.
 *
 * Realizes: docs/protocol/SerialProtocol.md @proto{FW-03}, @proto{FW-05}, @proto{FW-06}, @proto{FW-07}, @proto{FW-09} (@req{REQ-CoreLR-F-SerialControl-0004}..)
 * Design:   @req{REQ-CoreLR-D-SerialLink-0001}..
 *
 * What it owns: assembling a line from the receive ring (spaces, comments and control characters stripped, 80 significant
 * characters), submitting it to the parser, the one reply per line, the real-time bytes, the status report, the `$`
 * commands, the welcome line. What it does *not* own: the job state machine, which stays in `grbl_corelr.ino` -- this
 * module is told the state and answers with a request (SerialLink_Request_t), so that the rules of the link can be
 * tested on the host without the sketch.
 *
 * Never blocks: a reply is only started when it fits whole in the transmit ring; otherwise the line stays where it is
 * and is tried again on the next pass.
 */

#ifndef CORELR_SERIALLINK_H
#define CORELR_SERIALLINK_H

#include <stdint.h>
#include "JobState.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the link asks the job state machine to do. At most one per SerialLink_Task() call. */
typedef enum
{
   SERIALLINK_REQ_NONE = 0,
   SERIALLINK_REQ_START,       /**< a line was accepted while Idle: the job begins (state Run) */
   SERIALLINK_REQ_HALT,        /**< `!` while Run: ramped stop, pen up */
   SERIALLINK_REQ_RESUME,      /**< `~` while Hold:0 */
   SERIALLINK_REQ_CANCEL,      /**< Ctrl-X while Run or Hold: ramped stop, pen up, back to Idle */
   SERIALLINK_REQ_ACK_ERROR,   /**< `$X`, or Ctrl-X in the error state */
   SERIALLINK_REQ_FAIL,        /**< a line was refused during a job: ramped stop, pen up, error state */
   SERIALLINK_REQ_JOG_CANCEL   /**< 0x85 while a `$J=` jog is moving the machine (Idle, not Run): ramped stop, the rest of the jog dropped */
} SerialLink_Request_t;

/** @brief Reset the link's state and arrange for the welcome line to be sent (by the first SerialLink_Task() calls). */
void SerialLink_Init(void);

/**
 * @brief Service the link once. Call every loop() pass, after the job state has been settled for this pass.
 * @param state     the job state now
 * @param lcdBusy   1 while an LCD mode that moves the machine (a jog, a calibration wizard) is active: G-code is then refused
 * @return what the job state machine should do (SERIALLINK_REQ_NONE most of the time)
 */
SerialLink_Request_t SerialLink_Task(JobState_t state, uint8_t lcdBusy);

/** @return 1 if the link still holds work: a line being read or waiting for its reply, bytes in the ring, a welcome to send.
 *  A job is not over while this is 1 (docs/protocol/SerialProtocol.md @protosec{4-10,4.10}). */
uint8_t SerialLink_HasWork(void);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_SERIALLINK_H */

#endif /* CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL */
