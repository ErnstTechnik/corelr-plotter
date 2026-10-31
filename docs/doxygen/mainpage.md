<!-- Copyright (C) 2026 ErnstTechnik and the CoreLR contributors -- GPL-3.0-or-later. Read by Doxygen only (docs/doxygen/Doxyfile.in), as the main page. -->
# CoreLR plotter firmware: API reference {#mainpage}

\image html logo-wordmark.svg "" width=360px

Firmware of a cable-driven wall plotter: two stepper motors, **L** (left) and **R** (right), each paying out one cable; a pen raised and lowered by a servo;
an Arduino Nano (ATmega328P, 2 KB of RAM, 30 720 bytes of flash). This is the reference of the *code*: every function, type and tunable that carries a comment.
The *why* is in the requirements (`requirements/functional/` for what, `requirements/design/` for how: the `REQ-CoreLR-...` numbers quoted in the comments),
the wiring and the protocols in `docs/`, the dated evidence in `docs/bringup/Bringup-Status.md`.

\image html machine-schematic.svg "The machine: two motors L and R, one cable each, meeting at the pen; D is the distance between the motors, the dashed rectangle is the travel zone" width=80%

## One documentation per image
The sources build **two images**, chosen at build time by `CORELR_JOB_SOURCE`: `sd` (jobs read from an SD card) and `serial` (jobs streamed by Candle 1.1.9 over
the serial link, a grbl 1.1 subset). Each image has its own documentation, generated as the compiler sees that image: the code that exists in only one of them
appears only in its documentation.

\if serial
**This is the documentation of the `serial` image.** The SD card code (the `SDReader` module, the SD browser of `LcdJoystickUI.cpp`) is not part of it; the
serial link (the `Uart` and `SerialLink` modules, `GCodeParser_ExecuteJog()`, `G53`, `$H`...) is.
\endif
\if sd
**This is the documentation of the `sd` image.** The serial link (the `Uart` and `SerialLink` modules, `GCodeParser_ExecuteJog()`, `G53`, `$H`...) is not part of it; the SD
card code (the `SDReader` module, the SD browser of `LcdJoystickUI.cpp`) is.
\endif

It documents the **shipped configuration**: the 48-bit arithmetic (`CORELR_MATH_48`; the 64-bit code, e.g. `Motion_ISqrt()`, is not shown), the default
`SwConfig.h` selectors, and no debug switch (`CORELR_DEBUG_*` are off). The vendored code the image is built with is read as it is and grouped under *Third-party code*;
it is not ours, so its warnings are not counted as errors. Not documented here, on purpose: `tests/` and `tools/`.

## Types and assembler
The fixed-width integers (`uint8_t`, `int32_t`...) are those of `<stdint.h>` from avr-libc: they are not documented here (the Arduino core's own types are, under *Third-party code*).
There is **no hand-written assembler** in the firmware. The C files hold three `__asm__` statements, compiler barriers and a register constraint (`Uart.c`, `Motion.c`), which stay in
the source listings. The vendored core has one assembly file, `wiring_pulse.S` (for `pulseIn()`): Doxygen does not read `.S` files, and neither image links it (checked in the two map files).

## Where to start
The *Files* entry of the menu lists every module of this image with the `@brief` of its header, so it is always the list of what is in it (a module that belongs to the
other image is left out of this documentation). The sketch `grbl_corelr.ino` is the place to start: `setup()`, `loop()`, the job state machine, and the glue between
the modules; the modules themselves are one `.h` and one `.c` (or `.cpp`) each.

The machine itself, with its wiring pictures, is the chapter @subpage hardware. The GCode that this image accepts, with the answer of its parser to each line, is the page @subpage gcode. The requirements that the comments cite are the chapter @subpage requirements; the contract of the serial link (the `serial` image's: the `sd` documentation says so in its title) is the page @subpage SerialProtocol. The layout of the EEPROM (the calibration, and the ring that keeps the position), common to both images, is the page @subpage EepromLayout.

The tunables are in two groups of topics: @ref CoreLR_HwConfig (pins and mechanical facts, `HwConfig.h`) and @ref CoreLR_SwConfig (queue sizes, timing, the variant
selector, `SwConfig.h`).

## How this page is produced
`cmake --build --preset host --target doc` (or any preset: the AVR toolchain is not used) writes `build/doxygen/serial/html/` and `build/doxygen/sd/html/` (`doc_serial`
and `doc_sd` make one), a `build/doxygen/index.html` that links both, and the Doxygen warnings of each to its `doxygen-warnings.log`; `tools/analysis/doc_warnings.py` counts them per module. The target is not part of the firmware build and changes no image.
