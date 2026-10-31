# Walldraw: the reference kit

The firmware of this project is a from-scratch design; only the **mechanics** come from Walldraw, an open-source cable-driven wall plotter.
Last reviewed 2026-10-26.

> **The kit's wiring is NOT the wiring used here.** The kit is an Arduino UNO with a screw-terminal shield, the motors on other pins, the SD reader's chip-select on `D4`
> and the servo on `D13`; this project is an Arduino **Nano** with its own pin map, an LCD and a joystick ([`PinMapping.md`](PinMapping.md)). Do not wire a CoreLR board
> from the kit's manual, and do not flash the kit's sketches on this wiring (or the reverse). The comparison is in "Wiring: kit versus CoreLR".

## The reference
- **Walldraw** ("墙画机", wall-drawing machine): sketches and an installation manual, in Chinese, at <https://github.com/shihaipeng03/Walldraw>. The repository we examined carries no
  `LICENSE` file: check its terms before reusing anything. This project copies none of its code.
- It is sold as a **kit** through Chinese marketplaces (the sketch's own page points to a Taobao shop). Listings are sold under varying names, and their photos show **two
  versions of the mechanics** with the same principle: in one the motors sit on the base plate, in the other each motor has its own small bracket, connected to the base only by
  its cables. A listing's text rarely says which kit it is: compare the photos and the parts list below before ordering.
- Not to be confused with another project of the same name, a 3D-printed Walldraw on Printables: different author, different parts.
- The author's article explaining the principle ("如何将爱因斯坦画到墙上", Zhihu: <https://zhuanlan.zhihu.com/p/71602189>).

## What the reference contains
All in Chinese; this is a summary, not a translation.
- **`Walldraw.ino`**, the main sketch: it reads G-code from the SD card (`1.nc`) or from the serial port (115 200 baud), turns each move into the two cable lengths, and steps the
  motors with the `TinyStepper_28BYJ_48` library; the pen is a hobby servo (60 degrees up, 95 down). Its constants: `STEPS_PER_TURN` **2048**, `SPOOL_DIAMETER` 35 mm, hence 0.0537 mm
  of cable per step (*2048 is the "64:1" figure of the data sheets; the real ratio of the 28BYJ-48 is 63.68395:1, i.e. 2037.886 steps a turn, which CoreLR uses since 2026-10-26*),
  `X_SEPARATION` 507 mm (the distance between the two cable exits), `LIMYMIN`/`LIMYMAX` ±440 mm, the origin at the middle of the board.
- **`2Steper.ino`**, a test sketch that makes the two motors draw a cycloid, to check the wiring and the directions.
- **The installation manual** ("Wall Drawing Machine 2020 安装调试说明"): mounting, wiring diagram, performance figures (mean pen speed 13.7 mm/s, typical 9 to 27 mm/s), how the
  parameters are chosen (the distance between the cable exits, and the vertical distance from the exits to the pen at power-on, which must be measured and respected at every start,
  or the drawing is distorted), the first steps (flash `2Steper`, then the demo, test without the pen), how to make G-code (vector drawings from Inkscape and similar; the card in
  FAT32, 16 GB or less), and a FAQ.
- **The article**: what a two-motor, two-cable plotter (Hektor, V-plotter, Polargraph, Drawbot) is, why the drawing area is limited (the pull tends to infinity near the top and gets too
  weak at the sides), and how to make a drawing from a photograph (black and white, then vector, then G-code).

What the manual says about accuracy (useful for [`CableScaleMeasurement.md`](CableScaleMeasurement.md)): with a 0.16 mm thread, 20 turns of layers change the spool's diameter by
0.2 mm, judged negligible (0.3 mm of error over a 2 m picture); the thread's own thickness added once to the diameter is not counted (0.16 mm = 0.46 % of 35 mm). It also warns that the pen
cannot reach the top limit (the pull tends to infinity) nor the sides (too little tension).

## Wiring: kit versus CoreLR
| | Kit (manual, `Walldraw.ino`) | CoreLR ([`PinMapping.md`](PinMapping.md), `HwConfig.h`) |
|---|---|---|
| Board | Arduino **UNO** + screw-terminal shield | Arduino **Nano** (30 720 B of flash, A6/A7 analog-only) |
| Motor L ("M1") | `IN1..IN4` on `D7, D8, D9, D10` | `D6, D7, D8, D9` |
| Motor R ("M2") | `IN1..IN4` on `D2, D3, D5, D6` (`D4` skipped: SD) | `D2, D3, D4, D5` |
| SD card | CS on `D4`, MOSI `D11`, MISO `D12`, SCK `D13` | CS on **`D10`**, SPI `D11-D13` (hardware SPI) |
| Pen servo | `D13` in the manual, `A0` with the SD reader (the sketch uses `A0`) | **`A0`** |
| User interface | none (a PC over USB, or the card) | 2x16 LCD on I2C (`A4/A5`) + analog joystick (`A1-A3`) |
| Serial | G-code at 115 200 baud (a sketch option) | the serial variant: grbl 1.1 subset for Candle, `D0/D1` |
| Motor direction | `M1_REEL_OUT/IN` constants in the sketch | the `STEPPER_MOTOR_*_REVERSED` flags of `HwConfig.h` (bench-determined) |

The motors and their ULN2003 boards, the spools, the servo and the SD module are the same parts; **what changes is which pin each wire goes to**. Only the ribbon ends of the
motor boards are common: in both cases `IN1..IN4` are connected in order.

## Which mechanics
The firmware does not depend on the version. The model takes the two cable exits as the pivots and `D` is measured between them (`REQ-CoreLR-F-Calibration-0008`), wherever the brackets
are fixed. Which motor is "L" and which way each one turns depends on how each bracket and its cable are mounted: that is what `STEPPER_MOTOR_L_REVERSED` and
`STEPPER_MOTOR_R_REVERSED` (`HwConfig.h`) settle, determined on the bench. The machine this firmware was developed on is the version with the motors on their own brackets.

## What the kit is made of (from its manual)
| Part | What the manual says | In CoreLR |
|---|---|---|
| Stepper motors | 2 x **28BYJ-48** with a "64:1" gear (the real ratio is 63.68395:1: `MOTION_STEPS_PER_REV_X1000`), each on a ULN2003 driver board | the same |
| Spools | **35 mm** diameter, the cable wound on it (clockwise on the left, counter-clockwise on the right); the cable leaves through a plastic eyelet in the motor bracket, which is the pivot of the model | the same; the exit hole is the pivot |
| Cable | a thread of **0.16 mm** that holds 10 kg, tied to the pen holder with a loop | a sewing thread, very thin |
| Pen holder | a 3-part holder hung on the two cables, the pen pivoting on a screw; a **9 g servo** (SG90 type) lifts it by 5 to 10 degrees; a counterweight if the pen is not the kit's own | the servo on `A0` |
| Controller | an **Arduino UNO** (or compatible) on a screw-terminal shield, an optional **SD/TF reader**, 5 V supply | a **Nano**: the wiring differs (above) |
| Structure | a base plate with 12 brass standoffs, two motor brackets to fix on the wall or the board (as level as possible), screws | the same mechanics |

## Where to buy
- **The kit as designed**: Chinese marketplaces (Taobao, AliExpress). Check any listing against the parts above; one may be missing parts or be a different kit.
- **The generic parts**, in any online electronics shop: a 28BYJ-48 + ULN2003 board (sold as a pair), an SG90 9 g servo, an Arduino Nano, an SD/microSD SPI module, a 1602 I2C LCD, a thread.
- **The specific parts** (brackets, spools, base, pen holder) have no known second source; the 35 mm spool is the figure of the firmware (`MOTION_SPOOL_DIAMETER_UM`).
