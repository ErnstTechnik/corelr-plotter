/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the software pen servo (REQ-CoreLR-D-PenControl-0003): the real PenControl.c is
 * #included and Timer2 is simulated count by count (4 us per count, 250 counts per 1 ms tick).
 * Measures the pulse the pin actually carries, for every angle, against the Servo library's
 * mapping (544..2400 us over 0..180 degrees), and the 20 ms frame.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Ishim -I../../firmware/src test_pen.c -o out/test_pen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/src/PenControl.c"

volatile uint8_t PORTB, PORTC, PORTD, DDRB, DDRC, DDRD, TCCR1A, TCCR1B, TIMSK1, TIFR1, SREG, TIMSK2, TIFR2, OCR2B;
static uint8_t g_up = 60, g_down = 95;
void Calibration_GetPenAngles(uint8_t *u, uint8_t *d) { *u = g_up; *d = g_down; }
uint8_t Stepper_RequestPenOnly(Stepper_PenAction_t a, uint32_t h) { (void)a; (void)h; return 1; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* simulate `us` microseconds of Timer2 (CTC, 250 counts per tick); return the high time of each frame */
static long hiTime[64]; static int nFrames; static long frameStart[64];
static void simulate(long ms, int latencyCounts)
{
   long t = 0; int lastPin = 0; long riseT = 0; nFrames = 0;
   uint8_t cnt = 0;                      /* TCNT2 */
   int pendTick = -1;
   for (long c = 0; c < ms * 250; c++)    /* one iteration = one 4 us count */
   {
      int pin = (PORTC >> PEN_BIT) & 1;
      if (pin && !lastPin) { riseT = t; if (nFrames < 64) frameStart[nFrames] = t; }
      if (!pin && lastPin && nFrames < 64) { hiTime[nFrames++] = t - riseT; }
      lastPin = pin;
      /* compare B: matches when the counter equals OCR2B and the interrupt is enabled */
      if ((TIMSK2 & (1u << OCIE2B)) && cnt == OCR2B && OCR2B != 0xFF && cnt > 0) { PenControl_PulseEnd(); }
      cnt++;
      if (cnt == 250) { cnt = 0; pendTick = latencyCounts; }          /* compare A match: counter restarts */
      if (pendTick == 0) { PenControl_Tick(); pendTick = -1; }          /* ISR runs after its latency */
      else if (pendTick > 0) pendTick--;
      t += 4;
   }
}

int main(void)
{
   long worstErr = 0; int bad = 0;
   PenControl_Init();
   /* every angle, through the preview slot */
   for (int angle = 0; angle <= 180; angle++)
   {
      PenControl_PreviewAngle((uint8_t)angle);
      simulate(60, 4);                       /* 4-count (16 us) interrupt latency */
      long want = 544 + (long)angle * (2400 - 544) / 180;
      if (nFrames < 2) { bad++; continue; }
      long got = hiTime[nFrames - 1];
      long err = got - want; if (err < 0) err = -err;
      /* the floor on the compare value moves a few angles by up to 48 us (documented) */
      long allowed = ((want % 1000) < 48 + 8) ? 76 : 24;   /* + the tick ISR's latency: the pin rises that late, the end is hardware-timed */
      if (err > allowed) { bad++; if (bad < 6) printf("angle %3d: pulse %ld us, want %ld (err %ld)\n", angle, got, want, err); }
      if (err > worstErr) worstErr = err;
   }
   CHECK(bad == 0, "%d angles outside tolerance", bad);
   printf("pulse width over 0..180 degrees: worst error %ld us, with a 16 us interrupt latency (the pin rises that late; the end is hardware-timed, so the pulse is short by about the latency)\n", worstErr);
   /* frame period */
   PenControl_PreviewAngle(90); simulate(100, 4);
   {
      int ok = 1; for (int i = 1; i < nFrames; i++) if (frameStart[i] - frameStart[i - 1] != 20000) ok = 0;
      CHECK(ok && nFrames >= 4, "frame period is not 20 ms");
      printf("frame period: %d frames, all 20000 us\n", nFrames);
   }
   /* the pen follows the step ISR's hand-off, through the precomputed table */
   PenControl_Init();
   PenControl_ApplyFromIsr(STEPPER_PEN_DOWN); simulate(60, 4);
   { long want = 544 + 95L * 1856 / 180; CHECK(labs(hiTime[nFrames - 1] - want) < 24 && PenControl_IsDown(), "down setpoint not applied"); }
   PenControl_ApplyFromIsr(STEPPER_PEN_UP); simulate(60, 4);
   { long want = 544 + 60L * 1856 / 180; CHECK(labs(hiTime[nFrames - 1] - want) < 24 && !PenControl_IsDown(), "up setpoint not applied"); }
   /* changing the calibrated angles reaches the pulse via the Task, with no ISR arithmetic */
   g_down = 120; PenControl_Task(); PenControl_ApplyFromIsr(STEPPER_PEN_DOWN); simulate(60, 4);
   { long want = 544 + 120L * 1856 / 180; CHECK(labs(hiTime[nFrames - 1] - want) < 24, "a changed down angle did not reach the servo, got %ld want %ld", hiTime[nFrames - 1], want); }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
