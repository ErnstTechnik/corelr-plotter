/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
// lcd_model.h -- a model of what is on the display's bus, shared by test_lcdi2c.cpp and test_ui.cpp: a TWI unit
// (the registers of shim_lcd/avr/io.h), a PCF8574 and an HD44780 with its timing rules, on a virtual clock.
// Included once per test, after avr/io.h and util/delay.h, and before the driver it exercises.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "avr/io.h"
#include "util/delay.h"
#include "../../firmware/config/HwConfig.h"
#include "../../firmware/config/SwConfig.h"

// ---------------------------------------------------------------- the world
static double now_us;                               // virtual clock
static double busUsPerClock;                        // from TWBR
static void advance(double us) { now_us += us; }
void model_delay_us(double us) { advance(us); }

// fault injection
static int f_present = 1;                           // does the display answer its address?
static int f_stuck;                                 // the bus never completes an operation
static int f_stopStuck;                             // a STOP never completes
static int f_nackTxn = -1;                          // NACK the first data byte of the n-th transaction from now (1 = the next one); -1: never
static int f_nackDataAt = -1;                       // NACK the n-th data byte of the next transaction
static int g_polls;                                 // reads of TWCR (polls)

// ---------------------------------------------------------------- HD44780
struct Lcd
{
   int ifWidth8 = 1;                                // 1 until the switch to 4 bits
   int haveHigh, high;                              // 4-bit: first nibble waiting
   uint8_t ddram[128]; int addr, incr = 1;
   uint8_t cgram[64]; int inCg, cgAddr;                // the user characters: eight rows of eight bytes (the five low bits are the dots)
   int displayOn, twoLine, clearedSeen, entrySet;
   double busyUntil; double lastStrobe = -1; double firstStrobe = -1;
   int initStep;                                    // strobes seen while still in 8-bit mode
   int tooSoon, badRs, badRw, instrCount, dataCount;
   int dataWhileBusy;
   void exec(uint8_t v, int rs)
   {
      if (now_us < busyUntil) { tooSoon++; }
      instrCount++;
      double t = 37;
      if (rs)
      {
         dataCount++;
         if (inCg) { cgram[cgAddr & 63] = v; cgAddr = (cgAddr + (incr ? 1 : -1)) & 63; }
         else { ddram[addr & 127] = v; addr = (addr + (incr ? 1 : -1)) & 127; }
      }
      else if (v & 0x80) { addr = v & 0x7F; inCg = 0; }
      else if (v & 0x40) { cgAddr = v & 0x3F; inCg = 1; }
      else if (v & 0x20) { twoLine = !!(v & 0x08); ifWidth8 = !!(v & 0x10); haveHigh = 0; }   // DL: 1 = 8-bit interface
      else if (v & 0x10) { }
      else if (v & 0x08) { displayOn = !!(v & 0x04); }
      else if (v & 0x04) { incr = !!(v & 0x02); entrySet = 1; }
      else if (v & 0x02) { addr = 0; inCg = 0; t = 1520; }
      else if (v & 0x01) { memset(ddram, ' ', sizeof ddram); addr = 0; inCg = 0; t = 1520; clearedSeen = 1; }
      busyUntil = now_us + t;
   }
   void strobe(uint8_t nib, int rs, int rw)
   {
      if (firstStrobe < 0) firstStrobe = now_us;
      if (rw) badRw++;
      if (ifWidth8)
      {
         // 8-bit interface: only D7..D4 are wired, so a strobe is a whole instruction with D3..D0 = 0.
         // The datasheet's init waits: >4.1 ms after the first function set, >100 us after the second.
         double t = 37; if (now_us < busyUntil) tooSoon++;
         instrCount++;
         if ((nib & 0x2) && !(nib & 0x1)) { /* 0x20: DL = 0 -> 4-bit from now on */ ifWidth8 = 0; }
         else if (nib == 0x3) { initStep++; t = (initStep == 1) ? 4100 : (initStep == 2) ? 100 : 37; }
         busyUntil = now_us + t;
         return;
      }
      if (!haveHigh) { high = nib; haveHigh = 1; }
      else { haveHigh = 0; exec((uint8_t)((high << 4) | nib), rs); }
   }
   char row(int r, char *out) { for (int i = 0; i < 16; i++) out[i] = (char)ddram[((r ? 0x40 : 0) + i) & 127]; out[16] = 0; return out[0]; }
};
static Lcd lcd;
// the PCF8574 latch
static uint8_t pcf; static int backlightOn; static int pcfWrites;
static void pcf_write(uint8_t v)
{
   uint8_t prevE = pcf & 0x04;
   pcf = v; pcfWrites++; backlightOn = !!(v & 0x08);
   if (prevE && !(v & 0x04)) lcd.strobe((uint8_t)(v >> 4), v & 1, !!(v & 2));       // E falling edge: the controller samples
}

// ---------------------------------------------------------------- the TWI unit
static uint8_t r_twcr, r_twsr, r_twdr, r_twbr, r_portc;
static int busState;                                // 0 idle, 1 started (expect address), 2 addressed (data)
static int dataIndex;
static int lastStopWasClean = 1;
static int transactions, stopsSeen, startsSeen, protocolErrors;
Reg &Reg::operator=(uint8_t v)
{
   switch (id)
   {
      case 1: // TWSR
         r_twsr = v; break;
      case 2: r_twdr = v; break;
      case 3: r_twbr = v; busUsPerClock = 1e6 / ((double)F_CPU / (16.0 + 2.0 * v)); break;
      case 4: r_portc = v; break;
      case 0: // TWCR
      {
         int en = !!(v & _BV(TWEN));
         if (!en) { r_twcr = 0; busState = 0; return *this; }          // switching the unit off
         if (v & _BV(TWSTO))
         {
            if (!f_stopStuck) { busState = 0; advance(busUsPerClock); stopsSeen++; r_twcr = (uint8_t)(v & ~(_BV(TWSTO))); r_twcr &= (uint8_t)~_BV(TWINT); }
            else r_twcr = v & ~_BV(TWINT);
            if (!f_stopStuck) { lastStopWasClean = 1; }
            return *this;
         }
         if (!(v & _BV(TWINT))) { r_twcr = v; return *this; }          // no command
         r_twcr = (uint8_t)(v & ~_BV(TWINT));                          // the command clears the flag...
         if (f_stuck) return *this;                                    // ...and the stuck bus never sets it again
         if (v & _BV(TWSTA)) { busState = 1; dataIndex = 0; advance(busUsPerClock); startsSeen++; r_twsr = 0x08; }
         else if (busState == 1)
         {
            advance(9 * busUsPerClock);
            if (f_present && (r_twdr >> 1) == LCDJOYSTICKUI_I2C_ADDRESS && !(r_twdr & 1)) { busState = 2; r_twsr = 0x18; transactions++; }
            else { r_twsr = 0x20; busState = 3; }             // address NACKed: only a STOP is valid from here
         }
         else if (busState == 2)
         {
            advance(9 * busUsPerClock);
            if (dataIndex == 0 && f_nackTxn > 0 && --f_nackTxn == 0) { r_twsr = 0x30; f_nackTxn = -1; }
            else if (dataIndex == f_nackDataAt) { r_twsr = 0x30; f_nackDataAt = -1; }
            else { pcf_write(r_twdr); r_twsr = 0x28; }
            dataIndex++;
         }
         else { protocolErrors++; r_twsr = 0x38; }        // a data byte with no address phase behind it, or after a NACK
         r_twcr |= _BV(TWINT);
         break;
      }
      }
   return *this;
}
Reg::operator uint8_t() const
{
   switch (id) { case 0: g_polls++; return r_twcr; case 1: return r_twsr; case 2: return r_twdr; case 3: return r_twbr; default: return r_portc; }
}
Reg TWCR = { 0 }, TWSR = { 1 }, TWDR = { 2 }, TWBR = { 3 }, PORTC = { 4 };

// ---------------------------------------------------------------- helpers
static void power_on(void)
{
   now_us = 0; lcd = Lcd(); memset(lcd.ddram, 0, sizeof lcd.ddram); pcf = 0; pcfWrites = 0; busState = 0; r_twcr = 0;
   f_present = 1; f_stuck = 0; f_stopStuck = 0; f_nackDataAt = -1; f_nackTxn = -1; g_polls = 0; transactions = startsSeen = stopsSeen = protocolErrors = 0;
}
static void show(const char *what)
{
   char a[17], b[17]; lcd.row(0, a); lcd.row(1, b);
   printf("  %-34s |%s|%s|\n", what, a, b);
}

