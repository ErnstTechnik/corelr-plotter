/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "cordic.h"
int main(void)
{
   double worstR = 0, worstV = 0, worstL = 0; long n = 0; srand(5);
   for (int i = 0; i < 2000000; i++)
   {
      uint32_t r = (uint32_t)(rand() % 2000000) + ((i < 10) ? 2000000u : 0u);  /* up to 2 m */
      uint32_t a = ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand();
      int32_t x, y; Cordic_Rotate(r, a, &x, &y);
      double th = (double)a / 4294967296.0 * 2 * M_PI;
      double ex = fabs(x - r * cos(th)), ey = fabs(y - r * sin(th)); double e = ex > ey ? ex : ey; if (e > worstR) worstR = e;
      /* and back: the angle and length of the vector we just made */
      if (r > 1000) { uint32_t len; uint32_t b = Cordic_Vector(x, y, &len); double d = fabs((double)(int32_t)(b - a)) / 4294967296.0 * 2 * M_PI * r; if (d > worstV) worstV = d; double el = fabs((double)len - sqrt((double)x * x + (double)y * y)); if (el > worstL) worstL = el; }
      n++;
   }
   /* the cardinal directions and tiny vectors */
   { uint32_t len; int32_t xs[] = { 1, 0, -1, 0, 5, -5, 3, 100000 }, ys[] = { 0, 1, 0, -1, 5, 5, -4, -100000 };
     for (int i = 0; i < 8; i++) { uint32_t a = Cordic_Vector(xs[i], ys[i], &len); printf("  vector (%d,%d): angle %.6f turn, length %u\n", xs[i], ys[i], a / 4294967296.0, len); } }
   printf("rotate: %ld cases, worst error %.3f um (radius up to 2 m)\n", n, worstR);
   printf("vector: worst angle error %.3f um at the radius, worst length error %.3f um\n", worstV, worstL);
   return worstR > 1.0;
}
