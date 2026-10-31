/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of arcs (G2/G3) in the real Motion.c and Trig.c (REQ-CoreLR-D-Motion-0021..0023).
 * Motion.c is #included so that its generator can be stepped one iteration at a time and every
 * waypoint, and the speed of every segment, observed. Stepper and Calibration are stubs; every entry
 * Motion sends is recorded as {coil bits, hold in Timer1 counts}.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function -Ishim -I../../firmware/src test_arc.c \
 *       ../../firmware/src/Trig.c -lm -o out/test_arc
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "Motion.c"
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
static unsigned rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{
   (void)p;
   if (g_refusePct && (int)(rnd() % 100) < g_refusePct) { g_refused++; return 0; }
   g_hash = (g_hash * 16777619u) ^ l; g_hash = (g_hash * 16777619u) ^ r; g_hash = (g_hash * 16777619u) ^ h; g_entries_total++;
   if (nE < MAXE) { E[nE].l = l; E[nE].r = r; E[nE].hold = h; nE++; }
   return 1;
}
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 30) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int phaseOf(uint8_t bits, int rev) { for (int c = 0; c < 4; c++) if (Motion_StepToCoilBits(c, rev) == bits) return c; return -1; }
static int doubleJumps(void)
{
   int n = 0; for (int i = 1; i < nE; i++)
   {
      int dl = (phaseOf(E[i].l, STEPPER_MOTOR_L_REVERSED) - phaseOf(E[i - 1].l, STEPPER_MOTOR_L_REVERSED)) & 3;
      int dr = (phaseOf(E[i].r, STEPPER_MOTOR_R_REVERSED) - phaseOf(E[i - 1].r, STEPPER_MOTOR_R_REVERSED)) & 3;
      if (dl == 2 || dr == 2) n++;
   } return n;
}
static int shortHolds(void) { int n = 0; for (int i = 1; i < nE - 1; i++) if (E[i].hold < STEPPER_DELAY_MIN_COUNTS) n++; return n; }
static double idealL(Motion_Position_t p) { return (double)Motion_XYToLR(p).L * 2037.886 / 109955.0; }
static double idealR(Motion_Position_t p) { return (double)Motion_XYToLR(p).R * 2037.886 / 109955.0; }
static double endErr(void)
{
   int32_t l, r, x, y; Motion_GetPersistedState(&l, &r, &x, &y);
   Motion_Position_t p = Motion_GetComputedPosition();
   double a = fabs(l - idealL(p)), b = fabs(r - idealR(p)); return a > b ? a : b;
}
static void seed_at(Motion_Position_t p) { Motion_Init(); Motion_SetCurrentPosition(p); nE = 0; }

/* ---- the waypoints of a move, as they come out of the generator ---- */
#define MAXW 70000
static Motion_Position_t W[MAXW]; static int nW; static int maxCurV, g_n;

/* run until idle, recording every change of the last-completed waypoint and the highest segment speed */
static void runCollect(void)
{
   Motion_Position_t last = s_currentPosition; long guard = 0;
   nW = 0; maxCurV = 0;
   while (!MotionStepOnce() && guard++ < 20000000L)
   {
      if (s_move.Moving && s_move.CurV > maxCurV) maxCurV = s_move.CurV;
      if (s_move.Moving && s_move.Arc) g_n = s_move.N;
      if (s_currentPosition.X != last.X || s_currentPosition.Y != last.Y) { if (nW < MAXW) W[nW++] = s_currentPosition; last = s_currentPosition; }
   }
   CHECK(guard < 20000000L, "the generator did not finish");
}

/* ---- ideal geometry, in long double ---- */
typedef struct { long double cx, cy, rs, re, ths, sweep; } Ideal;
static long double twopi(void) { return 2 * M_PIl; }
static long double wrap(long double a) { while (a < 0) a += twopi(); while (a >= twopi()) a -= twopi(); return a; }
static Ideal ideal(Motion_Position_t S, long I, long J, Motion_Position_t Ed, int cw)
{
   Ideal d; d.cx = S.X + I; d.cy = S.Y + J;
   d.rs = hypotl(S.X - d.cx, S.Y - d.cy); d.re = hypotl(Ed.X - d.cx, Ed.Y - d.cy);
   d.ths = atan2l(S.Y - d.cy, S.X - d.cx); long double the = atan2l(Ed.Y - d.cy, Ed.X - d.cx);
   if (S.X == Ed.X && S.Y == Ed.Y) d.sweep = twopi(); else d.sweep = cw ? wrap(d.ths - the) : wrap(the - d.ths);
   return d;
}

static Motion_Position_t pointOnCircle(long cx, long cy, double r, double ang)
{ Motion_Position_t p; p.X = cx + lround((double)r * cos(ang)); p.Y = cy + lround((double)r * sin(ang)); return p; }

typedef struct { double worstDev, worstSag, worstChord; int n; } Result;

/* one arc, through Motion_QueueArc and the generator; waypoints checked against the circle */
static Result arc(Motion_Position_t S, long I, long J, Motion_Position_t Ed, int cw, const char *what)
{
   Result res; memset(&res, 0, sizeof res);
   Ideal d = ideal(S, I, J, Ed, cw);
   seed_at(S);
   CHECK(Motion_ArcCheck(&Ed, I, J, (uint8_t)cw), "%s: ArcCheck refused a valid arc", what);
   CHECK(Motion_QueueArc(&Ed, I, J, (uint8_t)cw), "%s: the planner refused the arc", what);
   runCollect();
   int N = nW; res.n = N;
   if (N < 1 || W[N - 1].X != Ed.X || W[N - 1].Y != Ed.Y) { CHECK(0, "%s: the last waypoint is (%ld,%ld), not the end point (%ld,%ld)", what, N ? (long)W[N - 1].X : 0, N ? (long)W[N - 1].Y : 0, (long)Ed.X, (long)Ed.Y); return res; }
   CHECK(Motion_GetComputedPosition().X == Ed.X && Motion_GetComputedPosition().Y == Ed.Y, "%s: computed position not exactly the end", what);
   for (int k = 1; k <= N; k++)
   {
      long double th = d.ths + (cw ? -1 : 1) * d.sweep * k / N, r = d.rs + (d.re - d.rs) * k / N;
      long double ix = d.cx + r * cosl(th), iy = d.cy + r * sinl(th);
      double dev = (double)hypotl(W[k - 1].X - ix, W[k - 1].Y - iy);
      if (k < N && dev > res.worstDev) res.worstDev = dev;
      /* The chord and its sagitta, on the IDEAL waypoints k-1 and k -- the design's own figures, free of the
       * rounding of the integer ones (<= 1.4 um on a chord, ~1.8 um on a point, which the deviation check bounds). */
      long double th0 = d.ths + (cw ? -1 : 1) * d.sweep * (k - 1) / N, r0 = d.rs + (d.re - d.rs) * (k - 1) / N;
      long double x0 = d.cx + r0 * cosl(th0), y0 = d.cy + r0 * sinl(th0);
      double chord = (double)hypotl(ix - x0, iy - y0);
      if (chord > res.worstChord) res.worstChord = chord;
      long double mx = (ix + x0) / 2 - d.cx, my = (iy + y0) / 2 - d.cy;
      long double rmid = d.rs + (d.re - d.rs) * (k - 0.5L) / N;
      double sag = (double)(rmid - hypotl(mx, my)); if (sag > res.worstSag) res.worstSag = sag;
   }
   (void)what;
   return res;
}

int main(void)
{
   E = malloc(sizeof(Ent) * MAXE);
   Motion_Position_t o = { 0, 0 };
   srand(11);

   printf("1. Waypoint accuracy against long-double geometry: radii 54 um..1.9 m, 11 start angles, 7 sweeps, both directions\n");
   {
      const double radii[] = { 54, 60, 100, 500, 1000, 2500, 10000, 50000, 200000, 1000000, 1900000 };
      const double startDeg[] = { 0, 10, 45, 80, 90, 135, 180, 225, 270, 315, 359 };
      const double sweepDeg[] = { 0.0006, 5, 90, 180, 270, 359.99, 360 };
      double worstDev = 0, worstSag = 0, worstChord = 0; long arcs = 0, segs = 0; int worstN = 0;
      char what[96];
      for (unsigned ri = 0; ri < sizeof radii / sizeof radii[0]; ri++)
         for (unsigned si = 0; si < sizeof startDeg / sizeof startDeg[0]; si++)
            for (unsigned wi = 0; wi < sizeof sweepDeg / sizeof sweepDeg[0]; wi++)
               for (int cw = 0; cw < 2; cw++)
               {
                  if (radii[ri] > 400000 && (wi == 4 || wi == 5 || wi == 6) && si % 3 != 0) continue;   /* the long ones: a third of the starts */
                  long cx = 37000 - (long)(si * 91), cy = -52000 + (long)(wi * 77);
                  double a0 = startDeg[si] * M_PI / 180, a1 = a0 + (cw ? -1 : 1) * sweepDeg[wi] * M_PI / 180;
                  Motion_Position_t S = pointOnCircle(cx, cy, radii[ri], a0);
                  Motion_Position_t Ed = (wi == 6) ? S : pointOnCircle(cx, cy, radii[ri], a1);
                  long I = cx - S.X, J = cy - S.Y;
                  if (S.X == Ed.X && S.Y == Ed.Y && wi != 6) continue;   /* the sweep rounded away: that would be a full circle */
                  snprintf(what, sizeof what, "r=%.0f start=%.0f sweep=%.4f %s", radii[ri], startDeg[si], sweepDeg[wi], cw ? "G2" : "G3");
                  if (hypot((double)(S.X - cx), (double)(S.Y - cy)) < 53.0)  { continue; }
                  Result r = arc(S, I, J, Ed, cw, what);
                  arcs++; segs += r.n; if (r.worstDev > worstDev) worstDev = r.worstDev; if (r.worstSag > worstSag) worstSag = r.worstSag;
                  if (r.worstChord > worstChord) worstChord = r.worstChord; if (r.n > worstN) worstN = r.n;
                  CHECK(r.worstDev <= 2.0, "%s: a waypoint is %.2f um from the circle", what, r.worstDev);
                  CHECK(r.worstChord <= (double)MOTION_WAYPOINT_UM + 0.01, "%s: a chord of %.3f um (waypoint spacing %ld)", what, r.worstChord, (long)MOTION_WAYPOINT_UM);
                  CHECK(r.worstSag <= (double)MOTION_ARC_TOLERANCE_UM, "%s: a chord falls %.3f um short of the circle (tolerance %lu um)", what, r.worstSag, (unsigned long)MOTION_ARC_TOLERANCE_UM);
               }
      printf("  %ld arcs, %ld segments (the longest arc %d): worst waypoint deviation %.2f um (limit 2), worst ideal chord %.1f um (limit %ld), worst sagitta %.3f um (limit %lu)\n",
             arcs, segs, worstN, worstDev, worstChord, (long)MOTION_WAYPOINT_UM, worstSag, (unsigned long)MOTION_ARC_TOLERANCE_UM);
   }

   printf("\n2. Direction, and the full circle: the end is the start, exactly, whichever way round and wherever it starts\n");
   {
      int bad = 0;
      for (int cw = 0; cw < 2; cw++)
         for (int deg = 0; deg < 360; deg += 15)
         {
            Motion_Position_t S = pointOnCircle(1000, 2000, 20000, deg * M_PI / 180);
            long I = 1000 - S.X, J = 2000 - S.Y;
            arc(S, I, J, S, cw, "full circle");
            double winding = 0; int wrongWay = 0;
            for (int k = 0; k < nW; k++)      /* k = 0 is the first segment, from the start point itself */
            {
               Motion_Position_t from = k ? W[k - 1] : S;
               double ax = (double)(from.X - 1000), ay = (double)(from.Y - 2000), bx = (double)(W[k].X - 1000), by = (double)(W[k].Y - 2000);
               double cross = ax * by - ay * bx; if ((cross > 0) == (cw != 0)) wrongWay++;
               winding += atan2(cross, ax * bx + ay * by);
            }
            if (wrongWay) bad++;
            CHECK(fabs(fabs(winding) - 2 * M_PI) < 0.01 || nW < 3, "full circle winds %.3f rad", winding);
            CHECK(W[nW - 1].X == S.X && W[nW - 1].Y == S.Y, "the circle did not close");
            CHECK(!wrongWay, "G%d went the wrong way round (%d segments)", cw ? 2 : 3, wrongWay);
            CHECK(endErr() < 0.6, "full circle: end deviation %.3f step", endErr());
         }
      printf("  48 circles (24 starts x 2 directions): %s\n", bad ? "SOME WENT THE WRONG WAY" : "all closed exactly, wound once the right way round");
   }

   printf("\n3. Radius consistency (REQ-CoreLR-F-GCodeInterpreter-0009) and the machine's reach\n");
   {
      const struct { long r; long dr; int ok; const char *why; } t[] = {
         { 10000, 0, 1, "perfect" }, { 10000, 5, 1, "5 um: always accepted" }, { 1000, 5, 1, "5 um at 1 mm (0.5 %): the floor" },
         { 1000, 6, 0, "6 um at 1 mm: over the floor and over 0.1 %" }, { 10000, 9, 1, "9 um at 10 mm: under 0.1 % (10 um)" },
         { 10000, 11, 0, "11 um at 10 mm: over 0.1 %" }, { 100000, 99, 1, "99 um at 100 mm" }, { 100000, 101, 0, "101 um at 100 mm" },
         { 1000000, 499, 1, "499 um at 1 m: under both" }, { 1000000, 501, 0, "501 um: over the 0.5 mm cap" },
         { 1900000, 600, 0, "600 um at 1.9 m: 0.03 %, but over the 0.5 mm cap" }, { 2200000, 0, 0, "radius 2.2 m: beyond the reach" },
         { 2096000, 0, 1, "radius 2.096 m: inside the reach" } };
      for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
      {
         /* start at the centre's left, end at its right: radii r and r + dr along X */
         Motion_Position_t S2 = { -t[i].r, 0 }, E2 = { t[i].r + t[i].dr, 0 };
         seed_at(S2);                      /* the start is where the queue ends */
         int got = Motion_ArcCheck(&E2, t[i].r, 0, 0);
         printf("  %-52s -> %s\n", t[i].why, got ? "accepted" : "refused");
         CHECK(got == t[i].ok, "%s: expected %s", t[i].why, t[i].ok ? "accepted" : "refused");
      }
      /* the blend: an accepted mismatch is drawn exactly through both ends, the radius moving linearly */
      Motion_Position_t S = { 10000, 0 }, Ed = { 0, 10009 };      /* centre (0,0): r_s = 10000, r_e = 10009 (0.09 %: accepted) */
      Result r = arc(S, -10000, 0, Ed, 0, "blend");
      printf("  blend 10000 -> 10009 um over a quarter turn: %d segments, worst deviation from the linear blend %.2f um, ends exact\n", r.n, r.worstDev);
      CHECK(r.worstDev <= 2.0, "blend deviation %.2f", r.worstDev);
   }

   printf("\n4. A refusal never changes the stream: lines and arcs, with 0 %% and 40 %% of requests refused\n");
   {
      static Ent ref[MAXE]; int nref = 0;
      for (int pass = 0; pass < 2; pass++)
      {
         g_refusePct = pass ? 40 : 0; g_refused = 0; g_rng = 777; seed_at(o);
         Motion_Position_t t1 = { 12000, 3000 }, t2 = { 12000, 23000 }, t3 = { 12000, 3000 }, t4 = { -3000, 3000 };
         int next = 0; long guard = 0;
         while ((next < 4 || !Motion_IsIdle()) && guard++ < 4000000)
         {
            if (Motion_PlannerHasRoom())
            {
               if (next == 0) { Motion_QueueMove(t1); next++; }
               else if (next == 1) { Motion_QueueArc(&t2, 0, 10000, 0); next++; }        /* G3 half circle, centre (12000,13000) */
               else if (next == 2) { Motion_QueueArc(&t3, 0, -10000, 1); next++; }       /* G2 back */
               else if (next == 3) { Motion_QueueMove(t4); next++; }
            }
            Motion_Task();
         }
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

   printf("\n5. Speed: the centripetal ceiling, and junctions between lines and arcs\n");
   {
      /* a full circle of 1 mm radius: v^2 <= a r, so 55 units (13.8 mm/s), never above */
      Motion_Position_t S = { 1000, 0 }; arc(S, -1000, 0, S, 0, "cap");
      /* the expected ceiling is the physics, not the firmware's own function: v = sqrt(a * r), in units of 0.25 mm/s */
      int expect = (int)(4.0 * sqrt((double)MOTION_ACCEL_MM_S2 * 1.0));
      printf("  r = 1 mm: the physical ceiling sqrt(a r) = %.1f mm/s = %d units; highest segment speed %d\n", expect / 4.0, expect, maxCurV);
      CHECK(maxCurV <= expect + 1 && maxCurV >= expect - 1, "1 mm circle ran at %d units, physics says %d", maxCurV, expect);
      S.X = 30000; arc(S, -30000, 0, S, 0, "free");
      printf("  r = 30 mm: highest segment speed %d units (VMAX %u)\n", maxCurV, (unsigned)MOTION_VMAX_V16);
      CHECK(maxCurV == (int)MOTION_VMAX_V16, "a large circle did not reach VMAX: %d", maxCurV);

      /* a line, then an arc leaving it tangentially: no corner, so the arc is entered at full speed */
      seed_at(o); { Motion_Position_t l = { 10000, 0 }, e = { 30000, 20000 }; Motion_QueueMove(l); Motion_QueueArc(&e, 0, 20000, 0); }
      printf("  line then tangent arc (r = 20 mm): the arc's planned entry speed %u units (VMAX %u)\n", (unsigned)s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV, (unsigned)MOTION_VMAX_V16);
      CHECK(s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV == MOTION_VMAX_V16, "a tangent junction was slowed");
      /* ... and one that is NOT tangent: the same arc entered from a line along Y */
      seed_at(o); { Motion_Position_t l = { 0, 10000 }, e = { 20000, 30000 }; Motion_QueueMove(l); Motion_QueueArc(&e, 0, 20000, 0); }
      printf("  line along +Y then an arc that leaves along +X (a right-angle corner): planned entry speed %u units\n", (unsigned)s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV);
      CHECK(s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV < MOTION_VMAX_V16, "a right-angle corner into an arc was taken at full speed");
      /* a tight arc (r = 1 mm) into a tangent line: the joint is the arc's exit, so its ceiling bounds the line's entry */
      seed_at(o); { Motion_Position_t e = { 1000, 1000 }, l = { 1000, 21000 }; Motion_QueueArc(&e, 0, 1000, 0); Motion_QueueMove(l); }
      {
         int expect = (int)(4.0 * sqrt((double)MOTION_ACCEL_MM_S2 * 1.0));
         unsigned v = s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV;
         printf("  1 mm arc into a tangent line: the line is entered at %u units (the arc's ceiling %d)\n", v, expect);
         CHECK((int)v <= expect + 1, "the line is entered at %u units, above the arc's centripetal ceiling %d", v, expect);
      }
      /* the arc in flight keeps its END tangent for the junction with what follows: a quarter arc, then a line that continues it */
      seed_at(o);
      {
         Motion_Position_t e = { 20000, 20000 }, l = { 20000, 60000 }; Motion_QueueArc(&e, 0, 20000, 0);
         for (int i = 0; i < 4 && !s_move.Moving; i++) MotionStepOnce();
         Motion_QueueMove(l);
         printf("  quarter arc (r = 20 mm) in flight, a tangent line queued behind it: the arc's exit speed %u units (VMAX %u)\n", (unsigned)s_move.ExitV, (unsigned)MOTION_VMAX_V16);
         CHECK(s_move.Moving && s_move.ExitV == MOTION_VMAX_V16, "the arc in flight leaves a tangent line at %u units, not VMAX", (unsigned)s_move.ExitV);
      }
      /* arc into arc, continuing the same circle direction: tangent at the join */
      seed_at(o); { Motion_Position_t m = { 20000, 0 }, e = { 40000, 0 }; Motion_QueueArc(&m, 10000, 0, 0); Motion_QueueArc(&e, 10000, 0, 1); }
      printf("  half circle then the opposite half (an S): planned entry of the second %u units\n", (unsigned)s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV);
      CHECK(s_plan[(s_planHead + 1) & (MOTION_PLANNER_DEPTH - 1)].EntryV == MOTION_VMAX_V16, "an S-bend join was slowed");
   }

   printf("\n6. Halt and resume in the middle of an arc\n");
   {
      Motion_Position_t S = { 20000, 0 }, Ed = { 0, 20000 };       /* centre (0,0)... three quarters round, clockwise */
      Ed.X = 0; Ed.Y = -20000;
      seed_at(S); Motion_QueueArc(&Ed, -20000, 0, 0);
      while (nE < 300) Motion_Task();
      int atHalt = nE; Motion_Halt(); int calls = 0; while (!Motion_IsHaltRampDone() && calls++ < 100000) Motion_Task();
      Motion_Position_t at = Motion_GetComputedPosition(); double rad = hypot((double)at.X, (double)at.Y);
      printf("  halted after %d entries, ramp %d entries; stopped at (%ld,%ld): %.2f um from the 20 mm circle\n", atHalt, nE - atHalt, (long)at.X, (long)at.Y, fabs(rad - 20000.0));
      CHECK(fabs(rad - 20000.0) <= 2.0, "stopped %.2f um off the circle", fabs(rad - 20000.0));
      CHECK(Motion_IsHaltRampDone() && !Motion_IsIdle(), "a paused arc must be done-ramping and not idle");
      CHECK(endErr() < 0.6, "at the halt the motors are %.3f step from where Motion thinks they are", endErr());
      int n0 = nE; for (int i = 0; i < 2000; i++) Motion_Task(); CHECK(nE == n0, "a paused machine kept producing entries");
      Motion_Resume(); { int g = 0; while (!Motion_Task() && g++ < 4000000) { } }
      at = Motion_GetComputedPosition();
      printf("  resumed: ended at (%ld,%ld), end deviation %.3f step, double jumps %d, short holds %d\n", (long)at.X, (long)at.Y, endErr(), doubleJumps(), shortHolds());
      CHECK(at.X == Ed.X && at.Y == Ed.Y && Motion_IsIdle(), "the resumed arc did not finish at its end point");
      CHECK(endErr() < 0.6 && doubleJumps() == 0 && shortHolds() == 0, "resumed arc: dev %.3f, double jumps %d, short holds %d", endErr(), doubleJumps(), shortHolds());
   }

   printf("\n7. An arc that is not an arc: a radius under one motor step is a straight move to the end point\n");
   {
      Motion_Position_t S = { 1000, 1000 }, Ed = { 1007, 1007 };       /* centre (1007, 1000), radius 7 um, a quarter turn */
      seed_at(S); CHECK(Motion_ArcCheck(&Ed, 7, 0, 0), "ArcCheck refused a tiny arc");
      Motion_QueueArc(&Ed, 7, 0, 0); runCollect();
      printf("  radius 7 um: %d waypoint(s), ended at (%ld,%ld), end deviation %.3f step\n", nW, (long)Motion_GetComputedPosition().X, (long)Motion_GetComputedPosition().Y, endErr());
      CHECK(Motion_GetComputedPosition().X == Ed.X && Motion_GetComputedPosition().Y == Ed.Y, "a degenerate arc did not reach its end");
   }

   printf("\n8. Fuzz: 500 random lines and arcs through the planner, random halts, 25 %% of requests refused\n");
   {
      seed_at(o); g_refusePct = 25; g_rng = 4242; srand(5);
      long items = 0, arcsN = 0, entries = 0, bad = 0, refusedByCheck = 0; double worst = 0; int dj = 0, sh = 0;
      Motion_Position_t p = o;
      for (int i = 0; i < 500; i++)
      {
         Motion_Position_t t; long I = 0, J = 0; int cw = rand() & 1, isArc = (rand() % 100) < 60;
         if (isArc)
         {
            double r = exp(log(20.0) + (log(150000.0) - log(20.0)) * (rand() / (double)RAND_MAX));
            double a0 = (rand() / (double)RAND_MAX) * 2 * M_PI, sweep = ((rand() % 10) == 0) ? 2 * M_PI : (rand() / (double)RAND_MAX) * 2 * M_PI;
            I = lround(-r * cos(a0)); J = lround(-r * sin(a0));
            long cx = p.X + I, cy = p.Y + J;
            t = (sweep == 2 * M_PI) ? p : pointOnCircle(cx, cy, hypot((double)I, (double)J), atan2((double)-J, (double)-I) + (cw ? -1 : 1) * sweep);
            if (!Motion_ArcCheck(&t, I, J, (uint8_t)cw)) { refusedByCheck++; continue; }
         }
         else
         {
            long reach = (i % 3 == 0) ? 600 : (i % 3 == 1) ? 30000 : 150000;
            t.X = p.X + (rand() % (2 * reach + 1)) - reach; t.Y = p.Y + (rand() % (2 * reach + 1)) - reach;
         }
         long guard = 0; int queued = 0, halted = 0;
         while ((!queued || !Motion_IsIdle() || !Motion_PlannerIsEmpty() || halted) && guard++ < 6000000)
         {
            if (!queued && (isArc ? Motion_QueueArc(&t, I, J, (uint8_t)cw) : Motion_QueueMove(t))) queued = 1;
            if ((rand() % 4000) == 0 && !halted && queued) { Motion_Halt(); halted = 1; }
            Motion_Task();
            if (halted && Motion_IsHaltRampDone() && (rand() % 50) == 0) { Motion_Resume(); halted = 0; }
         }
         if (guard >= 6000000) { bad++; printf("  item %d (%s) did not finish\n", i, isArc ? "arc" : "line"); break; }
         double e = endErr(); if (e > worst) worst = e; if (e > 0.62) bad++;
         if (Motion_GetComputedPosition().X != t.X || Motion_GetComputedPosition().Y != t.Y) { bad++; if (isArc) printf("  item %d: arc from (%ld,%ld) I=%ld J=%ld to (%ld,%ld) cw=%d\n", i, (long)p.X, (long)p.Y, I, J, (long)t.X, (long)t.Y, cw); printf("  item %d (%s): ended at (%ld,%ld), wanted (%ld,%ld)\n", i, isArc ? "arc" : "line", (long)Motion_GetComputedPosition().X, (long)Motion_GetComputedPosition().Y, (long)t.X, (long)t.Y); }
         p = t; items++; arcsN += isArc; entries += nE; dj += doubleJumps(); sh += shortHolds(); nE = 0;
      }
      g_refusePct = 0;
      printf("  %ld items (%ld arcs; %ld random arcs the radius rule refused), %ld entries: double jumps %d, holds below MIN %d, bad ends %ld, worst end deviation %.3f step\n", items, arcsN, refusedByCheck, entries, dj, sh, bad, worst);
      CHECK(dj == 0 && sh == 0 && bad == 0, "fuzz found %d double jumps, %d short holds, %ld bad ends", dj, sh, bad);
   }

   printf("\nstream hash over %ld entries: %08x\n", g_entries_total, (unsigned)g_hash);
   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
