/*
 * Trig.c
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
 * @file Trig.c
 * @brief See Trig.h.
 *
 * One technique throughout: values are Q32 fractions held in `uint32_t`, and
 * a product is the **high word** of a 32x32->64 multiply (Hi()) -- free on
 * AVR, where a 64-bit shift is a library call.
 *
 *   sin, cos   Taylor series on ONE octant (0..pi/4), nested, in a shared loop
 *   atan       a minimax odd polynomial on the ratio min/max of the vector's
 *              components (0..1), in the SAME loop
 *
 * The coefficient tables are in flash (PROGMEM) and the loop is shared by the
 * three evaluations: shared code is what this firmware's flash is short of.
 */

#include <avr/pgmspace.h>
#include "Trig.h"
#include "SwConfig.h" /* only for the CORELR_MATH selector */

/** @brief The high 32 bits of a * b: a Q32 product. Out of line on purpose:
 *  inlined at ~15 sites it cost 400 bytes of flash. */
CORELR_NOINLINE
static uint32_t Hi(uint32_t a, uint32_t b)
{
   return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}

/**
 * The ratio b / a as a Q32 fraction, for b < a: floor(b * 2^32 / a).
 *
 * Precondition of the 48 build: b < a < 2^31 (Trig_Angle passes components under 2^22).
 * CORELR_MATH_64: the 64-bit division. CORELR_MATH_48: 32 steps of restoring division on 32-bit words, ~35
 * bytes of code in place of libgcc's 162-byte 64-bit divide and the five helpers it drags in (measured,
 * @req{REQ-CoreLR-D-Motion-0024}). The quotients are identical.
 */
#if CORELR_MATH == CORELR_MATH_64
static uint32_t RatioQ32(uint32_t b, uint32_t a)
{
   return (uint32_t)(((uint64_t)b << 32) / a);
}
#else
static uint32_t RatioQ32(uint32_t b, uint32_t a)
{
   uint32_t q = 0;
   uint32_t r = b;
   uint8_t i;

   for (i = 0; i < 32u; i++)
   {
      r <<= 1;                              /* r < a < 2^22 (Trig_Angle's contract), so 2r fits: no carry out */
      q <<= 1;
      if (r >= a)
      {
         r -= a;
         q |= 1u;
      }
   }
   return q;
}
#endif

/** Nested-polynomial coefficients, innermost first, so that a series
 *  c0 - u (c1 - u (c2 - ... )) is one loop. sin and cos are Q32 fractions;
 *  the atan coefficients are Q30 (the leading one is 4/pi = 1.273). */
static const uint32_t kSinCoef[4] PROGMEM = { 11836UL, 852176UL, 35791394UL, 715827883UL };      /* 1/9!, 1/7!, 1/5!, 1/3! */
static const uint32_t kCosCoef[4] PROGMEM = { 106522UL, 5965232UL, 178956971UL, 2147483648UL };  /**< the cosine series' coefficients, Q32: 1/8!, 1/6!, 1/4!, 1/2! */
static const uint32_t kAtanCoef[7] PROGMEM = { 9312570UL, 45941233UL, 108855812UL, 180916984UL, 270798677UL, 455491915UL, 1367125235UL };   /**< the arctangent polynomial of the ratio q in [0, 1], Q30, scaled by 4/pi (the leading coefficient is 1.273) */

/** @brief c[n-1] - u*(c[n-2] - u*(... - u*c[0])): the innermost coefficient first.
 *  The result has the scale of the coefficients (u is a Q32 fraction). */
static uint32_t Nested(const uint32_t *coef, uint8_t count, uint32_t u)
{
   uint32_t t = pgm_read_dword(&coef[0]);
   uint8_t i;

   for (i = 1; i < count; i++)
   {
      t = pgm_read_dword(&coef[i]) - Hi(t, u);
   }
   return t;
}

void Trig_Rotate(uint32_t r, uint32_t angle, int32_t *outX, int32_t *outY)
{
   uint8_t octant = (uint8_t)(angle >> 29);
   uint32_t phi = angle & 0x1FFFFFFFUL;                        /* 0 .. 2^29: the angle within its octant */
   uint32_t x, x2, s, c, t;
   int32_t cx, sy;

   if (octant & 1u) { phi = 0x1FFFFFFFUL - phi; }               /* odd octants run backwards: measure from the near axis */
   x = Hi(phi << 3, 0xC90FDAA2UL);                              /* the angle in radians, Q32: phi * 2*pi */
   x2 = Hi(x, x);
   s = x - Hi(Hi(Nested(kSinCoef, 4u, x2), x2), x);             /* x - x^3 (1/6 - x2 (1/120 - ...)) */
   c = 0xFFFFFFFFUL - Hi(Nested(kCosCoef, 4u, x2), x2);         /* 1 - x2 (1/2 - x2 (1/24 - ...)) */
   if (octant & 1u) { t = s; s = c; c = t; }                    /* mirrored: sin and cos swap */
   /* r in 1/256 um (<= 2^30) times a Q32 fraction, high word: the result carries 8 fractional bits */
   cx = (int32_t)((Hi(r << 8, c) + 128u) >> 8);
   sy = (int32_t)((Hi(r << 8, s) + 128u) >> 8);
   switch (octant >> 1)                                         /* then rotate by the whole quadrants */
   {
      case 0: *outX = cx;  *outY = sy;  break;
      case 1: *outX = -sy; *outY = cx;  break;
      case 2: *outX = -cx; *outY = -sy; break;
      default: *outX = sy; *outY = -cx; break;
   }
}

uint32_t Trig_Angle(int32_t x, int32_t y)
{
   uint32_t a = (uint32_t)((x < 0) ? -x : x);
   uint32_t b = (uint32_t)((y < 0) ? -y : y);
   uint32_t q, ang, t;
   uint8_t steep = (uint8_t)(b > a);

   if (steep) { t = a; a = b; b = t; }                           /* now a >= b: the ratio is at most 1 */
   if (a == 0u) { return 0u; }
   q = (b == a) ? 0xFFFFFFFFUL : RatioQ32(b, a);
   t = Hi(Nested(kAtanCoef, 7u, Hi(q, q)), q);                   /* atan(q) * 4/pi, Q30 */
   ang = t >> 1;                                                 /* turns Q32 within the first octant */
   if (steep) { ang = 0x40000000UL - ang; }                      /* ... and within the first quadrant */
   if (x < 0) { return (y < 0) ? (0x80000000UL + ang) : (0x80000000UL - ang); }
   return (y < 0) ? (0u - ang) : ang;
}

uint32_t Trig_ArcLength(uint32_t r, uint32_t sweep)
{
   /* sweep * 2*pi in Q29 (0xC90FDAA2 is both pi/2 in Q31 and 2*pi in Q29), times r. The radius is
    * pre-shifted to use the word: r * g / 2^22, then 7 bits down to r * g / 2^29. */
   return (Hi(r << 10, Hi(sweep, 0xC90FDAA2UL)) + 64u) >> 7;
}
