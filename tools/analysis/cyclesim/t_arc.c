/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cycle measurement of arcs in the REAL Motion.c + Trig.c on the AVR simulator (REQ-CoreLR-D-Motion-0022).
 * Marks (GPIOR2): 4/5 around Motion_ArcCheck, 6/7 around Motion_QueueArc, 2 start of generation, 3 end.
 * Scenario at compile time: -DRADIUS=<um> (a full circle if -DFULL, else a quarter turn G3 from (RADIUS,0)),
 * or -DLINE=<um> for a straight move of that length along X, for comparison.
 *   ./build.sh t_arc.c t_arc ../../../firmware/src/Trig.c -DRADIUS=20000L                              */
#include <avr/io.h>
#include <stdint.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#define LOGB(v) (GPIOR0 = (uint8_t)(v))
#define MARK(v) (GPIOR2 = (uint8_t)(v))
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t hold, Stepper_PenAction_t pen)
{ (void)pen; MARK(1); LOGB(l); LOGB(r); LOGB(hold); LOGB(hold >> 8); LOGB(hold >> 16); return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
int main(void)
{
   Motion_Position_t o = { 0, 0 };
   Motion_Init(); Motion_SetCurrentPosition(o);
#ifdef LINE
   { Motion_Position_t t = { LINE, 0 }; (void)Motion_QueueMove(t); }
#else
   {
      Motion_Position_t s = { RADIUS, 0 }, t;
      Motion_SetCurrentPosition(s);
#ifdef FULL
      t = s;
#else
      t.X = 0; t.Y = RADIUS;
#endif
      MARK(4); { volatile uint8_t ok = Motion_ArcCheck(&t, -RADIUS, 0, 0); (void)ok; } MARK(5);
      MARK(6); (void)Motion_QueueArc(&t, -RADIUS, 0, 0); MARK(7);
   }
#endif
   MARK(2);
   while (!Motion_Task()) { }
   MARK(3);
   GPIOR1 = 1; for (;;) { }
}
