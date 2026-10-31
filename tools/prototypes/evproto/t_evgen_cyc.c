/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cycle measurement of the prototype event generator on the AVR simulator.
 * One 30 mm move at constant VMAX; GPIOR2 marks bracket each EvGen_Next() call:
 *   mark 1 = call start, mark 2 = call end, value 3 = the call that began a segment is not known here,
 * so the host script classifies a call by its cost (segment-start calls are the heavy ones). */
#include <avr/io.h>
#include <stdint.h>
#include "evgen.h"
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

#ifndef DX
#define DX 30000L
#endif
#ifndef DY
#define DY 0L
#endif
int main(void)
{
   Motion_Position_t o = { 0, 0 }, t = { DX, DY };
   EvGen_Entry_t e;
   Motion_CableLengths_t lr = Motion_XYToLR(o);
   int32_t cl = (int32_t)(((int64_t)lr.L * 2048 + 109955 / 2) / 109955);
   int32_t cr = (int32_t)(((int64_t)lr.R * 2048 + 109955 / 2) / 109955);
   uint8_t more;

   EvGen_Init(cl, cr, o);
   EvGen_StartMove(t, 86, 86);
   do { MARK(1); more = EvGen_Next(&e); MARK(2); } while (more);
   GPIOR1 = 1;
   for (;;) {}
}
