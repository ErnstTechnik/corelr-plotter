/*
 * IsrDispatch.h
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
 * @file IsrDispatch.h
 * @brief Sole owner of **Timer2** and its interrupt vectors. Dispatches
 * each 1 ms tick to the modules that need one.
 *
 * Realizes: @req{REQ-CoreLR-F-UserInterface-0009} (time base for the UI),
 *           @req{REQ-CoreLR-F-PenControl-0001} (the pen servo's frame)
 * Design:   @req{REQ-CoreLR-D-IsrDispatch-0001}..@req{REQ-CoreLR-D-IsrDispatch-0003}
 * See:      requirements/design/REQ-CoreLR-D-IsrDispatch.md
 *
 * One owner per timer (@req{REQ-CoreLR-D-IsrDispatch-0001}, amended 2026-10-06):
 * this module owns Timer2; Stepper owns Timer1, which is compare-driven and
 * has no periodic tick to dispatch. Before the v2 architecture this module
 * ran the motors' 4 kHz tick, and Stepper was an ordinary tick consumer.
 *
 *   ISR(TIMER2_COMPA_vect)   every 1 ms
 *      |
 *      +--> SysTick_Tick()      time base: 10 ms counter
 *      |
 *      +--> PenControl_Tick()   the servo's 20 ms frame
 *
 *   ISR(TIMER2_COMPB_vect)   armed once per frame, by PenControl_Tick()
 *      |
 *      +--> PenControl_PulseEnd()
 *
 * The ISRs hold no state and no logic of their own, so their worst-case
 * duration is exactly the sum of their consumers' -- which is what keeps
 * the budget auditable.
 */

#ifndef CORELR_ISRDISPATCH_H
#define CORELR_ISRDISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure Timer2 at ISRDISPATCH_FREQ_HZ and enable
 * its interrupt. Call once from setup(), **after** every tick
 * consumer's own _Init() -- once this returns, ticks start arriving.
 */
void IsrDispatch_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_ISRDISPATCH_H */
