/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the travel zone (REQ-CoreLR-F-Motion-0008, REQ-CoreLR-D-Motion-0028, REQ-CoreLR-D-GCodeParser-0016): the pen is never commanded where the
 * cables cannot hold it -- beyond the vertical of a motor (side margin), or too close to the line joining the motors (top margin).
 * The real Motion and GCodeParser, the calibration stubbed with a geometry the test can change.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src -I../../firmware/config test_travel.c \
 *       ../../firmware/src/GCodeParser.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_travel
 *
 * Default margins 20 mm (side) and 50 mm (top). The stub geometry: motors 1 000 mm apart, the drawing origin at the reference position of a 700 mm cable
 * (500, 489.898 mm below the motor line), so the zone in the drawing frame is X in [-480, 480] mm and Y (up) at most 439.898 mm.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"

static int32_t g_D = 1000000L, g_offX = 500000L, g_offY = 489898L;       /* the stub geometry */
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = g_D; p.WorkOriginOffsetX = g_offX; p.WorkOriginOffsetY = g_offY; return p; }
void Calibration_RestoreStoredPosition(void) { }
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { (void)x; (void)y; return 0; }   /* the origin is tested in test_origin.c */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = g_D / 2; *y = 489898L; }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = g_offX; *y = g_offY; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
void PenControl_RequestUp(void) { }
void PenControl_RequestDown(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
/* Everything below is written for the default orientation: Y UP the wall (CORELR_Y_UP = 1). With CORELR_Y_UP = 0 the same physical cases are run, their Y
 * coordinates, J offsets and arc directions turned into the other convention (Y_(), flip_line(), cw ^ !CORELR_Y_UP). */
#define POS(x, y) ((Motion_Position_t){ (int32_t)(x), (int32_t)(y) })
#define Y_(v) (CORELR_Y_UP ? (v) : -(v))
#define POSU(x, y) POS((x), Y_(y))                      /* a point given in the Y-up convention */
static void geometry(int32_t d, int32_t ox, int32_t oy) { g_D = d; g_offX = ox; g_offY = oy; }
static void fresh_at(int32_t x, int32_t y) { Motion_Init(); Motion_SetCurrentPosition(POSU(x, y)); GCodeParser_Init(); }
static void fresh(void) { fresh_at(0, 0); }
static int allows(int32_t x, int32_t y) { Motion_Position_t p = POSU(x, y); return Motion_TravelAllows(&p); }
/* a line written for Y up, as the parser of this build must be given it: Y and J values negated, G2 and G3 swapped, when Y is down */
static const char *flip_line(const char *in)
{
   static char out[96]; size_t o = 0;
   if (CORELR_Y_UP) return in;
   for (size_t i = 0; in[i] && o < sizeof out - 2; i++)
   {
      out[o++] = in[i];
      if ((in[i] == 'Y' || in[i] == 'J') && (in[i + 1] == '-' || (in[i + 1] >= '0' && in[i + 1] <= '9')))
      { if (in[i + 1] == '-') i++; else out[o++] = '-'; }
      if (in[i] == 'G' && (in[i + 1] == '2' || in[i + 1] == '3') && !(in[i + 2] >= '0' && in[i + 2] <= '9')) { out[o++] = (char)(in[i + 1] == '2' ? '3' : '2'); i++; }
   }
   out[o] = 0; return out;
}
static GCodeParser_Result_t try_(const char *line) { return GCodeParser_ExecuteLine(flip_line(line)); }
/* require a verdict; a refused line must leave no trace (the queue ends where it did) */
static void expect(const char *line, GCodeParser_Result_t want)
{
   Motion_Position_t b = Motion_GetTargetPosition();
   GCodeParser_Result_t got = try_(line);
   Motion_Position_t a = Motion_GetTargetPosition();
   CHECK(got == want, "'%s': expected %d, got %d", line, (int)want, (int)got);
   if (want != GCODEPARSER_OK) CHECK(a.X == b.X && a.Y == b.Y, "'%s' was refused but moved the target to (%ld,%ld)", line, (long)a.X, (long)a.Y);
}
static int arc(int32_t sx, int32_t sy, int32_t ex, int32_t ey, int32_t i, int32_t j, int cw)
{ Motion_Position_t e = POSU(ex, ey); fresh_at(sx, sy); return Motion_ArcCheck(&e, i, Y_(j), (uint8_t)(CORELR_Y_UP ? cw : !cw)); }

int main(void)
{
   /* ---- 1. the rectangle, edge by edge, to the micrometre. Y is UP in the drawing frame: the top limit is a maximum. */
   printf("1. The zone, to the micrometre (X in [-480000, 480000], Y <= 439898)\n");
   CHECK(allows(0, 0), "the origin is inside");
   CHECK(allows(-480000, 0) && !allows(-480001, 0), "the left edge is at X = -480000");
   CHECK(allows(480000, 0) && !allows(480001, 0), "the right edge is at X = 480000");
   CHECK(allows(0, 439898) && !allows(0, 439899), "the top edge is at Y = 439898");
   CHECK(allows(0, -1500000), "nothing limits the zone downwards (1.5 m below the origin)");
   CHECK(allows(-480000, 439898) && allows(480000, 439898), "the corners are inside");
   CHECK(!allows(-480001, 439899) && !allows(500000, 500000), "outside on both axes");
   CHECK(!allows(-500000, 0) && !allows(500000, 0), "the verticals of the motors themselves are outside");
   CHECK(!allows(0, 489898), "the line joining the motors (Y = 489898) is outside");

   /* ---- 2. the clamp */
   printf("2. Clamping to the zone\n");
   { Motion_Position_t p = POSU(-700000, 100000); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == -480000 && Y_(p.Y) == 100000, "left: (%ld,%ld)", (long)p.X, (long)Y_(p.Y)); }
   { Motion_Position_t p = POSU(700000, 100000);  int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == 480000 && Y_(p.Y) == 100000, "right: (%ld,%ld)", (long)p.X, (long)Y_(p.Y)); }
   { Motion_Position_t p = POSU(10000, 900000);   int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == 10000 && Y_(p.Y) == 439898, "top: (%ld,%ld)", (long)p.X, (long)Y_(p.Y)); }
   { Motion_Position_t p = POSU(-900000, 900000); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == -480000 && Y_(p.Y) == 439898, "corner: (%ld,%ld)", (long)p.X, (long)Y_(p.Y)); }
   { Motion_Position_t p = POSU(123456, -654321); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == 123456 && Y_(p.Y) == -654321, "a point inside is not touched: (%ld,%ld)", (long)p.X, (long)Y_(p.Y)); }
   { Motion_Position_t p = POSU(-480000, 439898); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == -480000 && Y_(p.Y) == 439898, "a point on the corner stays"); }

   /* ---- 3. no geometry, and an empty zone */
   printf("3. Not calibrated, and a zone that does not exist\n");
   geometry(0, 0, 0);
   CHECK(allows(5000000, 5000000), "without a motor distance there is no zone to apply");
   { Motion_Position_t p = POSU(7000000, 7000000); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == 7000000, "no zone: the clamp leaves the point"); }
   geometry(39999, 19999, 100000);                                                  /* D under twice the side margin: empty */
   CHECK(!allows(19999, 0) && !allows(20000, 0) && !allows(0, 0), "an empty zone allows nothing");
   { Motion_Position_t p = POSU(1, 2); int ok = Motion_TravelClamp(&p); CHECK(!ok && p.X == 1 && Y_(p.Y) == 2, "an empty zone: the clamp says so and leaves the point"); }
   geometry(40000, 20000, 100000);                                                  /* exactly twice the margin: one vertical line */
   CHECK(allows(0, 0) && !allows(1, 0) && !allows(-1, 0), "D = 2 x margin leaves a zone of zero width");
   { Motion_Position_t p = POSU(5000, 0); int ok = Motion_TravelClamp(&p); CHECK(ok && p.X == 0, "a zero-width zone can still be clamped to: ok %d, X = %ld", ok, (long)p.X); }
   geometry(1000000L, 500000L, 489898L);

   /* ---- 4. the zone follows the drawing origin */
   printf("4. The zone moves with the drawing origin\n");
   geometry(1000000L, 300000L, 200000L);                       /* the origin 300 mm from the left motor, 200 mm below the line: X in [-280000, 680000], Y <= 150000 */
   CHECK(allows(-280000, 150000) && !allows(-280001, 0) && !allows(0, 150001), "left/top edges moved with the origin");
   CHECK(allows(680000, 0) && !allows(680001, 0), "right edge moved with the origin");
   geometry(1000000L, 500000L, 489898L);

   /* ---- 5. G-code lines */
   printf("5. G-code lines: refused before anything is applied\n");
   fresh(); expect("G1 X480", GCODEPARSER_OK); expect("G1 X480.001", GCODEPARSER_ERR_TARGET_INVALID); expect("G1 X-480.001", GCODEPARSER_ERR_TARGET_INVALID);
   fresh(); expect("G1 Y439.898", GCODEPARSER_OK); expect("G1 Y439.899", GCODEPARSER_ERR_TARGET_INVALID);
   fresh(); expect("G0 X500", GCODEPARSER_ERR_TARGET_INVALID); expect("G0 Y600", GCODEPARSER_ERR_TARGET_INVALID);
   fresh(); expect("G1 X-480 Y439.898", GCODEPARSER_OK);                                                    /* a corner */
   fresh(); expect("G1 Y-1000", GCODEPARSER_OK);                                                            /* a metre below: not the zone's business (the reach is) */
   fresh(); expect("G91", GCODEPARSER_OK); expect("G1 X400", GCODEPARSER_OK); expect("G1 X80", GCODEPARSER_OK); expect("G1 X1", GCODEPARSER_ERR_TARGET_INVALID);   /* relative moves add up: 480, then one more */
   fresh(); expect("G1 X480 Y440", GCODEPARSER_ERR_TARGET_INVALID);                                         /* one axis in, the other out: refused whole */
   fresh(); expect("G1 X10", GCODEPARSER_OK); expect("G1 X500 Y0", GCODEPARSER_ERR_TARGET_INVALID); { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 10000, "a refused line left the target at X = %ld", (long)t.X); }

   /* ---- 6. a machine that is ALREADY outside (a margin raised after calibration) is not trapped */
   printf("6. Starting outside the zone\n");
   fresh_at(-490000, 0);
   expect("G1 X-495", GCODEPARSER_ERR_TARGET_INVALID);        /* further out */
   expect("G1 X-485", GCODEPARSER_ERR_TARGET_INVALID);        /* closer, still outside */
   expect("G1 X-480", GCODEPARSER_OK);                        /* back on the edge */
   fresh_at(0, 450000);
   expect("G1 Y445", GCODEPARSER_ERR_TARGET_INVALID); expect("G1 Y400", GCODEPARSER_OK);

   /* ---- 7. arcs: both ends, and the bulge */
   printf("7. Arcs\n");
   CHECK(arc(-100000, 400000, 100000, 400000, 100000, 0, 1) == 0, "a clockwise half circle over the top (r = 100 mm about Y = 400) reaches Y = 500: refused");
   CHECK(arc(-100000, 400000, 100000, 400000, 100000, 0, 0) == 1, "the same ends, counter-clockwise, under (down to Y = 300): accepted");
   CHECK(arc(400000, 0, 400000, 200000, 0, 100000, 0) == 0, "a counter-clockwise half circle out to X = 500 (r = 100 about X = 400): refused");
   CHECK(arc(400000, 0, 400000, 200000, 0, 100000, 1) == 1, "the same ends, clockwise, to the left (X = 300): accepted");
   CHECK(arc(-400000, 0, -400000, 200000, 0, 100000, 1) == 0, "a clockwise half circle out to X = -500: refused");
   CHECK(arc(-400000, 0, -400000, 200000, 0, 100000, 0) == 1, "the same ends, counter-clockwise, to the right: accepted");
   /* a full circle: the target is the start, the centre is I to the right of it -- here the start is the circle's leftmost point */
   CHECK(arc(-50000, 100000, -50000, 100000, 50000, 0, 0) == 1, "a full circle well inside (centre (0,100), r = 50 mm)");
   CHECK(arc(-39898, 400000, -39898, 400000, 39898, 0, 0) == 1, "a full circle about (0,400) whose top is exactly Y = 439898: inside, to the micrometre");
   CHECK(arc(-39898, 400000, -39898, 400000, 39898, 0, 1) == 1, "... the same, clockwise");
   CHECK(arc(-39900, 400000, -39900, 400000, 39900, 0, 0) == 0, "the same circle with the top 2 um over: refused");
   CHECK(arc(-39900, 400000, -39900, 400000, 39900, 0, 1) == 0, "... the same, clockwise");
   /* the two radii of an arc differ by up to 5 um: the bulge is tested at the LARGER one (centre (0,400), the top would be at 439898 for r = 39898) */
   CHECK(arc(-39898, 400000, 39901, 400000, 39898, 0, 1) == 0, "a start radius of 39898 and an end radius of 39901: the bulge is tested at the larger radius, the end's: refused");
   CHECK(arc(-39901, 400000, 39898, 400000, 39901, 0, 1) == 0, "a start radius of 39901 and an end radius of 39898: the bulge is tested at the larger radius, the start's: refused");
   CHECK(arc(-39897, 400000, 39898, 400000, 39897, 0, 1) == 1, "radii 39897 and 39898: the top is at 439898: inside");
   CHECK(arc(0, 0, 0, 0, 480000, 0, 0) == 0, "a full circle of r = 480 mm about X = 480 reaches X = 960: refused");
   CHECK(arc(0, 0, 100000, 100000, 100000, 0, 0) == 1, "a quarter turn inside, counter-clockwise");
   CHECK(arc(470000, 0, 470000, 20000, 0, 10000, 0) == 1 && arc(470000, 0, 470000, 20000, 0, 10000, 0) == 1, "a small arc near the right edge whose bulge stays at X = 480 (r = 10 about 470): inside");
   CHECK(arc(470000, 0, 470000, 20000, 0, 10000, 0) == 1 && arc(471000, 0, 471000, 20000, 0, 10000, 0) == 0, "one millimetre more to the right and the bulge is out");
   CHECK(arc(-490000, 0, 0, 0, 245000, 0, 0) == 0, "an arc that STARTS outside the zone is refused even if it ends inside");
   CHECK(arc(0, 0, 500000, 0, 250000, 0, 0) == 0, "an arc that ENDS outside the zone is refused");
   /* Only the START outside, the rest of the arc and all its extremes inside: a circle about (-470, 50) of radius 20 mm, from 200 degrees (x = -488.8: outside by 8.8 mm)
    * counter-clockwise to 300 degrees (x = -460). The extreme points on the way (the bottom, 270 degrees) are inside, so only the start test can refuse it. */
   CHECK(arc(-488794, 43160, -460000, 32679, 18794, 6840, 0) == 0, "an arc that starts outside the zone and never leaves it again was accepted");
   /* Only the END outside: the same circle the other way, from 300 degrees clockwise to 200 degrees. */
   CHECK(arc(-460000, 32679, -488794, 43160, -10000, 17321, 1) == 0, "an arc that ends outside the zone, the rest inside, was accepted");
   /* and the same two arcs one centimetre further in are fine: it was the position, nothing else */
   CHECK(arc(-478794, 43160, -450000, 32679, 18794, 6840, 0) == 1, "the same arc 10 mm further in was refused");
   CHECK(arc(-450000, 32679, -478794, 43160, -10000, 17321, 1) == 1, "the same arc, clockwise, 10 mm further in was refused");
   /* the sweep depends on the direction: from the bottom of the circle about (0,430) to its left point is a QUARTER clockwise (inside) and THREE quarters counter-clockwise
    * (over the top, Y = 450 > 439.898: refused) */
   CHECK(arc(0, 410000, -20000, 430000, 0, 20000, 1) == 1, "a clockwise quarter turn that stays below the top margin was refused");
   CHECK(arc(0, 410000, -20000, 430000, 0, 20000, 0) == 0, "the counter-clockwise way round the same circle goes over the top margin and was accepted");
   fresh(); expect("G1 X-100 Y400", GCODEPARSER_OK); expect("G2 X100 Y400 I100 J0", GCODEPARSER_ERR_ARC_RADIUS);   /* over the top: through the parser, the arc error number */
   fresh(); expect("G1 X-100 Y400", GCODEPARSER_OK); expect("G3 X100 Y400 I100 J0", GCODEPARSER_OK);              /* under */
   fresh(); expect("G1 X-100 Y400", GCODEPARSER_OK); expect("G3 X600 Y400 I100 J0", GCODEPARSER_ERR_TARGET_INVALID);  /* the end is outside: the target error */

   /* ---- 8. the joystick jog slice: stops at the edge, slides along it, queues nothing when it has nowhere to go */
   printf("8. Motion_QueueJog(): clamped slices\n");
   fresh_at(479900, 0);
   CHECK(Motion_QueueJog(POSU(481000, 0)) == 1, "a slice towards the right edge was refused");
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 480000 && Y_(t.Y) == 0, "a slice past the right edge ended at (%ld,%ld), expected the edge (480000,0)", (long)t.X, (long)Y_(t.Y)); }
   { uint8_t free0 = Motion_PlannerFree(); CHECK(Motion_QueueJog(POSU(481000, 0)) == 1, "pushing on at the edge was refused");
     CHECK(Motion_PlannerFree() == free0, "pushing on at the edge queued a block (a zero-length one?): free %d -> %d", (int)free0, (int)Motion_PlannerFree()); }
   CHECK(Motion_QueueJog(POSU(490000, 5000)) == 1, "a diagonal slice at the edge was refused");
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 480000 && Y_(t.Y) == 5000, "sliding along the edge ended at (%ld,%ld), expected (480000,5000)", (long)t.X, (long)Y_(t.Y)); }
   CHECK(Motion_QueueJog(POSU(470000, 5000)) == 1, "backing away from the edge was refused");
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 470000 && Y_(t.Y) == 5000, "backing away ended at (%ld,%ld)", (long)t.X, (long)Y_(t.Y)); }
   fresh_at(0, 439000);
   Motion_QueueJog(POSU(0, 450000));
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == 0 && Y_(t.Y) == 439898, "a slice over the top margin ended at (%ld,%ld), expected Y = 439898", (long)t.X, (long)Y_(t.Y)); }
   fresh_at(-490000, 0);                                          /* already outside: a slice further out is brought back to the edge, i.e. IN */
   Motion_QueueJog(POSU(-495000, 0));
   { Motion_Position_t t = Motion_GetTargetPosition(); CHECK(t.X == -480000 && Y_(t.Y) == 0, "from outside, a slice ended at (%ld,%ld), expected the edge", (long)t.X, (long)Y_(t.Y)); }
   geometry(30000, 15000, 100000); fresh_at(0, 0);                 /* an empty zone */
   { uint8_t free0 = Motion_PlannerFree(); CHECK(Motion_QueueJog(POSU(100, 0)) == 1 && Motion_PlannerFree() == free0, "a slice was queued in an empty zone"); }
   geometry(1000000L, 500000L, 489898L); fresh();
   { int n = 0; while (Motion_PlannerHasRoom() && n < 100) { char b[24]; sprintf(b, "G1 X%d", n + 1); try_(b); n++; }
     CHECK(!Motion_PlannerHasRoom(), "setup: the planner is not full");
     CHECK(Motion_QueueJog(POSU(400000L, 0)) == 0, "a slice with the planner full must say so (0) so that the caller retries"); }

   /* ---- 8b. an arc whose CENTRE is far outside the zone (up to the machine's reach, 2.097 m, from the start): only the points of the ARC count, not the centre */
   printf("8b. Arcs whose centre is outside the zone\n");
   {
      /* the circle about (-1500, 0) mm of radius 1100 mm: its right-hand point is (-400, 0), inside; the arc is the short way from -10 to +10 degrees about it */
      const double cx = -1500000.0, r = 1100000.0, d2r = 3.14159265358979323846 / 180.0;
      #define PT(deg) (long)llround(cx + r * cos((deg) * d2r)), (long)llround(r * sin((deg) * d2r))
      long sx = (long)llround(cx + r * cos(-10 * d2r)), sy = (long)llround(r * sin(-10 * d2r)), ex = (long)llround(cx + r * cos(10 * d2r)), ey = (long)llround(r * sin(10 * d2r));
      long i = (long)llround(cx) - sx, j = 0 - sy;
      CHECK(labs(i) < (1L << 21), "setup: I = %ld is beyond the reach", i);
      CHECK(arc(sx, sy, ex, ey, i, j, 0) == 1, "a gently curved counter-clockwise arc of radius 1.1 m, centre 1.5 m left of the machine, entirely inside the zone, was refused");
      CHECK(arc(sx, sy, ex, ey, i, j, 1) == 0, "the same two ends clockwise (the 340 degrees the long way round) was accepted");
      sx = (long)llround(cx + r * cos(-30 * d2r)); sy = (long)llround(r * sin(-30 * d2r)); ex = (long)llround(cx + r * cos(30 * d2r)); ey = (long)llround(r * sin(30 * d2r)); i = (long)llround(cx) - sx; j = 0 - sy;
      CHECK(arc(sx, sy, ex, ey, i, j, 0) == 0, "the same circle from -30 to +30 degrees reaches x = -547 mm, outside the zone, and was accepted");
      #undef PT
      /* the centre ABOVE the zone (beyond the top margin), the arc under it: the circle about (0, 1000) mm, radius 700 mm; its lowest point (0, 300) is inside, y <= 439.898 holds out to +-36.9 degrees */
      { long cy = 1000000, rr = 700000; double a0 = -90 - 20, a1 = -90 + 20;
        long bx = (long)llround(rr * cos(a0 * d2r)), by = cy + (long)llround(rr * sin(a0 * d2r)), fx = (long)llround(rr * cos(a1 * d2r)), fy = cy + (long)llround(rr * sin(a1 * d2r));
        CHECK(arc(bx, by, fx, fy, -bx, cy - by, 0) == 1, "an arc under a centre that is above the top margin, staying below it (+-20 degrees about the lowest point), was refused");
        a0 = -90 - 45; a1 = -90 + 45; bx = (long)llround(rr * cos(a0 * d2r)); by = cy + (long)llround(rr * sin(a0 * d2r)); fx = (long)llround(rr * cos(a1 * d2r)); fy = cy + (long)llround(rr * sin(a1 * d2r));
        CHECK(arc(bx, by, fx, fy, -bx, cy - by, 0) == 0, "the same circle at +-45 degrees climbs to y = 505 mm, over the top margin, and was accepted"); }
      /* a centre beyond the machine's reach is refused by the radius rule, whatever the zone */
      CHECK(arc(0, 0, 100000, 100000, 100000, 2200000, 0) == 0 && arc(0, 0, 100000, 100000, 2200000, 0, 0) == 0, "a centre 2.2 m from the start was accepted");
   }

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
   /* ---- 9. $J= (the serial variant): the same zone, the same status */
   printf("9. $J= jogs\n");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90X480Y0F600")) == GCODEPARSER_OK, "a jog to the right edge was refused");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90X480.001Y0F600")) == GCODEPARSER_ERR_TARGET_INVALID, "a jog 1 um past the right edge was accepted");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90X-480.001")) == GCODEPARSER_ERR_TARGET_INVALID, "a jog past the left edge was accepted");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90Y439.899")) == GCODEPARSER_ERR_TARGET_INVALID, "a jog 1 um over the top margin was accepted");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90Y439.898")) == GCODEPARSER_OK, "a jog to the top edge was refused");
   fresh(); CHECK(GCodeParser_ExecuteJog(flip_line("G21G91X481")) == GCODEPARSER_ERR_TARGET_INVALID, "a relative jog out of the zone was accepted");
   { Motion_Position_t t0 = Motion_GetTargetPosition(); Motion_Position_t t1; (void)GCodeParser_ExecuteJog(flip_line("G21G91X500")); t1 = Motion_GetTargetPosition(); CHECK(t0.X == t1.X && t0.Y == t1.Y, "a refused jog moved the target"); }
   fresh_at(-490000, 0); CHECK(GCodeParser_ExecuteJog(flip_line("G21G90X-480")) == GCODEPARSER_OK, "from outside, a jog to the edge (i.e. in) was refused");
#endif

   if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
   printf("all checks passed\n");
   return 0;
}
