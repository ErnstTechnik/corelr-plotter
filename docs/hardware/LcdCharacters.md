# The LCD's characters: the ROM, and the pen pictograms

## The character ROM of the maintainer's display (2026-10-13)
Measured with `tests/bench/T09_LcdRom` (two lines of the 256-code table on the glass, scrolled by the joystick): the controller carries the **A00 ROM** (the Japanese one, the most common on cheap 1602 modules).
What that means for the firmware:
- the codes `0x20..0x7D` are ASCII (with `¥` for `\` and `→`, `←` at `0x7E`, `0x7F`), `0xA1..0xDF` katakana, `0xE0..0xFF` Greek letters and symbols (`0xDF` `°`, `0xFF` a full block);
- there are **no up or down arrows** and no triangles (`0x10..0x1F` are blank): nothing in the ROM reads as "pen up" / "pen down";
- the codes `0x00..0x07` are the user characters (CGRAM), blank until the program defines them.
A display with the A02 (European) ROM would have `↑ ↓ ▲ ▼` at `0x18 0x19 0x1E 0x1F`; the firmware does not rely on them.

## The pen pictograms (`REQ-CoreLR-D-LcdJoystickUI-0032`)
Drawn by the maintainer, 5 x 8 dots, in their own module (`firmware/src/LcdGlyphs.h/.c`), loaded into the CGRAM at start-up (`LcdGlyphs_Load()` -> `LcdI2c_DefineCharP()`, `REQ-CoreLR-D-LcdI2c-0005`) as the codes 1 (down) and 2 (up), shown in column 0 of row 1 of the status screen:

```
 pen DOWN (code 1)    pen UP (code 2)
 . . # . .            . . # . .
 . . # . .            . . # . .
 . . # . .            # # # # #
 . . # . .            . # # # .
 # # # # #            . . # . .
 . # # # .            . . . . .
 . . # . .            . . . . .
 # # # # #            # # # # #
```
Rows (hexadecimal, top first): down `04 04 04 04 1F 0E 04 1F`, up `04 04 1F 0E 04 00 00 1F`.
They can be switched off at build time: `LCDJOYSTICKUI_PEN_GLYPHS` 0 in `SwConfig.h` shows `v` and `^` instead and compiles the tables and the loading out. Cost of the pictograms, measured: **+102 bytes of flash** in each variant (SD 30 422 -> 30 524, serial 28 750 -> 28 852), no RAM.
