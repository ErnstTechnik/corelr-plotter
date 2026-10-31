/*
 * gcode_probe.c
 * Part of the CoreLR wall-plotter firmware project.
 *
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* gcode_probe -- what does the REAL parser answer to one GCode line? Reads lines from standard input and prints one answer per line:
 *     ok            the line was accepted and executed
 *     end           accepted, and it was a program end (M2 / M30)
 *     error:N       refused, with grbl's status number
 * Every line is run from a fresh state (the head at the drawing origin, absolute mode, pen up), so the answers do not depend on each other. The parser, Motion and Trig are the
 * firmware's own, compiled for the variant that -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_<SD|SERIAL> says; Stepper, Calibration and PenControl are the same stubs as in gcode_check.c
 * (motors 1000 mm apart, the origin 700 mm of cable from each motor), except that an origin command is accepted, as the real Calibration does inside the travel zone.
 * Built and driven by tests/tools/test_gcode_page.py, which checks docs/doxygen/gcode.md against these answers (REQ-CoreLR-D-Build-0018).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"

static int32_t g_D = 1000000L, g_OX = 500000L, g_OY = 489898L;
static int32_t g_OX0 = 500000L, g_OY0 = 489898L;   /* where calibration left the origin: the machine frame hangs on it, G92 does not move it */
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = g_D; p.WorkOriginOffsetX = g_OX; p.WorkOriginOffsetY = g_OY; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = g_OX; *y = g_OY; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { g_OX = x; g_OY = y; return 1; }
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = g_OX0; *y = g_OY0; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
void PenControl_RequestUp(void) { }
void PenControl_RequestDown(void) { }

int main(void)
{
   char line[256];
   Motion_Position_t origin = { 0, 0 };

   while (fgets(line, sizeof line, stdin))
   {
      size_t n = strlen(line);
      GCodeParser_Result_t r;
      long tries = 0;

      while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
      {
         line[--n] = 0;
      }
      if (n == 0)
      {
         continue;
      }
      g_OX = g_OX0;
      g_OY = g_OY0;
      Motion_Init();
      Motion_SetCurrentPosition(origin);
      GCodeParser_Init();
      while ((r = GCodeParser_ExecuteLine(line)) == GCODEPARSER_BUSY && tries++ < 10000000L)
      {
         (void)Motion_Task();
      }
      if (r == GCODEPARSER_BUSY)
      {
         printf("busy\n");
      }
      else if (r == GCODEPARSER_END)
      {
         printf("end\n");
      }
      else if (GCODEPARSER_IS_ERROR(r))
      {
         printf("error:%u\n", (unsigned)r);
      }
      else
      {
         printf("ok\n");
      }
   }
   return 0;
}
