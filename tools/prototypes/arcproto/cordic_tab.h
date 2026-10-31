/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* generated: atan(2^-i) in turns * 2^32, i = 0..23; 1/K (the CORDIC gain) = 0.607252935009 */
#define CORDIC_N 24
#define CORDIC_INV_K_Q30 652032874L /* 1/K * 2^30 */
static const uint32_t kCordicAtan[CORDIC_N] = {
   536870912UL, 316933406UL, 167458907UL, 85004756UL,
   42667331UL, 21354465UL, 10679838UL, 5340245UL,
   2670163UL, 1335087UL, 667544UL, 333772UL,
   166886UL, 83443UL, 41722UL, 20861UL,
   10430UL, 5215UL, 2608UL, 1304UL,
   652UL, 326UL, 163UL, 81UL
};
