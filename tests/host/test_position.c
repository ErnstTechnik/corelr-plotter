/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the persisted position (REQ-CoreLR-D-Calibration-0006..0007)
 * and of the restore at start-up. The real Calibration.c and Motion.c are
 * compiled; Stepper is stubbed. The EEPROM is an array that can lose power
 * at any byte write -- cleanly before it, cleanly after it, or half-way
 * through it (the byte left as old&new, or as 0xFF).
 *
  *   gcc -m32 -fpack-struct=1 -O1 -Ishim -I../../firmware/src test_position.c \
 *       ../../firmware/src/Calibration.c ../../firmware/src/Motion.c -o out/test_position
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <avr/eeprom.h>
#include "Calibration.h"
#include "Motion.h"
#include "Stepper.h"
#include "SwConfig.h"
#include "HwConfig.h"

/* ---------------------------------------------------------------- EEPROM */
static uint8_t g_ee[1024];
static int g_writes;          /* byte writes that actually changed something */
static int g_cutAt = -1;      /* index of the write the power goes out on; -1 = never */
static int g_tornMode;        /* 0 = not applied, 1 = old&new, 2 = 0xFF, 3 = applied */
static int g_dead;
static unsigned g_cell[1024]; /* writes each cell has really had: the wear */

void eeprom_read_block(void *dst, const void *src, size_t n) { memcpy(dst, &g_ee[(size_t)src], n); }
uint8_t eeprom_read_byte(const uint8_t *a) { return g_ee[(size_t)a]; }
static void write_byte(size_t a, uint8_t v)
{
   if (g_dead || g_ee[a] == v) return;          /* update semantics: unchanged byte = no write */
   if (g_writes == g_cutAt)
   {
      switch (g_tornMode) { case 1: g_ee[a] &= v; break; case 2: g_ee[a] = 0xFF; break; case 3: g_ee[a] = v; break; default: break; }
      g_dead = 1;
   }
   else g_ee[a] = v;
   g_cell[a]++;
   g_writes++;
}
void eeprom_update_byte(uint8_t *a, uint8_t v) { write_byte((size_t)a, v); }
void eeprom_update_block(const void *s, void *d, size_t n) { for (size_t i = 0; i < n; i++) write_byte((size_t)d + i, ((const uint8_t *)s)[i]); }

/* ------------------------------------------------------- Stepper stubs */
static int g_entries; static uint8_t g_lastL, g_lastR; static int g_changesL, g_changesR;
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{ (void)h; (void)p; g_entries++; if (l != g_lastL) g_changesL++; if (r != g_lastR) g_changesR++; g_lastL = l; g_lastR = r; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void store_now(void) { Calibration_StoreCurrentPosition(); }
/* put Motion into a given state, then store it */
static void store_state(int32_t l, int32_t r, int32_t x, int32_t y) { Motion_Restore(l, r, x, y); Calibration_StoreCurrentPosition(); }

static void reboot(void) { g_dead = 0; g_cutAt = -1; Calibration_Init(); Motion_Init(); }

static void configure(void)
{
   Calibration_Params_t p;
   Calibration_GetParams(&p);
   p.MotorDistanceUm = 1000000L; p.ReferenceCableLengthUm = 700000L;
   Calibration_SetParams(&p);
   Calibration_MarkCalibrated();
}

int main(void)
{
   int32_t l, r;
   int k, mode;

   /* 1. blank EEPROM: not calibrated, nothing stored */
   memset(g_ee, 0xFF, sizeof g_ee);
   reboot();
   CHECK(!Calibration_IsCalibrated() && !Calibration_IsPositionStored(), "blank EEPROM must read as uncalibrated");

   /* 2. calibrate, store, reboot: restored exactly */
   configure();
   {
      Motion_Position_t p = { 12345, -6789 }, q;
      int32_t x, y;
      Motion_SetCurrentPosition(p);
      store_now();
      reboot();
      CHECK(Calibration_IsCalibrated() && Calibration_IsPositionStored(), "stored position must restore");
      q = Motion_GetComputedPosition();
      CHECK(q.X == p.X && q.Y == p.Y, "position must restore exactly, got %ld,%ld", (long)q.X, (long)q.Y);
      Motion_GetPersistedState(&l, &r, &x, &y);
      {  /* the persisted position is INTERNAL (Y down): drawing X + offset, offset - drawing Y. The flip of the drawing frame
          * (REQ-CoreLR-D-Motion-0025) leaves what is stored valid, since this conversion mirrors the one in Motion_XYToLR() */
         int32_t ox, oy; Calibration_GetWorkOrigin(&ox, &oy);
         CHECK(x == p.X + ox && y == (CORELR_Y_UP ? oy - p.Y : oy + p.Y), "persisted internal position (%ld,%ld), expected (%ld,%ld)", (long)x, (long)y, (long)(p.X + ox), (long)(CORELR_Y_UP ? oy - p.Y : oy + p.Y));
         /* and an image stored by the previous firmware (same internal numbers) restores to the same point, mirrored in the drawing frame */
         Motion_Restore(l, r, ox + 1000, oy + 7000);
         q = Motion_GetComputedPosition();
         CHECK(q.X == 1000 && q.Y == (CORELR_Y_UP ? -7000 : 7000), "restoring internal (+1000,+7000 from the origin) gave drawing (%ld,%ld), expected (1000,%d)", (long)q.X, (long)q.Y, CORELR_Y_UP ? -7000 : 7000);
      }
      Motion_SetCurrentPosition(p);
   }

   Motion_SetCurrentPosition(Motion_GetComputedPosition()); store_now(); reboot();
   /* 3. THE BENCH DEFECT: after a restore, a jog must not walk the motors
    *    one step per waypoint. 600 um in X is ~11 motor steps, not ~100. */
   {
      Motion_Position_t p = Motion_GetComputedPosition();
      g_entries = g_changesL = g_changesR = 0;
      p.X += 600;
      Motion_RequestLinearMove(p);
      for (int i = 0; i < 400 && !Motion_Task(); i++) {}
      printf("restored jog: %d entries, motor L changed %d times, R %d times (600 um is ~11 steps; a catch-up walk to the real ideal would be thousands)\n",
             g_entries, g_changesL, g_changesR);
      CHECK(g_changesL <= 14 && g_changesR <= 14, "restored machine is still catching up");
   }

   /* 4. power cut at every byte of  Invalidate; Store(B)  -- 4 torn modes */
   {
      int32_t A_l = 13000, A_r = 12900, B_l = 13400, B_r = 12500;
      int cases = 0;
      static const int pres[] = { 0, 1, 2, 5, 31, 32, 33, 63, 64, 65, 127, 128, 129, 200 };   /* cycles before the cut: the head and the sequence number are anywhere, wraps of both included (64 slots: 32 cycles; 8-bit sequence: 128 cycles) */
      for (int pi = 0; pi < (int)(sizeof pres / sizeof pres[0]); pi++)
      for (mode = 0; mode < 4; mode++)
         for (k = 0; k < 40; k++)
         {
            int invDone;
            memset(g_ee, 0xFF, sizeof g_ee);
            reboot(); configure(); store_state(A_l, A_r, A_l * 7, A_r * 3);
            for (int c = 0; c < pres[pi]; c++) { Calibration_InvalidatePosition(); store_state(A_l, A_r, A_l * 7, A_r * 3); }
            g_writes = 0; g_dead = 0; g_cutAt = k; g_tornMode = mode;
            Calibration_InvalidatePosition();
            invDone = !g_dead;                              /* the invalidation completed: from here on the stale position A must never come back */
            store_state(B_l, B_r, B_l * 7, B_r * 3);
            if (!g_dead && k > g_writes) break;            /* sequence finished before the cut: nothing more to test */
            reboot(); cases++;
            if (Calibration_IsCalibrated())
            {
               int32_t sx, sy;
               Motion_GetPersistedState(&l, &r, &sx, &sy);   /* Motion_Init() restored it */
               int isA = (l == A_l && r == A_r && sx == A_l * 7 && sy == A_r * 3), isB = (l == B_l && r == B_r && sx == B_l * 7 && sy == B_r * 3);
               CHECK(isA || isB, "after %d cycles, cut at write %d mode %d restored a MIXED position %ld,%ld,%ld,%ld", pres[pi], k, mode, (long)l, (long)r, (long)sx, (long)sy);
               if (invDone) CHECK(isB, "after %d cycles, cut at write %d mode %d restored the STALE position A after the invalidate had completed", pres[pi], k, mode);
            }
         }
      printf("power-cut sweep: %d cases checked\n", cases);
   }

   /* 4b. the same cut sweep for a Store over a record that is STILL VALID (no Invalidate before it: Calibration_ConfirmCalibration() does exactly that after re-seeding
    *     Motion): the Store must invalidate first, so that once that has completed the old position can never come back */
   {
      int32_t A_l = 13000, A_r = 12900, B_l = 13400, B_r = 12500;
      static const int pres[] = { 0, 1, 31, 32, 33, 127, 128, 129 };
      static uint8_t saved[sizeof g_ee];
      int cases = 0;
      for (int pi = 0; pi < (int)(sizeof pres / sizeof pres[0]); pi++)
         for (mode = 0; mode < 4; mode++)
            for (k = 0; k < 40; k++)
            {
               int nInv;
               memset(g_ee, 0xFF, sizeof g_ee);
               reboot(); configure(); store_state(A_l, A_r, A_l * 7, A_r * 3);
               for (int c = 0; c < pres[pi]; c++) { Calibration_InvalidatePosition(); store_state(A_l, A_r, A_l * 7, A_r * 3); }
               memcpy(saved, g_ee, sizeof g_ee);
               g_writes = 0; g_dead = 0; g_cutAt = -1; Calibration_InvalidatePosition(); nInv = g_writes;     /* what the invalidation alone costs, in writes */
               memcpy(g_ee, saved, sizeof g_ee); reboot();
               g_writes = 0; g_dead = 0; g_cutAt = k; g_tornMode = mode;
               store_state(B_l, B_r, B_l * 7, B_r * 3);                                                       /* over a valid record */
               if (!g_dead && k > g_writes) break;
               reboot(); cases++;
               if (Calibration_IsCalibrated())
               {
                  int32_t sx, sy;
                  Motion_GetPersistedState(&l, &r, &sx, &sy);
                  int isA = (l == A_l && r == A_r && sx == A_l * 7 && sy == A_r * 3), isB = (l == B_l && r == B_r && sx == B_l * 7 && sy == B_r * 3);
                  CHECK(isA || isB, "store over a valid record: cut at write %d mode %d restored a MIXED position", k, mode);
                  if (k >= nInv) CHECK(isB, "store over a valid record, after %d cycles: cut at write %d (the invalidation takes %d) mode %d restored the STALE position A", pres[pi], k, nInv, mode);
               }
            }
      printf("store over a valid record, power-cut sweep: %d cases checked\n", cases);
   }

   /* 5. geometry change invalidates; pen angles do not */
   memset(g_ee, 0xFF, sizeof g_ee);
   reboot(); configure(); store_now();
   {
      Calibration_Params_t p; Calibration_GetParams(&p);
      p.PenUpAngle = 70; Calibration_SetParams(&p);
      CHECK(Calibration_IsCalibrated() && Calibration_IsPositionStored(), "a pen-angle change must not invalidate the position");
      p.ReferenceCableLengthUm = 710000L; Calibration_SetParams(&p);
      CHECK(!Calibration_IsCalibrated() && !Calibration_IsPositionStored(), "a geometry change must invalidate the position");
      reboot();
      CHECK(!Calibration_IsCalibrated(), "a geometry change must survive a reboot as uncalibrated");
   }

   /* the Adjustment jog moves the motors without Motion following: Calibration_Invalidate() (REQ-CoreLR-D-Calibration-0009) */
   memset(g_ee, 0xFF, sizeof g_ee);
   reboot(); configure(); store_now();
   CHECK(Calibration_IsCalibrated() && Calibration_IsPositionStored(), "setup: calibrated and stored");
   Calibration_Invalidate();
   CHECK(!Calibration_IsCalibrated() && !Calibration_IsPositionStored(), "Calibration_Invalidate() must clear the flag and the stored position");
   reboot();
   CHECK(!Calibration_IsCalibrated(), "an invalidated machine must be uncalibrated after a reboot (the position keeper stores only a calibrated machine's position)");
   configure(); store_now();
   CHECK(Calibration_IsCalibrated() && Calibration_IsPositionStored(), "confirming (MarkCalibrated + store) is the way back");

   /* 6. the stored internal position restores the drawing position exactly, for any drawing-origin offset in force */
   {
      long worst = 0; int n = 0;
      memset(g_ee, 0xFF, sizeof g_ee);
      reboot(); configure();
      for (long x = -300000; x <= 300000; x += 50000)
         for (long y = -150000; y <= 300000; y += 50000)
         {
            Motion_Position_t p = { x, y }, q;
            Motion_SetCurrentPosition(p);
            store_now(); reboot();
            q = Motion_GetComputedPosition();
            if (labs(q.X - x) + labs(q.Y - y) > worst) worst = labs(q.X - x) + labs(q.Y - y);
            n++;
         }
      printf("store/restore round trip: %d positions, worst error %ld um\n", n, worst);
      CHECK(worst == 0, "the position must restore exactly");
   }

   /* 7. the calibration reference height, floor(sqrt(L^2 - (D/2)^2)), against the exact 64-bit value, in either arithmetic */
   {
      long n = 0, bad = 0; const int64_t lim = CORELR_MATH_48_LIMIT_UM;
      struct { int32_t d, l; } fixed[] = { { 1000000, 700000 }, { 1000000, 500000 }, { 1000000, 500001 }, { 6000, 5000 }, { 8000, 5000 }, { 2000000, 1000000 }, { 1000000, 0 }, { 1000000, 499999 },
                                           { 3000, 2500 }, { 3500000, 2000000 }, { 1, 1 }, { 2, 1 }, { 0, 123456 }, { 1000000, 4194303 }, { 2, 3 } };
      for (int i = 0; i < 2000 + (int)(sizeof fixed / sizeof fixed[0]); i++)
      {
         int32_t d, L; Calibration_Params_t p;
         if (i < (int)(sizeof fixed / sizeof fixed[0])) { d = fixed[i].d; L = fixed[i].l; }
         else
         {
            d = (int32_t)(rand() % 4000000) + 1; L = (int32_t)(rand() % 4000000) + 1;
            if (i % 5 == 0) L = d / 2 + (rand() % 5) - 2;
            if (i % 7 == 0) { int64_t m = 1 + rand() % 900, q = 1 + rand() % 800; if (q < m) { d = (int32_t)(2 * (m * m - q * q)); L = (int32_t)(m * m + q * q); } }
         }
         Calibration_GetParams(&p); p.MotorDistanceUm = d; p.ReferenceCableLengthUm = L; Calibration_SetParams(&p); Calibration_GetParams(&p);
         int64_t h = d / 2, LL = L; uint64_t want = 0;
         if (LL * LL >= h * h) { uint64_t v = (uint64_t)(LL * LL - h * h); want = (uint64_t)sqrtl((long double)v); while (want * want > v) want--; while ((want + 1) * (want + 1) <= v) want++; }
         if (h > lim || LL > lim) continue;               /* beyond the bound the 48-bit build saturates; only in-range answers are specified */
         n++;
         if ((uint64_t)p.ReferencePositionY != want || p.ReferencePositionX != (int32_t)h) { bad++; CHECK(0, "D=%ld L=%ld: Y = %ld, want %llu", (long)d, (long)L, (long)p.ReferencePositionY, (unsigned long long)want); }
      }
      printf("calibration height: %ld machines (random, Pythagorean, L = D/2 +-2, L < D/2), %ld wrong\n", n, bad);
      CHECK(bad == 0, "calibration height wrong");
   }

#if CALIBRATION_POSITION_RING
   printf("\nThe position ring (REQ-CoreLR-D-Calibration-0010): %u slots of %u bytes, %u-byte fields\n", (unsigned)CALIBRATION_POSITION_SLOTS, (unsigned)(3u + 4u * (CORELR_MATH == CORELR_MATH_64 ? 4u : 3u)), (unsigned)(CORELR_MATH == CORELR_MATH_64 ? 4u : 3u));
   {
      /* a. many cycles with a reboot after each: the head and the 6-bit sequence number wrap (64 appends = 32 cycles; 16 slots), the newest is always found */
      int c, bad = 0;
      memset(g_ee, 0xFF, sizeof g_ee);
      reboot(); configure();
      for (c = 0; c < 400; c++)
      {
         int32_t a = 1000 + c * 37, b = 2000 - c * 11, x = c * 101 - 20000, y = c * 53;
         Calibration_InvalidatePosition();
         CHECK(!Calibration_IsPositionStored(), "cycle %d: not invalid after Invalidate", c);
         store_state(a, b, x, y);
         reboot();
         { int32_t l2, r2, x2, y2; Motion_GetPersistedState(&l2, &r2, &x2, &y2);
           if (!Calibration_IsCalibrated() || l2 != a || r2 != b || x2 != x || y2 != y) { if (bad++ < 3) CHECK(0, "cycle %d: restored (%ld,%ld,%ld,%ld) instead of (%ld,%ld,%ld,%ld), calibrated %d", c, (long)l2, (long)r2, (long)x2, (long)y2, (long)a, (long)b, (long)x, (long)y, Calibration_IsCalibrated()); } }
      }
      printf("  400 cycles, a reboot after each: %d wrong (the head went round the ring %d times, the sequence number %d times)\n", bad, 800 / (int)CALIBRATION_POSITION_SLOTS, 800 / 256);
      CHECK(bad == 0, "%d cycles restored the wrong position", bad);
      /* b. an invalidation that is the newest record: uncalibrated after a reboot, even though older valid records are still in the ring */
      Calibration_InvalidatePosition(); reboot();
      CHECK(!Calibration_IsCalibrated(), "the newest record is an invalidation, yet the machine came back calibrated (an older valid record won)");
   }
   {
      /* c. the extremes of a field */
      const int32_t lim = (CORELR_MATH == CORELR_MATH_64) ? 2147483647L : 8388607L;
      const int32_t ext[][4] = { { 0, 0, 0, 0 }, { lim, -lim - 1, lim, -lim - 1 }, { -1, 1, -256, 255 }, { 8388607L, -8388608L, 65535, -65536 } };
      for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++)
      {
         int32_t l2, r2, x2, y2;
         memset(g_ee, 0xFF, sizeof g_ee); reboot(); configure();
         Calibration_InvalidatePosition(); store_state(ext[i][0], ext[i][1], ext[i][2], ext[i][3]); reboot();
         Motion_GetPersistedState(&l2, &r2, &x2, &y2);
         CHECK(Calibration_IsCalibrated() && l2 == ext[i][0] && r2 == ext[i][1] && x2 == ext[i][2] && y2 == ext[i][3], "extremes #%u came back (%ld,%ld,%ld,%ld) calibrated %d", i, (long)l2, (long)r2, (long)x2, (long)y2, Calibration_IsCalibrated());
      }
#if CORELR_MATH != CORELR_MATH_64
      /* a value that does not fit 24 bits is not stored: the machine stays "unknown" (it cannot happen in this arithmetic: asserted at build time) */
      memset(g_ee, 0xFF, sizeof g_ee); reboot(); configure(); Calibration_InvalidatePosition();
      store_state(8388608L, 0, 0, 0); reboot();
      CHECK(!Calibration_IsCalibrated(), "a 25-bit value was stored (and came back truncated)");
#endif
   }
   {
      /* d. the wear: the point of it all. 20 000 motion cycles (an invalidation and a store of a position that changes), the writes each cell has had */
      const int C = 20000; int c; unsigned worst = 0, worstAt = 0;
      memset(g_ee, 0xFF, sizeof g_ee); reboot(); configure(); memset(g_cell, 0, sizeof g_cell);
      for (c = 0; c < C; c++)
      {
         Calibration_InvalidatePosition();
         store_state(10000 + (c % 997) * 3, 9000 - (c % 991) * 2, (c % 4001) * 7 - 14000, (c % 3989) * 5);
      }
      for (unsigned a = CALIBRATION_POSITION_EEPROM_ADDRESS; a < 1024; a++) if (g_cell[a] > worst) { worst = g_cell[a]; worstAt = a; }
      printf("  %d cycles: the most worn cell (address %u) has been written %u times -- %.3f writes per cycle; the single record's marker is written 2.000 per cycle\n", C, worstAt, worst, (double)worst / C);
      printf("  => at ~100 000 writes a cell lasts ~%.0f cycles with %u slots, against 50 000 with one record (x%.1f)\n", 100000.0 / ((double)worst / C), (unsigned)CALIBRATION_POSITION_SLOTS, (100000.0 / ((double)worst / C)) / 50000.0);
      CHECK(worst <= (unsigned)(2 * C / CALIBRATION_POSITION_SLOTS + 2), "a cell was written %u times in %d cycles, more than 2 * C / SLOTS + 2 = %u", worst, C, (unsigned)(2 * C / CALIBRATION_POSITION_SLOTS + 2));
      { unsigned below = 0; for (unsigned a = 0; a < CALIBRATION_POSITION_EEPROM_ADDRESS; a++) below += g_cell[a]; CHECK(below == 0, "%u writes landed below the ring (the calibration image must not be touched)", below); }
   }
   {
      /* f. the layout of docs/protocol/EepromLayout.md, byte for byte, against an independent CRC-8 and little-endian packing written here, not taken from the code under test */
      const unsigned W = (CORELR_MATH == CORELR_MATH_64) ? 4u : 3u, SLOT = 3u + 4u * W, BASE = CALIBRATION_POSITION_EEPROM_ADDRESS;
      const int32_t v[4] = { -5, 0x12345, (CORELR_MATH == CORELR_MATH_64) ? -2147483647L - 1L : -8388608L, (CORELR_MATH == CORELR_MATH_64) ? 2147483647L : 8388607L };
      uint8_t want[19]; unsigned n = 0, f, b;
      const uint8_t seq = 0, flags = 0x80u | 0x55u;
      #define CRC8_STEP(c, byte) do { unsigned k_; (c) ^= (byte); for (k_ = 0; k_ < 8; k_++) (c) = (uint8_t)(((c) & 0x80u) ? (((c) << 1) ^ 0x07u) : ((c) << 1)); } while (0)
      uint8_t crc = 0;
      memset(g_ee, 0xFF, sizeof g_ee); reboot(); configure(); store_state(v[0], v[1], v[2], v[3]);
      want[n++] = seq; want[n++] = flags;
      for (f = 0; f < 4; f++) for (b = 0; b < W; b++) want[n++] = (uint8_t)((uint32_t)v[f] >> (8 * b));
      for (b = 0; b < n; b++) CRC8_STEP(crc, want[b]);
      want[n++] = crc;
      CHECK(n == SLOT, "the test's own slot is %u bytes, the layout says %u", n, SLOT);
      CHECK(memcmp(&g_ee[BASE], want, n) == 0, "the first slot is not what docs/protocol/EepromLayout.md describes (Seq 0, Flags 0xD5, the four fields little endian, CRC-8)");
      { char hex[80]; hex[0] = 0; for (b = 0; b < n; b++) sprintf(hex + strlen(hex), "%02X ", g_ee[BASE + b]); printf("  slot 0 after a store: %s\n", hex); }
      /* an invalidation: slot 1, Seq 1, Flags 0x55, the CRC of those two bytes alone, the payload cells untouched (still erased) */
      Calibration_InvalidatePosition();
      { uint8_t c2 = 0; CRC8_STEP(c2, 1u); CRC8_STEP(c2, 0x55u);
        CHECK(g_ee[BASE + SLOT] == 1u && g_ee[BASE + SLOT + 1u] == 0x55u && g_ee[BASE + SLOT + SLOT - 1u] == c2, "the invalidation record is %02X %02X ... CRC %02X, expected 01 55 ... CRC %02X", g_ee[BASE + SLOT], g_ee[BASE + SLOT + 1u], g_ee[BASE + SLOT + SLOT - 1u], c2);
        { int clean = 1; for (b = 2; b < SLOT - 1u; b++) if (g_ee[BASE + SLOT + b] != 0xFFu) clean = 0; CHECK(clean, "an invalidation wrote into the slot's payload (wear, and stale bytes)"); } }
      /* a slot whose magic is wrong, with a correct CRC, is no record; the same slot with the right magic is one */
      for (int right = 0; right < 2; right++)
      {
         uint8_t slot[19]; uint8_t c3 = 0; unsigned m = 0;
         memset(g_ee, 0xFF, sizeof g_ee); reboot(); configure();       /* a valid calibration image, an empty ring */
         slot[m++] = 9; slot[m++] = (uint8_t)(0x80u | (right ? 0x55u : 0x54u));
         for (f = 0; f < 4; f++) for (b = 0; b < W; b++) slot[m++] = (uint8_t)((uint32_t)v[f] >> (8 * b));
         for (b = 0; b < m; b++) CRC8_STEP(c3, slot[b]);
         slot[m++] = c3; memcpy(&g_ee[BASE], slot, m); reboot();
         CHECK(Calibration_IsCalibrated() == right, "a slot with %s magic and a correct CRC: calibrated %d, expected %d", right ? "the right" : "a WRONG", Calibration_IsCalibrated(), right);
      }
      #undef CRC8_STEP
   }
   {
      /* e. what garbage in the ring can pass for: random bytes, a 7-bit magic and a CRC-8 let a slot through one time in 32768, a valid one half of those; with 64 slots, ~0.1 % of the garbage EEPROMs. */
      unsigned trials = 20000, accepted = 0; srand(7);
      for (unsigned t = 0; t < trials; t++)
      {
         for (unsigned a = CALIBRATION_POSITION_EEPROM_ADDRESS; a < 1024; a++) g_ee[a] = (uint8_t)(rand() >> 7);
         reboot();
         if (Calibration_IsCalibrated()) accepted++;
      }
      printf("  random garbage in the ring: %u of %u (%.1f %%) came back calibrated\n", accepted, trials, 100.0 * accepted / trials);
      CHECK(accepted < trials / 100, "%u of %u random rings passed for calibrated (expected ~0.1 %%: 64 slots, a 7-bit magic and a CRC-8)", accepted, trials);
   }
#endif

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
