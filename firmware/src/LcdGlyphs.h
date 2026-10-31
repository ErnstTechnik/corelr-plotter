/*
 * LcdGlyphs.h
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
 * @file LcdGlyphs.h
 * @brief The user-defined characters (pictograms) of the display, and the loading of them into its CGRAM.
 *
 * The maintainer's display has the A00 character ROM (no arrows, nothing that reads as a pen: docs/hardware/LcdCharacters.md), so the pictograms are drawn here,
 * 5 x 8 dots each, and written to the controller's CGRAM at start-up through LcdI2c_DefineCharP(). One place for the shapes and for their codes: a screen that needs a
 * new icon adds a table and a code here, and does not touch the UI logic.
 *
 * Realizes: @req{REQ-CoreLR-D-LcdJoystickUI-0032}, @req{REQ-CoreLR-D-LcdI2c-0005}
 * Switch:   LCDJOYSTICKUI_PEN_GLYPHS (SwConfig.h): 0 compiles the module out (the status screen shows `v` and `^`, the tables and the loading cost nothing).
 */

#ifndef CORELR_LCDGLYPHS_H
#define CORELR_LCDGLYPHS_H

#include "SwConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Character codes of the pictograms in the controller's CGRAM. Codes 1..7: a code 0 is the string terminator in C. */
#define LCDGLYPHS_PEN_DOWN 1u   /**< the pen on the paper (a pen above a line, its tip on it) */
#define LCDGLYPHS_PEN_UP   2u   /**< the pen lifted (well above the line) */

#if LCDJOYSTICKUI_PEN_GLYPHS
/**
 * @brief Write every pictogram into the display's CGRAM. Call once, after LcdI2c_Init(). Leaves the controller's address counter in CGRAM: the caller sets the cursor
 * before its next character (the UI's first write after its init is a cursor move).
 */
void LcdGlyphs_Load(void);
#else
#define LcdGlyphs_Load() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* CORELR_LCDGLYPHS_H */
