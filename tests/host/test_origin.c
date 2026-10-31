/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the user's drawing origin (REQ-CoreLR-F-Calibration-0010, REQ-CoreLR-F-GCodeInterpreter-0017, REQ-CoreLR-D-Motion-0029, REQ-CoreLR-D-GCodeParser-0017):
 * Motion_SetOrigin() (an axis, both, or back to the reference point) and the G92 / G10 L20 P1 lines, on the real Motion and GCodeParser. Calibration is a model of the real one: the geometry,
 * the offsets, the reference point, and the same rule (the origin must lie inside the travel zone) with the real margins.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src -I../../firmware/config -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL test_origin.c \
 *       ../../firmware/src/GCodeParser.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_origin
 *
 * Written for Y up; with CORELR_Y_UP=0 the same cases run with their Y values negated (Y_()), as in test_travel.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"
#include "SwConfig.h"

/* ---- the model of Calibration */
static int32_t g_D = 1000000L, g_offX = 500000L, g_offY = 489898L, g_refX = 500000L, g_refY = 489898L;
static int g_stores;                                              /* how many times an origin was stored */
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = g_D; p.WorkOriginOffsetX = g_offX; p.WorkOriginOffsetY = g_offY; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = g_offX; *y = g_offY; }
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = g_refX; *y = g_refY; }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y)
{
   if (g_D <= 0 || !(x >= MOTION_MARGIN_SIDE_UM && x <= g_D - MOTION_MARGIN_SIDE_UM && y >= MOTION_MARGIN_TOP_UM)) return 0;
   g_offX = x; g_offY = y; g_stores++; return 1;
}
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
static int g_penUp, g_penDown;
void PenControl_RequestUp(void) { g_penUp++; }
void PenControl_RequestDown(void) { g_penDown++; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define Y_(v) (CORELR_Y_UP ? (v) : -(v))
#define POSU(x, y) ((Motion_Position_t){ (int32_t)(x), (int32_t)Y_(y) })
static void fresh(void) { g_D = 1000000L; g_offX = 500000L; g_offY = 489898L; g_refX = 500000L; g_refY = 489898L; g_stores = 0; Motion_Init(); Motion_SetCurrentPosition(POSU(0, 0)); GCodeParser_Init(); }
static void drain(void) { long g = 0; while (!Motion_Task() && g++ < 4000000) { } }
static void place(int32_t x, int32_t y) { Motion_SetCurrentPosition(POSU(x, y)); }                 /* the pen is here, in the CURRENT frame (as after a job) */
static int same_lr(Motion_CableLengths_t a, Motion_CableLengths_t b) { return a.L == b.L && a.R == b.R; }
static const char *flip(const char *in)                                                           /* a line written for Y up, as this build must be given it */
{
   static char out[96]; size_t o = 0;
   if (CORELR_Y_UP) return in;
   for (size_t i = 0; in[i] && o < sizeof out - 2; i++)
   { out[o++] = in[i]; if ((in[i] == 'Y' || in[i] == 'J') && (in[i + 1] == '-' || (in[i + 1] >= '0' && in[i + 1] <= '9'))) { if (in[i + 1] == '-') i++; else out[o++] = '-'; } }
   out[o] = 0; return out;
}
/* offer a line until it is no longer BUSY (as loop() does) and return the verdict */
static GCodeParser_Result_t run(const char *line) { GCodeParser_Result_t r; int n = 0; while ((r = GCodeParser_ExecuteLine(flip(line))) == GCODEPARSER_BUSY && n++ < 100000) (void)Motion_Task(); return r; }
static void expect(const char *line, GCodeParser_Result_t want)
{ GCodeParser_Result_t got = run(line); CHECK(got == want, "'%s': expected %d, got %d", line, (int)want, (int)got); }

int main(void)
{
   /* ---- 1. Motion_SetOrigin: the pen stays, the frame moves */
   printf("1. Motion_SetOrigin: the pen does not move, the origin does\n");
   fresh(); place(100000, -50000);
   { Motion_CableLengths_t before = Motion_XYToLR(Motion_GetComputedPosition()); int32_t l0, r0, ix0, iy0, l1, r1, ix1, iy1;
     Motion_GetPersistedState(&l0, &r0, &ix0, &iy0);
     CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 1, "refused 'here is (0,0)'");
     { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 0 && p.Y == 0, "the pen reads (%ld,%ld) after 'here is (0,0)'", (long)p.X, (long)p.Y); }
     CHECK(same_lr(before, Motion_XYToLR(Motion_GetComputedPosition())), "the cable lengths changed: the pen MOVED");
     Motion_GetPersistedState(&l1, &r1, &ix1, &iy1);
     CHECK(l0 == l1 && r0 == r1 && ix0 == ix1 && iy0 == iy1, "the persisted internal position changed with the origin: (%ld,%ld) -> (%ld,%ld)", (long)ix0, (long)iy0, (long)ix1, (long)iy1);
     CHECK(g_offX == 600000L && g_offY == 539898L, "offsets (%ld,%ld), expected (600000,539898): the same point of the wall whichever way Y points", (long)g_offX, (long)g_offY);
     CHECK(g_stores == 1, "the origin was stored %d times", g_stores); }
   printf("   one axis only, and an arbitrary value\n");
   fresh(); place(40000, 70000);
   CHECK(Motion_SetOrigin(MOTION_ORIGIN_X, 25000, 0) == 1, "refused X only");
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 25000 && p.Y == Y_(70000), "X only: reads (%ld,%ld), expected (25000,%ld)", (long)p.X, (long)p.Y, (long)Y_(70000)); }
   CHECK(g_offY == 489898L, "the Y offset moved although only X was set: %ld", (long)g_offY);
   fresh(); place(40000, 70000);
   CHECK(Motion_SetOrigin(MOTION_ORIGIN_Y, 0, Y_(-30000)) == 1, "refused Y only");
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 40000 && p.Y == Y_(-30000), "Y only: reads (%ld,%ld), expected (40000,%ld)", (long)p.X, (long)p.Y, (long)Y_(-30000)); }
   CHECK(g_offX == 500000L, "the X offset moved although only Y was set");

   /* ---- 2. the zone rule: the origin is always inside it */
   printf("2. The origin stays inside the travel zone\n");
   /* internal X of the pen at (0,0) is 500000; (x) given -> offX = 500000 - x: inside iff 20000 <= offX <= 980000, i.e. x in [-480000, 480000] */
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_X, 480000, 0) == 1 && g_offX == 20000L, "an origin exactly on the left limit must be accepted");
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_X, 480001, 0) == 0 && g_offX == 500000L, "an origin 1 um beyond the left limit must be refused, and nothing changed");
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_X, -480000, 0) == 1 && g_offX == 980000L, "an origin exactly on the right limit must be accepted");
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_X, -480001, 0) == 0 && g_offX == 500000L, "an origin 1 um beyond the right limit must be refused");
   /* internal Y of the pen at (0,0) is 489898: y given -> offY = 489898 + y (Y up): inside iff offY >= 50000, i.e. y >= -439898 */
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_Y, 0, Y_(-439898)) == 1 && g_offY == 50000L, "an origin exactly on the top margin must be accepted");
   fresh(); CHECK(Motion_SetOrigin(MOTION_ORIGIN_Y, 0, Y_(-439899)) == 0 && g_offY == 489898L, "an origin 1 um beyond the top margin must be refused");
   fresh(); { Motion_Position_t p0 = Motion_GetComputedPosition(); (void)Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 999999L, 999999L); Motion_Position_t p1 = Motion_GetComputedPosition();
     CHECK(p0.X == p1.X && p0.Y == p1.Y && g_stores == 0, "a refused origin changed the pen's reading or was stored"); }
   fresh(); g_D = 0; CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 0, "an origin with no geometry (not calibrated) must be refused");
   printf("   the zone follows the origin, the cables' limits do not\n");
   fresh(); place(100000, 0);
   { int allowed_before = 0; Motion_Position_t e = POSU(480000, 0); allowed_before = Motion_TravelAllows(&e);
     CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 1, "setup");
     e = POSU(380000, 0);                                                                   /* the same wall point as 480000 before: the pen was 100 mm from the origin */
     CHECK(allowed_before == 1 && Motion_TravelAllows(&e) == 1, "the zone's right edge did not follow the origin (now at X = 380 mm)");
     e = POSU(380001, 0); CHECK(!Motion_TravelAllows(&e), "one micrometre beyond the new right edge is allowed");
     e = POSU(-580000, 0); CHECK(Motion_TravelAllows(&e) == 1, "the left edge did not follow the origin (now at X = -580 mm)");
     e = POSU(-580001, 0); CHECK(!Motion_TravelAllows(&e), "one micrometre beyond the new left edge is allowed"); }

   /* ---- 3. not while something moves or is planned */
   printf("3. Only when nothing is moving\n");
   fresh(); (void)Motion_QueueMove(POSU(10000, 0));
   CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 0 && g_stores == 0, "the origin was set with a move planned");
   { int n = 0; while (!Motion_PlannerIsEmpty() && n++ < 100) (void)Motion_Task(); }                 /* the planner is empty now, but the move is being generated */
   CHECK(Motion_PlannerIsEmpty() && !Motion_IsIdle(), "setup: expected a move in flight with an empty planner (empty %d, idle %d)", (int)Motion_PlannerIsEmpty(), (int)Motion_IsIdle());
   CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 0 && g_stores == 0, "the origin was set while a move was being generated");
   drain();
   CHECK(Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0) == 1, "refused once everything was executed");

   /* ---- 4. back to the reference point */
   printf("4. Back to the calibration reference point\n");
   fresh(); place(100000, -50000); (void)Motion_SetOrigin(MOTION_ORIGIN_X | MOTION_ORIGIN_Y, 0, 0);
   { Motion_CableLengths_t before = Motion_XYToLR(Motion_GetComputedPosition());
     CHECK(Motion_SetOrigin(MOTION_ORIGIN_REFERENCE, 0, 0) == 1 && g_offX == g_refX && g_offY == g_refY, "the origin is (%ld,%ld), the reference (%ld,%ld)", (long)g_offX, (long)g_offY, (long)g_refX, (long)g_refY);
     { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 100000 && p.Y == Y_(-50000), "the pen reads (%ld,%ld) after the origin went back, expected (100000,%ld)", (long)p.X, (long)p.Y, (long)Y_(-50000)); }
     CHECK(same_lr(before, Motion_XYToLR(Motion_GetComputedPosition())), "the pen moved when the origin went back"); }
   fresh(); (void)Motion_QueueMove(POSU(10000, 0)); CHECK(Motion_SetOrigin(MOTION_ORIGIN_REFERENCE, 0, 0) == 0, "back to the reference with a move planned");
   fresh(); g_refX = 10000; CHECK(Motion_SetOrigin(MOTION_ORIGIN_REFERENCE, 0, 0) == 0 && g_offX == 500000L, "a reference outside the zone must be refused (the origin is always inside)");

   /* ---- 5. G92 and G10 L20 P1 */
   printf("5. G92 / G10 L20 P1 lines\n");
   fresh(); expect("G1 X100 Y-50", GCODEPARSER_OK); drain();
   expect("G92 X0 Y0", GCODEPARSER_OK);
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 0 && p.Y == 0, "after G92 X0 Y0 the pen reads (%ld,%ld)", (long)p.X, (long)p.Y); }
   expect("G1 X10", GCODEPARSER_OK); drain();                                                        /* modal G1; 10 mm from the NEW origin */
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 10000 && p.Y == 0, "10 mm from the new origin: (%ld,%ld)", (long)p.X, (long)p.Y); }
   fresh(); expect("G1 X30 Y20", GCODEPARSER_OK); drain(); expect("G10 L20 P1 X0 Y0", GCODEPARSER_OK);
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 0 && p.Y == 0 && g_offX == 530000L, "G10 L20 P1: reads (%ld,%ld), offX %ld", (long)p.X, (long)p.Y, (long)g_offX); }
   fresh(); expect("G1 X30 Y20", GCODEPARSER_OK); drain(); expect("G92 X5", GCODEPARSER_OK);
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 5000 && p.Y == Y_(20000), "G92 X5 alone: reads (%ld,%ld), expected (5000,%ld)", (long)p.X, (long)p.Y, (long)Y_(20000)); }
   fresh(); expect("G91", GCODEPARSER_OK); expect("G1 X30", GCODEPARSER_OK); drain(); expect("G92 X7", GCODEPARSER_OK);                 /* absolute whatever the distance mode */
   { Motion_Position_t p = Motion_GetComputedPosition(); CHECK(p.X == 7000, "G92 X7 under G91 read %ld, expected 7000 (not 37000)", (long)p.X); }
   { int8_t m; uint8_t inc, pen; GCodeParser_GetModal(&m, &inc, &pen); CHECK(inc == 1 && m == 1, "G92 disturbed the modal state: motion %d, incremental %d", (int)m, (int)inc); }
   printf("   waits for the end of what is queued\n");
   fresh(); expect("G1 X10", GCODEPARSER_OK);
   CHECK(GCodeParser_ExecuteLine("G92 X0 Y0") == GCODEPARSER_BUSY, "G92 was accepted with a move still queued");
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 10000 && g_stores == 0, "a BUSY G92 changed something"); }
   printf("   the refusals, each leaving everything as it was\n");
   fresh(); expect("G92", GCODEPARSER_ERR_NO_AXIS_WORDS);
   expect("G92 X0 G1", GCODEPARSER_ERR_AXIS_CONFLICT); expect("G92 X0 Y0 G0", GCODEPARSER_ERR_AXIS_CONFLICT); expect("G92 X0 M3", GCODEPARSER_ERR_AXIS_CONFLICT); expect("G92 X0 I5", GCODEPARSER_ERR_AXIS_CONFLICT);
   expect("G10 L20 P1 X0 G1", GCODEPARSER_ERR_AXIS_CONFLICT);
   expect("G10 L2 P1 X0", GCODEPARSER_ERR_UNSUPPORTED); expect("G10 L20 P2 X0", GCODEPARSER_ERR_UNSUPPORTED); expect("G10 X0", GCODEPARSER_ERR_UNSUPPORTED); expect("G10 L20 X0", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G10 L20 P1", GCODEPARSER_ERR_NO_AXIS_WORDS); expect("G92.1", GCODEPARSER_ERR_UNSUPPORTED); expect("G10 L20.5 P1 X0", GCODEPARSER_ERR_UNSUPPORTED);
   expect("G92 L20 X0", GCODEPARSER_ERR_UNUSED_WORDS); expect("G1 X5 L20", GCODEPARSER_ERR_UNUSED_WORDS); expect("G1 X5 P1", GCODEPARSER_ERR_UNUSED_WORDS);
   expect("G92 X1000", GCODEPARSER_ERR_TARGET_INVALID);                                              /* the pen at (0,0): the origin would be 1 m to its right: outside the zone */
   expect("G92 X480.001", GCODEPARSER_ERR_TARGET_INVALID); expect("G92 X480", GCODEPARSER_OK);        /* ... the edge, to the micrometre */
   fresh(); expect("G92 Y-439.899", GCODEPARSER_ERR_TARGET_INVALID); expect("G92 Y-439.898", GCODEPARSER_OK);
   fresh(); expect("G92 X3000", GCODEPARSER_ERR_TARGET_INVALID);                                      /* beyond the reach (2097 mm): refused before any arithmetic can overflow */
   fresh(); CHECK(g_stores == 0 && g_offX == 500000L, "a refusal stored something");
   printf("   a Z is ignored (Candle restores its work coordinates with G92 X Y Z), a pen word is not\n");
   fresh(); { int pen0 = g_penUp + g_penDown; expect("G92 X0 Y0 Z0", GCODEPARSER_OK); CHECK(g_penUp + g_penDown == pen0, "a Z on a G92 moved the pen"); }
   fresh(); expect("G92 X0 Y0 M5", GCODEPARSER_ERR_AXIS_CONFLICT);
   printf("   not a jog\n");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip("G91G92X5")) == GCODEPARSER_ERR_UNSUPPORTED, "a jog took a G92");
   CHECK(GCodeParser_ExecuteJog("G10L20P1X0") == GCODEPARSER_ERR_UNSUPPORTED, "a jog took a G10");
   CHECK(GCodeParser_ExecuteJog("G91X5L20") == GCODEPARSER_ERR_UNUSED_WORDS, "a jog took an L word");

   if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
   printf("all checks passed\n");
   return 0;
}
