/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
extern void model_delay_us(double us);
#define _delay_ms(x) model_delay_us((double)(x) * 1000.0)
#define _delay_us(x) model_delay_us((double)(x))
