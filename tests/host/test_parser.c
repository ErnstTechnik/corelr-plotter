/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the GCode parser driving the real Motion planner: where does each queued move START?
 * Regression test for the defect of 2026-10-07 (REQ-CoreLR-D-GCodeParser-0008), and the arc words
 * G2/G3/G17/I/J with their validation (REQ-CoreLR-D-GCodeParser-0009).
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_parser.c \
 *       ../../firmware/src/GCodeParser.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_parser
 */
#include <stdio.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"
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
static int g_penUp, g_penDown;
void PenControl_RequestUp(void) { g_penUp++; }
void PenControl_RequestDown(void) { g_penDown++; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void fresh(void) { Motion_Position_t o = { 0, 0 }; Motion_Init(); Motion_SetCurrentPosition(o); GCodeParser_Init(); }
/* run Motion until nothing is left to do: what loop() does while the planner is busy */
static void drain(void) { long g = 0; while (!Motion_Task() && g++ < 4000000) { } }
/* a line the firmware would be offered again until the planner has room, as grbl_corelr.ino does */
static void run(const char *line)
{
   GCodeParser_Result_t r; int tries = 0;
   while ((r = GCodeParser_ExecuteLine(line)) == GCODEPARSER_BUSY && tries++ < 100000) Motion_Task();
   CHECK(r == GCODEPARSER_OK, "'%s' was not accepted (%d)", line, (int)r);
}
static GCodeParser_Result_t try_(const char *line) { return GCodeParser_ExecuteLine(line); }
static const char *name(GCodeParser_Result_t r)
{ static char b[4][16]; static int k; k = (k + 1) & 3;
  if (r == GCODEPARSER_OK) return "OK"; if (r == GCODEPARSER_BUSY) return "BUSY"; if (r == GCODEPARSER_END) return "END";
  sprintf(b[k], "error:%d", (int)r); return b[k]; }
/* fill the planner: MOTION_PLANNER_DEPTH moves, G1 X1 ... (nothing is generated, so they all stay queued) */
static void fill_planner(void) { char b[24]; for (int i = 1; i <= (int)MOTION_PLANNER_DEPTH; i++) { sprintf(b, "G1 X%d", i); run(b); } }
/* plan one line, and require this verdict, and that a refused line left NO trace: nothing queued, no pen action */
static void expect(const char *line, GCodeParser_Result_t want)
{
   int pen0 = g_penUp + g_penDown; int before = Motion_PlannerIsEmpty(); Motion_Position_t b = Motion_GetTargetPosition();
   GCodeParser_Result_t got = try_(line);
   Motion_Position_t a = Motion_GetTargetPosition();
   printf("  %-40s -> %s\n", line, name(got));
   CHECK(got == want, "'%s': expected %s, got %s", line, name(want), name(got));
   if (want != GCODEPARSER_OK && want != GCODEPARSER_END) CHECK((g_penUp + g_penDown) == pen0 && a.X == b.X && a.Y == b.Y && Motion_PlannerIsEmpty() == before, "'%s' was refused but changed something", line);
}

int main(void)
{
   Motion_Position_t e;

   fresh(); run("G91"); run("G1 X10"); run("G1 X10"); run("G1 X10");
   e = Motion_GetTargetPosition();
   printf("three relative G1 X10 queued back to back: the queue ends at X = %ld um\n", (long)e.X);
   CHECK(e.X == 30000 && e.Y == 0, "relative moves did not chain: ended at %ld,%ld", (long)e.X, (long)e.Y);

   fresh(); run("G1 X10 Y5"); run("G1 X20");
   e = Motion_GetTargetPosition();
   printf("G1 X10 Y5, then G1 X20 (Y omitted): the queue ends at (%ld, %ld) um\n", (long)e.X, (long)e.Y);
   CHECK(e.X == 20000 && e.Y == 5000, "an omitted axis was not kept: ended at %ld,%ld", (long)e.X, (long)e.Y);

   fresh(); run("G90"); run("G1 X10"); run("G91"); run("G1 Y4"); run("G90"); run("G1 X0");
   e = Motion_GetTargetPosition();
   printf("absolute, relative, absolute mixed: ends at (%ld, %ld) um\n", (long)e.X, (long)e.Y);
   CHECK(e.X == 0 && e.Y == 4000, "mixed distance modes ended at %ld,%ld", (long)e.X, (long)e.Y);

   /* and with the head moving in between: execution must not change where a later line starts */
   fresh(); run("G91"); run("G1 X10");
   { int i; for (i = 0; i < 5; i++) Motion_Task(); }
   run("G1 X10"); run("G1 X10");
   e = Motion_GetTargetPosition();
   CHECK(e.X == 30000, "with the head already moving the queue ended at X = %ld", (long)e.X);

   printf("\nTwo G words of different groups on one line (the first used to be lost)\n");
   fresh(); run("G91 G1 X5"); run("G90 G1 X20"); run("G1 G91 X5");
   e = Motion_GetTargetPosition();
   printf("  G91 G1 X5 / G90 G1 X20 / G1 G91 X5 -> X = %ld um\n", (long)e.X);
   CHECK(e.X == 25000, "a distance word sharing a line with a motion word was lost: X = %ld", (long)e.X);

   fresh(); run("G91 G1 X5"); run("G1 X5");       /* the distance word of a mixed line must STAY in force, not only act on its own line */
   e = Motion_GetTargetPosition();
   CHECK(e.X == 10000, "G91 on a line with G1 did not persist: X = %ld", (long)e.X);

   printf("\nArcs\n");
   /* which way round: a half circle from (10,0) to (-10,0) about the origin passes through +Y counter-clockwise (G3), -Y clockwise (G2) */
   {
      const char *line[2] = { "G3 X-10 Y0 I-10 J0", "G2 X-10 Y0 I-10 J0" };
      for (int cw = 0; cw < 2; cw++)
      {
         fresh(); run("G1 X10 Y0"); drain(); run(line[cw]);
         long maxY = -1000000000L, minY = 1000000000L; long g = 0; Motion_Position_t p;
         while (!Motion_Task() && g++ < 4000000) { p = Motion_GetComputedPosition(); if (p.Y > maxY) maxY = p.Y; if (p.Y < minY) minY = p.Y; }
         printf("  %s: the path reaches Y from %ld to %ld um\n", line[cw], minY, maxY);
         CHECK(cw ? (minY < -9000 && maxY < 100) : (maxY > 9000 && minY > -100), "%s went the wrong way round", line[cw]);
      }
   }
   fresh(); run("G90"); run("G1 X10 Y0"); run("G3 X0 Y10 I-10 J0");     /* a quarter turn about the origin, counter-clockwise */
   e = Motion_GetTargetPosition();
   printf("  G3 X0 Y10 I-10 J0 from (10,0): the queue ends at (%ld, %ld) um\n", (long)e.X, (long)e.Y);
   CHECK(e.X == 0 && e.Y == 10000, "arc end wrong");
   run("X-10 Y0 I0 J-10");                                              /* modal: another arc, a bare line */
   e = Motion_GetTargetPosition();
   CHECK(e.X == -10000 && e.Y == 0, "a bare X Y I J line did not repeat the arc mode: ended at (%ld,%ld)", (long)e.X, (long)e.Y);
   run("G2 X0 Y-10 I10 J0");  run("G1 X5");
   e = Motion_GetTargetPosition();
   CHECK(e.X == 5000 && e.Y == -10000, "G1 after arcs: (%ld,%ld)", (long)e.X, (long)e.Y);

   fresh(); run("G1 X10 Y0"); run("G91"); run("G3 X-20 I-10");          /* relative: the end is (-10, 0); I stays relative to the START */
   e = Motion_GetTargetPosition();
   CHECK(e.X == -10000 && e.Y == 0, "relative arc: ended at (%ld,%ld)", (long)e.X, (long)e.Y);
   run("G3 X0 Y0 I10");                                                  /* target == start: a full circle, centre (0,0) */
   e = Motion_GetTargetPosition();
   CHECK(e.X == -10000 && e.Y == 0, "a full circle must end where it began: (%ld,%ld)", (long)e.X, (long)e.Y);
   run("G17");                                                           /* the only plane: accepted, means nothing */
   printf("  relative arc, full circle, G17: all accepted\n");

   printf("\nInvalid arc lines are refused with NO effect (planner, pen, position)\n");
   fresh(); run("G1 X10 Y0");
   expect("G2 X0 Y10", GCODEPARSER_ERR_ARC_NO_OFFSET);                  /* no centre */
   expect("G2 I-10", GCODEPARSER_ERR_NO_AXIS_WORDS);                     /* no end point */
   expect("G1 X5 I2", GCODEPARSER_ERR_UNUSED_WORDS);                    /* an offset on a straight move */
   expect("G2 X0 Y10 R10", GCODEPARSER_ERR_UNSUPPORTED);               /* R-format is not supported (yet) */
   expect("G18", GCODEPARSER_ERR_UNSUPPORTED);                         /* other planes */
   expect("G19", GCODEPARSER_ERR_UNSUPPORTED);
   drain();                                                   /* a pen word waits for a drained pipeline, so settle first */
   expect("G91 M3 G3 X-10 Y11 I-10 J0", GCODEPARSER_ERR_ARC_RADIUS);  /* end (0,11): radius 11 against 10 -- refused, and the G91 and M3 must NOT have happened */
   CHECK(g_penDown == 0, "the pen went down for a line that was refused");
   expect("G1 X5", GCODEPARSER_OK);                          /* still absolute: G91 was not applied */
   e = Motion_GetTargetPosition();
   CHECK(e.X == 5000, "the refused line's G91 leaked: a following G1 X5 went to X = %ld", (long)e.X);
   expect("G3 X0 Y0 I-10 J0", GCODEPARSER_ERR_ARC_RADIUS);            /* from (5,0), centre (-5,0): radius 10 at the start, 5 at the end */
   expect("G2 X0 Y3000 I-1000000 J0", GCODEPARSER_ERR_TARGET_INVALID);    /* ... and one far beyond the reach */
   drain();
   expect("G90 M3 G3 X0 Y5 I-5 J0", GCODEPARSER_OK);         /* a valid arc carrying a pen word, planner drained: accepted, pen down once */
   CHECK(g_penDown == 1, "the pen went down %d times", g_penDown);

   printf("\nBUSY is for a full planner only; an invalid arc is an ERROR even then\n");
   fresh(); run("G90"); fill_planner();   /* the planner holds MOTION_PLANNER_DEPTH */
   expect("G1 X5", GCODEPARSER_BUSY);
   { char arc[48]; sprintf(arc, "G3 X%d Y2 I0 J1", (int)MOTION_PLANNER_DEPTH); expect(arc, GCODEPARSER_BUSY); }   /* a valid arc from where the fill ended, planner full */
   { char arc[48]; sprintf(arc, "G3 X%d Y2 I0 J5", (int)MOTION_PLANNER_DEPTH); expect(arc, GCODEPARSER_ERR_ARC_RADIUS); }   /* an invalid arc is refused for being invalid */

   printf("\nReach: a target beyond 2^21 um (2.097 m) in either coordinate is an invalid line (REQ-CoreLR-F-GCodeInterpreter-0011)\n");
   fresh(); run("G90"); run("G1 X0 Y0"); drain();
   expect("G1 X2097.151", GCODEPARSER_OK);                       /* the last micrometre inside */
   expect("G1 X-2097.151 Y2097.151", GCODEPARSER_OK);
   fresh(); run("G90"); run("G1 X0 Y0"); drain();
   expect("G1 X2097.152", GCODEPARSER_ERR_TARGET_INVALID);                    /* 2^21 um exactly: outside */
   expect("G1 X-2097.152", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G1 Y2097.152", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G0 Y-3000", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G1 X1 Y9999", GCODEPARSER_ERR_TARGET_INVALID);                     /* one coordinate in, one out */
   /* digit strings that used to WRAP the int32 back into range: 8589934.592 mm is 2^33 um, which wrapped to 0 */
   expect("G1 X8589934.592", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G1 X4294967.296", GCODEPARSER_ERR_TARGET_INVALID);                 /* 2^32 um: wrapped to 0 as well */
   expect("G1 X99999999999999999999", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G1 X-99999999999999999999.999999", GCODEPARSER_ERR_TARGET_INVALID);
   expect("G1 Y2147483.648", GCODEPARSER_ERR_TARGET_INVALID);                 /* 2^31 um */
   /* relative moves are checked on where they END, from where the queue ends */
   fresh(); run("G90"); run("G1 X2000 Y0"); run("G91");
   expect("G1 X100", GCODEPARSER_ERR_TARGET_INVALID);                         /* 2100 mm: out */
   expect("G1 X90", GCODEPARSER_OK);                             /* 2090 mm: in */
   expect("G1 X10", GCODEPARSER_ERR_TARGET_INVALID);                          /* 2100 mm again, after the accepted one */
   /* a refused line leaves no trace, whatever else it carried */
   fresh(); run("G1 X10 Y0"); drain();
   expect("G91 M3 G1 X5000", GCODEPARSER_ERR_TARGET_INVALID);                 /* no pen action, and the G91 is not applied */
   expect("G1 X5", GCODEPARSER_OK); e = Motion_GetTargetPosition();
   CHECK(e.X == 5000, "a refused line's G91 leaked: X = %ld", (long)e.X);
   /* validity before busyness: out of reach is an ERROR even when the planner is full or a pen word wants a drained pipeline */
   fresh(); run("G90"); fill_planner();
   expect("G1 X3000", GCODEPARSER_ERR_TARGET_INVALID);                        /* planner full: still ERROR, not BUSY */
   expect("M5 G1 X3000", GCODEPARSER_ERR_TARGET_INVALID);                     /* pen word with queued moves: ERROR, not BUSY */
   expect("G1 X5", GCODEPARSER_BUSY);                            /* a valid line is still just BUSY */
   /* arcs: the end point, and the centre offsets (saturated, so they cannot wrap either) */
   fresh(); run("G90"); run("G1 X1000 Y0"); drain();
   expect("G3 X3000 Y0 I1000 J0", GCODEPARSER_ERR_TARGET_INVALID);            /* end point out of reach */
   expect("G3 X0 Y0 I99999999999 J0", GCODEPARSER_ERR_ARC_RADIUS);        /* an absurd centre */
   expect("G3 X-1000 Y0 I-1000 J0", GCODEPARSER_OK);             /* an ordinary half circle is untouched */
   /* inside the reach, everything is as before: a long but legal number is exact */
   fresh(); run("G90"); run("G1 X2097.151 Y-2097.151");
   e = Motion_GetTargetPosition();
   CHECK(e.X == 2097151L && e.Y == -2097151L, "a coordinate at the limit is not exact: %ld,%ld", (long)e.X, (long)e.Y);
   printf("  at the limit exact (%ld, %ld um); beyond it refused with no effect\n", (long)e.X, (long)e.Y);

   printf("\nWhy a line is refused: the grbl status number (REQ-CoreLR-F-GCodeInterpreter-0007, SerialProtocol section 4.6)\n");
   fresh(); run("G1 X10 Y0"); drain();
   expect("1G1", GCODEPARSER_ERR_EXPECTED_LETTER);               /* not a word letter */
   expect("G1 X5 *", GCODEPARSER_ERR_EXPECTED_LETTER);
   expect("%", GCODEPARSER_ERR_EXPECTED_LETTER);
   expect("[5", GCODEPARSER_ERR_EXPECTED_LETTER);                /* the characters just either side of A..Z */
   expect("@5", GCODEPARSER_ERR_EXPECTED_LETTER);
   expect("{1", GCODEPARSER_ERR_EXPECTED_LETTER);
   expect("G", GCODEPARSER_ERR_BAD_NUMBER);                      /* a letter with no number */
   expect("G1 X", GCODEPARSER_ERR_BAD_NUMBER);
   expect("G1 X-", GCODEPARSER_ERR_BAD_NUMBER);
   expect("G1 X.", GCODEPARSER_ERR_BAD_NUMBER);
   expect("Z", GCODEPARSER_ERR_BAD_NUMBER);                      /* number first, as grbl: an unknown letter without a number is a bad number */
   expect("Y5 Q1", GCODEPARSER_ERR_UNSUPPORTED);                 /* an unknown word (Z was the example until P7: now a pen word, see below) */
   expect("Q1", GCODEPARSER_ERR_UNSUPPORTED);
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   expect("T1", GCODEPARSER_OK);                                  /* serial variant: a tool number, ignored (REQ-CoreLR-F-GCodeInterpreter-0019) */
   expect("M9", GCODEPARSER_OK);
#else
   expect("T1", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M9", GCODEPARSER_ERR_UNSUPPORTED);
#endif
   expect("G1 X5 A2", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G4", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G20", GCODEPARSER_ERR_UNSUPPORTED);                   /* inches: still refused, 25.4 times too small otherwise */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   expect("G53", GCODEPARSER_OK);                                 /* serial variant: a machine-frame word with nothing to move (REQ-CoreLR-F-GCodeInterpreter-0018); the cases are in test_seriallink */
#else
   expect("G53", GCODEPARSER_ERR_UNSUPPORTED);   /* the SD variant has none of the serial words */
#endif
   expect("G53.1", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G55", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G92.1", GCODEPARSER_ERR_UNSUPPORTED);                 /* G92 itself is the origin command now (test_origin.c); its variants are not */
   expect("G92", GCODEPARSER_ERR_NO_AXIS_WORDS);
   expect("G95", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G1.5", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G-1", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M0", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M6", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M3.5", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M-5", GCODEPARSER_ERR_UNSUPPORTED);
   /* a code is one byte only after saturating: 256 + 21 must not become G21, nor 256 + 2 / 256 + 30 become M2 / M30 */
   expect("G277", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G310", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G346", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M258", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M286", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M259", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G4294967317", GCODEPARSER_ERR_UNSUPPORTED);           /* 2^32 + 21 */
   fresh();
   expect("X5", GCODEPARSER_ERR_NO_AXIS_WORDS);                  /* coordinates before any G0..G3 */
   expect("I5", GCODEPARSER_ERR_NO_AXIS_WORDS);
   expect("G1 X5", GCODEPARSER_OK);
   expect("G2 X5", GCODEPARSER_ERR_ARC_NO_OFFSET);               /* an end point but no centre */
   expect("G3 J2", GCODEPARSER_ERR_NO_AXIS_WORDS);               /* a centre but no end point */
   expect("g1 x6", GCODEPARSER_OK);                              /* lower case is the same */

   printf("\nG21 G54 G94 are accepted and mean nothing (REQ-CoreLR-F-GCodeInterpreter-0012)\n");
   fresh(); run("G1 X10 Y3"); drain();
   { int pen0 = g_penUp + g_penDown; Motion_Position_t b = Motion_GetTargetPosition();
     expect("G21", GCODEPARSER_OK); expect("G54", GCODEPARSER_OK); expect("G94", GCODEPARSER_OK);
     expect("G21 G90 G17 G54 G94", GCODEPARSER_OK); expect("g21 g94", GCODEPARSER_OK);
     e = Motion_GetTargetPosition();
     CHECK(Motion_PlannerIsEmpty() && e.X == b.X && e.Y == b.Y && (g_penUp + g_penDown) == pen0, "G21/G54/G94 had an effect"); }
   expect("G21 G1 X20", GCODEPARSER_OK);                          /* and they share a line with a move */
   e = Motion_GetTargetPosition();
   CHECK(e.X == 20000 && e.Y == 3000, "G21 G1 X20 ended at (%ld,%ld)", (long)e.X, (long)e.Y);

   printf("\nM2 / M30: program end (REQ-CoreLR-F-GCodeInterpreter-0012)\n");
   {
      const char *endw[3] = { "M2", "M30", "m30" };
      for (int k = 0; k < 3; k++)
      {
         fresh(); run("G91"); run("G1 X10 Y2"); run("M3");  /* pen word waits for a drained pipeline -> run() retries */
         drain();
         { int up0 = g_penUp, dn0 = g_penDown;
           expect(endw[k], GCODEPARSER_END);
           CHECK(g_penUp == up0 + 1 && g_penDown == dn0, "%s: the pen was not lifted exactly once (up %d->%d)", endw[k], up0, g_penUp); }
         /* the modal state is back to the job-start values: absolute, no motion mode */
         expect("X5", GCODEPARSER_ERR_NO_AXIS_WORDS);          /* no motion mode any more */
         expect("G1 X5", GCODEPARSER_OK);                      /* and G91 is gone: 5 mm absolute, not 15 relative */
         e = Motion_GetTargetPosition();
         CHECK(e.X == 5000, "%s: distance mode survived the program end, X = %ld", endw[k], (long)e.X);
      }
   }
   /* the pen lift waits for the pipeline, like M5: with moves queued it is BUSY, and nothing happens */
   fresh(); run("G90"); run("G1 X10"); run("G1 X20");
   expect("M30", GCODEPARSER_BUSY);
   drain();
   expect("N10 M30 S0", GCODEPARSER_END);                        /* tolerated words around it */
   /* a motion on the same line would be queued AFTER the pen lift: refused, not reordered */
   fresh(); run("G1 X1"); drain();
   expect("M30 G1 X5", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G1 X5 M2", GCODEPARSER_ERR_UNSUPPORTED);
   expect("M2 G3 X5 I1", GCODEPARSER_ERR_UNSUPPORTED);
   { int up0 = g_penUp; expect("M5", GCODEPARSER_OK); CHECK(g_penUp == up0 + 1, "M5 still lifts the pen"); }
   fresh(); run("G91"); run("G1 X2"); drain(); run("M5");        /* M5 is NOT a program end: the modal state stays */
   run("G1 X2"); e = Motion_GetTargetPosition();
   CHECK(e.X == 4000, "M5 reset the modal state: X = %ld", (long)e.X);

   printf("\nA long line is read to its end, never truncated (the tokenizer no longer copies the line)\n");
   fresh(); run("G90");
   run("G1 X10                                                                          Y5");      /* 84 characters: the old copy stopped at 47 and dropped Y5 */
   e = Motion_GetTargetPosition();
   CHECK(e.X == 10000 && e.Y == 5000, "a long line was cut: ended at (%ld,%ld)", (long)e.X, (long)e.Y);
   run("g1 x1                                                                            y2");
   e = Motion_GetTargetPosition();
   CHECK(e.X == 1000 && e.Y == 2000, "a long lower-case line was cut: ended at (%ld,%ld)", (long)e.X, (long)e.Y);

#if CORELR_GCODE_Z
   printf("\nP7: a Z word lifts or lowers the pen (REQ-CoreLR-D-GCodeParser-0014); absolute: Z <= 0 down, Z > 0 up; relative: the sign of the move\n");
   {
      int d, u; int8_t mo; uint8_t inc, pd;
      fresh(); run("G90"); run("G1 X10"); drain(); d = g_penDown; u = g_penUp;
      expect("G1 Z0", GCODEPARSER_OK);  CHECK(g_penDown == d + 1 && g_penUp == u, "G90 Z0: %d down, %d up (expected one down)", g_penDown - d, g_penUp - u);
      GCodeParser_GetModal(&mo, &inc, &pd); CHECK(pd == 1, "the modal pen state is %d after a Z0", pd);
      d = g_penDown; u = g_penUp;
      expect("G1 Z5", GCODEPARSER_OK);  CHECK(g_penUp == u + 1 && g_penDown == d, "G90 Z5: %d down, %d up (expected one up)", g_penDown - d, g_penUp - u);
      GCodeParser_GetModal(&mo, &inc, &pd); CHECK(pd == 0, "the modal pen state is %d after a Z5", pd);
      d = g_penDown; u = g_penUp;
      expect("G1 Z-1.5", GCODEPARSER_OK); CHECK(g_penDown == d + 1 && g_penUp == u, "G90 Z-1.5: %d down, %d up (expected one down)", g_penDown - d, g_penUp - u);
      d = g_penDown; u = g_penUp;
      expect("G91 G1 Z0", GCODEPARSER_OK); CHECK(g_penDown == d && g_penUp == u, "relative Z0 changed the pen (%d down, %d up)", g_penDown - d, g_penUp - u);
      expect("G1 Z3", GCODEPARSER_OK);     CHECK(g_penUp == u + 1 && g_penDown == d, "relative Z3: %d down, %d up (expected one up)", g_penDown - d, g_penUp - u);
      expect("G1 Z-2", GCODEPARSER_OK);    CHECK(g_penDown == d + 1, "relative Z-2: %d down (expected one)", g_penDown - d);
      /* a Z with a move: the pen first, then the move, in the same line */
      fresh(); run("G90"); run("G0 X5"); drain(); d = g_penDown;
      expect("G1 X20 Z0", GCODEPARSER_OK); { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(g_penDown == d + 1 && t.X == 20000, "X20 Z0: pen down %d, target X = %ld", g_penDown - d, (long)t.X); }
      /* like M3/M5 it waits for the pipeline to drain: BUSY while moves are queued, and nothing happens */
      fresh(); run("G90"); run("G1 X10"); run("G1 X20"); u = g_penUp; d = g_penDown;
      expect("G1 Z5", GCODEPARSER_BUSY); CHECK(g_penUp == u && g_penDown == d, "a BUSY Z line moved the pen");
      /* two pen words: neither is silently dropped */
      fresh(); run("G90"); expect("M3 Z5", GCODEPARSER_ERR_UNUSED_WORDS); expect("M5 G1 X1 Z0", GCODEPARSER_ERR_UNUSED_WORDS);
      expect("G1 Z", GCODEPARSER_ERR_BAD_NUMBER);
   }
#else
   printf("\nZ is not compiled in (CORELR_GCODE_Z = 0): it is an unsupported word\n");
   fresh(); run("G90"); expect("G1 Z5", GCODEPARSER_ERR_UNSUPPORTED);
#endif

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   printf("\n`$J=`: GCodeParser_ExecuteJog() (REQ-CoreLR-D-GCodeParser-0015, P2, P6)\n");
   {
      Motion_Position_t t0, t1; int8_t mo; uint8_t inc, pd; int d, u; GCodeParser_Result_t r;
      fresh(); run("G90"); run("G1 X10"); drain(); t0 = Motion_GetTargetPosition();
      r = GCodeParser_ExecuteJog("G21G91X10Y5F600Z0"); t1 = Motion_GetTargetPosition();
      CHECK(r == GCODEPARSER_OK && t1.X == t0.X + 10000 && t1.Y == t0.Y + 5000, "relative jog: result %d, target (%ld,%ld) from (%ld,%ld)", (int)r, (long)t1.X, (long)t1.Y, (long)t0.X, (long)t0.Y);
      GCodeParser_GetModal(&mo, &inc, &pd); CHECK(inc == 0 && mo == 1, "a jog changed the modal state (incremental %d, motion %d)", inc, mo);
      drain(); r = GCodeParser_ExecuteJog("G90X20Y0"); t1 = Motion_GetTargetPosition();
      CHECK(r == GCODEPARSER_OK && t1.X == 20000 && t1.Y == 0, "absolute jog: result %d, target (%ld,%ld)", (int)r, (long)t1.X, (long)t1.Y);
      /* the first jog of a fresh parser needs no motion word, and a distance mode absent from the line is the modal one */
      fresh(); run("G91"); r = GCodeParser_ExecuteJog("X3"); t1 = Motion_GetTargetPosition();
      CHECK(r == GCODEPARSER_OK && t1.X == 3000, "jog with the modal G91 on a fresh parser: result %d, X = %ld", (int)r, (long)t1.X);
      /* it starts from where the queue will END */
      fresh(); run("G90"); run("G1 X10"); r = GCodeParser_ExecuteJog("G91X5"); t1 = Motion_GetTargetPosition();
      CHECK(r == GCODEPARSER_OK && t1.X == 15000, "a jog behind a queued move ends at X = %ld, expected 15000", (long)t1.X);
      /* what is not a jog */
      fresh(); d = g_penDown; u = g_penUp;
      CHECK(GCodeParser_ExecuteJog("G1X5") == GCODEPARSER_ERR_UNSUPPORTED, "a motion word in a jog");
      CHECK(GCodeParser_ExecuteJog("G91X5I1") == GCODEPARSER_ERR_UNSUPPORTED, "an arc offset in a jog");
      CHECK(GCodeParser_ExecuteJog("M3") == GCODEPARSER_ERR_UNSUPPORTED, "a pen word in a jog");
      CHECK(GCodeParser_ExecuteJog("G91X5Q1") == GCODEPARSER_ERR_UNSUPPORTED, "an unknown word in a jog");
      CHECK(GCodeParser_ExecuteJog("G91X") == GCODEPARSER_ERR_BAD_NUMBER, "a letter without a number");
      CHECK(GCodeParser_ExecuteJog("G91X3000") == GCODEPARSER_ERR_TARGET_INVALID, "a jog beyond the reach");
      CHECK(Motion_PlannerIsEmpty() && g_penDown == d && g_penUp == u, "a refused jog left a trace");
      /* Z alone, or nothing to move: accepted, nothing done (Candle's Z buttons) */
      CHECK(GCodeParser_ExecuteJog("G21G91Z5F100") == GCODEPARSER_OK && Motion_PlannerIsEmpty() && g_penDown == d && g_penUp == u, "a Z-only jog moved something or touched the pen");
      CHECK(GCodeParser_ExecuteJog("G91") == GCODEPARSER_OK && Motion_PlannerIsEmpty(), "a jog with no axis word");
      /* a full planner: BUSY, kept and offered again */
      fresh(); run("G90"); fill_planner();
      CHECK(GCodeParser_ExecuteJog("G91X1") == GCODEPARSER_BUSY, "a jog into a full planner was not BUSY");
   }
#endif

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
