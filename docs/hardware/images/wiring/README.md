# Wiring diagrams

The pictures of the machine's wiring, in the Arduino "breadboard" style (the parts as they look, wires pin to pin). Vector (SVG), **generated** by
[`tools/generators/gen_wiring.py`](../../../../tools/generators/gen_wiring.py). **The pins are not written in the tool**: the configurable ones (motors, servo, joystick, SD chip
select) are read from [`HwConfig.h`](../../../../firmware/config/HwConfig.h), and the ones the silicon fixes (SPI, I²C) from the vendored Arduino core's
[`pins_arduino.h`](../../../../third_party/arduino-core/variants/standard/pins_arduino.h). Change a pin there, run the tool again: the wires move (the tool refuses a pin used
twice, or on an edge of the Nano the layout does not draw). What the tool does decide is the power wiring (PinMapping.md and the maintainer's statement) and the layout.

| Diagram | Shows |
|---|---|
| [`overview.svg`](overview.svg) | **the complete drawing**: Nano, both ULN2003 boards with their 28BYJ-48, pen servo, joystick, LCD, SD module, external supply, **with a Schottky diode (1N5817) between the supply and the Nano's `5V` pin** (a proposed variant, not built yet: the other diagrams show the machine as built, without it) |
| [`motors.svg`](motors.svg) | motors R (D2–D5) and L (D6–D9), IN1..IN4 in order, fed from the external supply |
| [`pen_servo.svg`](pen_servo.svg) | the pen servo on A0 |
| [`joystick.svg`](joystick.svg) | the joystick: X on A1, Y on A2, button on A3 |
| [`lcd_i2c.svg`](lcd_i2c.svg) | the 2×16 LCD on I²C: SDA A4, SCL A5 |
| [`sd_card.svg`](sd_card.svg) | the SD module on the hardware SPI: CS D10, MOSI D11, MISO D12, SCK D13 |
| [`nano_led.svg`](nano_led.svg) | style reference: a LED on D13 through 220 Ω (not part of the machine) |

**Power, as the maintainer built it** (2026-10-14): the external 5 V feeds the boards, the servo **and** the Nano's `5V` pin (in SD mode the USB port is not connected);
the grounds are common. The LCD, the joystick and the SD module are powered from the Nano's `5V`. Drawn as built in `motors` and `pen_servo`; `overview` adds the proposed series diode, which stops the USB 5 V and the external 5 V from feeding each other (the Nano then runs on the supply's 5 V less the diode's drop).

**Checked, not just drawn**: `gen_wiring.py --verify` rebuilds the electrical nets from the drawn wires (each signal, the +5 V, the ground) and compares them with
`HwConfig.h` and the core; a short or an open net is an error. `tests/tools/test_wiring.py` tests that check (44 mutants in `tests/mutation/mutants/wiring.txt`, all caught).

## Licence and credit
These drawings embed parts of **Fritzing** (<https://fritzing.org>, <https://github.com/fritzing/fritzing-parts>), published under
[**CC BY-SA 3.0**](https://creativecommons.org/licenses/by-sa/3.0/): *created with parts from Fritzing*. As an adaptation, **each SVG here is also CC BY-SA 3.0**
(not GPLv3: the firmware, the tools and the text are unaffected), and carries the credit in its own `<metadata>`.
Copyright (C) 2026 ErnstTechnik and the CoreLR contributors for the arrangement, the wiring and the parts drawn by the project (the ULN2003 board with its 28BYJ-48,
the I²C LCD, the supply); the other part artwork belongs to its authors, listed in
[`third_party/fritzing-parts/PROVENANCE.md`](../../../../third_party/fritzing-parts/PROVENANCE.md).

Regenerate: `tools/generators/gen_wiring.py --all` (or `gen_wiring.py <diagram>`; `--list` for the names).
