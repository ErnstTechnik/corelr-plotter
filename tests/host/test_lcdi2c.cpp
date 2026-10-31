/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
// Host test of the LcdI2c driver against a model of what is on the bus: the TWI unit, a PCF8574 and an HD44780
// (REQ-CoreLR-D-LcdI2c-0001..0003). The real LcdI2c.c is #included (compiled as C++).
//
//   g++ -m32 -O1 -Wall -Wno-unused-function -Ishim_lcd -I../../firmware/src test_lcdi2c.cpp -o out/test_lcdi2c
//
// The model keeps a virtual clock: a delay advances it, and so does every bus operation (9 clocks a byte at the
// configured bit rate, the start and the stop 1 clock each). The controller tracks when it is busy and counts a
// strobe that arrives too soon. That is where "no delay is needed between instructions" and the init waits are
// checked, rather than assumed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avr/io.h"
#include "util/delay.h"
#include "lcd_model.h"
#ifndef LCDI2C_SRC
#define LCDI2C_SRC "../../firmware/src/LcdI2c.c"
#endif
#include LCDI2C_SRC

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 25) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

int main(void)
{
   printf("1. Initialisation by instruction, against the controller's timing rules\n");
   power_on();
   uint8_t ok = LcdI2c_Init();
   printf("  returned %d; bus clock %.1f kHz (TWBR %u); virtual time %.1f ms; first strobe at %.1f ms\n", ok, 1000.0 / busUsPerClock, r_twbr, now_us / 1000.0, lcd.firstStrobe / 1000.0);
   CHECK(ok == 1, "Init reported failure");
   CHECK(r_twbr == 72 && busUsPerClock > 9.99 && busUsPerClock < 10.01, "bus clock is not 100 kHz (TWBR %u)", r_twbr);
   CHECK(lcd.firstStrobe >= 40000.0, "the first strobe came %.1f ms after power-up; the controller needs 40 ms", lcd.firstStrobe / 1000.0);
   CHECK(lcd.tooSoon == 0, "%d strobes arrived while the controller was busy", lcd.tooSoon);
   CHECK(!lcd.ifWidth8 && lcd.twoLine && lcd.displayOn && lcd.clearedSeen && lcd.incr && lcd.entrySet, "controller state: 4-bit %d two-line %d display %d cleared %d increment %d entry-mode %d", !lcd.ifWidth8, lcd.twoLine, lcd.displayOn, lcd.clearedSeen, lcd.incr, lcd.entrySet);
   CHECK(backlightOn, "the backlight is not on");
   CHECK((r_portc & 0x30) == 0x30, "the internal pull-ups on SDA and SCL are not enabled");
   CHECK(startsSeen == stopsSeen, "%d starts, %d stops", startsSeen, stopsSeen);
   CHECK(protocolErrors == 0, "%d bus protocol errors during init", protocolErrors);
   show("after init");

   printf("\n2. Text: both rows, the way the display code drives it\n");
   const char *l0 = "L,R=1234,5678", *l1 = "idle  X=-12.500";
   LcdI2c_SetCursor(0, 0); for (const char *p = l0; *p; p++) LcdI2c_PutChar((uint8_t)*p);
   LcdI2c_SetCursor(0, 1); for (const char *p = l1; *p; p++) LcdI2c_PutChar((uint8_t)*p);
   show("after two lines");
   { char a[17]; lcd.row(0, a); CHECK(strncmp(a, l0, strlen(l0)) == 0, "row 0 shows '%s'", a); lcd.row(1, a); CHECK(strncmp(a, l1, strlen(l1)) == 0, "row 1 shows '%s'", a); }
   // one character, and one cursor move: what they cost on the bus (protocol arithmetic in the model, not a bench figure)
   double t0 = now_us; LcdI2c_PutChar('x'); double tc = now_us - t0; t0 = now_us; LcdI2c_SetCursor(3, 0); double tp = now_us - t0;
   printf("  one character %.0f us on the bus, one cursor move %.0f us  (start + address + 4 data bytes + stop = 1 + 5 x 9 + 1 clocks)\n", tc, tp);
   CHECK(tc > 450 && tc < 480 && tp == tc, "a character took %.0f us", tc);
   // the cursor lands where it says: rows 0 and 1 at every column
   for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) { LcdI2c_SetCursor((uint8_t)c, (uint8_t)r); CHECK(lcd.addr == (r ? 0x40 : 0) + c, "cursor (%d,%d) went to %02X", c, r, lcd.addr); }
   // the address counter runs on past the last visible column, NOT to the next row (what the caller must know)
   LcdI2c_SetCursor(15, 0); LcdI2c_PutChar('A'); LcdI2c_PutChar('B');
   CHECK(lcd.addr == 0x11, "after the last column the address counter is %02X (the controller's own memory runs on to 0x10)", lcd.addr);
   { char r1[17]; lcd.row(1, r1); CHECK(r1[0] != 'B', "a character written after the last column landed on row 1"); }

   printf("\n2b. User characters: LcdI2c_DefineCharP() writes the CGRAM from flash (REQ-CoreLR-D-LcdI2c-0005)\n");
   {
      static const uint8_t kA[8] = { 0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x1F };
      static const uint8_t kB[8] = { 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00, 0x00, 0x1F };
      memset(lcd.cgram, 0xEE, sizeof lcd.cgram);
      CHECK(LcdI2c_DefineCharP(1, kA) == 1, "defining a character failed");
      CHECK(memcmp(&lcd.cgram[8], kA, 8) == 0, "glyph 1 landed wrong");
      CHECK(lcd.cgram[0] == 0xEE && lcd.cgram[16] == 0xEE, "a neighbouring glyph was disturbed");
      CHECK(LcdI2c_DefineCharP(2, kB) == 1 && memcmp(&lcd.cgram[16], kB, 8) == 0 && memcmp(&lcd.cgram[8], kA, 8) == 0, "the second glyph disturbed the first, or landed wrong");
      CHECK(LcdI2c_DefineCharP(9, kA) == 1 && memcmp(&lcd.cgram[8], kA, 8) == 0, "a code above 7 must wrap into 0..7 (here 9 -> 1)");
      CHECK(lcd.inCg == 1, "the address counter should be in CGRAM after a definition");
      LcdI2c_SetCursor(0, 0); LcdI2c_PutChar(1);
      CHECK(lcd.inCg == 0 && lcd.ddram[0] == 1, "after a cursor move the next character did not go to the display (inCg %d, ddram[0] %d)", lcd.inCg, lcd.ddram[0]);
      CHECK(memcmp(&lcd.cgram[8], kA, 8) == 0, "the glyph changed when a character was displayed");
      f_present = 0; CHECK(LcdI2c_DefineCharP(1, kA) == 0, "a definition on a display that does not answer reported success"); f_present = 1;
      f_nackTxn = 5; CHECK(LcdI2c_DefineCharP(1, kA) == 0, "a failure in the middle of a definition (the 5th write) was not reported"); busState = 0;
      CHECK(startsSeen == stopsSeen, "the failed definition left the bus held");
      CHECK(LcdI2c_DefineCharP(1, kA) == 1, "not usable after a failed definition");
   }

   printf("\n3. No delay between ordinary instructions: 200 000 random cursor moves and characters\n");
   {
      int before = lcd.tooSoon; srand(3);
      for (int i = 0; i < 200000; i++) { if (rand() & 1) LcdI2c_SetCursor((uint8_t)(rand() & 15), (uint8_t)(rand() & 1)); else LcdI2c_PutChar((uint8_t)(32 + rand() % 90)); }
      printf("  %d instructions in all; strobes that arrived while the controller was busy: %d\n", lcd.instrCount, lcd.tooSoon - before);
      CHECK(lcd.tooSoon == before, "%d strobes arrived too soon", lcd.tooSoon - before);
      CHECK(startsSeen == stopsSeen, "starts and stops differ: %d / %d", startsSeen, stopsSeen);
   }

   printf("\n4. A display that does not answer, a NACK in mid-transaction, a stuck bus -- all bounded, all released\n");
   {
      power_on(); LcdI2c_Init();
      f_present = 0; g_polls = 0; double t0 = now_us;
      uint8_t r = LcdI2c_PutChar('z');
      printf("  absent: PutChar returned %d after %.0f us and %d polls; bus idle %d\n", r, now_us - t0, g_polls, busState == 0);
      CHECK(r == 0 && busState == 0 && (r_twcr & _BV(TWEN)), "an absent display: returned %d, bus state %d", r, busState);
      CHECK(startsSeen == stopsSeen, "an absent display left the bus held");
      CHECK(protocolErrors == 0, "after a NACKed address the driver went on to send %d data byte(s): only a STOP is valid there", protocolErrors);
      f_present = 1; r = LcdI2c_PutChar('y'); CHECK(r == 1, "the display was not usable again after being absent");

      for (int n = 0; n < 4; n++)
      {
         f_nackDataAt = n; int w = pcfWrites; r = LcdI2c_PutChar('q');
         CHECK(r == 0 && busState == 0, "a NACK of data byte %d: returned %d, bus %d", n, r, busState);
         CHECK(startsSeen == stopsSeen, "a NACK of data byte %d left the bus held", n);
         CHECK(pcfWrites - w == n, "a NACK of byte %d: %d bytes got through", n, pcfWrites - w);
      }
      printf("  NACK of each of the 4 data bytes: returned 0, bus released, the bytes before it delivered\n");
      r = LcdI2c_PutChar('w'); CHECK(r == 1, "not usable after a NACK");

      f_stuck = 1; g_polls = 0; r = LcdI2c_PutChar('s');
      printf("  stuck bus: PutChar returned %d after %d polls (bound %u per wait); unit switched off %d\n", r, g_polls, LCDI2C_TIMEOUT_LOOPS, r_twcr == 0 || !(r_twcr & _BV(TWEN)));
      CHECK(r == 0 && g_polls <= 3 * (int)LCDI2C_TIMEOUT_LOOPS, "a stuck bus: returned %d after %d polls", r, g_polls);
      f_stuck = 0; r = LcdI2c_PutChar('v'); CHECK(r == 1, "not usable again once the bus recovered");

      f_stopStuck = 1; g_polls = 0; r = LcdI2c_PutChar('u');
      printf("  a STOP that never completes: PutChar returned %d after %d polls; the TWI unit was switched off (TWCR %u)\n", r, g_polls, r_twcr);
      CHECK(r_twcr == 0 && g_polls <= 3 * (int)LCDI2C_TIMEOUT_LOOPS, "a stuck STOP: TWCR %u after %d polls", r_twcr, g_polls);
      f_stopStuck = 0; r = LcdI2c_PutChar('t'); CHECK(r == 1, "not usable again after a stuck STOP");
   }

   printf("\n5. Init reports a dead display, without hanging\n");
   {
      power_on(); f_present = 0; g_polls = 0; uint8_t r = LcdI2c_Init();
      printf("  Init on an absent display returned %d after %.1f ms of virtual time and %d polls\n", r, now_us / 1000.0, g_polls);
      CHECK(r == 0, "Init claimed success on an absent display");
      CHECK(now_us < 80000.0, "Init took %.1f ms on an absent display", now_us / 1000.0);
   }

   printf("\n6. KNOWN LIMITATION, measured: a failure after the first nibble of a byte has been strobed splits the byte\n");
   {
      for (int n = 0; n < 4; n++)
      {
         power_on(); LcdI2c_Init(); LcdI2c_SetCursor(0, 0);
         f_nackDataAt = n; LcdI2c_PutChar('A');                    // fails at data byte n
         for (const char *p = "BCDEFGH"; *p; p++) { LcdI2c_SetCursor((uint8_t)(p - "BCDEFGH"), 1); LcdI2c_PutChar((uint8_t)*p); }
         char r1[17]; lcd.row(1, r1); int good = strncmp(r1, "BCDEFGH", 7) == 0;
         printf("  NACK of data byte %d, then 7 good characters: row 1 reads |%.8s| -> %s\n", n, r1, good ? "intact" : "GARBLED (the controller pairs the wrong nibbles)");
         CHECK(good == (n < 2), "a NACK of byte %d: the display is %s, expected %s", n, good ? "intact" : "garbled", (n < 2) ? "intact" : "garbled");
         LcdI2c_Init();                                            // the recovery: initialisation by instruction
         for (const char *p = "BCDEFGH"; *p; p++) { LcdI2c_SetCursor((uint8_t)(p - "BCDEFGH"), 1); LcdI2c_PutChar((uint8_t)*p); }
         lcd.row(1, r1); CHECK(strncmp(r1, "BCDEFGH", 7) == 0, "Init did not recover the display after a NACK of byte %d", n);
      }
      printf("  (a NACK of byte 0 or 1 is harmless; of byte 2 or 3 it splits a byte, the display garbles, and only LcdI2c_Init() re-synchronises (from any phase, as the model shows) -- the libraries had the same weakness)\n");
   }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
