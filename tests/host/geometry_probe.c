/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* geometry_probe -- what the firmware's own geometry says, for a machine of given dimensions. Driven by tools/generators/gen_geometry_svg.py --verify.
 *
 * The real Calibration.c and Motion.c: Calibration_SetParams(D, Lref) and Calibration_MarkCalibrated() give the reference position and the drawing-origin
 * offset; then every drawing-frame point read on stdin ("x_um y_um") is turned into cable lengths by the real Motion_XYToLR().
 *   gcc -m32 -fpack-struct=1 -O1 -w -Ishim -I../../firmware/src geometry_probe.c ../../firmware/src/{Calibration,Motion,Trig}.c -lm
 *   echo "0 0" | ./geometry_probe 1000 700        (D and Lref in mm)
 * Output: "motors D=<um>", "ref X=<um> Y=<um>" (internal frame, origin at motor L, Y down), "offset X=<um> Y=<um>", then "pt <x> <y> L=<um> R=<um>" per point. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/eeprom.h>
#include "Calibration.h"
#include "Motion.h"
#include "Stepper.h"

static uint8_t g_ee[1024];
uint8_t eeprom_read_byte(const uint8_t *a) { return g_ee[(size_t)a]; }
void eeprom_read_block(void *dst, const void *src, size_t n) { memcpy(dst, &g_ee[(size_t)src], n); }
void eeprom_update_byte(uint8_t *a, uint8_t v) { g_ee[(size_t)a] = v; }
void eeprom_update_block(const void *s, void *d, size_t n) { memcpy(&g_ee[(size_t)d], s, n); }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

int main(int argc, char **argv)
{
   Calibration_Params_t p; int32_t ox, oy; long x, y;
   if (argc != 3) { fprintf(stderr, "usage: geometry_probe D_mm Lref_mm  (points \"x_um y_um\" on stdin)\n"); return 2; }
   memset(g_ee, 0xFF, sizeof g_ee);
   Calibration_Init(); Motion_Init();
   Calibration_GetParams(&p);
   p.MotorDistanceUm = (int32_t)(atof(argv[1]) * 1000.0 + 0.5); p.ReferenceCableLengthUm = (int32_t)(atof(argv[2]) * 1000.0 + 0.5);
   Calibration_SetParams(&p); Calibration_MarkCalibrated();
   Calibration_GetParams(&p); Calibration_GetWorkOrigin(&ox, &oy);
   printf("motors D=%ld\nref X=%ld Y=%ld\noffset X=%ld Y=%ld\n", (long)p.MotorDistanceUm, (long)p.ReferencePositionX, (long)p.ReferencePositionY, (long)ox, (long)oy);
   while (scanf("%ld %ld", &x, &y) == 2)
   {
      Motion_Position_t q; Motion_CableLengths_t c; q.X = x; q.Y = y; c = Motion_XYToLR(q);
      printf("pt %ld %ld L=%ld R=%ld\n", x, y, (long)c.L, (long)c.R);
   }
   return 0;
}
