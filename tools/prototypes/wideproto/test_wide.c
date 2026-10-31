/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Wide prototype against native 64-bit arithmetic: gcc -O1 -Wall test_wide.c Wide.c -lm -o test_wide */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "Wide.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 15) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static uint64_t rnd64(void) { return ((uint64_t)rand() << 42) ^ ((uint64_t)rand() << 21) ^ (uint64_t)rand(); }
static uint32_t ref_isqrt(uint64_t v) { uint64_t r = (uint64_t)sqrtl((long double)v); while (r * r > v) r--; while ((r + 1) * (r + 1) <= v) r++; return (uint32_t)r; }
int main(int argc, char **argv)
{
   if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
   {
      printf("usage: test_wide [-h|--help]\n\nChecks the Wide prototype (NOT adopted, see README.md) against native 64-bit arithmetic:\n"
             "20 M products, 20 M roots below 2^45, 10 M hypots below 2^22, 5 M Q32 divisions. Takes ~5 s. Exit 0 = all exact.\n");
      return 0;
   }
   srand(1);
   long n = 0;
   for (long i = 0; i < 20000000; i++)
   {
      uint32_t a = (uint32_t)rnd64(), b = (uint32_t)rnd64(); if (i < 64) { a = (i & 1) ? 0xFFFFFFFFu : (i >> 1); b = (i & 2) ? 0xFFFFFFFFu : (i >> 2); }
      uint64_t p = (uint64_t)a * b; uint32_t hi, lo = Wide_Mul(a, b, &hi);
      CHECK(lo == (uint32_t)p && hi == (uint32_t)(p >> 32), "Mul %u %u", a, b);
      CHECK(Wide_MulHi(a, b) == (uint32_t)(p >> 32), "MulHi %u %u", a, b); n++;
   }
   printf("Wide_Mul / Wide_MulHi: %ld random + edge cases, exact\n", n);
   long ns = 0;
   for (long i = 0; i < 20000000; i++)
   {
      int bits = 1 + rand() % 45; uint64_t v = rnd64() & ((1ull << bits) - 1); if (v >= (1ull << 45)) v &= (1ull << 45) - 1;
      CHECK(Wide_Sqrt((uint32_t)(v >> 32), (uint32_t)v) == ref_isqrt(v), "Sqrt(%llu)", (unsigned long long)v); ns++;
   }
   for (uint64_t r = 0; r < (1u << 22) + 2000; r += (r < 70000 ? 1 : 37))
      for (int d = -1; d <= 1; d++) { uint64_t v = r * r + d; if (v >= (1ull << 45)) continue; CHECK(Wide_Sqrt((uint32_t)(v >> 32), (uint32_t)v) == ref_isqrt(v), "square edge %llu", (unsigned long long)v); ns++; }
   printf("Wide_Sqrt: %ld values below 2^45, exact\n", ns);
   long nh = 0; const int32_t lim = (1 << 22) - 1;
   for (long i = 0; i < 10000000; i++)
   {
      int32_t x = (int32_t)(rnd64() % (2 * (uint64_t)lim + 1)) - lim, y = (int32_t)(rnd64() % (2 * (uint64_t)lim + 1)) - lim;
      if (i % 7 == 0) x /= (1 + rand() % 4096);
      if (i % 11 == 0) y = 0;
      uint64_t v = (uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y);
      CHECK(Wide_Hypot(x, y) == ref_isqrt(v), "Hypot(%d,%d)", x, y); nh++;
   }
   printf("Wide_Hypot: %ld pairs, exact\n", nh);
   long nd = 0;
   for (long i = 0; i < 5000000; i++)
   {
      uint32_t a = (uint32_t)rnd64() | 1u, b = (uint32_t)rnd64(); if (b >= a) b = a - 1; if (a == 1) continue;
      CHECK(Wide_DivQ32(b, a) == (uint32_t)((((uint64_t)b) << 32) / a), "Div %u/%u", b, a); nd++;
   }
   printf("Wide_DivQ32: %ld divisions, identical to the 64-bit quotient\n", nd);
   printf(fails ? "\n%d FAILURES\n" : "\nall checks passed\n", fails);
   return fails != 0;
}
