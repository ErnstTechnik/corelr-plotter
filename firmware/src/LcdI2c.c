/*
 * LcdI2c.c
 * Part of the CoreLR wall-plotter firmware project.
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
 * @file LcdI2c.c
 * @brief See LcdI2c.h.
 *
 * Two layers in one file, each a handful of lines:
 *
 *   TWI master   Step() issues one TWCR command and polls for TWINT;
 *                Write() is START, SLA+W, n data bytes, STOP.
 *   controller   a character or an instruction is ONE transaction of four
 *                data bytes, the two nibbles each strobed on E:
 *                  {hi|E, hi, lo|E, lo}      hi, lo = nibble in P7..P4, plus RS and backlight
 *                The ~90 us a byte takes on the bus is longer than the controller's 37 us
 *                instruction time, so no delay is needed between ordinary instructions; only
 *                clear (1.52 ms) and the power-up steps wait, and only Init() issues those.
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include "LcdI2c.h"
#include "HwConfig.h"
#include "SwConfig.h"

#if LCDJOYSTICKUI_ROWS > 2
#error "LcdI2c supports one or two lines: the row offsets of a four-line display differ"
#endif

/* Backpack bits (the usual PCF8574 wiring). RW, P1, stays low: the controller is only written to. */
#define LCDI2C_RS 0x01u   /**< backpack bit P0: register select (0 = instruction, 1 = data) */
#define LCDI2C_E  0x04u   /**< bit P2: enable, the controller latches on its falling edge */
#define LCDI2C_BL 0x08u   /**< bit P3: backlight, kept on */

/* TWSR status codes (prescaler bits masked off). */
#define TWI_ST_START     0x08u   /**< a START condition has been transmitted */
#define TWI_ST_SLAW_ACK  0x18u   /**< the slave address with write was sent and acknowledged */
#define TWI_ST_DATA_ACK  0x28u   /**< a data byte was sent and acknowledged */
#define TWI_ST_TIMEOUT   0xFFu   /**< not a TWSR value: Wait() gave up (the bus is stuck or the backpack is absent) */

/** Poll until the TWI unit has finished the command. @return 0 on timeout. */
static uint8_t Wait(void)
{
   uint16_t n = LCDI2C_TIMEOUT_LOOPS;

   while (!(TWCR & _BV(TWINT)))
   {
      if (--n == 0u)
      {
         return 0;
      }
   }
   return 1;
}

/** Issue one command and wait for it. @return the status, or TWI_ST_TIMEOUT. */
static uint8_t Step(uint8_t cmd)
{
   TWCR = (uint8_t)(cmd | _BV(TWINT) | _BV(TWEN));
   return Wait() ? (uint8_t)(TWSR & 0xF8u) : (uint8_t)TWI_ST_TIMEOUT;
}

/**
 * START, the display's address for writing, `n` bytes, STOP.
 * @return 1 if every byte was acknowledged. The STOP is sent whatever happened -- it is also what
 * releases the bus after a NACK -- and if even that does not complete (the bus is stuck), the TWI
 * unit is switched off, to be switched on by the next call.
 */
static uint8_t Write(const uint8_t *data, uint8_t n)
{
   uint8_t ok = 0;
   uint16_t guard = LCDI2C_TIMEOUT_LOOPS;

   if (Step(_BV(TWSTA)) == TWI_ST_START)
   {
      TWDR = (uint8_t)(LCDJOYSTICKUI_I2C_ADDRESS << 1);   /* SLA+W */
      if (Step(0) == TWI_ST_SLAW_ACK)
      {
         ok = 1;
         while (n-- != 0u)
         {
            TWDR = *data++;
            if (Step(0) != TWI_ST_DATA_ACK)
            {
               ok = 0;
               break;
            }
         }
      }
   }
   TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN) | _BV(TWSTO));
   while (TWCR & _BV(TWSTO))
   {
      if (--guard == 0u)
      {
         TWCR = 0;
         break;
      }
   }
   return ok;
}

/** One byte to the controller, as an instruction (rs = 0) or as data (rs = LCDI2C_RS). */
static uint8_t Send(uint8_t value, uint8_t rs)
{
   uint8_t hi = (uint8_t)((value & 0xF0u) | LCDI2C_BL | rs);
   uint8_t lo = (uint8_t)((uint8_t)(value << 4) | LCDI2C_BL | rs);
   uint8_t buf[4];

   buf[0] = (uint8_t)(hi | LCDI2C_E);
   buf[1] = hi;
   buf[2] = (uint8_t)(lo | LCDI2C_E);
   buf[3] = lo;
   return Write(buf, 4u);
}

/** One nibble (the controller starts in an unknown interface width, so the first steps are nibbles). */
static uint8_t SendNibble(uint8_t nibble)
{
   uint8_t hi = (uint8_t)((uint8_t)(nibble << 4) | LCDI2C_BL);
   uint8_t buf[2];

   buf[0] = (uint8_t)(hi | LCDI2C_E);
   buf[1] = hi;
   return Write(buf, 2u);
}

uint8_t LcdI2c_Init(void)
{
   uint8_t ok;

   PORTC |= (uint8_t)(_BV(PC4) | _BV(PC5));                      /* SDA, SCL: the internal pull-ups, as Wire did */
   TWSR = 0;                                                     /* prescaler 1 */
   TWBR = (uint8_t)(((F_CPU / LCDJOYSTICKUI_I2C_HZ) - 16UL) / 2UL);
   TWCR = _BV(TWEN);

   /* "Initialization by instruction", HD44780 datasheet: wait > 40 ms after power-up, then three 8-bit
    * function sets with the waits the datasheet gives (4.1 ms, 100 us) -- rounded up -- and the switch to 4 bits. */
   _delay_ms(50);
   ok = SendNibble(0x3u);
   _delay_us(4500);
   ok &= SendNibble(0x3u);
   _delay_us(200);
   ok &= SendNibble(0x3u);
   _delay_us(200);
   ok &= SendNibble(0x2u);
   ok &= Send(0x28u, 0u);                                        /* function set: 4 bits, two lines, 5x8 */
   ok &= Send(0x0Cu, 0u);                                        /* display on, no cursor, no blink */
   ok &= Send(0x01u, 0u);                                        /* clear: the one instruction that takes 1.52 ms */
   _delay_us(1600);
   ok &= Send(0x06u, 0u);                                        /* entry mode: address increments, no shift */
   return ok;
}

uint8_t LcdI2c_SetCursor(uint8_t col, uint8_t row)
{
   return Send((uint8_t)(0x80u | ((row != 0u) ? 0x40u : 0x00u) | col), 0u);
}

uint8_t LcdI2c_PutChar(uint8_t c)
{
   return Send(c, LCDI2C_RS);
}

uint8_t LcdI2c_DefineCharP(uint8_t code, const uint8_t *rowsP)
{
   uint8_t row;
   uint8_t ok = Send((uint8_t)(0x40u | ((code & 7u) << 3)), 0u);   /* the CGRAM address; the controller then increments it itself */

   for (row = 0; row < 8u; row++)
   {
      ok = (uint8_t)(ok & Send(pgm_read_byte(&rowsP[row]), LCDI2C_RS));
   }
   return ok;
}
