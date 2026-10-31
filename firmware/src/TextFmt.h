/*
 * TextFmt.h
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
 * @file TextFmt.h
 * @brief Bounded text composition: append characters, strings and
 * decimal integers into a caller-supplied buffer.
 *
 * =====================================================================
 * THIS IS OUR OWN REPLACEMENT FOR THE STANDARD LIBRARY -- NOT avr-libc.
 * =====================================================================
 *
 * Use these instead of snprintf()/snprintf_P() anywhere in this
 * project. Not a matter of taste: pulling in one snprintf_P call
 * costs **1372 bytes of flash**, measured on this firmware --
 * avr-libc's vfprintf (948) plus __ultoa_invert (188), snprintf_P
 * (96), fputc (96) and two strnlen (22 each). The display code used it
 * for nothing but splicing small integers and short strings together,
 * and paying 1372 bytes for that is what pushed the image over the
 * Arduino Nano's 30720-byte flash limit (@req{REQ-CoreLR-D-LcdJoystickUI-0013}).
 *
 * If you find yourself reaching for printf, reach for this instead. If
 * this is missing a conversion you need, add it here rather than
 * falling back to the standard library -- one call site is enough to
 * bring the whole 1372 bytes back.
 *
 * Design: no buffer object and no hidden state. Each function takes
 * the current write position and returns the new one, so composing a
 * line is a chain, and every function keeps the destination
 * NUL-terminated and never writes at or past `capacity`:
 *
 * @code
 *   char line[17];
 *   uint8_t at = TextFmt_StrP(line, 0,  sizeof(line), PSTR("L,R="));
 *   at         = TextFmt_Str (line, at, sizeof(line), lengthL);
 *   at         = TextFmt_Char(line, at, sizeof(line), ',');
 *                TextFmt_Str (line, at, sizeof(line), lengthR);
 * @endcode
 *
 * Truncation is silent by design -- these compose fixed-width display
 * lines, where clipping is the desired behaviour and an error return
 * would only be discarded at every call site.
 */

#ifndef CORELR_TEXTFMT_H
#define CORELR_TEXTFMT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Append one character.
 * @param dst      destination buffer
 * @param at       current write position
 * @param capacity size of `dst`, including the terminator
 * @param c        the character
 * @return the new write position (unchanged if the character did not fit)
 */
uint8_t TextFmt_Char(char *dst, uint8_t at, uint8_t capacity, char c);

/** @brief Append a RAM-resident string. @see TextFmt_Char */
uint8_t TextFmt_Str(char *dst, uint8_t at, uint8_t capacity, const char *src);

/**
 * @brief Append a flash-resident string, i.e. a PSTR("...") literal or
 * a PROGMEM array. @see TextFmt_Char
 *
 * Prefer this over TextFmt_Str() for every *fixed* string: on classic
 * AVR a plain literal is copied into RAM at boot, which is exactly the
 * cost this project has fought elsewhere.
 */
uint8_t TextFmt_StrP(char *dst, uint8_t at, uint8_t capacity, const char *srcP);

/**
 * @brief Append an unsigned value in decimal, no padding.
 *
 * 16-bit deliberately: a 16-bit divide-by-ten is markedly cheaper than
 * a 32-bit one on this target, and 65535 covers every quantity this
 * firmware displays -- millimetre counts, servo angles, list indices.
 * Widen only if something genuinely needs it, and then as a separate
 * function so the cheap path stays cheap.
 */
uint8_t TextFmt_Uint(char *dst, uint8_t at, uint8_t capacity, uint16_t value);

/**
 * @brief Append a length in micrometres as millimetres with three decimals: `-123.456`, `0.000`, `7.008`.
 *
 * Always three decimals (a status report is parsed by senders, which want a fixed shape), a minus sign only for a negative value
 * (so `-1` is `-0.001` and `0` is `0.000`). The magnitude is limited to 65 535.999 mm, far beyond the machine's reach (2 097 mm):
 * a larger one is shown as 65535.999. Used by the status report, @req{REQ-CoreLR-D-SerialLink-0005}.
 * @see TextFmt_Char
 */
uint8_t TextFmt_Micrometres(char *dst, uint8_t at, uint8_t capacity, int32_t micrometres);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_TEXTFMT_H */
