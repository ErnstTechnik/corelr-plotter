/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* hypot_bound.c -- checks the bound on which CORELR_MATH_48's Motion_Hypot stands (REQ-CoreLR-D-Motion-0024).
 *
 * For a root r0 correct to s bits (the coordinates shifted down until each is under 2^15, the root of the shifted sum of
 * squares, shifted back), the TRUE residual v - r0^2 must stay below 2^32, because Motion_Hypot knows it only from the LOW
 * 32 bits of v. This hunts the worst case over adversarial coordinates (the patterns that truncate the most) and reports
 * the largest true residual, as a fraction of 2^32, and the most iterations the downward fix-up loop ever ran.
 * Measured 2026-10-08, 60 M cases: 0.847 x 2^32 (must be < 1), fix-up at most once.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static uint32_t isq(uint32_t v) { uint32_t r = (uint32_t)sqrt((double)v); while ((uint64_t)r * r > v) r--; while ((uint64_t)(r + 1) * (r + 1) <= v) r++; return r; }
int main(int argc, char **argv)
{
   long n = 60000000; unsigned seed = 3;
   for (int i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
      {
         printf("usage: hypot_bound [-h|--help] [-n CASES] [-s SEED]\n\n"
                "Hunts the worst true residual of Motion_Hypot's 48-bit method (see the top of hypot_bound.c).\n"
                "  -n CASES   how many random adversarial vectors (default 60000000, ~15 s)\n"
                "  -s SEED    random seed (default 3)\n"
                "Prints the cases tried, the largest shift s, the largest true residual / 2^32 (it must be < 1)\n"
                "and the most downward fix-up iterations. Exit status 1 if the residual reached 2^32.\n"
                "Build and run:  tools/prototypes/hypot_bound.sh [-n CASES]\n");
         return 0;
      }
      if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atol(argv[++i]);
      else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = (unsigned)atoi(argv[++i]);
      else { fprintf(stderr, "hypot_bound: unknown option '%s' (try -h)\n", argv[i]); return 2; }
   }
   uint64_t maxE = 0; int maxIt = 0, maxS = 0; long used = 0; srand(seed);
   for (long i = 0; i < n; i++)
   {
      uint32_t ax, ay;
      if (i % 3 == 0) { ax = (uint32_t)((rand() % 8192) * 512 + 511) & 0x3FFFFF; ay = (uint32_t)((rand() % 8192) * 512 + 511) & 0x3FFFFF; }
      else if (i % 3 == 1) { int s = 1 + rand() % 8; ax = ((((uint32_t)rand() << 8) ^ (uint32_t)rand()) & 0x3FFFFF) | ((1u << s) - 1); ay = ((((uint32_t)rand() << 8) ^ (uint32_t)rand()) & 0x3FFFFF) | ((1u << s) - 1); }
      else { ax = ((uint32_t)rand() * 37u) & 0x3FFFFF; ay = ((uint32_t)rand() * 91u) & 0x3FFFFF; }
      uint32_t xs = ax, ys = ay; int s = 0; while ((xs | ys) >= 0x8000u) { xs >>= 1; ys >>= 1; s++; }
      if (!s) continue; if (s > maxS) maxS = s;
      uint32_t r0 = isq(xs * xs + ys * ys) << s;
      uint64_t v = (uint64_t)ax * ax + (uint64_t)ay * ay, e = v - (uint64_t)r0 * r0; if (e > maxE) maxE = e;
      uint32_t vl = (uint32_t)v, r1 = r0 + (vl - r0 * r0) / (r0 << 1); int it = 0; while ((int32_t)(vl - r1 * r1) < 0) { r1--; it++; }
      if (it > maxIt) maxIt = it; used++;
   }
   double frac = (double)maxE / 4294967296.0;
   printf("%ld cases, max shift s=%d, max TRUE residual %.4f * 2^32 (must be < 1), max downward fix-up iterations %d\n", used, maxS, frac, maxIt);
   return frac >= 1.0;
}
