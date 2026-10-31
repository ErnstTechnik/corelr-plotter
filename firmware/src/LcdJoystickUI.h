/*
 * LcdJoystickUI.h
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
 * @file LcdJoystickUI.h
 * @brief LCD display driver, joystick reading, and the menu state
 * machine (status screen, main menu, jog modes, calibration wizards,
 * SD browser).
 *
 * Realizes: @req{REQ-CoreLR-F-UserInterface-0001}..@req{REQ-CoreLR-F-UserInterface-0008}
 * Design:   @req{REQ-CoreLR-D-LcdJoystickUI-0001}..@req{REQ-CoreLR-D-LcdJoystickUI-0009}
 * See:      requirements/design/REQ-CoreLR-D-LcdJoystickUI.md
 *
 * Still a .cpp file, from when it drove the C++-only LiquidCrystal_PCF8574
 * library; the display is now LcdI2c (plain C, our own driver), so nothing here
 * needs C++ any more and a rename to .c is open (@req{REQ-CoreLR-D-LcdI2c-0001}). The
 * public API below is a plain C surface (extern "C") either way.
 */

#ifndef CORELR_LCDJOYSTICKUI_H
#define CORELR_LCDJOYSTICKUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Overall machine/UI state, drives the status-tag character. */
typedef enum
{
   LCDJOYSTICKUI_STATE_IDLE = 0,        /**< tag 'I' */
   LCDJOYSTICKUI_STATE_RUNNING,         /**< tag 'R' */
   LCDJOYSTICKUI_STATE_PAUSED,          /**< tag 'P' */
   LCDJOYSTICKUI_STATE_ERROR,           /**< tag 'E', @req{REQ-CoreLR-D-GCodeParser-0005}/@req{REQ-CoreLR-D-GCodeParser-0006} */
   LCDJOYSTICKUI_STATE_UNCAL     /**< tag 'U' */
} LcdJoystickUI_MachineState_t;

/*
 * The UI-mode enumeration is NOT declared here: no other module names
 * it and no public signature mentions it, so it lives in
 * LcdJoystickUI.cpp (docs/standards/STD-FileOrganization.md,
 * "Expose only what callers need"). LcdJoystickUI_MachineState_t below
 * does stay public -- LcdJoystickUI_SetMachineState() takes it.
 */


/** @brief Initialize the LCD, joystick pins, and UI state (status screen, idle). */
void LcdJoystickUI_Init(void);

/**
 * @brief Service the UI: read the joystick, advance the menu state
 * machine (status screen / main menu / Control jog / Adjustment jog /
 * pen calibration wizard / SD browser, @req{REQ-CoreLR-D-LcdJoystickUI-0003}..@req{REQ-CoreLR-D-LcdJoystickUI-0008}),
 * redraw the display. Call every loop() iteration.
 */
void LcdJoystickUI_Task(void);

/** @brief Set the overall machine state (updates the status-tag character). */
void LcdJoystickUI_SetMachineState(LcdJoystickUI_MachineState_t state);

/**
 * One-shot request raised by the UI for the top-level job state machine
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0012}).
 *
 * The UI does not decide job state -- it is a display and an input
 * device. It reports what the user asked for and grbl_corelr.ino
 * decides. Polled rather than delivered by callback: it mirrors
 * Stepper_TakePendingPenAction(), an already-reviewed pattern in this
 * codebase, costs one byte, and keeps function pointers (which cost
 * RAM on AVR) out of the module.
 */
typedef enum
{
   LCDJOYSTICKUI_EVENT_NONE = 0,
   LCDJOYSTICKUI_EVENT_JOB_START,  /**< a file was selected and opened successfully */
   LCDJOYSTICKUI_EVENT_JOB_HALT,   /**< pause: ramp down, lift the pen, stay resumable */
   LCDJOYSTICKUI_EVENT_JOB_RESUME, /**< put the pen back if it was down, ramp back up */
   LCDJOYSTICKUI_EVENT_JOB_CANCEL, /**< like halt, but the job is abandoned */
   LCDJOYSTICKUI_EVENT_ERROR_ACK   /**< short press on the status screen while in error */
} LcdJoystickUI_Event_t;

/**
 * @brief Read and clear the pending UI request.
 *
 * No critical section: the byte is written and read from main-loop
 * context only -- no interrupt handler touches it.
 */
LcdJoystickUI_Event_t LcdJoystickUI_TakeEvent(void);

/**
 * @return 1 while a mode that can move the machine -- a jog, a calibration wizard -- is on the screen. The serial link
 * refuses G-code then (error:9) and reports `Jog` (docs/protocol/SerialProtocol.md @protosec{4-7,4.7}, @proto{FW-08}).
 */
uint8_t LcdJoystickUI_IsBusy(void);

/**
 * @brief Read one joystick axis with the configured dead zone applied
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0009}).
 * @return signed deflection, 0 within the dead zone, else the raw
 * ADC reading minus LCDJOYSTICKUI_ADC_CENTER, negated for an axis whose
 * unit reads the other way round (LCDJOYSTICKUI_X_INVERT / _Y_INVERT in
 * HwConfig.h): so the value is positive when the stick is pushed to the
 * right (X) or down (Y), whatever the hardware.
 */
int16_t LcdJoystickUI_ReadAxis(uint8_t analogPin);

/**
 * @brief The stick as a choice: -1 (left / up... see below), 0 or +1, for the discrete uses -- selecting a motor, cycling a step size, moving in a menu
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0022}). Same sign convention as LcdJoystickUI_ReadAxis(): +1 right (X) / down (Y). The stick must reach
 * LCDJOYSTICKUI_TERNARY_ON to count and stays counted until below LCDJOYSTICKUI_TERNARY_OFF (hysteresis); it keeps one state per axis, so call it freely.
 */
int8_t LcdJoystickUI_ReadAxisTernary(uint8_t analogPin);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_LCDJOYSTICKUI_H */
