/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of Motion's integer arithmetic against exact references (long double, corrected by exact 64-bit squares):
 * the 64-bit root, the inverse kinematics, and the length-to-sub-step conversion. Motion.c is #included so that
 * its statics are reachable. Built for either arithmetic (REQ-CoreLR-D-Motion-0024): add
 * -DCORELR_MATH=CORELR_MATH_64 or =CORELR_MATH_48 (the default is the one in SwConfig.h); both must pass.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Ishim -I../../firmware/src test_arith.c ../../firmware/src/Trig.c -lm -o out/test_arith
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../../firmware/src/Motion.c"

Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 12) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static uint64_t rnd64(void) { return ((uint64_t)rand() << 40) ^ ((uint64_t)rand() << 20) ^ (uint64_t)rand() ^ ((uint64_t)rand() << 60); }

static uint32_t ref_isqrt(uint64_t v)
{
   uint64_t r = (uint64_t)sqrtl((long double)v);
   if (r > 0xFFFFFFFFULL) r = 0xFFFFFFFFULL;
   while (r * r > v) r--;                                   /* r <= 2^32-1, so r*r fits in 64 bits */
   while (r < 0xFFFFFFFFULL && (r + 1) * (r + 1) <= v) r++;
   return (uint32_t)r;
}

int main(void)
{
   srand(9);
   printf("arithmetic under test: %s\n", CORELR_MATH == CORELR_MATH_64 ? "CORELR_MATH_64 (64-bit values)" : "CORELR_MATH_48 (nothing wider than 32 bits)");
#if CORELR_MATH == CORELR_MATH_64
   /* 1. the root: edges, squares and their neighbours, random at every magnitude */
   {
      long n = 0;
      uint64_t edge[] = { 0, 1, 2, 3, 4, 15, 16, 17, 99999, 1000000000000ULL, 0xFFFFFFFFULL, 0x100000000ULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFE00000001ULL, 0xFFFFFFFE00000000ULL };
      for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) { CHECK(Motion_ISqrt(edge[i]) == ref_isqrt(edge[i]), "root of %llu", (unsigned long long)edge[i]); n++; }
      for (uint64_t k = 1; k < 3000000; k += 997) { uint64_t sq = k * k; for (int d = -1; d <= 1; d++) { uint64_t v = sq + (uint64_t)d; CHECK(Motion_ISqrt(v) == ref_isqrt(v), "root of %llu", (unsigned long long)v); n++; } }
      for (int i = 0; i < 400000; i++) { uint64_t v = rnd64() >> (rand() % 64); CHECK(Motion_ISqrt(v) == ref_isqrt(v), "root of %llu", (unsigned long long)v); n++; }
      printf("root: %ld values, all exact\n", n);
   }
#endif
   /* 1b. the length of a vector, exact: edges, squares +-1, Pythagorean triples, random at every magnitude, every sign.
    * Inside |x|, |y| < 2^22 in both builds; the 64-bit build is also checked far outside it. */
   {
      long n = 0; const int32_t lim = (int32_t)CORELR_MATH_48_LIMIT_UM;
      const int32_t edge[] = { 0, 1, 2, 3, 15, 16, 17, 255, 256, 32767, 32768, 32769, 65535, 65536, 1000000, 2097151, 2097152, 4194302, 4194303 };
      for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) for (unsigned j = 0; j < sizeof edge / sizeof edge[0]; j++)
         for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2)
         {
            int32_t x = sx * edge[i], y = sy * edge[j]; uint64_t v = (uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y);
            CHECK(Motion_Hypot(x, y) == ref_isqrt(v), "Hypot(%ld,%ld) = %lu, want %lu", (long)x, (long)y, (unsigned long)Motion_Hypot(x, y), (unsigned long)ref_isqrt(v)); n++;
         }
      for (int32_t k = 1; k <= lim; k += 1 + k / 300)           /* on and next to the axes and the exact squares */
         for (int d = -1; d <= 1; d++) { int32_t y = d; uint64_t v = (uint64_t)k * k + (uint64_t)(d * d); CHECK(Motion_Hypot(k, y) == ref_isqrt(v), "Hypot(%ld,%d)", (long)k, y); n++; }
      for (int32_t m = 1; m < 2000; m++) for (int32_t q = 1; q < m; q += 7)    /* Pythagorean triples (m^2-q^2, 2mq, m^2+q^2) */
      {
         int64_t a = (int64_t)m * m - (int64_t)q * q, b = 2LL * m * q, c = (int64_t)m * m + (int64_t)q * q;
         if (c > lim || a > lim || b > lim) continue;
         CHECK(Motion_Hypot((int32_t)a, (int32_t)b) == (uint32_t)c, "triple %lld %lld %lld: got %lu", (long long)a, (long long)b, (long long)c, (unsigned long)Motion_Hypot((int32_t)a, (int32_t)b)); n++;
      }
      for (long i = 0; i < 12000000; i++)
      {
         int bx = 1 + rand() % 22, by = 1 + rand() % 22;
         int32_t x = (int32_t)(rnd64() % ((1ull << bx))), y = (int32_t)(rnd64() % ((1ull << by)));
         if (x > lim) x = lim;
         if (y > lim) y = lim;
         if (rand() & 1) x = -x;
         if (rand() & 1) y = -y;
         uint64_t v = (uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y);
         CHECK(Motion_Hypot(x, y) == ref_isqrt(v), "Hypot(%ld,%ld) = %lu, want %lu", (long)x, (long)y, (unsigned long)Motion_Hypot(x, y), (unsigned long)ref_isqrt(v)); n++;
      }
      printf("Motion_Hypot: %ld vectors with |x|,|y| < 2^22 (edges, every exact square +-1, Pythagorean triples, random at every magnitude), all exact\n", n);
#if CORELR_MATH == CORELR_MATH_64
      for (int i = 0; i < 200000; i++)
      {
         int32_t x = (int32_t)rnd64(), y = (int32_t)rnd64(); uint64_t ax = (uint64_t)(x < 0 ? -(int64_t)x : (int64_t)x), ay = (uint64_t)(y < 0 ? -(int64_t)y : (int64_t)y);
         CHECK(Motion_Hypot(x, y) == ref_isqrt(ax * ax + ay * ay), "wide Hypot(%ld,%ld)", (long)x, (long)y);
      }
      printf("  and 200 000 vectors over the whole int32 range (the 64-bit build only)\n");
#else
      /* beyond the bound: saturated, never a hang -- a component is clamped to the limit */
      CHECK(Motion_Hypot(2147483647L, 0) == (uint32_t)lim, "a huge x is not saturated: %lu", (unsigned long)Motion_Hypot(2147483647L, 0));
      CHECK(Motion_Hypot((int32_t)0x80000000L, 5) == ref_isqrt((uint64_t)lim * lim + 25u), "INT32_MIN is not saturated");
      CHECK(Motion_Hypot(-2147483647L, 2147483647L) == ref_isqrt(2ull * lim * lim), "both huge: not saturated");
      CHECK(Motion_Hypot(5000000L, 0) == (uint32_t)lim, "5 m is not saturated");
      printf("  beyond 2^22: saturated at %ld um (a wrong length, not a hang)\n", (long)lim);
#endif
   }
   /* 2. inverse kinematics against the exact formula, over the work area and well beyond it */
   {
      long n = 0;
      for (long x = -1500000; x <= 1500000; x += 37771) for (long y = -1500000; y <= 1500000; y += 41233)
      {
         Motion_Position_t p = { x, y }; Motion_CableLengths_t lr = Motion_XYToLR(p);
         int64_t ix = x + 500000LL, iy = (CORELR_Y_UP ? 489898LL - y : 489898LL + y), dx = ix - 1000000LL;   /* CORELR_Y_UP: drawing +Y is UP, the internal Y (down) is the offset minus it */
         CHECK(lr.L == (int32_t)ref_isqrt((uint64_t)(ix * ix + iy * iy)) && lr.R == (int32_t)ref_isqrt((uint64_t)(dx * dx + iy * iy)), "XYToLR(%ld,%ld) = %ld,%ld", x, y, (long)lr.L, (long)lr.R); n++;
      }
      printf("XYToLR: %ld positions, exact\n", n);
      /* the orientation itself, as a physical fact rather than a formula: up the wall (+Y) shortens BOTH cables, down lengthens both,
       * and a move along X changes them in opposite senses (REQ-CoreLR-D-Motion-0025, REQ-CoreLR-F-GCodeInterpreter-0014) */
      {
         Motion_Position_t o = { 0, 0 }, up = { 0, 50000 }, dn = { 0, -50000 }, rt = { 50000, 0 };
         Motion_CableLengths_t c = Motion_XYToLR(o), cu = Motion_XYToLR(up), cd = Motion_XYToLR(dn), cr = Motion_XYToLR(rt);
         const int up_ = CORELR_Y_UP;   /* with CORELR_Y_UP = 0 the senses of Y are exchanged */
         CHECK(up_ ? (cu.L < c.L && cu.R < c.R) : (cu.L > c.L && cu.R > c.R), "+Y with CORELR_Y_UP=%d: wrong sense (L %ld->%ld, R %ld->%ld)", up_, (long)c.L, (long)cu.L, (long)c.R, (long)cu.R);
         CHECK(up_ ? (cd.L > c.L && cd.R > c.R) : (cd.L < c.L && cd.R < c.R), "-Y with CORELR_Y_UP=%d: wrong sense", up_);
         CHECK(cr.L > c.L && cr.R < c.R, "+X must be to the right: L longer, R shorter");
         printf("  orientation: +Y 50 mm -> L %ld R %ld (from %ld, %ld): up the wall shortens both cables\n", (long)cu.L, (long)cu.R, (long)c.L, (long)c.R);
      }
   }
   /* 3. length -> 1/256 step, against the exact rounded quotient */
   {
      long n = 0, worst = 0;
      for (int32_t l = 0; l < 4000000; l += 13) { long double e = (long double)l * 2037.886L * 256.0L / 109955.0L; int32_t want = (int32_t)floorl(e + 0.5L); long d = labs((long)IdealSubSteps(l) - want); if (d > worst) worst = d; n++; }
      printf("IdealSubSteps: %ld lengths up to 4 m, worst difference from the exact rounded quotient %ld sub-step (1/256 of a step)\n", n, worst);
      CHECK(worst <= 1, "IdealSubSteps is %ld sub-steps off", worst);
   }
   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
