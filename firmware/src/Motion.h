/*
 * Motion.h
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
 * @file Motion.h
 * @brief Turns a commanded Cartesian move into a stream of intermediate
 * waypoints, then into L,R cable-length targets, then into stepper-queue
 * entries.
 *
 * Realizes: @req{REQ-CoreLR-F-Motion-0001}..@req{REQ-CoreLR-F-Motion-0003}, @req{REQ-CoreLR-F-Calibration-0006}
 * Design:   @req{REQ-CoreLR-D-Motion-0001}..@req{REQ-CoreLR-D-Motion-0009}
 * See:      requirements/design/REQ-CoreLR-D-Motion.md
 *
 * Two coordinate frames (confirmed 2026-09-07): all `Motion_Position_t`
 * values in this API, and everywhere else in the project, are in the
 * **drawing frame** (what GCode and the status screen use).
 * Motion_XYToLR() is the *only* place the internal kinematics
 * frame (origin at Motor L, not mechanically reachable, never shown to
 * the user) and the drawing-origin offset exist -- see
 * @req{REQ-CoreLR-D-Motion-0005}.
 *
 *   GCode (target X,Y, drawing frame)
 *      |
 *   Cartesian planner (accel/decel/cruise, drawing frame)
 *      |
 *   Trajectory generator (integer Bresenham, ~54um grid, drawing frame)
 *      |
 *   Inverse transform (+ drawing-origin offset -> internal frame -> isqrt) -> (L,R)
 *      |
 *   Step quantization (L,R um -> absolute step counts -> coil bits)
 *      |
 *   Stepper_RequestMotion() -- one entry per waypoint
 */

#ifndef CORELR_MOTION_H
#define CORELR_MOTION_H

#include <stdint.h>
#include "SwConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Absolute Cartesian position, drawing frame, signed 32-bit
 * micrometres. Realizes @req{REQ-CoreLR-D-Motion-0001} (no floating
 * point).
 */
typedef struct
{
   int32_t X;   /**< um, to the right of the drawing origin */
   int32_t Y;   /**< um, up the wall when CORELR_Y_UP (SwConfig.h) */
} Motion_Position_t;

/** @brief Absolute per-motor cable length, signed 32-bit micrometres. */
typedef struct
{
   int32_t L;   /**< motor L's cable, um */
   int32_t R;   /**< motor R's cable, um */
} Motion_CableLengths_t;

/** @brief Initialize the pipeline's internal current-position state. */
void Motion_Init(void);

/**
 * @brief Request a standalone linear move to an absolute drawing-frame
 * target: it starts and finishes at rest, bypassing the planner. Does not
 * block -- see Motion_Task(). **Ignored if a move is already being
 * generated**; callers wanting to chain moves use Motion_QueueMove().
 * Realized per @req{REQ-CoreLR-D-Motion-0018}..@req{REQ-CoreLR-D-Motion-0020}: coarse waypoints on the exact
 * line, step events at the crossing instants of each motor's cable length,
 * a time-domain trapezoidal profile.
 */
void Motion_RequestLinearMove(Motion_Position_t target);

/**
 * @brief Queue a job move for the junction look-ahead planner
 * (@req{REQ-CoreLR-D-Motion-0013}).
 * @return 1 if accepted, 0 if the buffer is full (retry later).
 *
 * Unlike Motion_RequestLinearMove(), which executes one move in
 * isolation and therefore starts and stops at rest, queued moves are
 * planned together: each junction is given the highest speed the
 * corner and the remaining braking distance allow, so a run of
 * collinear or gently-turning moves is traversed without stopping.
 *
 * Callers must keep pen actions ordered against these moves
 * themselves: a pen entry goes straight into the step queue, so it
 * would overtake anything still sitting in this buffer. GCodeParser
 * handles that by draining the pipeline before an M3/M5
 * (@req{REQ-CoreLR-D-GCodeParser-0007}).
 */
uint8_t Motion_QueueMove(Motion_Position_t target);

/** @return 1 when no job move is waiting in the look-ahead buffer.
 *  Job completion needs this as well as Motion_IsIdle(). */
uint8_t Motion_PlannerIsEmpty(void);

/** @return 1 if Motion_QueueMove() would accept a move right now. */
uint8_t Motion_PlannerHasRoom(void);

/** @return how many more moves the look-ahead buffer accepts (the status report's `Bf:`, docs/protocol/SerialProtocol.md @protosec{5,5}). */
uint8_t Motion_PlannerFree(void);

#if CORELR_DEBUG_JOBSTATE
/** Bench diagnostics (CORELR_DEBUG_JOBSTATE): flag bits of the generator, see SerialLink.c; and its segment index (which = 0) or count (1). */
uint8_t Motion_DebugFlags(void);
uint16_t Motion_DebugSegment(uint8_t which);
#endif

/**
 * @brief Is this arc one the machine accepts? A pure query -- nothing is queued, nothing changes -- so the
 * parser can refuse a bad line before it applies any of its words (@req{REQ-CoreLR-D-GCodeParser-0009}).
 *
 * The arc starts where the queue ends, Motion_GetTargetPosition(): there is no other start, so it is not a parameter
 * (it was, until 2026-10-08, and passing two 8-byte structures by value cost 250 bytes of flash in the parser).
 *
 * @param target  its end point, drawing frame, um.
 * @param i,j     the centre relative to the start, um (G2/G3's I and J).
 * @param clockwise 1 for G2, 0 for G3: the bulge test needs the direction (@req{REQ-CoreLR-D-Motion-0028}).
 * @return 1 if accepted; 0 if the radius exceeds the machine's reach (2^21 um), if any point of the arc is outside the travel zone, or the start and end
 * radii disagree beyond grbl's rule: up to 5 um always, otherwise at most 0.5 mm and 0.1 % of the
 * radius (@req{REQ-CoreLR-F-GCodeInterpreter-0009}).
 */
uint8_t Motion_ArcCheck(const Motion_Position_t *target, int32_t i, int32_t j, uint8_t clockwise);

/**
 * @brief Is this position inside the travel zone (@req{REQ-CoreLR-F-Motion-0008}, @req{REQ-CoreLR-D-Motion-0028})? A pure query, drawing frame.
 * The zone keeps MOTION_MARGIN_SIDE_UM from the vertical of each motor and MOTION_MARGIN_TOP_UM below the line joining them
 * (SwConfig.h). Without a known geometry (not calibrated) there is no zone to apply and the answer is 1.
 */
uint8_t Motion_TravelAllows(const Motion_Position_t *p);

/**
 * @brief Move `*p` to the nearest point of the travel zone, axis by axis (the joystick jog uses it: the pen stops at the edge and slides along it).
 * @return 1 if `*p` is usable (inside the zone after the call, or no zone to apply); 0 if the zone is empty, in which case `*p` is unchanged.
 */
uint8_t Motion_TravelClamp(Motion_Position_t *p);

/**
 * @brief Queue a joystick jog slice (@req{REQ-CoreLR-D-Motion-0028}, @req{REQ-CoreLR-D-LcdJoystickUI-0029}): Motion_QueueMove() to `target` brought into the travel zone,
 * so the pen stops at the edge and slides along it. When the clamped target is where the queue already ends (the pen is at the edge and the stick pushes
 * out), or the zone is empty, nothing is queued.
 * @return 0 if the planner is full (retry later), else 1 -- including when there was nothing to queue.
 */
uint8_t Motion_QueueJog(Motion_Position_t target);

/**
 * @brief Queue an arc (G2 clockwise / G3 counter-clockwise) from where the queue ends
 * (Motion_GetTargetPosition()) to `*target`, as ONE block of the look-ahead planner
 * (@req{REQ-CoreLR-D-Motion-0022}, @req{REQ-CoreLR-D-Motion-0023}). A full circle is a target equal to the start.
 *
 * The caller is expected to have accepted it with Motion_ArcCheck(); an arc the machine cannot draw
 * is queued as the straight move it degenerates to rather than refused, so this never fails except
 * for a full planner.
 * @return 1 if accepted, 0 if the buffer is full (retry later), as for Motion_QueueMove().
 */
uint8_t Motion_QueueArc(const Motion_Position_t *target, int32_t i, int32_t j, uint8_t clockwise);

/** Motion_SetOrigin() selector: the X axis takes the given value. */
#define MOTION_ORIGIN_X          1u
/** Motion_SetOrigin() selector: the Y axis takes the given value. */
#define MOTION_ORIGIN_Y          2u
/** Motion_SetOrigin() selector: the origin goes back to the calibration reference point. */
#define MOTION_ORIGIN_REFERENCE  4u

/**
 * @brief Make the current position of the pen read (x, y) on the axes selected -- G92, G10 L20 P1, the menu's "Set origin" -- by moving the DRAWING ORIGIN under it
 * (@req{REQ-CoreLR-F-Calibration-0010}, @req{REQ-CoreLR-D-Motion-0029}). The pen does not move. The new origin is stored. Refused (0, nothing changed) while a move is being
 * generated or the planner is not empty (the coordinates queued were meant in the old frame), or if the new origin would be outside the travel zone.
 * @param what  MOTION_ORIGIN_X and/or MOTION_ORIGIN_Y: the axes that take the value x / y (the other keeps its offset); or MOTION_ORIGIN_REFERENCE: the origin goes
 *              back to the calibration reference point ("Origin = calib."), x and y unused.
 * @param x     the X the pen's position is to read, um, drawing frame (with MOTION_ORIGIN_X).
 * @param y     the Y the pen's position is to read, um (with MOTION_ORIGIN_Y).
 * @return 1 if done.
 */
uint8_t Motion_SetOrigin(uint8_t what, int32_t x, int32_t y);

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
/**
 * @brief The work coordinate offset WCO (@req{REQ-CoreLR-D-Motion-0030}, serial variant): machine position = drawing position + WCO, the machine frame having its origin at
 * the calibration reference point, +Y up the wall. G53 and the status report's MPos / WCO use it. Zero when the geometry is unknown.
 */
Motion_Position_t Motion_GetMachineOffset(void);
#endif

/**
 * @brief Do a bounded amount of work on the current request (enqueue
 * waypoints while the step queue accepts them), per
 * @req{REQ-CoreLR-D-Motion-0008}. Call every loop() iteration,
 * mirroring PenControl_Task()'s pattern.
 * @return 1 once the current request is fully enqueued (idle, ready
 * for the next request), 0 while still in progress.
 */
uint8_t Motion_Task(void);

/**
 * @brief The position of the **last calculation**: the last waypoint whose step
 * events Motion has fully generated, in the drawing frame. It is *not* where
 * the pen physically is -- the step queue holds some 15 to 40 ms of motion
 * already generated, so the head is that far behind -- and nothing here needs
 * the physical position, which would have to be worked out from the queue.
 * What it is for: the status display, the persisted position, and the start of
 * the move being generated.
 *
 * Do NOT use it to compute where a new line starts: that is
 * Motion_GetTargetPosition(). Using this one there was a defect
 * (@req{REQ-CoreLR-D-GCodeParser-0008}). Named Motion_GetCurrentPosition() until
 * 2026-10-07, which read as the physical position.
 */
Motion_Position_t Motion_GetComputedPosition(void);

/**
 * @brief The **target** of everything accepted so far: where the machine will be
 * once every block in the planner and the move in flight have been executed --
 * the end of the last queued block, else of the move in flight, else the
 * computed position. This is where a new line starts: what a relative X/Y is
 * added to, what an omitted axis keeps, and where a jog slice or an arc begins
 * (@req{REQ-CoreLR-D-GCodeParser-0008}, @req{REQ-CoreLR-D-LcdJoystickUI-0017}).
 * Named Motion_GetEndPosition() until 2026-10-07.
 */
Motion_Position_t Motion_GetTargetPosition(void);

/**
 * @return 1 if no move is currently in progress. A pure query -- unlike
 * Motion_Task(), it never advances anything, so callers that
 * only need to know whether it's safe to request a new move (e.g.
 * LcdJoystickUI's "Control" jog mode) can check this instead of calling
 * Task() a second time in the same loop() iteration (grbl_corelr.ino's
 * loop() already calls it once, unconditionally, every iteration).
 */
uint8_t Motion_IsIdle(void);
/**
 * @note This reports only on the move **being generated**. It is NOT a
 * "pipeline drained" test: a block popped from the planner that turns
 * out shorter than half a grid unit leaves no segment in flight, so
 * this returns 1 while moves are still queued. Anything that needs
 * "nothing left to do" must also test Motion_PlannerIsEmpty(), and
 * usually Stepper_IsEmpty() on top.
 */

/**
 * @brief Begin a ramped stop of the move in progress (pause), keeping
 * it resumable (@req{REQ-CoreLR-D-Motion-0012}).
 *
 * Installs a *virtual* segment end one deceleration ramp ahead of the
 * current position, so the existing trapezoidal profile decelerates
 * into it instead of the motion stopping dead when the queue drains.
 * The segment stays in progress; Motion_Resume() continues it.
 *
 * No effect if nothing is moving or a ramp is already under way.
 */
void Motion_Halt(void);

/**
 * @brief Resume the move a Motion_Halt() ramped down
 * (@req{REQ-CoreLR-D-Motion-0012}). Acceleration restarts from the current
 * position, so speed rises from VMIN through the normal ramp rather
 * than jumping straight back to mid-segment speed.
 */
void Motion_Resume(void);

/**
 * @return 1 once a Motion_Halt() ramp has finished enqueuing, i.e. the
 * machine will come to rest as soon as the step queue drains. 1 also
 * when nothing is moving at all.
 *
 * This is the moment to lift the pen: a pen entry requested now lands
 * behind the ramp's last motion entry, so the servo moves once the
 * machine has actually stopped -- no need to poll for an empty queue.
 */
uint8_t Motion_IsHaltRampDone(void);

/**
 * @brief Drop the move in progress (cancel). The current position
 * remains valid -- it tracks what has been *enqueued*, not what has
 * executed -- so only the remainder of the move is lost, not the
 * machine's registration.
 */
void Motion_Abort(void);

/**
 * @brief Force-seed the tracked position, bypassing path planning.
 * Used exactly once per calibration (@req{REQ-CoreLR-D-Calibration-0001}),
 * the moment the user confirms the machine sits at the geometric
 * reference position -- never during normal motion.
 */
void Motion_SetCurrentPosition(Motion_Position_t position);

/**
 * @brief What the position store persists (@req{REQ-CoreLR-D-Calibration-0006}):
 * the absolute step count last commanded to each motor, and the position in
 * the **internal** frame (X: drawing position plus the drawing-origin offset; Y: the offset minus the drawing position,
 * since the drawing frame has +Y up the wall -- @req{REQ-CoreLR-D-Motion-0025}).
 * Meaningful once the machine is calibrated; the adjustment jog moves the
 * motors behind Motion's back, which is why calibration re-seeds them.
 */
void Motion_GetPersistedState(int32_t *outStepsL, int32_t *outStepsR,
                              int32_t *outInternalX, int32_t *outInternalY);

/**
 * @brief Restore the commanded step counts and the position from what
 * Motion_GetPersistedState() produced at the last standstill
 * (@req{REQ-CoreLR-D-Calibration-0007}). Called by Motion_Init() when the
 * persisted position is valid. The counts are taken exactly, not
 * re-quantised from the position.
 */
void Motion_Restore(int32_t stepsL, int32_t stepsR, int32_t internalX, int32_t internalY);

/**
 * @brief Inverse kinematics: absolute drawing-frame X,Y -> absolute L,R.
 * The only function that applies the drawing-origin offset
 * (@req{REQ-CoreLR-F-Calibration-0006}) and converts to the internal
 * kinematics frame. Implemented per @req{REQ-CoreLR-D-Motion-0005}
 * (integer square root, no float).
 */
Motion_CableLengths_t Motion_XYToLR(Motion_Position_t position);

/**
 * @brief floor(sqrt(x^2 + y^2)), exact -- the one place a length is computed from two
 * components (inverse kinematics, the length of a line, the radii of an arc, the
 * reference geometry of the calibration). Two implementations, chosen by CORELR_MATH
 * (SwConfig.h, @req{REQ-CoreLR-D-Motion-0024}):
 *  - CORELR_MATH_64: any int32 x and y, through a 64-bit sum of squares and Motion_ISqrt();
 *  - CORELR_MATH_48: no value wider than 32 bits; |x| and |y| are SATURATED at
 *    CORELR_MATH_48_LIMIT_UM (2^22 - 1 um = 4.19 m), so the result is exact inside the
 *    range and merely wrong -- never a hang -- beyond it.
 */
uint32_t Motion_Hypot(int32_t x, int32_t y);

#if CORELR_MATH == CORELR_MATH_64
/**
 * @brief Integer square root (no floating point) of a 64-bit value
 * (@req{REQ-CoreLR-D-Motion-0005}): Newton iteration from above, exact floor.
 * Only in the CORELR_MATH_64 build; the 48-bit build has no 64-bit value to take the root of.
 */
uint32_t Motion_ISqrt(uint64_t value);
#endif

/**
 * @brief Coil bits for one motor at a given absolute step count
 * (@req{REQ-CoreLR-D-Motion-0007}), shared with LcdJoystickUI's
 * Adjustment jog mode (raw per-motor stepping, before calibration
 * exists so no Cartesian kinematics apply yet) so the 4-phase table
 * is not duplicated.
 */
uint8_t Motion_StepToCoilBits(int32_t stepCount, uint8_t reversed);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_MOTION_H */
