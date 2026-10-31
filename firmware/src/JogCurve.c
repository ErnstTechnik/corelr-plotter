/*
 * JogCurve.c
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
 * @file JogCurve.c
 * @brief See JogCurve.h.
 *
 * All 32-bit, no floating point, no 64-bit. The chain, with its widest intermediate at each step:
 *   m      = norm of (dx, dy)                                    Motion_Hypot, <= 723
 *   r      = m - dead zone, clamped to the range that is left     <= 468
 *   n      = r / range in Q12 (0..4096), by a reciprocal multiply  r * K <= 1.7e7
 *   f      = shape(n) in Q16 (0..65535): the table, interpolated
 *   disp   = Vmax * f * elapsed          micrometres * 2^16      <= 40 * 65535 * 60 = 1.6e8
 *   out    = ((disp >> 6) * dx) / m      micrometres * 2^10      <= 2.5e6 * 511 = 1.3e9, signed; then the remainder is carried and the whole micrometres returned
 * (The direction is dx / m exactly, by one multiplication and one division: a unit vector held in Q8 first was tried and put the magnitude up to
 * 0.55 % off, 14 micrometres in 2400.)
 */

#include <avr/pgmspace.h>
#include "JogCurve.h"
#include "HwConfig.h"
#include "SwConfig.h"
#include "Motion.h"

#define JOGCURVE_RANGE (LCDJOYSTICKUI_STICK_FULL_SCALE - LCDJOYSTICKUI_DEADZONE)   /**< counts of deflection between the dead zone and full speed */
#if LCDJOYSTICKUI_STICK_FULL_SCALE <= LCDJOYSTICKUI_DEADZONE
#error "LCDJOYSTICKUI_STICK_FULL_SCALE (HwConfig.h) must exceed LCDJOYSTICKUI_DEADZONE (SwConfig.h): the curve would have no range"
#endif
#if LCDJOYSTICKUI_STICK_FULL_SCALE > 511
#error "LCDJOYSTICKUI_STICK_FULL_SCALE cannot exceed 511: the 10-bit ADC reads at most 511 either side of its centre"
#endif
/** n = r * K >> 12 maps r in 0..RANGE to 0..4096 (Q12), K being 16 bits so that the product is a 16 x 16 -> 32 multiplication (a library call of a few dozen bytes, not a 32 x 32 one).
 *  Rounded UP, so that r = RANGE gives n = 4096 exactly, not 4095. Q8 was tried first and is too coarse: the quantisation of n alone put the speed up to 0.8 % (9 micrometres in 1200) off the curve. */
#define JOGCURVE_K ((4096UL * 4096UL + (JOGCURVE_RANGE) - 1UL) / (JOGCURVE_RANGE))
#if JOGCURVE_K > 65535UL
#error "JOGCURVE_K does not fit 16 bits: LCDJOYSTICKUI_STICK_FULL_SCALE - LCDJOYSTICKUI_DEADZONE is too small"
#endif
#if (LCDJOYSTICKUI_JOG_MAX_SPEED * 65535UL * LCDJOYSTICKUI_JOG_SLICE_MAX_MS) > 0x7FFFFFFFUL
#error "LCDJOYSTICKUI_JOG_MAX_SPEED * LCDJOYSTICKUI_JOG_SLICE_MAX_MS overflows the 32-bit displacement"
#endif

#ifndef JOGCURVE_USE_TABLE
#define JOGCURVE_USE_TABLE 0      /**< 0: f = n^2 computed (default, -124 bytes); 1: 33-entry table, linear interpolation, for another exponent (@req{REQ-CoreLR-D-LcdJoystickUI-0021}) */
#endif
#if JOGCURVE_USE_TABLE
#include "JogCurveTable.h"
#endif

static int16_t s_remainderX;      /**< micrometres * 2^10 not yet returned, in 0 .. 1023 */
static int16_t s_remainderY;      /**< same, for Y */

void JogCurve_Reset(void)
{
   s_remainderX = 0;
   s_remainderY = 0;
}

/** Shape: n in Q12 (0..4096) -> f in Q16 (0..65535). */
static uint16_t Shape(uint16_t n)
{
#if JOGCURVE_USE_TABLE
   uint16_t i = (uint16_t)(n >> 7);                 /* 32 intervals of 128 */
   uint16_t a;
   uint16_t b;

   if (i >= JOGCURVE_TABLE_STEPS)
   {
      return pgm_read_word(&kJogCurve[JOGCURVE_TABLE_STEPS]);
   }
   a = pgm_read_word(&kJogCurve[i]);
   b = pgm_read_word(&kJogCurve[i + 1u]);
   return (uint16_t)(a + (uint16_t)((((uint32_t)(uint16_t)(b - a)) * (n & 127u)) >> 7));
#else
   uint32_t f = ((uint32_t)n * n) >> 8;             /* Q24 -> Q16 */
   return (uint16_t)((f > 65535UL) ? 65535UL : f);
#endif
}

/** Add `part` (micrometres * 2^10) to a carried remainder; return the whole micrometres, keep the rest (always 0..1023, so a negative part floors). */
static int32_t Carry(int16_t *remainder, int32_t part)
{
   int32_t total = (int32_t)*remainder + part;

   *remainder = (int16_t)(total & 1023L);     /* two's complement: total = whole * 1024 + (total & 1023), also for a negative total (it floors) */
   return total >> 10;                        /* arithmetic shift */
}

uint16_t JogCurve_Fraction(uint16_t magnitude)
{
   uint16_t r;
   uint16_t n;

   if (magnitude <= (uint16_t)LCDJOYSTICKUI_DEADZONE)
   {
      return 0;
   }
   r = (uint16_t)(magnitude - (uint16_t)LCDJOYSTICKUI_DEADZONE);
   if (r > (uint16_t)JOGCURVE_RANGE)
   {
      r = (uint16_t)JOGCURVE_RANGE;
   }
   n = (uint16_t)(((uint32_t)r * (uint32_t)(uint16_t)JOGCURVE_K) >> 12);
   if (n > 4096u)
   {
      n = 4096u;   /* never reached while r is clamped (K is rounded up: r = RANGE gives exactly 4096); kept because measured: the build is 38 bytes SMALLER with it */
   }
   return Shape(n);
}

void JogCurve_Displacement(int16_t dx, int16_t dy, uint16_t elapsedMs, int32_t *outDxUm, int32_t *outDyUm)
{
   uint32_t m = Motion_Hypot((int32_t)dx, (int32_t)dy);
   int32_t scaled;

   if (m <= (uint32_t)LCDJOYSTICKUI_DEADZONE)
   {
      JogCurve_Reset();                       /* the stick is centred: nothing carried into the next push */
      *outDxUm = 0;
      *outDyUm = 0;
      return;
   }
   scaled = (int32_t)(((uint32_t)LCDJOYSTICKUI_JOG_MAX_SPEED * (uint32_t)JogCurve_Fraction((uint16_t)m) * (uint32_t)elapsedMs) >> 6);
   *outDxUm = Carry(&s_remainderX, (scaled * (int32_t)dx) / (int32_t)m);
   *outDyUm = Carry(&s_remainderY, (scaled * (int32_t)dy) / (int32_t)m);
}
