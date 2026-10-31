/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* The bench scenario of 2026-10-10 on the REAL Motion.c compiled for the ATmega328P: six chained +-10 mm moves fed through the planner,
 * a pause (Motion_Halt, gate closed at the end of the ramp as grbl_corelr.ino does), a resume, then the rest of the job. The stepper queue is
 * a 31-slot model drained one entry per 40 calls. The result goes out through GPIOR0 (log), as t_motion.c does:
 *   byte 0: halted (1) / resumed (1) flags as bits 0 and 1;   bytes 1-2: entries produced after the resume (16 bit);
 *   bytes 3-6: the final X (um, 32 bit);                      byte 7: 1 if Motion ended idle with an empty planner.
 * Build:  ./build.sh t_halt.c t_halt ../../../firmware/src/Trig.c && python3 run_halt.py out/t_halt.bin
 * The same source built for the host (out/t_halt_host) must print the same bytes: the difference between the two is what int width does. */
#include <avr/io.h>
#include <stdint.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#define LOGB(v) (GPIOR0 = (uint8_t)(v))
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }
static uint8_t g_qcount, g_gate;
static uint16_t g_entries;
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t hold, Stepper_PenAction_t pen)
{ (void)l; (void)r; (void)hold; (void)pen; if (g_gate || g_qcount >= 31) { return 0; } g_qcount++; g_entries++; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
int main(void)
{
   Motion_Position_t o = { 0, 0 };
   uint8_t fed = 0, halted = 0, resumed = 0, done = 0;
   uint32_t iter; uint16_t still = 0, atResume = 0;
   Motion_Init(); Motion_SetCurrentPosition(o);
   for (iter = 0; iter < 400000UL && !done; iter++)
   {
      if (fed < 6 && Motion_PlannerHasRoom())
      {
         Motion_Position_t t = o; t.X = (fed % 2 == 0) ? 10000L : 0L;
         if (Motion_QueueMove(t)) { fed++; }
      }
      (void)Motion_Task();
      if ((iter % 40u) == 0u && g_qcount > 0u) { g_qcount--; }
      if (!halted && g_entries > 300u) { Motion_Halt(); halted = 1; }
      if (halted && !resumed && Motion_IsHaltRampDone())
      {
         if (!g_gate) { g_gate = 1; }
         else if (still++ > 500u) { g_gate = 0; Motion_Resume(); resumed = 1; atResume = g_entries; }
      }
      if (resumed && fed == 6 && Motion_IsIdle() && Motion_PlannerIsEmpty()) { done = 1; }
   }
   {
      int32_t x = Motion_GetComputedPosition().X; uint16_t after = (uint16_t)(g_entries - atResume);
      LOGB((halted ? 1 : 0) | (resumed ? 2 : 0)); LOGB(after); LOGB(after >> 8);
      LOGB(x); LOGB(x >> 8); LOGB(x >> 16); LOGB(x >> 24); LOGB(done);
   }
   GPIOR1 = 1; for (;;) { }
}
