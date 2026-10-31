/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
// Host test of the real LcdJoystickUI.cpp against the display model (REQ-CoreLR-D-LcdJoystickUI-0011, REQ-CoreLR-D-LcdI2c-*).
// The UI is #included so its frame buffer is visible: the property tested is that whatever is on the glass converges to
// the frame the UI means to show, with no traffic when nothing changes and at most one character per Task() call.
//
//   g++ -m32 -O1 -Wall -Wno-unused-function -Ishim_lcd -Ishim_ui -I../../firmware/src test_ui.cpp ../../firmware/src/TextFmt.c ../../firmware/src/JogCurve.c -o out/test_ui
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "avr/io.h"
#include "util/delay.h"
#include "lcd_model.h"
#ifndef LCDI2C_SRC
#define LCDI2C_SRC "../../firmware/src/LcdI2c.c"
#endif
#include LCDI2C_SRC
#ifndef LCDGLYPHS_SRC
#define LCDGLYPHS_SRC "../../firmware/src/LcdGlyphs.c"
#endif
#include LCDGLYPHS_SRC
#include "Arduino.h"
#ifndef UI_SRC
#define UI_SRC "../../firmware/src/LcdJoystickUI.cpp"
#endif
#include UI_SRC

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 25) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

// ---- the rest of the firmware, as stubs the UI can call
static uint8_t g_tick; static Motion_Position_t g_pos; static int g_calibrated = 1; static int g_sw, g_ax = 512, g_ay = 512;
uint8_t SysTick_Get(void) { return g_tick; }
uint8_t SysTick_Elapsed(uint8_t ref, uint8_t n) { return (uint8_t)(g_tick - ref) >= n; }
void pinMode(uint8_t, uint8_t) {}
int analogRead(uint8_t pin) { return pin == LCDJOYSTICKUI_X_PIN ? g_ax : g_ay; }
int digitalRead(uint8_t) { return g_sw ? LOW : HIGH; }
uint8_t Calibration_IsCalibrated(void) { return (uint8_t)g_calibrated; }
int32_t Calibration_GetMotorDistanceUm(void) { return 1000000L; }
int32_t Calibration_GetReferenceCableLengthUm(void) { return 700000L; }
void Calibration_GetParams(Calibration_Params_t *p) { memset(p, 0, sizeof *p); p->MotorDistanceUm = 1000000L; p->ReferenceCableLengthUm = 700000L; }
void Calibration_GetPenAngles(uint8_t *up, uint8_t *down) { *up = 40; *down = 90; }
static int g_setParamsRefuse, g_setParamsCalls;                  // the machine refuses the geometry (the travel zone, REQ-CoreLR-D-Calibration-0011)
uint8_t Calibration_SetParams(const Calibration_Params_t *) { g_setParamsCalls++; return (uint8_t)!g_setParamsRefuse; }
static int g_invalidations;
void Calibration_Invalidate(void) { g_invalidations++; g_calibrated = 0; }
void Calibration_MarkCalibrated(void) {}
void Calibration_StoreCurrentPosition(void) {}
Motion_Position_t Motion_GetComputedPosition(void) { return g_pos; }
Motion_Position_t Motion_GetTargetPosition(void) { return g_pos; }
// "Return home" model: the planner/generator state the test drives by hand, and the order of the calls
static int g_penUpRequests, g_halts, g_aborts, g_calls;      // calls: order of pen/queue to check the pen goes first
static int g_penAt, g_queueAt;
static int g_moving, g_plannerCount, g_haltRampDone = 1, g_plannerRoom = 1, g_queueRefuse;
uint8_t Motion_PlannerHasRoom(void) { return (uint8_t)g_plannerRoom; }
uint8_t Motion_PlannerFree(void) { return (uint8_t)(g_plannerRoom ? MOTION_PLANNER_DEPTH : 0); }
static Motion_Position_t g_queued; static int g_nQueued;
uint32_t Motion_Hypot(int32_t x, int32_t y) { return (uint32_t)sqrt((double)x * x + (double)y * y); }      // JogCurve.c needs it; the real one is tested in test_jogcurve
uint8_t Motion_QueueMove(Motion_Position_t t) { if (g_queueRefuse) { return 0; } g_queued = t; g_nQueued++; g_queueAt = ++g_calls; g_plannerCount++; return 1; }
void Motion_SetCurrentPosition(Motion_Position_t p) { g_pos = p; }
uint8_t Motion_StepToCoilBits(int32_t, uint8_t) { return 0; }
// the Control jog's slices go to Motion_QueueJog() (the travel zone is Motion's: REQ-CoreLR-D-LcdJoystickUI-0029, tested with the real Motion in test_travel.c)
// the origin (REQ-CoreLR-D-LcdJoystickUI-0031): Motion_SetOrigin / Motion_SetOriginToReference are recorded and answer what the test says
static int g_setOriginCalls, g_setRefCalls, g_originOk = 1; static uint8_t g_soWhat; static int32_t g_soXv, g_soYv;
uint8_t Motion_SetOrigin(uint8_t what, int32_t x, int32_t y) { if (what & MOTION_ORIGIN_REFERENCE) { g_setRefCalls++; } else { g_setOriginCalls++; } g_soWhat = what; g_soXv = x; g_soYv = y; return (uint8_t)g_originOk; }
static int g_nJogQueued;
uint8_t Motion_QueueJog(Motion_Position_t t) { g_nJogQueued++; return Motion_QueueMove(t); }
Motion_CableLengths_t Motion_XYToLR(Motion_Position_t p) { Motion_CableLengths_t l; l.L = 700000L + p.X + p.Y / 2; l.R = 700000L - p.X + p.Y / 3; return l; }
void PenControl_PreviewAngle(uint8_t) {}
// "Return home": the motion and pen calls it makes, recorded; the planner/generator state is a small model the test drives by hand
void PenControl_RequestUp(void) { g_penUpRequests++; g_penAt = ++g_calls; }
static int g_penIsDown; uint8_t PenControl_IsDown(void) { return (uint8_t)g_penIsDown; }
void Motion_Halt(void) { g_halts++; }
void Motion_Abort(void) { g_aborts++; g_moving = 0; g_plannerCount = 0; }
uint8_t Motion_IsHaltRampDone(void) { return (uint8_t)g_haltRampDone; }
uint8_t Motion_IsIdle(void) { return (uint8_t)!g_moving; }
uint8_t Motion_PlannerIsEmpty(void) { return (uint8_t)(g_plannerCount == 0); }
uint8_t Stepper_QueuedCount(void) { return 0; }
uint8_t Stepper_RequestMotion(uint8_t, uint8_t, uint32_t, Stepper_PenAction_t) { return 1; }
void Stepper_ResetStarvedCount(void) {}
uint8_t Stepper_StarvedCount(void) { return 0; }
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
static int g_sdRefuse, g_sdIsDir;
SDReader_CardStatus_t SDReader_CardStatus(void) { return (SDReader_CardStatus_t)0; }
uint8_t SDReader_Mount(void) { return 1; }
uint8_t SDReader_BrowserEntryCount(void) { return 5; }
void SDReader_BrowserGetEntryName(uint8_t i, char *out, uint8_t cap) { snprintf(out, cap, "PLOT%u.GCO", (unsigned)i); }
uint8_t SDReader_BrowserIsDirectory(uint8_t) { return (uint8_t)g_sdIsDir; }
uint8_t SDReader_BrowserEnter(uint8_t) { return 1; }
void SDReader_BrowserGoUp(void) {}
uint8_t SDReader_BrowserAtRoot(void) { return 1; }
uint8_t SDReader_BrowserRecover(void) { return 0; }
uint8_t SDReader_BrowserEnterRefused(void) { return (uint8_t)g_sdRefuse; }
#endif

// ---- helpers
static void settle(void)                           // run Task() until the frame is on the glass (bounded)
{
   for (int i = 0; i < 400 && !FrameIsClean(); i++) { LcdJoystickUI_Task(); }
}
static int glassMatchesFrame(void)
{
   char a[17];
   for (int r = 0; r < 2; r++) { lcd.row(r, a); if (memcmp(a, s_frame + r * 16, 16) != 0) return 0; }
   return 1;
}
static void dump(const char *what) { char a[17], b[17]; lcd.row(0, a); lcd.row(1, b); printf("  %-30s glass |%s|%s|\n", what, a, b); }

int main(void)
{
   srand(9);
   printf("1. Start-up: the glass converges to the frame, and then the bus is quiet\n");
   power_on(); g_tick = 0; g_pos.X = 0; g_pos.Y = 0;
   LcdJoystickUI_Init();
   settle(); dump("after Init + settle");
   CHECK(glassMatchesFrame(), "the glass does not match the frame after start-up");
   {
      int tr = transactions;
      for (int i = 0; i < 300; i++) { g_tick++; LcdJoystickUI_Task(); }
      printf("  300 Task() calls with nothing changing: %d bus transactions\n", transactions - tr);
      CHECK(transactions == tr, "a static screen kept the bus busy: %d transactions in 300 calls", transactions - tr);
   }

   printf("\n2. At most one character (a cursor move and a character) per Task() call: the blocking cost\n");
   {
      int worst = 0; double worstUs = 0;
      for (int i = 0; i < 600; i++)
      {
         g_pos.X += (rand() % 41) - 20; g_pos.Y += (rand() % 41) - 20; g_tick++;
         int tr = transactions; double t0 = now_us; LcdJoystickUI_Task();
         if (transactions - tr > worst) { worst = transactions - tr; }
         if (now_us - t0 > worstUs) { worstUs = now_us - t0; }
      }
      printf("  600 calls with the position changing: at most %d transactions and %.2f ms of bus time in one call\n", worst, worstUs / 1000.0);
      CHECK(worst <= 2 && worstUs < 1000.0, "a Task() call used %d transactions, %.2f ms", worst, worstUs / 1000.0);
      settle(); CHECK(glassMatchesFrame(), "the glass does not match the frame after 600 changes");
   }

   printf("\n3. Every screen: the glass equals the frame, whatever the content does (random changes, drained at random)\n");
   {
      const UiMode_t modes[] = { LCDJOYSTICKUI_MODE_STATUS, LCDJOYSTICKUI_MODE_MENU, LCDJOYSTICKUI_MODE_JOG_CONTROL, LCDJOYSTICKUI_MODE_CAL_PARAMS, LCDJOYSTICKUI_MODE_JOG_ADJUST, LCDJOYSTICKUI_MODE_CAL_PEN, LCDJOYSTICKUI_MODE_SD_BROWSER };
      int checks = 0;
      for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++)
      {
         s_uiMode = modes[m];
         for (int i = 0; i < 400; i++)
         {
            if (rand() % 3 == 0) { g_pos.X += (rand() % 4001) - 2000; g_pos.Y += (rand() % 4001) - 2000; }
            g_tick++; LcdJoystickUI_Task();
            if (rand() % 25 == 0) { settle(); checks++; CHECK(glassMatchesFrame(), "mode %u: the glass does not match the frame (check %d)", m, checks); }
         }
         settle(); checks++; CHECK(glassMatchesFrame(), "mode %u: not converged at the end", m);
         char nm[40]; snprintf(nm, sizeof nm, "mode %u", m); dump(nm);
      }
      printf("  7 screens, %d convergence checks\n", checks);
   }

   printf("\n4. The row boundary: a change that spans the last cell of row 0 and the first of row 1\n");
   {
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();
      PrintPadded(0, "0123456789ABCDEF"); PrintPadded(1, "ghijklmnopqrstuv"); settle(); CHECK(glassMatchesFrame(), "setup");
      PrintPadded(0, "0123456789ABCDEx"); PrintPadded(1, "Xhijklmnopqrstuv"); settle();    // ONLY cells 15 and 16 changed
      dump("cells 15 and 16 changed");
      CHECK(glassMatchesFrame(), "a change across the end of row 0 did not reach the glass in place");
   }

   printf("\n4a. The step-starvation counter (CORELR_DEBUG_MISS_COUNT = %d): shown on the jog screens, or not at all\n", (int)CORELR_DEBUG_MISS_COUNT);
   {
      char a[17], b[17];
      s_uiMode = LCDJOYSTICKUI_MODE_JOG_CONTROL; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, a);
      dump("Control jog");
      CHECK((strstr(a, "miss:") != NULL) == (CORELR_DEBUG_MISS_COUNT != 0), "Control jog row 1 is '%s' with CORELR_DEBUG_MISS_COUNT = %d", a, (int)CORELR_DEBUG_MISS_COUNT);
      CHECK(CORELR_DEBUG_MISS_COUNT || (a[1] == 'L' && a[9] == 'R'), "without the counter the Control jog must show the cable lengths on row 1: '%s'", a);
      s_uiMode = LCDJOYSTICKUI_MODE_JOG_ADJUST; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, b);
      dump("Adjustment jog");
      CHECK((strstr(b, "miss:") != NULL) == (CORELR_DEBUG_MISS_COUNT != 0), "Adjustment jog row 1 is '%s'", b);
      CHECK(strncmp(b, "SW=ok", 5) == 0, "the Adjustment jog's hint is missing: '%s'", b);
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();
   }

   printf("\n4a2. The Control jog's direction: the drawing frame has Y UP (REQ-CoreLR-D-Motion-0025)\n");
   {
      /* The physical stick, as this unit reads it: HwConfig.h says whether the analog value rises (INVERT 0) or falls (INVERT 1) when the stick is pushed right / down.
       * Whatever it says, pushing the stick down must put the pen DOWN the wall (Y decreases) and pushing it right must move it right: that is what the flag is for.
       * A stick that is released does nothing. */
      const int sx = LCDJOYSTICKUI_X_INVERT ? -400 : 400, sy = LCDJOYSTICKUI_Y_INVERT ? -400 : 400;     // the raw deflection of "pushed right" and of "pushed down"
      const int ax[4] = { 512, 512, 512 + sx, 512 - sx }, ay[4] = { 512 + sy, 512 - sy, 512, 512 };
      const char *what[4] = { "stick down", "stick up", "stick right", "stick left" };
      for (int k = 0; k < 4; k++)
      {
         g_pos.X = 0; g_pos.Y = 0; g_ax = ax[k]; g_ay = ay[k]; g_nQueued = 0;
         s_uiMode = LCDJOYSTICKUI_MODE_JOG_CONTROL; s_lastJogTick = g_tick; g_tick += 6; LcdJoystickUI_Task();
         CHECK(g_nQueued == 1, "%s: %d moves queued", what[k], g_nQueued);
         CHECK(k != 0 || LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_Y_PIN) > 0, "ReadAxis(Y) must be positive with the stick pushed down");
         CHECK(k != 1 || LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_Y_PIN) < 0, "ReadAxis(Y) must be negative with the stick pushed up");
         CHECK(k != 2 || LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_X_PIN) > 0, "ReadAxis(X) must be positive with the stick pushed right");
         CHECK(k != 3 || LcdJoystickUI_ReadAxis(LCDJOYSTICKUI_X_PIN) < 0, "ReadAxis(X) must be negative with the stick pushed left");
         printf("  %-11s -> target (%ld, %ld) um\n", what[k], (long)g_queued.X, (long)g_queued.Y);
         CHECK(k != 0 || ((CORELR_Y_UP ? g_queued.Y < 0 : g_queued.Y > 0) && g_queued.X == 0), "stick down must move the pen DOWN the wall (Y < 0 if CORELR_Y_UP, else Y > 0)");
         CHECK(k != 1 || ((CORELR_Y_UP ? g_queued.Y > 0 : g_queued.Y < 0) && g_queued.X == 0), "stick up must move the pen UP the wall (Y > 0 if CORELR_Y_UP, else Y < 0)");
         CHECK(k != 2 || (g_queued.X > 0 && g_queued.Y == 0), "stick right must move the pen right");
         CHECK(k != 3 || (g_queued.X < 0 && g_queued.Y == 0), "stick left must move the pen left");
      }
      g_ax = g_ay = 512; g_pos.X = g_pos.Y = 0; s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();
   }

   printf("\n4b. Rendering: padding, clipping, replacement of longer text by shorter\n");
   {
      char a[17];
      PrintPadded(0, "a much longer line"); PrintPadded(1, "0123456789ABCDEF-clipped");
      PrintPadded(0, "short");              PrintPadded(1, "");
      settle(); dump("short, then empty");
      lcd.row(0, a); CHECK(strcmp(a, "short           ") == 0, "row 0 is '%s', not the padded 'short'", a);
      lcd.row(1, a); CHECK(strcmp(a, "                ") == 0, "row 1 is '%s', not blank", a);
      PrintPadded(1, "0123456789ABCDEF-clipped"); settle();
      lcd.row(1, a); CHECK(strcmp(a, "0123456789ABCDEF") == 0, "a 24-character text shows '%s'", a);
      PrintPadded_P(0, PSTR("from flash")); settle();
      lcd.row(0, a); CHECK(strcmp(a, "from flash      ") == 0, "a flash text shows '%s'", a);
   }

   printf("\n5. Transient faults (a NACKed address, a NACK of the first or second byte): the glass still converges, nothing hangs\n");
   {
      int injected = 0;
      for (int i = 0; i < 500; i++)
      {
         g_pos.X += (rand() % 301) - 150; g_pos.Y += (rand() % 301) - 150; g_tick++;
         int k = rand() % 8;
         if (k == 0) { f_present = 0; injected++; } else if (k == 1) { f_nackDataAt = 0; injected++; } else if (k == 2) { f_nackDataAt = 1; injected++; }
         LcdJoystickUI_Task(); f_present = 1; f_nackDataAt = -1;
      }
      settle();
      printf("  %d faults injected over 500 calls; strobes too soon %d, protocol errors %d\n", injected, lcd.tooSoon, protocolErrors);
      CHECK(glassMatchesFrame(), "after transient faults the glass does not match the frame");
      CHECK(lcd.tooSoon == 0, "%d strobes arrived too soon", lcd.tooSoon);
   }

   printf("\n6. Everything above, with no instruction sent while the controller was busy and every start matched by a stop\n");
   CHECK(lcd.tooSoon == 0 && protocolErrors == 0, "controller timing violations %d, protocol errors %d", lcd.tooSoon, protocolErrors);
   CHECK(startsSeen == stopsSeen, "starts %d, stops %d", startsSeen, stopsSeen);

   printf("\n4d. The stick as a choice (-1, 0, +1): the threshold, the hysteresis, both axes, every polarity (REQ-CoreLR-D-LcdJoystickUI-0022)\n");
   {
      /* a deflection d, in the convention of ReadAxis (positive right / down), as the unit would read it: the raw value is mirrored when the flag says so */
      #define RAWX(d) (512 + (LCDJOYSTICKUI_X_INVERT ? -(d) : (d)))
      #define RAWY(d) (512 + (LCDJOYSTICKUI_Y_INVERT ? -(d) : (d)))
      const int ON = LCDJOYSTICKUI_TERNARY_ON, OFF = LCDJOYSTICKUI_TERNARY_OFF;
      struct { int d; int want; const char *why; } seq[] = {
         { 0, 0, "centred" }, { 100, 0, "past the dead zone but short of the threshold: nothing" }, { ON - 1, 0, "one short of the threshold" }, { ON, 1, "at the threshold" },
         { ON - 1, 1, "back a little: still counted (hysteresis)" }, { OFF, 1, "at the release level: still counted" }, { OFF - 1, 0, "below the release level" },
         { ON - 1, 0, "and it must reach the threshold again" }, { ON + 100, 1, "pushed far" }, { 0, 0, "released" },
         { -ON, -1, "the other way, at the threshold" }, { -(ON - 1), -1, "back a little" }, { -(OFF - 1), 0, "released" }, { -ON, -1, "again" }, { ON, 1, "through the middle to the other side in one reading" }, { -(OFF + 10), 0, "the other side, in the band: not the old side" },
         { 511, 1, "the extreme" }, { 0, 0, "centred" } };
      unsigned i;
      for (i = 0; i < sizeof seq / sizeof seq[0]; i++)
      {
         g_ax = RAWX(seq[i].d); g_ay = 512; { int8_t got = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN); CHECK(got == seq[i].want, "X, step %u (%s): deflection %d gave %d, expected %d", i, seq[i].why, seq[i].d, got, seq[i].want); }
         g_ax = 512; g_ay = RAWY(seq[i].d); { int8_t got = LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN); CHECK(got == seq[i].want, "Y, step %u (%s): deflection %d gave %d, expected %d", i, seq[i].why, seq[i].d, got, seq[i].want); }
      }
      /* the two axes keep their own state */
      g_ax = RAWX(ON); g_ay = RAWY(0); CHECK(LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN) == 1 && LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN) == 0, "the axes are not independent");
      g_ax = RAWX(ON - 1); g_ay = RAWY(-ON); CHECK(LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN) == 1 && LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN) == -1, "the axes are not independent (2)");
      g_ax = 512; g_ay = 512; (void)LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN); (void)LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);
      printf("   threshold %d (on) / %d (off), checked for both axes, polarity flags %d/%d\n", ON, OFF, LCDJOYSTICKUI_X_INVERT, LCDJOYSTICKUI_Y_INVERT);
   }

   printf("\n4e. Menus and selections stop at their ends, they do not wrap round\n");
   {
      int i;
      s_machineState = LCDJOYSTICKUI_STATE_IDLE; g_calibrated = 1;
      s_menuSelectedIndex = 0; AdvanceMenuSelection(-1); CHECK(s_menuSelectedIndex == 0, "up from the first item went to %d", s_menuSelectedIndex);
      for (i = 0; i < 20; i++) AdvanceMenuSelection(1);
      { uint8_t last = s_menuSelectedIndex; int k; int lastAvail = -1; for (k = 0; k < (int)MENU_ITEM_COUNT; k++) if (MenuItemAvailable((MenuItem_t)k)) lastAvail = k;
        CHECK((int)last == lastAvail, "pushing down 20 times ended on item %d, the last available is %d", last, lastAvail);
        AdvanceMenuSelection(1); CHECK(s_menuSelectedIndex == last, "down from the last item wrapped to %d", s_menuSelectedIndex); }
      for (i = 0; i < 20; i++) AdvanceMenuSelection(-1);
      CHECK(s_menuSelectedIndex == 0, "pushing up 20 times ended on item %d", s_menuSelectedIndex);
      g_calibrated = 0; s_machineState = LCDJOYSTICKUI_STATE_UNCAL;                         /* the jog is not offered: the first AVAILABLE item is the end */
      s_menuSelectedIndex = (uint8_t)FirstAvailableMenuItem(); { uint8_t first = s_menuSelectedIndex; AdvanceMenuSelection(-1); CHECK(s_menuSelectedIndex == first, "up from the first available item moved to %d", s_menuSelectedIndex); }
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      s_machineState = LCDJOYSTICKUI_STATE_RUNNING;                                           /* Pause (HALT), Cancel, and Back are offered: Back is the LAST item of all */
      s_menuSelectedIndex = (uint8_t)FirstAvailableMenuItem(); CHECK(s_menuSelectedIndex == MENU_HALT, "while running the first item is %d, expected Pause", s_menuSelectedIndex);
      AdvanceMenuSelection(1); CHECK(s_menuSelectedIndex == MENU_CANCEL, "down from Pause went to %d, expected Cancel (skipping Resume)", s_menuSelectedIndex);
      AdvanceMenuSelection(1); CHECK(s_menuSelectedIndex == MENU_BACK, "down from Cancel went to %d, expected Back", s_menuSelectedIndex);
      AdvanceMenuSelection(1); CHECK(s_menuSelectedIndex == MENU_BACK, "down from Back moved to %d", s_menuSelectedIndex);
      s_machineState = LCDJOYSTICKUI_STATE_IDLE;
   }

   printf("\n4f. Adjustment: the stick SELECTS the motor (left = L, right = R, never toggles); calibration: the step size stops at its ends\n");
   {
      #define PUSH(d) do { g_ax = RAWX(d); g_ay = 512; g_tick += 3; (void)LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN); } while (0)
      const int ON = LCDJOYSTICKUI_TERNARY_ON;
      s_uiMode = LCDJOYSTICKUI_MODE_JOG_ADJUST; s_adjustSelectedMotor = STEPPER_MOTOR_R; g_ax = 512; g_ay = 512;
      g_ax = RAWX(-ON); HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_L, "left did not select L");
      g_ax = RAWX(-ON); HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_L, "left again moved the selection (a toggle)");
      g_ax = RAWX(0);   HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_L, "a centred stick changed the selection");
      g_ax = RAWX(ON);  HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_R, "right did not select R");
      g_ax = RAWX(ON);  HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_R, "right again moved the selection (a toggle)");
      g_ax = RAWX(ON - 1); HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_R, "released slightly (hysteresis) changed it");
      g_ax = RAWX(100); HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_R, "a push short of the threshold changed the selection");
      g_ax = RAWX(0);   HandleJogAdjustmentInput(0, 0, g_tick);
      g_ax = RAWX(-100); HandleJogAdjustmentInput(0, 0, g_tick); CHECK(s_adjustSelectedMotor == STEPPER_MOTOR_R, "a push to the left short of the threshold selected L");
      g_ax = RAWX(0);   HandleJogAdjustmentInput(0, 0, g_tick);

      s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; s_calMachineStep = 0; s_calMachineStepIndex = 0; s_xWasDeflected = 0; g_ax = 512; g_ay = 512;
      { int i, ok = 1;
        g_ax = RAWX(-ON); HandleCalMachineParamsInput(0, 0, g_tick); CHECK(s_calMachineStepIndex == 0, "left from the finest step wrapped to %d", s_calMachineStepIndex);
        g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick);
        for (i = 0; i < 5; i++) { g_ax = RAWX(ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick); }
        CHECK(s_calMachineStepIndex == LCDJOYSTICKUI_CAL_STEP_COUNT - 1, "five pushes right ended on step %d (the coarsest is %d: it must stop there, not wrap)", s_calMachineStepIndex, LCDJOYSTICKUI_CAL_STEP_COUNT - 1);
        g_ax = RAWX(ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(ON); HandleCalMachineParamsInput(0, 0, g_tick);
        CHECK(s_calMachineStepIndex == LCDJOYSTICKUI_CAL_STEP_COUNT - 1, "holding right moved the step more than once");
        g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick);
        g_ax = RAWX(-ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(-ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(-ON); HandleCalMachineParamsInput(0, 0, g_tick);
        CHECK(s_calMachineStepIndex == LCDJOYSTICKUI_CAL_STEP_COUNT - 2, "holding left from the coarsest moved to step %d (expected one step, to %d)", s_calMachineStepIndex, LCDJOYSTICKUI_CAL_STEP_COUNT - 2);
        g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick);
        g_ax = RAWX(ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick);
        for (i = 0; i < 5; i++) { g_ax = RAWX(-ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ax = RAWX(0); HandleCalMachineParamsInput(0, 0, g_tick); }
        CHECK(s_calMachineStepIndex == 0, "five pushes left ended on step %d", s_calMachineStepIndex); (void)ok; }
      /* "Motor dist" / "Ref length": the stick UP increases the value by the step, DOWN decreases it, and it never goes below 0 */
      { int32_t v0; s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; s_calMachineStep = 0; s_calMachineStepIndex = 1; s_calMachineValueUm = 500000L; g_ax = 512; s_xWasDeflected = 0;
        v0 = s_calMachineValueUm; g_tick += 30; g_ay = RAWY(-ON); HandleCalMachineParamsInput(0, 0, g_tick);
        CHECK(s_calMachineValueUm == v0 + 10000L, "stick up changed the value by %ld um, expected +10000", (long)(s_calMachineValueUm - v0));
        v0 = s_calMachineValueUm; g_tick += 30; g_ay = RAWY(ON); HandleCalMachineParamsInput(0, 0, g_tick);
        CHECK(s_calMachineValueUm == v0 - 10000L, "stick down changed the value by %ld um, expected -10000", (long)(s_calMachineValueUm - v0));
        s_calMachineValueUm = 5000L; g_tick += 30; g_ay = RAWY(ON); HandleCalMachineParamsInput(0, 0, g_tick);
        CHECK(s_calMachineValueUm == 0, "the value went to %ld um (a distance cannot be negative)", (long)s_calMachineValueUm);
        g_ay = 512; }
#if LCDJOYSTICKUI_HAS_SD
      { int i; s_uiMode = LCDJOYSTICKUI_MODE_SD_BROWSER; s_browserTotal = 5; s_browserIndex = 0; g_ax = 512;
        g_ay = RAWY(-ON); g_tick += 30; HandleSdBrowserInput(0, 0, g_tick); CHECK(s_browserIndex == 0, "up from the first file went to %d", s_browserIndex);
        for (i = 0; i < 7; i++) { g_ay = RAWY(ON); g_tick += 30; HandleSdBrowserInput(0, 0, g_tick); }
        CHECK(s_browserIndex == 4, "seven pushes down the list of five ended on %d: it must stop on the last", s_browserIndex);
        g_ay = 512; }
      /* a directory whose name would not fit the path: the screen says so, the entry stays selected, any move ends the message (REQ-CoreLR-D-SDReader-0011) */
      { char a[17], b[17]; s_uiMode = LCDJOYSTICKUI_MODE_SD_BROWSER; s_browserTotal = 5; s_browserIndex = 2; g_sdIsDir = 1; s_browserCurrentIsDirectory = 1; g_sdRefuse = 1;
        HandleSdBrowserInput(1, 0, g_tick); CHECK(s_browserRefused == 1 && s_browserIndex == 2, "a refused descent: refused %d, index %d (it must stay on 2)", s_browserRefused, s_browserIndex);
        g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, b); lcd.row(0, a);
        CHECK(strncmp(b, "Path too long", 13) == 0, "row 1 shows '%s'", b);
        g_sdRefuse = 0; g_ay = RAWY(ON); g_tick += 30; HandleSdBrowserInput(0, 0, g_tick); g_ay = 512;
        CHECK(s_browserRefused == 0 && s_browserIndex == 3, "a move did not end the message: refused %d, index %d", s_browserRefused, s_browserIndex);
        g_sdRefuse = 0; HandleSdBrowserInput(1, 0, g_tick); CHECK(s_browserRefused == 0 && s_browserIndex == 0, "an accepted descent: refused %d, index %d (the list restarts at 0)", s_browserRefused, s_browserIndex);
        g_sdIsDir = 0; s_browserCurrentIsDirectory = 0; }
#endif
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; g_ax = 512; g_ay = 512; (void)LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_X_PIN); (void)LcdJoystickUI_ReadAxisTernary(LCDJOYSTICKUI_Y_PIN);
   }

   printf("\n4c. %s variant: the SD browser is %s, and LcdJoystickUI_IsBusy() says when a mode can move the machine\n",
          LCDJOYSTICKUI_HAS_SD ? "SD" : "Serial", LCDJOYSTICKUI_HAS_SD ? "there" : "gone (FW-08)");
   {
      uint8_t n = 0, i; const UiMode_t busy[] = { LCDJOYSTICKUI_MODE_JOG_CONTROL, LCDJOYSTICKUI_MODE_CAL_PARAMS, LCDJOYSTICKUI_MODE_JOG_ADJUST, LCDJOYSTICKUI_MODE_CAL_PEN };
      s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      for (i = 0; i < (uint8_t)MENU_ITEM_COUNT; i++) if (MenuItemAvailable((MenuItem_t)i)) n++;
      printf("   %d menu items offered while idle (calibrated)\n", n);
      CHECK(n == (LCDJOYSTICKUI_HAS_SD ? 4 : 3) + CORELR_MENU_HOME + 1 + 2, "%d items offered while idle: expected %d (the two origin items included)", n, (LCDJOYSTICKUI_HAS_SD ? 4 : 3) + CORELR_MENU_HOME + 1 + 2);
#if !LCDJOYSTICKUI_HAS_SD
      CHECK((int)MENU_ITEM_COUNT == 9 + CORELR_MENU_HOME, "the serial menu has %d entries, expected %d (no 'Load file'; the two origin items included)", (int)MENU_ITEM_COUNT, 9 + CORELR_MENU_HOME);
#endif
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; CHECK(!LcdJoystickUI_IsBusy(), "the status screen is not busy");
      s_uiMode = LCDJOYSTICKUI_MODE_MENU;   CHECK(!LcdJoystickUI_IsBusy(), "the menu is not busy");
#if CORELR_MENU_HOME
      s_uiMode = LCDJOYSTICKUI_MODE_HOMING; CHECK(LcdJoystickUI_IsBusy(), "\"Return home\" moves the machine but is not busy");
#endif
      for (i = 0; i < sizeof busy / sizeof busy[0]; i++) { s_uiMode = busy[i]; CHECK(LcdJoystickUI_IsBusy(), "mode %d can move the machine but is not busy", (int)busy[i]); }
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }

   printf("\n4g. Lengths on the LCD: fixed decimal columns, one decimal, nearest tenth, no \"-0.0\" (REQ-CoreLR-D-LcdJoystickUI-0023; LCDJOYSTICKUI_DISPLAY_CAP = %d)\n", (int)LCDJOYSTICKUI_DISPLAY_CAP);
   {
      struct V { int32_t um; uint8_t sign; const char *want; };
      /* each want is the first 8 columns (0..7) when the point is at column 6, rest blank */
      const struct V v[] = {
         {  123400L, 1, "   123.4" }, { -123400L, 1, "  -123.4" }, {       0L, 1, "     0.0" }, {  -40L, 1, "     0.0" },
         {      49L, 1, "     0.0" }, {      50L, 1, "     0.1" }, {     -49L, 1, "     0.0" }, {  -50L, 1, "    -0.1" },
         {      99L, 1, "     0.1" }, {     149L, 1, "     0.1" }, {     150L, 1, "     0.2" }, { -150L, 1, "    -0.2" },
         { 1234500L, 1, "  1234.5" }, { -1234500L, 1, " -1234.5" }, { 2097151L, 1, "  2097.2" }, { -2097151L, 1, " -2097.2" },
         { -123400L, 0, "   123.4" }, { 4000000L, 0, "  4000.0" }, {  9999L, 1, "    10.0" }, { 99950L, 1, "   100.0" },
      };
      char want[17];
      unsigned i;
      for (i = 0; i < sizeof v / sizeof v[0]; i++)
      {
         ClearCompose(); PutMm(6, v[i].um, v[i].sign);
         snprintf(want, sizeof want, "%s        ", v[i].want);            /* 8 + 8 blanks */
         CHECK(strcmp(s_composeLine, want) == 0, "%ld um (sign %d): '%s', expected '%s'", (long)v[i].um, v[i].sign, s_composeLine, want);
         ClearCompose(); PutMm(14, v[i].um, v[i].sign);                    /* the same at the second point */
         snprintf(want, sizeof want, "        %s", v[i].want);
         CHECK(strcmp(s_composeLine, want) == 0, "%ld um at column 14: '%s', expected '%s'", (long)v[i].um, s_composeLine, want);
      }
      /* A sweep against an independent reference (llround: half away from zero): the point is always where it should be, nothing is written outside the field */
      {
         long n = 0, bad = 0;
         int32_t um;
         for (um = -2100000L; um <= 2100000L; um++)       /* every micrometre, so every rounding edge */
         {
            long tenths = llround(fabs((double)um) / 100.0);
            char ref[24]; int len;
            for (int col = 6; col <= 14; col += 8)
            {
               ClearCompose(); PutMm((uint8_t)col, um, 1);
               len = snprintf(ref, sizeof ref, "%s%ld.%ld", (um < 0 && tenths != 0) ? "-" : "", tenths / 10, tenths % 10);
               char exp[17]; memset(exp, ' ', 16); exp[16] = 0;
               memcpy(exp + col + 2 - len, ref, (size_t)len);
               n++;
               if (strcmp(s_composeLine, exp) != 0 || s_composeLine[col] != '.') { if (bad++ < 5) { CHECK(0, "%ld um at point %d: '%s', expected '%s'", (long)um, col, s_composeLine, exp); } }
            }
         }
         printf("  sweep -2100.0..2100.0 mm: %ld renderings, %ld wrong\n", n, bad);
         CHECK(bad == 0, "%ld wrong renderings in the sweep", bad);
         /* lengths: 0..4200 mm (the 48-bit arithmetic's limit is 4194.3), no sign */
         bad = 0; n = 0;
         for (um = 0; um <= 4200000L; um += 37)
         {
            long tenths = llround((double)um / 100.0);
            char ref[24]; int len = snprintf(ref, sizeof ref, "%ld.%ld", tenths / 10, tenths % 10);
            char exp[17]; memset(exp, ' ', 16); exp[16] = 0;
            ClearCompose(); PutMm(14, um, 0); memcpy(exp + 16 - len, ref, (size_t)len); n++;
            if (strcmp(s_composeLine, exp) != 0) { if (bad++ < 5) { CHECK(0, "%ld um: '%s', expected '%s'", (long)um, s_composeLine, exp); } }
         }
         printf("  sweep 0..4200.0 mm: %ld renderings, %ld wrong\n", n, bad);
         CHECK(bad == 0, "%ld wrong renderings in the length sweep", bad);
      }
      /* Whether a cap must exist is worked out here from the configuration, not read back from the code under test (a mutant of the condition would
       * otherwise also switch off the test that should catch it): 64-bit arithmetic lets any int32 length through; 48-bit stops at 4194.3 mm. */
#define TEST_CAN_EXCEED ((CORELR_MATH == CORELR_MATH_64) || (LCDJOYSTICKUI_CAL_MAX_UM >= 9999950L))
      CHECK(LCDJOYSTICKUI_DISPLAY_CAP == (TEST_CAN_EXCEED ? 1 : 0), "LCDJOYSTICKUI_DISPLAY_CAP is %d but this configuration %s reach 9999.95 mm", (int)LCDJOYSTICKUI_DISPLAY_CAP, TEST_CAN_EXCEED ? "can" : "cannot");
#if TEST_CAN_EXCEED
      {
         const int32_t big[] = { 9999900L, 9999949L, 9999950L, 10000000L, 123456789L, 2147483647L };
         for (i = 0; i < sizeof big / sizeof big[0]; i++)
         {
            ClearCompose(); PutMm(6, big[i], 0); CHECK(strncmp(s_composeLine, "  9999.9", 8) == 0 || big[i] < 9999950L, "%ld um shown as '%s' (must be capped to 9999.9)", (long)big[i], s_composeLine);
            ClearCompose(); PutMm(6, -big[i], 1); CHECK(strncmp(s_composeLine + 1, "-9999.9", 7) == 0, "-%ld um shown as '%s'", (long)big[i], s_composeLine);
         }
         ClearCompose(); PutMm(6, -2147483647L - 1L, 1); CHECK(strncmp(s_composeLine + 1, "-9999.9", 7) == 0, "INT32_MIN shown as '%s'", s_composeLine);
      }
#else
      printf("  no cap needed: every value this configuration can reach fits (reach %ld um, lengths %ld um, entry %ld um)\n", (long)LCDJOYSTICKUI_JOG_LIMIT_UM, (long)LCDJOYSTICKUI_LENGTH_BOUND_UM, (long)LCDJOYSTICKUI_CAL_MAX_UM);
#endif
   }

   printf("\n4h. The screens, as the glass shows them\n");
   {
      char r0[17], r1[17], want[17];
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      g_pos.X = 123400L; g_pos.Y = -123400L; g_tick += 6; settle(); LcdJoystickUI_Task(); settle();
      lcd.row(0, r0); lcd.row(1, r1); dump("status");
      snprintf(want, sizeof want, "%c  123.4  -123.4", LCDJOYSTICKUI_TAG_IDLE);
      CHECK(strcmp(r0, want) == 0, "status row 0 is '%s', expected '%s'", r0, want);
      snprintf(want, sizeof want, "%cL 761.7 R 535.5", LCDJOYSTICKUI_PEN_UP_CHAR);
      CHECK(strcmp(r1, want) == 0, "status row 1 (the pen up glyph in column 0) is '%s'", r1);
      g_pos.X = -5L; g_pos.Y = 1234567L; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      snprintf(want, sizeof want, "%c    0.0  1234.6", LCDJOYSTICKUI_TAG_IDLE);        /* -5 um is -0.0: no sign; the point of Y stays at 14 */
      CHECK(strcmp(r0, want) == 0, "status row 0 is '%s', expected '%s'", r0, want);
      CHECK(r0[6] == '.' && r0[14] == '.', "the points of '%s' are not at columns 6 and 14", r0);
      g_pos.X = -123400L; g_pos.Y = -5L; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      snprintf(want, sizeof want, "%c -123.4     0.0", LCDJOYSTICKUI_TAG_IDLE);        /* the sign of X touches its first digit, never the tag */
      CHECK(strcmp(r0, want) == 0, "status row 0 is '%s', expected '%s'", r0, want);
      s_machineState = LCDJOYSTICKUI_STATE_ERROR; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      CHECK(r0[0] == LCDJOYSTICKUI_TAG_ERROR, "the tag is in column 0: '%s'", r0);
      s_machineState = LCDJOYSTICKUI_STATE_IDLE;

      s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; s_calMachineStep = 0; s_calMachineStepIndex = 2; s_calMachineValueUm = 1234500L;
      g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0); lcd.row(1, r1); dump("Motor dist");
      CHECK(strcmp(r0, "Motor dist (mm) ") == 0, "wizard row 0 is '%s'", r0);
      CHECK(strcmp(r1, "  1234.5 +-100mm") == 0, "wizard row 1 is '%s'", r1);
      s_calMachineStep = 1; s_calMachineStepIndex = 0; s_calMachineValueUm = 700000L;
      g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0); lcd.row(1, r1);
      CHECK(strcmp(r0, "Ref length (mm) ") == 0, "wizard row 0 is '%s'", r0);
      CHECK(strcmp(r1, "   700.0 +-1mm  ") == 0, "wizard row 1 is '%s'", r1);
      /* the entry stops at its configured maximum */
      s_calMachineStepIndex = 2; s_calMachineValueUm = LCDJOYSTICKUI_CAL_MAX_UM - 50000L; s_xWasDeflected = 0; g_ax = 512;
      for (int k = 0; k < 4; k++) { g_tick += 30; g_ay = RAWY(-LCDJOYSTICKUI_TERNARY_ON); HandleCalMachineParamsInput(0, 0, g_tick); }
      CHECK(s_calMachineValueUm == LCDJOYSTICKUI_CAL_MAX_UM, "four pushes up from MAX - 50 mm gave %ld, not the maximum %ld", (long)s_calMachineValueUm, (long)LCDJOYSTICKUI_CAL_MAX_UM);
      g_ay = 512; s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }

#if CORELR_MENU_HOME
   printf("\n4i. \"Return home\": second in the menu, offered like the jog, pen up first, one move to (0,0), SW ramps it down and drops the rest\n");
   {
      char r0[17];
      #define HOME_RESET() do { g_penUpRequests = g_halts = g_aborts = g_calls = g_penAt = g_queueAt = g_nQueued = 0; g_moving = g_plannerCount = 0; g_haltRampDone = 1; g_plannerRoom = 1; g_queueRefuse = 0; \
                                s_homeQueued = s_homeStopping = 0; g_pos.X = 123400L; g_pos.Y = -45000L; g_queued.X = g_queued.Y = 777; } while (0)
      #define HOME_ENTER() do { HOME_RESET(); s_machineState = LCDJOYSTICKUI_STATE_IDLE; g_calibrated = 1; s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_HOME); } while (0)

      CHECK((int)MENU_HOME == 1, "\"Return home\" is entry %d of the menu, expected the second (1)", (int)MENU_HOME);
      CHECK(strcmp((const char *)pgm_read_word(&kMenuLabels[MENU_HOME]), "Return home") == 0, "label '%s'", (const char *)pgm_read_word(&kMenuLabels[MENU_HOME]));
      /* availability: as the jog */
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE;     CHECK(MenuItemAvailable(MENU_HOME), "not offered while idle and calibrated");
      g_calibrated = 0; s_machineState = LCDJOYSTICKUI_STATE_UNCAL;    CHECK(!MenuItemAvailable(MENU_HOME), "offered while uncalibrated (the origin is unknown)");
      g_calibrated = 1;
      s_machineState = LCDJOYSTICKUI_STATE_RUNNING;                    CHECK(!MenuItemAvailable(MENU_HOME), "offered while a job runs");
      s_machineState = LCDJOYSTICKUI_STATE_PAUSED;                     CHECK(!MenuItemAvailable(MENU_HOME), "offered while a job is paused");
      s_machineState = LCDJOYSTICKUI_STATE_ERROR;                      CHECK(!MenuItemAvailable(MENU_HOME), "offered in the error state");
      /* uncalibrated: entering does nothing (defensive) */
      HOME_RESET(); g_calibrated = 0; s_machineState = LCDJOYSTICKUI_STATE_UNCAL; s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_HOME);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_MENU && g_penUpRequests == 0, "entered \"Return home\" while uncalibrated");

      /* the normal run: pen first, then the move to the origin, then back to the status screen when everything is done */
      HOME_ENTER();
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_HOMING && g_penUpRequests == 1, "entering: mode %d, %d pen-up requests", (int)s_uiMode, g_penUpRequests);
      HandleHomingInput(0);
      CHECK(g_nQueued == 1 && g_queued.X == 0 && g_queued.Y == 0, "queued %d move(s) to (%ld,%ld), expected one to (0,0)", g_nQueued, (long)g_queued.X, (long)g_queued.Y);
      CHECK(g_penAt < g_queueAt, "the move was queued (call %d) before the pen request (call %d)", g_queueAt, g_penAt);
      HandleHomingInput(0); HandleHomingInput(0); CHECK(g_nQueued == 1, "the move was queued %d times", g_nQueued);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "left the mode with the move still in the planner");
      g_plannerCount = 0; g_moving = 1; HandleHomingInput(0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "left the mode while the move is being generated");
      g_moving = 0; g_plannerCount = 1; HandleHomingInput(0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "left the mode while a block is still planned (idle generator is not done)");
      g_plannerCount = 0; HandleHomingInput(0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS && g_aborts == 0 && g_halts == 0, "arrival: mode %d, %d aborts, %d halts", (int)s_uiMode, g_aborts, g_halts);

      /* SW: ramp down first, drop the rest only once the ramp is enqueued; a second press changes nothing */
      HOME_ENTER(); HandleHomingInput(0); g_plannerCount = 0; g_moving = 1; g_haltRampDone = 0;
      HandleHomingInput(1);
      CHECK(g_halts == 1 && g_aborts == 0 && s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "press: %d halts, %d aborts, mode %d", g_halts, g_aborts, (int)s_uiMode);
      HandleHomingInput(0); HandleHomingInput(1);
      CHECK(g_halts == 1 && g_aborts == 0 && s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "while ramping: %d halts, %d aborts, mode %d", g_halts, g_aborts, (int)s_uiMode);
      g_haltRampDone = 1; HandleHomingInput(0);
      CHECK(g_aborts == 1 && s_uiMode == LCDJOYSTICKUI_MODE_STATUS && g_nQueued == 1, "ramp done: %d aborts, mode %d", g_aborts, (int)s_uiMode);

      /* a full planner: queue as soon as there is room, and again if the planner refuses */
      HOME_ENTER(); g_plannerRoom = 0; HandleHomingInput(0); HandleHomingInput(0);
      CHECK(g_nQueued == 0 && s_uiMode == LCDJOYSTICKUI_MODE_HOMING, "queued %d moves into a full planner", g_nQueued);
      g_plannerRoom = 1; g_queueRefuse = 1; HandleHomingInput(0);
      CHECK(g_nQueued == 0 && !s_homeQueued, "a refused move counted as queued");
      g_queueRefuse = 0; HandleHomingInput(0);
      CHECK(g_nQueued == 1 && s_homeQueued, "the move was not queued once the planner had room");
      /* SW before the move was ever queued: stop, queue nothing */
      HOME_ENTER(); g_plannerRoom = 0; HandleHomingInput(1);          /* nothing in motion: the ramp is trivially done, so it ends at once */
      CHECK(g_nQueued == 0 && g_halts == 1 && g_aborts == 1 && s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "press before queueing: %d queued, %d halts, %d aborts, mode %d", g_nQueued, g_halts, g_aborts, (int)s_uiMode);

      /* a press at the very first call, with room in the planner: it stops, it does not queue the move first */
      HOME_ENTER(); g_haltRampDone = 0; HandleHomingInput(1);
      CHECK(g_nQueued == 0 && g_halts == 1, "pressed at once: %d moves queued, %d halts", g_nQueued, g_halts);
      /* through Task(), the real SW reading: a short press and a long press both stop it */
      for (int kind = 0; kind < 2; kind++)
      {
         HOME_ENTER(); LcdJoystickUI_Task(); g_plannerCount = 0; g_moving = 1; g_haltRampDone = 0;
         g_sw = 1; g_tick++; LcdJoystickUI_Task(); g_tick += kind ? (LCDJOYSTICKUI_LONGPRESS_TICKS + 2) : 1; LcdJoystickUI_Task();
         g_sw = 0; g_tick++; LcdJoystickUI_Task();
         CHECK(g_halts == 1, "%s press of SW: %d halts", kind ? "a long" : "a short", g_halts);
      }
      g_sw = 0; g_moving = 0; g_haltRampDone = 1; HandleHomingInput(0); s_uiMode = LCDJOYSTICKUI_MODE_STATUS;

      /* what the glass shows: the status layout, tag H while it lasts */
      HOME_ENTER(); HandleHomingInput(0); g_plannerCount = 0; g_moving = 1; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0); dump("Return home");
      CHECK(r0[0] == LCDJOYSTICKUI_TAG_HOMING && r0[6] == '.' && r0[14] == '.', "row 0 while homing: '%s'", r0);
      g_moving = 0; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS && r0[0] == LCDJOYSTICKUI_TAG_IDLE, "after arriving: mode %d, row 0 '%s'", (int)s_uiMode, r0);
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE; s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }
#endif

   printf("\n4j. Control jog: tag J while it lasts, and either press of SW leaves it for the status screen, not the menu (REQ-CoreLR-D-LcdJoystickUI-0025)\n");
   {
      char r0[17];
      for (int kind = 0; kind < 3; kind++)           /* 0: short press, 1: long press, 2: no press */
      {
         g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE; g_ax = 512; g_ay = 512; g_sw = 0;
         s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_JOG);
         CHECK(s_uiMode == LCDJOYSTICKUI_MODE_JOG_CONTROL, "the jog was not entered");
         g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
         CHECK(r0[0] == LCDJOYSTICKUI_TAG_JOG, "row 0 in the jog: '%s' (tag expected '%c')", r0, LCDJOYSTICKUI_TAG_JOG);
         if (kind == 2) { g_tick += 6; LcdJoystickUI_Task(); CHECK(s_uiMode == LCDJOYSTICKUI_MODE_JOG_CONTROL, "left the jog without a press"); continue; }
         g_sw = 1; g_tick++; LcdJoystickUI_Task(); g_tick += kind ? (LCDJOYSTICKUI_LONGPRESS_TICKS + 2) : 1; LcdJoystickUI_Task();
         g_sw = 0; g_tick++; LcdJoystickUI_Task();
         CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "%s press in the jog: mode %d, expected the status screen (%d)", kind ? "a long" : "a short", (int)s_uiMode, (int)LCDJOYSTICKUI_MODE_STATUS);
         g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
         CHECK(r0[0] == LCDJOYSTICKUI_TAG_IDLE, "row 0 after the jog: '%s'", r0);
      }
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }

   printf("\n4k. Menu: a last entry \"Back\" in every state; the menu gives way to the status screen when the machine's state changes under it (REQ-CoreLR-D-LcdJoystickUI-0026)\n");
   {
      const LcdJoystickUI_MachineState_t states[] = { LCDJOYSTICKUI_STATE_IDLE, LCDJOYSTICKUI_STATE_UNCAL, LCDJOYSTICKUI_STATE_RUNNING, LCDJOYSTICKUI_STATE_PAUSED, LCDJOYSTICKUI_STATE_ERROR };
      const char *label = (const char *)pgm_read_word(&kMenuLabels[MENU_BACK]);
      unsigned i;
      char r0[17];
      CHECK((int)MENU_BACK == (int)MENU_ITEM_COUNT - 1, "Back is entry %d of %d: it must be the last", (int)MENU_BACK, (int)MENU_ITEM_COUNT);
      CHECK(label[0] == (char)LCDJOYSTICKUI_BACK_ARROW && strcmp(label + 1, " Back") == 0, "the label of Back is '%s'", label);
      for (i = 0; i < sizeof states / sizeof states[0]; i++)
      {
         g_calibrated = (states[i] != LCDJOYSTICKUI_STATE_UNCAL); s_machineState = states[i];
         CHECK(MenuItemAvailable(MENU_BACK), "Back is not offered in state %d", (int)states[i]);
         s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_BACK);
         CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "Back in state %d led to mode %d", (int)states[i], (int)s_uiMode);
      }
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      /* chosen with a press, from the menu proper */
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; s_menuSelectedIndex = MENU_BACK; HandleMainMenuInput(1, 0, g_tick);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "a short press on Back left the menu for mode %d", (int)s_uiMode);
      /* and drawn: the arrow, then the word */
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; s_menuSelectedIndex = MENU_BACK; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(0, r0); dump("Back");
      CHECK(r0[0] == (char)LCDJOYSTICKUI_BACK_ARROW && strncmp(r0 + 1, " Back", 5) == 0, "row 0 on Back: '%s'", r0);
      /* the state changes under an open menu (a job started from the serial link): back to the status screen */
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_IDLE);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_MENU, "the menu closed although the state did not change");
      LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_RUNNING);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS && s_machineState == LCDJOYSTICKUI_STATE_RUNNING, "a job started under the menu: mode %d, state %d", (int)s_uiMode, (int)s_machineState);
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_PAUSED); CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "a pause under the menu left mode %d", (int)s_uiMode);
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_ERROR); CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "an error under the menu left mode %d", (int)s_uiMode);
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_IDLE); CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS, "the end of a job under the menu left mode %d", (int)s_uiMode);
      /* any other mode stays where it is */
      {
         const UiMode_t others[] = { LCDJOYSTICKUI_MODE_STATUS, LCDJOYSTICKUI_MODE_JOG_CONTROL, LCDJOYSTICKUI_MODE_CAL_PARAMS, LCDJOYSTICKUI_MODE_JOG_ADJUST, LCDJOYSTICKUI_MODE_CAL_PEN };
         for (i = 0; i < sizeof others / sizeof others[0]; i++)
         {
            s_uiMode = others[i]; s_machineState = LCDJOYSTICKUI_STATE_IDLE; LcdJoystickUI_SetMachineState(LCDJOYSTICKUI_STATE_RUNNING);
            CHECK(s_uiMode == others[i], "mode %d was changed by a state change", (int)others[i]);
         }
      }
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
   }

   printf("\n4l. The Adjustment jog makes the machine uncalibrated at its first raw step (REQ-CoreLR-D-Calibration-0009)\n");
   {
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE; g_invalidations = 0;
      s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; EnterJogAdjustment();
      CHECK(g_invalidations == 0 && g_calibrated == 1, "entering the Adjustment jog must not invalidate by itself (%d)", g_invalidations);
      g_ax = 512; g_ay = 512; g_tick += 10; HandleJogAdjustmentInput(0, 0, g_tick);
      CHECK(g_invalidations == 0 && g_calibrated == 1, "a centred stick moved nothing yet the calibration was invalidated");
      g_ay = RAWY(400); g_tick += 10; HandleJogAdjustmentInput(0, 0, g_tick);
      CHECK(g_invalidations == 1 && g_calibrated == 0, "the first raw step: %d invalidations, calibrated %d", g_invalidations, g_calibrated);
      g_tick += 10; HandleJogAdjustmentInput(0, 0, g_tick); g_tick += 10; HandleJogAdjustmentInput(0, 0, g_tick);
      CHECK(g_invalidations == 1, "later steps invalidated again (%d): once is the call, then the flag is already clear", g_invalidations);
      g_ay = 512; HandleJogAdjustmentInput(0, 0, g_tick);
      /* a long press leaves the mode: the machine stays uncalibrated (nothing re-marks it) */
      HandleJogAdjustmentInput(0, 1, g_tick);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_MENU && g_calibrated == 0, "after a cancel: mode %d, calibrated %d", (int)s_uiMode, g_calibrated);
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE; s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
   }

   printf("\n4m. The L/R jog's speed is the Control jog's shape mapped onto its step-rate range (REQ-CoreLR-D-LcdJoystickUI-0027)\n");
   {
      int d, bad = 0, mono = 1; uint16_t prev = 0;
      CHECK(AdjustTargetRate(0) == LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S && AdjustTargetRate(LCDJOYSTICKUI_DEADZONE) == LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S,
            "inside the dead zone the rate is %u, expected the minimum %u", AdjustTargetRate(0), LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S);
      CHECK(AdjustTargetRate(LCDJOYSTICKUI_STICK_FULL_SCALE) == LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S - 0 || AdjustTargetRate(LCDJOYSTICKUI_STICK_FULL_SCALE) == LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S - 1,
            "at full scale the rate is %u, expected %u", AdjustTargetRate(LCDJOYSTICKUI_STICK_FULL_SCALE), LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S);
      CHECK(AdjustTargetRate(-LCDJOYSTICKUI_STICK_FULL_SCALE) == AdjustTargetRate(LCDJOYSTICKUI_STICK_FULL_SCALE), "the rate depends on the sign of the deflection");
      for (d = 0; d <= 511; d++)
      {
         uint16_t r = AdjustTargetRate((int16_t)d);
         double n = (d <= LCDJOYSTICKUI_DEADZONE) ? 0.0 : (double)((d > LCDJOYSTICKUI_STICK_FULL_SCALE ? LCDJOYSTICKUI_STICK_FULL_SCALE : d) - LCDJOYSTICKUI_DEADZONE) / (double)(LCDJOYSTICKUI_STICK_FULL_SCALE - LCDJOYSTICKUI_DEADZONE);
         double want = LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S + (LCDJOYSTICKUI_ADJUST_MAX_STEPS_PER_S - LCDJOYSTICKUI_ADJUST_MIN_STEPS_PER_S) * n * n;
         if (r < prev) mono = 0;
         prev = r;
         if (fabs((double)r - want) > 1.5) bad++;
      }
      CHECK(mono, "the rate fell while the stick was pushed further");
      CHECK(bad == 0, "%d deflections are more than 1.5 steps/s from MIN + (MAX - MIN) * n^2", bad);
   }

   printf("\n4n. The travel zone (REQ-CoreLR-D-LcdJoystickUI-0029): the Control jog's slices are zone-aware; the wizard refuses a geometry the machine cannot work in\n");
   {
      const int sx = LCDJOYSTICKUI_X_INVERT ? -400 : 400;                                                  // "pushed right"
      g_pos.X = 0; g_pos.Y = 0; g_ax = 512 + sx; g_ay = 512; g_nQueued = 0; g_nJogQueued = 0;
      s_uiMode = LCDJOYSTICKUI_MODE_JOG_CONTROL; s_lastJogTick = g_tick; g_tick += 6; LcdJoystickUI_Task();
      CHECK(g_nJogQueued == 1 && g_nQueued == 1 && g_queued.X > 0, "a jog slice must go through Motion_QueueJog (the zone-aware call): %d via QueueJog, %d queued, X = %ld", g_nJogQueued, g_nQueued, (long)g_queued.X);
      g_ax = g_ay = 512; s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();

      // the wizard: step 2's confirming press is refused by Calibration_SetParams(): stay, say so, accept once the value changes to one it takes
      char r0[17];
      s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; s_calMachineStep = 1; s_calMachineMotorDistanceUm = 1000000L; s_calMachineValueUm = 400000L; s_calMachineRefused = 0;
      s_calMachineStepIndex = 1; s_xWasDeflected = 0; g_ax = 512; g_ay = 512; g_setParamsRefuse = 1; g_setParamsCalls = 0;
      HandleCalMachineParamsInput(1, 0, g_tick);
      CHECK(g_setParamsCalls == 1 && s_uiMode == LCDJOYSTICKUI_MODE_CAL_PARAMS && s_calMachineStep == 1 && s_calMachineRefused == 1,
            "a refused geometry: SetParams called %d time(s), mode %d, step %d, refused flag %d (expected: stay on step 1, flag set)", g_setParamsCalls, (int)s_uiMode, (int)s_calMachineStep, (int)s_calMachineRefused);
      g_tick += 6; LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      CHECK(strstr(r0, "out of zone") != NULL, "row 0 after a refusal: '%s' (expected 'Ref out of zone!')", r0);
      HandleCalMachineParamsInput(1, 0, g_tick);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_CAL_PARAMS && g_setParamsCalls == 2, "pressing again with the same value left the step (mode %d, %d calls)", (int)s_uiMode, g_setParamsCalls);
      g_tick += 30; g_ay = RAWY(-LCDJOYSTICKUI_TERNARY_ON); HandleCalMachineParamsInput(0, 0, g_tick); g_ay = 512;
      CHECK(s_calMachineRefused == 0, "changing the value did not clear the refusal");
      g_tick += 6; LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      CHECK(strstr(r0, "out of zone") == NULL && strstr(r0, "Ref length") != NULL, "row 0 after changing the value: '%s'", r0);
      s_calMachineRefused = 1; s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_CAL_MACHINE);
      CHECK(s_calMachineRefused == 0 && s_calMachineStep == 0, "entering the wizard again kept an old refusal (flag %d, step %d)", (int)s_calMachineRefused, (int)s_calMachineStep);
      s_uiMode = LCDJOYSTICKUI_MODE_CAL_PARAMS; s_calMachineStep = 1; s_calMachineMotorDistanceUm = 1000000L; s_calMachineValueUm = 400000L;
      g_setParamsRefuse = 0; HandleCalMachineParamsInput(1, 0, g_tick);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_JOG_ADJUST && s_calMachineRefused == 0, "an accepted geometry did not lead to step 3 (mode %d)", (int)s_uiMode);
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();
   }

   printf("\n4p. \"Set origin\" and \"Origin = calib.\" (REQ-CoreLR-D-LcdJoystickUI-0031)\n");
   {
      char r0[17], r1[17];
      #define LABEL(item) ((const char *)pgm_read_word(&kMenuLabels[item]))
      CHECK(strcmp("Set origin", LABEL(MENU_SET_ORIGIN)) == 0 && strcmp("Origin = calib.", LABEL(MENU_ORIGIN_CAL)) == 0, "the menu labels are '%s' and '%s'", LABEL(MENU_SET_ORIGIN), LABEL(MENU_ORIGIN_CAL));
      // availability: like the jog -- the position must be known, and no job running or paused
      g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      CHECK(MenuItemAvailable(MENU_SET_ORIGIN) && MenuItemAvailable(MENU_ORIGIN_CAL), "the origin items are not offered when calibrated and idle");
      g_calibrated = 0; s_machineState = LCDJOYSTICKUI_STATE_UNCAL;
      CHECK(!MenuItemAvailable(MENU_SET_ORIGIN) && !MenuItemAvailable(MENU_ORIGIN_CAL) && MenuItemAvailable(MENU_CAL_MACHINE), "the origin items are offered while uncalibrated (or 'Calibrate machine' is not)");
      g_calibrated = 1;
      { LcdJoystickUI_MachineState_t st[3] = { LCDJOYSTICKUI_STATE_RUNNING, LCDJOYSTICKUI_STATE_PAUSED, LCDJOYSTICKUI_STATE_ERROR };
        for (int i = 0; i < 3; i++) { s_machineState = st[i]; CHECK(!MenuItemAvailable(MENU_SET_ORIGIN) && !MenuItemAvailable(MENU_ORIGIN_CAL), "the origin items are offered in job state %d", (int)st[i]); } }
      s_machineState = LCDJOYSTICKUI_STATE_IDLE;
      // "Set origin": a confirmation first, then Motion_SetOrigin(1, 0, 1, 0), then the status screen
      g_setOriginCalls = g_setRefCalls = 0; g_originOk = 1;
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_SET_ORIGIN);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM && s_originToReference == 0 && g_setOriginCalls == 0, "choosing 'Set origin' did not open the confirmation (mode %d, calls %d)", (int)s_uiMode, g_setOriginCalls);
      CHECK(LcdJoystickUI_IsBusy(), "the confirmation must keep the serial link out (IsBusy)");
      g_tick += 6; LcdJoystickUI_Task(); settle(); lcd.row(0, r0); lcd.row(1, r1);
      CHECK(strcmp(r0, "Origin = here?  ") == 0 && strcmp(r1, "SW=ok  hold=no  ") == 0, "the confirmation reads '%s' / '%s'", r0, r1);
      HandleOriginConfirmInput(0, 0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM && g_setOriginCalls == 0, "the confirmation acted without a press (mode %d, calls %d)", (int)s_uiMode, g_setOriginCalls);
      HandleOriginConfirmInput(0, 1);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_MENU && g_setOriginCalls == 0, "a long press must leave without doing anything (mode %d, calls %d)", (int)s_uiMode, g_setOriginCalls);
      EnterMenuItem(MENU_SET_ORIGIN); g_originOk = 0; HandleOriginConfirmInput(1, 0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM && g_setOriginCalls == 1, "a refused origin must leave the confirmation on screen (mode %d, calls %d)", (int)s_uiMode, g_setOriginCalls);
      g_originOk = 1; HandleOriginConfirmInput(1, 0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS && g_setOriginCalls == 2 && g_soWhat == (MOTION_ORIGIN_X | MOTION_ORIGIN_Y) && g_soXv == 0 && g_soYv == 0 && g_setRefCalls == 0,
            "the second press must set the origin to (0,0) on both axes and show the status screen (mode %d, calls %d, what %d, values %ld,%ld)", (int)s_uiMode, g_setOriginCalls, (int)g_soWhat, (long)g_soXv, (long)g_soYv);
      CHECK(!LcdJoystickUI_IsBusy(), "the status screen must not keep the link out");
      // "Origin = calib.": the same, towards the reference point
      g_setOriginCalls = g_setRefCalls = 0;
      s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_ORIGIN_CAL);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_ORIGIN_CONFIRM && s_originToReference == 1, "'Origin = calib.' did not open its confirmation");
      g_tick += 6; LcdJoystickUI_Task(); settle(); lcd.row(0, r0);
      CHECK(strcmp(r0, "Origin=calib.?  ") == 0, "the confirmation reads '%s'", r0);
      HandleOriginConfirmInput(1, 0);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_STATUS && g_setRefCalls == 1 && g_setOriginCalls == 0, "'Origin = calib.' must ask Motion_SetOrigin() for the reference only (mode %d, ref %d, set %d)", (int)s_uiMode, g_setRefCalls, g_setOriginCalls);
      // not calibrated: choosing it does nothing
      g_calibrated = 0; s_uiMode = LCDJOYSTICKUI_MODE_MENU; EnterMenuItem(MENU_SET_ORIGIN);
      CHECK(s_uiMode == LCDJOYSTICKUI_MODE_MENU, "'Set origin' opened while uncalibrated");
      g_calibrated = 1; s_uiMode = LCDJOYSTICKUI_MODE_STATUS; settle();
      #undef LABEL
   }

   printf("\n4o. The pen's state on the status screen: column 0 of row 1, the down and up glyphs (REQ-CoreLR-D-LcdJoystickUI-0030, -0032)\n");
   {
      char r0[17], r1[17], r1b[17];
      g_pos.X = 12300L; g_pos.Y = -4500L; g_calibrated = 1; s_machineState = LCDJOYSTICKUI_STATE_IDLE; s_uiMode = LCDJOYSTICKUI_MODE_STATUS;
      g_penIsDown = 0; g_tick += 6; LcdJoystickUI_Task(); settle(); lcd.row(0, r0); lcd.row(1, r1);
      CHECK(r1[0] == LCDJOYSTICKUI_PEN_UP_CHAR, "pen up: row 1 starts with code %d ('%s')", r1[0], r1);
      CHECK(r1[LCDJOYSTICKUI_L_LABEL_COLUMN] == 'L' && r1[LCDJOYSTICKUI_R_LABEL_COLUMN] == 'R', "the L and R labels moved: '%s'", r1);
      CHECK(r0[0] == LCDJOYSTICKUI_TAG_IDLE, "the state tag in row 0 changed: '%s'", r0);
      g_penIsDown = 1; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, r1b);
      CHECK(r1b[0] == LCDJOYSTICKUI_PEN_DOWN_CHAR, "pen down: row 1 starts with code %d ('%s')", r1b[0], r1b);
      CHECK(strcmp(r1 + 1, r1b + 1) == 0, "only the pen character may differ: '%s' vs '%s'", r1, r1b);
      g_penIsDown = 0; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, r1b);
      CHECK(r1b[0] == LCDJOYSTICKUI_PEN_UP_CHAR, "pen up again: code %d", r1b[0]);
      // it follows the pen while a job runs, too, and in the Control jog (where the pen stays up)
      s_machineState = LCDJOYSTICKUI_STATE_RUNNING; g_penIsDown = 1; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, r1b);
      CHECK(r1b[0] == LCDJOYSTICKUI_PEN_DOWN_CHAR, "while a job runs the pen state is not shown: code %d", r1b[0]);
      s_machineState = LCDJOYSTICKUI_STATE_IDLE; g_penIsDown = 0;
#if !CORELR_DEBUG_MISS_COUNT
      s_uiMode = LCDJOYSTICKUI_MODE_JOG_CONTROL; g_tick += 6; settle(); LcdJoystickUI_Task(); settle(); lcd.row(1, r1b);
      CHECK(r1b[0] == LCDJOYSTICKUI_PEN_UP_CHAR, "in the Control jog row 1 starts with code %d", r1b[0]);
#endif
      s_uiMode = LCDJOYSTICKUI_MODE_STATUS; g_penIsDown = 0; settle();
#if LCDJOYSTICKUI_PEN_GLYPHS
      /* the pictograms are in the display's CGRAM after the start-up: code 1 = pen down, code 2 = pen up (REQ-CoreLR-D-LcdJoystickUI-0032) */
      { static const uint8_t down[8] = { 0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x1F }, up[8] = { 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00, 0x00, 0x1F };
        CHECK(memcmp(&lcd.cgram[8], down, 8) == 0, "glyph 1 (pen down) is not in the CGRAM");
        CHECK(memcmp(&lcd.cgram[16], up, 8) == 0, "glyph 2 (pen up) is not in the CGRAM");
        CHECK(LCDJOYSTICKUI_PEN_DOWN_CHAR == 1 && LCDJOYSTICKUI_PEN_UP_CHAR == 2, "the glyph codes are %d and %d", LCDJOYSTICKUI_PEN_DOWN_CHAR, LCDJOYSTICKUI_PEN_UP_CHAR); }
#else
      CHECK(LCDJOYSTICKUI_PEN_DOWN_CHAR == 'v' && LCDJOYSTICKUI_PEN_UP_CHAR == '^', "without the pictograms the pen is v and ^");
#endif
   }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
