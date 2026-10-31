/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "Wide.h"

static uint32_t ISqrt32(uint32_t value)
{
   uint32_t remainder = value;
   uint32_t root = 0;
   uint32_t bit = (uint32_t)1 << 30;

   while (bit > remainder) { bit >>= 2; }
   while (bit != 0)
   {
      if (remainder >= root + bit) { remainder -= root + bit; root = (root >> 1) + bit; }
      else { root >>= 1; }
      bit >>= 2;
   }
   return root;
}

__attribute__((noinline))
uint32_t Wide_Mul(uint32_t a, uint32_t b, uint32_t *hi)
{
   uint16_t al = (uint16_t)a, ah = (uint16_t)(a >> 16), bl = (uint16_t)b, bh = (uint16_t)(b >> 16);
   uint32_t ll = (uint32_t)al * bl, lh = (uint32_t)al * bh, hl = (uint32_t)ah * bl, hh = (uint32_t)ah * bh;
   uint32_t mid = (ll >> 16) + (uint16_t)lh + (uint16_t)hl;      /* < 3 * 2^16 */

   *hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
   return (mid << 16) | (uint16_t)ll;
}

__attribute__((noinline))
uint32_t Wide_MulHi(uint32_t a, uint32_t b)
{
   uint16_t al = (uint16_t)a, ah = (uint16_t)(a >> 16), bl = (uint16_t)b, bh = (uint16_t)(b >> 16);
   uint32_t ll = (uint32_t)al * bl, lh = (uint32_t)al * bh, hl = (uint32_t)ah * bl, hh = (uint32_t)ah * bh;
   uint32_t mid = (ll >> 16) + (uint16_t)lh + (uint16_t)hl;

   return hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

__attribute__((noinline))
uint32_t Wide_Sqrt(uint32_t hi, uint32_t lo)
{
   uint32_t r0, r1, e;
   int32_t e1;

   if (hi == 0u)
   {
      return ISqrt32(lo);
   }
   /* The root to 8 bits short, from the value's top 29 bits: r0 <= sqrt(v) < r0 + 256. */
   r0 = ISqrt32((hi << 16) | (lo >> 16)) << 8;
   /* The residual v - r0^2 is below 512 * r0 + 2^16 < 2^32 for v < 2^45, so it is known exactly from the LOW words alone. */
   e = lo - r0 * r0;
   r1 = r0 + e / (r0 << 1);
   e1 = (int32_t)(lo - r1 * r1);
   while (e1 < 0) { r1--; e1 += (int32_t)(r1 << 1) + 1; }
   while (e1 > (int32_t)(r1 << 1)) { e1 -= (int32_t)(r1 << 1) + 1; r1++; }
   return r1;
}

__attribute__((noinline))
uint32_t Wide_Hypot(int32_t x, int32_t y)
{
   uint32_t ax = (uint32_t)((x < 0) ? -x : x), ay = (uint32_t)((y < 0) ? -y : y);
   uint32_t h1, h2, l1, l2;

   l1 = Wide_Mul(ax, ax, &h1);
   l2 = Wide_Mul(ay, ay, &h2);
   l1 += l2;
   h1 += h2 + (l1 < l2);
   return Wide_Sqrt(h1, l1);
}

__attribute__((noinline))
uint32_t Wide_DivQ32(uint32_t b, uint32_t a)
{
   uint32_t q = 0, r = b;
   uint8_t i;

   for (i = 0; i < 32u; i++)
   {
      uint8_t carry = (uint8_t)(r >> 31);

      r <<= 1;
      q <<= 1;
      if (carry || r >= a) { r -= a; q |= 1u; }
   }
   return q;
}
