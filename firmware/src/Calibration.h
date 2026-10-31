/*
 * Calibration.h
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
 * @file Calibration.h
 * @brief Persistence for configurable calibration parameters and the
 * last known position; the reference-position geometry itself.
 *
 * Realizes: @req{REQ-CoreLR-F-Calibration-0002}..@req{REQ-CoreLR-F-Calibration-0007}
 * Design:   @req{REQ-CoreLR-D-Calibration-0001}..@req{REQ-CoreLR-D-Calibration-0008}
 * See:      requirements/design/REQ-CoreLR-D-Calibration.md
 */

#ifndef CORELR_CALIBRATION_H
#define CORELR_CALIBRATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configurable calibration parameters (@req{REQ-CoreLR-F-Calibration-0002}),
 * persisted in non-volatile memory, with factory defaults.
 */
typedef struct
{
   int32_t MotorDistanceUm;     /**< distance between the two motors */
   int32_t ReferencePositionX;  /**< reference position, X, internal frame (see -0004) */
   int32_t ReferencePositionY;  /**< reference position, Y, internal frame */
   int32_t ReferenceCableLengthUm; /**< Lref, see @req{REQ-CoreLR-D-Calibration-0001} */
   int32_t WorkOriginOffsetX;   /**< drawing-origin offset, X, internal frame
                                     (@req{REQ-CoreLR-F-Calibration-0006}); defaults to
                                     ReferencePositionX but stored independently */
   int32_t WorkOriginOffsetY;   /**< drawing-origin offset, Y, internal frame */
   uint8_t PenUpAngle;          /**< servo angle, pen lifted */
   uint8_t PenDownAngle;        /**< servo angle, pen lowered */
} Calibration_Params_t;

/**
 * @brief The 3 fields Motion_XYToLR() actually reads, out of
 * Calibration_Params_t's 7 (@req{REQ-CoreLR-D-Calibration-0004}):
 * a narrower copy for a call site reached at least once per Bresenham
 * grid step during motion, instead of the full struct.
 */
typedef struct
{
   int32_t WorkOriginOffsetX;   /**< drawing-origin offset, X, internal frame */
   int32_t WorkOriginOffsetY;   /**< drawing-origin offset, Y, internal frame */
   int32_t MotorDistanceUm;     /**< distance between the two motors */
} Calibration_KinematicsParams_t;

/**
 * @brief Load calibration parameters and last known calibrated flag
 * from the AVR internal EEPROM (@req{REQ-CoreLR-D-Calibration-0003}),
 * or factory defaults if none saved yet (blank EEPROM) or the saved
 * image fails its magic-number/checksum integrity check.
 */
void Calibration_Init(void);

/**
 * @brief Copy out the current in-memory calibration parameters, in full.
 * Prefer Calibration_GetKinematicsParams()/_GetPenAngles() in a
 * hot path that only needs a few fields -- this copies all 7
 * (@req{REQ-CoreLR-D-Calibration-0004}). Still the right call for a
 * fetch-mutate-set sequence (the LCD calibration wizards), which need
 * the whole struct anyway and run at human-interaction, not motion, pace.
 */
void Calibration_GetParams(Calibration_Params_t *outParams);

/**
 * @brief Narrow getter for Motion_XYToLR() -- see
 * Calibration_KinematicsParams_t. @req{REQ-CoreLR-D-Calibration-0004}.
 */
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void);

/**
 * @brief Narrow getter for PenControl -- avoids copying the full
 * struct just to read 2 of its 8 bytes on every PenControl_Task()/
 * _Init() call. @req{REQ-CoreLR-D-Calibration-0004}.
 */
void Calibration_GetPenAngles(uint8_t *outUpAngle, uint8_t *outDownAngle);

/** @return the motor spacing, in micrometres. Narrow getter: reading
 *  one field through Calibration_GetParams() would copy all 26 bytes
 *  (@req{REQ-CoreLR-D-Calibration-0005}). */
int32_t Calibration_GetMotorDistanceUm(void);

/** @return the reference cable length, in micrometres. @see above. */
int32_t Calibration_GetReferenceCableLengthUm(void);

/**
 * @brief Set and persist updated calibration parameters to EEPROM.
 * ReferencePositionX/Y are always recomputed from the given
 * MotorDistanceUm/ReferenceCableLengthUm (@req{REQ-CoreLR-D-Calibration-0001})
 * rather than trusted from the caller.
 *
 * A change of the motor distance or of the reference cable length is REFUSED when the resulting reference position is not inside the travel zone
 * (@req{REQ-CoreLR-F-Calibration-0009}, @req{REQ-CoreLR-D-Calibration-0011}): the side margin from the vertical of each motor, the top margin below the line joining
 * them (SwConfig.h) -- which also covers a distance too small for a zone, and a cable shorter than half the distance. Nothing is changed then.
 * A call that leaves both as they are (the pen angles, the drawing origin) is not subject to this.
 * @return 1 if the parameters were stored, 0 if they were refused.
 */
uint8_t Calibration_SetParams(const Calibration_Params_t *params);

/**
 * @return 1 if the machine's position is known (@req{REQ-CoreLR-F-Calibration-0003}).
 * After a power cycle this is true only if the persisted position was valid
 * (@req{REQ-CoreLR-D-Calibration-0007}); it is cleared when the installation
 * geometry changes.
 */
uint8_t Calibration_IsCalibrated(void);

/**
 * @brief Mark the machine as calibrated (called once the reference
 * position has been reached and confirmed — @req{REQ-CoreLR-F-Calibration-0004}).
 * Defaults WorkOriginOffsetX/Y to the reference position
 * (@req{REQ-CoreLR-D-Calibration-0002}) and persists to EEPROM. The *position*
 * is stored separately, by Calibration_StorePosition().
 */
void Calibration_MarkCalibrated(void);

/**
 * @brief The machine no longer knows where it is: clear the calibrated flag and mark the persisted position invalid
 * (@req{REQ-CoreLR-D-Calibration-0009}). Called by the Adjustment jog (step 3 of "Calibrate machine") at its first raw step: that jog moves the
 * motors without Motion following, so whatever position Motion tracked -- and the position keeper would go on to store -- is
 * no longer where the head is. Confirming the calibration (Calibration_MarkCalibrated()) is the only way back.
 */
void Calibration_Invalidate(void);

/**
 * @name Persisted position
 * Realizes @req{REQ-CoreLR-D-Calibration-0006}. The motors' absolute step counts, stored while the machine stands
 * still and marked invalid before it moves, so that a power cut during
 * motion can never restore a stale position
 * (@req{REQ-CoreLR-F-Calibration-0007}).
 * @{
 */

/** @return 1 if the persisted position is currently marked valid, i.e.
 *  nothing has moved since it was stored. */
uint8_t Calibration_IsPositionStored(void);

/**
 * @brief Mark the persisted position invalid. Must be called -- and has
 * returned -- before the first step that could move a motor
 * (@req{REQ-CoreLR-D-Stepper-0014}). Idempotent: a flag test when already
 * invalid. Otherwise one EEPROM byte, **waited for** (<= ~3.4 ms), so the
 * invalidation is on the chip before any step.
 */
void Calibration_InvalidatePosition(void);

/**
 * @brief Store Motion's current state -- the absolute step counts and the
 * position -- and mark it valid. Blocks ~60 ms writing eighteen bytes: call
 * it only while the machine stands still. The marker is written last, so a
 * power cut anywhere in the sequence leaves the position invalid.
 *
 * The position is stored in the **internal** kinematics frame (X: drawing
 * position plus the drawing-origin offset; Y: the offset minus the drawing
 * position, @req{REQ-CoreLR-D-Motion-0025}), so that a later change of the
 * offset cannot leave it describing a different point.
 *
 * The position is stored beside the steps only to avoid computing it at
 * start-up: deriving it from the counts costs ~700 bytes of flash, measured
 * twice (2026-10-06 and, with the optimised arithmetic, 2026-10-07), to save
 * 8 bytes of EEPROM. Kept as a patch with the numbers: docs/design-notes/experiments/.
 */
void Calibration_StoreCurrentPosition(void);

/**
 * @brief If the persisted position is valid, restore Motion from it
 * (Motion_Restore()). Called once, from Motion_Init().
 */
void Calibration_RestoreStoredPosition(void);

/** @brief The drawing-origin offset, internal frame. Narrow getter: two
 *  fields, no struct copy (@req{REQ-CoreLR-D-Calibration-0004}). */
void Calibration_GetWorkOrigin(int32_t *outX, int32_t *outY);

/**
 * @brief Set the drawing origin to this internal-frame point and store it (@req{REQ-CoreLR-F-Calibration-0010}, @req{REQ-CoreLR-D-Calibration-0012}).
 * It is refused -- 0, nothing changed -- if the geometry is unknown or the point is outside the travel zone (the side margins from the motors' verticals,
 * the top margin below the line joining them): the origin is always inside the zone. The stored position is internal and does not move with it; the caller
 * (Motion_SetOrigin()) is the one that keeps the drawing position consistent.
 * @return 1 if stored.
 */
uint8_t Calibration_SetWorkOrigin(int32_t offsetX, int32_t offsetY);

/** @brief The calibration reference point, internal frame (for "Origin = calib."). */
void Calibration_GetReferencePosition(int32_t *outX, int32_t *outY);
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CORELR_CALIBRATION_H */
