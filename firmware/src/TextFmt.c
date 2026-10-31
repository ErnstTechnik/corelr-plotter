/*
 * TextFmt.c
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
 * @file TextFmt.c
 * @brief See TextFmt.h -- and note in particular that this module
 * exists to keep avr-libc's printf family out of the image.
 */

#include <avr/pgmspace.h>
#include "TextFmt.h"

uint8_t TextFmt_Char(char *dst, uint8_t at, uint8_t capacity, char c)
{
   if ((uint16_t)(at + 1u) < capacity)
   {
      dst[at] = c;
      at++;
      dst[at] = '\0';
   }
   return at;
}

uint8_t TextFmt_Str(char *dst, uint8_t at, uint8_t capacity, const char *src)
{
   while ((*src != '\0') && ((uint16_t)(at + 1u) < capacity))
   {
      dst[at] = *src;
      at++;
      src++;
   }
   dst[at] = '\0';
   return at;
}

uint8_t TextFmt_StrP(char *dst, uint8_t at, uint8_t capacity, const char *srcP)
{
   char c;

   while (((c = (char)pgm_read_byte(srcP)) != '\0') && ((uint16_t)(at + 1u) < capacity))
   {
      dst[at] = c;
      at++;
      srcP++;
   }
   dst[at] = '\0';
   return at;
}

uint8_t TextFmt_Uint(char *dst, uint8_t at, uint8_t capacity, uint16_t value)
{
   char digits[5]; /* 65535 is five digits */
   uint8_t count = 0;

   /* Generated least-significant first, then emitted in reverse -- the
    * usual way to avoid needing to know the digit count up front. */
   do
   {
      digits[count] = (char)('0' + (value % 10u));
      count++;
      value = (uint16_t)(value / 10u);
   } while ((value != 0u) && (count < sizeof(digits)));

   while (count > 0u)
   {
      count--;
      at = TextFmt_Char(dst, at, capacity, digits[count]);
   }
   return at;
}

uint8_t TextFmt_Micrometres(char *dst, uint8_t at, uint8_t capacity, int32_t micrometres)
{
   uint32_t magnitude = (micrometres < 0) ? (uint32_t)(0u - (uint32_t)micrometres) : (uint32_t)micrometres;
   uint32_t whole = magnitude / 1000u;
   uint16_t fraction = (uint16_t)(magnitude - (whole * 1000u));

   if (whole > 65535u)
   {
      whole = 65535u;
      fraction = 999u;
   }
   if (micrometres < 0)
   {
      at = TextFmt_Char(dst, at, capacity, '-');
   }
   at = TextFmt_Uint(dst, at, capacity, (uint16_t)whole);
   at = TextFmt_Char(dst, at, capacity, '.');
   at = TextFmt_Char(dst, at, capacity, (char)('0' + (fraction / 100u)));
   at = TextFmt_Char(dst, at, capacity, (char)('0' + ((fraction / 10u) % 10u)));
   return TextFmt_Char(dst, at, capacity, (char)('0' + (fraction % 10u)));
}
