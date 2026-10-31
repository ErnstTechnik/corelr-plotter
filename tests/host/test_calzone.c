/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the geometry check of Calibration_SetParams() (REQ-CoreLR-F-Calibration-0009, REQ-CoreLR-D-Calibration-0011): a motor distance and a
 * reference cable length that put the reference position outside the travel zone -- 20 mm from each motor's vertical, 50 mm below the line joining
 * the motors -- are refused, and a refusal changes nothing. The real Calibration.c, Motion.c and Trig.c; the EEPROM is an array.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Ishim -I../../firmware/src -I../../firmware/config test_calzone.c \
 *       ../../firmware/src/Calibration.c ../../firmware/src/Motion.c ../../firmware/src/Trig.c -lm -o out/test_calzone
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <avr/eeprom.h>
#include "Calibration.h"
#include "Motion.h"
#include "Stepper.h"
#include "SwConfig.h"

static uint8_t g_ee[1024];
void eeprom_read_block(void *dst, const void *src, size_t n) { memcpy(dst, &g_ee[(size_t)src], n); }
uint8_t eeprom_read_byte(const uint8_t *a) { return g_ee[(size_t)a]; }
void eeprom_update_byte(uint8_t *a, uint8_t v) { g_ee[(size_t)a] = v; }
void eeprom_update_block(const void *s, void *d, size_t n) { memcpy(&g_ee[(size_t)d], s, n); }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the oracle: the reference position's Y by floating point, and the zone's rule written out */
static long refY(long d, long l) { double h = d / 2, v = (double)l * l - h * h; return v < 0 ? 0 : (long)floor(sqrt(v)); }
static int should_fit(long d, long l) { return (d / 2) >= MOTION_MARGIN_SIDE_UM && (d / 2) <= d - MOTION_MARGIN_SIDE_UM && refY(d, l) >= MOTION_MARGIN_TOP_UM; }
static uint8_t try_geometry(long d, long l)
{ Calibration_Params_t p; Calibration_GetParams(&p); p.MotorDistanceUm = d; p.ReferenceCableLengthUm = l; return Calibration_SetParams(&p); }
static void fresh(void) { memset(g_ee, 0xFF, sizeof g_ee); Calibration_Init(); Motion_Init(); }

int main(void)
{
   long k;

   printf("1. A normal installation is accepted (D = 1000 mm, Lref = 700 mm)\n");
   fresh();
   CHECK(try_geometry(1000000L, 700000L) == 1, "refused a normal geometry");
   { Calibration_Params_t p; Calibration_GetParams(&p); CHECK(p.MotorDistanceUm == 1000000L && p.ReferenceCableLengthUm == 700000L && p.ReferencePositionX == 500000L && p.ReferencePositionY == 489897L, "stored %ld %ld ref (%ld,%ld)", (long)p.MotorDistanceUm, (long)p.ReferenceCableLengthUm, (long)p.ReferencePositionX, (long)p.ReferencePositionY); }

   printf("2. The top margin, to the micrometre: Lref around the one whose reference is 50 mm below the line (D = 1000 mm, ref Y >= 50000)\n");
   { long lmin = 0; for (long l = 500000L; l < 520000L; l++) if (refY(1000000L, l) >= MOTION_MARGIN_TOP_UM) { lmin = l; break; }
     for (long l = lmin - 3; l <= lmin + 3; l++) { fresh(); CHECK(try_geometry(1000000L, l) == should_fit(1000000L, l), "D=1000000 Lref=%ld: %s, expected %s (ref Y %ld)", l, try_geometry(1000000L, l) ? "accepted" : "refused", should_fit(1000000L, l) ? "accepted" : "refused", refY(1000000L, l)); }
     printf("   the limit is Lref = %ld um\n", lmin); }

   printf("2b. The top margin exactly: a 5-12-13 triangle scaled to 50 mm (half distance 120 mm, cable 130 mm: the reference is exactly 50 mm below the line)\n");
   fresh(); CHECK(try_geometry(240000L, 130000L) == should_fit(240000L, 130000L) && should_fit(240000L, 130000L) == 1, "D=240000 Lref=130000 (ref Y exactly 50000) refused");
   fresh(); CHECK(try_geometry(240000L, 129999L) == 0, "D=240000 Lref=129999 (ref Y 49999.6) accepted");

   printf("3. The side margin: D around 2 x 20 mm\n");
   for (long d = 39996; d <= 40004; d++) { fresh(); uint8_t got = try_geometry(d, 400000L); CHECK(got == should_fit(d, 400000L), "D=%ld Lref=400000: %s, expected %s", d, got ? "accepted" : "refused", should_fit(d, 400000L) ? "accepted" : "refused"); }
   fresh(); CHECK(try_geometry(39998L, 400000L) == 0, "a zone narrower than nothing was accepted");
   fresh(); CHECK(try_geometry(40000L, 400000L) == 1, "D = exactly twice the margin was refused");

   printf("4. A cable shorter than half the distance has no reference point at all\n");
   fresh(); CHECK(try_geometry(1000000L, 499999L) == 0, "Lref < D/2 accepted");
   fresh(); CHECK(try_geometry(1000000L, 500000L) == 0, "Lref = D/2: the point is ON the motor line, accepted");
   fresh(); CHECK(try_geometry(1000000L, 0L) == 0, "Lref = 0 accepted");
   fresh(); CHECK(try_geometry(0L, 700000L) == 0, "D = 0 accepted");

   printf("5. A refusal changes nothing: the parameters, the EEPROM, the calibrated flag\n");
   fresh(); try_geometry(1000000L, 700000L); Calibration_MarkCalibrated(); Calibration_StoreCurrentPosition();
   {
      Calibration_Params_t before, after; uint8_t ee[1024]; memcpy(ee, g_ee, sizeof ee); Calibration_GetParams(&before);
      CHECK(Calibration_IsCalibrated(), "setup: calibrated");
      CHECK(try_geometry(30000L, 700000L) == 0, "a distance too small was accepted");
      Calibration_GetParams(&after);
      CHECK(memcmp(&before, &after, sizeof before) == 0, "a refused geometry changed the parameters in memory");
      CHECK(memcmp(ee, g_ee, sizeof ee) == 0, "a refused geometry wrote to the EEPROM");
      CHECK(Calibration_IsCalibrated(), "a refused geometry made the machine uncalibrated");
      CHECK(try_geometry(1000000L, 501000L) == 0, "a cable only just over half the distance (the reference 32 mm below the line) was accepted");
      Calibration_GetParams(&after);
      CHECK(memcmp(&before, &after, sizeof before) == 0 && Calibration_IsCalibrated(), "the second refusal changed something");
   }

   printf("6. An ACCEPTED change of geometry still invalidates the position (REQ-CoreLR-D-Calibration-0007)\n");
   fresh(); try_geometry(1000000L, 700000L); Calibration_MarkCalibrated(); Calibration_StoreCurrentPosition();
   CHECK(Calibration_IsCalibrated(), "setup");
   CHECK(try_geometry(1000000L, 710000L) == 1 && !Calibration_IsCalibrated(), "a valid new geometry did not make the machine uncalibrated");

   printf("7. A call that leaves D and Lref alone is never judged: the pen angles of a machine whose geometry predates the margins\n");
   {  /* an EEPROM image written by an earlier build: D = Lref = 30 mm, which the zone would refuse today */
      struct { uint32_t Magic; Calibration_Params_t P; uint8_t Sum; } img; uint8_t *b = (uint8_t *)&img; uint8_t sum = 0;
      memset(&img, 0, sizeof img); img.Magic = 0x43524C32UL;
      img.P.MotorDistanceUm = 30000L; img.P.ReferenceCableLengthUm = 30000L; img.P.ReferencePositionX = 15000L; img.P.ReferencePositionY = 25980L; img.P.PenUpAngle = 40; img.P.PenDownAngle = 90;
      for (size_t i = 0; i < sizeof img - 1; i++) sum = (uint8_t)(sum + b[i]); img.Sum = sum;
      memset(g_ee, 0xFF, sizeof g_ee); memcpy(&g_ee[0], &img, sizeof img); Calibration_Init(); Motion_Init();
      Calibration_Params_t p; Calibration_GetParams(&p);
      CHECK(p.MotorDistanceUm == 30000L, "setup: the old image was not loaded (D = %ld)", (long)p.MotorDistanceUm);
      p.PenUpAngle = 55;
      CHECK(Calibration_SetParams(&p) == 1, "a pen-angle change was refused on a machine whose geometry is outside the zone");
      Calibration_GetParams(&p); CHECK(p.PenUpAngle == 55, "the pen angle was not stored");
      Calibration_GetParams(&p); p.ReferenceCableLengthUm = 30001L; CHECK(Calibration_SetParams(&p) == 0, "a change of Lref to another unusable value was accepted");
   }

   printf("8. The drawing origin (REQ-CoreLR-D-Calibration-0012): inside the zone, stored, the reference point\n");
   fresh(); CHECK(Calibration_SetWorkOrigin(500000L, 489897L) == 0, "an origin with no geometry (factory defaults, D = 0) was accepted");
   fresh(); try_geometry(1000000L, 700000L);
   { int32_t rx, ry, ox, oy;
     Calibration_GetReferencePosition(&rx, &ry); CHECK(rx == 500000L && ry == 489897L, "the reference point is (%ld,%ld), expected (500000,489897)", (long)rx, (long)ry);
     CHECK(Calibration_SetWorkOrigin(20000L, 50000L) == 1, "an origin exactly in the top-left corner of the zone was refused");
     Calibration_GetWorkOrigin(&ox, &oy); CHECK(ox == 20000L && oy == 50000L, "the origin reads (%ld,%ld) after being set", (long)ox, (long)oy);
     CHECK(Calibration_SetWorkOrigin(980000L, 50000L) == 1, "an origin exactly on the right limit was refused");
     CHECK(Calibration_SetWorkOrigin(500000L, 1000000L) == 1, "an origin far below the motors (nothing limits the zone downwards) was refused");
     { uint8_t ee[1024]; memcpy(ee, g_ee, sizeof ee); Calibration_GetWorkOrigin(&ox, &oy);
       CHECK(Calibration_SetWorkOrigin(19999L, 60000L) == 0, "an origin 1 um beyond the left limit was accepted");
       CHECK(Calibration_SetWorkOrigin(980001L, 60000L) == 0, "an origin 1 um beyond the right limit was accepted");
       CHECK(Calibration_SetWorkOrigin(500000L, 49999L) == 0, "an origin 1 um beyond the top margin was accepted");
       CHECK(Calibration_SetWorkOrigin(-1L, -1L) == 0 && Calibration_SetWorkOrigin(500000L, 0L) == 0, "an origin on or above the line joining the motors was accepted");
       { int32_t ox2, oy2; Calibration_GetWorkOrigin(&ox2, &oy2); CHECK(ox2 == ox && oy2 == oy, "a refused origin changed the stored one: (%ld,%ld) -> (%ld,%ld)", (long)ox, (long)oy, (long)ox2, (long)oy2); }
       CHECK(memcmp(ee, g_ee, sizeof ee) == 0, "a refused origin wrote to the EEPROM"); }
     CHECK(Calibration_SetWorkOrigin(123456L, 654321L) == 1, "setup: a normal origin");
     Calibration_Init(); Calibration_GetWorkOrigin(&ox, &oy);
     CHECK(ox == 123456L && oy == 654321L, "the origin did not survive a reboot: (%ld,%ld)", (long)ox, (long)oy); }
   fresh(); try_geometry(1000000L, 700000L); Calibration_MarkCalibrated();
   { int32_t ox, oy; Calibration_GetWorkOrigin(&ox, &oy); CHECK(ox == 500000L && oy == 489897L, "'Calibrate machine' (MarkCalibrated) must put the origin at the reference point: (%ld,%ld)", (long)ox, (long)oy);
     Calibration_SetWorkOrigin(300000L, 300000L); Calibration_MarkCalibrated(); Calibration_GetWorkOrigin(&ox, &oy);
     CHECK(ox == 500000L && oy == 489897L, "calibrating again must reset a user-set origin: (%ld,%ld)", (long)ox, (long)oy); }

   if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
   printf("all checks passed\n");
   return 0;
}
