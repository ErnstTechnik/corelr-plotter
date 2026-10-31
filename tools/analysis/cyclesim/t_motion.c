/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cycle measurement of the REAL Motion.c (the v2 event generator) on the AVR simulator.
 * Stepper and Calibration are stubs; each entry Motion sends is a mark (1) and its coil bits and hold are
 * logged (GPIOR0), so the host script can recompute the duration of the move and the CPU load it implies.
 * The scenario is chosen at compile time: -DDX=.. -DDY=.. (um). */
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
#ifndef DX
#define DX 30000L
#define DY 0L
#endif
int main(void)
{
   Motion_Position_t o = { 0, 0 }, t = { DX, DY };
   Motion_Init(); Motion_SetCurrentPosition(o);
   (void)Motion_QueueMove(t);
   MARK(2);
   while (!Motion_Task()) { }
   MARK(3);
   GPIOR1 = 1; for (;;) { }
}
