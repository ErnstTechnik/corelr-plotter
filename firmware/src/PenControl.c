/*
 * PenControl.cpp
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
 * @file PenControl.c
 * @brief See PenControl.h.
 *
 * The pen servo, driven in software from Timer2 (@req{REQ-CoreLR-D-PenControl-0003}).
 * It replaced the Arduino Servo library, which owned Timer1 -- the only 16-bit
 * timer, which Stepper needs -- and was C++. The pen takes two positions and
 * needs no precision, so a minimal driver is enough:
 *
 *   frame = 20 ticks of 1 ms (IsrDispatch runs Timer2 at 1 kHz)
 *
 *   tick:        0            n  (n = pulse / 1000)                 19
 *   pin:   ______/````````````\_________________________________________
 *                ^ raised at   ^ lowered by compare-B at
 *                  tick 0        (pulse mod 1000) / 4 counts into tick n
 *
 * The pulse is 544..2400 us for an angle of 0..180 degrees -- the same mapping
 * as the Servo library, so the angles stored in EEPROM keep their meaning.
 * One Timer2 count is 4 us, i.e. about 0.4 degree. Nothing here does arithmetic
 * in an interrupt: the pulse widths are worked out in the main loop whenever the
 * calibration angles change, and the ISRs only index a three-entry table.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "PenControl.h"
#include "HwConfig.h"
#include "SwConfig.h"
#include "Calibration.h"
#include "Stepper.h"

#if ISRDISPATCH_FREQ_HZ != 1000u
#error "PenControl.c builds the servo frame from a 1 ms tick: ISRDISPATCH_FREQ_HZ must be 1000"
#endif
#if !(PENCONTROL_SERVO_PIN >= 14 && PENCONTROL_SERVO_PIN <= 19)
#error "PENCONTROL_SERVO_PIN must be an analog pin A0..A5 (digital 14..19, PORTC)"
#endif
#define PEN_PORT PORTC   /**< PORT register of the servo pin (A0..A5 are PORTC) */
#define PEN_DDR  DDRC    /**< its direction register */
#define PEN_BIT  (PENCONTROL_SERVO_PIN - 14)   /**< the pin's bit in both */
#define PEN_SET() (PEN_PORT |= (uint8_t)(1u << PEN_BIT))   /**< raise the servo line (start of the pulse) */
#define PEN_CLR() (PEN_PORT &= (uint8_t)~(1u << PEN_BIT))  /**< lower it (end of the pulse) */

#define PENCONTROL_FRAME_TICKS  20u    /**< the servo frame, in 1 ms ticks (20 ms) */
#define PENCONTROL_PULSE_MIN_US 544u   /**< pulse width for 0 degrees, us (the Servo library's mapping) */
#define PENCONTROL_PULSE_MAX_US 2400u  /**< pulse width for 180 degrees, us */
/** Compare-B is armed from inside the tick ISR, a few counts after the
 * counter restarted; a compare value the counter has already passed would
 * not match until the next millisecond, stretching the pulse by 1 ms. This
 * floor (12 counts = 48 us) clears the worst interrupt latency; it costs up
 * to 48 us (under 5 degrees) for the few angles whose pulse falls within
 * 48 us of a whole millisecond. */
#define PENCONTROL_MIN_FRAC 12u

/** The settling time, in Stepper's unit (@req{REQ-CoreLR-F-PenControl-0003}). */
#define PENCONTROL_SETTLE_COUNTS ((uint32_t)PENCONTROL_SETTLE_TIME_MS * (STEPPER_COUNTS_PER_S / 1000UL))

/** Table slots: the two real setpoints, and the wizard's live preview. */
#define SLOT_UP      0u   /**< the pen-up setpoint */
#define SLOT_DOWN    1u   /**< the pen-down setpoint */
#define SLOT_PREVIEW 2u   /**< the calibration wizard's live angle (PenControl_PreviewAngle()) */

static uint8_t s_pulseTick[3]; /**< whole milliseconds of the pulse, per slot */
static uint8_t s_pulseFrac[3]; /**< remainder, in 4 us Timer2 counts, per slot */
static uint8_t s_angleUp;      /**< the angles the table was last computed from */
static uint8_t s_angleDown;     /**< same, for the pen-down angle */
static volatile uint8_t s_slot; /**< which slot the next frame uses; written by the step ISR, one byte */

/** Servo frame state; touched only by the Timer2 ISRs. */
static uint8_t s_frame;
static uint8_t s_endTick;      /**< whole milliseconds of the current frame's pulse, copied from the table at the frame's start */
static uint8_t s_endFrac;      /**< its remainder, in 4 us Timer2 counts: the compare-B value that ends the pulse */

/**
 * Desired vs. issued pen state (split 2026-09-10, fixing a real defect).
 *
 * s_requestedPenDown is what the caller wants; s_issuedPenDown is what
 * has actually been handed to Stepper as a queue entry. PenControl_Task()
 * re-issues until they agree.
 *
 * Before this split, Request*() called Stepper_RequestPenOnly() once and
 * **discarded its return value**, updating the tracked state regardless.
 * A full queue therefore lost the request silently and left this module
 * believing the pen was up while it was physically down -- breaking the
 * synchronisation @req{REQ-CoreLR-F-PenControl-0004} exists to guarantee. It
 * matters most at exactly the wrong moment: lifting the pen on a pause,
 * a stop or an error is requested when the queue is at its fullest.
 *
 * Ordering note, load-bearing: loop() calls PenControl_Task() *before*
 * Motion_Task(). With a single free slot the pen entry therefore takes
 * it, never a waypoint, so a pen change can never be overtaken by the
 * motion that should follow it.
 */
static uint8_t s_requestedPenDown;   /**< what the caller wants: 1 = down, 0 = up */
static uint8_t s_issuedPenDown;      /**< what has been handed to Stepper as a queue entry (PenControl_Task() makes it follow the request) */

/** Angle in degrees -> servo pulse, split into whole ms and 4 us counts. */
static void PulseFor(uint8_t angle, uint8_t slot)
{
   uint16_t us;
   uint8_t frac;

   if (angle > 180u)
   {
      angle = 180u;
   }
   us = (uint16_t)(PENCONTROL_PULSE_MIN_US
                   + ((uint32_t)angle * (PENCONTROL_PULSE_MAX_US - PENCONTROL_PULSE_MIN_US)) / 180u);
   frac = (uint8_t)((us % 1000u) / 4u);
   if (frac < PENCONTROL_MIN_FRAC)
   {
      frac = PENCONTROL_MIN_FRAC;
   }
   s_pulseTick[slot] = (uint8_t)(us / 1000u);
   s_pulseFrac[slot] = frac;
}

/** Recompute the two setpoint pulses. Interrupts off while the table
 *  changes, so a frame can never start on half of a new pulse. */
static void RefreshPulses(uint8_t upAngle, uint8_t downAngle)
{
   uint8_t previousSreg = SREG;

   cli();
   PulseFor(upAngle, SLOT_UP);
   PulseFor(downAngle, SLOT_DOWN);
   s_angleUp = upAngle;
   s_angleDown = downAngle;
   SREG = previousSreg;
}

void PenControl_Init(void)
{
   uint8_t upAngle;
   uint8_t downAngle;

   Calibration_GetPenAngles(&upAngle, &downAngle); /* narrow getter: avoids copying the full 7-field struct for 1 byte */
   RefreshPulses(upAngle, downAngle);
   s_slot = SLOT_UP;
   s_requestedPenDown = 0;
   s_issuedPenDown = 0;
   s_frame = 0;
   PEN_DDR |= (uint8_t)(1u << PEN_BIT);
   PEN_CLR();
   /* No motion is happening yet at boot, so no synchronisation is needed:
    * the pen is simply up. The pulses start when IsrDispatch_Init() arms
    * Timer2. */
}

void PenControl_RequestUp(void)
{
   s_requestedPenDown = 0; /* PenControl_Task() issues the queue entry, and retries until accepted */
}

void PenControl_RequestDown(void)
{
   s_requestedPenDown = 1; /* PenControl_Task() issues the queue entry, and retries until accepted */
}

uint8_t PenControl_IsRequestedDown(void)
{
   return s_requestedPenDown;
}

/* CORELR_NOINLINE: keeps this out of main()'s permanent frame -- see SwConfig.h */
CORELR_NOINLINE
void PenControl_Task(void)
{
   uint8_t upAngle;
   uint8_t downAngle;

   /* Issue, or re-issue, the queue entry for a state change the caller
    * asked for but Stepper has not accepted yet (a full queue). Cheap
    * no-op -- one comparison -- whenever desired and issued agree,
    * which is every iteration except just after a Request*(). */
   if (s_issuedPenDown != s_requestedPenDown)
   {
      if (Stepper_RequestPenOnly(s_requestedPenDown ? STEPPER_PEN_DOWN : STEPPER_PEN_UP, PENCONTROL_SETTLE_COUNTS))
      {
         s_issuedPenDown = s_requestedPenDown;
      }
   }

   /* The ISR cannot compute a pulse, so the table is kept current here:
    * if the calibration angles changed (the wizard), refresh it. */
   Calibration_GetPenAngles(&upAngle, &downAngle);
   if ((upAngle != s_angleUp) || (downAngle != s_angleDown))
   {
      RefreshPulses(upAngle, downAngle);
   }
}

uint8_t PenControl_IsDown(void)
{
   return (uint8_t)(s_slot == SLOT_DOWN);
}

void PenControl_ApplyFromIsr(uint8_t action)
{
   /* One byte: atomic, no flag, no critical section, no polling latency --
    * the pen follows the motion at the event itself
    * (REQ-CoreLR-D-PenControl-0003). */
   s_slot = (action == (uint8_t)STEPPER_PEN_DOWN) ? SLOT_DOWN : SLOT_UP;
}

void PenControl_PreviewAngle(uint8_t angle)
{
   /**
    * Deliberately bypasses the request/queue mechanism entirely --
    * used only by the LcdJoystickUI pen-calibration wizard
    * (@req{REQ-CoreLR-D-LcdJoystickUI-0007}) to move the servo live as the
    * user adjusts an angle that isn't a real setpoint yet. Does not
    * update s_requestedPenDown: the next real PenControl_RequestUp()/
    * Down() re-asserts the tracked state. Never call this from job
    * execution -- it is exactly the direct servo write the rest of this
    * module exists to avoid.
    */
   uint8_t previousSreg = SREG;

   cli();
   PulseFor(angle, SLOT_PREVIEW);
   SREG = previousSreg;
   s_slot = SLOT_PREVIEW;
}

void PenControl_Tick(void)
{
   uint8_t frame = s_frame;

   if (frame == 0u)
   {
      uint8_t slot = s_slot;

      s_endTick = s_pulseTick[slot];
      s_endFrac = s_pulseFrac[slot];
      PEN_SET();
   }
   if (frame == s_endTick)
   {
      OCR2B = s_endFrac;
      TIFR2 = (uint8_t)(1u << OCF2B);                    /* a stale match flag must not fire at once */
      TIMSK2 = (uint8_t)(TIMSK2 | (1u << OCIE2B));
   }
   frame++;
   if (frame >= (uint8_t)PENCONTROL_FRAME_TICKS)
   {
      frame = 0u;
   }
   s_frame = frame;
}

void PenControl_PulseEnd(void)
{
   PEN_CLR();
   TIMSK2 = (uint8_t)(TIMSK2 & (uint8_t)~(1u << OCIE2B));
}
