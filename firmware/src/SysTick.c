/*
 * SysTick.c
 * Part of the CoreLR wall-plotter firmware project.
 *
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file SysTick.c
 * @brief See SysTick.h.
 */

#include "SysTick.h"
#include "SwConfig.h"

/**
 * ISR ticks per SysTick increment, derived -- never written by hand
 * (@req{REQ-CoreLR-D-SysTick-0002}). The three guards below reject a period
 * that is not exactly reachable, rather than silently rounding the
 * time base.
 */
#define SYSTICK_DIVIDER ((ISRDISPATCH_FREQ_HZ * SYSTICK_PERIOD_MS) / 1000u)

#if ((ISRDISPATCH_FREQ_HZ * SYSTICK_PERIOD_MS) % 1000u) != 0
#error "SYSTICK_PERIOD_MS does not divide exactly into ISRDISPATCH_FREQ_HZ -- pick a period that does, rather than accepting a rounded time base"
#endif
#if SYSTICK_DIVIDER == 0
#error "SYSTICK_PERIOD_MS is shorter than one ISR tick -- raise it, or raise ISRDISPATCH_FREQ_HZ"
#endif
#if SYSTICK_DIVIDER > 255
#error "SYSTICK_DIVIDER does not fit uint8_t -- raise ISRDISPATCH_FREQ_HZ's period or lower SYSTICK_PERIOD_MS"
#endif

/**
 * volatile: written by SysTick_Tick() in ISR context, read by
 * SysTick_Get()/_Elapsed() from main-loop context. 8 bits, so the read
 * is atomic on AVR and needs no critical section -- that width is a
 * requirement, not an accident (@req{REQ-CoreLR-D-SysTick-0001}).
 */
static volatile uint8_t s_tick;

/** Sub-divider. Touched only by SysTick_Tick(), i.e. only in ISR
 * context, so it is neither shared nor volatile. */
static uint8_t s_subCount;

void SysTick_Init(void)
{
   s_tick = 0;
   s_subCount = 0;
}

void SysTick_Tick(void)
{
   uint8_t subCount = (uint8_t)(s_subCount + 1u);

   if (subCount >= (uint8_t)SYSTICK_DIVIDER)
   {
      subCount = 0;
      s_tick++;
   }
   s_subCount = subCount;
}

uint8_t SysTick_Get(void)
{
   return s_tick;
}

uint8_t SysTick_Elapsed(uint8_t sinceTick, uint8_t periodTicks)
{
   /* Unsigned subtraction is correct across the counter's wraparound,
    * provided periodTicks <= SYSTICK_TICKS_MAX (half the range minus
    * one) -- see REQ-CoreLR-D-SysTick-0003. */
   return (uint8_t)((uint8_t)(s_tick - sinceTick) >= periodTicks);
}
