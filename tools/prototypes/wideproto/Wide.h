/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Wide: arithmetic wider than 32 bits WITHOUT a 64-bit type (prototype, NOT adopted -- see README.md). Values travel as pairs of 32-bit words. */
#ifndef CORELR_WIDE_H
#define CORELR_WIDE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** a * b as two words: returns the low word, *hi the high word. */
uint32_t Wide_Mul(uint32_t a, uint32_t b, uint32_t *hi);
/** The high word of a * b (a Q32 product). */
uint32_t Wide_MulHi(uint32_t a, uint32_t b);
/** floor(sqrt(hi * 2^32 + lo)), for a value below 2^45 (hi < 2^13). */
uint32_t Wide_Sqrt(uint32_t hi, uint32_t lo);
/** floor(sqrt(x^2 + y^2)), exact, for |x|, |y| < 2^22. */
uint32_t Wide_Hypot(int32_t x, int32_t y);
/** floor(b * 2^32 / a), for b < a. */
uint32_t Wide_DivQ32(uint32_t b, uint32_t a);
#ifdef __cplusplus
}
#endif
#endif
