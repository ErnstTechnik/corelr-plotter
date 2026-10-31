/*
 * HwConfig.h
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
 * @file HwConfig.h
 * @brief Hardware facts: pin assignments and fixed mechanical parameters.
 *
 * Every \#define in this file describes something about the physical
 * board/wiring, never a tunable software behaviour (see
 * SwConfig.h for that). See docs/hardware/PinMapping.md for the full
 * board pin allocation, the per-connector wiring list, and the
 * rationale behind both.
 *
 * Realizes: @req{REQ-CoreLR-F-Calibration-0001} (fixed mechanical parameters)
 * Informs:  @req{REQ-CoreLR-D-Stepper-0001}..@req{REQ-CoreLR-D-Stepper-0005}, @req{REQ-CoreLR-D-PenControl-0001},
 *           @req{REQ-CoreLR-D-LcdJoystickUI-0001}, @req{REQ-CoreLR-D-SDReader-0004}
 *
 * Note: the per-wire port/DDR/bit derivation and the SET/CLR/DDR_SET
 * register macros built from the pin numbers below live in
 * Stepper.c, not here (minimum-scope principle,
 * confirmed 2026-09-06 — nothing else uses them; this header only
 * needs to state the raw pin facts, which is what several other
 * modules actually consume via pinMode()/analogRead()/the Servo library's attach call).
 *
 * Bit-level view of the board (full rationale: docs/hardware/PinMapping.md).
 * Only 6 bits remain free in PORTD once serial (D0/D1) is reserved, and
 * only 2 in PORTB once hardware SPI and the SD chip-select claim
 * D10-D13 -- exactly 8 for the two motors, with nothing left over:
 *
 *   PORTD (8 bits)
 *   bit:   7    6    5    4    3    2    1    0
 *   pin:  D7   D6   D5   D4   D3   D2   D1   D0
 *        +----+----+----+----+----+----+----+----+
 *        | L2 | L1 | R4 | R3 | R2 | R1 | TX | RX |
 *        +----+----+----+----+----+----+----+----+
 *         \__/  \____Motor R (IN1-IN4)___/ \___/
 *        Motor L                             serial
 *        (IN1-IN2)                          (reserved)
 *
 *   PORTB (6 usable bits -- PB6/PB7 are the crystal, not GPIO)
 *   bit:    5     4     3     2     1    0
 *   pin:   D13   D12   D11   D10   D9   D8
 *        +-----+-----+-----+-----+----+----+
 *        | SCK | MISO| MOSI| CS  | L4 | L3 |
 *        +-----+-----+-----+-----+----+----+
 *         \_____SD (hw SPI)_____/ \_Motor L_/
 *                                  (IN3-IN4)
 *
 * Motor L straddles the PORTD/PORTB boundary, and that costs nothing:
 * the ISR sets each wire with its own sbi/cbi on a compile-time bit
 * position (@req{REQ-CoreLR-D-Stepper-0005}), so there is no per-port grouping
 * to preserve. Masked whole-port writes were tried in an early revision
 * and rejected; the "give a motor a contiguous nibble" reasoning that
 * came with them is obsolete and was dropped on 2026-09-10.
 */

#ifndef CORELR_HWCONFIG_H
#define CORELR_HWCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup HwConfig_Motors Motor output pins (docs/hardware/PinMapping.md)
 * @ingroup CoreLR_HwConfig
 *
 * Laid out to follow the connectors (revised 2026-09-10): Motor R takes
 * D2-D5 and Motor L takes D6-D9, each in IN1..IN4 order, so the ribbon
 * from each ULN2003 board lands on four consecutive Arduino pins in the
 * same order it leaves the driver.
 *
 * There is no register-level reason to prefer any other arrangement:
 * the ISR drives each wire with its own single-cycle sbi/cbi on a
 * compile-time-constant bit position (@req{REQ-CoreLR-D-Stepper-0005}), so
 * whether a motor's four bits are contiguous, or even in the same port,
 * makes no difference to the generated code. Motor L therefore straddles
 * PORTD (D6,D7) and PORTB (D8,D9) with no penalty. Earlier revisions of
 * this file justified the layout by "one motor gets a contiguous
 * nibble", which belonged to a masked whole-port write scheme that was
 * tried and rejected -- see docs/hardware/PinMapping.md.
 *
 * D0/D1 stay free for serial, D10-D13 for the SD card (chip-select plus
 * hardware SPI). The port/bit values below are for the reader only:
 * Stepper.c derives them from the pin numbers with `#if`, so a stale
 * comment here can never silently drive the code.
 *
 * Each PINn drives the ULN2003 input of the same rank -- PIN0 to IN1,
 * PIN1 to IN2, PIN2 to IN3, PIN3 to IN4 -- which is what Motion.c's
 * 4-phase table expects (kPhaseTable, bit 0 = IN1, verified against
 * Walldraw's TinyStepper_28BYJ_48). docs/hardware/PinMapping.md carries the
 * per-connector wiring list with the 28BYJ-48 wire colours.
 * @{
 */
#define STEPPER_MOTOR_R_PIN0  2   /**< PORTD bit 2 -> ULN2003 IN1 */
#define STEPPER_MOTOR_R_PIN1  3   /**< PORTD bit 3 -> ULN2003 IN2 */
#define STEPPER_MOTOR_R_PIN2  4   /**< PORTD bit 4 -> ULN2003 IN3 */
#define STEPPER_MOTOR_R_PIN3  5   /**< PORTD bit 5 -> ULN2003 IN4 */

#define STEPPER_MOTOR_L_PIN0  6   /**< PORTD bit 6 -> ULN2003 IN1 */
#define STEPPER_MOTOR_L_PIN1  7   /**< PORTD bit 7 -> ULN2003 IN2 */
#define STEPPER_MOTOR_L_PIN2  8   /**< PORTB bit 0 -> ULN2003 IN3 */
#define STEPPER_MOTOR_L_PIN3  9   /**< PORTB bit 1 -> ULN2003 IN4 */
/** @} */

/**
 * @defgroup HwConfig_Servo Pen servo pin
 * @ingroup CoreLR_HwConfig
 * @{
 */
#define PENCONTROL_SERVO_PIN 14  /**< A0 = digital 14 (PORTC bit 0); matches Walldraw/Walldraw.ino's pen.attach(A0). A number, not the Arduino symbol: PenControl.c is plain C and includes no Arduino header */
/** @} */

/**
 * @defgroup HwConfig_Joystick Joystick pins (2-axis analog + button, not a rotary encoder)
 * @ingroup CoreLR_HwConfig
 * @{
 */
#define LCDJOYSTICKUI_X_PIN  A1   /**< analog pin of the stick's X axis */
#define LCDJOYSTICKUI_Y_PIN  A2   /**< analog pin of the stick's Y axis */
#define LCDJOYSTICKUI_SW_PIN A3   /**< pin of the stick's push button (active low) */
/**
 * Polarity of the joystick, per axis (@req{REQ-CoreLR-D-LcdJoystickUI-0020}). The firmware's convention is that LcdJoystickUI_ReadAxis() is POSITIVE when the stick
 * is pushed to the RIGHT (X) or DOWN (Y): the Control jog moves the pen right / down, the menu goes to the next item, the Adjustment jog lengthens the cable.
 * What the analog value does when the stick moves depends on how the module is turned and wired, so it is a hardware fact stated here: 1 = this unit reads
 * the other way round and the value is negated. Set from the maintainer's bench observation of 2026-10-10: with 0 and 0 the jog was inverted in X and in Y.
 */
/**
 * The stick's travel as the ADC reads it: the deflection (after the polarity above, from the centre) at the mechanical stop, or a little before it.
 * The 10-bit ADC gives at most 511 either side of 512 and the extremes are rarely reached, so the full speed is given from here, not from 511.
 * Both jogs use it (the Control jog's curve and the Adjustment jog's), minus the dead zone (SwConfig.h). Start 500; lower it if the stick never gets there.
 */
#ifndef LCDJOYSTICKUI_STICK_FULL_SCALE
#define LCDJOYSTICKUI_STICK_FULL_SCALE 500
#endif
#ifndef LCDJOYSTICKUI_X_INVERT        /* guarded only so that the host test can run every combination */
#define LCDJOYSTICKUI_X_INVERT 1   /**< 1: this unit's X reads the other way round, the value is negated (see the polarity note above) */
#endif
#ifndef LCDJOYSTICKUI_Y_INVERT
#define LCDJOYSTICKUI_Y_INVERT 1   /**< 1: same for Y */
#endif
/** @} */

/**
 * @defgroup HwConfig_Lcd LCD (I2C on fixed silicon pins A4=SDA/A5=SCL)
 * @ingroup CoreLR_HwConfig
 * @{
 */
#define LCDJOYSTICKUI_I2C_ADDRESS 0x3F   /**< 7-bit I2C address of the PCF8574 backpack: 0x27 or 0x3F, by the jumpers of the module */
#define LCDJOYSTICKUI_I2C_HZ      100000UL  /**< the PCF8574's own limit; Wire's default too */
#define LCDJOYSTICKUI_COLS        16   /**< characters per row */
#define LCDJOYSTICKUI_ROWS        2    /**< rows (LcdI2c.c supports two) */
/** The character code of the arrow in the menu's "Back" entry (@req{REQ-CoreLR-D-LcdJoystickUI-0026}). It depends on the character ROM of the
 * HD44780 on the backpack: 0x7F is a left arrow in the common A00 (Japanese) ROM, something else in others -- if the entry shows an odd
 * symbol, use '<' (0x3C), which every ROM has. */
#define LCDJOYSTICKUI_BACK_ARROW  0x7F
/** @} */

/**
 * @defgroup HwConfig_Sd SD card
 * @ingroup CoreLR_HwConfig
 *
 * Driven over the ATmega328P's **hardware** SPI unit (sdfatlib with
 * USE_SPI_LIB), whose pins are fixed by the silicon and cannot be
 * reassigned: **D11 = MOSI, D12 = MISO, D13 = SCK** (PIN_SPI_* in the
 * Uno variant's pins_arduino.h). Only the chip select is ours to
 * choose, and it must be an ordinary GPIO -- once SPI.begin() sets SPE
 * with MSTR, the SPI unit takes over the direction of its three pins
 * whatever DDRB says, so a CS placed on one of them could never be
 * driven. Nor may a motor wire land on them.
 *
 * D10 is both the Arduino-conventional SD chip-select and the SPI SS
 * pin. Using it as CS is the intended arrangement, and it also settles
 * the well-known AVR requirement that SS be an output in master mode.
 * @{
 */
#define SDREADER_CS_PIN 10 /**< PORTB bit 2; also SPI SS -- see above */
/** @} */

/**
 * @defgroup HwConfig_Mechanical Fixed mechanical parameters
 * @ingroup CoreLR_HwConfig
 * Realizes @req{REQ-CoreLR-F-Calibration-0001}.
 * MOTION_SPOOL_DIAMETER_UM confirmed against the real hardware by the
 * maintainer, 2026-09-10: the 35mm carried over from the Walldraw
 * reference is correct. It is the scale factor between motor steps and
 * millimetres, so it bounds the machine's accuracy -- 35mm gives a
 * circumference of 35 * 355/113 = 109.96mm and therefore
 * 109955/2037.886 = 53.96um per step (MOTION_WAYPOINT_UM in Motion.c; it was 53.69 with the 2048 steps of before 2026-10-26).
 * That in turn fixes the velocity budget: the ~1kHz per-motor ceiling
 * is 54.0mm/s. (MOTION_VMAX_UM_PER_TICK = 10, 40mm/s, was accepted on that argument on 2026-09-10;
 * the motors only hold 400 steps/s under load, so MOTION_VMAX_V16 is 86 = 21.5mm/s since 2026-09-11, SwConfig.h.)
 * @{
 */
/**
 * Steps per turn of the 28BYJ-48's OUTPUT shaft, times 1000. It is NOT 2048 (= 32 x 64): the gear train is 32/9 x 22/11 x 26/9 x 31/10 =
 * 567424/8910 = 63.68395 : 1, so with the motor's 32 full steps a turn the shaft needs 32 x 63.68395 = 9078784/4455 = 2037.8866 steps for a turn.
 * 2048 (the "64:1" of the data sheets and of the Walldraw reference) over-estimated the steps per turn by 0.5 %, so every step wound 0.5 % more
 * cable than the firmware thought: 5 mm a metre, and at the top limit, where the cables are nearly horizontal, a margin of 50 mm shrank to 33 mm
 * with the tension x1.5 (docs/hardware/CableScaleMeasurement.md). Corrected 2026-10-26; the 4-phase full-step table is unchanged.
 * Thousandths of a step, so that the figure stays an integer: 2037.886 differs from 2037.8866 by 3e-7.
 */
#define MOTION_STEPS_PER_REV_X1000 2037886L
#define MOTION_SPOOL_DIAMETER_UM 35000L  /**< 35 mm, in micrometres */
/**
 * Direction of rotation, per motor (@req{REQ-CoreLR-F-Calibration-0001}): 1 inverts it in software, 0 leaves it. The intent is that **more steps
 * lengthens the cable** on both motors (docs/hardware/images/Geometry.svg). It depends on how each motor's IN1..IN4 are wired and on which way its
 * spool turns, which no reading of the code can settle -- only the bench can. Changed 2026-10-10 from L = 0, R = 1 to L = 1, R = 0: the bench
 * showed both motors turning the wrong way (maintainer). If one motor ever has to be turned round, change only its flag.
 */
#define STEPPER_MOTOR_L_REVERSED   1
#define STEPPER_MOTOR_R_REVERSED   0   /**< motor R: 1 inverts its direction in software, 0 leaves it */
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CORELR_HWCONFIG_H */
