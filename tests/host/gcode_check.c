/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* gcode_check -- run GCode files through the REAL parser, planner and generator of the firmware, and report what the machine would do.
 * Built and driven by tools/bench/gcode_check.sh (see its -h). Stepper, Calibration and PenControl are stubs: every grid step the generator
 * takes is observed, so the bounding box and the length are those of the path the machine would follow, not of the file's numbers.
 *
 * For each file: every line is stripped of its comments as the SD reader does, offered to GCodeParser_ExecuteLine() until it is no longer
 * BUSY, and refused lines are reported with grbl's error number. Then: the bounding box of everything and of what is drawn (pen down), the
 * length drawn, whether the file starts with a pen-up return to the origin and ends pen up at the origin, and whether any line is too long
 * for the SD variant (47 characters before comment stripping, SDREADER_LINE_SIZE - 1) or for the serial link (80 significant characters).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "GCodeParser.h"

/* The machine's geometry, which decides the travel zone: motors 1000 mm apart, the drawing origin at the reference position of a 700 mm cable, unless --geometry D,OX,OY or D,L (mm) says otherwise. */
static int32_t g_D = 1000000L, g_OX = 500000L, g_OY = 489898L;
Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{ Calibration_KinematicsParams_t p; p.MotorDistanceUm = g_D; p.WorkOriginOffsetX = g_OX; p.WorkOriginOffsetY = g_OY; return p; }
void Calibration_RestoreStoredPosition(void) { }
void Calibration_GetWorkOrigin(int32_t *x, int32_t *y) { *x = g_OX; *y = g_OY; }
static int serialMode = 0;   /* --serial: what the serial variant does with a program end and with G92 / G10 (the next line starts a new job; the origin moves) */
uint8_t Calibration_SetWorkOrigin(int32_t x, int32_t y) { if (!serialMode) return 0; g_OX = x; g_OY = y; return 1; }   /* the SD check does not exercise it; --serial stores it, the zone follows */
static int32_t g_OX0, g_OY0; static int g_originKept = 0;   /* the origin where calibration left it = the reference point: what G53 and the machine frame hang on; G92 moves g_OX/g_OY, not this */
void Calibration_GetReferencePosition(int32_t *x, int32_t *y) { *x = g_OX0; *y = g_OY0; }
uint8_t Stepper_RequestMotion(uint8_t l, uint8_t r, uint32_t h, Stepper_PenAction_t p) { (void)l; (void)r; (void)h; (void)p; return 1; }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_IsIdle(void) { return 0; }
void Stepper_CountStarvation(void) { }
static int g_penDown;
void PenControl_RequestUp(void) { g_penDown = 0; }
void PenControl_RequestDown(void) { g_penDown = 1; }

#define SD_LINE_MAX 47
#define LINK_LINE_MAX 80
static double limitMm = 100.0;
static FILE *svg;
static double minX, maxX, minY, maxY, inkMinX, inkMaxX, inkMinY, inkMaxY, inkLen, travelLen;
static long samples, svgPoints;
static Motion_Position_t last; static int lastPen;
static int svgOpenPath;

static void sample(void)
{
   Motion_Position_t p = Motion_GetComputedPosition();
   double x = p.X / 1000.0, y = p.Y / 1000.0;
   if (p.X == last.X && p.Y == last.Y) return;
   samples++;
   if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y;
   if (g_penDown)
   {
      { double lx = last.X / 1000.0, ly = last.Y / 1000.0;              /* where the stroke began */
        if (lx < inkMinX) inkMinX = lx; if (lx > inkMaxX) inkMaxX = lx; if (ly < inkMinY) inkMinY = ly; if (ly > inkMaxY) inkMaxY = ly; }
      if (x < inkMinX) inkMinX = x; if (x > inkMaxX) inkMaxX = x; if (y < inkMinY) inkMinY = y; if (y > inkMaxY) inkMaxY = y;
      inkLen += hypot((double)(p.X - last.X), (double)(p.Y - last.Y)) / 1000.0;
   }
   else travelLen += hypot((double)(p.X - last.X), (double)(p.Y - last.Y)) / 1000.0;
   if (svg && ((samples % 8) == 0 || g_penDown != lastPen))        /* decimated: every 8th grid step is 0.4 mm */
   {
      if (g_penDown != lastPen || !svgOpenPath)
      {
         if (svgOpenPath) fprintf(svg, "\"/>\n");
         fprintf(svg, g_penDown ? "<path class=\"ink\" d=\"M%.3f %.3f" : "<path class=\"move\" d=\"M%.3f %.3f", last.X / 1000.0, -last.Y / 1000.0);
         svgOpenPath = 1;
      }
      fprintf(svg, " L%.3f %.3f", x, -y); svgPoints++;
   }
   lastPen = g_penDown; last = p;
}

/* the SD reader's comment stripping: ';' to the end, (...) anywhere, trailing blanks; returns the length left */
static size_t strip(char *s)
{
   char *w = s; int paren = 0;
   for (const char *r = s; *r; r++)
   {
      if (paren) { if (*r == ')') paren = 0; continue; }
      if (*r == ';') break;
      if (*r == '(') { paren = 1; continue; }
      *w++ = *r;
   }
   while (w > s && isspace((unsigned char)w[-1])) w--;
   *w = 0; return (size_t)(w - s);
}
static void squeeze(const char *in, char *out) { while (*in) { if (!isspace((unsigned char)*in)) *out++ = (char)toupper((unsigned char)*in); in++; } *out = 0; }

static int check(const char *path, const char *svgPath)
{
   FILE *f = fopen(path, "r");
   char raw[1024], code[1024], tight[1024];
   int lineNo = 0, errors = 0, warnings = 0, commands = 0, tooLongSd = 0, tooLongLink = 0, startOk = 1;
   Motion_Position_t o = { 0, 0 };
   if (!f) { printf("%s: cannot open\n", path); return 1; }
   if (!g_originKept) { g_OX0 = g_OX; g_OY0 = g_OY; g_originKept = 1; }
   g_OX = g_OX0; g_OY = g_OY0;     /* every file starts with the origin where calibration left it, whatever the file before did with G92 */
   Motion_Init(); Motion_SetCurrentPosition(o); GCodeParser_Init(); g_penDown = 0; last = o; lastPen = 0; svgOpenPath = 0; samples = svgPoints = 0;
   minX = minY = inkMinX = inkMinY = 0; maxX = maxY = inkMaxX = inkMaxY = 0; inkLen = travelLen = 0;      /* the start is the origin */
   inkMinX = inkMinY = 1e9; inkMaxX = inkMaxY = -1e9;
   svg = svgPath ? fopen(svgPath, "w") : NULL;
   if (svg)
   {
      double L = limitMm, V = L * 1.1, sw = fmax(L / 200.0, 0.1);       /* the picture is the limit's square, with a 10 % margin; strokes scale with it */
      fprintf(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%g %g %g %g\" width=\"660\" height=\"660\">\n<style>.ink{fill:none;stroke:#111;stroke-width:%g}"
                   ".move{fill:none;stroke:#c33;stroke-width:%g;stroke-dasharray:%g %g}.frame{fill:none;stroke:#999;stroke-width:%g}</style>\n"
                   "<rect class=\"frame\" x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"/>\n", -V, -V, 2 * V, 2 * V, sw, sw * 0.3, sw * 2, sw * 2, sw * 0.4, -L, -L, 2 * L, 2 * L);
   }
   while (fgets(raw, sizeof raw, f))
   {
      size_t rawLen = strlen(raw), n; GCodeParser_Result_t r; int tries = 0;
      lineNo++;
      while (rawLen && (raw[rawLen - 1] == '\n' || raw[rawLen - 1] == '\r')) raw[--rawLen] = 0;
      if (rawLen > SD_LINE_MAX) { tooLongSd++; printf("%s:%d: %zu characters: over the SD variant's %d\n", path, lineNo, rawLen, SD_LINE_MAX); }
      strcpy(code, raw); n = strip(code); squeeze(code, tight);
      if (strlen(tight) > LINK_LINE_MAX) { tooLongLink++; printf("%s:%d: %zu significant characters: over the serial link's %d\n", path, lineNo, strlen(tight), LINK_LINE_MAX); }
      if (n == 0) continue;
      if (commands == 0 && strcmp(tight, "M5") != 0) startOk = 0;
      if (commands == 1 && strcmp(tight, "G0X0Y0") != 0 && strcmp(tight, "G90G0X0Y0") != 0) startOk = 0;
      commands++;
      while ((r = GCodeParser_ExecuteLine(code)) == GCODEPARSER_BUSY && tries++ < 10000000) { (void)Motion_Task(); sample(); }
      if (r == GCODEPARSER_BUSY) { printf("%s:%d: never accepted: %s\n", path, lineNo, raw); errors++; break; }
      if (GCODEPARSER_IS_ERROR(r)) { printf("%s:%d: error:%u  %s\n", path, lineNo, (unsigned)r, raw); errors++; warnings++; if (errors >= 10) break; }
      if (r == GCODEPARSER_END && !serialMode) break;
   }
   { long g = 0; while (!Motion_Task() && g++ < 20000000L) sample(); sample(); }
   fclose(f);
   if (svg) { if (svgOpenPath) fprintf(svg, "\"/>\n"); fprintf(svg, "</svg>\n"); fclose(svg); svg = NULL; }
   {
      double ext = fmax(fmax(fabs(minX), fabs(maxX)), fmax(fabs(minY), fabs(maxY)));
      int endOk = (last.X == 0 && last.Y == 0 && !g_penDown), inBox = (ext <= limitMm + 1e-9), failed;
      printf("%s: %d lines, %d commands, %ld waypoints\n", path, lineNo, commands, samples);
      printf("   travelled box  X %.3f .. %.3f  Y %.3f .. %.3f mm   (limit +-%.0f mm: %s)\n", minX, maxX, minY, maxY, limitMm, inBox ? "inside" : "OUTSIDE");
      if (inkLen > 0) printf("   drawn box      X %.3f .. %.3f  Y %.3f .. %.3f mm   ink %.1f mm, pen-up travel %.1f mm\n", inkMinX, inkMaxX, inkMinY, inkMaxY, inkLen, travelLen);
      else printf("   nothing is drawn (the pen is never down while the machine moves)   pen-up travel %.1f mm\n", travelLen);
      printf("   starts with M5 then G0 X0 Y0 (G90 allowed): %s;  ends pen up at the origin: %s;  lines over 47: %d, over 80 significant: %d\n", startOk ? "yes" : "NO", endOk ? "yes" : "NO", tooLongSd, tooLongLink);
      failed = errors || !inBox || !startOk || !endOk || tooLongSd || tooLongLink;
      printf("   %s\n", failed ? "FAIL" : "ok");
      (void)warnings; return failed;
   }
}

int main(int argc, char **argv)
{
   int bad = 0, i; const char *svgDir = NULL;
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--serial")) { serialMode = 1; continue; }
      if (!strcmp(argv[i], "--limit") && i + 1 < argc) { limitMm = atof(argv[++i]); continue; }
      if (!strcmp(argv[i], "--svg") && i + 1 < argc) { svgDir = argv[++i]; continue; }
      if (!strcmp(argv[i], "--geometry") && i + 1 < argc)
      { double d, ox, oy; int k = sscanf(argv[++i], "%lf,%lf,%lf", &d, &ox, &oy);
        if (k == 3) { g_D = (int32_t)lround(d * 1000); g_OX = (int32_t)lround(ox * 1000); g_OY = (int32_t)lround(oy * 1000); continue; }
        if (k == 2)      /* D,L: the motor distance and the reference cable length; the origin is where calibration leaves it (REQ-CoreLR-D-Calibration-0001) */
        { int64_t h = (int64_t)lround(d * 1000) / 2, l = (int64_t)lround(ox * 1000), q, y;
          if (l <= h) { fprintf(stderr, "gcode_check: a cable of %g mm cannot reach the middle of %g mm\n", ox, d); return 2; }
          q = l * l - h * h; y = (int64_t)sqrt((double)q); while (y * y > q) y--; while ((y + 1) * (y + 1) <= q) y++;
          g_D = (int32_t)lround(d * 1000); g_OX = (int32_t)h; g_OY = (int32_t)y; continue; }
        fprintf(stderr, "gcode_check: --geometry D,OX,OY or D,L in mm\n"); return 2; }
      { char sp[1024]; const char *base = strrchr(argv[i], '/'); base = base ? base + 1 : argv[i];
        if (svgDir) snprintf(sp, sizeof sp, "%s/%s.svg", svgDir, base);
        bad += check(argv[i], svgDir ? sp : NULL); }
   }
   return bad;
}
