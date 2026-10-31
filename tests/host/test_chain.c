/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the look-ahead at work: the mean speed of a straight chain of equal short segments, with the BACK-PRESSURE of the real machine
 * (REQ-CoreLR-D-Motion-0013).
 *
 * Why this and not test_motion's chain: a stub that accepts every entry at once lets the generator finish each block before the next one arrives, so the planner
 * never holds more than one and the look-ahead has nothing to look at (the first version of this measurement said 6 mm/s for 0.2 mm segments, which is the speed
 * WITHOUT look-ahead, and was wrong for the machine). Here the step queue holds 32 entries and is drained in real time (holds in 2 MHz counts), one generator call costs
 * a fixed time (a waypoint: 3 072 cycles = 0.19 ms, tools/analysis/cyclesim), and the sender gives a line every few calls.
 * Motion.c is #included (its planner is read); Stepper and Calibration are stubs.
 * Prints the table of the planner depth this build has (MOTION_PLANNER_DEPTH: 8 in both variants since 2026-10-13; it was 4 on SD, 15.4 mm/s at 0.2 mm; `-D MOTION_PLANNER_DEPTH=4` is not supported, edit SwConfig.h to try another).
 * Realizes the figures of REQ-CoreLR-D-Motion-0013. Prints 'all checks passed' or the failing case. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "Motion.c"

Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }

static uint32_t fifo[64]; static int fh, ft, fn; static long clkCounts;
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{ (void)l; (void)r; (void)p; if (fn >= 32) return 0; fifo[ft] = h; ft = (ft + 1) & 63; fn++; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
static void drain(long counts)
{
   clkCounts += counts;
   while (counts > 0 && fn > 0) { if (fifo[fh] > (uint32_t)counts) { fifo[fh] -= (uint32_t)counts; counts = 0; } else { counts -= (long)fifo[fh]; fh = (fh + 1) & 63; fn--; } }
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 30) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* mean speed, mm/s, of 100 mm of `len` um segments along X; one generator call = iterUs, one line every feedEvery calls */
static double chain(int len, int iterUs, int feedEvery)
{
   int n = 100000 / len, fed = 0; long guard = 0; Motion_Position_t o = { 0, 0 };
   fh = ft = fn = 0; clkCounts = 0; Motion_Init(); Motion_SetCurrentPosition(o);
   while ((fed < n || !Motion_IsIdle() || s_planCount) && guard++ < 6000000L)
   {
      if (fed < n && (guard % feedEvery) == 0 && Motion_PlannerHasRoom()) { Motion_Position_t t = Motion_GetTargetPosition(); t.X += len; Motion_QueueMove(t); fed++; }
      Motion_Task();
      drain((long)iterUs * 2);
   }
   while (fn) drain(2000);
   return Motion_GetComputedPosition().X / 1000.0 / (clkCounts / 2e6);
}

int main(void)
{
   static const int lens[] = { 200, 500, 1000, 2000, 5000, 20000 };
   double vmax = MOTION_VMAX_V16 / 4.0;
   printf("Planner depth %u, VMAX %.2f mm/s, a generator call 190 us, a line every 3 calls (0.6 ms; 3 072 cycles per waypoint, cyclesim)\n  segment, mm   ", (unsigned)MOTION_PLANNER_DEPTH, vmax);
   for (unsigned k = 0; k < sizeof lens / sizeof *lens; k++) printf("%7.1f", lens[k] / 1000.0);
   printf("\n  mean speed    ");
   for (unsigned k = 0; k < sizeof lens / sizeof *lens; k++)
   {
      double v = chain(lens[k], 190, 3);
      printf("%7.1f", v);
      /* the horizon: v^2/(2a) = 1.2 mm to stop from VMAX, so DEPTH blocks of 0.2 mm cover 1.6 mm (serial), 0.8 mm (SD): the speed holds from about 0.3 mm (serial) / 0.5 mm (SD) up */
      CHECK(lens[k] < 500 || v > 0.93 * vmax, "%d um segments: %.1f mm/s, expected within 7 %% of VMAX %.1f", lens[k], v, vmax);
   }
   printf("\n  a slow sender (a line every 40 calls = 7.6 ms) with 0.5 mm segments: %.1f mm/s\n", chain(500, 190, 40));
   CHECK(chain(500, 190, 40) > 0.9 * vmax, "a line every 7.6 ms starved 0.5 mm segments");
   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
