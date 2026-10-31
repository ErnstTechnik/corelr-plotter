/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* PROTOTYPE: integer CORDIC for arcs (REQ-CoreLR-D-Motion, arcs). Angles are in turns, Q32 (2^32 = one full turn),
 * so wrap-around is the natural modular arithmetic of uint32_t. Vectors are in um with 8 fractional bits inside. */
#ifndef CORDIC_H
#define CORDIC_H
#include <stdint.h>
/** (r, 0) rotated by `angle` turns: *outX = r*cos, *outY = r*sin, in um, rounded. r >= 0, r < 2^22. */
void Cordic_Rotate(uint32_t r, uint32_t angle, int32_t *outX, int32_t *outY);
/** Angle of (x, y) in turns (0..2^32), and the vector's length; |x|,|y| < 2^22. */
uint32_t Cordic_Vector(int32_t x, int32_t y, uint32_t *outLength);
#endif
