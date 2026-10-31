/*
 * PenControl.h
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
 * @file PenControl.h
 * @brief Pen servo driving. Pen state changes are *requested*, not
 * executed directly, so they stay synchronized with motion.
 *
 * Realizes: @req{REQ-CoreLR-F-PenControl-0001}..@req{REQ-CoreLR-F-PenControl-0004}
 * Design:   @req{REQ-CoreLR-D-PenControl-0001}..@req{REQ-CoreLR-D-PenControl-0003}
 * See:      requirements/design/REQ-CoreLR-D-PenControl.md
 *
 * @req{REQ-CoreLR-F-PenControl-0004}: a GCode M3/M5 line is parsed well ahead
 * of the corresponding physical motion (the motion queue buffers tens
 * of milliseconds of pending movement). PenControl_RequestUp()/Down()
 * therefore do not touch the servo directly -- they enqueue the action
 * into Stepper, synchronized to fire only once motion actually reaches
 * that point. When Stepper's ISR pops that entry it calls
 * PenControl_ApplyFromIsr(), a one-byte write; the servo's own 20 ms
 * frame, run from Timer2 by IsrDispatch, picks it up.
 *
 * Plain C since 2026-10-06 (@req{REQ-CoreLR-D-PenControl-0003}): the servo is
 * driven in software, no Arduino Servo library -- which owned Timer1, the
 * only 16-bit timer, and is C++ only. The extern "C" below is for the C++
 * callers (the LCD UI).
 */

#ifndef CORELR_PENCONTROL_H
#define CORELR_PENCONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Attach the pen servo and set it to "up" immediately (no
 * motion is happening yet at boot, so no synchronization is needed
 * here). Call once from setup(). */
void PenControl_Init(void);

/**
 * @brief Request the pen be raised once motion reaches this point in
 * the sequence (@req{REQ-CoreLR-F-PenControl-0004}). Does not touch the
 * servo directly. No-op if already up *and* no down request is still
 * pending in the queue.
 */
void PenControl_RequestUp(void);

/** @brief Symmetric to PenControl_RequestUp(), for lowering the pen. */
void PenControl_RequestDown(void);

/**
 * @brief Re-issue a pen request Stepper has not accepted yet (a full
 * queue), and keep the servo pulse table in step with the calibrated pen
 * angles. Call every loop() iteration, *before* Motion_Task().
 */
void PenControl_Task(void);

/** @return 1 if the pen is currently (physically) down. */
uint8_t PenControl_IsDown(void);

/**
 * @return 1 if the pen is *requested* down -- what the caller last
 * asked for, which may not yet be what the servo shows.
 *
 * Distinct from PenControl_IsDown(), which reports the physical state.
 * The job state machine needs this one to remember, across a pause,
 * whether the pen has to be put back down on resume: at the moment a
 * pause lifts the pen, the physical state is still "down" and would be
 * the wrong thing to record.
 */
uint8_t PenControl_IsRequestedDown(void);

/**
 * @brief Move the servo to an arbitrary angle at its next frame, bypassing
 * the request/queue mechanism. Calibration-only (the LCD pen wizard,
 * @req{REQ-CoreLR-D-LcdJoystickUI-0007}) -- never call this during normal
 * job execution.
 */
void PenControl_PreviewAngle(uint8_t angle);

/**
 * @name Interrupt entry points
 * Called from interrupt context only. Not for application code.
 * @{
 */
/** Stepper's ISR popped an entry carrying a pen action (a Stepper_PenAction_t
 *  value, UP or DOWN): select that setpoint for the servo's next frame. */
void PenControl_ApplyFromIsr(uint8_t action);
/** Every 1 ms, from IsrDispatch's Timer2 ISR: advance the 20 ms servo frame. */
void PenControl_Tick(void);
/** From IsrDispatch's Timer2 compare-B ISR: end the pulse. */
void PenControl_PulseEnd(void);
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CORELR_PENCONTROL_H */
