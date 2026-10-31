/*
 * JobState.h
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
 * @file JobState.h
 * @brief The job state machine's vocabulary, shared by the sketch and the serial link.
 *
 * Types only -- no code, no storage. The state machine itself lives in `grbl_corelr.ino`
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0012}); this header exists so that `SerialLink` can name the
 * states it reports and reacts to without the sketch exporting its internals.
 */

#ifndef CORELR_JOBSTATE_H
#define CORELR_JOBSTATE_H

/**
 * Top-level job state (@req{REQ-CoreLR-F-Actuation-0002}, @req{REQ-CoreLR-F-GCodeInterpreter-0007}). The diagram of its
 * transitions is in `grbl_corelr.ino`. Three of the seven are transient ramps: halting, cancelling and erroring
 * all stop the same way -- a ramped stop, then the pen is lifted.
 */
typedef enum
{
   JOB_IDLE = 0,
   JOB_RUNNING,
   JOB_HALTING,      /* ramping down, becomes JOB_PAUSED */
   JOB_PAUSED,
   JOB_CANCELLING,   /* ramping down, becomes JOB_IDLE */
   JOB_ERRORSTOP,    /* ramping down, becomes JOB_ERROR */
   JOB_ERROR
} JobState_t;

#endif /* CORELR_JOBSTATE_H */
