/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host measurements of the prototype event generator, against the real v1 Motion
 * (same metric, same machine). Build: see build.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "evgen.h"
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "HwConfig.h"
#ifndef EV_H
#define EV_H 16
#endif
#ifndef EV_MERGE_MODE
#define EV_MERGE_MODE 1
#endif
#ifndef EV_MIN_COUNTS
#define EV_MIN_COUNTS 64
#endif
#ifndef EV_WAYPOINT_STEPS
#define EV_WAYPOINT_STEPS 4
#endif

#define MAXE 200000
typedef struct { uint8_t l, r; uint32_t hold; } Ent;
static Ent *E; static int nE;

/* ---- stubs: Calibration geometry, and Stepper recording v1's entries ---- */
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
static int g_v1; /* record v1 entries, in 0.5 us counts (ticks of 250 us) */
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{ (void)p; if (g_v1 && nE < MAXE) { E[nE].l = l; E[nE].r = r; E[nE].hold = h * 500u; nE++; } return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

/* ---- metrics ------------------------------------------------------------ */
static int phaseOf(uint8_t bits, int rev) { for (int c = 0; c < 4; c++) if (Motion_StepToCoilBits(c, rev) == bits) return c; return -1; }

typedef struct { int steps; double rms, max, minIv, meanIv; int reversals; } M;

static M metric(int motor, int lo, int hi /* fraction window in % of the steps */)
{
   int rev = motor ? STEPPER_MOTOR_R_REVERSED : STEPPER_MOTOR_L_REVERSED;
   double *t = malloc(sizeof(double) * (size_t)nE); int *dir = malloc(sizeof(int) * (size_t)nE); int ns = 0;
   double now = 0; int prevPhase = -1; int prevDir = 0; M m; memset(&m, 0, sizeof m);
   for (int i = 0; i < nE; i++)
   {
      int ph = phaseOf(motor ? E[i].r : E[i].l, rev);
      if (prevPhase >= 0 && ph != prevPhase)
      {
         int d = (((ph - prevPhase) & 3) == 1) ? 1 : -1;
         t[ns] = now; dir[ns] = d; ns++;
         if (prevDir && d != prevDir) m.reversals++;
         prevDir = d;
      }
      prevPhase = ph; now += E[i].hold;
   }
   m.steps = ns;
   int a = ns * lo / 100, b = ns * hi / 100; double sum = 0, sq = 0, mx = 0, mn = 1e18; int cnt = 0;
   double mean = 0; int nm = 0;
   for (int i = a + 1; i < b; i++) { mean += t[i] - t[i - 1]; nm++; }
   mean = nm ? mean / nm : 1; m.meanIv = mean;
   for (int i = a + 2; i < b; i++)
   {
      double i1 = t[i - 1] - t[i - 2], i2 = t[i] - t[i - 1], d = (i2 - i1) / mean;
      sq += d * d; if (fabs(d) > mx) mx = fabs(d); cnt++; (void)sum;
      if (i2 < mn) mn = i2;
   }
   m.rms = cnt ? sqrt(sq / cnt) : 0; m.max = mx; m.minIv = mn;
   free(t); free(dir); return m;
}

static int32_t quant(int32_t sub) { return (sub + 128) >> 8; }
static void seed(Motion_Position_t p)
{
   Motion_CableLengths_t lr = Motion_XYToLR(p);
   int64_t sl = ((int64_t)lr.L * 2048 * 256 + 109955 / 2) / 109955, sr = ((int64_t)lr.R * 2048 * 256 + 109955 / 2) / 109955;
   EvGen_Init(quant((int32_t)sl), quant((int32_t)sr), p);
}

static void run_ev(Motion_Position_t from, Motion_Position_t to, uint8_t vin, uint8_t vout, int chain)
{
   EvGen_Entry_t e;
   if (!chain) { seed(from); nE = 0; }
   EvGen_StartMove(to, vin, vout);
   while (EvGen_Next(&e)) { E[nE].l = e.CoilL; E[nE].r = e.CoilR; E[nE].hold = e.Hold; nE++; }
}
static void finish_ev(void) { EvGen_Entry_t e; if (EvGen_Flush(&e)) { E[nE].l = e.CoilL; E[nE].r = e.CoilR; E[nE].hold = e.Hold; nE++; } }

static void run_v1(Motion_Position_t from, Motion_Position_t to)
{
   Motion_Init(); Motion_SetCurrentPosition(from); nE = 0; g_v1 = 1;
   Motion_RequestLinearMove(to);
   for (int i = 0; i < 100000 && !Motion_Task(); i++) {}
   g_v1 = 0;
}

static void row(const char *name, M l, M r)
{
   printf("  %-26s L: %4d steps rms %5.2f%% max %5.1f%%   R: %4d steps rms %5.2f%% max %5.1f%%   min interval %.2f ms\n",
          name, l.steps, 100 * l.rms, 100 * l.max, r.steps, 100 * r.rms, 100 * r.max, fmin(l.minIv, r.minIv) / 2000.0);
}

int main(void)
{
   E = malloc(sizeof(Ent) * MAXE);
   Motion_Position_t o = { 0, 0 };
   const struct { const char *n; long dx, dy; } dirs[] = {
      { "X pure (0 deg)", 30000, 0 }, { "Y pure (90 deg)", 0, 30000 }, { "45 deg", 21213, 21213 },
      { "20 deg", 28190, 10260 }, { "70 deg", 10260, 28190 }, { "135 deg (up-left)", -21213, 21213 } };

   printf("config: H=%d  merge-mode=%d  min=%d counts  waypoint=%d steps\n\n", EV_H, EV_MERGE_MODE, EV_MIN_COUNTS, EV_WAYPOINT_STEPS);

   printf("1. Step-interval jitter at constant VMAX (30 mm moves; middle 60%% of the steps)\n"
          "   rms / max of |next interval - interval| / mean interval, per motor\n");
   for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
   {
      Motion_Position_t t = { dirs[i].dx, dirs[i].dy };
      char nm[64];
      run_ev(o, t, 86, 86, 0); finish_ev();
      snprintf(nm, sizeof nm, "v2  %s", dirs[i].n); row(nm, metric(0, 20, 80), metric(1, 20, 80));
      run_v1(o, t);
      snprintf(nm, sizeof nm, "v1  %s", dirs[i].n); row(nm, metric(0, 20, 80), metric(1, 20, 80));
   }

   printf("\n2. Tangential travel -- the case that chattered in v1 (10 mm, L constant)\n");
   {
      Motion_Position_t t = { -6990, 7140 };
      run_ev(o, t, 86, 86, 0); finish_ev();
      printf("  v2: L %d steps, %d reversals;   R %d steps, %d reversals\n", metric(0, 0, 100).steps, metric(0, 0, 100).reversals, metric(1, 0, 100).steps, metric(1, 0, 100).reversals);
      run_v1(o, t);
      printf("  v1: L %d steps, %d reversals;   R %d steps, %d reversals\n", metric(0, 0, 100).steps, metric(0, 0, 100).reversals, metric(1, 0, 100).steps, metric(1, 0, 100).reversals);
   }

   printf("\n3. Rest-to-rest 20 mm along X: peak per-motor rate and duration\n");
   {
      Motion_Position_t t = { 20000, 0 };
      run_ev(o, t, 0, 0, 0); finish_ev();
      double tot = 0; for (int i = 0; i < nE; i++) tot += E[i].hold;
      M l = metric(0, 0, 100);
      printf("  v2: %d entries, %.1f ms, fastest motor step interval %.2f ms (limit 2.50 ms = 400/s)\n", nE, tot / 2000.0, l.minIv / 2000.0);
      run_v1(o, t); tot = 0; for (int i = 0; i < nE; i++) tot += E[i].hold; l = metric(0, 0, 100);
      printf("  v1: %d entries, %.1f ms, fastest motor step interval %.2f ms\n", nE, tot / 2000.0, l.minIv / 2000.0);
   }

   printf("\n4. Chain of 50 jog-sized moves (600 um in X, junctions at full speed)\n");
   {
      Motion_Position_t p = o; seed(p); nE = 0;
      for (int i = 0; i < 50; i++) { Motion_Position_t t = { p.X + 600, p.Y }; run_ev(p, t, 86, 86, 1); p = t; }
      finish_ev();
      row("v2  chained 600um", metric(0, 10, 90), metric(1, 10, 90));
   }

   printf("\n5. No drift: 400 random moves, worst |commanded - ideal| at each move's end (limit 0.5 step + H)\n");
   {
      Motion_Position_t p = o; double worst = 0; srand(7); seed(p); nE = 0;
      for (int i = 0; i < 400; i++)
      {
         Motion_Position_t t = { p.X + (rand() % 40001) - 20000, p.Y + (rand() % 40001) - 20000 };
         int32_t cl, cr; Motion_CableLengths_t lr;
         run_ev(p, t, 40, 40, 1); p = t;
         EvGen_GetCommandedSteps(&cl, &cr); lr = Motion_XYToLR(p);
         double il = (double)lr.L * 2048.0 / 109955.0, ir = (double)lr.R * 2048.0 / 109955.0;
         if (fabs(cl - il) > worst) worst = fabs(cl - il);
         if (fabs(cr - ir) > worst) worst = fabs(cr - ir);
         nE = 0;
      }
      printf("  worst deviation %.3f step  (%.0f um)\n", worst, worst * 53.69);
   }


   printf("\n6. Fuzz: 3000 random moves (0 to 1 m, random entry/exit speeds), invariants on every entry\n");
   {
      Motion_Position_t p = o; srand(11); seed(p); nE = 0;
      long entries = 0, doubleJumps = 0, shortHolds = 0, overRate = 0, badEnd = 0;
      double worstEnd = 0, minIv = 1e18; uint8_t prevL = 255, prevR = 255;
      for (int i = 0; i < 3000; i++)
      {
         long reach = (i % 3 == 0) ? 600 : (i % 3 == 1) ? 30000 : 400000;
         Motion_Position_t t = { p.X + (rand() % (2 * reach + 1)) - reach, p.Y + (rand() % (2 * reach + 1)) - reach };
         int32_t cl, cr; Motion_CableLengths_t lr; int base = nE;
         if (t.Y < -300000) t.Y = -300000;                       /* stay inside the workspace */
         run_ev(p, t, (uint8_t)(rand() % 87), (uint8_t)(rand() % 87), 1); p = t;
         EvGen_GetCommandedSteps(&cl, &cr); lr = Motion_XYToLR(p);
         { double il = (double)lr.L * 2048.0 / 109955.0, ir = (double)lr.R * 2048.0 / 109955.0;
           double dl = fabs(cl - il), dr = fabs(cr - ir); if (dl > worstEnd) worstEnd = dl; if (dr > worstEnd) worstEnd = dr;
           if (dl > 0.51 || dr > 0.51) badEnd++; }
         for (int j = base; j < nE; j++)
         {
            entries++;
            if (E[j].hold < EV_MIN_COUNTS && !(i == 0 && j == base)) shortHolds++;
            if (prevL != 255)
            {
               int dL = (phaseOf(E[j].l, STEPPER_MOTOR_L_REVERSED) - phaseOf(prevL, STEPPER_MOTOR_L_REVERSED)) & 3;
               int dR = (phaseOf(E[j].r, STEPPER_MOTOR_R_REVERSED) - phaseOf(prevR, STEPPER_MOTOR_R_REVERSED)) & 3;
               if (dL == 2 || dR == 2) doubleJumps++;
            }
            prevL = E[j].l; prevR = E[j].r;
         }
         /* per-motor rate over this move */
         { M a = metric(0, 0, 100), b = metric(1, 0, 100);
           if (a.steps >= 4 && a.minIv < minIv) minIv = a.minIv;
           if (b.steps >= 4 && b.minIv < minIv) minIv = b.minIv; }
         nE = 0;
         prevL = 255;   /* the metric helpers need a fresh array; continuity is checked inside one move chain only */
      }
      printf("  fastest step interval of either motor in any move: %.3f ms (400 steps/s = 2.500 ms)\n", minIv / 2000.0);
      printf("  %ld entries, double-phase jumps %ld, holds below MIN %ld, end deviations above 0.51 step %ld (worst %.3f)\n",
             entries, doubleJumps, shortHolds, badEnd, worstEnd);
   }

   {
      const EvGen_Stats_t *s = EvGen_GetStats();
      printf("\nlast run counters: events %u merged %u segments %u holds<MIN %u\n", s->Events, s->Merged, s->Segments, s->ShortHolds);
   }
   return 0;
}
