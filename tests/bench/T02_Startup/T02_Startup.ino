/*
 * T02_Startup.ino
 * Part of the CoreLR wall-plotter firmware project -- bring-up tests.
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

/**
 * @file T02_Startup.ino
 * @brief Bring-up test 2: bisect the firmware's start-up.
 *
 * T01 proved the LCD, the joystick and the I2C bus are sound, so the
 * roughly-once-a-second reset comes from our own code. This sketch
 * runs the **real modules** -- `src/corelr` is a symlink to
 * `firmware/src`, so there is no copy to drift -- in the real order,
 * printing a marker before and after every single step.
 *
 * Read the last marker printed before the banner reappears: that is
 * the step that killed the board. `A>` means "about to call",
 * `<` means "returned".
 *
 * Why this and not a module-by-module unit test first: a reset is a
 * whole-system symptom, and bisecting the start-up costs one flash
 * and localises it to one call. Individual module tests come after,
 * on whichever module this accuses.
 *
 * Serial Monitor at 115200.
 */

#include <Arduino.h>
#include <avr/wdt.h>

#include "HwConfig.h"
#include "SwConfig.h"
#include "Calibration.h"
#include "SysTick.h"
#include "Stepper.h"
#include "Motion.h"
#include "PenControl.h"
#include "GCodeParser.h"
#include "SDReader.h"
#include "LcdJoystickUI.h"
#include "IsrDispatch.h"

static uint16_t s_beat;
static uint32_t s_lastBeatMs;

/** @brief Announce a step, flushing so nothing is lost if it resets. */
static void Step(const __FlashStringHelper *what)
{
   Serial.print(F("A> "));
   Serial.println(what);
   Serial.flush(); /* essential: an unflushed buffer dies with the board */
}

static void Done(void)
{
   Serial.println(F("   < ok"));
   Serial.flush();
}

void setup(void)
{
   uint8_t cause = MCUSR;

   MCUSR = 0;
   wdt_disable();

   Serial.begin(115200);
   while (!Serial && millis() < 2000) { }

   Serial.println();
   Serial.println(F("=== CoreLR T02: start-up bisection ==="));
   Serial.print(F("Reset cause: MCUSR=0x"));
   Serial.print(cause, HEX);
   if (cause == 0)      { Serial.println(F(" (bootloader cleared it -- inconclusive)")); }
   else
   {
      if (cause & _BV(PORF))  { Serial.print(F(" POWER-ON")); }
      if (cause & _BV(EXTRF)) { Serial.print(F(" EXTERNAL")); }
      if (cause & _BV(BORF))  { Serial.print(F(" BROWN-OUT")); }
      if (cause & _BV(WDRF))  { Serial.print(F(" WATCHDOG")); }
      Serial.println();
   }
   Serial.flush();

   Step(F("Calibration_Init  (reads EEPROM)"));       Calibration_Init();   Done();
   Step(F("SysTick_Init"));                           SysTick_Init();       Done();
   Step(F("Stepper_Init      (D2-D9 as outputs)"));   Stepper_Init();       Done();
   Step(F("Motion_Init"));                            Motion_Init();        Done();
   Step(F("PenControl_Init   (software servo on A0, Timer2)")); PenControl_Init();   Done();
   Step(F("GCodeParser_Init"));                       GCodeParser_Init();   Done();
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   Step(F("SDReader_Init     (SPI, ~2s with no card)")); SDReader_Init();   Done();
#endif
   Step(F("LcdJoystickUI_Init (Wire + LCD)"));        LcdJoystickUI_Init(); Done();
   Step(F("IsrDispatch_Init  (Timer2 @ 4kHz ARMS THE ISR)")); IsrDispatch_Init(); Done();

   Serial.println(F("--- setup() complete, entering loop() ---"));
   Serial.flush();
}

void loop(void)
{
   uint32_t nowMs = millis();

   /* Each Task is announced only on the first pass, so the log stays
    * readable; after that only the heartbeat prints. If the board dies
    * on a later pass the heartbeat tells us how far it got. */
   static uint8_t firstPass = 1;

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD
   if (firstPass) { Step(F("SDReader_Task")); }
   SDReader_Task();
   if (firstPass) { Done(); }
#endif
   if (firstPass) { Step(F("Stepper_Task")); }
   Stepper_Task();
   if (firstPass) { Done(); }

   if (firstPass) { Step(F("LcdJoystickUI_Task")); }
   LcdJoystickUI_Task();
   if (firstPass) { Done(); }

   if (firstPass) { Step(F("PenControl_Task")); }
   PenControl_Task();
   if (firstPass) { Done(); }

   if (firstPass) { Step(F("Motion_Task")); }
   (void)Motion_Task();
   if (firstPass) { Done(); }

   if (firstPass)
   {
      Serial.println(F("--- first loop() pass complete ---"));
      Serial.flush();
      firstPass = 0;
   }

   if ((nowMs - s_lastBeatMs) >= 500u)
   {
      s_lastBeatMs = nowMs;
      s_beat++;
      Serial.print(F("beat="));
      Serial.print(s_beat);
      Serial.print(F(" up="));
      Serial.print(nowMs);
      Serial.println(F("ms"));
      Serial.flush();
   }
}
