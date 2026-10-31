/*
 * SysTick.h
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
 * @file SysTick.h
 * @brief Coarse time base: an 8-bit counter incremented every
 * SYSTICK_PERIOD_MS, readable from main-loop context with no critical
 * section.
 *
 * Realizes: @req{REQ-CoreLR-F-UserInterface-0009} (bounded refresh latency)
 * Design:   @req{REQ-CoreLR-D-SysTick-0001}..@req{REQ-CoreLR-D-SysTick-0004}
 * See:      requirements/design/REQ-CoreLR-D-SysTick.md
 *
 * The counter is deliberately **8 bits**: a single-byte read is
 * inherently atomic on 8-bit AVR, so readers need no cli()/sei(). A
 * wider counter would need one on every read, and the readers here sit
 * on the busiest loop path in the firmware.
 *
 * Ticks arrive from IsrDispatch, which owns the hardware timer and its
 * vector -- this module never touches a timer register.
 */

#ifndef CORELR_SYSTICK_H
#define CORELR_SYSTICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Largest period, in ticks, that may be compared with
 * SysTick_Elapsed() (@req{REQ-CoreLR-D-SysTick-0003}).
 *
 * Half the counter range minus one. The wraparound-safe unsigned
 * subtraction used by SysTick_Elapsed() is only unambiguous below this
 * bound; every period constant shall be checked against it at compile
 * time. At SYSTICK_PERIOD_MS = 10 this is 1270 ms.
 */
#define SYSTICK_TICKS_MAX 127u

/** @brief Reset the time base. Call once from setup(), before IsrDispatch_Init(). */
void SysTick_Init(void);

/**
 * @brief Advance the time base by one ISR tick. Called only from
 * IsrDispatch's ISR (@req{REQ-CoreLR-D-IsrDispatch-0002}), never from
 * application code.
 */
void SysTick_Tick(void);

/** @return the current tick count. Atomic: a single-byte read. */
uint8_t SysTick_Get(void);

/**
 * @brief Wraparound-safe elapsed-time test
 * (@req{REQ-CoreLR-D-SysTick-0003}): has at least `periodTicks` elapsed
 * since `sinceTick`?
 *
 * Centralised here so the unsigned-subtraction idiom is written once
 * and cannot be got subtly wrong at a call site. `periodTicks` must be
 * <= SYSTICK_TICKS_MAX.
 *
 * Callers use the **skip-missed** pattern
 * (@req{REQ-CoreLR-D-SysTick-0004}) -- reassign the reference to
 * SysTick_Get(), never advance it by periodTicks:
 * @code
 *   if (SysTick_Elapsed(s_lastTick, PERIOD_TICKS))
 *   {
 *      s_lastTick = SysTick_Get();   // skip: only the latest state matters
 *      DoTheThing();
 *   }
 * @endcode
 */
uint8_t SysTick_Elapsed(uint8_t sinceTick, uint8_t periodTicks);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_SYSTICK_H */
