/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
/* Host test of the Trig library against long-double trigonometry.
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_trig.c ../../firmware/src/Trig.c -lm -o out/test_trig
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Trig.h"
static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 12) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
int main(void)
{
   double worstR = 0, worstA = 0, worstRT = 0; long nR = 0, nA = 0; srand(5);
   /* rotation: random angles and radii up to 2 m */
   for (int i = 0; i < 3000000; i++)
   {
      uint32_t r = (uint32_t)(rand() % 2000000) + ((i < 10) ? 2000000u : 0u);
      uint32_t a = ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand();
      int32_t x, y; Trig_Rotate(r, a, &x, &y);
      long double th = (long double)a / 4294967296.0L * 2 * M_PIl;
      double e = fmax(fabs(x - r * cosl(th)), fabs(y - r * sinl(th))); if (e > worstR) worstR = e; nR++;
   }
   /* the 8 octant boundaries and 16 neighbours of each, exactly */
   for (uint32_t k = 0; k < 8; k++) for (int d = -8; d <= 8; d++)
   {
      uint32_t a = (k << 29) + (uint32_t)d; int32_t x, y; Trig_Rotate(1000000, a, &x, &y);
      long double th = (long double)a / 4294967296.0L * 2 * M_PIl;
      double e = fmax(fabs(x - 1000000 * cosl(th)), fabs(y - 1000000 * sinl(th))); if (e > worstRT) worstRT = e;
   }
   /* angle: vectors from 16 um to 2 m, and the axes */
   for (int i = 0; i < 2000000; i++)
   {
      int mag = 4 + rand() % 18; int32_t x = (int32_t)(rand() % (1 << mag)) - (1 << (mag - 1)), y = (int32_t)(rand() % (1 << mag)) - (1 << (mag - 1));
      if (i % 50 == 0) x = 0; if (i % 53 == 0) y = 0; if (x == 0 && y == 0) continue;
      uint32_t a = Trig_Angle(x, y); long double want = atan2l(y, x); if (want < 0) want += 2 * M_PIl;
      double d = fabs((double)((long double)a / 4294967296.0L * 2 * M_PIl - want)); if (d > M_PI) d = 2 * M_PI - d;
      double um = d * hypot(x, y); if (um > worstA) worstA = um; nA++;
   }
   /* arc length: radius up to 2 m, sweeps from 1 unit to a full turn (passed as 0xFFFFFFFF) */
   double worstL = 0;
   for (int i = 0; i < 1000000; i++)
   {
      uint32_t r = (uint32_t)(rand() % 2000000) + 1u;
      uint32_t sw = (i < 3) ? 0xFFFFFFFFUL : (((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand()) >> (rand() % 24);
      long double want = (long double)r * sw / 4294967296.0L * 2 * M_PIl;
      double e = fabs((double)(Trig_ArcLength(r, sw) - want)); if (e > worstL) worstL = e;
   }
   printf("Trig_ArcLength: 1 000 000 cases up to 2 m, worst error %.3f um (a full turn at 2 m is 12.57 m)\n", worstL);
   CHECK(worstL <= 0.51, "arc length error %.3f", worstL);
   CHECK(Trig_Angle(0, 0) == 0, "zero vector"); CHECK(Trig_Angle(1000, 0) == 0, "+X"); CHECK(Trig_Angle(0, 1000) == 0x40000000UL, "+Y");
   CHECK(Trig_Angle(-1000, 0) == 0x80000000UL, "-X"); CHECK(Trig_Angle(0, -1000) == 0xC0000000UL, "-Y");
   printf("Trig_Rotate: %ld random cases up to 2 m, worst error %.3f um; octant boundaries +-8 units, worst %.3f um\n", nR, worstR, worstRT);
   printf("Trig_Angle:  %ld vectors 16 um..2 m, worst displacement at the vector's own radius %.3f um\n", nA, worstA);
   CHECK(worstR < 1.0 && worstRT < 1.0 && worstA < 1.0, "outside 1 um");
   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
