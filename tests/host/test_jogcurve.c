/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Control jog's response (REQ-CoreLR-D-LcdJoystickUI-0021): the real JogCurve.c with the real Motion_Hypot, against the formula in floating point.
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_jogcurve.c ../../firmware/src/{Motion,Trig}.c -lm -o out/test_jogcurve
 * Run for both ways of computing the shape: -DJOGCURVE_USE_TABLE=1 (default) and =0.
 *
 * The curve: speed = Vmax * n^2, n = (norm - dead zone) / (full scale - dead zone), clamped to 0..1, the pen moving in the stick's direction. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "../../firmware/src/JogCurve.c"        /* #included, so Shape() and the constants are visible */
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"

Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 25) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define DZ LCDJOYSTICKUI_DEADZONE
#define FS LCDJOYSTICKUI_STICK_FULL_SCALE
#define VMAX LCDJOYSTICKUI_JOG_MAX_SPEED       /* um per ms = mm/s */

/* the curve in floating point: displacement magnitude in um over `ms`, for a stick of norm m */
static double ideal(double m, double ms)
{
   double n = (m - DZ) / (double)(FS - DZ);
   if (n < 0) n = 0; if (n > 1) n = 1;
   return VMAX * n * n * ms;
}
static void one(int dx, int dy, int ms, int32_t *x, int32_t *y) { JogCurve_Reset(); JogCurve_Displacement((int16_t)dx, (int16_t)dy, (uint16_t)ms, x, y); }

int main(void)
{
   int32_t x, y; int d, k;
   printf("JogCurve: shape %s, dead zone %d, full scale %d, Vmax %ld mm/s\n", JOGCURVE_USE_TABLE ? "tabulated" : "computed", DZ, FS, (long)VMAX);

   printf("1. Inside the dead zone nothing moves; just outside it the speed is ~0 (no jump)\n");
   for (d = -DZ; d <= DZ; d++) { one(d, 0, 30, &x, &y); CHECK(x == 0 && y == 0, "(%d,0) moved (%ld,%ld)", d, (long)x, (long)y); }
   { int ok = 1; for (k = 0; k <= DZ; k++) { int h = (int)floor(sqrt((double)DZ * DZ - (double)k * k)); one(k, h, 30, &x, &y); if (x || y) ok = 0; } CHECK(ok, "a point of the dead zone's circle moved"); }
   one(DZ + 1, 0, 30, &x, &y); CHECK(x >= 0 && x <= 1, "one past the dead zone: %ld um in 30 ms (a jump)", (long)x);
   one(DZ + 2, 0, 30, &x, &y); CHECK(x <= 1, "two past the dead zone: %ld um in 30 ms", (long)x);

   printf("2. Along an axis: the quadratic of the formula, to 2 um, rising, flat from the full scale\n");
   { double worst = 0; long last = -1; int mono = 1;
     for (d = DZ + 1; d <= 511; d++)
     {
        double want = ideal(d, 30); one(d, 0, 30, &x, &y);
        { double e = fabs((double)x - want); if (e > worst) worst = e; }
        if (x < last) mono = 0; last = x;
        CHECK(y == 0, "a push along X moved Y by %ld at %d", (long)y, d);
     }
     printf("   worst difference from the formula over the whole stick: %.2f um\n", worst);
     CHECK(worst <= 2.0, "the curve is %.2f um from the formula", worst); CHECK(mono, "the displacement fell while the stick was pushed further");
     one(FS, 0, 30, &x, &y); CHECK(x >= VMAX * 30 - 2 && x <= VMAX * 30, "full scale: %ld um in 30 ms, expected %ld", (long)x, (long)(VMAX * 30));
     one(511, 0, 30, &x, &y); CHECK(x >= VMAX * 30 - 2 && x <= VMAX * 30, "beyond the full scale it is clamped: %ld", (long)x);
     one(0, FS, 30, &x, &y); CHECK(y >= VMAX * 30 - 2 && y <= VMAX * 30 && x == 0, "full scale in Y: (%ld,%ld)", (long)x, (long)y); }

   printf("3. The speed is the norm of the vector, in the stick's direction\n");
   { double worstMag = 0, worstAng = 0; int n = 0;
     for (d = 60; d <= 500; d += 20) for (k = 0; k < 360; k += 3)
     {
        int dx = (int)lround(d * cos(k * M_PI / 180)), dy = (int)lround(d * sin(k * M_PI / 180));
        double m = hypot(dx, dy), mag, ang, want = ideal(m, 60);
        if (m <= DZ) continue;
        one(dx, dy, 60, &x, &y); mag = hypot((double)x, (double)y);
        { double e = fabs(mag - want); if (e > worstMag) worstMag = e; }
        if (mag > 150) { ang = fabs(atan2((double)y, (double)x) - atan2((double)dy, (double)dx)); if (ang > M_PI) ang = 2 * M_PI - ang; if (ang * 180 / M_PI > worstAng) worstAng = ang * 180 / M_PI; }
        n++;
     }
     printf("   %d stick positions over the whole disc: magnitude within %.2f um of the formula, direction within %.3f degrees\n", n, worstMag, worstAng);
     CHECK(worstMag <= 0.012 * 2400 + 3, "the displacement's magnitude is %.2f um from the formula", worstMag); CHECK(worstAng <= 0.6, "the direction is %.3f degrees off", worstAng); }
   one(FS, FS, 30, &x, &y); { double mag = hypot((double)x, (double)y); CHECK(fabs(mag - VMAX * 30) <= 3, "a corner of the square stick is %.1f um, expected %ld (the speed is capped, not per axis)", mag, (long)(VMAX * 30)); }
   one(511, 511, 30, &x, &y); { double mag = hypot((double)x, (double)y); CHECK(fabs(mag - VMAX * 30) <= 3, "the extreme corner is %.1f um", mag); }

   printf("4. Symmetric, and proportional to the time\n");
   { int bad = 0; for (d = 40; d <= 500; d += 17) { int32_t a, b, c, e; one(d, d / 2, 30, &a, &b); one(-d, -d / 2, 30, &c, &e); if (labs(a + c) > 1 || labs(b + e) > 1) bad++; }
     CHECK(!bad, "%d points are not symmetric to the opposite stick position", bad); }
   { int bad = 0; for (d = 40; d <= 500; d += 17) { int32_t a, b, c, e; one(d, 0, 30, &a, &b); one(d, 0, 60, &c, &e); if (labs(c - 2 * a) > 2) bad++; }
     CHECK(!bad, "%d points: 60 ms is not twice 30 ms", bad); }

   printf("5. What is below a micrometre is carried, not lost\n");
   /* The expected total is computed with the module's own integer norm and shape (the table's interpolation, and the division by an integer norm, make the SPEED
    * a fraction of a per cent off the pure formula: that is checked in 2 and 3); what is tested here is that the sum of the whole-micrometre blocks equals the exact sum. */
   { int stick, sgn; for (sgn = -1; sgn <= 1; sgn += 2) for (stick = DZ + 8; stick <= DZ + 300; stick += 29)
     { int32_t sumX = 0, sumY = 0; int i, sy = stick / 3; double exX, exY, scaled;
       uint32_t m = Motion_Hypot(stick, sy), r = m - DZ, n;
       if (r > (uint32_t)JOGCURVE_RANGE) r = JOGCURVE_RANGE;
       n = (uint16_t)((r * (uint32_t)JOGCURVE_K) >> 12); if (n > 4096) n = 4096;
       scaled = (double)(((uint32_t)LCDJOYSTICKUI_JOG_MAX_SPEED * (uint32_t)Shape((uint16_t)n) * 30u) >> 6) / 1024.0;      /* um along the stick, per block */
       JogCurve_Reset();
       for (i = 0; i < 2000; i++) { JogCurve_Displacement((int16_t)(sgn * stick), (int16_t)(sgn * sy), 30, &x, &y); sumX += x; sumY += y; }
       exX = sgn * scaled * 2000 * stick / (double)m; exY = sgn * scaled * 2000 * sy / (double)m;
       CHECK(fabs(sumX - exX) <= 4 && fabs(sumY - exY) <= 4, "stick %d (sign %d): 2000 blocks moved (%ld,%ld) um, exactly (%.1f,%.1f): distance was lost", stick, sgn, (long)sumX, (long)sumY, exX, exY);
       if (sgn == 1 && stick == DZ + 8 + 29 * 2) printf("   e.g. stick %d: %.2f um a block, 2000 blocks moved (%ld,%ld) um against (%.1f,%.1f) exactly\n", stick, scaled, (long)sumX, (long)sumY, exX, exY); } }
   { int32_t a, b, c, e;                                              /* Reset and a centred stick clear the remainder */
     JogCurve_Reset(); JogCurve_Displacement(DZ + 40, 0, 30, &a, &b); JogCurve_Displacement(DZ + 40, 0, 30, &a, &b);
     JogCurve_Displacement(0, 0, 30, &a, &b); JogCurve_Displacement(DZ + 40, 0, 30, &c, &e);
     one(DZ + 40, 0, 30, &a, &b); CHECK(c == a, "a centred stick did not clear the carried remainder (%ld, from rest %ld)", (long)c, (long)a);
     /* the stick comes back to near the centre, not exactly to it: anywhere in the dead zone it is "centred" (the mutant 'm < 1' only knows the exact centre) */
     { int k; int16_t inx[4], iny[4];
       inx[0] = 1; iny[0] = 0; inx[1] = 0; iny[1] = 1; inx[2] = DZ; iny[2] = 0; inx[3] = 0; iny[3] = DZ;
       for (k = 0; k < 4; k++)
       { JogCurve_Reset(); JogCurve_Displacement(DZ + 40, 0, 30, &a, &b); JogCurve_Displacement(DZ + 40, 0, 30, &a, &b);
         CHECK(s_remainderX != 0, "the test needs a non-zero remainder to start from (it is %d)", (int)s_remainderX);
         JogCurve_Displacement(inx[k], iny[k], 30, &a, &b);
         CHECK(s_remainderX == 0 && s_remainderY == 0, "a stick at (%d,%d), inside the dead zone, left a remainder (%d,%d): it is 'centred' there too", inx[k], iny[k], (int)s_remainderX, (int)s_remainderY);
         JogCurve_Displacement(DZ + 40, 0, 30, &c, &e);
         one(DZ + 40, 0, 30, &a, &b);
         CHECK(c == a, "a stick at (%d,%d), inside the dead zone, did not clear the carried remainder (%ld, from rest %ld)", inx[k], iny[k], (long)c, (long)a); } } }

   printf("6. Bounds: no overflow at the extremes, for the longest block\n");
   { int bad = 0; int sx, sy; for (sx = -511; sx <= 511; sx += 511) for (sy = -511; sy <= 511; sy += 511) { one(sx, sy, LCDJOYSTICKUI_JOG_SLICE_MAX_MS, &x, &y);
        { double mag = hypot((double)x, (double)y); double lim = VMAX * (double)LCDJOYSTICKUI_JOG_SLICE_MAX_MS; if (mag > lim + 3) bad++; if ((sx || sy) && mag < lim - 3) bad++; } }
     CHECK(!bad, "%d extreme corners are not at the speed limit", bad); }

   printf("7. JogCurve_Fraction(): the shape alone (the L/R jog uses it, REQ-CoreLR-D-LcdJoystickUI-0027)\n");
   { int m, bad = 0, mono = 1; unsigned prev = 0; double worst = 0;
     for (m = 0; m <= 600; m++)
     {
        unsigned f = JogCurve_Fraction((uint16_t)m);
        double r = (m <= DZ) ? 0.0 : (double)((m > FS ? FS : m) - DZ) / (double)(FS - DZ);
        double want = r * r * 65535.0;                              /* the table is quadratic (gen_jogcurve.py -e 2) */
        if (m <= DZ && f != 0u) bad++;
        if (f < prev) mono = 0;
        prev = f;
        if (fabs((double)f - want) > worst) worst = fabs((double)f - want);
     }
     CHECK(bad == 0, "%d points of the dead zone have a non-zero fraction", bad);
     CHECK(mono, "the fraction fell while the stick was pushed further");
     CHECK(JogCurve_Fraction((uint16_t)FS) == 65535u && JogCurve_Fraction(600) == 65535u, "full scale is %u, beyond it %u: both must be 65535", JogCurve_Fraction((uint16_t)FS), JogCurve_Fraction(600));
     { unsigned long wrong = 0; unsigned a; for (a = 512; a <= 65535u; a++) if (JogCurve_Fraction((uint16_t)a) != 65535u) wrong++;
       CHECK(wrong == 0, "%lu magnitudes beyond the full scale (up to 65535) do not give 65535 (n would overflow 16 bits)", wrong); }
     CHECK(worst <= 60.0, "the fraction is %.0f (of 65535) from the quadratic", worst);
     /* the same number drives the displacement: one axis, Displacement == Vmax * Fraction * ms / 64 (to the carried micrometre) */
     for (m = DZ + 1; m <= FS; m += 7)
     { int32_t x, y; JogCurve_Reset(); JogCurve_Displacement(0, (int16_t)m, 40, &x, &y);
       { double want = (double)VMAX * (double)JogCurve_Fraction((uint16_t)m) * 40.0 / 65536.0; if (fabs((double)y - want) > 1.5) bad++; } }
     CHECK(bad == 0, "%d points where the displacement is not Vmax * Fraction * time", bad);
     printf("   0..600 counts: fraction within %.0f/65535 of the quadratic, monotone, 0 in the dead zone, 65535 from full scale\n", worst); }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
