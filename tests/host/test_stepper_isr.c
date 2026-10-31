/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Timer1 event ISR (REQ-CoreLR-D-Stepper-0010..0014): the real Stepper.c
 * is #included, so its statics are visible, and Timer1 is simulated count by count with a
 * configurable interrupt latency.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Ishim -I../../firmware/src test_stepper_isr.c -o out/test_stepper_isr
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/src/Stepper.c"

volatile uint8_t PORTB, PORTC, PORTD, DDRB, DDRC, DDRD, TCCR1A, TCCR1B, TIMSK1, TIFR1, SREG;
volatile uint16_t TCNT1, OCR1A;

static int g_invalidations, g_penCalls; static long g_penTime; static uint8_t g_penAction;
static uint8_t g_wIdxAtInvalidate = 255;
void Calibration_InvalidatePosition(void) { g_invalidations++; g_wIdxAtInvalidate = s_queue.Indices.WriteIndex; }

static long now;                 /* absolute count */
static int g_latency;            /* extra counts between the compare match and the ISR starting */
static int g_isrCost = 20;       /* counts the ISR itself takes before it reads TCNT1 */
static int g_fired;
void PenControl_ApplyFromIsr(uint8_t a) { g_penCalls++; g_penTime = now; g_penAction = a; }

/* log of every ISR that applied an entry: when, and what */
typedef struct { long t; uint8_t bits; } Ev;
static Ev g_ev[100000]; static int g_nev;

static uint8_t portBits(void)  /* decode the ports back into the entry's 8 bits, via the real wire macros */
{
   uint8_t b = 0;
#define RD(w, n) do { if (MOTOR_##w##_PORT & (1u << MOTOR_##w##_BIT)) b |= (uint8_t)(1u << (n)); } while (0)
   RD(L_WIRE0,0); RD(L_WIRE1,1); RD(L_WIRE2,2); RD(L_WIRE3,3); RD(R_WIRE0,4); RD(R_WIRE1,5); RD(R_WIRE2,6); RD(R_WIRE3,7);
   return b;
}

/* one count of simulated time: advance the counter, and fire the compare ISR when it matches */
static long pendingFire = -1;
static void tick(void)
{
   now++;
   TCNT1 = (uint16_t)(TCNT1 + 1u);
   if (TCNT1 == OCR1A && (TIMSK1 & (1u << OCIE1A)) && pendingFire < 0) { pendingFire = now + (g_latency ? rand() % (g_latency + 1) : 0); }
   if (pendingFire >= 0 && now >= pendingFire && (TIMSK1 & (1u << OCIE1A)))
   {
      uint8_t readBefore = s_queue.Indices.ReadIndex; pendingFire = -1;
      /* the ISR reads TCNT1 g_isrCost counts into itself; that time passes for everyone */
      now += g_isrCost; TCNT1 = (uint16_t)(TCNT1 + g_isrCost);
      ISR_TIMER1_COMPA_vect();
      g_fired++;
      if (s_queue.Indices.ReadIndex != readBefore && g_nev < 100000) { g_ev[g_nev].t = now - g_isrCost; g_ev[g_nev].bits = portBits(); g_nev++; }
   }
}
static void run(long counts) { while (counts-- > 0) tick(); }
static void reset_world(void)
{
   memset(&s_queue, 0, sizeof s_queue); s_pendingTicksRemaining = 0; s_armed = 0; s_lateCount = 0; s_lastCoilBits = 0; s_enqueueHalted = 0;
   TCNT1 = 0; OCR1A = 0; TIMSK1 = 0; TIFR1 = 0; PORTB = PORTD = 0; now = 0; pendingFire = -1; g_nev = 0; g_fired = 0;
   g_invalidations = 0; g_penCalls = 0; g_wIdxAtInvalidate = 255;
   Stepper_Init();
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
   srand(3);

   /* ---- 1. the split rule: every piece within [MIN, MAX], pieces sum to the request ---- */
   {
      long bad = 0, n = 0;
      for (uint32_t d = STEPPER_DELAY_MIN_COUNTS; d < 400000; d++)
      {
         uint32_t rem = d, sum = 0; int pieces = 0;
         while (rem > 0)
         {
            uint16_t c = NextChunk(rem);
            if (c < STEPPER_DELAY_MIN_COUNTS || c > STEPPER_DELAY_MAX_COUNTS || c > rem) { bad++; break; }
            rem -= c; sum += c; pieces++;
         }
         if (sum != d && rem == 0) bad++;
         n++;
      }
      CHECK(bad == 0, "split rule broke the limits on %ld of %ld requests", bad, n);
      printf("split rule: %ld requests checked (every piece in [%u, %u], sum exact)\n", n, STEPPER_DELAY_MIN_COUNTS, STEPPER_DELAY_MAX_COUNTS);
      /* the maintainer's example: MAX + 5 must not become MAX then 5 */
      CHECK(NextChunk(STEPPER_DELAY_MAX_COUNTS + 5) == STEPPER_DELAY_MAX_COUNTS + 5 - STEPPER_DELAY_MIN_COUNTS, "MAX+5 left a remainder below MIN");
   }

   /* ---- 2. timing: error never accumulates, whatever the latency ---- */
   for (int lat = 0; lat <= 40; lat += 20)
   {
      reset_world(); g_latency = lat;
      long nominal[400]; long t = 0; int nq = 0, fed = 0; uint8_t bits = 1;
      Stepper_PenAction_t none = STEPPER_PEN_NONE;
      while (fed < 300 || !Stepper_IsIdle())
      {
         if (fed < 300)
         {
            uint32_t hold = 80 + (uint32_t)(rand() % 12000);
            uint8_t l = (uint8_t)(bits & 0xF), r = (uint8_t)((bits >> 4) & 0xF);
            if (Stepper_RequestMotion(l, r, hold, none))
            {
               nominal[nq++] = t; t += hold; fed++; bits = (uint8_t)(bits * 3u + 1u); if (!bits) bits = 1;
            }
         }
         Stepper_Task(); run(37);
      }
      /* entry k is applied at start + nominal[k]; start = the first ISR time */
      long worst = 0, drift = 0;
      for (int k = 0; k < g_nev && k < nq; k++)
      {
         long e = (g_ev[k].t - g_ev[0].t) - nominal[k]; if (e < 0) e = -e;
         if (e > worst) worst = e;
         if (k == nq - 1) drift = e;
      }
      printf("timing, latency %2d counts: %d entries, worst error %ld counts, last error %ld\n", lat, g_nev, worst, drift);
      CHECK(worst <= lat + 40, "timing error %ld exceeds latency %d (+margin)", worst, lat);
      CHECK(g_nev >= nq, "ISR applied %d entries, %d queued", g_nev, nq);
   }

   /* ---- 3. the late guard: a holds shorter than the latency must not stall the timer for a counter wrap ---- */
   reset_world(); g_latency = 150;   /* far above MIN: the interrupt is served very late */
   {
      Stepper_RequestMotion(1, 0, 64, STEPPER_PEN_NONE); Stepper_RequestMotion(2, 0, 64, STEPPER_PEN_NONE); Stepper_RequestMotion(3, 0, 64, STEPPER_PEN_NONE);
      run(2000);
      CHECK(g_nev == 3, "late guard: only %d of 3 entries ran within 2000 counts (a stall would show as a 65536-count gap)", g_nev);
      CHECK(s_lateCount > 0, "late guard never fired though the latency exceeded the hold");
      printf("late guard: 3 entries of %u counts under a %d-count latency all ran, late count %u\n", 64u, g_latency, s_lateCount);
   }

   /* ---- 4. idle / arm / idle, repeatedly ---- */
   reset_world(); g_latency = 3;
   for (int round = 0; round < 5; round++)
   {
      CHECK(Stepper_IsIdle(), "round %d: must be idle before a request", round);
      CHECK(Stepper_RequestMotion(5, 5, 3000, STEPPER_PEN_NONE), "round %d: request refused", round);
      CHECK(!Stepper_IsIdle(), "round %d: armed machine reported idle", round);
      run(2000);
      CHECK(!Stepper_IsIdle(), "round %d: idle reported while the last hold (3000) is still running", round);
      run(1500);
      CHECK(Stepper_IsIdle(), "round %d: not idle after the last hold expired (armed=%d)", round, s_armed);
   }

   /* ---- 5. pen settle: 600 000 counts through the backlog, pen action exactly once, at the start ---- */
   reset_world(); g_latency = 2;
   {
      long t0; int fedOk = Stepper_RequestPenOnly(STEPPER_PEN_DOWN, 600000UL);
      CHECK(fedOk, "pen-only request refused");
      while (!Stepper_IsIdle() && now < 2000000) { Stepper_Task(); run(50); }
      t0 = g_ev[0].t;
      CHECK(g_penCalls == 1 && g_penAction == STEPPER_PEN_DOWN, "pen action fired %d times", g_penCalls);
      CHECK(g_penTime == t0 + g_isrCost, "pen action not at the first entry's instant");
      printf("pen settle: 600000 counts in %d entries, finished at %ld counts after the first (expect ~600000)\n", g_nev, now - t0);
      CHECK(labs((now - t0) - 600000L) < 400, "pen settle took %ld counts", now - t0);
   }

   /* ---- 6. invalidation happens before the entry is visible to the ISR ---- */
   reset_world(); s_lastCoilBits = 0; g_latency = 0;
   {
      uint8_t wBefore = s_queue.Indices.WriteIndex;
      Stepper_RequestMotion(1, 0, 500, STEPPER_PEN_NONE);
      CHECK(g_invalidations == 1, "motion entry with new coil bits must invalidate once, got %d", g_invalidations);
      CHECK(g_wIdxAtInvalidate == wBefore, "invalidation ran AFTER the entry was published");
      Stepper_RequestMotion(1, 0, 500, STEPPER_PEN_NONE);
      CHECK(g_invalidations == 2 || g_invalidations == 1, "unchanged bits");
      int before = g_invalidations;
      Stepper_RequestPenOnly(STEPPER_PEN_UP, 500);
      CHECK(g_invalidations == before, "a pen-only entry must not invalidate the position");
   }

   /* ---- 7. the ports carry the entry's bits ---- */
   reset_world(); g_latency = 0;
   {
      const uint8_t pat[] = { 0x00, 0xFF, 0x0C, 0x06, 0x93, 0x5A };
      for (unsigned i = 0; i < sizeof pat; i++) Stepper_RequestMotion((uint8_t)(pat[i] & 0xF), (uint8_t)(pat[i] >> 4), 400, STEPPER_PEN_NONE);
      run(5000);
      CHECK(g_nev == (int)(sizeof pat), "%d of %zu entries applied", g_nev, sizeof pat);
      for (int i = 0; i < g_nev; i++) CHECK(g_ev[i].bits == pat[i], "entry %d: ports show %02X, entry says %02X", i, g_ev[i].bits, pat[i]);
   }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
