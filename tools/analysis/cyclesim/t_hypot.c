/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cycle cost of Motion_Hypot on the AVR simulator, for both arithmetics (REQ-CoreLR-D-Motion-0024).
 * Marks (GPIOR2): 1 before a call, 2 after it. The inputs span the sizes that occur: the work area (~1 m), a long line, a short chord.
 *   ./build.sh t_hypot.c t_hypot_48 ../../../firmware/src/Trig.c -DCORELR_MATH=CORELR_MATH_48
 *   ./build.sh t_hypot.c t_hypot_64 ../../../firmware/src/Trig.c -DCORELR_MATH=CORELR_MATH_64      then  python3 run_hypot.py out/t_hypot_48.bin */
#include <avr/io.h>
#include <stdint.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
#define LOGB(v) (GPIOR0 = (uint8_t)(v))
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t hold, Stepper_PenAction_t pen) { (void)l; (void)r; (void)hold; (void)pen; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
static const int32_t in[][2] = { { 500000, 489898 }, { 700000, 300000 }, { -123456, 789012 }, { 1000000, 1000000 }, { 4194303, 4194303 }, { 30000, 40000 }, { 214, 0 }, { 5000, 12000 }, { 40, 30 }, { 32767, 1 }, { 32768, 1 }, { 3000000, 17 } };
int main(void)
{
   for (uint8_t i = 0; i < sizeof in / sizeof in[0]; i++)
   {
      volatile uint32_t r;
      MARK(1); r = Motion_Hypot(in[i][0], in[i][1]); MARK(2);
      LOGB(r); LOGB(r >> 8); LOGB(r >> 16); LOGB(r >> 24);
   }
   GPIOR1 = 1; for (;;) { }
}
