/*
 * LcdGlyphs.c
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

/** @file LcdGlyphs.c @brief The pictograms and their loading (@req{REQ-CoreLR-D-LcdJoystickUI-0032}). See LcdGlyphs.h. */

#include <avr/pgmspace.h>
#include "LcdGlyphs.h"
#include "LcdI2c.h"

#if LCDJOYSTICKUI_PEN_GLYPHS

/* 5 x 8 dots, top row first, the five low bits of a byte being the dots of a row: a pen above a line of paper, its tip on the line (down) or well above it (up).
 *
 *   pen DOWN (1)         pen UP (2)
 *   . . # . .  04        . . # . .  04
 *   . . # . .  04        . . # . .  04
 *   . . # . .  04        # # # # #  1F
 *   . . # . .  04        . # # # .  0E
 *   # # # # #  1F        . . # . .  04
 *   . # # # .  0E        . . . . .  00
 *   . . # . .  04        . . . . .  00
 *   # # # # #  1F        # # # # #  1F
 */
static const uint8_t kPenDownGlyph[8] PROGMEM = { 0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x1F };   /**< the pen-down pictogram: 8 rows of 5 dots, as drawn above */
static const uint8_t kPenUpGlyph[8]   PROGMEM = { 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00, 0x00, 0x1F };   /**< the pen-up pictogram */

/* SwConfig.h cannot include this header (this one includes it), so it states the two codes by hand: they must agree, or the status screen shows the wrong character. */
typedef char LcdGlyphs_PenDownCodeAgrees_t[(LCDJOYSTICKUI_PEN_DOWN_CHAR == LCDGLYPHS_PEN_DOWN) ? 1 : -1];   /**< compile-time check, no object: a negative size if the two pen-down codes disagree */
typedef char LcdGlyphs_PenUpCodeAgrees_t[(LCDJOYSTICKUI_PEN_UP_CHAR == LCDGLYPHS_PEN_UP) ? 1 : -1];   /**< same for pen up */

void LcdGlyphs_Load(void)
{
   (void)LcdI2c_DefineCharP(LCDGLYPHS_PEN_DOWN, kPenDownGlyph);
   (void)LcdI2c_DefineCharP(LCDGLYPHS_PEN_UP, kPenUpGlyph);
}

#endif /* LCDJOYSTICKUI_PEN_GLYPHS */
