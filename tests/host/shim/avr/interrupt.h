/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* ISR(vec) becomes an ordinary function the test can call at a simulated interrupt. */
#define ISR(vec) void ISR_##vec(void)
#define cli() ((void)0)
#define sei() ((void)0)
