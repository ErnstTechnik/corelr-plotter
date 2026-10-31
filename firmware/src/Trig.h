/*
 * Trig.h
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
 * @file Trig.h
 * @brief Trigonometry for arcs, in integers: a point on a circle from an
 * angle, and the angle of a vector.
 *
 * =====================================================================
 * OUR OWN SMALL LIBRARY, NOT <math.h>: no float, no table of angles.
 * =====================================================================
 * avr-libc has no integer trigonometry, and its float one would drag in
 * the software floating-point library for two uses (@req{REQ-CoreLR-D-Motion-0021}).
 *
 * Angles are `uint32_t` **turns, Q32**: 2^32 is one full turn, so a full
 * circle wraps for free and no angle ever needs normalising. Lengths and
 * coordinates are micrometres.
 *
 * Both functions are *absolute*: they compute from their arguments alone,
 * never from a previous result, so nothing accumulates along an arc
 * (the maintainer's rule for circles).
 *
 * Measured (tools/prototypes/arcproto, 2 M random cases up to a 2 m radius; the
 * AVR figures on the cycle simulator):
 *
 *   | | worst error | cycles |
 *   |---|---|---|
 *   | Trig_Rotate | 0.55 um | ~4 300 |
 *   | Trig_Angle  | 0.36 um at the vector's own radius | ~4 000 |
 *
 * far below the machine's 54 um motor step.
 */

#ifndef CORELR_TRIG_H
#define CORELR_TRIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The vector (r, 0) rotated by `angle`: *outX = r*cos(angle),
 * *outY = r*sin(angle), in um, rounded to nearest.
 * @param r      length in um, 0 <= r <= 2^22.
 * @param angle  turns, Q32, counter-clockwise from +X.
 * @param outX   receives r*cos(angle), um.
 * @param outY   receives r*sin(angle), um.
 */
void Trig_Rotate(uint32_t r, uint32_t angle, int32_t *outX, int32_t *outY);

/**
 * @brief The angle of the vector (x, y), in turns Q32, counter-clockwise
 * from +X. The angle of the zero vector is 0.
 * @param x,y  |x|, |y| < 2^22 um. The precision is that of the inputs: a
 *             vector of integer micrometres has an angle known to about
 *             1/length radians, whatever this function does.
 */
uint32_t Trig_Angle(int32_t x, int32_t y);

/**
 * @brief The length of an arc: r * sweep * 2*pi, in um.
 * @param r      radius in um, 0 <= r <= 2^22.
 * @param sweep  the angle swept, turns Q32. A full turn is passed as
 *               0xFFFFFFFF (2^-32 of a turn short: 0.003 um at 2 m).
 * @return the length, rounded to nearest; fits 32 bits for r <= 2^22.
 */
uint32_t Trig_ArcLength(uint32_t r, uint32_t sweep);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_TRIG_H */
