/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <avr/io.h>
#include <stdint.h>
#define LOGB(v) (GPIOR0 = (uint8_t)(v))
#define MARK(v) (GPIOR2 = (uint8_t)(v))
extern uint32_t Motion_ISqrt(uint64_t);

/* B: 32-bit bit-by-bit, for inputs known to fit 32 bits */
static uint32_t isqrt32(uint32_t value)
{
   uint32_t rem = value, root = 0, bit = 1UL << 30;
   while (bit > rem) bit >>= 2;
   while (bit != 0) {
      if (rem >= root + bit) { rem -= root + bit; root = (root >> 1) + bit; }
      else root >>= 1;
      bit >>= 2;
   }
   return root;
}
/* C: 64-bit input, result known < 2^22, so start at 4^21 */
static uint32_t isqrt64b(uint64_t value)
{
   uint64_t rem = value, root = 0, bit = (uint64_t)1 << 42;
   while (bit > rem) bit >>= 2;
   while (bit != 0) {
      if (rem >= root + bit) { rem -= root + bit; root = (root >> 1) + bit; }
      else root >>= 1;
      bit >>= 2;
   }
   return (uint32_t)root;
}
/* D: Newton, seeded with the previous root */
static uint32_t isqrt_newton(uint64_t n, uint32_t seed)
{
   uint32_t x = seed ? seed : 1, y;
   y = (uint32_t)(((uint64_t)x + n / x) >> 1);
   do { x = y; y = (uint32_t)(((uint64_t)x + n / x) >> 1); } while (y < x);
   return x;
}
static uint64_t xs = 88172645463325252ULL;
static uint64_t rnd(void){ xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17; return xs; }

int main(void)
{
   uint8_t i;
   /* walk a "path": L changes by <= 6 um per step, like consecutive waypoints */
   uint32_t L = 700000UL;
   uint32_t prev = L;
   for (i = 0; i < 16; i++) {
      volatile uint64_t n;
      uint32_t r, got;
      L += (uint32_t)(rnd() % 13) - 6;
      n = (uint64_t)L * L + (rnd() % (2 * (uint64_t)L));   /* a non-square just above L^2 */
      MARK(10); r = Motion_ISqrt(n);          MARK(11);
      MARK(20); got = isqrt64b(n);            MARK(21); LOGB(got == r);
      MARK(30); got = isqrt_newton(n, prev);  MARK(31); LOGB(got == r);
      prev = r;
      { volatile uint32_t m = (uint32_t)(rnd() % 6000000UL);   /* v^2-sized argument */
        uint32_t a, b;
        MARK(40); a = Motion_ISqrt(m);        MARK(41);
        MARK(50); b = isqrt32(m);             MARK(51); LOGB(a == b); }
   }
   GPIOR1 = 1; for(;;);
}
