/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "cordic.h"
#include "cordic_tab.h"

/* 24 iterations: residual angle 2^-23 rad, i.e. 0.25 um at a 2 m radius. */
void Cordic_Rotate(uint32_t r, uint32_t angle, int32_t *outX, int32_t *outY)
{
   uint8_t quadrant = (uint8_t)(angle >> 30);
   uint32_t z = angle & 0x3FFFFFFFUL;                         /* the angle within its quadrant: 0 .. pi/2 */
   int32_t x = (int32_t)((((uint64_t)r << 8) * (uint32_t)CORDIC_INV_K_Q30) >> 30);   /* r / K, 8 fractional bits */
   int32_t y = 0;
   int32_t xs, ys;
   uint8_t i;

   for (i = 0; i < CORDIC_N; i++)
   {
      /* residual angle target: drive z to zero, signed through two's complement */
      int32_t dx = y >> i;
      int32_t dy = x >> i;

      if ((int32_t)z >= 0) { x -= dx; y += dy; z -= kCordicAtan[i]; }
      else                 { x += dx; y -= dy; z += kCordicAtan[i]; }
   }
   x = (x + 128) >> 8; y = (y + 128) >> 8;
   switch (quadrant)                                           /* rotate by quadrant * 90 degrees */
   {
      case 0: xs = x;  ys = y;  break;
      case 1: xs = -y; ys = x;  break;
      case 2: xs = -x; ys = -y; break;
      default: xs = y; ys = -x; break;
   }
   *outX = xs; *outY = ys;
}

uint32_t Cordic_Vector(int32_t x, int32_t y, uint32_t *outLength)
{
   uint32_t quadrantAngle = 0;
   int32_t xs = x, ys = y;
   uint32_t z = 0;
   uint8_t i;

   /* bring the vector into the first quadrant (x >= 0, y >= 0), remembering the angle taken off */
   if (xs < 0 && ys >= 0)       { quadrantAngle = 0x40000000UL; xs = ys; ys = -x; }          /* rotate by -90 */
   else if (xs < 0 && ys < 0)   { quadrantAngle = 0x80000000UL; xs = -x; ys = -y; }
   else if (xs >= 0 && ys < 0)  { quadrantAngle = 0xC0000000UL; xs = -y; ys = x; }
   xs <<= 8; ys <<= 8;
   for (i = 0; i < CORDIC_N; i++)
   {
      int32_t dx = ys >> i;
      int32_t dy = xs >> i;

      if (ys > 0) { xs += dx; ys -= dy; z += kCordicAtan[i]; }
      else        { xs -= dx; ys += dy; z -= kCordicAtan[i]; }
   }
   if (outLength)
   {
      *outLength = (uint32_t)(((uint64_t)(uint32_t)xs * (uint32_t)CORDIC_INV_K_Q30 + ((uint64_t)1 << 37)) >> 38);   /* /K, then drop 8 fractional bits */
   }
   return quadrantAngle + z;
}
