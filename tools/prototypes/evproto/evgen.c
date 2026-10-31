/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * evgen.c -- PROTOTYPE of the v2 step-event generator. See evgen.h.
 *
 * The chain it implements (REQ-CoreLR-D-Motion-0018):
 *
 *   straight move -> N coarse waypoints on the exact line (~215 um apart)
 *     -> per waypoint: Motion_XYToLR, then cable length in 1/256 steps
 *     -> inside a segment each motor's position is LINEAR in time
 *     -> a step is emitted at the instant that line crosses
 *        (c +- 1/2) * 256 +- H, with c the commanded count
 *     -> the two motors' events are merged in time order into entries
 *        { coil bits after the event, hold until the next event }
 */
#include <stdint.h>
#include "evgen.h"
#include "HwConfig.h"

/* ---- tunables (overridable with -D for the experiments) ---------------- */
#ifndef EV_H
#define EV_H 16                /* tie guard, in 1/256 step, each side of +-1/2 (measured: 0 chatters on ties) */
#endif
#ifndef EV_MERGE_MODE
#define EV_MERGE_MODE 1        /* 0: apply both at the earlier instant; 1: delay the later one (no hold below MIN, measured) */
#endif
#ifndef EV_MIN_COUNTS
#define EV_MIN_COUNTS 64       /* 32 us at 0.5 us/count */
#endif
#ifndef EV_WAYPOINT_STEPS
#define EV_WAYPOINT_STEPS 4
#endif
#ifndef EV_VMAX_U
#define EV_VMAX_U 86           /* 21.5 mm/s, in 0.25 mm/s */
#endif
#ifndef EV_VMIN_U
#define EV_VMIN_U 16
#endif
#ifndef EV_ACCEL_MM_S2
#define EV_ACCEL_MM_S2 192
#endif

#define EV_SUBSTEP       256L
#define EV_CIRC_UM       ((MOTION_SPOOL_DIAMETER_UM * 355L) / 113L)
#define EV_WAYPOINT_UM   ((EV_WAYPOINT_STEPS * EV_CIRC_UM) / (long)MOTION_STEPS_PER_REV)
#define EV_NONE          0xFFFFFFFFUL
/* v^2 in (0.25 mm/s)^2 gains  2*a*s  with s in um:  2 * a[mm/s^2]*1000 um/s^2 / 62500 = a * 32 / 1000 per um */
#define EV_V2_PER_MILLE_UM (EV_ACCEL_MM_S2 * 32L)

static uint32_t ISqrt32(uint32_t value)
{
   uint32_t remainder = value, root = 0, bit = 1UL << 30;

   while (bit > remainder) { bit >>= 2; }
   while (bit != 0)
   {
      if (remainder >= root + bit) { remainder -= root + bit; root = (root >> 1) + bit; }
      else { root >>= 1; }
      bit >>= 2;
   }
   return root;
}

/** Cable length in um -> 1/256 steps, round to nearest. */
static int32_t LengthToSub(int32_t lengthUm)
{
   int64_t numerator = (int64_t)lengthUm * (int64_t)MOTION_STEPS_PER_REV * EV_SUBSTEP;

   return (int32_t)((numerator + EV_CIRC_UM / 2) / EV_CIRC_UM);
}

static void ToSub(Motion_Position_t p, int32_t *sL, int32_t *sR)
{
   Motion_CableLengths_t lr = Motion_XYToLR(p);

   *sL = LengthToSub(lr.L);
   *sR = LengthToSub(lr.R);
}

typedef struct
{
   /* commanded step counts: the only per-motor state that survives a segment */
   int32_t cL, cR;

   /* timeline, in Timer1 counts. All differences, so wrap-around is harmless. */
   uint32_t tSeg;           /* start of the current segment */
   uint8_t havePending;
   uint8_t pendCoilL, pendCoilR;
   uint32_t pendTime;
   uint32_t lastEvent;      /* time of the last emitted event, for the delay-mode merge */
   uint8_t anyEvent;

   /* move */
   Motion_Position_t pos;   /* the waypoint we are at */
   int32_t sL, sR;          /* its cable lengths, 1/256 steps */
   int32_t signX, signY;
   uint32_t absDx, absDy, len, n, k, qx, rx, qy, ry, accX, accY;
   uint32_t segLenQ8, sQ8;
   Motion_Position_t target;
   uint8_t vEntry, vExit;
   uint8_t moving;

   /* segment */
   uint8_t inSegment;
   int32_t sL1, sR1;
   Motion_Position_t nextPos;
   uint32_t T;
   uint32_t tL, tR;         /* next crossing, relative to the segment start, or EV_NONE */
} Gen_t;

static Gen_t g;
static EvGen_Stats_t s_stats;

const EvGen_Stats_t *EvGen_GetStats(void) { return &s_stats; }

void EvGen_GetCommandedSteps(int32_t *stepsL, int32_t *stepsR) { *stepsL = g.cL; *stepsR = g.cR; }

void EvGen_Init(int32_t stepsL, int32_t stepsR, Motion_Position_t position)
{
   g.cL = stepsL; g.cR = stepsR;
   g.pos = position;
   ToSub(position, &g.sL, &g.sR);
   g.tSeg = 0; g.lastEvent = 0; g.anyEvent = 0;
   g.pendCoilL = Motion_StepToCoilBits(stepsL, STEPPER_MOTOR_L_REVERSED);
   g.pendCoilR = Motion_StepToCoilBits(stepsR, STEPPER_MOTOR_R_REVERSED);
   g.pendTime = 0; g.havePending = 1;
   g.moving = 0; g.inSegment = 0;
   s_stats.Events = s_stats.Merged = s_stats.Segments = s_stats.ShortHolds = 0;
}

void EvGen_StartMove(Motion_Position_t target, uint8_t entryV, uint8_t exitV)
{
   int32_t dx = target.X - g.pos.X;
   int32_t dy = target.Y - g.pos.Y;

   g.target = target;
   g.signX = (dx < 0) ? -1 : 1; g.absDx = (uint32_t)((dx < 0) ? -dx : dx);
   g.signY = (dy < 0) ? -1 : 1; g.absDy = (uint32_t)((dy < 0) ? -dy : dy);
   g.len = (uint32_t)Motion_ISqrt((uint64_t)g.absDx * g.absDx + (uint64_t)g.absDy * g.absDy);
   g.n = (g.len + EV_WAYPOINT_UM - 1) / EV_WAYPOINT_UM;
   if (g.n < 1) { g.n = 1; }
   g.qx = g.absDx / g.n; g.rx = g.absDx % g.n; g.accX = g.n / 2;
   g.qy = g.absDy / g.n; g.ry = g.absDy % g.n; g.accY = g.n / 2;
   g.segLenQ8 = (g.len * 256UL) / g.n;
   g.sQ8 = 0; g.k = 0;
   g.vEntry = entryV; g.vExit = exitV;
   g.moving = 1; g.inSegment = 0;
   g.nextPos = g.pos;
}

static uint32_t CrossTime(int32_t c, int32_t s0, int32_t s1, uint32_t T)
{
   int32_t ds = s1 - s0;
   int32_t thr;
   int32_t num;

   if (ds > 0)
   {
      thr = (int32_t)(c * EV_SUBSTEP) + 128 + EV_H;
      if (s1 < thr) { return EV_NONE; }
      num = thr - s0;
   }
   else if (ds < 0)
   {
      thr = (int32_t)(c * EV_SUBSTEP) - 128 - EV_H;
      if (s1 > thr) { return EV_NONE; }
      num = s0 - thr;
      ds = -ds;
   }
   else
   {
      return EV_NONE;
   }
   if (num <= 0) { return 0; }
   return ((uint32_t)num * T + (uint32_t)ds - 1u) / (uint32_t)ds;   /* ceil: first instant at or past the threshold */
}

/** Set up the next segment: its end waypoint, its duration, its first crossings. */
static void BeginSegment(void)
{
   uint32_t sMid, rem, v2a, v2d, v2, v;
   Motion_Position_t p;

   /* waypoint k+1, exactly on the line, by incremental proportional rounding */
   if (g.k + 1u == g.n)
   {
      p = g.target;                                   /* the last one is exact, not rounded */
   }
   else
   {
      uint32_t ix = g.qx, iy = g.qy;

      g.accX += g.rx; if (g.accX >= g.n) { g.accX -= g.n; ix++; }
      g.accY += g.ry; if (g.accY >= g.n) { g.accY -= g.n; iy++; }
      p.X = g.pos.X + g.signX * (int32_t)ix;
      p.Y = g.pos.Y + g.signY * (int32_t)iy;
   }
   g.nextPos = p;
   ToSub(p, &g.sL1, &g.sR1);

   /* velocity at the middle of the segment (REQ-CoreLR-D-Motion-0020) */
   sMid = (g.sQ8 + g.segLenQ8 / 2u) >> 8;
   rem = (g.len > sMid) ? (g.len - sMid) : 0u;
   if (sMid > 2000u) { sMid = 2000u; }                /* clamp: v^2 saturates far below VMAX^2 anyway */
   if (rem > 2000u) { rem = 2000u; }
   v2a = (uint32_t)g.vEntry * g.vEntry + (uint32_t)((EV_V2_PER_MILLE_UM * (int32_t)sMid) / 1000L);
   v2d = (uint32_t)g.vExit * g.vExit + (uint32_t)((EV_V2_PER_MILLE_UM * (int32_t)rem) / 1000L);
   v2 = (v2a < v2d) ? v2a : v2d;
   v = ISqrt32(v2);
   if (v > EV_VMAX_U) { v = EV_VMAX_U; }
   if (v < EV_VMIN_U) { v = EV_VMIN_U; }

   /* T = d * 2e6 / (v * 250) = d * 8000 / v, d in um carried as Q8 */
   g.T = ((g.segLenQ8 * 8000UL) / v + 128u) >> 8;
   if (g.T < 1u) { g.T = 1u; }

   g.tL = CrossTime(g.cL, g.sL, g.sL1, g.T);
   g.tR = CrossTime(g.cR, g.sR, g.sR1, g.T);
   g.inSegment = 1;
   s_stats.Segments++;
}

static void EndSegment(void)
{
   g.tSeg += g.T;
   g.sQ8 += g.segLenQ8;
   g.pos = g.nextPos;
   g.sL = g.sL1; g.sR = g.sR1;
   g.k++;
   g.inSegment = 0;
   if (g.k == g.n) { g.moving = 0; }
}

uint8_t EvGen_Next(EvGen_Entry_t *out)
{
   for (;;)
   {
      uint32_t tFirst, tOther;
      uint8_t firstIsL;

      if (!g.inSegment)
      {
         if (!g.moving) { return 0; }
         BeginSegment();
      }

#if EV_MERGE_MODE == 1
      /* delay mode: no event earlier than lastEvent + MIN. One pushed past the end of the
       * segment is not lost: its threshold is still behind the next segment's start, so it
       * comes out there at relative time 0, clamped the same way. */
      if (g.anyEvent)
      {
         int32_t floorT = (int32_t)((g.lastEvent + EV_MIN_COUNTS) - g.tSeg);

         if (floorT > 0)
         {
            if (g.tL != EV_NONE && (int32_t)g.tL < floorT) { g.tL = (uint32_t)floorT; }
            if (g.tR != EV_NONE && (int32_t)g.tR < floorT) { g.tR = (uint32_t)floorT; }
         }
      }
      if (g.tL != EV_NONE && g.tL > g.T) { g.tL = EV_NONE; }
      if (g.tR != EV_NONE && g.tR > g.T) { g.tR = EV_NONE; }
#endif

      if (g.tL == EV_NONE && g.tR == EV_NONE)
      {
         EndSegment();
         continue;
      }

      firstIsL = (uint8_t)(g.tL <= g.tR);
      tFirst = firstIsL ? g.tL : g.tR;
      tOther = firstIsL ? g.tR : g.tL;

      /* apply the first motor's step */
      if (firstIsL) { g.cL += (g.sL1 > g.sL) ? 1 : -1; g.tL = CrossTime(g.cL, g.sL, g.sL1, g.T); }
      else          { g.cR += (g.sR1 > g.sR) ? 1 : -1; g.tR = CrossTime(g.cR, g.sR, g.sR1, g.T); }

#if EV_MERGE_MODE == 0
      if (tOther != EV_NONE && (tOther - tFirst) < EV_MIN_COUNTS)
      {
         /* both inside the minimum delay: one entry, the later step comes early by < MIN */
         if (firstIsL) { g.cR += (g.sR1 > g.sR) ? 1 : -1; g.tR = CrossTime(g.cR, g.sR, g.sR1, g.T); }
         else          { g.cL += (g.sL1 > g.sL) ? 1 : -1; g.tL = CrossTime(g.cL, g.sL, g.sL1, g.T); }
         s_stats.Merged++;
      }
#endif
      {
         uint32_t tAbs = g.tSeg + tFirst;
         uint32_t hold = tAbs - g.pendTime;

         out->CoilL = g.pendCoilL; out->CoilR = g.pendCoilR; out->Hold = hold;
         if (hold < EV_MIN_COUNTS) { s_stats.ShortHolds++; }
         g.pendCoilL = Motion_StepToCoilBits(g.cL, STEPPER_MOTOR_L_REVERSED);
         g.pendCoilR = Motion_StepToCoilBits(g.cR, STEPPER_MOTOR_R_REVERSED);
         g.pendTime = tAbs;
         g.lastEvent = tAbs;
         g.anyEvent = 1;
         s_stats.Events++;
      }
      return 1;
   }
}

uint8_t EvGen_Flush(EvGen_Entry_t *out)
{
   if (!g.havePending) { return 0; }
   out->CoilL = g.pendCoilL; out->CoilR = g.pendCoilR;
   out->Hold = g.tSeg - g.pendTime;
   g.havePending = 0;
   return 1;
}
