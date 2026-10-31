/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <avr/io.h>
#include <stdint.h>
#include "cordic.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
int main(void)
{
   uint8_t i; int32_t x, y; uint32_t len;
   volatile uint32_t r = 500000UL;                 /* a 0.5 m radius */
   for (i = 0; i < 12; i++)
   {
      uint32_t a = 0x1234567UL * (uint32_t)(i + 1) + 0x20000000UL * (uint32_t)i;
      MARK(1); Cordic_Rotate(r, a, &x, &y); MARK(2);
      MARK(3); (void)Cordic_Vector(x, y, &len); MARK(4);
   }
   GPIOR1 = 1; for (;;) {}
}
