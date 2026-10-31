/*
 * T09_LcdRom.ino
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
 * @file T09_LcdRom.ino
 * @brief Bring-up test 9: which character ROM does the LCD controller (HD44780 or compatible) carry?
 *
 * The 2x16 display shows two lines of the 256-code table, eight characters each:
 *
 *      0x00: cccccccc        <- the codes 0x00..0x07
 *      0x08: cccccccc        <- the codes 0x08..0x0F
 *
 * The joystick scrolls the table by one line (eight codes) at a time: down = the next lines (0x10, 0x18 ...), up = the previous ones; held, it repeats. The push button
 * (SW) goes back to the top. The last two lines are 0xF0 and 0xF8.
 *
 * What to look for, to tell the two usual ROMs apart:
 *   - 0x00..0x07 are the user-definable characters (CGRAM): blank or scrambled until something loads them. 0x10..0x1F: blank on the A00 (Japanese) ROM, arrows and
 *     triangles (up, down, right, left, ...) on the A02 (European) ROM.
 *   - 0x7E and 0x7F: a right and a left arrow on the A00 ROM; a house-like and a delta on the A02.
 *   - 0xA1..0xDF: katakana on the A00; accented and punctuation characters on the A02. 0xFF: a full block on the A00.
 * Nothing here touches the motors, the servo or the SD card; the EEPROM is not written. The display is driven by the firmware's own LcdI2c.
 */

#include <Arduino.h>
#include "HwConfig.h"
#include "LcdI2c.h"

#define ROM_LINES      32u          /* 256 codes / 8 per line */
#define STICK_ON       250          /* deflection to count as a move (10-bit ADC counts around the centre) */
#define STICK_OFF      120          /* ... and to count as released (hysteresis) */
#define REPEAT_MS      350u

static uint8_t  s_top;              /* the line shown on the first row; the second shows s_top + 1 */
static uint8_t  s_held;             /* the stick is past STICK_ON */
static uint32_t s_lastMoveMs;
static uint8_t  s_swWasDown;

static void Hex2(uint8_t v)
{
   static const char kDigits[] = "0123456789ABCDEF";

   LcdI2c_PutChar((uint8_t)kDigits[v >> 4]);
   LcdI2c_PutChar((uint8_t)kDigits[v & 15u]);
}

/** @brief One row: "0xNN: " then the eight codes of line `line`. */
static void DrawLine(uint8_t row, uint8_t line)
{
   uint8_t i;
   uint8_t first = (uint8_t)(line * 8u);

   LcdI2c_SetCursor(0, row);
   LcdI2c_PutChar('0');
   LcdI2c_PutChar('x');
   Hex2(first);
   LcdI2c_PutChar(':');
   LcdI2c_PutChar(' ');
   for (i = 0; i < 8u; i++)
   {
      LcdI2c_PutChar((uint8_t)(first + i));
   }
   LcdI2c_PutChar(' ');
   LcdI2c_PutChar(' ');
}

static void Draw(void)
{
   DrawLine(0, s_top);
   DrawLine(1, (uint8_t)(s_top + 1u));
}

void setup(void)
{
   Serial.begin(115200);
   pinMode(LCDJOYSTICKUI_SW_PIN, INPUT_PULLUP);
   Serial.println(LcdI2c_Init() ? F("T09: the display answered") : F("T09: the display did NOT answer"));
   s_top = 0;
   Draw();
}

void loop(void)
{
   int16_t dy = (int16_t)analogRead(LCDJOYSTICKUI_Y_PIN) - 512;
   uint8_t sw = (digitalRead(LCDJOYSTICKUI_SW_PIN) == LOW) ? 1u : 0u;
   uint32_t now = millis();

   if (LCDJOYSTICKUI_Y_INVERT)
   {
      dy = (int16_t)-dy;                  /* positive = down, as everywhere in the firmware */
   }
   if (sw && !s_swWasDown)
   {
      s_top = 0;
      Draw();
   }
   s_swWasDown = sw;

   if ((dy > STICK_ON) || (dy < -STICK_ON))
   {
      if (!s_held || ((now - s_lastMoveMs) >= REPEAT_MS))
      {
         s_held = 1;
         s_lastMoveMs = now;
         if ((dy > 0) && (s_top < (ROM_LINES - 2u)))
         {
            s_top++;
            Draw();
         }
         else if ((dy < 0) && (s_top > 0u))
         {
            s_top--;
            Draw();
         }
      }
   }
   else if ((dy < STICK_OFF) && (dy > -STICK_OFF))
   {
      s_held = 0;
   }
}
