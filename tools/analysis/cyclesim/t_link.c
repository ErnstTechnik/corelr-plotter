/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* CPU cost of ONE SerialLink_Task() pass, on the AVR simulator, for the passes that matter (request M-6 of docs/protocol/SerialProtocol.md, the
 * CPU part: it is what the longest loop() pass of the serial variant is made of, apart from the LCD and the planner's own work).
 * The real SerialLink.c, Uart.c, GCodeParser.c, Motion.c, Trig.c, TextFmt.c; Calibration, Stepper and PenControl are stubs.
 * The line is fed to the receive ring by calling the receive interrupt directly (not counted); the pass that assembles it, runs it
 * through the parser and answers it is what is bracketed. Marks (GPIOR2): 10*k+1 .. 10*k+2 around scenario k.
 *   build:  ./build.sh t_link.c t_link ../../../firmware/src/{SerialLink,Uart,GCodeParser,Trig,TextFmt}.c -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL
 *   run:    python3 run_link.py out/t_link.bin */
#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"
#include "SerialLink.h"
#include "Uart.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
extern void __vector_18(void); extern void __vector_19(void);
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* Motion_SetOrigin(): not exercised here */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void Calibration_RestoreStoredPosition(void) { }
uint8_t Calibration_IsCalibrated(void) { return 1; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
void PenControl_RequestUp(void) { }
void PenControl_RequestDown(void) { }

static void rx(uint8_t b) { UCSR0A = 0; UDR0 = b; __vector_18(); }
static void feed(const char *s) { while (*s) rx((uint8_t)*s++); }
static void drain(void) { while (UCSR0B & (1u << UDRIE0)) __vector_19(); }
static void settle(void) { uint8_t i; drain(); for (i = 0; i < 6; i++) { (void)SerialLink_Task(JOB_RUNNING, 0); drain(); } while (!Motion_Task()) { } }

/* scenario k: feed `text` (ended by a newline), then bracket the one SerialLink_Task() pass that handles it */
static void scenario(uint8_t k, const char *text, JobState_t state)
{
   feed(text); rx('\n');
   MARK(10u * k + 1u); (void)SerialLink_Task(state, 0); MARK(10u * k + 2u);
   settle();
}

int main(void)
{
   Motion_Position_t o = { 0, 0 };
   Motion_Init(); Motion_SetCurrentPosition(o); GCodeParser_Init();
   Uart_Init(); SerialLink_Init(); settle();
   { MARK(1); (void)SerialLink_Task(JOB_IDLE, 0); MARK(2); }                          /* 0: nothing to do */
   scenario(1, "G1 X10.5 Y20.25", JOB_RUNNING);                                      /* a short line */
   scenario(2, "G1 X123.456789 Y-234.567891 F1000 S0", JOB_RUNNING);                  /* a typical long one: 6 decimals */
   scenario(3, "G1X1Y0000000000000000000000000000000000000000000000000000000000000000000000000", JOB_RUNNING);   /* 80 significant characters */
   scenario(4, "G2 X12.345 Y-67.890 I-10.000 J5.500", JOB_RUNNING);                  /* an arc (ArcCheck + QueueArc) */
   scenario(5, "G1 X10 ; a comment of some length that the assembler has to read and drop (and this too)", JOB_RUNNING);
   scenario(6, "G1 Z5", JOB_RUNNING);                                                /* refused: error:20 */
   scenario(7, "$G", JOB_IDLE);
   scenario(8, "$$", JOB_IDLE);
   scenario(9, "$I", JOB_IDLE);
   rx('?'); MARK(101); (void)SerialLink_Task(JOB_RUNNING, 0); MARK(102); settle();    /* a status report */
   GPIOR1 = 1; for (;;) {}
}
