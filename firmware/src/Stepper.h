/*
 * Stepper.h
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
 * @file Stepper.h
 * @brief A single, unified queue of combined motor-output-plus-pen-action
 * entries, drained by a compare-match ISR on Timer1 (one interrupt per
 * entry, not a periodic tick -- @req{REQ-CoreLR-D-Stepper-0011}).
 *
 * Realizes: @req{REQ-CoreLR-F-Actuation-0001}, @req{REQ-CoreLR-F-Actuation-0002},
 *           @req{REQ-CoreLR-F-PenControl-0004}
 * Design:   @req{REQ-CoreLR-D-Stepper-0001}..@req{REQ-CoreLR-D-Stepper-0003}, 0005, 0007..0008, 0010..0014
 * See:      requirements/design/REQ-CoreLR-D-Stepper.md,
 *           docs/hardware/PinMapping.md
 *
 * Settled design (2026-09-06), after three rounds of code review:
 *  - A single combined queue (not one per motor) makes the two motors
 *    desynchronizing structurally impossible: one write/read index pair.
 *  - Each 3-byte entry packs both motors' coil bits (1 byte), a 14-bit
 *    hold in Timer1 counts (0.5 us), and a 2-bit pen action stored as the actual
 *    target state (not a toggle -- see the entry layout in Stepper.c).
 *  - The ISR applies coil bits via compile-time-constant per-wire
 *    set/clear macros (HwConfig.h), so avr-gcc emits atomic
 *    sbi/cbi instructions -- no runtime pin/port resolution needed.
 *
 * Single-producer/single-consumer: the control/planning code
 * (Motion, PenControl) enqueues; only the ISR dequeues. No
 * locking is required for the queue itself -- see the header of
 * Stepper.c for the state that does need a critical section.
 *
 * A hold request longer than the queue can currently take is no longer
 * an all-or-nothing failure (revised 2026-09-08, @req{REQ-CoreLR-D-Stepper-0003}):
 * it is *accepted* once its first chunk lands, and the remainder
 * becomes a one-request-deep pending backlog that Stepper_Task()
 * keeps draining in the background -- see EnqueueMotion()'s and
 * EnqueueSplit()'s (Stepper.c) doc comments for the full
 * picture, including why a new request is never interleaved with an
 * older, still-draining backlog.
 */

#ifndef CORELR_STEPPER_H
#define CORELR_STEPPER_H

#include <stdint.h>
#include "SwConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Identifies which physical motor an operation targets (used by
 * callers outside this module, e.g. the Adjustment jog mode's L/R
 * selection -- this module's own queue is unified, not per-motor). */
typedef enum
{
   STEPPER_MOTOR_L = 0,
   STEPPER_MOTOR_R = 1,
   STEPPER_MOTOR_COUNT
} Stepper_Motor_t;

/** A pen action, as seen by callers of this module's public API. */
typedef enum
{
   STEPPER_PEN_NONE = 0,
   STEPPER_PEN_UP,
   STEPPER_PEN_DOWN
} Stepper_PenAction_t;

/*
 * The queue's own types (entry layout, indices, the ring buffer itself)
 * are NOT declared here: nothing outside this module names them and no
 * public signature mentions them, so they live in Stepper.c
 * (docs/standards/STD-FileOrganization.md, "Expose only what
 * callers need"). Leaving a full queue structure in the header reads as
 * permission to touch it -- and the single-producer/single-consumer
 * reasoning in this module quietly depends on nobody doing that.
 * The entry layout is documented at its definition in the .c.
 */


/**
 * @brief Initialize the queue and set every motor pin as an output
 * (@req{REQ-CoreLR-D-Stepper-0005} uses compile-time set/clear macros
 * for the per-event writes; only the initial pin-mode setup happens
 * here) and configure Timer1 -- free-running, prescaler 8, its compare
 * interrupt left disarmed until the first entry is queued
 * (@req{REQ-CoreLR-D-Stepper-0013}). Must be called once from setup().
 */
void Stepper_Init(void);

/**
 * @brief Enqueue a combined motion entry for both motors.
 * @param lCoilBits Motor L's coil pattern (low nibble used).
 * @param rCoilBits Motor R's coil pattern (low nibble used).
 * @param holdCounts how long to hold this state, in Timer1 counts
 *        (STEPPER_COUNTS_PER_S per second, 0.5 us each). Clamped up to
 *        STEPPER_DELAY_MIN_COUNTS; above STEPPER_DELAY_MAX_COUNTS this
 *        call splits it into several entries internally, never leaving a
 *        remainder below the minimum (@req{REQ-CoreLR-D-Stepper-0012}).
 * @param penAction fires once, on the first chunk, when it becomes
 *        current -- encoded internally as the actual target state, see
 *        @req{REQ-CoreLR-D-Stepper-0001}.
 * @return 1 if the request was *accepted*: its first chunk landed in
 *         the queue, and the rest either landed too or is now pending
 *         backlog that Stepper_Task() (or a later Enqueue*()
 *         call) will keep draining in the background -- this does
 *         *not* mean physical execution is complete yet, only that the
 *         request has been durably registered. Returns 0 if not even
 *         the first chunk fit (ordinary queue-full, or an older
 *         request's backlog is still draining) -- nothing was enqueued
 *         at all in that case, and the caller should retry the whole
 *         request later, same as before this backlog mechanism existed.
 */
uint8_t Stepper_RequestMotion(uint8_t lCoilBits, uint8_t rCoilBits,
                                       uint32_t holdCounts, Stepper_PenAction_t penAction);

/**
 * @brief Enqueue a pen-only entry: repeats the last successfully
 * enqueued coil bits (no motion) for holdCounts, carrying penAction.
 * Used for the pen settling delay (@req{REQ-CoreLR-F-PenControl-0003}) and
 * to keep pen actions synchronized with motion
 * (@req{REQ-CoreLR-F-PenControl-0004}). Same accept/backlog semantics as
 * Stepper_RequestMotion() -- see its @return doc.
 */
uint8_t Stepper_RequestPenOnly(Stepper_PenAction_t penAction, uint32_t holdCounts);

/**
 * @brief Background task (@req{REQ-CoreLR-D-Stepper-0003}): drains as
 * much of any pending backlog as currently fits, so a request accepted
 * by Enqueue*() but not fully enqueued yet keeps making progress even
 * if nothing calls Enqueue*() again for a while. Call every loop()
 * iteration, like every other module's *_Task(); a cheap no-op (one
 * comparison) whenever nothing is pending.
 */
void Stepper_Task(void);

/** @return 1 if the queue is empty *and* no backlog is pending -- i.e.
 * nothing left to do at all, not just "the ring buffer has no entries
 * right now" (@req{REQ-CoreLR-D-Stepper-0003}). */
uint8_t Stepper_IsEmpty(void);

/**
 * @return 1 if nothing is queued, no backlog is pending **and the step
 * timer is disarmed** -- which is true exactly when the last entry's hold
 * has expired, i.e. the motors have reached their final state. Stricter
 * than Stepper_IsEmpty(), which is true while the last entry is still being
 * held. This is the "machine is stationary" test the position store needs
 * (@req{REQ-CoreLR-D-Calibration-0008}).
 */
uint8_t Stepper_IsIdle(void);

/**
 * Number of entries currently waiting in the ring buffer.
 *
 * Does **not** include the entry the ISR is executing right now, so a
 * return of 0 still leaves up to one hold period of motion in flight.
 * Callers topping the queue up want exactly this: how much more can be
 * committed, not how much remains overall.
 *
 * The ISR may advance the read index between the two byte reads below,
 * which can only make the answer one too large -- i.e. err towards
 * under-filling on the next pass, never towards overrunning the ring.
 * Each index is a byte, so neither read can tear.
 */
uint8_t Stepper_QueuedCount(void);

#if CORELR_DEBUG_JOBSTATE
/** Bench diagnostics (CORELR_DEBUG_JOBSTATE): flag bits of the step queue, see SerialLink.c. */
uint8_t Stepper_DebugFlags(void);
#endif

#if CORELR_DEBUG_MISS_COUNT
/**
 * Saturating count of *step starvations*: the producer was still feeding a
 * move and found the step timer idle -- the queue had run dry mid-move
 * (@req{REQ-CoreLR-D-Stepper-0013}). In v2 that is a pause, not a repeat of the
 * same coil bits, but it shifts the timeline, so it must be rare.
 *
 * Counted by the PRODUCER (Motion, via Stepper_CountStarvation()), because
 * the ISR cannot tell a starvation from the normal end of a move: both look
 * like an empty queue at a compare.
 */
uint8_t Stepper_StarvedCount(void);

/**
 * Clear the starvation counter.
 *
 * Callers reset it at the start of a movement so the figure reads as
 * "starvations during *this* move". Without that it accumulates the
 * one-per-motion-end count across every attempt and stops being
 * legible, which is what the first bench run showed.
 */
void Stepper_ResetStarvedCount(void);

/** @brief Count one starvation. Called by the producer, once per episode,
 *  when it finds the timer idle while a move it has been feeding is still
 *  in progress. */
void Stepper_CountStarvation(void);
#endif /* CORELR_DEBUG_MISS_COUNT */

/**
 * @brief Stop mechanism (V1): halt further enqueuing system-wide.
 * The last enqueued entry/entries still run to completion; the motors
 * then hold their last commanded position. Realizes
 * @req{REQ-CoreLR-F-Actuation-0002} / @req{REQ-CoreLR-D-Stepper-0007}. Also
 * reused by @req{REQ-CoreLR-D-GCodeParser-0005} to halt a job on an invalid
 * line.
 */
void Stepper_HaltRequests(void);

/** @brief Resume enqueuing after Stepper_HaltRequests(). */
void Stepper_ResumeRequests(void);

/** @return 1 if enqueuing is currently halted. */
uint8_t Stepper_IsHalted(void);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_STEPPER_H */
