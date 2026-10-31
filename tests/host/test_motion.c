/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the real Motion.c as ported to the step-event generator (REQ-CoreLR-D-Motion-0018..0020).
 * Stepper, Calibration are stubs; every entry Motion sends is recorded as {coil bits, hold in Timer1 counts}.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_motion.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_motion
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "HwConfig.h"
#include "SwConfig.h"
#include "mirror_y.h"   /* -DMIRROR_Y: the same scenarios in the mirror image (see mirror_y.h) */

#define MAXE 400000
typedef struct { uint8_t l, r; uint32_t hold; } Ent;
static Ent *E; static int nE;

Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }

static uint32_t g_hash = 2166136261u; static long g_entries_total;   /* FNV-style hash of EVERY entry ever sent: a refactor must not change it */
static int g_refusePct; static unsigned g_rng = 12345; static int g_refused;
static int g_qcap, g_qcount;   /* when g_qcap > 0: the stepper queue has that many slots and the test drains it */
static uint32_t g_maxHold;   /* the largest hold of any entry ever accepted: a negative one wraps to ~4e9 */
static int g_gate;   /* Stepper_HaltRequests() as the real Stepper applies it: while set, every motion request is refused */
static unsigned rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{
   (void)p;
   if (g_gate) { return 0; }
   if (g_qcap && g_qcount >= g_qcap) { return 0; }   /* a bounded step queue, drained by the test as time passes */
   if (g_qcap) { g_qcount++; }
   if (g_refusePct && (int)(rnd() % 100) < g_refusePct) { g_refused++; return 0; }
   if (h > g_maxHold) { g_maxHold = h; }
   g_hash = (g_hash * 16777619u) ^ l; g_hash = (g_hash * 16777619u) ^ r; g_hash = (g_hash * 16777619u) ^ h; g_entries_total++;
   if (nE < MAXE) { E[nE].l = l; E[nE].r = r; E[nE].hold = h; nE++; }
   return 1;
}
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int phaseOf(uint8_t bits, int rev) { for (int c = 0; c < 4; c++) if (Motion_StepToCoilBits(c, rev) == bits) return c; return -1; }
typedef struct { int steps; double rms, max, minIv, meanIv; int reversals; } M;
static M metric(int motor, int lo, int hi)
{
   int rev = motor ? STEPPER_MOTOR_R_REVERSED : STEPPER_MOTOR_L_REVERSED;
   double *t = malloc(sizeof(double) * (size_t)(nE + 1)); int ns = 0; double now = 0; int prevPhase = -1, prevDir = 0; M m; memset(&m, 0, sizeof m);
   for (int i = 0; i < nE; i++)
   {
      int ph = phaseOf(motor ? E[i].r : E[i].l, rev);
      if (prevPhase >= 0 && ph != prevPhase)
      {
         int d = (((ph - prevPhase) & 3) == 1) ? 1 : -1; t[ns++] = now; if (prevDir && d != prevDir) m.reversals++; prevDir = d;
      }
      prevPhase = ph; now += E[i].hold;
   }
   m.steps = ns; int a = ns * lo / 100, b = ns * hi / 100; double sq = 0, mx = 0, mn = 1e18, mean = 0; int cnt = 0, nm = 0;
   for (int i = a + 1; i < b; i++) { mean += t[i] - t[i - 1]; nm++; }
   mean = nm ? mean / nm : 1; m.meanIv = mean;
   for (int i = a + 2; i < b; i++) { double i1 = t[i - 1] - t[i - 2], i2 = t[i] - t[i - 1], d = (i2 - i1) / mean; sq += d * d; if (fabs(d) > mx) mx = fabs(d); cnt++; if (i2 < mn) mn = i2; }
   m.rms = cnt ? sqrt(sq / cnt) : 0; m.max = mx; m.minIv = mn; free(t); return m;
}
static double totalMs(void) { double t = 0; for (int i = 0; i < nE; i++) t += E[i].hold; return t / 2000.0; }

/* run Motion until it reports idle; returns the number of Motion_Task calls */
static int drive(int cap) { int n = 0; while (!Motion_Task() && n < cap) n++; return n; }

static void seed_at(Motion_Position_t p) { Motion_Init(); Motion_SetCurrentPosition(p); nE = 0; }

/* the ideal count of each motor at a position, to compare against what Motion commanded */
static double idealL(Motion_Position_t p) { return (double)Motion_XYToLR(p).L * 2037.886 / 109955.0; }
static double idealR(Motion_Position_t p) { return (double)Motion_XYToLR(p).R * 2037.886 / 109955.0; }
static double endErr(void)
{
   int32_t l, r, x, y; Motion_GetPersistedState(&l, &r, &x, &y);
   Motion_Position_t p = Motion_GetComputedPosition();
   double a = fabs(l - idealL(p)), b = fabs(r - idealR(p)); return a > b ? a : b;
}
static int doubleJumps(void)
{
   int n = 0; for (int i = 1; i < nE; i++)
   {
      int dl = (phaseOf(E[i].l, STEPPER_MOTOR_L_REVERSED) - phaseOf(E[i - 1].l, STEPPER_MOTOR_L_REVERSED)) & 3;
      int dr = (phaseOf(E[i].r, STEPPER_MOTOR_R_REVERSED) - phaseOf(E[i - 1].r, STEPPER_MOTOR_R_REVERSED)) & 3;
      if (dl == 2 || dr == 2) n++;
   } return n;
}
/* An entry below the minimum is legitimate only as the LAST of a move: the flush, whose hold runs from the last
 * event to the end of the timeline and can be short by chance; Stepper clamps it up. Anywhere else it is a defect. */
static int shortHolds(void) { int n = 0; for (int i = 1; i < nE - 1; i++) if (E[i].hold < STEPPER_DELAY_MIN_COUNTS) n++; return n; }
static int shortFlushes(void) { return (nE > 1 && E[nE - 1].hold < STEPPER_DELAY_MIN_COUNTS) ? 1 : 0; }

int main(void)
{
   E = malloc(sizeof(Ent) * MAXE);
   Motion_Position_t o = { 0, 0 };

   printf("1. Cruise jitter at VMAX (one 30 mm block through the planner; middle 60%% of each motor's steps)\n");
   {
      const struct { const char *n; long dx, dy; } d[] = { { "X pure", 30000, 0 }, { "Y pure", 0, 30000 }, { "20 deg", 28190, 10260 }, { "70 deg", 10260, 28190 }, { "135 deg", -21213, 21213 } };
      for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++)
      {
         Motion_Position_t t = { d[i].dx, d[i].dy };
         seed_at(o); Motion_QueueMove(t); drive(100000);
         M l = metric(0, 20, 80), r = metric(1, 20, 80);
         printf("  %-8s L: %4d steps rms %5.2f%% max %5.1f%%   R: %4d steps rms %5.2f%% max %5.1f%%   fastest step %.2f ms\n", d[i].n, l.steps, 100 * l.rms, 100 * l.max, r.steps, 100 * r.rms, 100 * r.max, fmin(l.minIv, r.minIv) / 2000.0);
         if (l.steps > 50) CHECK(l.rms < 0.03, "%s: motor L jitter %.2f %% over the 3 %% target", d[i].n, 100 * l.rms);
         if (r.steps > 50) CHECK(r.rms < 0.03, "%s: motor R jitter %.2f %% over the 3 %% target", d[i].n, 100 * r.rms);
         CHECK(doubleJumps() == 0 && shortHolds() == 0, "%s: double jumps %d, short holds %d", d[i].n, doubleJumps(), shortHolds());
         CHECK(endErr() < 0.6, "%s: end deviation %.3f step", d[i].n, endErr());
      }
   }

   printf("\n2. Tangential travel (10 mm, L constant): reversals\n");
   { Motion_Position_t t = { -6990, 7140 }; seed_at(o); Motion_QueueMove(t); drive(100000); printf("  L %d steps, %d reversals; R %d steps, %d reversals\n", metric(0,0,100).steps, metric(0,0,100).reversals, metric(1,0,100).steps, metric(1,0,100).reversals);
     CHECK(metric(0,0,100).reversals == 0 && metric(1,0,100).reversals == 0, "tangential travel chattered"); }

   printf("\n3. Rest to rest, 20 mm along X: starts AT REST (not at the planner's entry speed)\n");
   { Motion_Position_t t = { 20000, 0 }; seed_at(o); Motion_QueueMove(t); drive(100000);
     printf("  %d entries, %.1f ms (v1: 1021 ms for the same move), fastest step %.2f ms\n", nE, totalMs(), metric(0,0,100).minIv / 2000.0);
     CHECK(totalMs() > 1000 && totalMs() < 1060, "rest-to-rest took %.1f ms", totalMs());
     /* the first step must not come sooner than a ramp from rest allows: at VMAX the first interval would be ~2.5 ms */
     double first = 0; int k = 0; { M m = metric(0,0,100); (void)m; }
     for (int i = 0; i < nE && k < 2; i++) { first += E[i].hold; k++; }
     CHECK(first > 6000.0, "the first two entries add up to %.0f counts: it started at speed", first); }

   printf("\n4. A chain of 50 jog-sized 600 um blocks through the planner (fed as there is room)\n");
   {
      seed_at(o); int fed = 0; long guard = 0;
      while ((fed < 50 || !Motion_IsIdle()) && guard++ < 2000000)
      {
         if (fed < 50 && Motion_PlannerHasRoom()) { Motion_Position_t t = Motion_GetTargetPosition(); t.X += 600; Motion_QueueMove(t); fed++; }
         Motion_Task();
      }
      double secs = totalMs() / 1000.0; Motion_Position_t p = Motion_GetComputedPosition();
      printf("  %d entries, 30 mm in %.2f s = %.1f mm/s on average (VMAX 21.5), jitter L %.2f %% R %.2f %%\n", nE, secs, p.X / 1000.0 / secs, 100 * metric(0,10,90).rms, 100 * metric(1,10,90).rms);
      CHECK(p.X == 30000, "the chain ended at %ld um", (long)p.X);
      CHECK(p.X / 1000.0 / secs > 15.0, "the chain did not carry speed across its junctions: %.1f mm/s", p.X / 1000.0 / secs);
      CHECK(doubleJumps() == 0 && shortHolds() == 0, "chain: double jumps %d, short holds %d", doubleJumps(), shortHolds());
   }

   printf("\n5. A refusal never changes the stream: the same moves with 0 %% and 40 %% of requests refused\n");
   {
      static Ent ref[MAXE]; int nref;
      Motion_Position_t tg[] = { { 12000, 3000 }, { 12500, 9000 }, { -4000, 9500 }, { -4000, -3000 }, { 0, 0 } };
      for (int pass = 0; pass < 2; pass++)
      {
         g_refusePct = pass ? 40 : 0; g_refused = 0; g_rng = 777; seed_at(o);
         int next = 0; long guard = 0;
         while ((next < 5 || !Motion_IsIdle()) && guard++ < 2000000)
         { if (next < 5 && Motion_PlannerHasRoom()) { Motion_QueueMove(tg[next]); next++; } Motion_Task(); }
         if (pass == 0) { memcpy(ref, E, sizeof(Ent) * (size_t)nE); nref = nE; }
         else
         {
            int same = (nE == nref) && memcmp(ref, E, sizeof(Ent) * (size_t)nE) == 0;
            printf("  %d entries; with 40 %% refused (%d refusals) the stream is %s\n", nref, g_refused, same ? "IDENTICAL" : "DIFFERENT");
            CHECK(same, "a refused request changed the entries Motion produced");
         }
      }
      g_refusePct = 0;
   }

   printf("\n6. Halt and resume in the middle of a long move\n");
   {
      Motion_Position_t t = { 40000, 0 }; seed_at(o); Motion_QueueMove(t);
      while (nE < 150) Motion_Task();
      int atHalt = nE; Motion_Halt(); int calls = 0; while (!Motion_IsHaltRampDone() && calls++ < 100000) Motion_Task();
      int ramp = nE - atHalt; double msRamp = 0; for (int i = atHalt; i < nE; i++) msRamp += E[i].hold / 2000.0;
      Motion_Position_t at = Motion_GetComputedPosition(); int32_t l0, r0, x0, y0; Motion_GetPersistedState(&l0, &r0, &x0, &y0);
      printf("  halted after %d entries; ramp %d entries, %.0f ms, stopped at X = %ld um; motors at %ld,%ld counts\n", atHalt, ramp, msRamp, (long)at.X, (long)l0, (long)r0);
      CHECK(Motion_IsHaltRampDone() && !Motion_IsIdle(), "a paused move must be done-ramping and not idle");
      CHECK(at.X < 40000, "it ran to the end instead of stopping");
      CHECK(endErr() < 0.6, "at the halt the motors are %.3f step from where Motion thinks they are", endErr());
      int n0 = nE; for (int i = 0; i < 2000; i++) Motion_Task(); CHECK(nE == n0, "a paused machine kept producing entries");
      Motion_Resume(); { int nr = nE; drive(200000); uint32_t mx = 0;
         for (int i = nr; i < nE; i++) { if (E[i].hold > mx) { mx = E[i].hold; } }
         /* a hold is at most one segment's time: 50 ms is generous. The bench found 4 294 118 081 counts (36 minutes) as the first one after a resume. */
         CHECK(mx < 100000u, "the largest hold after the resume is %lu counts (%.1f ms)", (unsigned long)mx, mx / 2000.0);
         CHECK(nE > nr, "nothing was produced after the resume"); }
      at = Motion_GetComputedPosition(); printf("  resumed: ended at X = %ld um, %d entries, end deviation %.3f step\n", (long)at.X, nE, endErr());
      CHECK(at.X == 40000 && Motion_IsIdle(), "the resumed move did not finish");
      CHECK(endErr() < 0.6 && doubleJumps() == 0 && shortHolds() == 0, "resumed move: dev %.3f, double jumps %d, short holds %d", endErr(), doubleJumps(), shortHolds());
   }

   printf("\n6b. The halt ramp is made of entries: it completes only while Stepper keeps accepting them (bench, 2026-10-10: a pause that never finished)\n");
   {
      Motion_Position_t t = { 40000, 0 }; int calls;
      /* what grbl_corelr.ino used to do: close the stepper's gate together with Motion_Halt() */
      seed_at(o); Motion_QueueMove(t); while (nE < 150) Motion_Task();
      Motion_Halt(); g_gate = 1; calls = 0; while (!Motion_IsHaltRampDone() && calls++ < 20000) Motion_Task();
      CHECK(!Motion_IsHaltRampDone(), "the ramp reports done although Stepper refused every entry of it");
      printf("  gate closed with the halt: after %d calls the ramp is %s\n", calls, Motion_IsHaltRampDone() ? "done" : "NOT done, and never will be");
      g_gate = 0; Motion_Abort(); drive(1000);
      /* what it must do: leave the gate open until the ramp is done, close it then */
      seed_at(o); Motion_QueueMove(t); while (nE < 150) Motion_Task();
      Motion_Halt(); calls = 0; while (!Motion_IsHaltRampDone() && calls++ < 100000) Motion_Task();
      CHECK(Motion_IsHaltRampDone(), "the ramp did not complete with the gate open");
      g_gate = 1; { int n0 = nE; for (int i = 0; i < 2000; i++) Motion_Task(); CHECK(nE == n0, "entries were produced after the ramp, with the gate closed"); }
      CHECK(Motion_IsHaltRampDone(), "the gate closing afterwards un-did the ramp");
      g_gate = 0; Motion_Resume(); drive(200000);
      CHECK(Motion_GetComputedPosition().X == 40000 && Motion_IsIdle(), "the move did not finish after the gate was reopened and Motion_Resume()");
      printf("  gate closed after the ramp: done, stays done, resumes to X = %ld um\n", (long)Motion_GetComputedPosition().X);
   }

   printf("\n6c. The bench scenario: six chained moves through the planner, pause with a bounded queue, resume (bench 2026-10-10: Run, but frozen)\n");
   {
      /* +40,-40,+40,-40,+40,-40 mm along X, fed as the serial link feeds them (one more whenever the planner has room), the stepper queue 31 slots
       * deep and drained one entry per 40 calls. Pause after some travel, gate closed at the end of the ramp as the sketch does, resume, finish. */
      const int32_t dx[6] = { 40000, -40000, 40000, -40000, 40000, -40000 };
      int fed = 0, iter = 0, halted = 0, resumed = 0, n_at_resume = 0; long frozen = 0;
      Motion_Position_t at, pos0; int32_t x_at_pause = 0, x_after = 0;
      seed_at(o); g_qcap = 31; g_qcount = 0; g_gate = 0; nE = 0; pos0 = Motion_GetComputedPosition();
      for (iter = 0; iter < 3000000; iter++)
      {
         if (fed < 6 && Motion_PlannerHasRoom()) { Motion_Position_t t = pos0; t.X += (fed % 2 == 0) ? 40000 : 0; if (Motion_QueueMove(t)) { fed++; } }
         (void)Motion_Task();
         if (iter % 40 == 0 && g_qcount > 0) { g_qcount--; }
         if (!halted && nE > 2500) { Motion_Halt(); halted = 1; }
         if (halted && !resumed && Motion_IsHaltRampDone())
         {
            if (!g_gate) { g_gate = 1; x_at_pause = Motion_GetComputedPosition().X; }
            else if (iter > 0 && (frozen++ > 5000)) { g_gate = 0; Motion_Resume(); resumed = 1; n_at_resume = nE; }
         }
         if (resumed && fed == 6 && Motion_IsIdle() && Motion_PlannerIsEmpty()) { break; }
      }
      at = Motion_GetComputedPosition(); x_after = at.X;
      printf("  paused at X = %ld um; resumed after %d entries; ended at X = %ld um after %d calls, %d entries more\n", (long)x_at_pause, n_at_resume, (long)x_after, iter, nE - n_at_resume);
      CHECK(halted && resumed, "the scenario did not reach the pause and the resume (halted %d, resumed %d)", halted, resumed);
      CHECK(nE > n_at_resume, "nothing was produced after the resume: Motion is frozen");
      CHECK(Motion_IsIdle() && Motion_PlannerIsEmpty() && x_after == pos0.X, "the job did not finish at its start: X = %ld um, idle %d, planner empty %d", (long)x_after, Motion_IsIdle(), Motion_PlannerIsEmpty());
      g_qcap = 0; g_qcount = 0; g_gate = 0;
   }

   printf("\n7. Abort after a halt, then a fresh move\n");
   {
      Motion_Position_t t = { 25000, 5000 }; seed_at(o); Motion_QueueMove(t); while (nE < 100) Motion_Task();
      Motion_Halt(); while (!Motion_IsHaltRampDone()) Motion_Task();
      Motion_Abort(); drive(1000);
      CHECK(Motion_IsIdle(), "not idle after abort"); CHECK(endErr() < 0.6, "after abort: motors %.3f step off", endErr());
      Motion_Position_t t2 = { 0, 0 }; Motion_QueueMove(t2); drive(200000);
      CHECK(Motion_GetComputedPosition().X == 0 && endErr() < 0.6 && doubleJumps() == 0, "move after abort ended at X=%ld, dev %.3f", (long)Motion_GetComputedPosition().X, endErr());
      printf("  ok: back at the origin, end deviation %.3f step\n", endErr());
   }

   printf("\n8. Fuzz: 600 random moves, queued through the planner, random halt/resume, 25 %% of requests refused\n");
   {
      seed_at(o); g_refusePct = 25; g_rng = 4242; srand(5);
      long moves = 0, entries = 0, bad = 0; double worst = 0; int dj = 0, sh = 0, sf = 0;
      Motion_Position_t p = o;
      for (int i = 0; i < 600; i++)
      {
         long reach = (i % 3 == 0) ? 600 : (i % 3 == 1) ? 30000 : 200000;
         Motion_Position_t t = { p.X + (rand() % (2 * reach + 1)) - reach, p.Y + (rand() % (2 * reach + 1)) - reach };
         if (t.Y < -200000) t.Y = -200000;
         long guard = 0; int queued = 0, halted = 0;
         while ((!queued || !Motion_IsIdle()) && guard++ < 3000000)
         {
            if (!queued && Motion_QueueMove(t)) queued = 1;
            if ((rand() % 4000) == 0 && !halted && queued) { Motion_Halt(); halted = 1; }
            Motion_Task();
            if (halted && Motion_IsHaltRampDone() && (rand() % 50) == 0) { Motion_Resume(); halted = 0; }
         }
         if (guard >= 3000000) { bad++; break; }
         double e = endErr(); if (e > worst) worst = e; if (e > 0.62) bad++;
         p = t; moves++; entries += nE; dj += doubleJumps(); sh += shortHolds(); sf += shortFlushes(); nE = 0;
         if (Motion_GetComputedPosition().X != t.X || Motion_GetComputedPosition().Y != t.Y) bad++;
      }
      g_refusePct = 0;
      printf("  %ld moves, %ld entries: double jumps %d, holds below MIN %d (+ %d short final flushes, clamped by Stepper), bad ends %ld, worst end deviation %.3f step\n", moves, entries, dj, sh, sf, bad, worst);
      CHECK(dj == 0 && sh == 0 && bad == 0, "fuzz found %d double jumps, %d short holds, %ld bad ends", dj, sh, bad);
   }

   CHECK(g_maxHold < 400000u, "the largest hold of any entry in the whole test is %lu counts (%.1f ms)", (unsigned long)g_maxHold, g_maxHold / 2000.0);
   printf("  largest hold of any entry, all sections: %lu counts (%.2f ms)\n", (unsigned long)g_maxHold, g_maxHold / 2000.0);
   printf("\nstream hash over %ld entries: %08x\n", g_entries_total, (unsigned)g_hash);
   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
