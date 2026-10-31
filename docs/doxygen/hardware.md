# Hardware {#hardware}
<!-- Copyright (C) 2026 ErnstTechnik and the CoreLR contributors -- GPL-3.0-or-later (the text). The wiring pictures it shows are CC BY-SA 3.0: see "Credits and licences" below. Read by Doxygen only (docs/doxygen/Doxyfile.in). -->

What the firmware drives. The pins are not repeated here: they are those of @ref CoreLR_HwConfig (`HwConfig.h`), and the wiring pictures below are **generated from that file**
(`tools/generators/gen_wiring.py`, which refuses a pin used twice and checks the drawn nets against `HwConfig.h`). The full wiring guide, with the reasons for each choice, is
`docs/hardware/PinMapping.md` in the repository; the kit this machine derives from is described in `docs/hardware/Walldraw.md`.

An **Arduino Nano** (ATmega328P, 2 KB of RAM, 30 720 bytes of flash after the bootloader): not an Uno. **Every digital and analog pin is allocated; none is spare.**
\if serial
In this image the serial link uses `D0`/`D1` (the USB port of the Nano, towards the PC). The SD card module of the other image is part of the wiring but this image does not use it.
\endif
\if sd
In this image the SD card module uses the hardware SPI. `D0`/`D1` (the USB port) stay reserved for the serial link of the other image and for flashing.
\endif

## The whole machine
\image html overview.svg "The whole wiring: the Nano, the two ULN2003 boards with their 28BYJ-48 motors, the pen servo, the joystick, the LCD, the SD card module and the external supply" width=100%

**Power.** The two motors and the servo draw far more than the Nano's regulator can give: they are fed from a **separate 5 V supply**, whose ground is the Nano's ground. As built, the same
supply also feeds the Nano's `5V` pin (with the SD image the USB port is not connected, so the supply is the Nano's only source); the LCD, the joystick and the SD module are powered from
that pin. The picture also draws a **proposed** Schottky diode (1N5817) between the supply and the `5V` pin, which would stop the USB 5 V and the supply from being tied together when both are
connected: it is **not built**. Nothing in the firmware measures or manages the supply current: it is a wiring discipline.

## The motors
\image html motors.svg "Motors R and L: each 28BYJ-48 through a ULN2003 board, IN1 to IN4 in order, fed from the external supply" width=100%

Two 28BYJ-48 stepper motors, **L** (left) and **R** (right), each winding one cable, through a ULN2003 driver board each. Motor R is on the first four pins, motor L on the next four, `IN1` to
`IN4` in order (the ribbon of the board). The firmware keeps two phases energised at all times (a two-phase-on sequence); the real ratio of the gearbox is 63.68395:1 (2037.886 steps a turn).

## The pen servo
\image html pen_servo.svg "The pen servo, its signal on one analog pin" width=70%

A hobby servo raises and lowers the pen; the `PenControl` module drives it and asks the step queue to change the pen in step with the position.

## The joystick
\image html joystick.svg "The joystick: X and Y on two analog inputs, the push button on a third pin (pulled up)" width=70%

Two analog axes and a push button (not a rotary encoder): the jog, the menu and the acknowledgment of an error (`LcdJoystickUI`).

## The LCD
\image html lcd_i2c.svg "The 2x16 LCD with its I2C backpack: SDA on A4, SCL on A5" width=70%

A 2x16 character LCD behind a PCF8574 backpack on the I2C bus, whose pins (`A4`, `A5`) are fixed by the silicon. The backpack's address is `0x27` or `0x3F` according to the board
(`HwConfig.h`); the driver is the project's own, `LcdI2c`.

\if sd
## The SD card module
\image html sd_card.svg "The SD card module on the hardware SPI: MOSI, MISO and SCK on the pins of the silicon, the chip select on a pin of ours" width=70%

On the hardware SPI: `D11` (MOSI), `D12` (MISO) and `D13` (SCK) are fixed by the silicon; the chip select is `SDREADER_CS_PIN` (`HwConfig.h`), which is also the SPI `SS` pin (an output, as the AVR requires).
A compile-time guard of this image rejects a pin map where a motor wire or the chip select sits on an SPI pin.
\endif

## The frame and the cables
The machine is two motors on the wall, a distance **D** apart, and a pen that hangs from two cables that can only pull. The drawing frame has **+Y up the wall** and an origin the user sets;
the internal frame has its origin at motor L. The picture shows the dimensions to measure, the two frames, the calibration and the cable lengths in a test square. **The numbers on it are an
example (D = 1000 mm, reference cable 700 mm), not the maintainer's machine.**

\image html Geometry.svg "The machine's geometry: the dimensions to measure, the two coordinate frames, the calibration and the cable lengths" width=100%

Where the cables are nearly horizontal each has to pull many times the weight of the pen, and outside a motor's vertical nothing holds it. That is why the firmware never commands a position
outside the **travel zone**: 20 mm inside the vertical of each motor and 50 mm below the line joining the motors (`MOTION_MARGIN_SIDE_UM`, `MOTION_MARGIN_TOP_UM` in @ref CoreLR_SwConfig).
With the top margin at 50 mm the cables carry at most about five times the weight of the pen; the chart (D = 1000 mm, a 15 g pen assembly) shows the tension over the zone.

\image html Tension.svg "Cable tension over the machine's area, for D = 1000 mm and a 15 g pen assembly" width=100%

## Credits and licences
The wiring pictures (`overview`, `motors`, `pen_servo`, `joystick`, `lcd_i2c`, `sd_card`) were *created with parts from Fritzing* (https://fritzing.org,
https://github.com/fritzing/fritzing-parts), whose graphics are published under the **Creative Commons Attribution-ShareAlike 3.0 Unported** licence
(https://creativecommons.org/licenses/by-sa/3.0/). The parts are used unmodified and embedded, scaled, in a larger drawing, so **each of these pictures is an adaptation and is
also CC BY-SA 3.0** (not GPLv3: the firmware, the tools and the text are unaffected). Each picture carries this credit in its own metadata. The authors, from the part definitions:

| Part | Author |
|---|---|
| Arduino Nano (Rev3.0) | Althaus (2013) |
| Red LED 5 mm and 220 ohm resistor (style reference) | Brendan Howell (2008, 2009) |
| Basic servo | Stefan Hermann (2008) |
| Thumb joystick (SparkFun breakout) | SparkFun (2011) |
| MicroSD card module | Richard Bruneau and F. Althaus (2015) |
| Schottky diode 1N5817 | Andre Knorig (2010) |

The ULN2003 board with its 28BYJ-48 motor, the I2C LCD and the external supply are drawn by the project, in the same style, and follow the licence of the picture that contains them.
Copyright (C) 2026 ErnstTechnik and the CoreLR contributors for the arrangement and the wiring. The geometry and tension pictures are generated by the project's tools
(`gen_geometry_svg.py`, `gen_tension_chart.py`) and are not Fritzing material. The provenance of the parts is `third_party/fritzing-parts/PROVENANCE.md`.
