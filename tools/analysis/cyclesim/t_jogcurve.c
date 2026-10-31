/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cost of one JogCurve_Displacement() call on the AVR simulator (REQ-CoreLR-D-LcdJoystickUI-0021): the real JogCurve.c and Motion_Hypot, for a few stick positions.
 *   build:  ./build.sh t_jogcurve.c t_jog_table ../../../firmware/src/JogCurve.c ../../../firmware/src/Trig.c -DJOGCURVE_USE_TABLE=1   (=0: the curve computed, not tabulated)
 *   run:    python3 run_jogcurve.py out/t_jog_table.bin */
#include <avr/io.h>
#include <stdint.h>
#include "JogCurve.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
#define LOGB(v) (GPIOR0 = (uint8_t)(v))
static const int16_t in[][2] = { { 500, 0 }, { 33, 0 }, { 200, 150 }, { -300, 100 }, { 400, 400 }, { 0, -250 }, { 20, 20 } };
int main(void)
{
   uint8_t i;
   for (i = 0; i < sizeof in / sizeof in[0]; i++)
   {
      int32_t x, y;
      JogCurve_Reset();
      MARK(2 * i + 1); JogCurve_Displacement(in[i][0], in[i][1], 30, &x, &y); MARK(2 * i + 2);
      LOGB(x); LOGB(x >> 8); LOGB(y); LOGB(y >> 8);
   }
   GPIOR1 = 1; for (;;) {}
}
