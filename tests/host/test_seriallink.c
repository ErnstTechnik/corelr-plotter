/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the serial link, whole stack but the hardware: the real Uart.c (its interrupts called by the test), SerialLink.c,
 * GCodeParser.c, Motion.c and Trig.c, TextFmt.c. Stepper, PenControl and Calibration are stubs. The sketch's job state machine
 * is modelled in apply()/step() below -- exactly the table grbl_corelr.ino implements for each request (REQ-CoreLR-D-SerialLink-0006).
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL -Ishim -I../../firmware/src \
 *       test_seriallink.c ../../firmware/src/{SerialLink,Uart,GCodeParser,Motion,Trig,TextFmt}.c -lm -o out/test_seriallink
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <avr/io.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"
#include "SerialLink.h"
#include "Uart.h"
#include "Version.h"

volatile uint8_t UCSR0A, UCSR0B, UCSR0C, UDR0, SREG;
volatile uint16_t UBRR0;
void ISR_USART_RX_vect(void); void ISR_USART_UDRE_vect(void);

static int g_calibrated = 1;
uint8_t Calibration_IsCalibrated(void) { return (uint8_t)g_calibrated; }
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = 1000000L; p.WorkOriginOffsetX = 500000L; p.WorkOriginOffsetY = 489898L; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = 500000L; *y = 489898L; }
static int g_originOk = 1;   /* the real Calibration refuses an origin outside the zone; here the zone is not modelled */
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return (uint8_t)g_originOk; }
static int32_t g_refX = 500000L, g_refY = 489898L;   /* the reference point is the drawing origin: WCO = 0, unless a test moves it (section 15) */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = g_refX; *y = g_refY; }
static int g_stepperCap = -1, g_stepperCount;   /* >= 0: the step queue takes that many entries, then refuses (a move stays "in flight") */
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p)
{ (void)l; (void)r; (void)h; (void)p; if (g_stepperCap >= 0) { if (g_stepperCount >= g_stepperCap) return 0; g_stepperCount++; } return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
static int g_penUp, g_penDown;
void PenControl_RequestUp(void) { g_penUp++; }
void PenControl_RequestDown(void) { g_penDown++; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 30) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- the world around the link ---- */
static JobState_t st = JOB_IDLE;
static uint8_t g_lcdBusy;
static int g_moveMotion = 1;          /* 0: Motion_Task is not run, so the planner fills and stays full */
static int g_txPerPass = 1000;        /* bytes the "UART" sends per loop pass: the baud rate, as seen by the loop */
static int g_rampPasses;              /* passes a ramped stop (HALTING, CANCELLING, ERRORSTOP) lasts; 0: it ends on the next pass */
static int rampLeft;
static char out[8192]; static int outN;
static int nreq[8];
static int lastReq;
static int jogCancelling;

static void txPump(void)
{
   int n = g_txPerPass;
   while (n-- > 0 && (UCSR0B & (1u << UDRIE0))) { ISR_USART_UDRE_vect(); if (outN < (int)sizeof out - 1) out[outN++] = (char)UDR0; }
   out[outN] = 0;
}
static void rx(uint8_t b) { UCSR0A = 0; UDR0 = b; ISR_USART_RX_vect(); }
static void feed(const char *s) { while (*s) rx((uint8_t)*s++); }
static void feedN(const char *s, int n) { while (n--) rx((uint8_t)*s++); }

/* what grbl_corelr.ino does for each request (the table of REQ-CoreLR-D-SerialLink-0006) */
static void apply(SerialLink_Request_t r)
{
   nreq[r]++; if (r) lastReq = r;
   if (r != SERIALLINK_REQ_NONE) rampLeft = g_rampPasses;
   switch (r)
   {
      case SERIALLINK_REQ_START:     if (st == JOB_IDLE) st = JOB_RUNNING; break;
      case SERIALLINK_REQ_HALT:      if (st == JOB_RUNNING) st = JOB_HALTING; break;
      case SERIALLINK_REQ_RESUME:    if (st == JOB_PAUSED) st = JOB_RUNNING; break;
      case SERIALLINK_REQ_CANCEL:    if (st == JOB_RUNNING || st == JOB_HALTING || st == JOB_PAUSED) st = JOB_CANCELLING; break;
      case SERIALLINK_REQ_ACK_ERROR: if (st == JOB_ERROR) st = JOB_IDLE; break;
      case SERIALLINK_REQ_FAIL:      st = JOB_ERRORSTOP; break;
      case SERIALLINK_REQ_JOG_CANCEL: if (!jogCancelling) { Motion_Halt(); jogCancelling = 1; } break;   /* the sketch: ramp down, then drop the rest */
      default: break;
   }
}
static void step(void)
{
   SerialLink_Request_t r;
   if (g_moveMotion) (void)Motion_Task();
   /* ramps end, as the sketch ends them: the planned moves are dropped (Motion_Abort) when a cancel or an error completes */
   if (st == JOB_HALTING || st == JOB_CANCELLING || st == JOB_ERRORSTOP)
   {
      if (rampLeft > 0) rampLeft--;
      else if (st == JOB_HALTING) st = JOB_PAUSED;
      else if (st == JOB_CANCELLING) { Motion_Abort(); st = JOB_IDLE; }
      else { Motion_Abort(); st = JOB_ERROR; }
   }
   if (jogCancelling && Motion_IsHaltRampDone()) { Motion_Abort(); jogCancelling = 0; }
   r = SerialLink_Task(st, g_lcdBusy);
   apply(r);
   /* a running job ends when nothing is left anywhere (the sketch's completion rule for the serial variant) */
   if (st == JOB_RUNNING && !SerialLink_HasWork() && Motion_PlannerIsEmpty() && Motion_IsIdle()) st = JOB_IDLE;
   txPump();
}
static void run(int n) { while (n-- > 0) step(); }
static void settle(void) { run(50); }
static void clearOut(void) { outN = 0; out[0] = 0; }
static void reset_all(int calibrated)
{
   Motion_Position_t o = { 0, 0 };
   Motion_Init(); Motion_SetCurrentPosition(o); GCodeParser_Init();
   UCSR0B = 0; Uart_Init(); SerialLink_Init();
   g_calibrated = calibrated; st = JOB_IDLE; g_lcdBusy = 0; g_moveMotion = 1; g_txPerPass = 1000; g_rampPasses = 0; rampLeft = 0;
   memset(nreq, 0, sizeof nreq); lastReq = 0; jogCancelling = 0; g_penUp = g_penDown = 0;
   clearOut(); run(3); clearOut();                                  /* the welcome line, not under test here */
}
#define EXPECT(s) do { if (strcmp(out, (s)) != 0) { failures++; if (failures < 30) { printf("FAIL line %d: reply was '", __LINE__); { const char *q; for (q = out; *q; q++) { if (*q == '\r') printf("\\r"); else if (*q == '\n') printf("\\n"); else putchar(*q); } } printf("', expected '%s'\n", (s)); } } clearOut(); } while (0)
/* the first report after a reset carries a WCO field (REQ-CoreLR-D-SerialLink-0012): the tests of the rest of the report take it out */
static void stripWco(void) { char *w = strstr(out, "|WCO:0.000,0.000,0.000"); if (w) memmove(w, w + 22, strlen(w + 22) + 1); }
#define EXPECT_ST(s) do { stripWco(); EXPECT(s); } while (0)

/* send one line the way a sender does and run until it has been answered */
#if MOTION_PLANNER_DEPTH == 8u
#define PD "8"
#elif MOTION_PLANNER_DEPTH == 4u
#define PD "4"
#else
#error "test_seriallink: add this planner depth to PD"
#endif
static void line(const char *l) { feed(l); feed("\n"); settle(); }
/* MOTION_PLANNER_DEPTH lines "G1 X1".. (the planner then holds as many as it can), and the reply "ok" for each */
static void fill_planner(void) { char b[24]; for (int i = 1; i <= (int)MOTION_PLANNER_DEPTH; i++) { sprintf(b, "G1 X%d", i); line(b); } }
static const char *oks(int n) { static char o[64]; o[0] = 0; while (n-- > 0) strcat(o, "ok\r\n"); return o; }
static const char *bf(int free_, int rx) { static char o[96]; sprintf(o, "<Hold:0|MPos:0.000,0.000,0.000|Bf:%d,%d|FS:0,0>\r\n", free_, rx); return o; }
/* a line longer than the ring, fed the way a host does: only what fits, and the firmware runs between bursts */
static void linePaced(const char *l)
{
   while (*l)
   {
      while (*l && Uart_RxFree() > 0) rx((uint8_t)*l++);
      step();
   }
   feed("\n"); settle();
}
static void drainMotion(void) { long g = 0; while (!(Motion_PlannerIsEmpty() && Motion_IsIdle()) && g++ < 4000000L) (void)Motion_Task(); }

int main(void)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   printf("1. The welcome line\n");
   UCSR0B = 0; Uart_Init(); g_calibrated = 1; st = JOB_IDLE; SerialLink_Init(); clearOut(); run(5);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");
   run(5); EXPECT("");
   UCSR0B = 0; Uart_Init(); g_calibrated = 0; SerialLink_Init(); clearOut(); run(5);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n[MSG:Not calibrated: use the LCD menu]\r\n");
   UCSR0B = 0; Uart_Init(); g_calibrated = 1; SerialLink_Init(); clearOut(); feed("G1 X1\n"); run(1);
   CHECK(strstr(out, "ok") == NULL, "a line was answered before the welcome line had gone");
   settle(); CHECK(strstr(out, "Grbl") == out + 2, "the welcome line is not first: '%s'", out);

   printf("2. Line assembly: what is stripped, where a line ends, one reply per line\n");
   {
      static const struct { const char *in; const char *want; } t[] = {
         { "\n", "ok\r\n" }, { "\r\n", "ok\r\n" }, { "\r", "ok\r\n" }, { "  \t \n", "ok\r\n" }, { "\x01\x02\n", "ok\r\n" },
         { "; a comment\n", "ok\r\n" }, { "(a comment)\n", "ok\r\n" }, { "( ; )\n", "ok\r\n" }, { "(never closed\n", "ok\r\n" },
         { "G21\n", "ok\r\n" }, { "g21\r\n", "ok\r\n" }, { "G 2 1\n", "ok\r\n" }, { "G21;go\n", "ok\r\n" }, { "G21 (a;b)\n", "ok\r\n" },
         { "N10 G21 F100 S0\n", "ok\r\n" }, { "G21\n\n", "ok\r\nok\r\n" }, { "\n\n\n", "ok\r\nok\r\nok\r\n" }, { "G21\r\r", "ok\r\nok\r\n" }, { "G21\n\rG21\n", "ok\r\nok\r\nok\r\n" },
         { "X5\n", "error:26\r\n" }, { "Q5\n", "error:20\r\n" }, { "G1 X\n", "error:2\r\n" }, { "1\n", "error:1\r\n" }, { "G20\n", "error:20\r\n" },
      };
      unsigned i; int seen = 0;
      for (i = 0; i < sizeof t / sizeof t[0]; i++) { reset_all(1); feed(t[i].in); settle(); seen++;
         if (strcmp(out, t[i].want) != 0) { failures++; printf("FAIL: input #%u gave '%s' instead of '%s'\n", i, out, t[i].want); } clearOut(); }
      printf("   %d inputs\n", seen);
   }
   reset_all(1);
   feed("G1 X1\r"); settle(); EXPECT("ok\r\n");                       /* a CR, and the LF arrives later in another burst: dropped */
   feed("\n"); settle(); EXPECT("");
   feed("G1 X2\r\n"); settle(); EXPECT("ok\r\n");
   feed("G1 X3\n"); feed("\n"); settle(); EXPECT("ok\r\nok\r\n");     /* LF LF is an empty line */
   drainMotion();

   printf("3. 80 significant characters, never truncated\n");
   {
      static char b[8000]; int i;
      reset_all(1);
      strcpy(b, "G1X1Y"); for (i = 5; i < 80; i++) b[i] = '0'; b[80] = 0;      /* 80 significant characters */
      line(b); EXPECT("ok\r\n");
      strcpy(b, "G1X1Y"); for (i = 5; i < 81; i++) b[i] = '0'; b[81] = 0;      /* 81 */
      drainMotion(); settle(); clearOut();
      line(b); EXPECT("error:11\r\n");
      CHECK(st == JOB_IDLE, "an over-long line outside a job moved the state to %d", st);
      line("G1 X2"); EXPECT("ok\r\n");                                          /* the next line is unaffected */
      drainMotion();
      b[0] = 0; for (i = 0; i < 100; i++) strcat(b, " \t "); strcat(b, "G1X3"); for (i = 0; i < 100; i++) strcat(b, "  ");
      strcat(b, "Y0"); linePaced(b); EXPECT("ok\r\n");                         /* blanks do not count */
      drainMotion(); settle(); clearOut();
      b[0] = 0; strcat(b, "G1X4 ("); for (i = 0; i < 300; i++) strcat(b, "comment "); strcat(b, ") Y7 ;"); for (i = 0; i < 50; i++) strcat(b, "more comment ");
      linePaced(b); EXPECT("ok\r\n");                                                 /* comments do not count */
      drainMotion(); settle(); clearOut();
      { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 4000 && p.Y == 7000, "the lines did not all take effect: (%ld,%ld)", (long)p.X, (long)p.Y); }
   }

   printf("4. A line that follows lost bytes is refused, the ones before it are not\n");
   reset_all(1);
   feed("G21\nG21\nG21"); UCSR0A = 1u << 4; UDR0 = 'x'; ISR_USART_RX_vect(); UCSR0A = 0;   /* a framing error in the middle of the third line */
   feed("\nG21\n"); settle();
   EXPECT("ok\r\nok\r\nerror:11\r\nerror:9\r\n");                     /* a refusal during a job stops it: the line in flight gets error:9 */
   CHECK(st == JOB_ERROR, "state %d", st);
   reset_all(1);                                                     /* the same, spaced out: outside a job nothing stops */
   line("G21"); line("G21"); feed("G21"); UCSR0A = 1u << 4; UDR0 = 'x'; ISR_USART_RX_vect(); UCSR0A = 0; line(""); line("G21");
   EXPECT("ok\r\nok\r\nerror:11\r\nok\r\n"); CHECK(st == JOB_IDLE, "state %d", st);
   reset_all(1);                                                     /* an overflow of the ring: 128 bytes of a line fill it, the next ones are lost */
   feed("G1 X"); { int i; for (i = 4; i < 128; i++) rx(' '); }
   feed("5\n");                                                      /* lost: the ring is full and the loop has not run */
   run(5); EXPECT("");
   feed("G21\n"); settle();                                           /* G1X + (lost "5\n") + G21: a damaged line, refused */
   EXPECT("error:11\r\n");
   line("G21"); EXPECT("ok\r\n");

   reset_all(1);                                                     /* a loss followed by an empty line: the line is damaged all the same */
   UCSR0A = 1u << 4; UDR0 = 'x'; ISR_USART_RX_vect(); UCSR0A = 0; clearOut();               /* the ring is empty when the byte is lost */
   feed("\n"); settle(); EXPECT("error:11\r\n");
   reset_all(1); line("$G"); clearOut(); line(""); EXPECT("ok\r\n");        /* an empty line after a $ line is not the $ line again */
   reset_all(1); feed("G1 X1\n"); (void)SerialLink_Task(JOB_ERRORSTOP, 0); txPump(); EXPECT("error:9\r\n");   /* locked during the ramp to the alarm too */

   printf("5. Which line does what, and its reply\n");
   reset_all(1);
   g_moveMotion = 0; line("G1 X10 Y5");
   CHECK(st == JOB_RUNNING && nreq[SERIALLINK_REQ_START] == 1, "the first line while Idle must start the job (state %d, %d starts)", st, nreq[SERIALLINK_REQ_START]);
   EXPECT("ok\r\n");
   g_moveMotion = 1; drainMotion(); settle();
   CHECK(st == JOB_IDLE, "the job did not end with its pipeline (state %d)", st);
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 10000 && p.Y == 5000, "position (%ld,%ld)", (long)p.X, (long)p.Y); }
   CHECK(g_penUp == 0 && g_penDown == 0, "ending a job touched the pen");
   line("X5"); EXPECT("ok\r\n");                                        /* the motion mode is modal */
   drainMotion(); settle(); clearOut();
   line("G91"); EXPECT("ok\r\n"); line("G1 X1"); EXPECT("ok\r\n");
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 6000, "G91 did not persist across the implicit jobs: X = %ld", (long)p.X); }
   drainMotion(); settle(); clearOut();
   line("G90"); line("M3"); EXPECT("ok\r\nok\r\n"); CHECK(g_penDown == 1, "M3 did not reach the pen");
   line("M5"); EXPECT("ok\r\n");
   line("M30"); EXPECT("[MSG:Pgm End]\r\nok\r\n");
   line("X5"); EXPECT("error:26\r\n");                                  /* the modal state is back to its job-start values */
   CHECK(st == JOB_IDLE, "an error outside a job changed the state to %d", st);
   CHECK(nreq[SERIALLINK_REQ_FAIL] == 0, "an error outside a job asked to stop the job");

   reset_all(1); g_txPerPass = 0; feed("\n\n\n\n\n"); run(10);
   CHECK(Uart_TxFree() == UART_TX_SIZE - 20, "five replies of 4 bytes with nothing sent yet: %u bytes free, expected %u (a reply must not wait for an empty ring)", Uart_TxFree(), (unsigned)(UART_TX_SIZE - 20));
   g_txPerPass = 1000; run(3); clearOut();

   printf("6. An invalid line during a job: error, ramped stop, alarm; the lines in flight are answered error:9\n");
   reset_all(1); g_moveMotion = 0;
   line("G1 X10"); EXPECT("ok\r\n");
   feed("G1 Q3\nG1 X5\nG21\n"); run(3);
   EXPECT("error:20\r\nerror:9\r\nerror:9\r\n");
   CHECK(st == JOB_ERROR && nreq[SERIALLINK_REQ_FAIL] == 1, "state %d, %d FAIL requests", st, nreq[SERIALLINK_REQ_FAIL]);
   line("$X"); EXPECT("[MSG:Caution: Unlocked]\r\nok\r\n");
   CHECK(st == JOB_IDLE, "$X did not acknowledge the error (state %d)", st);
   g_moveMotion = 1; drainMotion();
   line("G21"); EXPECT("ok\r\n");

   printf("7. BUSY: a refused line gets its reply only when it is accepted, and nothing behind it is read\n");
   reset_all(1); g_moveMotion = 0;
   fill_planner(); EXPECT(oks((int)MOTION_PLANNER_DEPTH));
   CHECK(nreq[SERIALLINK_REQ_START] == 1, "%d start requests for the lines of one job", nreq[SERIALLINK_REQ_START]);
   feed("G1 X99\nG21\n"); run(20); EXPECT("");
   CHECK(Uart_RxCount() == 4, "the line behind the held one was read (%u bytes left in the ring, expected 4)", Uart_RxCount());
   g_moveMotion = 1; drainMotion(); run(20);
   EXPECT("ok\r\nok\r\n");
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 99000, "the held line was lost: X = %ld", (long)p.X); }
   reset_all(1); g_moveMotion = 0;
   line("G1 X1"); EXPECT("ok\r\n");
   line("M5"); run(10); EXPECT("");                                     /* a pen word waits for the whole pipeline to drain */
   g_moveMotion = 1; drainMotion(); run(10); EXPECT("ok\r\n");

   printf("8. Hold, resume, cancel\n");
   reset_all(1); g_moveMotion = 0;
   line("G1 X10"); EXPECT("ok\r\n"); CHECK(st == JOB_RUNNING, "state %d", st);
   rx('!'); run(3);
   CHECK(st == JOB_PAUSED && nreq[SERIALLINK_REQ_HALT] == 1, "'!' did not pause (state %d)", st);
   feed("G1 X20\nG1 X30\n"); run(10); EXPECT("");                          /* received, held: no reply, nothing executed */
   rx('!'); run(3); CHECK(nreq[SERIALLINK_REQ_HALT] == 1, "'!' while paused did something");
   rx('?'); run(2); EXPECT_ST(bf((int)MOTION_PLANNER_DEPTH - 1, 121));
   rx('~'); run(3); CHECK(st == JOB_RUNNING && nreq[SERIALLINK_REQ_RESUME] == 1, "'~' did not resume (state %d)", st);
   run(10); EXPECT("ok\r\nok\r\n");
   rx('~'); run(3); CHECK(nreq[SERIALLINK_REQ_RESUME] == 1, "'~' while running did something");
   g_moveMotion = 1; drainMotion(); settle(); clearOut();
   reset_all(1);
   rx('!'); rx('~'); run(3); CHECK(nreq[SERIALLINK_REQ_HALT] == 0 && nreq[SERIALLINK_REQ_RESUME] == 0, "'!' or '~' with no job did something");

   printf("9. Ctrl-X\n");
   reset_all(1); g_moveMotion = 0;
   { Motion_Position_t p3 = { 3000, 0 }; Motion_SetCurrentPosition(p3); }
   line("G91"); line("G1 X10"); clearOut();
   feed("G1 X20\nG1 X30\nG1 X4"); rx(0x18); feed("0\nG1 X50\n"); run(1);                        /* bytes sent behind the Ctrl-X ... */
   run(8);
   CHECK(nreq[SERIALLINK_REQ_CANCEL] == 1 && st == JOB_IDLE, "Ctrl-X: %d cancel requests, state %d", nreq[SERIALLINK_REQ_CANCEL], st);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");                                            /* ... are dropped, and the welcome line comes back */
   CHECK(Uart_RxCount() == 0 && !SerialLink_HasWork(), "something is left after the Ctrl-X: %u bytes", Uart_RxCount());
   g_moveMotion = 1; drainMotion();
   line("$G"); EXPECT("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\nok\r\n");        /* the modal state is back to its start values */
   line("G1 X7"); EXPECT("ok\r\n");
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 7000, "the modal state (G91) survived the Ctrl-X: X = %ld", (long)p.X); }
   drainMotion(); settle(); clearOut();
   reset_all(1); rx(0x18); run(5); EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");                 /* idle: just the welcome line */
   CHECK(nreq[SERIALLINK_REQ_CANCEL] == 0, "Ctrl-X while idle asked for a cancel");
   /* FW-14, REQ-CoreLR-F-SerialControl-0015: after a Ctrl-X the motion mode is G0, as grbl's after a reset: Candle's restoration after an Abort has no G word */
   reset_all(1); line("G1 X3"); drainMotion(); settle(); rx(0x18); run(8); clearOut();
   line("$G"); EXPECT("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\nok\r\n");
   line("G21G53G90X10Y5Z0"); EXPECT("ok\r\n"); drainMotion(); settle();
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 10000 && p.Y == 5000, "Candle's post-Abort G53 went to (%ld,%ld)", (long)p.X, (long)p.Y); }
   reset_all(1); rx(0x18); run(8); clearOut(); line("X7 Y2"); EXPECT("ok\r\n"); drainMotion(); settle();       /* a bare coordinate line is accepted after a Ctrl-X */
   reset_all(1); line("X7"); EXPECT("error:26\r\n");                                                                /* ... and still is not at the start (no Ctrl-X) */
   reset_all(1); g_moveMotion = 0; line("G1 X10"); rx('!'); run(3); clearOut();
   rx(0x18); run(8); CHECK(st == JOB_IDLE && nreq[SERIALLINK_REQ_CANCEL] == 1, "Ctrl-X while paused: state %d", st); EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");
   reset_all(1); g_moveMotion = 0; line("G1 X10"); feed("G1 Q1\n"); run(3); clearOut();       /* an error: alarm */
   CHECK(st == JOB_ERROR, "no alarm (state %d)", st);
   rx(0x18); run(8); CHECK(st == JOB_IDLE && nreq[SERIALLINK_REQ_ACK_ERROR] == 1, "Ctrl-X in alarm must acknowledge it (state %d)", st);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");
   reset_all(0); rx(0x18); run(8); EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n[MSG:Not calibrated: use the LCD menu]\r\n");

   /* REQ-CoreLR-F-GCodeInterpreter-0019: M9 and T are accepted and mean nothing; Candle's Abort sequence (bench 2026-10-12) leaves the machine Idle */
   reset_all(1);
   line("M9"); line("T0"); line("T3 M9"); line("G54 G17 G21 G90 G94 M9 T0"); EXPECT("ok\r\nok\r\nok\r\nok\r\n");
   CHECK(g_penUp == 0 && g_penDown == 0, "M9 / T touched the pen (up %d, down %d)", g_penUp, g_penDown);
   line("M9 M3"); EXPECT("ok\r\n"); CHECK(g_penDown == 1, "M3 beside M9 was lost");
   line("M9 M5"); EXPECT("ok\r\n"); CHECK(g_penUp == 1, "M5 beside M9 was lost");
   line("M7"); EXPECT("error:20\r\n");                                                   /* the other coolant words are still refused */
   reset_all(1); line("M9.5"); EXPECT("error:20\r\n");
   reset_all(1); g_moveMotion = 0; line("G1 X50"); g_moveMotion = 1; rx(0x18); run(8); clearOut();
   feed("G21G53G90X5Y0Z0\nG21G92X5Y0Z0\nG54 G17 G21 G90 G94 M9 T0\nS0\n"); settle(); run(50);
   EXPECT("ok\r\nok\r\nok\r\nok\r\n"); CHECK(st == JOB_IDLE, "Candle's Abort sequence left the state at %d (Idle expected)", st);
   printf("9b. Ctrl-X with a ramp that takes time: the welcome waits for it, what arrives meanwhile is dropped, a held line goes\n");
   reset_all(1); g_rampPasses = 5; g_moveMotion = 0;
   line("G1 X10"); clearOut();
   rx(0x18); run(2);
   CHECK(st == JOB_CANCELLING, "state %d, expected the ramp to be running", st);
   CHECK(strstr(out, "Grbl") == NULL, "the welcome line came before the ramp had ended");
   feed("G1 X50\nG21\n"); run(2); CHECK(Uart_RxCount() == 0, "bytes received during the ramp were not dropped (%u in the ring)", Uart_RxCount());
   run(10); CHECK(st == JOB_IDLE, "state %d", st);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");
   g_moveMotion = 1; drainMotion(); settle(); EXPECT("");
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X == 0 && Motion_PlannerIsEmpty(), "a move survived the Ctrl-X: X = %ld", (long)p.X); }
   reset_all(1); g_moveMotion = 0; g_rampPasses = 3;
   fill_planner(); clearOut();
   feed("G1 X99\n"); run(5); EXPECT("");                                          /* held: the planner is full */
   rx(0x18); run(20); EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");                /* the held line is gone: no reply for it */
   g_moveMotion = 1; drainMotion(); settle(); EXPECT("");
   { Motion_Position_t p = Motion_GetTargetPosition(); CHECK(p.X != 99000, "the line held at the Ctrl-X was run afterwards"); }
   reset_all(1); g_rampPasses = 3; g_moveMotion = 0; line("G1 X10"); feed("G1 Q1\n"); run(2);                 /* an error: the ramp to the alarm */
   CHECK(st == JOB_ERRORSTOP, "state %d", st); clearOut();
   rx(0x18); run(2); CHECK(strstr(out, "Grbl") == NULL, "the welcome came during the ramp to the alarm"); run(20);
   CHECK(st == JOB_IDLE && nreq[SERIALLINK_REQ_ACK_ERROR] == 1, "Ctrl-X during the ramp to the alarm: state %d, %d acknowledgements", st, nreq[SERIALLINK_REQ_ACK_ERROR]);
   EXPECT("\r\nGrbl 1.1h ['$' for help]\r\n");

   reset_all(1); g_moveMotion = 0; g_rampPasses = 0; line("G1 X10"); clearOut();
   { char fill[UART_TX_SIZE]; int i; for (i = 0; i < (int)UART_TX_SIZE; i++) fill[i] = 'x'; Uart_TxWrite(fill, UART_TX_SIZE); }
   g_txPerPass = 0; rx(0x18); run(6);                                               /* idle again, but the welcome has no room: it waits */
   CHECK(st == JOB_IDLE, "state %d", st);
   feed("G21\n?"); run(3);                                                           /* in the window between the Ctrl-X and the welcome line */
   g_txPerPass = 1000; run(10);
   CHECK(strstr(out, "ok") == NULL && strstr(out, "<") == NULL, "something sent in the window was answered: '%s'", out);
   clearOut();

   printf("9c. The job is not over while the link holds something\n");
   reset_all(1); g_moveMotion = 0; line("G1 X10"); g_moveMotion = 1; drainMotion();
   feed("G1 X2"); run(5);                                                            /* a line begun, not finished: the host is slow */
   CHECK(st == JOB_RUNNING, "the job ended with a line half received (state %d)", st);
   feed("\n"); settle(); CHECK(st == JOB_IDLE, "state %d once everything is done", st); clearOut();
   reset_all(1);
   { char fill[UART_TX_SIZE]; int i; for (i = 0; i < (int)UART_TX_SIZE; i++) fill[i] = 'x'; Uart_TxWrite(fill, UART_TX_SIZE); }
   g_txPerPass = 0; feed("\n"); run(5);
   CHECK(SerialLink_HasWork(), "a line waiting for room for its reply is not work");
   g_txPerPass = 1000; run(5); CHECK(!SerialLink_HasWork(), "work left after the reply");
   printf("   the welcome line, with a transmit ring that has no room for it yet\n");
   UCSR0B = 0; Uart_Init(); g_calibrated = 1; st = JOB_IDLE; clearOut(); g_txPerPass = 0;
   { char fill[UART_TX_SIZE - 24]; int i; for (i = 0; i < (int)sizeof(fill); i++) fill[i] = 'x'; Uart_TxWrite(fill, sizeof(fill)); }
   SerialLink_Init(); run(5);
   CHECK(SerialLink_HasWork(), "the welcome line is pending but is not work");
   g_txPerPass = 1000; run(10);
   { char e[UART_TX_SIZE + 40]; memset(e, 'x', UART_TX_SIZE - 24); strcpy(e + UART_TX_SIZE - 24, "\r\nGrbl 1.1h ['$' for help]\r\n"); CHECK(strcmp(out, e) == 0, "the welcome line was written without room or lost: '%s'", out); }
   clearOut();

   printf("10. The status report\n");
   reset_all(1);
   rx('?'); run(2); EXPECT_ST("<Idle|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   rx('?'); rx('?'); rx('?'); run(5); EXPECT_ST("<Idle|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");       /* three '?' before the loop looks: one report */
   { Motion_Position_t p = { -1234567, 7 }; Motion_SetCurrentPosition(p); }
   rx('?'); run(2); EXPECT_ST("<Idle|MPos:-1234.567,0.007,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   { Motion_Position_t p = { 0, 0 }; Motion_SetCurrentPosition(p); }
   g_moveMotion = 0; line("G1 X10"); rx('?'); run(2);
   { char e[96]; sprintf(e, "ok\r\n<Run|MPos:0.000,0.000,0.000|Bf:%d,128|FS:0,0>\r\n", (int)MOTION_PLANNER_DEPTH - 1); EXPECT_ST(e); }
   feed("G21"); rx('?'); run(2); { char e[96]; sprintf(e, "<Run|MPos:0.000,0.000,0.000|Bf:%d,125|FS:0,0>\r\n", (int)MOTION_PLANNER_DEPTH - 1); EXPECT_ST(e); }               /* a partial line: 3 bytes in the ring? no: read into the line */
   g_moveMotion = 1; drainMotion(); feed("\n"); settle(); clearOut();
   reset_all(0); rx('?'); run(2); EXPECT_ST("<Alarm|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   reset_all(1); g_lcdBusy = 1; rx('?'); run(2); EXPECT_ST("<Jog|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   line("G1 X1"); EXPECT_ST("error:9\r\n");
   line("$G"); CHECK(strstr(out, "[GC:") != NULL, "$G refused during a jog: '%s'", out); clearOut();
   line(""); EXPECT_ST("ok\r\n");
   g_lcdBusy = 0;
   /* the transient states are reported as they are, so ask the link directly: step() would move on from them */
   reset_all(1); rx('?'); (void)SerialLink_Task(JOB_HALTING, 0); txPump(); EXPECT_ST("<Hold:1|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   rx('?'); (void)SerialLink_Task(JOB_CANCELLING, 0); txPump(); EXPECT_ST("<Hold:1|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   rx('?'); (void)SerialLink_Task(JOB_ERRORSTOP, 0); txPump(); EXPECT_ST("<Alarm|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   rx('?'); (void)SerialLink_Task(JOB_ERROR, 0); txPump(); EXPECT_ST("<Alarm|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   rx('?'); (void)SerialLink_Task(JOB_RUNNING, 0); txPump(); EXPECT_ST("<Run|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
   st = JOB_IDLE;
   { Motion_Position_t p = { -2097151, -2097151 }; reset_all(1); Motion_SetCurrentPosition(p); st = JOB_PAUSED; rx('?'); run(1); st = JOB_PAUSED;
     stripWco(); CHECK(strlen(out) == 57, "the longest report is %u characters, expected 57", (unsigned)strlen(out)); EXPECT_ST("<Hold:0|MPos:-2097.151,-2097.151,0.000|Bf:" PD ",128|FS:0,0>\r\n"); }
   /* it waits for room instead of blocking or truncating */
   reset_all(1); g_txPerPass = 0; feed("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"); run(20); rx('?'); run(5);
   CHECK(strstr(out, "<") == NULL, "a report started without room");
   g_txPerPass = 1000; run(10);
   CHECK(strstr(out, "<Idle|") != NULL && strstr(out, "FS:0,0") != NULL, "the report did not come once there was room: '%s'", out);
   clearOut();

   printf("11. $ commands\n");
   reset_all(1);
   line("$"); EXPECT("[HLP:$$ $# $G $H $I $X ~ ! ? ctrl-x]\r\nok\r\n");
   line("$$"); EXPECT("$10=3\r\n$13=0\r\n$20=0\r\n$21=0\r\n$22=0\r\n$32=0\r\nok\r\n");
   line("$I"); EXPECT("[VER:1.1h.20261011:CoreLR " CORELR_VERSION_STRING "]\r\n[OPT:," PD ",128]\r\nok\r\n");
   line("$G"); EXPECT("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\nok\r\n");
   line("G91 G2 M3"); line("X5 I1"); drainMotion(); settle(); clearOut();
   line("$g"); EXPECT("[GC:G2 G54 G17 G21 G91 G94 M3 M9 T0 F0 S0]\r\nok\r\n");
   line("$ G"); EXPECT("[GC:G2 G54 G17 G21 G91 G94 M3 M9 T0 F0 S0]\r\nok\r\n");           /* blanks are stripped from `$` lines too */
   line("$X"); EXPECT("[MSG:Caution: Unlocked]\r\nok\r\n"); CHECK(nreq[SERIALLINK_REQ_ACK_ERROR] == 0, "$X when no error asked to acknowledge one");
   { static const char *bad[] = { "$N", "$C", "$SLP", "$RST", "$100=250", "$1", "$$$", "$GG", "$XX" }; unsigned i;
     for (i = 0; i < sizeof bad / sizeof bad[0]; i++) { line(bad[i]); if (strcmp(out, "error:3\r\n") != 0) { failures++; printf("FAIL: '%s' gave '%s' instead of error:3\n", bad[i], out); } clearOut(); } }
   reset_all(0);
   line("$X"); EXPECT("[MSG:Not calibrated: use the LCD menu]\r\nerror:9\r\n");
   line("G1 X1"); EXPECT("error:9\r\n");
   line("$I"); CHECK(strstr(out, "[VER:") != NULL, "$I refused when uncalibrated"); clearOut();
   line("$$"); CHECK(strstr(out, "$13=0") != NULL, "$$ refused when uncalibrated"); clearOut();
   line("S100"); EXPECT("error:9\r\n");
   line(""); EXPECT("ok\r\n");
   reset_all(1); g_moveMotion = 0; line("G1 X10"); clearOut();
   line("$X"); EXPECT("error:8\r\n");
   line("$G"); CHECK(strstr(out, "[GC:") != NULL, "$G refused while running"); clearOut();
   rx('!'); run(3); line("$G"); CHECK(strstr(out, "[GC:") != NULL, "$G refused while paused: a $ line must not wait behind G-code"); clearOut();
   line("$X"); EXPECT("error:8\r\n");
   { char b[100]; int i; strcpy(b, "$"); for (i = 0; i < 90; i++) strcat(b, "A"); reset_all(1); line(b); EXPECT("error:11\r\n"); }

   printf("12. Nothing sent contains the substrings 'ok' or 'error' except a reply (Candle takes any line containing either as a reply)\n");
   reset_all(0);
   { static const char *cmds[] = { "$", "$$", "$G", "$I", "$X" }; unsigned i;
     for (i = 0; i < sizeof cmds / sizeof cmds[0]; i++) { line(cmds[i]); }
     rx('?'); settle();
     { char *p = out, *nl; while ((nl = strstr(p, "\r\n")) != NULL) { char save = *nl; *nl = 0;
          if (p[0] == '[' && (strstr(p, "ok") || strstr(p, "error"))) { failures++; printf("FAIL: the message line '%s' contains ok/error\n", p); }
          if (p[0] == '<' && (strstr(p, "ok") || strstr(p, "error"))) { failures++; printf("FAIL: the report '%s' contains ok/error\n", p); }
          *nl = save; p = nl + 2; } } }
   clearOut();

   printf("13. A host that counts to 128 bytes: 3000 random lines, random pacing, random real-time bytes\n");
   {
      /* A sender with character counting: it keeps the sum of the lengths of the lines sent and not yet answered at or below 128. The
       * firmware runs one pass at a time with a random number of transmit bytes per pass. Invariants: exactly one reply per line, in
       * order; no overflow; every byte sent by the firmware is well-formed. */
      unsigned seed = 99; int sentLines = 0, replies = 0, inflight[200], inN = 0, inHead = 0, inBytes = 0, i, errs = 0, reports = 0;
      char rep[8192]; int repN = 0;
      reset_all(1); g_txPerPass = 3;
      for (i = 0; i < 200000 && (sentLines < 3000 || inN > 0); i++)
      {
         seed = seed * 1664525u + 1013904223u;
         if (sentLines < 3000 && ((seed >> 20) % 3) == 0)
         {
            char l[100]; int len, k; unsigned kind = (seed >> 8) % 8; seed = seed * 1664525u + 1013904223u;
            if (kind == 0) strcpy(l, "G1 X1.5 Y-2.25");
            else if (kind == 1) strcpy(l, "G91 G1 X0.2 Y0.2 F1000");
            else if (kind == 2) strcpy(l, "G90 G1 X0 Y0");
            else if (kind == 3) strcpy(l, "(a comment line) ; and more");
            else if (kind == 4) strcpy(l, "G4");                       /* an invalid line: error:20, and the stream goes on in Idle... or stops the job */
            else if (kind == 5) strcpy(l, "$G");
            else if (kind == 6) strcpy(l, "M5");
            else strcpy(l, "");
            len = (int)strlen(l) + 1;
            if (kind == 4) { /* an invalid line during a job stops it: keep the model simple, send it only when nothing runs */ if (st != JOB_IDLE) { continue; } }
            if (inBytes + len <= 128) { for (k = 0; l[k]; k++) rx((uint8_t)l[k]); rx('\n'); inflight[(inHead + inN) % 200] = len; inN++; inBytes += len; sentLines++;
                                       if (((seed >> 5) & 7u) == 0) rx('?'); }
         }
         step();
         /* count the replies the firmware has sent so far; each ends with \r\n, status reports and messages are not replies */
         { int k; for (k = 0; k < outN; k++) rep[repN++ % 8000] = out[k]; clearOut(); }
         { char *p = rep; int total = repN > 8000 ? 8000 : repN; (void)total; (void)p; }
         /* parse replies out of rep */
         { int k, startK = 0; rep[repN] = 0;
           for (k = 0; k + 1 < repN; k++) if (rep[k] == '\r' && rep[k + 1] == '\n') {
              rep[k] = 0; { const char *ln = rep + startK;
                if (ln[0] == '<') reports++;
                else if (ln[0] == '[' || ln[0] == '$' || ln[0] == 0 && 0) { /* a message line of $G: not a reply, the "ok" after it is */ }
                else if (strcmp(ln, "ok") == 0 || strncmp(ln, "error:", 6) == 0) { if (inN > 0) { inBytes -= inflight[inHead]; inHead = (inHead + 1) % 200; inN--; } replies++; }
                else if (ln[0] != 0) { errs++; if (errs < 5) printf("   malformed reply line '%s'\n", ln); } }
              startK = k + 2; k++; }
           memmove(rep, rep + startK, (size_t)(repN - startK)); repN -= startK; }
      }
      CHECK(sentLines == 3000 && inN == 0, "%d lines sent, %d still unanswered after %d passes", sentLines, inN, i);
      CHECK(replies == sentLines, "%d lines sent but %d replies", sentLines, replies);
      CHECK(errs == 0, "%d malformed lines in the output", errs);
      CHECK(Uart_RxCount() == 0 || sentLines == 3000, "ring");
      printf("   %d lines, %d replies, %d status reports, %d passes\n", sentLines, replies, reports, i);
   }

   printf("14. `$J=` jogging and 0x85 (docs/protocol/SerialProtocol.md P2, P6; REQ-CoreLR-D-SerialLink-0011)\n");
   {
      Motion_Position_t p; char e[96];
      /* a jog: ok, no job started, the state stays Idle, the target moved, and the status says Jog while it moves */
      reset_all(1); g_moveMotion = 0;
      line("$J=G21G91X10Y5Z0F600"); EXPECT("ok\r\n");
      p = Motion_GetTargetPosition();
      CHECK(p.X == 10000 && p.Y == 5000, "target (%ld,%ld) after a relative jog of (10,5)", (long)p.X, (long)p.Y);
      CHECK(nreq[SERIALLINK_REQ_START] == 0 && st == JOB_IDLE, "a jog started a job (%d start requests, state %d)", nreq[SERIALLINK_REQ_START], st);
      rx('?'); run(2); CHECK(strncmp(out, "<Jog|", 5) == 0, "status while a jog is queued: '%s'", out); clearOut();
      g_moveMotion = 1; drainMotion(); run(5); clearOut();
      rx('?'); run(2); CHECK(strncmp(out, "<Idle|", 6) == 0, "status once the jog is done: '%s'", out); clearOut();
      { Motion_Position_t q = Motion_GetComputedPosition(); CHECK(q.X == 10000 && q.Y == 5000, "the jog ended at (%ld,%ld)", (long)q.X, (long)q.Y); }
      /* absolute, lower case, spaces, a comment: the same line assembly as G-code */
      line("$j=g90 x20 y0 (go)"); EXPECT("ok\r\n");
      drainMotion();
      /* a continuous jog: lines chained while the previous one still moves; the planner takes them in turn */
      reset_all(1); g_moveMotion = 0;
      { int i; for (i = 0; i < (int)MOTION_PLANNER_DEPTH; i++) line("$J=G91X1F600"); }
      EXPECT(oks((int)MOTION_PLANNER_DEPTH));
      feed("$J=G91X1F600\n"); run(20); EXPECT("");                                   /* planner full: kept, no reply */
      g_moveMotion = 1; drainMotion(); run(20); EXPECT("ok\r\n");                   /* ... and answered when accepted */
      drainMotion(); p = Motion_GetTargetPosition(); CHECK(p.X == (int32_t)(MOTION_PLANNER_DEPTH + 1) * 1000, "chained jogs ended at X = %ld", (long)p.X);
      /* refusals: what the machine says depends on why */
      reset_all(0); line("$J=G91X1"); EXPECT("error:9\r\n");                         /* uncalibrated: locked */
      reset_all(1); g_lcdBusy = 1; line("$J=G91X1"); EXPECT("error:9\r\n"); g_lcdBusy = 0;
      reset_all(1); st = JOB_ERROR; line("$J=G91X1"); EXPECT("error:9\r\n");
      reset_all(1); g_moveMotion = 0; line("G1 X5"); EXPECT("ok\r\n");                 /* a job runs */
      line("$J=G91X1"); EXPECT("error:8\r\n"); drainMotion();
      reset_all(1); st = JOB_PAUSED; line("$J=G91X1"); run(3); EXPECT("error:8\r\n");
      reset_all(1); line("$J=G1X5"); EXPECT("error:20\r\n");
      reset_all(1); line("$J=G91X5I1"); EXPECT("error:20\r\n");
      reset_all(1); line("$J=M3"); EXPECT("error:20\r\n");
      reset_all(1); line("$J=G91X3000"); EXPECT("error:33\r\n");
      reset_all(1); line("$J=G91X"); EXPECT("error:2\r\n");
      reset_all(1); line("$J="); EXPECT("error:3\r\n");                                /* nothing after the = : an invalid statement */
      reset_all(1); line("$J=G91Z5F100"); EXPECT("ok\r\n");                           /* Candle's Z buttons: accepted, nothing to move */
      p = Motion_GetTargetPosition(); CHECK(p.X == 0 && p.Y == 0 && Motion_PlannerIsEmpty(), "a Z-only jog moved something");
      { char over[400]; int k; strcpy(over, "$J=G91X1"); for (k = 0; k < 90; k++) strcat(over, " Y0"); reset_all(1); linePaced(over); EXPECT("error:11\r\n"); }
      /* 0x85: while a jog moves, a ramped stop and the rest dropped; at rest, ignored; never stored in the ring */
      reset_all(1); g_moveMotion = 0; line("$J=G91X30"); line("$J=G91X30"); clearOut();
      g_stepperCap = 150; g_stepperCount = 0; g_moveMotion = 1; run(200);                 /* 150 entries go out, then the queue "is full": the jog is in flight */
      CHECK(!Motion_IsIdle() && !Motion_PlannerIsEmpty(), "the jog is not in flight (idle %d, planner empty %d)", Motion_IsIdle(), Motion_PlannerIsEmpty());
      clearOut(); rx('?'); run(2); CHECK(strncmp(out, "<Jog|", 5) == 0, "status during a jog: '%s'", out); clearOut();
      rx(0x85); g_stepperCap = -1; run(2);
      CHECK(nreq[SERIALLINK_REQ_JOG_CANCEL] == 1 && jogCancelling, "0x85 during a jog: %d cancel requests", nreq[SERIALLINK_REQ_JOG_CANCEL]);
      run(2000);
      CHECK(!jogCancelling && Motion_IsIdle() && Motion_PlannerIsEmpty(), "the cancel did not end: cancelling %d, idle %d, planner empty %d", jogCancelling, Motion_IsIdle(), Motion_PlannerIsEmpty());
      p = Motion_GetComputedPosition(); CHECK(p.X > 0 && p.X < 30000, "after the cancel the head is at X = %ld, expected somewhere between 0 and 60 mm", (long)p.X);
      CHECK(Uart_RxCount() == 0 && st == JOB_IDLE, "0x85 left bytes in the ring (%u) or changed the state (%d)", Uart_RxCount(), st);
      clearOut(); rx('?'); run(2); CHECK(strncmp(out, "<Idle|", 6) == 0, "status after the cancel: '%s'", out); clearOut();
      { int before = nreq[SERIALLINK_REQ_JOG_CANCEL]; rx(0x85); run(5); CHECK(nreq[SERIALLINK_REQ_JOG_CANCEL] == before, "0x85 at rest asked for a cancel"); }
      reset_all(1); g_moveMotion = 0; line("G1 X5"); clearOut(); rx(0x85); run(5);
      CHECK(nreq[SERIALLINK_REQ_JOG_CANCEL] == 0, "0x85 during a JOB asked for a jog cancel (that is what ! and Ctrl-X are for)");
      (void)e; g_stepperCap = -1;
   }

   printf("15. Machine frame, WCO, `$#`, `$H`, G53 (REQ-CoreLR-D-SerialLink-0012, REQ-CoreLR-D-GCodeParser-0018)\n");
   {
      Motion_Position_t p; int i, withWco;
      /* the reference point 100 mm left of and 100 mm above... the drawing origin: MPos = drawing + (100,100) */
      g_refX = 400000L; g_refY = 589898L;
      reset_all(1);
      rx('?'); run(2); EXPECT("<Idle|MPos:100.000,100.000,0.000|Bf:" PD ",128|FS:0,0|WCO:100.000,100.000,0.000>\r\n");     /* the first report carries the WCO */
      withWco = 0;
      for (i = 0; i < 30; i++) { rx('?'); run(2); if (strstr(out, "WCO:") != NULL) withWco++; if (i == 8) CHECK(withWco == 0, "a WCO within the first nine reports after the first"); clearOut(); }
      CHECK(withWco == 3, "WCO in %d of 30 reports, expected 3 (every tenth: grbl's rhythm)", withWco);
      g_refY = 489898L; rx('?'); run(2); EXPECT("<Idle|MPos:100.000,0.000,0.000|Bf:" PD ",128|FS:0,0|WCO:100.000,0.000,0.000>\r\n");   /* Y alone */
      g_refY = 589898L; rx('?'); run(2); EXPECT("<Idle|MPos:100.000,100.000,0.000|Bf:" PD ",128|FS:0,0|WCO:100.000,100.000,0.000>\r\n");
      g_refX = 300000L; rx('?'); run(2); EXPECT("<Idle|MPos:200.000,100.000,0.000|Bf:" PD ",128|FS:0,0|WCO:200.000,100.000,0.000>\r\n");   /* a change goes out at once */
      rx('?'); run(2); EXPECT("<Idle|MPos:200.000,100.000,0.000|Bf:" PD ",128|FS:0,0>\r\n");
      line("$#"); EXPECT("[G54:0.000,0.000,0.000]\r\n[G92:200.000,100.000,0.000]\r\n[TLO:0.000]\r\nok\r\n");
      g_refX = 400000L; clearOut();
      /* the first report after a reset carries the WCO even when it is zero (grbl does) */
      g_refX = 500000L; g_refY = 489898L; reset_all(1);
      rx('?'); run(2); EXPECT("<Idle|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0|WCO:0.000,0.000,0.000>\r\n");
      reset_all(1); rx('?'); run(2); EXPECT("<Idle|MPos:0.000,0.000,0.000|Bf:" PD ",128|FS:0,0|WCO:0.000,0.000,0.000>\r\n");   /* unchanged since the last: still sent first */
      g_refX = 400000L; g_refY = 589898L;

      /* G53: Candle's "Restore origin" is `G21`, `G53 G90 G0 X0 Y0 Z0`, `G92 X0 Y0 Z0` */
      reset_all(1);
      line("G21"); EXPECT("ok\r\n");
      line("G53 G90 G0 X0 Y0 Z0"); EXPECT("ok\r\n"); drainMotion();
      p = Motion_GetTargetPosition(); CHECK(p.X == -100000L && p.Y == -100000L, "G53 X0 Y0 went to (%ld,%ld) in the drawing frame, expected (-100000,-100000)", (long)p.X, (long)p.Y);
      CHECK(g_penUp == 1 && g_penDown == 0, "G53 Z0 did not lift the pen (up %d, down %d)", g_penUp, g_penDown);
      line("G1 X5"); EXPECT("ok\r\n"); drainMotion();                              /* G53 is not modal: the next line is drawing-frame again */
      p = Motion_GetTargetPosition(); CHECK(p.X == 5000L && p.Y == -100000L, "the line after G53 went to (%ld,%ld)", (long)p.X, (long)p.Y);
      line("G53 G0 Y10"); EXPECT("ok\r\n"); drainMotion();                           /* a missing word keeps that axis: X stays at 5 */
      p = Motion_GetTargetPosition(); CHECK(p.X == 5000L && p.Y == -90000L, "G53 G0 Y10 went to (%ld,%ld), expected (5000,-90000)", (long)p.X, (long)p.Y);
      line("G53 G0 X10"); EXPECT("ok\r\n"); drainMotion();
      p = Motion_GetTargetPosition(); CHECK(p.X == -90000L && p.Y == -90000L, "G53 G0 X10 went to (%ld,%ld), expected (-90000,-90000)", (long)p.X, (long)p.Y);
      line("G91"); line("G53 G0 X0 Y0"); drainMotion();                            /* absolute whatever G91 says */
      p = Motion_GetTargetPosition(); CHECK(p.X == -100000L && p.Y == -100000L, "G53 under G91 went to (%ld,%ld)", (long)p.X, (long)p.Y);
      clearOut();
      line("G90"); clearOut();
      /* Safe position: `G53 G0 Z0` -- the pen up, nothing moves; a negative machine Z lowers it */
      reset_all(1);
      line("G53 G0 Z0"); EXPECT("ok\r\n"); drainMotion();
      CHECK(g_penUp == 1 && g_penDown == 0, "G53 G0 Z0 alone: pen up %d, down %d", g_penUp, g_penDown);
      p = Motion_GetTargetPosition(); CHECK(p.X == 0 && p.Y == 0, "G53 G0 Z0 moved the head to (%ld,%ld)", (long)p.X, (long)p.Y);
      line("G53 G0 Z-1"); EXPECT("ok\r\n"); CHECK(g_penDown == 1, "a negative machine Z did not lower the pen");
      line("Z0"); EXPECT("ok\r\n"); CHECK(g_penDown == 2, "a plain Z0 is the pen on the paper (P7)");
      /* what G53 is refused with */
      reset_all(1); line("G53 X1"); EXPECT("error:20\r\n");                       /* no G0/G1 yet */
      reset_all(1); line("G2"); line("G53 X1 Y1 I1"); EXPECT("ok\r\nerror:20\r\n");        /* an arc mode */
      reset_all(1); line("G53 G0 X0 Y0 I1"); EXPECT("error:36\r\n");                       /* an arc offset on a straight move: the same refusal as without G53 */
      reset_all(1); line("G53 G92 X0"); EXPECT("error:21\r\n");
      reset_all(1); line("G53 G0 X-9999"); EXPECT("error:33\r\n");                 /* outside the travel zone, like any move */
      reset_all(1); line("G53 G0 X0 M3"); EXPECT("ok\r\n"); drainMotion();         /* a pen word alongside is as for any line */

      /* $H: pen up if down, then the drawing origin */
      reset_all(1);
      line("M3"); line("G0 X30 Y20"); drainMotion(); settle(); clearOut(); g_penUp = 0;
      line("$H"); EXPECT("ok\r\n"); drainMotion();
      p = Motion_GetTargetPosition(); CHECK(p.X == 0 && p.Y == 0, "$H ended at (%ld,%ld)", (long)p.X, (long)p.Y);
      CHECK(g_penUp == 1, "$H did not lift the pen (%d lifts)", g_penUp);
      settle(); CHECK(st == JOB_IDLE, "$H left the state at %d", st);
      line("$H"); EXPECT("ok\r\n"); CHECK(g_penUp == 1, "$H with the pen up lifted it again");
      reset_all(1); g_moveMotion = 0; line("$J=G91X10"); EXPECT("ok\r\n");        /* a jog in flight: $H waits, then answers */
      feed("$H\n"); run(20); EXPECT(""); g_moveMotion = 1; drainMotion(); run(20); EXPECT("ok\r\n"); drainMotion();
      reset_all(0); line("$H"); EXPECT("error:9\r\n");
      reset_all(1); g_lcdBusy = 1; line("$H"); EXPECT("error:9\r\n"); g_lcdBusy = 0;
      reset_all(1); g_moveMotion = 0; line("G1 X5"); EXPECT("ok\r\n"); line("$H"); EXPECT("error:8\r\n"); drainMotion();
      reset_all(1); line("$HH"); EXPECT("error:3\r\n");
      g_refX = 500000L; g_refY = 489898L;
   }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
