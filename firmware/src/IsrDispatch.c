/*
 * IsrDispatch.c
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
 * @file IsrDispatch.c
 * @brief See IsrDispatch.h.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "IsrDispatch.h"
#include "SwConfig.h"
#include "PenControl.h"
#include "SysTick.h"

/**
 * This module assumes the ATmega328(P) Timer2 register set (Arduino
 * Uno/Nano). Fail the build outright on any other target rather than
 * silently programming registers that mean something else -- Mega,
 * Leonardo, Due and ESP32 differ in register layout or have no AVR-style
 * timers at all.
 */
#if !(defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__))
#error "IsrDispatch.c assumes the ATmega328(P) Timer2 register set (Arduino Uno/Nano)"
#endif

/**
 * Timer2 CTC configuration, derived from ISRDISPATCH_FREQ_HZ by macro
 * rather than written by hand (@req{REQ-CoreLR-D-IsrDispatch-0003}).
 *
 * Arduino Uno and Nano both clock their ATmega328P from a 16MHz
 * crystal -- a board hardware fact, not a software choice -- so F_CPU
 * is 16000000 for both. Guarded explicitly rather than assumed.
 *
 * At the 1kHz in use: prescaler 64, OCR2A 249, exact with no rounding:
 *   F_CPU / (prescaler * (OCR2A + 1)) = 16000000 / (64 * 250) = 1000 Hz
 * One count is 4 us, which is also the resolution of the pen servo's pulse
 * (@req{REQ-CoreLR-D-PenControl-0003}).
 */
#if F_CPU != 16000000UL
#error "Timer2 configuration below assumes F_CPU == 16MHz (Arduino Uno/Nano) -- recompute prescaler/OCR2A for a different clock"
#endif
#define TIMER2_PRESCALER 64u
/* Note: the #if tests below are evaluated by the preprocessor, which
 * knows nothing of C casts or `u` suffix semantics beyond integer
 * arithmetic -- plain integer expressions only. */
#if (F_CPU % (TIMER2_PRESCALER * ISRDISPATCH_FREQ_HZ)) != 0
#error "TIMER2_PRESCALER/ISRDISPATCH_FREQ_HZ do not divide F_CPU exactly -- a silently rounded time base would show up as a drifting SysTick and a wrong servo frame, so pick a different prescaler"
#endif
#if ((F_CPU / (TIMER2_PRESCALER * ISRDISPATCH_FREQ_HZ)) - 1) > 255
#error "Computed OCR2A does not fit in 8 bits -- pick a larger TIMER2_PRESCALER"
#endif
/** The compare value that makes Timer2 (CTC mode) fire at ISRDISPATCH_FREQ_HZ: F_CPU / (prescaler * frequency) - 1, exact (the checks above refuse a rate that does not divide) and 8 bits. */
#define TIMER2_OCR2A ((uint8_t)((F_CPU / (TIMER2_PRESCALER * (uint32_t)ISRDISPATCH_FREQ_HZ)) - 1u))

void IsrDispatch_Init(void)
{
   /* Read-modify-write on these registers is safe here: this runs from
    * setup(), before the interrupt is armed on the last line. */
   TCCR2A = (uint8_t)(1u << WGM21);                 /* CTC mode (WGM22:20 = 010), TOP = OCR2A */
   TCCR2B = (uint8_t)(1u << CS22);                  /* prescaler /64 */
   OCR2A = TIMER2_OCR2A;
   TCNT2 = 0;
   TIMSK2 |= (uint8_t)(1u << OCIE2A);               /* arm Compare Match A -- ticks start now */
}

/** Every millisecond: the coarse time base, and the pen servo's frame. */
ISR(TIMER2_COMPA_vect)
{
   SysTick_Tick();
   PenControl_Tick();
}

/** The pen servo's pulse end -- armed only for one tick in each frame by
 *  PenControl_Tick(), so it fires once per 20 ms. */
ISR(TIMER2_COMPB_vect)
{
   PenControl_PulseEnd();
}
