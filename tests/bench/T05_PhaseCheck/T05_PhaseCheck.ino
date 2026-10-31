/*
 * T05_PhaseCheck.ino
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
 * @file T05_PhaseCheck.ino
 * @brief Bring-up test 5: are the motor phases wired in the order the
 * firmware assumes?
 *
 * The phase table itself is provably right. `{0x0C, 0x06, 0x03, 0x09}`
 * is IN3+IN4, IN2+IN3, IN1+IN2, IN1+IN4 -- consecutive pairs always
 * share exactly one coil and change exactly two, which is a uniform
 * two-phase-on rotating field. (It runs "backwards" relative to
 * AB-BC-CD-DA, which is what STEPPER_MOTOR_*_REVERSED exists for.)
 *
 * What no amount of reading can settle is whether IN1..IN4 reach the
 * motor's coils **in that physical order**. Get two wires crossed and
 * the motor still turns -- the field still rotates, just unevenly --
 * but it loses torque and breaks down at a much lower speed. That is
 * exactly the symptom being chased: break-up above ~400 steps/s even
 * with the queue kept full.
 *
 * Three phases, slow enough to watch:
 *
 *  1. **Identify.** Each coil energised alone for 1.5s, announcing
 *     which IN and which Arduino pin. Follow the shaft, or a finger on
 *     the motor: it should snap to four distinct positions, in order.
 *  2. **Wave drive**, one coil at a time, 2 steps/s. With the order
 *     right the shaft creeps steadily one way. With two wires crossed
 *     it stutters -- forward, forward, back -- or barely moves. This is
 *     the decisive one, because single-coil drive has no ambiguity to
 *     hide behind.
 *  3. **The production table**, same speed. Should creep the same way
 *     as phase 2 (or exactly opposite, which is fine) and noticeably
 *     stronger, two coils being energised instead of one.
 *
 * Each phase runs forwards then backwards, so the cable ends where it
 * started.
 *
 * Serial Monitor at 115200.
 */

#include <Arduino.h>
#include "HwConfig.h"
#include "SwConfig.h"
#include "SysTick.h"
#include "Stepper.h"
#include "IsrDispatch.h"

/** Which motor is under test: L drives the low nibble, R the high one. */
#define TEST_MOTOR_L 1

/** Wave drive: one coil at a time. */
static const uint8_t kWave[4] = { 0x01u, 0x02u, 0x04u, 0x08u };

/** The production table, copied here so the test states what it checks
 *  rather than depending on Motion's internals. Kept identical to
 *  Motion.c's kPhaseTable -- if that changes, change this. */
static const uint8_t kFullStep[4] = { 0x0Cu, 0x06u, 0x03u, 0x09u };

static const char *PinNameOf(uint8_t coilIndex)
{
   switch (coilIndex)
   {
      case 0:  return "IN1";
      case 1:  return "IN2";
      case 2:  return "IN3";
      default: return "IN4";
   }
}

static uint8_t PinNumberOf(uint8_t coilIndex)
{
#if TEST_MOTOR_L
   const uint8_t pins[4] = { STEPPER_MOTOR_L_PIN0, STEPPER_MOTOR_L_PIN1,
                             STEPPER_MOTOR_L_PIN2, STEPPER_MOTOR_L_PIN3 };
#else
   const uint8_t pins[4] = { STEPPER_MOTOR_R_PIN0, STEPPER_MOTOR_R_PIN1,
                             STEPPER_MOTOR_R_PIN2, STEPPER_MOTOR_R_PIN3 };
#endif
   return pins[coilIndex];
}

/** @brief Hold `bits` on the motor under test for `ms`, the other motor
 *  de-energised. Goes through the real driver, so the real pin mapping
 *  is what gets exercised. */
static void HoldPattern(uint8_t bits, uint16_t ms)
{
   uint32_t ticks = ((uint32_t)ms * STEPPER_COUNTS_PER_S) / 1000u; /* Timer1 counts since the v2 Stepper (0.5 us), no longer ISR ticks */

   while (ticks > 0u)
   {
      uint32_t chunk = (ticks > 16000u) ? 16000u : ticks;

#if TEST_MOTOR_L
      if (Stepper_RequestMotion(bits, 0u, chunk, STEPPER_PEN_NONE))
#else
      if (Stepper_RequestMotion(0u, bits, chunk, STEPPER_PEN_NONE))
#endif
      {
         ticks -= chunk;
      }
      Stepper_Task();
   }
   while (!Stepper_IsEmpty()) { Stepper_Task(); }
}

static void Identify(void)
{
   uint8_t i;

   Serial.println(F("--- phase 1: identify each coil (1.5s each) ---"));
   for (i = 0; i < 4u; i++)
   {
      Serial.print(F("  energising ")); Serial.print(PinNameOf(i));
      Serial.print(F(" alone  (Arduino D")); Serial.print(PinNumberOf(i));
      Serial.println(F(")"));
      Serial.flush();
      HoldPattern(kWave[i], 1500u);
   }
   HoldPattern(0u, 200u);
}

static void Sweep(const uint8_t *table, const __FlashStringHelper *what)
{
   uint8_t pass;

   Serial.print(F("--- ")); Serial.print(what);
   Serial.println(F(", 2 steps/s, 16 forward then 16 back ---"));
   Serial.flush();
   for (pass = 0; pass < 2u; pass++)
   {
      uint8_t n;

      Serial.println(pass == 0u ? F("  forward") : F("  backward"));
      Serial.flush();
      for (n = 0; n < 16u; n++)
      {
         uint8_t idx = (pass == 0u) ? (uint8_t)(n & 3u) : (uint8_t)((4u - (n & 3u)) & 3u);

         HoldPattern(table[idx], 500u);
      }
   }
   HoldPattern(0u, 200u);
}

void setup(void)
{
   Serial.begin(115200);
   while (!Serial && millis() < 2000) { }

   Serial.println();
   Serial.println(F("=== CoreLR T05: motor phase and wiring check ==="));
#if TEST_MOTOR_L
   Serial.println(F("Motor under test: L"));
#else
   Serial.println(F("Motor under test: R"));
#endif
   Serial.println(F("Watch the shaft. Phase 2 is the decisive one:"));
   Serial.println(F("  steady creep one way  -> wiring order is right"));
   Serial.println(F("  stutter or no net motion -> two wires are crossed"));
   Serial.flush();

   SysTick_Init();
   Stepper_Init();
   IsrDispatch_Init();
}

void loop(void)
{
   Identify();
   Sweep(kWave, F("phase 2: WAVE drive, one coil at a time"));
   Sweep(kFullStep, F("phase 3: production table, two coils at a time"));
   Serial.println(F("--- complete, repeating ---"));
   Serial.flush();
   delay(2000);
}
