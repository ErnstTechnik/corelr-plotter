/*
 * T03_StackWatch.ino
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
 * @file T03_StackWatch.ino
 * @brief Bring-up test 3: measure how deep the stack actually goes, running THE FIRMWARE ITSELF.
 *
 * `tools/analysis/stack_depth.py` estimates the worst-case stack from the disassembly, and has known blind spots: it follows direct calls only, so a call
 * through a vtable truncates the chain it measures. This test stops estimating and measures: it paints the free RAM with a known byte at start-up, and whatever
 * the stack touches destroys the paint, so the lowest surviving painted byte is the high-water mark -- the real deepest point ever reached, including the
 * paths no static analysis can see. Read "stack used" against "free RAM": if they meet, the stack is writing over static variables.
 *
 * Until 2026-10-11 this sketch carried a COPY of the firmware's job state machine and loop, which had to be refreshed by hand (and had not been). It is now the
 * real `firmware/app/grbl_corelr.ino` itself, included here with its setup() and loop() renamed, so what is measured is always what is flashed; the only addition
 * is the painting before the firmware's setup() and a line `S <stack used>/<room>` on the UART every two seconds. (The SD variant leaves USART0 free; the Arduino `Serial` is not used, it costs ~1.7 KB and the firmware has 174 bytes to spare, so the UART is driven by hand.)
 *
 * Drive the machine as in a real session -- browse the SD card (the deepest path of the firmware, REQ-CoreLR-D-SDReader-0005), run a job, jog, pause -- and read
 * the report. Serial Monitor at 115200. Built with the SD variant's settings.
 */

#include <Arduino.h>
#include <avr/io.h>
#include "SysTick.h"

#define setup corelr_setup
#define loop corelr_loop
#include "grbl_corelr.ino"
#undef setup
#undef loop

#define PAINT_BYTE 0xA5u

extern uint8_t __heap_start;
extern uint8_t *__brkval;
extern uint8_t __bss_end;

static uint8_t *s_paintLow;      /**< the first painted byte: the end of static data (or of the heap, if one exists) */
static uint8_t s_lastReportTick;
static uint16_t s_room;           /**< bytes between the end of static data and the top of RAM */

/** @brief Paint everything from the end of static RAM up to just below where the stack is NOW (the stack starts at RAMEND and is shallow at setup()). */
static void PaintFreeRam(void)
{
   uint8_t *p = (__brkval != 0) ? __brkval : &__bss_end;
   uint8_t *sp = (uint8_t *)SP;

   s_paintLow = p;
   s_room = (uint16_t)(RAMEND - (uintptr_t)p + 1u);
   for (; p < sp - 16; p++)
   {
      *p = PAINT_BYTE;
   }
}

/** @return how many bytes above the painted region's start have been overwritten by the stack: the deepest the stack has been, counted from RAMEND. */
static uint16_t StackUsedBytes(void)
{
   uint8_t *p = s_paintLow;

   while (p < (uint8_t *)RAMEND && *p == PAINT_BYTE)
   {
      p++;
   }
   return (uint16_t)((uint8_t *)RAMEND - p + 1);
}

/* USART0 by hand: the Arduino `Serial` costs ~1.7 KB of flash, and the SD firmware has 174 bytes to spare. Polled, 115200 8N1, transmit only. */
static void TxInit(void)
{
   UCSR0A = 0;
   UBRR0 = (uint16_t)((F_CPU / 8UL / 115200UL) - 1UL) | 0u;
   UCSR0A = _BV(U2X0);
   UCSR0B = _BV(TXEN0);
   UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

static void TxChar(char c)
{
   while (!(UCSR0A & _BV(UDRE0))) { }
   UDR0 = (uint8_t)c;
}

static void TxHex(uint16_t v)
{
   uint8_t n;

   for (n = 0; n < 4u; n++)
   {
      uint8_t d = (uint8_t)(v >> 12);

      TxChar((char)(d + ((d < 10u) ? '0' : ('A' - 10))));
      v <<= 4;
   }
}

void setup(void)
{
   PaintFreeRam();
   TxInit();
   corelr_setup();
}

void loop(void)
{
   corelr_loop();

   if ((uint8_t)(SysTick_Get() - s_lastReportTick) >= 200u)       /* 200 ticks of 10 ms: every two seconds */
   {
      s_lastReportTick = SysTick_Get();
      TxChar('S'); TxChar(' '); TxHex(StackUsedBytes()); TxChar('/'); TxHex(s_room); TxChar('\r'); TxChar('\n');   /* "S <stack used>/<room>", hexadecimal bytes */
   }
}
