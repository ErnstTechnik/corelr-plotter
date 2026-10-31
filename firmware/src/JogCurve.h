/*
 * JogCurve.h
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
 * @file JogCurve.h
 * @brief The Control jog's response: the stick's deflection (X, Y) and the time since the last block, in; the displacement of the pen over that time, out.
 *
 * Design: @req{REQ-CoreLR-D-LcdJoystickUI-0021}. Pure arithmetic, no hardware, so it is tested on the host and its cost measured on the cycle simulator.
 *
 * The speed is a function of the NORM of the deflection vector, not of each axis alone: the direction of the pen is the direction of the stick,
 * its speed depends only on how far the stick is pushed. The dead zone is subtracted (the speed is 0 at its edge, no jump), the result is scaled to
 * the stick's full scale (LCDJOYSTICKUI_STICK_FULL_SCALE, HwConfig.h) and run through a SHAPE (the table of JogCurveTable.h: quadratic by default),
 * times the maximum speed (LCDJOYSTICKUI_JOG_MAX_SPEED, SwConfig.h).
 */

#ifndef CORELR_JOGCURVE_H
#define CORELR_JOGCURVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Forget the sub-micrometre remainder carried from block to block. Call when the jog mode is entered. */
void JogCurve_Reset(void);

/**
 * @brief The shape of the response, alone: how much of the full speed a stick at `magnitude` asks for.
 * @param magnitude  how far the stick is pushed, in counts of LcdJoystickUI_ReadAxis(): the norm of (dx, dy), or |dy| for one axis
 * @return 0 inside the dead zone, rising through the table of JogCurveTable.h to 65535 (= 1.0) at the stick's full scale (Q16)
 *
 * The Control jog multiplies it by its maximum speed (JogCurve_Displacement()); the Adjustment jog (the L/R wires of "Calibrate machine") maps it onto its step-rate
 * range (@req{REQ-CoreLR-D-LcdJoystickUI-0027}): one shape for both, so that retuning it (tools/generators/gen_jogcurve.py) retunes both.
 */
uint16_t JogCurve_Fraction(uint16_t magnitude);

/**
 * @brief The displacement over `elapsedMs` for a stick deflection.
 * @param dx,dy      the stick as LcdJoystickUI_ReadAxis() gives it: positive to the right (X) and down (Y), 0 inside the dead zone
 * @param elapsedMs  the time the displacement covers, at most LCDJOYSTICKUI_JOG_SLICE_MAX_MS
 * @param outDxUm    displacement to the right, micrometres (negative to the left)
 * @param outDyUm    displacement DOWN, micrometres (negative up): the caller turns it into the drawing frame's Y
 *
 * What is less than a micrometre is not lost: it is carried into the next call, so a slow speed moves the pen exactly as far, in total, as
 * the speed says. (Truncating each block instead drops up to 15 % of the distance when a block is a few micrometres long.)
 */
void JogCurve_Displacement(int16_t dx, int16_t dy, uint16_t elapsedMs, int32_t *outDxUm, int32_t *outDyUm);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_JOGCURVE_H */
