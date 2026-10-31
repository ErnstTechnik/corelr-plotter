/*
 * T01_LcdJoystick.ino
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
 * @file T01_LcdJoystick.ino
 * @brief Bring-up test 1: the I2C bus, the LCD and the joystick, **before** anything of the firmware runs.
 *
 * Needs only the Arduino core, the display driver (LcdI2c, polled TWI, no Wire) and the pin numbers of HwConfig.h, so a failure here is a wiring,
 * address or supply fault, not the job state machine, the stepper or the planner. Serial Monitor at 115200.
 *
 * What it reports, in order:
 *
 *  1. WHY THE BOARD LAST RESET, read from MCUSR. This is the reason this sketch exists in the form it does: the main firmware was seen resetting about
 *     once a second, and brown-out, watchdog and external reset are three quite different investigations.
 *  2. An I2C bus scan, with raw TWI registers (nothing in between) -- the PCF8574 backpacks ship as either 0x27 or 0x3F depending on the chip variant and
 *     its solder jumpers, and a blank-but-backlit screen is exactly what the wrong one looks like. The scan finds it rather than assuming, and says whether it
 *     is the address the firmware is built for (LCDJOYSTICKUI_I2C_ADDRESS, HwConfig.h): if not, change that one line.
 *  3. A live LCD frame plus raw joystick values, refreshed twice a second, with a heartbeat counter. If the board is resetting, the counter returns to zero
 *     and the period is visible directly.
 *
 * Nothing here touches the motors, the servo or the SD card.
 *
 * Ported to CMake 2026-10-11 (REQ-CoreLR-D-Build-0011): it used the Wire and LiquidCrystal_PCF8574 libraries, which the firmware no longer uses and which are not
 * in this repository; the LCD half now goes through the firmware's own driver (so it tests that driver on the real glass as well), the scan is written here.
 */

#include <Arduino.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <util/twi.h>
#include "HwConfig.h"
#include "LcdI2c.h"

static uint8_t  s_lcdAddress;   /* 0 until the scan finds one */
static uint8_t  s_lcdOk;
static uint16_t s_heartbeat;
static uint32_t s_lastRefreshMs;

/** @brief Report why the board last reset, then clear the flags (MCUSR is sticky; some bootloaders clear it first: an all-zero read means "the bootloader got there first"). */
static void ReportResetCause(void)
{
   uint8_t cause = MCUSR;

   MCUSR = 0;
   wdt_disable(); /* a watchdog reset leaves the WDT running with a short period */

   Serial.print(F("Reset cause: MCUSR=0x"));
   Serial.print(cause, HEX);
   Serial.print(F("  "));
   if (cause == 0)         { Serial.print(F("(cleared by the bootloader -- inconclusive)")); }
   if (cause & _BV(PORF))  { Serial.print(F("POWER-ON ")); }
   if (cause & _BV(EXTRF)) { Serial.print(F("EXTERNAL(reset pin) ")); }
   if (cause & _BV(BORF))  { Serial.print(F("BROWN-OUT(supply dipped) ")); }
   if (cause & _BV(WDRF))  { Serial.print(F("WATCHDOG ")); }
   Serial.println();
}

/** @brief Does a device acknowledge its address? One START, SLA+W, STOP, on the raw TWI registers (100 kHz). */
static uint8_t TwiProbe(uint8_t address)
{
   uint16_t guard;
   uint8_t ack = 0;

   TWBR = (uint8_t)(((F_CPU / 100000UL) - 16UL) / 2UL);
   TWSR = 0;
   TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
   for (guard = 0; guard < 20000u && !(TWCR & _BV(TWINT)); guard++) { }
   if ((TWSR & 0xF8u) == TW_START)
   {
      TWDR = (uint8_t)(address << 1);                       /* SLA+W */
      TWCR = _BV(TWINT) | _BV(TWEN);
      for (guard = 0; guard < 20000u && !(TWCR & _BV(TWINT)); guard++) { }
      ack = (uint8_t)((TWSR & 0xF8u) == TW_MT_SLA_ACK);
   }
   TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);              /* STOP, whatever happened */
   for (guard = 0; guard < 20000u && (TWCR & _BV(TWSTO)); guard++) { }
   return ack;
}

/** @brief Scan the bus and report every device that answers. @return the first address found, 0 if none. */
static uint8_t ScanI2c(void)
{
   uint8_t found = 0;
   uint8_t address;

   Serial.println(F("I2C scan:"));
   for (address = 1; address < 127; address++)
   {
      if (TwiProbe(address))
      {
         Serial.print(F("  device at 0x"));
         Serial.println(address, HEX);
         if (found == 0)
         {
            found = address; /* first one wins; an LCD backpack is normally alone */
         }
      }
   }
   if (found == 0)
   {
      Serial.println(F("  NOTHING ANSWERED -- check SDA=A4, SCL=A5, power and ground"));
   }
   return found;
}

void setup(void)
{
   Serial.begin(115200);
   while (!Serial && millis() < 2000) { /* wait briefly for USB, but never hang */ }

   Serial.println();
   Serial.println(F("=== CoreLR T01: I2C, LCD and joystick, before the firmware ==="));
   ReportResetCause();

   pinMode(LED_BUILTIN, OUTPUT);
   pinMode(LCDJOYSTICKUI_SW_PIN, INPUT_PULLUP);

   s_lcdAddress = ScanI2c();
   if (s_lcdAddress != 0)
   {
      Serial.print(F("The firmware is built for 0x"));
      Serial.print((uint8_t)LCDJOYSTICKUI_I2C_ADDRESS, HEX);
      Serial.println(s_lcdAddress == (uint8_t)LCDJOYSTICKUI_I2C_ADDRESS ? F("  -- the scan agrees")
                                                                       : F("  -- the scan DISAGREES: set LCDJOYSTICKUI_I2C_ADDRESS (HwConfig.h) to the found address"));
      s_lcdOk = LcdI2c_Init();
      Serial.println(s_lcdOk ? F("LcdI2c_Init: the display answered") : F("LcdI2c_Init: the display did NOT answer at the built-in address"));
      if (s_lcdOk)
      {
         uint8_t i;
         static const char kHello[] PROGMEM = "CoreLR T01";
         static const char kOk[] PROGMEM = "LCD OK";

         LcdI2c_SetCursor(0, 0);
         for (i = 0; i < sizeof(kHello) - 1u; i++) { LcdI2c_PutChar((uint8_t)pgm_read_byte(&kHello[i])); }
         LcdI2c_SetCursor(0, 1);
         for (i = 0; i < sizeof(kOk) - 1u; i++) { LcdI2c_PutChar((uint8_t)pgm_read_byte(&kOk[i])); }
      }
   }
   else
   {
      Serial.println(F("No LCD -- continuing with serial only"));
   }
   Serial.println(F("Running. Heartbeat should count up without restarting."));
}

/** @brief Write `text` at (0, row), padded with blanks to the 16 columns. */
static void LcdLine(uint8_t row, const char *text)
{
   uint8_t col;

   LcdI2c_SetCursor(0, row);
   for (col = 0; col < LCDJOYSTICKUI_COLS; col++)
   {
      LcdI2c_PutChar((uint8_t)((*text != '\0') ? *text++ : ' '));
   }
}

void loop(void)
{
   uint32_t nowMs = millis();

   /* Visible proof of life, independent of the LCD: if the board is resetting, this counter goes back to zero and the period is whatever it reached. */
   digitalWrite(LED_BUILTIN, ((nowMs / 250u) & 1u) ? HIGH : LOW);

   if ((nowMs - s_lastRefreshMs) < 500u)
   {
      return;
   }
   s_lastRefreshMs = nowMs;
   s_heartbeat++;

   {
      int x = analogRead(LCDJOYSTICKUI_X_PIN);
      int y = analogRead(LCDJOYSTICKUI_Y_PIN);
      uint8_t sw = (digitalRead(LCDJOYSTICKUI_SW_PIN) == LOW) ? 1 : 0;

      Serial.print(F("beat="));
      Serial.print(s_heartbeat);
      Serial.print(F("  up="));
      Serial.print(nowMs / 1000u);
      Serial.print(F("s  X="));
      Serial.print(x);
      Serial.print(F(" Y="));
      Serial.print(y);
      Serial.print(F(" SW="));
      Serial.println(sw ? F("PRESSED") : F("released"));

      if (s_lcdOk)
      {
         char line[LCDJOYSTICKUI_COLS + 1];

         snprintf(line, sizeof(line), "X%4d Y%4d %c", x, y, sw ? '*' : '.');
         LcdLine(0, line);
         snprintf(line, sizeof(line), "beat %-10u", (unsigned)s_heartbeat);
         LcdLine(1, line);
      }
   }
}
