/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cost of the v2 interrupts, on the AVR simulator (the interrupts are called directly: a call+RETI
 * costs the same 4+4 cycles as the hardware's response+RETI; the 3-cycle JMP in the vector table is
 * not counted). Registers are plain memory in the simulator, so TCNT1 is set by hand before each call
 * to look like "20 counts after the previous compare", which is what a healthy ISR sees.
 *   mark 1/2 bracket Timer1 compare-A, marks 3/4 the Timer2 tick, marks 5/6 the Timer2 compare-B,
 *   with the second mark's value distinguishing the cases. */
#include <avr/io.h>
#include <stdint.h>
#include "Stepper.h"
#include "PenControl.h"
#include "SysTick.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
extern void __vector_11(void); extern void __vector_7(void); extern void __vector_8(void);
void Calibration_InvalidatePosition(void) { }
void Calibration_GetPenAngles(uint8_t *u, uint8_t *d) { *u = 60; *d = 95; }

int main(void)
{
   uint8_t i;
   Stepper_Init(); SysTick_Init(); PenControl_Init();

   /* Timer1: eight entries of 5000 counts, every third carries a pen action */
   for (i = 0; i < 8; i++) { (void)Stepper_RequestMotion((uint8_t)(0x0C ^ i), 0x09, 5000, (i % 3 == 2) ? STEPPER_PEN_DOWN : STEPPER_PEN_NONE); }
   for (i = 0; i < 8; i++)
   {
      uint8_t pen = (i % 3 == 2);
      TCNT1 = (uint16_t)(OCR1A + 20u);
      MARK(10 + pen); __vector_11(); MARK(20 + pen);          /* 10/20: no pen action, 11/21: with one */
   }
   TCNT1 = (uint16_t)(OCR1A + 20u);
   MARK(12); __vector_11(); MARK(22);                          /* empty queue: disarm */

   /* Timer2: one full 20 ms servo frame of ticks (SysTick + servo), then the compare-B ISR */
   for (i = 0; i < 20; i++) { MARK(30); __vector_7(); MARK(40); if (i == 1) { MARK(50); __vector_8(); MARK(60); } }
   GPIOR1 = 1; for (;;) {}
}
