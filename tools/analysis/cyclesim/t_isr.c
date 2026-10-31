/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cost of ONE Timer2 interrupt as the firmware runs it today:
 * __vector_7 = Stepper_Tick() + SysTick_Tick(), called directly (a call+RETI
 * costs the same 4+4 cycles as the hardware's response+RETI; the 3-cycle JMP in
 * the vector table is not counted). Queue is kept full of hold=11 entries, which is
 * the steady state at VMAX; "expiry" ticks are those where an entry is consumed. */
#include <avr/io.h>
#include <stdint.h>
#include "Stepper.h"
#include "SysTick.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
extern void __vector_7(void);
int main(void)
{
   uint8_t i, k;
   Stepper_Init();
   SysTick_Init();
   for (k = 0; k < 8; k++) (void)Stepper_RequestMotion(0x0C ^ k, 0x09, 11, STEPPER_PEN_NONE);
   for (i = 0; i < 60; i++)
   {
      MARK(1); __vector_7(); MARK(2);
      if ((i % 11) == 0) (void)Stepper_RequestMotion(0x06 ^ (i & 7), 0x03, 11, STEPPER_PEN_NONE);
   }
   GPIOR1 = 1; for (;;) {}
}
