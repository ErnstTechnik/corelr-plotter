/*
 * Stepper.c
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
 * @file Stepper.c
 * @brief See Stepper.h.
 *
 * Event-driven stepping (v2, @req{REQ-CoreLR-D-Stepper-0010}..@req{REQ-CoreLR-D-Stepper-0014}). This module
 * owns **Timer1**, free-running at prescaler 8 (0.5 us per count), and its
 * compare-A vector. There is no periodic tick: the ISR runs once per queue
 * entry, and does only this:
 *
 *   1. Pop the next entry. None queued: disarm the interrupt and stop --
 *      the last hold has expired.
 *   2. Apply its coil bits, via the 8 compile-time set/clear macros defined
 *      below -- each compiles to a single atomic sbi/cbi instruction.
 *   3. Hand its pen action, if any, to PenControl (a one-byte write).
 *   4. OCR1A += hold: the next compare is relative to the PREVIOUS compare,
 *      never to the counter, so interrupt latency does not accumulate.
 *   5. If that point is already (almost) behind the counter, fire at once
 *      rather than after a whole 32.8 ms counter wrap.
 *
 * The ISR keeps no position or phase state: the coil bits arrive ready-made
 * from Motion, which derives them from its absolute step count
 * (@req{REQ-CoreLR-D-Motion-0007}). Nothing in here can desynchronise from it.
 *
 * Concurrency, deliberately case-by-case (not blanket critical sections):
 *   - Per-wire sbi/cbi writes are genuinely atomic (single instruction,
 *     compile-time-constant bit) -- no section needed at all.
 *   - The one pin sharing a port with motor wires that has no hardware
 *     override (SD CS, D10) must only ever be touched via
 *     digitalWrite()/pinMode() elsewhere, never a raw register write --
 *     see @req{REQ-CoreLR-D-Stepper-0008}.
 *   - Indices.ReadIndex/WriteIndex: single-byte reads/writes are
 *     inherently atomic on AVR; each index is only ever written from
 *     one context -- no section needed.
 *   - Arming (@req{REQ-CoreLR-D-Stepper-0013}) is the one place the two contexts
 *     meet: the producer publishes an entry, then tests-and-arms inside a
 *     critical section; the ISR disarms from inside itself. 16-bit timer
 *     registers are touched only in the ISR or with interrupts off.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "Stepper.h"
#include "Calibration.h"
#include "PenControl.h"
#include "HwConfig.h"

/**
 * This file assumes the ATmega328(P) pin layout (Arduino Uno/Nano):
 * digital pins 0-7 map directly to PORTD bits 0-7, and 8-13 to PORTB
 * bits 0-5 -- the only PORTB bits exposed as Arduino pins on this chip
 * (PB6/PB7 are the crystal). This mapping is chip/variant-specific, not
 * a general Arduino guarantee -- e.g. Mega, Leonardo, Due and ESP32
 * boards map digital pins to ports completely differently, or don't
 * have AVR-style PORTx/DDRx registers at all. Confirmed 2026-09-06:
 * fail the build outright on anything else, rather than silently
 * deriving a wrong port/bit for a pin number that happens to fall in
 * range by coincidence.
 */
#if !(defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__))
#error "Stepper.c assumes the ATmega328(P) PORTD(0-7)/PORTB(8-13) pin layout (Arduino Uno/Nano) -- port/bit derivation below is invalid on any other target"
#endif

/**
 * Per-wire port/DDR-register and bit-index derivation, and the generic
 * SET/CLR/DDR_SET macros built from them (@req{REQ-CoreLR-D-Stepper-0005}).
 * Private to this file (minimum-scope principle, confirmed 2026-09-06):
 * nothing else in the project needs register-level pin access, so this
 * stays out of the shared HwConfig.h.
 *
 * The port/DDR/bit are derived automatically from each wire's raw pin
 * number (HwConfig.h) via \#if, not written out by hand -- so
 * there is no possibility of a human pairing the wrong port with a pin
 * (the earlier revision's \#error checks could only catch that mistake
 * after the fact; this design makes it structurally impossible instead).
 * Valid range is 0-13: pins 0-7 live on PORTD, pins 8-13 on PORTB (the
 * only PORTB bits exposed as Arduino pins on the Uno/Nano -- PB6/PB7 are
 * the crystal, not GPIO, so this must stop at 13, not extend to 15).
 */
#if !(STEPPER_MOTOR_L_PIN0 >= 0 && STEPPER_MOTOR_L_PIN0 <= 13)
#error "STEPPER_MOTOR_L_PIN0 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_L_PIN0 < 8
#define MOTOR_L_WIRE0_PORT PORTD
#define MOTOR_L_WIRE0_DDR  DDRD  /**< DDR register of motor L's wire 0 (direction) */
#define MOTOR_L_WIRE0_BIT  STEPPER_MOTOR_L_PIN0  /**< bit of motor L's wire 0 in its PORT and DDR registers */
#else
#define MOTOR_L_WIRE0_PORT PORTB  /**< PORT register of motor L's wire 0 */
#define MOTOR_L_WIRE0_DDR  DDRB  /**< DDR register of motor L's wire 0 (direction) */
#define MOTOR_L_WIRE0_BIT  (STEPPER_MOTOR_L_PIN0 - 8)  /**< bit of motor L's wire 0 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_L_PIN1 >= 0 && STEPPER_MOTOR_L_PIN1 <= 13)
#error "STEPPER_MOTOR_L_PIN1 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_L_PIN1 < 8
#define MOTOR_L_WIRE1_PORT PORTD  /**< PORT register of motor L's wire 1 */
#define MOTOR_L_WIRE1_DDR  DDRD  /**< DDR register of motor L's wire 1 (direction) */
#define MOTOR_L_WIRE1_BIT  STEPPER_MOTOR_L_PIN1  /**< bit of motor L's wire 1 in its PORT and DDR registers */
#else
#define MOTOR_L_WIRE1_PORT PORTB  /**< PORT register of motor L's wire 1 */
#define MOTOR_L_WIRE1_DDR  DDRB  /**< DDR register of motor L's wire 1 (direction) */
#define MOTOR_L_WIRE1_BIT  (STEPPER_MOTOR_L_PIN1 - 8)  /**< bit of motor L's wire 1 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_L_PIN2 >= 0 && STEPPER_MOTOR_L_PIN2 <= 13)
#error "STEPPER_MOTOR_L_PIN2 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_L_PIN2 < 8
#define MOTOR_L_WIRE2_PORT PORTD  /**< PORT register of motor L's wire 2 */
#define MOTOR_L_WIRE2_DDR  DDRD  /**< DDR register of motor L's wire 2 (direction) */
#define MOTOR_L_WIRE2_BIT  STEPPER_MOTOR_L_PIN2  /**< bit of motor L's wire 2 in its PORT and DDR registers */
#else
#define MOTOR_L_WIRE2_PORT PORTB  /**< PORT register of motor L's wire 2 */
#define MOTOR_L_WIRE2_DDR  DDRB  /**< DDR register of motor L's wire 2 (direction) */
#define MOTOR_L_WIRE2_BIT  (STEPPER_MOTOR_L_PIN2 - 8)  /**< bit of motor L's wire 2 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_L_PIN3 >= 0 && STEPPER_MOTOR_L_PIN3 <= 13)
#error "STEPPER_MOTOR_L_PIN3 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_L_PIN3 < 8
#define MOTOR_L_WIRE3_PORT PORTD  /**< PORT register of motor L's wire 3 */
#define MOTOR_L_WIRE3_DDR  DDRD  /**< DDR register of motor L's wire 3 (direction) */
#define MOTOR_L_WIRE3_BIT  STEPPER_MOTOR_L_PIN3  /**< bit of motor L's wire 3 in its PORT and DDR registers */
#else
#define MOTOR_L_WIRE3_PORT PORTB  /**< PORT register of motor L's wire 3 */
#define MOTOR_L_WIRE3_DDR  DDRB  /**< DDR register of motor L's wire 3 (direction) */
#define MOTOR_L_WIRE3_BIT  (STEPPER_MOTOR_L_PIN3 - 8)  /**< bit of motor L's wire 3 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_R_PIN0 >= 0 && STEPPER_MOTOR_R_PIN0 <= 13)
#error "STEPPER_MOTOR_R_PIN0 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_R_PIN0 < 8
#define MOTOR_R_WIRE0_PORT PORTD  /**< PORT register of motor R's wire 0 */
#define MOTOR_R_WIRE0_DDR  DDRD  /**< DDR register of motor R's wire 0 (direction) */
#define MOTOR_R_WIRE0_BIT  STEPPER_MOTOR_R_PIN0  /**< bit of motor R's wire 0 in its PORT and DDR registers */
#else
#define MOTOR_R_WIRE0_PORT PORTB  /**< PORT register of motor R's wire 0 */
#define MOTOR_R_WIRE0_DDR  DDRB  /**< DDR register of motor R's wire 0 (direction) */
#define MOTOR_R_WIRE0_BIT  (STEPPER_MOTOR_R_PIN0 - 8)  /**< bit of motor R's wire 0 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_R_PIN1 >= 0 && STEPPER_MOTOR_R_PIN1 <= 13)
#error "STEPPER_MOTOR_R_PIN1 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_R_PIN1 < 8
#define MOTOR_R_WIRE1_PORT PORTD  /**< PORT register of motor R's wire 1 */
#define MOTOR_R_WIRE1_DDR  DDRD  /**< DDR register of motor R's wire 1 (direction) */
#define MOTOR_R_WIRE1_BIT  STEPPER_MOTOR_R_PIN1  /**< bit of motor R's wire 1 in its PORT and DDR registers */
#else
#define MOTOR_R_WIRE1_PORT PORTB  /**< PORT register of motor R's wire 1 */
#define MOTOR_R_WIRE1_DDR  DDRB  /**< DDR register of motor R's wire 1 (direction) */
#define MOTOR_R_WIRE1_BIT  (STEPPER_MOTOR_R_PIN1 - 8)  /**< bit of motor R's wire 1 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_R_PIN2 >= 0 && STEPPER_MOTOR_R_PIN2 <= 13)
#error "STEPPER_MOTOR_R_PIN2 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_R_PIN2 < 8
#define MOTOR_R_WIRE2_PORT PORTD  /**< PORT register of motor R's wire 2 */
#define MOTOR_R_WIRE2_DDR  DDRD  /**< DDR register of motor R's wire 2 (direction) */
#define MOTOR_R_WIRE2_BIT  STEPPER_MOTOR_R_PIN2  /**< bit of motor R's wire 2 in its PORT and DDR registers */
#else
#define MOTOR_R_WIRE2_PORT PORTB  /**< PORT register of motor R's wire 2 */
#define MOTOR_R_WIRE2_DDR  DDRB  /**< DDR register of motor R's wire 2 (direction) */
#define MOTOR_R_WIRE2_BIT  (STEPPER_MOTOR_R_PIN2 - 8)  /**< bit of motor R's wire 2 in its PORT and DDR registers */
#endif

#if !(STEPPER_MOTOR_R_PIN3 >= 0 && STEPPER_MOTOR_R_PIN3 <= 13)
#error "STEPPER_MOTOR_R_PIN3 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_R_PIN3 < 8
#define MOTOR_R_WIRE3_PORT PORTD  /**< PORT register of motor R's wire 3 */
#define MOTOR_R_WIRE3_DDR  DDRD  /**< DDR register of motor R's wire 3 (direction) */
#define MOTOR_R_WIRE3_BIT  STEPPER_MOTOR_R_PIN3  /**< bit of motor R's wire 3 in its PORT and DDR registers */
#else
#define MOTOR_R_WIRE3_PORT PORTB  /**< PORT register of motor R's wire 3 */
#define MOTOR_R_WIRE3_DDR  DDRB  /**< DDR register of motor R's wire 3 (direction) */
#define MOTOR_R_WIRE3_BIT  (STEPPER_MOTOR_R_PIN3 - 8)  /**< bit of motor R's wire 3 in its PORT and DDR registers */
#endif

/**
 * Generic SET/CLR/DDR_SET macros, expressed only in terms of the
 * per-wire _PORT/_DDR/_BIT derivations above -- no register name is
 * duplicated here, so these never need editing when a wire moves port
 * (only the pin number in HwConfig.h does, and the \#if blocks
 * above pick the right port/DDR/bit for it automatically).
 */
#define MOTOR_L_WIRE0_SET()     (MOTOR_L_WIRE0_PORT |= (uint8_t)(1u << MOTOR_L_WIRE0_BIT))
#define MOTOR_L_WIRE0_CLR()     (MOTOR_L_WIRE0_PORT &= (uint8_t)~(1u << MOTOR_L_WIRE0_BIT))  /**< drive motor L's wire 0 low */
#define MOTOR_L_WIRE0_DDR_SET() (MOTOR_L_WIRE0_DDR  |= (uint8_t)(1u << MOTOR_L_WIRE0_BIT))  /**< make motor L's wire 0 an output */

#define MOTOR_L_WIRE1_SET()     (MOTOR_L_WIRE1_PORT |= (uint8_t)(1u << MOTOR_L_WIRE1_BIT))  /**< drive motor L's wire 1 high */
#define MOTOR_L_WIRE1_CLR()     (MOTOR_L_WIRE1_PORT &= (uint8_t)~(1u << MOTOR_L_WIRE1_BIT))  /**< drive motor L's wire 1 low */
#define MOTOR_L_WIRE1_DDR_SET() (MOTOR_L_WIRE1_DDR  |= (uint8_t)(1u << MOTOR_L_WIRE1_BIT))  /**< make motor L's wire 1 an output */

#define MOTOR_L_WIRE2_SET()     (MOTOR_L_WIRE2_PORT |= (uint8_t)(1u << MOTOR_L_WIRE2_BIT))  /**< drive motor L's wire 2 high */
#define MOTOR_L_WIRE2_CLR()     (MOTOR_L_WIRE2_PORT &= (uint8_t)~(1u << MOTOR_L_WIRE2_BIT))  /**< drive motor L's wire 2 low */
#define MOTOR_L_WIRE2_DDR_SET() (MOTOR_L_WIRE2_DDR  |= (uint8_t)(1u << MOTOR_L_WIRE2_BIT))  /**< make motor L's wire 2 an output */

#define MOTOR_L_WIRE3_SET()     (MOTOR_L_WIRE3_PORT |= (uint8_t)(1u << MOTOR_L_WIRE3_BIT))  /**< drive motor L's wire 3 high */
#define MOTOR_L_WIRE3_CLR()     (MOTOR_L_WIRE3_PORT &= (uint8_t)~(1u << MOTOR_L_WIRE3_BIT))  /**< drive motor L's wire 3 low */
#define MOTOR_L_WIRE3_DDR_SET() (MOTOR_L_WIRE3_DDR  |= (uint8_t)(1u << MOTOR_L_WIRE3_BIT))  /**< make motor L's wire 3 an output */

#define MOTOR_R_WIRE0_SET()     (MOTOR_R_WIRE0_PORT |= (uint8_t)(1u << MOTOR_R_WIRE0_BIT))  /**< drive motor R's wire 0 high */
#define MOTOR_R_WIRE0_CLR()     (MOTOR_R_WIRE0_PORT &= (uint8_t)~(1u << MOTOR_R_WIRE0_BIT))  /**< drive motor R's wire 0 low */
#define MOTOR_R_WIRE0_DDR_SET() (MOTOR_R_WIRE0_DDR  |= (uint8_t)(1u << MOTOR_R_WIRE0_BIT))  /**< make motor R's wire 0 an output */

#define MOTOR_R_WIRE1_SET()     (MOTOR_R_WIRE1_PORT |= (uint8_t)(1u << MOTOR_R_WIRE1_BIT))  /**< drive motor R's wire 1 high */
#define MOTOR_R_WIRE1_CLR()     (MOTOR_R_WIRE1_PORT &= (uint8_t)~(1u << MOTOR_R_WIRE1_BIT))  /**< drive motor R's wire 1 low */
#define MOTOR_R_WIRE1_DDR_SET() (MOTOR_R_WIRE1_DDR  |= (uint8_t)(1u << MOTOR_R_WIRE1_BIT))  /**< make motor R's wire 1 an output */

#define MOTOR_R_WIRE2_SET()     (MOTOR_R_WIRE2_PORT |= (uint8_t)(1u << MOTOR_R_WIRE2_BIT))  /**< drive motor R's wire 2 high */
#define MOTOR_R_WIRE2_CLR()     (MOTOR_R_WIRE2_PORT &= (uint8_t)~(1u << MOTOR_R_WIRE2_BIT))  /**< drive motor R's wire 2 low */
#define MOTOR_R_WIRE2_DDR_SET() (MOTOR_R_WIRE2_DDR  |= (uint8_t)(1u << MOTOR_R_WIRE2_BIT))  /**< make motor R's wire 2 an output */

#define MOTOR_R_WIRE3_SET()     (MOTOR_R_WIRE3_PORT |= (uint8_t)(1u << MOTOR_R_WIRE3_BIT))  /**< drive motor R's wire 3 high */
#define MOTOR_R_WIRE3_CLR()     (MOTOR_R_WIRE3_PORT &= (uint8_t)~(1u << MOTOR_R_WIRE3_BIT))  /**< drive motor R's wire 3 low */
#define MOTOR_R_WIRE3_DDR_SET() (MOTOR_R_WIRE3_DDR  |= (uint8_t)(1u << MOTOR_R_WIRE3_BIT))  /**< make motor R's wire 3 an output */

#if F_CPU != 16000000UL
#error "Timer1 configuration assumes F_CPU == 16MHz -- recompute STEPPER_COUNTS_PER_S and the delay limits for another clock"
#endif
#if (F_CPU / STEPPER_TIMER_PRESCALER) != STEPPER_COUNTS_PER_S
#error "STEPPER_COUNTS_PER_S is not F_CPU / STEPPER_TIMER_PRESCALER -- the hold unit would silently be wrong"
#endif

/**
 * @brief One combined step entry (@req{REQ-CoreLR-D-Stepper-0001}, unit changed by -0010).
 * Packed to 3 bytes -- exactly what the fields need.
 *
 *   byte:        0                 1        2
 *            +--------+        +--------+--------+
 *            |CoilBits|        |  HoldCountsAndPen  |
 *            +--------+        +--------+--------+
 *
 *   MotorCoilBits, bit 7..0:
 *     | R3 R2 R1 R0 | L3 L2 L1 L0 |
 *      Motor R wires   Motor L wires
 *
 *   HoldCountsAndPen (uint16_t), bit 15..0:
 *     | Pen |         hold counts (0..16383)          |
 *     15  14 13                                      0
 *     Stepper_PenAction_t,        STEPPER_MAX_HOLD_COUNTS max;
 *     the actual target state,   Timer1 counts to hold this entry (0.5 us each)
 *     never a toggle (see the
 *     module header's rationale)
 *
 * Was 4 bytes until 2026-09-10, with a 4th reserved byte kept so the
 * array stride would be a power of two -- letting the compiler index
 * with a shift instead of a multiply. Dropped on maintainer review:
 * with RAM the binding constraint (85% used), 16 bytes of queue matter
 * more than the few instructions a multiply-by-3 costs, and on AVR that
 * multiply is itself just a shift plus an add.
 */
typedef struct
{
   uint8_t  MotorCoilBits;   /**< bits 0-3 = Motor L wires 0-3, bits 4-7 = Motor R wires 0-3 */
   uint16_t HoldCountsAndPen; /**< bits 0-13 = hold in Timer1 counts (STEPPER_MAX_HOLD_COUNTS max);
                                   bits 14-15 = a Stepper_PenAction_t value, stored directly
                                   (the actual target state, not a toggle -- so a
                                   skipped/reordered entry can never leave a tracked
                                   state silently wrong) */
} __attribute__((packed)) Entry_t;

/** Queue indices (write/read). */
typedef struct
{
   uint8_t WriteIndex;   /**< the slot the producer (main loop) fills next; advanced only by it */
   uint8_t ReadIndex;    /**< the slot the ISR empties next; advanced only by it */
} QueueIndices_t;

/**
 * @brief The single, unified step queue -- a classic ring buffer, one
 * slot always kept empty to distinguish full from empty without a
 * separate counter (`WriteIndex+1 == ReadIndex` means full):
 *
 *          ReadIndex                    WriteIndex
 *              |                            |
 *              v                            v
 *      +----+----+----+----+----+----+----+----+
 *      |    | E1 | E2 | E3 |    |    |    |    |   Entries[STEPPER_QUEUE_SIZE]
 *      +----+----+----+----+----+----+----+----+
 *        0    1    2    3    4    5    6    7
 *
 *   enqueue (Motion/PenControl) -> writes at WriteIndex, advances it
 *   dequeue (the ISR) -> reads at ReadIndex, advances it
 *
 *   Stepper_IsEmpty() is *not* simply WriteIndex == ReadIndex --
 *   see its own doc comment: a hold request too long to fit here in one
 *   go leaves a pending backlog (@req{REQ-CoreLR-D-Stepper-0003}) that
 *   isn't reflected in this ring buffer yet, and still counts as "not
 *   empty".
 */
typedef struct
{
   QueueIndices_t Indices;                 /**< where the next entry is written and where the next is read */
   Entry_t Entries[STEPPER_QUEUE_SIZE];    /**< the ring's slots (one is always kept empty) */
} Queue_t;

/**
 * Forward declarations for every file-local helper, right after the
 * macros -- confirmed 2026-09-06 as a standing convention: function
 * bodies can then be ordered however reads best, without a definition
 * having to appear before its first use (this is exactly the ordering
 * bug the real Timer2 ISR hit earlier in this file).
 */
static void ApplyMotorCoilBits(uint8_t bits);
static uint8_t QueueIsEmpty(void);
static uint8_t EnqueueRawEntry(uint8_t combinedCoilBits, uint16_t holdCountsChunk, Stepper_PenAction_t penAction);
static uint16_t NextChunk(uint32_t remaining);
static void ArmIfNeeded(void);
static uint8_t DrainPending(void);
static uint8_t EnqueueSplit(uint8_t combinedCoilBits, uint32_t holdCounts, Stepper_PenAction_t penAction);

/** The step queue: filled from the main loop (Motion, PenControl, the jogs), emptied by the compare ISR. */
static Queue_t s_queue;
/** Coil bits (both motors) of the last entry enqueued: a pen-only request repeats them, so it commands no movement, and a motion request whose bits differ
 *  from them is the first step of a movement, which invalidates the stored position (@req{REQ-CoreLR-D-Stepper-0014}). */
static uint8_t s_lastCoilBits;
/** 1 between Stepper_HaltRequests() and Stepper_ResumeRequests(): Stepper_RequestMotion() refuses new motion (pen-only entries still pass). */
static volatile uint8_t s_enqueueHalted;

/** 1 while the compare interrupt is armed, i.e. while the ISR still has an
 * entry's hold to run out. Set by ArmIfNeeded() inside a critical section,
 * cleared by the ISR; one byte (@req{REQ-CoreLR-D-Stepper-0013}). */
static volatile uint8_t s_armed;

#if CORELR_DEBUG_MISS_COUNT
/** Diagnostic starvation counter -- see Stepper_StarvedCount(). Written
 * only from main-loop context (Stepper_CountStarvation()), one byte. */
static volatile uint8_t s_starvedCount;
#endif

/** Late events: the ISR found its next compare point already behind the
 * counter and had to fire at once (@req{REQ-CoreLR-D-Stepper-0011}). Diagnostic. */
static volatile uint8_t s_lateCount;

/**
 * Backlog for a hold request that couldn't be fully enqueued in one go
 * (@req{REQ-CoreLR-D-Stepper-0003}, revised 2026-09-08).
 *
 * No critical section guards these two, and that is a checked fact,
 * not an oversight: the rule for needing one is "touched by more than
 * one execution context that can genuinely interleave", not "wider
 * than 8 bits". `s_pendingTicksRemaining` (uint32_t, several
 * instructions to read/write on AVR) is written/read *only* from
 * main-loop context -- EnqueueSplit(), DrainPending(),
 * Stepper_Task(), Stepper_IsEmpty() -- and verified
 * (by reading the ISR's actual body, not by assuming) to never be touched
 * by it. The Timer1 ISR firing mid-update is therefore harmless: it doesn't
 * read or write this memory, so there is nothing to tear -- main-loop
 * execution simply pauses and resumes its own sequential update, unobserved
 * by anyone else. This is the same reasoning applied to
 * s_queue.Indices.WriteIndex/ReadIndex. Contrast with s_armed, which the ISR
 * clears and the producer sets, and which is therefore only ever set inside
 * the critical section of ArmIfNeeded().
 */
static uint8_t s_pendingCoilBits;
static uint32_t s_pendingTicksRemaining;   /**< hold counts of the request that did not fit in the ring yet; 0 = no backlog */

static void ApplyMotorCoilBits(uint8_t bits)
{
   if (bits & 0x01u) { MOTOR_L_WIRE0_SET(); } else { MOTOR_L_WIRE0_CLR(); }
   if (bits & 0x02u) { MOTOR_L_WIRE1_SET(); } else { MOTOR_L_WIRE1_CLR(); }
   if (bits & 0x04u) { MOTOR_L_WIRE2_SET(); } else { MOTOR_L_WIRE2_CLR(); }
   if (bits & 0x08u) { MOTOR_L_WIRE3_SET(); } else { MOTOR_L_WIRE3_CLR(); }
   if (bits & 0x10u) { MOTOR_R_WIRE0_SET(); } else { MOTOR_R_WIRE0_CLR(); }
   if (bits & 0x20u) { MOTOR_R_WIRE1_SET(); } else { MOTOR_R_WIRE1_CLR(); }
   if (bits & 0x40u) { MOTOR_R_WIRE2_SET(); } else { MOTOR_R_WIRE2_CLR(); }
   if (bits & 0x80u) { MOTOR_R_WIRE3_SET(); } else { MOTOR_R_WIRE3_CLR(); }
}

void Stepper_Init(void)
{
   s_queue.Indices.WriteIndex = 0;
   s_queue.Indices.ReadIndex = 0;
   s_lastCoilBits = 0;
   s_enqueueHalted = 0;
   s_armed = 0;
#if CORELR_DEBUG_MISS_COUNT
   s_starvedCount = 0;
#endif
   s_lateCount = 0;
   s_pendingCoilBits = 0;
   s_pendingTicksRemaining = 0;

   /* Motor pins as outputs, all low. Via the same per-wire DDR macros
    * used everywhere else in this module -- this function never
    * computes a port/bit itself, so the port mapping stays
    * encapsulated in exactly one place. */
   MOTOR_L_WIRE0_DDR_SET();
   MOTOR_L_WIRE1_DDR_SET();
   MOTOR_L_WIRE2_DDR_SET();
   MOTOR_L_WIRE3_DDR_SET();
   MOTOR_R_WIRE0_DDR_SET();
   MOTOR_R_WIRE1_DDR_SET();
   MOTOR_R_WIRE2_DDR_SET();
   MOTOR_R_WIRE3_DDR_SET();

   /* Timer1: normal mode (free-running), prescaler 8. The compare interrupt
    * stays disarmed until the first entry is queued (ArmIfNeeded()). Timer1 is
    * this module's alone since the Servo library, which used to own it, was
    * replaced (REQ-CoreLR-D-PenControl-0003). */
   TCCR1A = 0u;
   TCCR1B = (uint8_t)(1u << CS11);
   TIMSK1 = (uint8_t)(TIMSK1 & (uint8_t)~(1u << OCIE1A));
}

/**
 * @brief Ring-buffer-only emptiness check -- safe to call from
 * the ISR. Unlike the public
 * Stepper_IsEmpty(), never touches s_pendingTicksRemaining: that
 * uint32_t is written only from main-loop context (EnqueueSplit()/
 * DrainPending()) and is not atomically readable from an ISR on 8-bit
 * AVR, so an ISR-reachable emptiness check must never look at it.
 */
static uint8_t QueueIsEmpty(void)
{
   uint8_t writeIndex = s_queue.Indices.WriteIndex;
   uint8_t readIndex = s_queue.Indices.ReadIndex;
   return (uint8_t)(writeIndex == readIndex);
}

/** @brief Enqueue one already-split (STEPPER_DELAY_MIN_COUNTS ..
 * STEPPER_DELAY_MAX_COUNTS) entry, publish it, and arm the timer if it is idle.
 * penAction is stored as the actual target state (@req{REQ-CoreLR-D-Stepper-0001})
 * -- not a toggle -- so this module never needs to track "what pen state
 * did we last enqueue/apply" anywhere; each entry is self-describing.
 *
 * There is no merging of adjacent identical entries any more
 * (@req{REQ-CoreLR-D-Stepper-0009}, removed 2026-10-06): every entry now follows a
 * step event, so none can match its predecessor, and the pieces of a split hold
 * are already full. */
static uint8_t EnqueueRawEntry(uint8_t combinedCoilBits, uint16_t holdCountsChunk, Stepper_PenAction_t penAction)
{
   uint8_t writeIndex;
   uint8_t nextWriteIndex;

   /* No halt check here, deliberately (revised 2026-09-10): the halt
    * gates *motion requests*, in Stepper_RequestMotion(), not the queue
    * machinery. Two consequences, both wanted:
    *   - DrainPending() keeps draining an already-accepted backlog, so
    *     work the caller was told "1" for still completes;
    *   - a pen-only entry still gets in, which is what lets a pause or
    *     a stop lift the pen off the paper. Blocking it here would have
    *     made lifting the pen impossible at exactly the moment it
    *     matters (REQ-CoreLR-D-Stepper-0007). */
   writeIndex = s_queue.Indices.WriteIndex;
   nextWriteIndex = (uint8_t)((writeIndex + 1u) % STEPPER_QUEUE_SIZE);
   if (nextWriteIndex == s_queue.Indices.ReadIndex)
   {
      return 0; /* queue full */
   }

   s_queue.Entries[writeIndex].MotorCoilBits = combinedCoilBits;
   s_queue.Entries[writeIndex].HoldCountsAndPen = (uint16_t)((holdCountsChunk & STEPPER_MAX_HOLD_COUNTS)
                                                             | (((uint16_t)penAction << STEPPER_PEN_SHIFT) & STEPPER_PEN_MASK));
   s_queue.Indices.WriteIndex = nextWriteIndex; /* publish first ... */
   s_lastCoilBits = combinedCoilBits;
   ArmIfNeeded();                                /* ... then arm (REQ-CoreLR-D-Stepper-0013) */
   return 1;
}

/**
 * @brief The size of the next piece of a hold of `remaining` counts
 * (@req{REQ-CoreLR-D-Stepper-0012}). A hold above STEPPER_DELAY_MAX_COUNTS is split,
 * and the trap is a remainder below STEPPER_DELAY_MIN_COUNTS, which is itself
 * unusable: MAX+5 must not become MAX then 5 (maintainer, 2026-10-06).
 *
 *     remaining <= MAX                       -> all of it
 *     remaining - MAX < MIN                  -> remaining - MIN   (leaves exactly MIN)
 *     otherwise                              -> MAX
 *
 * Needs MAX >= 2*MIN (checked in SwConfig.h), which also makes every piece
 * at least MIN. The caller guarantees remaining >= MIN on entry.
 */
static uint16_t NextChunk(uint32_t remaining)
{
   if (remaining <= (uint32_t)STEPPER_DELAY_MAX_COUNTS)
   {
      return (uint16_t)remaining;
   }
   if ((remaining - STEPPER_DELAY_MAX_COUNTS) < (uint32_t)STEPPER_DELAY_MIN_COUNTS)
   {
      return (uint16_t)(remaining - STEPPER_DELAY_MIN_COUNTS);
   }
   return (uint16_t)STEPPER_DELAY_MAX_COUNTS;
}

/**
 * @brief Arm the compare interrupt if it is idle and there is something to
 * pop (@req{REQ-CoreLR-D-Stepper-0013}). Called after an entry has been published.
 *
 * The test is inside the critical section, and it re-checks the queue: if the
 * ISR popped the entry we just published between publish and here, the queue
 * is empty and arming would only start a timer with nothing to do.
 */
static void ArmIfNeeded(void)
{
   uint8_t previousSreg = SREG;

   cli();
   if (!s_armed && !QueueIsEmpty())
   {
      OCR1A = (uint16_t)(TCNT1 + STEPPER_START_DELAY_COUNTS);
      TIFR1 = (uint8_t)(1u << OCF1A);       /* a stale match flag must not fire at once */
      TIMSK1 = (uint8_t)(TIMSK1 | (1u << OCIE1A));
      s_armed = 1;
   }
   SREG = previousSreg;
}

/**
 * @brief Try to enqueue as much of the pending backlog as fits right
 * now (@req{REQ-CoreLR-D-Stepper-0003}, revised 2026-09-08 after
 * maintainer review). All backlog chunks carry
 * STEPPER_PEN_NONE: the pen action, if any, already fired on the
 * original request's first chunk, before any backlog could exist.
 * @return 1 if the backlog is now fully drained (possibly already was),
 *         0 if some of it still remains (queue filled up again).
 */
static uint8_t DrainPending(void)
{
   while (s_pendingTicksRemaining > 0)
   {
      uint16_t chunk = NextChunk(s_pendingTicksRemaining);

      if (!EnqueueRawEntry(s_pendingCoilBits, chunk, STEPPER_PEN_NONE))
      {
         return 0; /* still not fully drained */
      }
      s_pendingTicksRemaining -= chunk;
   }
   return 1;
}

/**
 * @brief Shared hold-splitting entry point (@req{REQ-CoreLR-D-Stepper-0003},
 * revised 2026-09-08): a request that doesn't fit the queue in one go is
 * no longer an all-or-nothing failure. Once its first chunk lands, the
 * request is "accepted" (the caller may move on -- Motion
 * commits its candidate waypoint, PenControl marks the request issued)
 * even if the tail has to trickle in later via Stepper_Task()
 * or a later Enqueue*() call, as pending backlog:
 *
 *   holdCounts = 600000 (the 300 ms pen settle; > STEPPER_DELAY_MAX_COUNTS = 16000),
 *   queue has room for only 2 entries right now:
 *
 *   this call:  [16000 counts] -> [16000 counts] -> (queue full)
 *                penAction         NONE             568000 counts stashed as
 *                (fires once)                       s_pendingCoilBits/
 *                                                   s_pendingTicksRemaining
 *   returns 1 (accepted)
 *
 *   later (Stepper_Task(), once space frees up):
 *              [16000] [16000] ... <- drains the backlog, no caller involved
 *
 * A *new* request is never interleaved with a still-pending backlog
 * from an *older* one (they may have different coil bits -- physically
 * different motor states -- so mixing them would corrupt the motion):
 * the backlog is drained first; if it does not fully drain, the new
 * request is rejected outright (same as an ordinary queue-full), never
 * partially mixed in behind it.
 */
static uint8_t EnqueueSplit(uint8_t combinedCoilBits, uint32_t holdCounts, Stepper_PenAction_t penAction)
{
   uint16_t firstChunk;

   /* See EnqueueRawEntry(): the halt is enforced by
    * Stepper_RequestMotion(), not here. */
   if (holdCounts < (uint32_t)STEPPER_DELAY_MIN_COUNTS)
   {
      holdCounts = STEPPER_DELAY_MIN_COUNTS; /* shorter than the ISR can honour (REQ-CoreLR-D-Stepper-0012) */
   }

   if (s_pendingTicksRemaining > 0 && !DrainPending())
   {
      return 0; /* older backlog still not fully drained -- reject the new request */
   }

   firstChunk = NextChunk(holdCounts);
   if (!EnqueueRawEntry(combinedCoilBits, firstChunk, penAction))
   {
      return 0; /* not even the first chunk fits -- request fully rejected */
   }
   holdCounts -= firstChunk;

   while (holdCounts > 0)
   {
      uint16_t chunk = NextChunk(holdCounts);

      if (!EnqueueRawEntry(combinedCoilBits, chunk, STEPPER_PEN_NONE))
      {
         break; /* queue filled up mid-split: stash the remainder as pending, below */
      }
      holdCounts -= chunk;
   }
   if (holdCounts > 0)
   {
      s_pendingCoilBits = combinedCoilBits;
      s_pendingTicksRemaining = holdCounts;
   }
   return 1; /* accepted -- fully enqueued now, or the tail is pending */
}

uint8_t Stepper_RequestMotion(uint8_t lCoilBits, uint8_t rCoilBits, uint32_t holdCounts, Stepper_PenAction_t penAction)
{
   uint8_t combined;

   /* The single place the halt is enforced (REQ-CoreLR-D-Stepper-0007,
    * semantics revised 2026-09-10): it blocks *new motion*, and lets
    * whatever is already queued run to completion. Pen-only entries are
    * deliberately not gated -- see Stepper_RequestPenOnly(). */
   if (s_enqueueHalted)
   {
      return 0;
   }
   combined = (uint8_t)((lCoilBits & 0x0Fu) | ((rCoilBits & 0x0Fu) << 4));

   /* The persisted position must be invalid before the first step that
    * could move a motor (REQ-CoreLR-F-Calibration-0007). This is the one
    * place every motion entry passes through -- Motion's waypoints and the
    * adjustment jog alike -- so it is also the one place that enforces it
    * (REQ-CoreLR-D-Stepper-0014). A flag test once already invalid; one
    * waited-for EEPROM byte on the first entry of a movement. Done before
    * the entry is published, hence before the ISR can see it. */
   if (combined != s_lastCoilBits)
   {
      Calibration_InvalidatePosition();
   }
   return EnqueueSplit(combined, holdCounts, penAction);
}

uint8_t Stepper_RequestPenOnly(Stepper_PenAction_t penAction, uint32_t holdCounts)
{
   /* Not gated by the halt, by design: lifting the pen is a
    * put-into-safety action, not job motion, and a pause or a stop has
    * to be able to get the nib off the paper. The entry carries
    * s_lastCoilBits, so it commands no movement. */
   return EnqueueSplit(s_lastCoilBits, holdCounts, penAction);
}

#if CORELR_DEBUG_JOBSTATE
uint8_t Stepper_DebugFlags(void)
{
   return (uint8_t)((s_enqueueHalted ? 1u : 0u) | (s_armed ? 2u : 0u) | ((s_pendingTicksRemaining > 0u) ? 4u : 0u) | (Stepper_IsIdle() ? 8u : 0u));
}
#endif

uint8_t Stepper_QueuedCount(void)
{
   uint8_t writeIndex = s_queue.Indices.WriteIndex;
   uint8_t readIndex = s_queue.Indices.ReadIndex;

   /* Modulo, not a mask: STEPPER_QUEUE_SIZE is a power of two today but
    * nothing in this module requires it to stay one. */
   return (uint8_t)(((uint8_t)(writeIndex + STEPPER_QUEUE_SIZE - readIndex))
                    % (uint8_t)STEPPER_QUEUE_SIZE);
}

#if CORELR_DEBUG_MISS_COUNT
uint8_t Stepper_StarvedCount(void)
{
   return s_starvedCount;
}

void Stepper_ResetStarvedCount(void)
{
   s_starvedCount = 0u;
}

void Stepper_CountStarvation(void)
{
   if (s_starvedCount < 255u)
   {
      s_starvedCount++;
   }
}
#endif /* CORELR_DEBUG_MISS_COUNT */

uint8_t Stepper_IsEmpty(void)
{
   /* "Empty" means nothing left to do at all, including backlog not yet
    * even written to the ring buffer -- see QueueIsEmpty()'s comment
    * for why the ISR uses a separate, narrower check instead of this
    * one. Safe to read s_pendingTicksRemaining here: this function only
    * runs from main-loop context, same as the code that writes it. */
   return (uint8_t)(QueueIsEmpty() && (s_pendingTicksRemaining == 0));
}

uint8_t Stepper_IsIdle(void)
{
   /* Disarmed means the ISR found the queue empty at a compare, i.e. the last
    * entry's hold has expired (REQ-CoreLR-D-Stepper-0013). One byte: no section. */
   return (uint8_t)(!s_armed && Stepper_IsEmpty());
}

void Stepper_HaltRequests(void)
{
   s_enqueueHalted = 1;
}

void Stepper_ResumeRequests(void)
{
   s_enqueueHalted = 0;
}

uint8_t Stepper_IsHalted(void)
{
   return s_enqueueHalted;
}

void Stepper_Task(void)
{
   /* Opportunistically drains any pending backlog (REQ-CoreLR-D-Stepper-0003)
    * as soon as queue space frees up, rather than only when the next
    * Enqueue*() call happens to come in -- a caller could otherwise
    * stall waiting on a success it will only see once this runs.
    * Cheap no-op (one comparison) when nothing is pending, which is
    * true for every caller in this project today. */
   if (s_pendingTicksRemaining > 0)
   {
      DrainPending();
   }
}

/**
 * @brief The step-event ISR (@req{REQ-CoreLR-D-Stepper-0011}).
 *
 *   pop entry --none--> disarm, done            (the last hold has expired)
 *      |
 *   apply coil bits; hand over pen action
 *      |
 *   OCR1A += hold          relative to the previous compare, not to TCNT1
 *      |
 *   already (almost) late? --> OCR1A = TCNT1 + margin, count it
 */
ISR(TIMER1_COMPA_vect)
{
   uint8_t readIndex = s_queue.Indices.ReadIndex;
   uint16_t packed;
   uint16_t hold;
   uint16_t previous;
   uint8_t penAction;

   if (readIndex == s_queue.Indices.WriteIndex)
   {
      TIMSK1 = (uint8_t)(TIMSK1 & (uint8_t)~(1u << OCIE1A));
      s_armed = 0;
      return;
   }

   ApplyMotorCoilBits(s_queue.Entries[readIndex].MotorCoilBits);
   packed = s_queue.Entries[readIndex].HoldCountsAndPen;
   s_queue.Indices.ReadIndex = (uint8_t)((readIndex + 1u) % STEPPER_QUEUE_SIZE);

   /* Stored as the actual target state, not a toggle -- read and forward
    * directly, no tracked baseline that could drift. */
   penAction = (uint8_t)((packed & STEPPER_PEN_MASK) >> STEPPER_PEN_SHIFT);
   if (penAction != (uint8_t)STEPPER_PEN_NONE)
   {
      PenControl_ApplyFromIsr(penAction);
   }

   hold = (uint16_t)(packed & STEPPER_MAX_HOLD_COUNTS);
   previous = OCR1A;
   OCR1A = (uint16_t)(previous + hold);
   if ((uint16_t)((uint16_t)(TCNT1 - previous) + STEPPER_LATE_MARGIN_COUNTS) >= hold)
   {
      OCR1A = (uint16_t)(TCNT1 + STEPPER_LATE_MARGIN_COUNTS);
      if (s_lateCount < 255u)
      {
         s_lateCount++;
      }
   }
}
