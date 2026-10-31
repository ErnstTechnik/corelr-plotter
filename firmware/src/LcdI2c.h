/*
 * LcdI2c.h
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
 * @file LcdI2c.h
 * @brief A character LCD (HD44780) behind a PCF8574 I2C backpack: the three
 * operations the display code needs, over a polled TWI master.
 *
 * =====================================================================
 * OUR OWN REPLACEMENT FOR Wire AND LiquidCrystal_PCF8574 -- NOT THE LIBRARIES.
 * =====================================================================
 *
 * Measured on this firmware (Arduino Nano build, 2026-10-07, the whole image before
 * and after): replacing the two libraries with this module took the flash from 32 932
 * to 30 858 bytes (-2 074) and the RAM from 1 619 to 1 350 (-269, of which 17 are a
 * buffer the display code no longer needs). What went: Wire's interrupt-driven TWI
 * state machine and its vector (574), endTransmission (462), the rest of TwoWire and
 * twi_* (~700), the library and its constructor (~500), and 160 bytes of buffers for a
 * master that sends five bytes at a time. This module's own functions are 216 bytes.
 * The vector was also the longest interrupt in the latency chain of the step timer
 * (@req{REQ-CoreLR-D-Stepper-0012}). Same protocol, no interrupt, no buffer, no object.
 *
 * Do not "simplify" it back to the libraries to save writing: they bring the
 * bytes back, and Wire's waits have no timeout, so a stuck bus froze the
 * main loop for ever. See @req{REQ-CoreLR-D-LcdI2c-0001}..@req{REQ-CoreLR-D-LcdI2c-0004}.
 *
 * Supported: a 2-line display of up to 40 columns on the usual backpack
 * wiring -- P0 RS, P1 RW (held low), P2 E, P3 backlight (held on), P4..P7 D4..D7
 * -- at LCDJOYSTICKUI_I2C_ADDRESS. The characters are those of the controller's ROM.
 *
 * Blocking: a call polls the bus and returns when the transaction is over,
 * 0.47 ms for one character at 100 kHz (start, 5 bytes of 9 clocks, stop: 47
 * clocks -- protocol arithmetic, checked by tests/hosttest's model, not a bench
 * figure), exactly what the libraries took. Interrupts stay enabled throughout.
 *
 * Failure: every call returns 1 if the display acknowledged every byte, 0 if
 * not (absent, wrong address, bus stuck). A call is bounded: no wait exceeds
 * LCDI2C_TIMEOUT_LOOPS polls, and a failed call releases the bus. The caller
 * decides what a failure means; nothing here retries.
 *
 * Known limitation, measured (@req{REQ-CoreLR-D-LcdI2c-0004}): a failure after the first
 * nibble of a byte was strobed (the 3rd or 4th data byte of a call NACKed) leaves the
 * controller holding half a byte, and from then on it pairs the wrong nibbles: the
 * display garbles. LcdI2c_Init() re-synchronises it, from any phase. The libraries had
 * the same weakness.
 */

#ifndef CORELR_LCDI2C_H
#define CORELR_LCDI2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the TWI unit at LCDJOYSTICKUI_I2C_HZ and run the controller's
 * "initialisation by instruction" sequence: 4-bit interface, two lines,
 * display on, cleared, left to right, backlight on. Blocks ~60 ms, mostly the
 * controller's power-up and reset times. Call once from setup().
 * @return 1 if the display answered throughout.
 */
uint8_t LcdI2c_Init(void);

/** @brief Move the cursor. `row` 0 or 1. @return 1 on success. */
uint8_t LcdI2c_SetCursor(uint8_t col, uint8_t row);

/**
 * @brief Write one character at the cursor, which then advances by one
 * (the controller increments its own address counter).
 *
 * Note for callers: a row is longer in the controller's memory than on the
 * glass -- after the last visible column the address counter runs on into
 * memory that is not displayed, not to the next row. A caller tracking the
 * cursor must treat the end of a row as "unknown".
 * @return 1 on success.
 */
uint8_t LcdI2c_PutChar(uint8_t c);

/**
 * @brief Define the user character `code` (0..7, CGRAM): eight row bytes read from FLASH at `rowsP`, top row first, the five low bits of each being the dots of the row
 * (@req{REQ-CoreLR-D-LcdI2c-0005}). Leaves the controller's address counter in CGRAM: set the cursor before the next character. Use codes 1..7 (0 is the string terminator in C).
 * @return 1 on success.
 */
uint8_t LcdI2c_DefineCharP(uint8_t code, const uint8_t *rowsP);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_LCDI2C_H */
