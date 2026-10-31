/*
 * Motion.c
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
 * @file Motion.c
 * @brief See Motion.h.
 *
 * The step-event generator (v2, @req{REQ-CoreLR-D-Motion-0018}..@req{REQ-CoreLR-D-Motion-0020}). A straight
 * move is cut into coarse waypoints exactly on the line, ~214 um apart; every
 * waypoint goes through the absolute inverse transform; inside a segment each
 * motor's cable length is linear in time, and a step is emitted at the instant
 * that line crosses the decision threshold around the commanded count:
 *
 *   Cartesian line -> waypoints P_k -> L,R in 1/256 step -> crossing instants
 *                                                       -> entries {coil bits,
 *                                                          hold to next event}
 *
 * Absolute throughout (@req{REQ-CoreLR-D-Motion-0002}/@req{REQ-CoreLR-D-Motion-0004}): the only per-motor state
 * carried from one segment to the next is the commanded count; the fractional
 * position is recomputed from the waypoint each time, never accumulated.
 *
 * Retry discipline: an entry that Stepper refuses (queue full) is kept in a
 * one-entry outbox and offered again, so the generator's state never runs ahead
 * of what was actually queued.
 *
 * Measured and verified as a prototype first: tools/prototypes/evproto, whose test and
 * fuzz results are in @req{REQ-CoreLR-D-Motion-0018}.
 */

#include <avr/pgmspace.h>
#include <string.h>
#include "Motion.h"
#include "Stepper.h"
#include "Calibration.h"
#include "Trig.h"
#include "HwConfig.h"
#include "SwConfig.h"

/** The drawing <-> internal conversion of Y, and its inverse: the one place CORELR_Y_UP (SwConfig.h) acts in this file.
 *  `offset` is the internal Y of the drawing origin. @req{REQ-CoreLR-D-Motion-0025}. */
#if CORELR_Y_UP
#define MOTION_Y_TO_INTERNAL(offset, y)    ((offset) - (y))
/** The drawing Y of the internal Y `iy`, the inverse of MOTION_Y_TO_INTERNAL(). */
#define MOTION_Y_FROM_INTERNAL(offset, iy) ((offset) - (iy))
/** The offset (internal Y of the drawing origin) that makes the internal Y `iy` read as the drawing Y `y`. */
#define MOTION_OFFSET_FOR(iy, y)           ((iy) + (y))
#else
#define MOTION_Y_TO_INTERNAL(offset, y)    ((offset) + (y))
/** The drawing Y of the internal Y `iy`, the inverse of MOTION_Y_TO_INTERNAL(). */
#define MOTION_Y_FROM_INTERNAL(offset, iy) ((iy) - (offset))
/** The offset (internal Y of the drawing origin) that makes the internal Y `iy` read as the drawing Y `y`. */
#define MOTION_OFFSET_FOR(iy, y)           ((iy) - (y))
#endif

/** Spool circumference, integer-only (355/113 ~= pi). */
#define SPOOL_CIRCUMFERENCE_UM ((MOTION_SPOOL_DIAMETER_UM * 355L) / 113L)

/** Sub-steps per um of cable, as a 2^24 fixed-point constant:
 *  steps_per_rev * MOTION_SUBSTEP / circumference. */
#define MOTION_SUB_PER_UM_Q24 ((uint32_t)((((uint64_t)MOTION_STEPS_PER_REV_X1000 * MOTION_SUBSTEP) << 24) / ((uint64_t)SPOOL_CIRCUMFERENCE_UM * 1000u)))

/** Waypoint spacing, in um of Cartesian travel (@req{REQ-CoreLR-D-Motion-0019}). */
#define MOTION_WAYPOINT_UM     (((long)MOTION_WAYPOINT_STEPS * SPOOL_CIRCUMFERENCE_UM * 1000L) / MOTION_STEPS_PER_REV_X1000)

/** v^2 (in (0.25 mm/s)^2) gains a * 32 / 1000 per um of distance
 *  (2 * a[mm/s^2] * 1000 um/s^2 / 62500). As a multiply and a shift, since a
 *  32-bit division costs ~550 cycles and ~30 bytes a use: x * 6291 >> 10
 *  for a = 192 (6.1436 against 6.1440, 0.007 %). */
#define MOTION_V2_Q10 ((uint32_t)((MOTION_ACCEL_MM_S2 * 32UL * 1024UL + 500UL) / 1000UL))

/** Timer1 counts per um of travel per unit of velocity:
 *  T = d * counts_per_s / (v * 250 um/s) = d * 8000 / v. */
#define MOTION_COUNTS_PER_UM_UNIT (STEPPER_COUNTS_PER_S / 250UL)

/**
 * A pointer to s_move in a Y/Z register. Reaching a static struct member by
 * its constant address costs lds/sts, 4 bytes per byte moved; through a
 * pointer it is ldd/std, 2 bytes. LTO sees through `&s_move` and puts the
 * constant back, so the pointer is hidden from the optimiser with an empty
 * asm. Measured: see @req{REQ-CoreLR-D-Motion-0018}'s implementation notes.
 */
#define MOTION_MOVE_PTR(name) Move_t *name = &s_move; __asm__ volatile ("" : "+b"(name))

/** One motor step of cable, um (integer: 53.95 truncated). Below it a circle is not
 *  a circle to this machine (@req{REQ-CoreLR-D-Motion-0022}, degenerate cases). */
#define MOTION_STEP_UM ((uint32_t)((SPOOL_CIRCUMFERENCE_UM * 1000L) / MOTION_STEPS_PER_REV_X1000))

/** The radius from which the centripetal limit v^2 <= a*r no longer binds, i.e.
 *  where it reaches VMAX (@req{REQ-CoreLR-D-Motion-0023}): v^2 = r * Q10 >> 11. Above it
 *  the limit is VMAX, and r * Q10 is never computed (it would overflow 32 bits). */
#define MOTION_ARC_FREE_RADIUS_UM (((uint32_t)MOTION_VMAX_V16 * MOTION_VMAX_V16 << 11) / MOTION_V2_Q10 + 1u)

/** PlannerBlock_t::Flags bit: the block is an arc (else a straight line). */
#define PLAN_ARC 1u
/** PlannerBlock_t::Flags bit: the arc is clockwise (G2); clear for G3. Meaningful with PLAN_ARC. */
#define PLAN_CW  2u

/** "No crossing in this segment". */
#define MOTION_NO_EVENT 0xFFFFFFFFUL

/** 4-phase full-step coil table (@req{REQ-CoreLR-D-Motion-0007}),
 * validated against Walldraw/Lib/libraries/TinyStepper_28BYJ_48.
 * PROGMEM (flash-only, read via pgm_read_byte()): on classic AVR, a
 * plain `const` array is still copied into RAM at boot -- normal `ld`
 * instructions cannot address flash directly, only `lpm` can -- so
 * without PROGMEM this table would occupy both flash *and* RAM for no
 * reason (raised 2026-09-07, part of the project's RAM pass). */
static const uint8_t kPhaseTable[4] PROGMEM = { 0x0Cu, 0x06u, 0x03u, 0x09u };

/** Everything about the move being generated, its current segment, and the
 *  timeline of the entries (@req{REQ-CoreLR-D-Motion-0018}). One move at a time. */
typedef struct
{
   /* the move */
   Motion_Position_t Target;        /**< where the move ends, drawing frame, exact (not grid-rounded) */
   int8_t SignX;                    /**< direction of travel in X, +1 or -1 (lines) */
   int8_t SignY;                    /**< direction of travel in Y, +1 or -1 (lines) */
   uint16_t N;                      /**< segments in the move */
   uint16_t K;                      /**< segments completed */
   uint16_t Qx;                     /**< X, proportional interpolation (lines): |dx| / N, the quotient */
   uint16_t Rx;                     /**< X, proportional interpolation: |dx| % N, the remainder */
   uint16_t AccX;                   /**< X, proportional interpolation: the accumulator of the remainder (starts at N / 2) */
   uint16_t Qy;                     /**< Y, as Qx */
   uint16_t Ry;                     /**< Y, as Rx */
   uint16_t AccY;                   /**< Y, as AccX */
   uint32_t Len;                    /**< exact length of the move, um (the arc length for an arc) */
   uint16_t SegLenQ8;               /**< length of one segment, um in Q8 */
   uint32_t SQ8;                    /**< distance travelled at the start of the current segment, Q8 */
   uint8_t EntryV;                  /**< speed at the start of the move, units of 0.25 mm/s */
   uint8_t ExitV;                   /**< speed at the end of the move (the next block's entry speed, 0 at the last block), same unit */
   uint8_t CurV;                    /**< speed at the middle of the current segment, same unit; Motion_Halt() sizes its ramp from it */
   uint8_t Cap;                     /**< speed ceiling of the move: VMAX, or the centripetal limit of an arc */
   int16_t Ux;                      /**< direction of travel at the END of the move, X, normalised so that |Ux| + |Uy| = 256 (UnitDir()); the junction with the next block uses it */
   int16_t Uy;                      /**< same, Y */
   int16_t HaltSeg;                 /**< virtual end of a halt ramp, in segments; -1 = none (@req{REQ-CoreLR-D-Motion-0012}) */
   uint32_t RampStartQ8;            /**< distance the acceleration is measured from; 0, or the resume point */
   /* An arc (REQ-CoreLR-D-Motion-0022). The waypoint k is Center + Rotate(Rad_k, Theta_k), each of the two
    * a start value plus k/N of the total, as a quotient and a remainder spread by an accumulator -- the
    * rational k * total / N to the unit, not a sum of rounded steps. */
   uint8_t Arc;                     /**< 1 = this move is an arc */
   uint8_t Cw;                      /**< arc: 1 = clockwise (G2), 0 = counter-clockwise (G3) */
   Motion_Position_t Center;        /**< arc: the centre, drawing frame */
   uint32_t Theta;                  /**< arc: angle of the last waypoint, turns Q32 */
   uint32_t DTh;                    /**< arc: the angle step, the quotient of sweep / N, turns Q32 */
   uint16_t AccTh;                  /**< arc: accumulator of the angle step's remainder (starts at N / 2) */
   uint16_t RemTh;                  /**< arc: remainder of sweep / N, spread over the segments */
   uint32_t Rad;                    /**< arc: radius at the last waypoint, um */
   uint16_t DRq;                    /**< arc: the radius step, the quotient of |end radius - start radius| / N, um */
   uint16_t AccR;                   /**< arc: accumulator of the radius step's remainder (starts at N / 2) */
   uint16_t RemR;                   /**< arc: remainder of |end radius - start radius| / N, spread over the segments */
   int8_t RSign;                    /**< arc: +1 if the radius grows from start to end, -1 if it shrinks */
   uint8_t Moving;                  /**< a move is being generated */
   /* the segment in flight */
   uint8_t InSegment;               /**< a segment has been set up by BeginSegment() and its step events are being emitted */
   Motion_Position_t NextPos;       /**< its end waypoint, drawing frame */
   int32_t SL1;                     /**< its end, in 1/256 step: motor L */
   int32_t SR1;                     /**< its end, in 1/256 step: motor R */
   uint32_t T;                      /**< its duration, Timer1 counts */
   uint32_t TL;                     /**< next crossing of motor L, relative to the segment's start, or MOTION_NO_EVENT */
   uint32_t TR;                     /**< same for motor R */
   /* Commanded step counts: the only per-motor state that survives a segment, and what the
    * position store persists (REQ-CoreLR-D-Calibration-0006). Entry coil bits derive from them. */
   int32_t CmdL;                    /**< absolute step count commanded to motor L */
   int32_t CmdR;                    /**< absolute step count commanded to motor R */
   /* The ideal cable length, 1/256 step, at the last completed waypoint; valid only while SubValid. */
   int32_t SubL;                    /**< motor L, 1/256 step */
   int32_t SubR;                    /**< motor R, 1/256 step */
   uint8_t SubValid;                /**< SubL and SubR hold the ideal length of the last completed waypoint */
   /* The timeline of the entries, Timer1 counts, all differences (so wrap-around is harmless). An
    * entry's hold is the distance to the NEXT event, so the entry for an event is held back one
    * event -- the pending one -- until that distance is known; at the end of a move it is flushed,
    * its hold running to the end of the timeline. */
   uint32_t TSeg;                   /**< start of the current segment on the timeline */
   uint32_t PendTime;               /**< when the pending entry's bits were applied */
   uint32_t LastEvent;              /**< the time of the last step event; the next one is kept STEPPER_DELAY_MIN_COUNTS after it at least */
   uint8_t PendCoilL;               /**< the pending entry's coil bits, motor L */
   uint8_t PendCoilR;               /**< the pending entry's coil bits, motor R */
   uint8_t HavePending;             /**< a timeline is open: there is a pending entry (BeginTimeline()), not yet flushed (FlushPending()) */
   uint8_t AnyEvent;                /**< at least one step event on this timeline, so LastEvent is valid */
   /* The one-entry outbox: an entry Stepper refused, to be offered again. */
   uint8_t OutCoilL;                /**< the outbox entry's coil bits, motor L */
   uint8_t OutCoilR;                /**< the outbox entry's coil bits, motor R */
   uint8_t OutValid;                /**< the outbox holds an entry that Stepper has not accepted yet */
   uint32_t OutHold;                /**< the outbox entry's hold, Timer1 counts */
} Move_t;
/* All of it in ONE struct on purpose: reached through a pointer each byte costs ldd/std (2 bytes),
 * through a static's address lds/sts (4); the generator touches these dozens of times. */

/** One planned move (@req{REQ-CoreLR-D-Motion-0013}, @req{REQ-CoreLR-D-Motion-0023}). Only the end is stored for a line: its start is the
 *  previous block's end. An arc also carries its centre, its length (saturated: the planner only needs
 *  it up to ~2 m) and its speed ceiling. */
typedef struct
{
   Motion_Position_t Target;  /**< the block's end, drawing frame, exact (not grid-rounded) */
   Motion_Position_t Center;  /**< arcs only: the centre, drawing frame */
   uint16_t Len16;            /**< arcs only: length in um, saturating */
   uint8_t EntryV;            /**< planned entry speed, units of 0.25 mm/s */
   uint8_t Cap;               /**< speed ceiling: VMAX, or the centripetal limit */
   uint8_t Flags;             /**< PLAN_ARC, PLAN_CW */
} PlannerBlock_t;

/** What an arc is, once its end points and centre are known (@req{REQ-CoreLR-D-Motion-0022}). */
typedef struct
{
   uint32_t Rs;         /**< radius at the start, um */
   uint32_t Re;         /**< radius at the end, um */
   uint32_t ThetaS;     /**< angle of the start, turns Q32 */
   uint32_t Sweep;      /**< angle swept, turns Q32 (a full turn is 0xFFFFFFFF) */
   uint32_t Len;        /**< length of the arc, um */
} ArcGeo_t;

/**
 * Forward declarations for every file-local helper, right after the
 * macros/types -- confirmed 2026-09-07 as a standing convention:
 * function bodies can then be ordered however reads best.
 */
static uint8_t ComputeCoilBits(int32_t stepCount, uint8_t reversed);
static int32_t QuantizeToSteps(int32_t lengthUm);
static int32_t IdealSubSteps(int32_t lengthUm);
static uint32_t ISqrt32(uint32_t value);
static void IdealAt(Motion_Position_t p, int32_t *outSubL, int32_t *outSubR);
static void SeedCommandedSteps(void);
/* Junction look-ahead helpers (REQ-CoreLR-D-Motion-0013). */
static uint32_t BlockLength(Motion_Position_t from, Motion_Position_t to, int32_t *outDx, int32_t *outDy);
static uint32_t JunctionVelocity(int16_t uxA, int16_t uyA, int16_t uxB, int16_t uyB);
static void UnitDir(int32_t dx, int32_t dy, int16_t *outUx, int16_t *outUy);
static uint32_t ReachableV(uint32_t fromV, uint32_t lenUm);
static void Replan(void);
static void StartMove(const PlannerBlock_t *blk, uint8_t entryV, uint8_t exitV);
static void BeginTimeline(void);
static uint32_t Spread(uint16_t *acc, uint16_t rem, uint16_t n, uint32_t q);
static uint32_t BlockShape(const PlannerBlock_t *blk, Motion_Position_t from, int16_t *dir);
static uint8_t ArcSolve(const Motion_Position_t *from, const PlannerBlock_t *blk, ArcGeo_t *g, uint8_t full);
static uint32_t CrossTime(int32_t c, int32_t s0, int32_t s1, uint32_t duration);
static void BeginSegment(void);
static void EndSegment(void);
static uint8_t SendOutbox(void);
static void FlushPending(void);

/** The last completed waypoint, drawing frame: what Motion_GetComputedPosition() returns, and the start of the move being generated. */
static Motion_Position_t s_currentPosition;

/**
 * 1 once an entry of the move being generated has been enqueued, until the
 * generator reports idle or a halt ramp completes. While it is set, finding
 * the step timer idle means the queue ran dry mid-move -- a starvation
 * (@req{REQ-CoreLR-D-Stepper-0013}). The ISR cannot tell that from the normal end
 * of a move, so the producer counts it.
 */
#if CORELR_DEBUG_MISS_COUNT
static uint8_t s_primed;
#endif

/**
 * Step counts commanded to each motor: the only per-motor state that survives
 * a segment, and what the position store persists (@req{REQ-CoreLR-D-Calibration-0006}).
 * The coil bits of every entry are derived from them, so the ISR keeps none.
 */

/** The ideal cable length, in 1/256 step, at the last completed waypoint.
 *  Valid only while s_move.SubValid: re-derived from the position after a seed. */

static Move_t s_move;

/**
 * The timeline of the entries, in Timer1 counts, all differences (so wrap-around
 * is harmless). An entry's hold is the distance to the NEXT event, so the entry
 * for an event is held back one event -- the "pending" one -- until that
 * distance is known; at the end of a move it is flushed with its hold running
 * to the end of the timeline.
 */

/** The one-entry outbox: an entry Stepper refused, to be offered again. */

/**
 * Junction look-ahead planner (@req{REQ-CoreLR-D-Motion-0013}).
 *
 * THE PROBLEM. The trapezoidal profile is a function of position
 * *within one segment*, so every segment used to start and finish at
 * VMIN. Two collinear moves therefore stopped in between, and a
 * drawing made of short chords -- an SVG-derived file, or any future
 * arc decomposition -- never came near VMAX:
 *
 *   without look-ahead                with look-ahead
 *   v                                 v
 *   |  /\    /\    /\                 |    ______________
 *   | /  \  /  \  /  \                |   /              \ 
 *   |/    \/    \/    \               |  /                \ 
 *   +-------------------- t           +--------------------- t
 *    seg1  seg2  seg3                  seg1  seg2  seg3
 *    stops at every joint              one ramp for the whole run
 *
 * THE BUFFER. Moves are parsed ahead of execution and held here as
 * absolute targets. For a line only the target is stored: its length and
 * direction are derived from the previous block's target, and the first
 * block's predecessor is the segment being executed (or the current
 * position when idle). An arc also carries its centre, its length and its
 * speed ceiling (@req{REQ-CoreLR-D-Motion-0023}). 21 bytes a block.
 *
 *   s_currentPosition / s_segment.SegmentTarget
 *          |
 *          v
 *        [ B0 ]---[ B1 ]---[ B2 ]--- ... ---[ Bn-1 ]
 *          ^                                    ^
 *      s_planHead                       s_planHead + s_planCount - 1
 *      (next to execute)                (most recently parsed)
 *
 * THE PLAN. One reverse pass, from the newest block back to the next
 * one to execute. Each block's entry speed is the lowest of three
 * ceilings:
 *
 *   1. the corner it turns through          JunctionVelocity()
 *   2. the machine limit                    VMAX
 *   3. what can still be shed before the
 *      following block's own entry speed    sqrt(vNext^2 + 2*a*len)
 *
 * Ceiling 3 is what makes one pass sufficient and is why the pass runs
 * backwards: a sharp corner far ahead propagates its low speed back
 * through as many earlier blocks as it takes to brake in time.
 *
 *   B0        B1        B2        B3 (sharp corner ahead)
 *   |         |         |         |
 *   v=10 <--- v=10 <--- v=7 <---- v=2
 *        (3)       (3)       (1)
 *
 * A forward pass is not needed: ComputeHoldTicks() already takes the
 * minimum of the acceleration and deceleration terms at every step, so
 * a block that cannot actually reach its planned entry speed is
 * naturally clamped as it executes.
 *
 * THE HORIZON, and its cost. Sustaining VMAX requires the buffer to
 * span VMAX^2/(2a) = 1.2mm (21.5 mm/s, 192 mm/s^2). At MOTION_PLANNER_DEPTH = 8 (serial)
 * that holds for segments of 0.2mm and longer, at 4 (SD) from 0.5mm; below it the achievable speed falls off
 * as sqrt(depth * length) -- see MOTION_PLANNER_DEPTH's comment for the
 * measured table. This is a RAM ceiling, not an algorithmic one.
 */

static PlannerBlock_t s_plan[MOTION_PLANNER_DEPTH];
/** Index in s_plan of the next block to execute (the ring's head). */
static uint8_t s_planHead;
/** Blocks waiting in s_plan: 0 .. MOTION_PLANNER_DEPTH. */
static uint8_t s_planCount;

/** Set by Motion_Halt(), cleared by Motion_Resume(): stops the planner
 *  handing a new block to the generator while paused. Without it a
 *  segment finishing just as the halt lands would start the next one,
 *  and Motion_IsHaltRampDone() would never report the pause complete. */
static uint8_t s_planHalted;


void Motion_Init(void)
{
   s_currentPosition.X = 0;
   s_currentPosition.Y = 0;
   s_move.Moving = 0;
   s_move.InSegment = 0;
   s_move.HaltSeg = -1;
   s_move.EntryV = 0;
   s_move.ExitV = 0;
   s_move.CurV = 0;
   s_move.RampStartQ8 = 0;
   s_planHead = 0;
   s_planCount = 0;
   s_planHalted = 0;
#if CORELR_DEBUG_MISS_COUNT
   s_primed = 0;
#endif
   s_move.CmdL = 0;
   s_move.CmdR = 0;
   s_move.SubValid = 0;
   s_move.HavePending = 0;
   s_move.OutValid = 0;
   s_move.TSeg = 0;
   s_move.PendTime = 0;
   s_move.LastEvent = 0;
   s_move.AnyEvent = 0;

   /* Restore the position persisted at the last standstill, if it is
    * known (REQ-CoreLR-D-Calibration-0007). Without this the machine
    * believes it is calibrated while holding position (0,0) and zero
    * commanded steps -- and the first move then walks both motors towards the
    * real ideal, whatever the stick does (found on the bench 2026-10-06). */
   Calibration_RestoreStoredPosition();
}

Motion_Position_t Motion_GetComputedPosition(void)
{
   return s_currentPosition;
}

Motion_Position_t Motion_GetTargetPosition(void)
{
   if (s_planCount > 0u)
   {
      return s_plan[(uint8_t)((s_planHead + s_planCount - 1u) & (MOTION_PLANNER_DEPTH - 1u))].Target;
   }
   return s_move.Moving ? s_move.Target : s_currentPosition;
}

void Motion_RequestLinearMove(Motion_Position_t target)
{
   /* A standalone move, planner bypassed: starts and finishes at rest. Ignored
    * if a move is already being generated -- callers wanting to chain moves use
    * Motion_QueueMove(), which is what the jog does since 2026-10-06
    * (REQ-CoreLR-D-LcdJoystickUI-0017). */
   if (!s_move.Moving && !s_move.OutValid)
   {
      PlannerBlock_t blk;

      blk.Target = target;
      blk.Cap = (uint8_t)MOTION_VMAX_V16;
      blk.Flags = 0;
      StartMove(&blk, 0, 0);
   }
}

/** The next free slot of the planner ring, or 0 if it is full. */
static PlannerBlock_t *PlanSlot(void)
{
   if (s_planCount >= (uint8_t)MOTION_PLANNER_DEPTH)
   {
      return 0;
   }
   return &s_plan[(uint8_t)((s_planHead + s_planCount) & (MOTION_PLANNER_DEPTH - 1u))];
}

/** Take the block just filled into the plan. Appending changes what every earlier block may enter at --
 *  a corner that has just become known can force earlier blocks to brake sooner
 *  (@req{REQ-CoreLR-D-Motion-0013}). */
static void PlanCommit(PlannerBlock_t *blk)
{
   blk->EntryV = (uint8_t)MOTION_VMIN_V16;
   s_planCount++;
   Replan();
}

uint8_t Motion_QueueMove(Motion_Position_t target)
{
   PlannerBlock_t *blk = PlanSlot();

   if (blk == 0)
   {
      return 0; /* buffer full: caller retries (GCODEPARSER_BUSY) */
   }
   blk->Target = target;
   blk->Cap = (uint8_t)MOTION_VMAX_V16;
   blk->Flags = 0;
   PlanCommit(blk);
   return 1;
}

uint8_t Motion_PlannerIsEmpty(void)
{
   return (uint8_t)(s_planCount == 0u);
}

uint8_t Motion_PlannerHasRoom(void)
{
   return (uint8_t)(s_planCount < (uint8_t)MOTION_PLANNER_DEPTH);
}

#if CORELR_DEBUG_JOBSTATE
uint8_t Motion_DebugFlags(void)
{
   MOTION_MOVE_PTR(m);
   return (uint8_t)((m->Moving ? 1u : 0u) | (m->InSegment ? 2u : 0u) | ((m->HaltSeg >= 0) ? 4u : 0u) | (s_planHalted ? 8u : 0u)
                    | (m->OutValid ? 16u : 0u) | (m->HavePending ? 32u : 0u));
}

uint16_t Motion_DebugSegment(uint8_t which)
{
   MOTION_MOVE_PTR(m);
   return which ? (uint16_t)m->N : (uint16_t)m->K;
}
#endif

uint8_t Motion_PlannerFree(void)
{
   return (uint8_t)((uint8_t)MOTION_PLANNER_DEPTH - s_planCount);
}

void Motion_Halt(void)
{
   MOTION_MOVE_PTR(m);
   uint32_t rampUm;
   uint32_t segments;
   uint16_t firstFree;

   /* Set first, and unconditionally: even with nothing moving right now
    * this must stop the planner handing over the next block. */
   s_planHalted = 1;

   if (!m->Moving || (m->HaltSeg >= 0))
   {
      return; /* nothing moving, or already ramping down */
   }

   /* Ramp length from the current speed, by the same relation the profile
    * itself uses: d = v^2 / (a * 32 / 1000) um. At VMAX this is ~1.2 mm,
    * i.e. about six segments. The segment in flight completes first, at the
    * speed it was planned for; the ramp starts at the segment after it. */
   rampUm = (((uint32_t)m->CurV * m->CurV) << 10) / MOTION_V2_Q10;
   segments = ((rampUm << 8) + m->SegLenQ8 - 1u) / m->SegLenQ8;
   if (segments < 1u)
   {
      segments = 1u; /* always leave one segment, so the halt point is reachable */
   }
   firstFree = (uint16_t)(m->K + (m->InSegment ? 1u : 0u));
   m->HaltSeg = (int16_t)(((uint32_t)firstFree + segments > m->N) ? m->N : (firstFree + segments));
}

void Motion_Resume(void)
{
   MOTION_MOVE_PTR(m);
   s_planHalted = 0;

   if (!m->Moving || (m->HaltSeg < 0))
   {
      return;
   }
   /* Accelerate from here, from rest, rather than from wherever the original
    * entry speed would put us: the speed rises from VMIN through the normal ramp. */
   m->RampStartQ8 = m->SQ8 + (m->InSegment ? m->SegLenQ8 : 0u);
   m->EntryV = 0;
   m->HaltSeg = -1;
   BeginTimeline();   /* the timeline the pause flushed starts afresh, as a move does from rest */
}

CORELR_NOINLINE
uint8_t Motion_IsHaltRampDone(void)
{
   MOTION_MOVE_PTR(m);
   if (m->OutValid || m->HavePending)
   {
      return 0; /* the last entries are not queued yet: the ramp is not complete */
   }
   if (!m->Moving)
   {
      return 1; /* nothing to ramp down -- blocks may still be planned, awaiting resume */
   }
   return (uint8_t)((m->HaltSeg >= 0) && !m->InSegment && (m->K >= (uint16_t)m->HaltSeg));
}

void Motion_Abort(void)
{
   MOTION_MOVE_PTR(m);
   /* Drops the move in flight. s_currentPosition is NOT touched: it is the last
    * completed waypoint, and the commanded counts are those of the last emitted
    * event, which agree to within a step. What is lost is the remainder of the move.
    * The pending entry is NOT lost: its coil change is already in the commanded
    * count, so it goes to the outbox and is still queued -- dropping it would leave
    * the motors one step short of where Motion believes them to be. */
   if (m->HavePending)
   {
      FlushPending();
   }
   m->Moving = 0;
   m->InSegment = 0;
   m->HaltSeg = -1;
   m->RampStartQ8 = 0;
   m->ExitV = 0;
   /* The job is over, so the moves parsed ahead of it are void too --
    * dropping them is what makes a cancel a cancel rather than a pause
    * (REQ-CoreLR-F-Actuation-0002). */
   s_planCount = 0;
   s_planHalted = 0;
}

CORELR_NOINLINE
uint8_t Motion_IsIdle(void)
{
   MOTION_MOVE_PTR(m);
   return (uint8_t)(!m->Moving && !m->OutValid && !m->HavePending);
}

#if CORELR_MATH == CORELR_MATH_64
CORELR_NOINLINE
uint32_t Motion_ISqrt(uint64_t value)
{
   /**
    * Integer Newton iteration from above (@req{REQ-CoreLR-D-Motion-0005}), replacing the
    * classic bit-by-bit root (502 bytes of flash, ~7 000 cycles) on 2026-10-06:
    *
    *     x0 = a power of two >= sqrt(value);   x <- (x + value / x) / 2   while it decreases
    *
    * Starting at or above the root, the iteration decreases monotonically and stops
    * exactly at floor(sqrt(value)) -- the standard result, checked here against an exact
    * reference over 409 045 values (edges, every square +-1, random at every magnitude).
    * The seed is within a factor of two of the root, so it converges in about six
    * divisions. It reuses the 64-bit division the firmware already links for other
    * reasons, which is what makes it the smaller one.
    *
    * (History, kept so it is not re-tried: `static` locals in the old version did not
    * shrink its 40-byte frame; a float root was rejected -- AVR emulates float in
    * software, and it would break the no-float convention of this pipeline.)
    */
   uint64_t x;
   uint64_t y;
   uint32_t word;
   uint8_t bits;

   if (value == 0u)
   {
      return 0u;
   }
   /* bits = the length of value in bits, found on 32-bit words (cheap on AVR) */
   word = (uint32_t)(value >> 32);
   bits = 32u;
   if (word == 0u)
   {
      word = (uint32_t)value;
      bits = 0u;
   }
   while (word != 0u)
   {
      bits++;
      word >>= 1;
   }
   x = (uint64_t)1 << ((uint8_t)(bits + 1u) >> 1);   /* 2^ceil(bits/2) >= sqrt(value) */

   for (;;)
   {
      y = (x + value / x) >> 1;
      if (y >= x)
      {
         return (uint32_t)x;
      }
      x = y;
   }
}
#endif /* CORELR_MATH_64 */

/** @brief v squared, as 32 x 32 -> 64 on the absolute value. */
#if CORELR_MATH == CORELR_MATH_64

static uint64_t Square32(int32_t v)
{
   uint32_t a = (uint32_t)((v < 0) ? -v : v);

   return (uint64_t)a * a;
}

/** sqrt(x^2 + y^2), exact (floor), for any int32 x and y: one copy of the widening squares, the 64-bit
 *  add and the root, shared by the inverse kinematics, the length of a line and the radii of an arc.
 *  Out of line on purpose: inlined at each of five sites it cost ~600 bytes of flash. */
CORELR_NOINLINE
uint32_t Motion_Hypot(int32_t x, int32_t y)
{
   return Motion_ISqrt(Square32(x) + Square32(y));
}

#else /* CORELR_MATH_48 */

/** a^2 + b^2 on 32 bits: the low word of a sum of squares, or the whole of it when the operands are small. */
CORELR_NOINLINE
static uint32_t SumSq(uint32_t a, uint32_t b)
{
   return a * a + b * b;
}

/**
 * One Newton step from r0 <= sqrt(v), then the downward fix-up, given v mod 2^32 ONLY (@req{REQ-CoreLR-D-Motion-0024}).
 * Newton's step from below never lands under floor(sqrt(v)) (AM-GM), and overshoots by under 2 for the shifts
 * used here (s <= 7), so only "too high" needs a loop. The comparison v - r1^2 < 0 is made mod 2^32: the true
 * difference is far smaller than 2^31, so its sign is that of the 32-bit result.
 */
CORELR_NOINLINE
static uint32_t Refine(uint32_t r0, uint32_t v)
{
   uint32_t r1 = r0 + (v - r0 * r0) / (r0 << 1);

   while ((int32_t)(v - r1 * r1) < 0)
   {
      r1--;
   }
   return r1;
}

/**
 * sqrt(x^2 + y^2), exact (floor), with NO value wider than 32 bits (@req{REQ-CoreLR-D-Motion-0024}).
 *
 * With s the shift that brings both coordinates under 2^15, the root of the SHIFTED coordinates' sum of squares
 * (which fits 32 bits), shifted back, is the root to s bits short: r0 <= r < r0 + 2.5 * 2^s. The residual
 * v - r0^2 is then below 2^(s + 24.8) <= 2^32 for s <= 7 -- i.e. for coordinates under 2^22 -- so it is known
 * exactly from the LOW 32 bits of v alone, which is plain 32-bit arithmetic wrapping. Then Refine().
 *
 * Coordinates beyond CORELR_MATH_48_LIMIT_UM are saturated first: the loops below terminate only inside the
 * bound, and a wrong length is the better failure than a hang.
 */
CORELR_NOINLINE
uint32_t Motion_Hypot(int32_t x, int32_t y)
{
   uint32_t ax = (uint32_t)((x < 0) ? -x : x);
   uint32_t ay = (uint32_t)((y < 0) ? -y : y);
   uint32_t xs, ys, r0;
   uint8_t s = 0;

   if (ax > (uint32_t)CORELR_MATH_48_LIMIT_UM) { ax = (uint32_t)CORELR_MATH_48_LIMIT_UM; }
   if (ay > (uint32_t)CORELR_MATH_48_LIMIT_UM) { ay = (uint32_t)CORELR_MATH_48_LIMIT_UM; }
   xs = ax;
   ys = ay;
   while ((xs | ys) >= 0x8000UL)
   {
      xs >>= 1;
      ys >>= 1;
      s++;
   }
   r0 = ISqrt32(SumSq(xs, ys));
   if (s == 0u)
   {
      return r0;                       /* the sum of squares fitted 32 bits: already exact */
   }
   while (s-- != 0u)
   {
      r0 <<= 1;
   }
   return Refine(r0, SumSq(ax, ay));
}

#endif /* CORELR_MATH */

Motion_CableLengths_t Motion_XYToLR(Motion_Position_t position)
{
   /**
    * The only function in the project that knows the internal
    * kinematics frame and the drawing-origin offset exist
    * (@req{REQ-CoreLR-D-Motion-0005}). Every caller passes/receives
    * drawing-frame positions; this function alone translates into the
    * internal frame (origin at Motor L, not mechanically reachable,
    * never shown to the user) before computing distances. The translation
    * is X_int = X + offset, Y_int = offset - Y (CORELR_Y_UP, the default; offset + Y
    * without it): the drawing frame has Y UP the wall, the internal one has it down
    * (@req{REQ-CoreLR-D-Motion-0025}).
    */
   /**
    * @note `static` on these locals was tried and reverted -- see
    * Motion_ISqrt()'s note. Measured no change to this
    * function's own frame either (the compiler needs the same 64-bit
    * arithmetic scratch space regardless), so it would only have cost
    * RAM permanently for zero stack-depth benefit.
    */
   Calibration_KinematicsParams_t params = Calibration_GetKinematicsParams(); /* narrow getter: this runs at
      least once per waypoint, no need to copy all 7 fields of Calibration_Params_t for the 3 used here */
   int32_t internalX = position.X + params.WorkOriginOffsetX;
   int32_t internalY = MOTION_Y_TO_INTERNAL(params.WorkOriginOffsetY, position.Y); /* the Y flip: with CORELR_Y_UP drawing +Y is UP the wall, internal +Y is down (-0025) */
   int32_t dxR = internalX - params.MotorDistanceUm;
   Motion_CableLengths_t result;

   /* Each square is 32 x 32 -> 64 on the absolute value: no 64 x 64 multiply (158 bytes of
    * library) and no signed overflow, whatever the inputs. */
   result.L = (int32_t)Motion_Hypot(internalX, internalY);
   result.R = (int32_t)Motion_Hypot(dxR, internalY);
   return result;
}

/**
 * Cable length in micrometres to motor steps scaled by MOTION_SUBSTEP.
 * Sub-step resolution is what lets a step be placed to a fraction of a step
 * without floating point.
 */
CORELR_NOINLINE
static int32_t IdealSubSteps(int32_t lengthUm)
{
   /* length * (steps_per_rev * 256 / circumference), the factor as a 2^24 fixed-point constant:
    * a 32 x 32 -> 64 multiply and a shift, in place of a 64-bit division (~1 500 cycles and the
    * library code for it). Within 1/256 step of the exact rounded quotient over 0..4 m, tested. */
   return (int32_t)(((uint64_t)(uint32_t)lengthUm * MOTION_SUB_PER_UM_Q24 + (1UL << 23)) >> 24);
}

/**
 * Integer square root of a 32-bit value, for the velocity profile, whose
 * arguments (squares of speeds in units of 0.25 mm/s) are far below 2^32.
 * Measured on the cycle simulator: 615 cycles, against 5 488 for
 * Motion_ISqrt(uint64_t) on the same input.
 */
CORELR_NOINLINE
static uint32_t ISqrt32(uint32_t value)
{
   uint32_t remainder = value;
   uint32_t root = 0;
   uint32_t bit = (uint32_t)1 << 30;

   while (bit > remainder)
   {
      bit >>= 2;
   }
   while (bit != 0)
   {
      if (remainder >= root + bit)
      {
         remainder -= root + bit;
         root = (root >> 1) + bit;
      }
      else
      {
         root >>= 1;
      }
      bit >>= 2;
   }
   return root;
}

/** The ideal cable lengths at a position, in 1/256 step. One place, shared by
 *  StartMove() and BeginSegment(): written out in each, the by-value structs of
 *  Motion_XYToLR() were copied to and from the stack twice over. */
CORELR_NOINLINE
static void IdealAt(Motion_Position_t p, int32_t *outSubL, int32_t *outSubR)
{
   Motion_CableLengths_t lr = Motion_XYToLR(p);

   *outSubL = IdealSubSteps(lr.L);
   *outSubR = IdealSubSteps(lr.R);
}

/**
 * Re-seed the commanded counts from the current position, for the one case
 * where the machine's position changes without the motors moving:
 * Motion_SetCurrentPosition() declaring where calibration has just
 * established the head to be.
 */
static void SeedCommandedSteps(void)
{
   Motion_CableLengths_t lr = Motion_XYToLR(s_currentPosition);

   s_move.CmdL = QuantizeToSteps(lr.L);
   s_move.CmdR = QuantizeToSteps(lr.R);
}

/** A cable length (>= 0) in whole motor steps, rounded to the nearest: the ideal length in 1/256 steps (IdealSubSteps()) plus half a step, shifted down. */
static int32_t QuantizeToSteps(int32_t lengthUm)
{
   return (IdealSubSteps(lengthUm) + MOTION_SUBSTEP / 2) >> 8; /* non-negative: a shift, not a signed division */
}

/** @brief Coil bits for one motor at an absolute step count. Phase is
 * derived from the step count via modulo 4 -- not tracked as separate
 * persistent state -- so it can never desynchronize from position
 * (@req{REQ-CoreLR-D-Motion-0007}). Exposed publicly as
 * Motion_StepToCoilBits() so LcdJoystickUI's Adjustment jog
 * mode (raw per-motor stepping, no Cartesian kinematics involved) can
 * reuse the same table instead of duplicating it. */
static uint8_t ComputeCoilBits(int32_t stepCount, uint8_t reversed)
{
   /* The low two bits of a two's-complement integer ARE its value modulo 4,
    * negative or not (-1 & 3 == 3): no 32-bit signed remainder, no fix-up for
    * negatives. Inlined at four sites, so it matters (REQ-CoreLR-D-Motion-0007). */
   uint8_t effective = (uint8_t)(reversed ? -stepCount : stepCount);

   return pgm_read_byte(&kPhaseTable[effective & 3u]);
}

uint8_t Motion_StepToCoilBits(int32_t stepCount, uint8_t reversed)
{
   return ComputeCoilBits(stepCount, reversed);
}

void Motion_SetCurrentPosition(Motion_Position_t position)
{
   /**
    * Direct seed of the tracked position, bypassing path planning --
    * used exactly once per calibration, the moment the user confirms
    * (via the LCD/joystick) that the machine physically sits at the
    * geometric reference position (@req{REQ-CoreLR-D-Calibration-0001}).
    * Before that confirmation, s_currentPosition is meaningless (no
    * calibration parameters exist yet to relate it to real cable
    * lengths), so there is nothing to preserve -- an outright
    * overwrite is correct, not a merge/delta.
    */
   s_currentPosition = position;

   /* The head is declared to be here; the motors did not move to get
    * here. Re-seed the counts, and forget the ideal lengths, so the next move
    * derives them from this position. */
   SeedCommandedSteps();
   s_move.SubValid = 0;
   s_move.HavePending = 0;
   s_move.TSeg = 0;
   s_move.AnyEvent = 0;
}

void Motion_GetPersistedState(int32_t *outStepsL, int32_t *outStepsR,
                              int32_t *outInternalX, int32_t *outInternalY)
{
   int32_t offsetX;
   int32_t offsetY;

   Calibration_GetWorkOrigin(&offsetX, &offsetY);
   *outStepsL = s_move.CmdL;
   *outStepsR = s_move.CmdR;
   *outInternalX = s_currentPosition.X + offsetX;
   *outInternalY = MOTION_Y_TO_INTERNAL(offsetY, s_currentPosition.Y);   /* the flip, as in Motion_XYToLR() */
}

uint8_t Motion_SetOrigin(uint8_t what, int32_t x, int32_t y)
{
   int32_t offsetX;
   int32_t offsetY;
   int32_t internalX;
   int32_t internalY;

   if (!Motion_IsIdle() || !Motion_PlannerIsEmpty())
   {
      return 0;
   }
   Calibration_GetWorkOrigin(&offsetX, &offsetY);
   internalX = s_currentPosition.X + offsetX;                          /* where the pen is, in the internal frame */
   internalY = MOTION_Y_TO_INTERNAL(offsetY, s_currentPosition.Y);
   if (what & MOTION_ORIGIN_REFERENCE)
   {
      Calibration_GetReferencePosition(&offsetX, &offsetY);
   }
   else
   {
      if (what & MOTION_ORIGIN_X)
      {
         offsetX = internalX - x;
      }
      if (what & MOTION_ORIGIN_Y)
      {
         offsetY = MOTION_OFFSET_FOR(internalY, y);
      }
   }
   if (!Calibration_SetWorkOrigin(offsetX, offsetY))                   /* stored, or refused: outside the travel zone */
   {
      return 0;
   }
   s_currentPosition.X = internalX - offsetX;                          /* the pen has not moved: the same internal point, read in the new frame */
   s_currentPosition.Y = MOTION_Y_FROM_INTERNAL(offsetY, internalY);
   return 1;
}

#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL
Motion_Position_t Motion_GetMachineOffset(void)
{
   Motion_Position_t offset;
   int32_t offsetX;
   int32_t offsetY;
   int32_t referenceX;
   int32_t referenceY;

   Calibration_GetWorkOrigin(&offsetX, &offsetY);
   Calibration_GetReferencePosition(&referenceX, &referenceY);
   offset.X = offsetX - referenceX;                                   /* -R: R is the reference point read in the drawing frame */
   offset.Y = -MOTION_Y_FROM_INTERNAL(offsetY, referenceY);          /* the one Y flip, as everywhere in this file */
   return offset;
}
#endif

void Motion_Restore(int32_t stepsL, int32_t stepsR, int32_t internalX, int32_t internalY)
{
   /**
    * The internal position is persisted beside the step counts because deriving
    * it from them costs flash: measured twice, 718 bytes with the original 64-bit
    * arithmetic and 696 with the optimised one, to save 8 bytes of EEPROM. Not
    * adopted -- kept as a patch with the numbers: docs/design-notes/experiments/ (2026-10-07).
    */
   int32_t offsetX;
   int32_t offsetY;

   Calibration_GetWorkOrigin(&offsetX, &offsetY);
   s_move.CmdL = stepsL;
   s_move.CmdR = stepsR;
   s_currentPosition.X = internalX - offsetX;
   s_currentPosition.Y = MOTION_Y_FROM_INTERNAL(offsetY, internalY);   /* the flip, as in Motion_XYToLR() */
   s_move.SubValid = 0;
}

/** @brief Length of one planned move and its deltas.
 *
 * Approximate on purpose (alpha-max plus beta-min, +-4 %): the planner calls
 * this for every block on every Replan(), and an exact root costs ~7000 cycles
 * each. The profile of the move actually executed uses the exact length,
 * computed once in StartMove(). */
CORELR_NOINLINE
static uint32_t BlockLength(Motion_Position_t from, Motion_Position_t to, int32_t *outDx, int32_t *outDy)
{
   int32_t dx = to.X - from.X;
   int32_t dy = to.Y - from.Y;
   uint32_t a = (uint32_t)((dx < 0) ? -dx : dx);
   uint32_t b = (uint32_t)((dy < 0) ? -dy : dy);
   uint32_t big = (a > b) ? a : b;
   uint32_t small = (a > b) ? b : a;

   *outDx = dx;
   *outDy = dy;
   return (big * 246u + small * 102u) >> 8;   /* 0.961*max + 0.398*min */
}

/**
 * @brief Speed at which the corner between two moves may be taken,
 * um/tick (@req{REQ-CoreLR-D-Motion-0013}). Axis-jerk model.
 *
 * Each move's direction is expressed as a fixed-point unit vector
 * normalised by its *Manhattan* length, so |ux| + |uy| = 256. That is
 * the natural normalisation here rather than a mathematical nicety:
 * ux/256 is exactly the fraction of grid steps that advance X, so
 * v * ux/256 is the X component of the pen's velocity.
 *
 * The junction is then limited by requiring that neither component
 * jump by more than the configured jerk:
 *
 *        move A                  move B
 *      ------------->        .
 *                    \      .
 *                     \    .          at the joint, the X and Y
 *                      \  .           velocity components change
 *                       v .           abruptly -- by how much depends
 *                                     only on the two directions
 *
 *   |v*uxA/256 - v*uxB/256| <= JERK    ->  v <= JERK*256 / |uxA-uxB|
 *   |v*uyA/256 - v*uyB/256| <= JERK    ->  v <= JERK*256 / |uyA-uyB|
 *
 * and v is the smaller of the two. Two useful degenerate cases fall
 * out with no special handling:
 *
 *   collinear   uxA == uxB and uyA == uyB, both denominators zero,
 *               so no limit at all -- full speed through the joint.
 *               This is the defect the look-ahead exists to fix.
 *   reversal    uA = -uB, denominators maximal (512), so
 *               v <= JERK/2, i.e. essentially a stop. Correct: the
 *               pen has to turn around.
 *
 * Chosen over grbl's junction-deviation formula, which needs
 * trigonometry and divides by (1 - sin(theta/2)) -- zero exactly at
 * the collinear case that matters most here.
 */
/** @brief Direction as a fixed-point vector normalised by the Manhattan
 *  length, so |ux| + |uy| = 256 (see JunctionVelocity()). Computed once
 *  per block and carried across Replan()'s iterations, since each
 *  direction serves two junctions. */
CORELR_NOINLINE
static void UnitDir(int32_t dx, int32_t dy, int16_t *outUx, int16_t *outUy)
{
   int32_t len = ((dx < 0) ? -dx : dx) + ((dy < 0) ? -dy : dy);

   if (len <= 0)
   {
      *outUx = 0;
      *outUy = 0;
      return;
   }
   *outUx = (int16_t)((dx * 256) / len);
   *outUy = (int16_t)((dy * 256) / len);
}

/**
 * The speed at which the pen may pass the corner between two blocks, units of 0.25 mm/s: the highest speed at which neither axis's velocity jumps by more
 * than MOTION_JUNCTION_JERK_V16 (the jerk limit of each axis, see the table above), within [MOTION_VMIN_V16, MOTION_VMAX_V16]
 * (@req{REQ-CoreLR-D-Motion-0013}).
 * @param uxA,uyA  direction of the block before the corner, at its end (UnitDir(): |ux| + |uy| = 256).
 * @param uxB,uyB  direction of the block after the corner, at its start.
 */
CORELR_NOINLINE
static uint32_t JunctionVelocity(int16_t uxA, int16_t uyA, int16_t uxB, int16_t uyB)
{
   int32_t denomX, denomY;
   uint32_t limit = MOTION_VMAX_V16;

   denomX = uxA - uxB;
   denomY = uyA - uyB;
   if (denomX < 0) { denomX = -denomX; }
   if (denomY < 0) { denomY = -denomY; }

   if (denomX > 0)
   {
      uint32_t vx = ((uint32_t)MOTION_JUNCTION_JERK_V16 * 256u) / (uint32_t)denomX;
      if (vx < limit) { limit = vx; }
   }
   if (denomY > 0)
   {
      uint32_t vy = ((uint32_t)MOTION_JUNCTION_JERK_V16 * 256u) / (uint32_t)denomY;
      if (vy < limit) { limit = vy; }
   }
   if (limit < MOTION_VMIN_V16)
   {
      limit = MOTION_VMIN_V16;
   }
   return limit;
}

/**
 * v0^2 plus what accelerating over the distance (hiQ8 - loQ8) adds, in the units of @req{REQ-CoreLR-D-Motion-0020}:
 * the distance in um (Q8 in, floored at 0 when hi <= lo) is clamped to 2000, since v^2 saturates far below VMAX^2
 * well before that, and gains Q10/1024 per um. The one expression the profile used three times (acceleration, braking,
 * and ReachableV()'s look-ahead), each 60-80 bytes of flash.
 */
CORELR_NOINLINE
static uint32_t V2Gain(uint32_t v0, uint32_t hiQ8, uint32_t loQ8)
{
   uint32_t um = (hiQ8 > loQ8) ? ((hiQ8 - loQ8) >> 8) : 0u;

   if (um > 2000u)
   {
      um = 2000u;
   }
   return v0 * v0 + ((um * MOTION_V2_Q10) >> 10);
}

/** @brief Speed reachable at the end of `lenUm` of travel starting from
 *  `fromV`: v = sqrt(fromV^2 + a * 32 / 1000 * d), in units of 0.25 mm/s
 *  (@req{REQ-CoreLR-D-Motion-0020}). 32-bit: the argument saturates far below 2^32. */
CORELR_NOINLINE
static uint32_t ReachableV(uint32_t fromV, uint32_t lenUm)
{
   if (lenUm > 2000u)
   {
      lenUm = 2000u; /* v^2 saturates far below VMAX^2 well before this */
   }
   return ISqrt32(V2Gain(fromV, lenUm << 8, 0u));
}

/** @brief The direction of travel at a point of an arc from its radius vector (ax, ay), turned a quarter
 *  turn the way the arc goes: (-ay, ax) counter-clockwise, (ay, -ax) clockwise. No trigonometry
 *  (@req{REQ-CoreLR-D-Motion-0023}); UnitDir() normalises by the Manhattan length, which a quarter turn
 *  preserves. */
static void Tangent(int32_t ax, int32_t ay, uint8_t cw, int16_t *outUx, int16_t *outUy)
{
   if (cw)
   {
      UnitDir(ay, -ax, outUx, outUy);
   }
   else
   {
      UnitDir(-ay, ax, outUx, outUy);
   }
}

/**
 * @brief The length of a planned block, and its directions: dir[0..1] at the start, dir[2..3] at the
 * end (equal for a line). The planner needs both because the corner between two blocks joins the
 * END of one to the START of the next -- which for an arc are different.
 */
CORELR_NOINLINE
static uint32_t BlockShape(const PlannerBlock_t *blk, Motion_Position_t from, int16_t *dir)
{
   if (blk->Flags & PLAN_ARC)
   {
      uint8_t cw = (uint8_t)((blk->Flags & PLAN_CW) != 0u);

      Tangent(from.X - blk->Center.X, from.Y - blk->Center.Y, cw, &dir[0], &dir[1]);
      Tangent(blk->Target.X - blk->Center.X, blk->Target.Y - blk->Center.Y, cw, &dir[2], &dir[3]);
      return blk->Len16;
   }
   else
   {
      int32_t dx, dy;
      uint32_t len = BlockLength(from, blk->Target, &dx, &dy);

      UnitDir(dx, dy, &dir[0], &dir[1]);
      dir[2] = dir[0];
      dir[3] = dir[1];
      return len;
   }
}

/**
 * @brief Recompute every planned entry speed, newest block backwards
 * (@req{REQ-CoreLR-D-Motion-0013}). Called whenever the buffer changes.
 *
 * Also refreshes the executing segment's ExitV, which is by definition
 * the first planned block's entry speed -- that is the one number that
 * lets the segment in flight decelerate to a junction instead of to a
 * standstill.
 */
static void Replan(void)
{
   uint8_t k;
   uint8_t idx;
   uint32_t nextEntry = 0; /* nothing is planned beyond the last block, so it must be able to stop */
   int16_t nextUx = 0, nextUy = 0;
   uint8_t haveNext = 0;
   uint8_t nextIdx = 0;   /* buffer slot of the block after the one being processed */

   /* Walk backwards from the newest block. Each block's geometry comes
    * from its predecessor's target; the predecessor of block 0 is the
    * move being executed, or the current position when idle.
    *
    * Indices wrap with a bitmask. MOTION_PLANNER_DEPTH is required to
    * be a power of two for exactly this reason (checked at compile
    * time where it is defined): "% DEPTH" would be a real 16-bit
    * division, and this loop performs several per block. Measured on
    * the way here -- modulo, then comparison-wrapping, then the mask:
    * 428 bytes of flash for the first step, 38 more for the second. */
   idx = (uint8_t)((s_planHead + s_planCount - 1u) & (MOTION_PLANNER_DEPTH - 1u));

   for (k = s_planCount; k > 0u; k--)
   {
      Motion_Position_t from;
      int16_t dir[4];
      uint32_t len;
      uint32_t entry;
      uint8_t cap = s_plan[idx].Cap;

      if (k >= 2u)
      {
         uint8_t prevIdx = (uint8_t)((idx - 1u) & (MOTION_PLANNER_DEPTH - 1u));

         from = s_plan[prevIdx].Target;
      }
      else
      {
         from = s_move.Moving ? s_move.Target : s_currentPosition;
      }
      len = BlockShape(&s_plan[idx], from, dir);

      /*
       * The corner between THIS block and the next one limits the speed
       * *at the joint*, which is this block's exit and simultaneously
       * the next block's entry -- so it caps the entry already computed
       * for the next block, not this one's.
       *
       *        block k            block k+1
       *   ...------------>*------------------>
       *                   ^
       *                   the joint: exit of k == entry of k+1
       *
       * Getting this off by one was a real bug, caught by the host test
       * (2026-09-10): a right-angle corner and even a full reversal
       * were being taken at VMAX, because the limit landed on the entry
       * of the block *before* the corner.
       *
       * Lowering the next block's entry here is safe in this backwards
       * walk, and necessary: `nextEntry` feeds the braking constraint
       * below, and a block can only ever need to brake *harder*. Blocks
       * beyond the next one are unaffected -- each depends on its own
       * successor, not on this joint.
       */
      if (haveNext)
      {
         /* The joint joins the END of this block to the START of the next. And it is this block's exit speed,
          * so an arc's centripetal ceiling bounds it too (REQ-CoreLR-D-Motion-0023). */
         uint32_t corner = JunctionVelocity(dir[2], dir[3], nextUx, nextUy);

         if (cap < corner) { corner = cap; }
         if (corner < nextEntry)
         {
            nextEntry = corner;
            s_plan[nextIdx].EntryV = (uint8_t)corner;
         }
      }

      /* Ceiling: what can still be shed before the following block's
       * entry speed. This is what propagates a distant sharp corner
       * back through earlier blocks. */
      entry = ReachableV(nextEntry, len);
      if (entry > cap)
      {
         entry = cap;
      }
      if (entry < MOTION_VMIN_V16)
      {
         entry = MOTION_VMIN_V16;
      }
      s_plan[idx].EntryV = (uint8_t)entry;

      nextEntry = entry;
      nextUx = dir[0];
      nextUy = dir[1];
      nextIdx = idx;
      haveNext = 1;
      idx = (uint8_t)((idx - 1u) & (MOTION_PLANNER_DEPTH - 1u));
   }

   /* The executing segment's exit is the first planned block's entry,
    * further limited by the corner between the two. Its own direction
    * needs no recomputation: the generator already holds it as
    * Ux/Uy (the direction at its END, for an arc).
    * Nothing planned means the segment
    * must come to rest, which is the pre-look-ahead behaviour. */
   if (s_planCount > 0u)
   {
      uint32_t exitV = s_plan[s_planHead].EntryV;

      if (s_move.Moving)
      {
         uint32_t corner = JunctionVelocity(s_move.Ux, s_move.Uy, nextUx, nextUy);

         if (s_move.Cap < corner) { corner = s_move.Cap; }
         if (corner < exitV) { exitV = corner; }
         if (exitV < MOTION_VMIN_V16) { exitV = MOTION_VMIN_V16; }
         s_plan[s_planHead].EntryV = (uint8_t)exitV;
      }
      s_move.ExitV = (uint8_t)exitV;
   }
   else
   {
      s_move.ExitV = (uint8_t)MOTION_VMIN_V16;
   }
}


/** Cable-length crossing of a decision threshold inside a segment: the first
 *  instant, in Timer1 counts from the segment's start, at which a motor whose
 *  ideal position runs linearly from s0 to s1 over `duration` reaches the point
 *  that makes it step; MOTION_NO_EVENT if it does not within the segment. */
static uint32_t CrossTime(int32_t c, int32_t s0, int32_t s1, uint32_t duration)
{
   int32_t ds = s1 - s0;
   int32_t thr;
   int32_t num;

   if (ds > 0)
   {
      thr = (int32_t)(c * MOTION_SUBSTEP) + MOTION_SUBSTEP / 2 + MOTION_TIE_GUARD_SUBSTEPS;
      if (s1 < thr) { return MOTION_NO_EVENT; }
      num = thr - s0;
   }
   else if (ds < 0)
   {
      thr = (int32_t)(c * MOTION_SUBSTEP) - MOTION_SUBSTEP / 2 - MOTION_TIE_GUARD_SUBSTEPS;
      if (s1 > thr) { return MOTION_NO_EVENT; }
      num = s0 - thr;
      ds = -ds;
   }
   else
   {
      return MOTION_NO_EVENT;
   }
   if (num <= 0) { return 0; }
   return ((uint32_t)num * duration + (uint32_t)ds - 1u) / (uint32_t)ds;   /* ceil: first instant at or past it */
}

/**
 * Start a fresh timeline from rest: the machine holds the phase of the commanded counts, and the first entry holds it until the first step.
 * Used by StartMove() and by Motion_Resume() -- both begin generating from a machine at rest. Resume did not, until 2026-10-10: FlushPending()
 * leaves PendTime and LastEvent from the old timeline and HavePending clear, so the first event after a pause computed
 * hold = TSeg + t - PendTime with a stale PendTime: about -850 000 counts, i.e. a hold of 36 minutes (bench: Run, but frozen), and the move's last
 * entry was never flushed.
 */
CORELR_NOINLINE
static void BeginTimeline(void)
{
   MOTION_MOVE_PTR(m);
   m->PendCoilL = ComputeCoilBits(m->CmdL, STEPPER_MOTOR_L_REVERSED);
   m->PendCoilR = ComputeCoilBits(m->CmdR, STEPPER_MOTOR_R_REVERSED);
   m->PendTime = 0;
   m->TSeg = 0;
   m->LastEvent = 0;
   m->AnyEvent = 0;
   m->HavePending = 1;
}

/**
 * Set up a move: its length, its waypoints, its speeds (@req{REQ-CoreLR-D-Motion-0019}). Leaves Moving at 0 for a zero-length move.
 * From rest (no pending entry) the timeline restarts and the entry speed is forced to zero, whatever the planner says; otherwise the move
 * continues the timeline of the one before it.
 * @param blk     the planned block: its end, and for an arc its centre, length and flags.
 * @param entryV  the speed at its start, units of 0.25 mm/s (the planner's, unless from rest).
 * @param exitV   the speed at its end, same unit: the next block's entry speed, 0 if none follows.
 */
static void StartMove(const PlannerBlock_t *blk, uint8_t entryV, uint8_t exitV)
{
   MOTION_MOVE_PTR(m);
   Motion_Position_t target = blk->Target;
   int32_t dx = target.X - s_currentPosition.X;
   int32_t dy = target.Y - s_currentPosition.Y;
   uint32_t absDx = (uint32_t)((dx < 0) ? -dx : dx);
   uint32_t absDy = (uint32_t)((dy < 0) ? -dy : dy);
   uint32_t n;
   ArcGeo_t g;
   uint8_t arc = 0;

   if (blk->Flags & PLAN_ARC)
   {
      arc = ArcSolve(&s_currentPosition, blk, &g, 1);
   }
   m->Arc = arc;
   m->Len = arc ? g.Len : Motion_Hypot(dx, dy);
   if (m->Len == 0u)
   {
      /* Nothing to move, but still snap the logical position exactly
       * (REQ-CoreLR-D-Motion-0004). */
      s_currentPosition = target;
      m->SubValid = 0;
      return;
   }
   if (!m->SubValid)
   {
      IdealAt(s_currentPosition, &m->SubL, &m->SubR);
      m->SubValid = 1;
   }
   if (!m->HavePending)
   {
      BeginTimeline();
      /* ... and from rest the speed is zero, whatever the planner says. Its reverse pass
       * knows what a block can brake from, not whether the machine is actually moving: left
       * alone, the first block after a stop would start at its planned entry speed, up to
       * VMAX, from standstill. (Found while writing the host test for this port; v1 had
       * the same latent defect for planned moves, and never met it because the jog bypassed
       * the planner.) */
      entryV = 0;
   }

   n = (m->Len + MOTION_WAYPOINT_UM - 1u) / MOTION_WAYPOINT_UM;
   if (arc)
   {
      /* Cut finer where the chord tolerance asks for it: segments of sqrt(8 * tol * r) at most
       * (REQ-CoreLR-D-Motion-0022). */
      uint32_t chord = ISqrt32(8UL * MOTION_ARC_TOLERANCE_UM * g.Rs);
      uint32_t n2 = (m->Len + chord - 1u) / chord;
      uint32_t dr;

      if (n2 > n) { n = n2; }
      m->Cw = (uint8_t)((blk->Flags & PLAN_CW) != 0u);
      m->Center = blk->Center;
      m->Theta = g.ThetaS;
      m->DTh = g.Sweep / n;
      m->RemTh = (uint16_t)(g.Sweep % n);
      m->AccTh = (uint16_t)(n / 2u);
      dr = (g.Re > g.Rs) ? (g.Re - g.Rs) : (g.Rs - g.Re);
      m->RSign = (g.Re > g.Rs) ? 1 : -1;
      m->Rad = g.Rs;
      m->DRq = (uint16_t)(dr / n);
      m->RemR = (uint16_t)(dr % n);
      m->AccR = (uint16_t)(n / 2u);
   }
   m->N = (uint16_t)n;
   m->K = 0;
   m->SignX = (dx < 0) ? -1 : 1;
   m->SignY = (dy < 0) ? -1 : 1;
   m->Qx = (uint16_t)(absDx / n); m->Rx = (uint16_t)(absDx % n); m->AccX = (uint16_t)(n / 2u);
   m->Qy = (uint16_t)(absDy / n); m->Ry = (uint16_t)(absDy % n); m->AccY = (uint16_t)(n / 2u);
   m->SegLenQ8 = (uint16_t)((m->Len * 256UL) / n);
   m->SQ8 = 0;
   m->RampStartQ8 = 0;
   m->HaltSeg = -1;
   m->Target = target;
   m->EntryV = entryV;
   m->ExitV = exitV;
   m->Cap = blk->Cap;
   m->CurV = (entryV > (uint8_t)MOTION_VMIN_V16) ? entryV : (uint8_t)MOTION_VMIN_V16;
   if (arc)
   {
      /* the direction at the END, which is what the junction with the next block needs */
      Tangent(target.X - m->Center.X, target.Y - m->Center.Y, m->Cw, &m->Ux, &m->Uy);
   }
   else
   {
      UnitDir(dx, dy, &m->Ux, &m->Uy);
   }
   m->InSegment = 0;
   m->Moving = 1;
}

/** Set up the next segment: its end waypoint, its duration from the speed at
 *  its middle, and each motor's first crossing (@req{REQ-CoreLR-D-Motion-0018}). */
CORELR_NOINLINE
static void BeginSegment(void)
{
   MOTION_MOVE_PTR(m);
   uint32_t sMidQ8, endQ8, v2a, v2d, v2, v;
   uint8_t exitV;
   Motion_Position_t p;

   /* waypoint k+1, exactly on the line, by incremental proportional rounding */
   if ((uint16_t)(m->K + 1u) == m->N)
   {
      p = m->Target;                               /* the last one is exact, not rounded */
   }
   else if (m->Arc)
   {
      /* waypoint k+1 of the arc, from its own definition (REQ-CoreLR-D-Motion-0022): the radius blended
       * linearly start -> end, the angle advanced by its share of the sweep */
      uint32_t dth = Spread(&m->AccTh, m->RemTh, m->N, m->DTh);
      uint32_t dr = Spread(&m->AccR, m->RemR, m->N, m->DRq);
      int32_t cx, cy;

      m->Theta = m->Cw ? (m->Theta - dth) : (m->Theta + dth);
      m->Rad = (uint32_t)((int32_t)m->Rad + m->RSign * (int32_t)dr);
      Trig_Rotate(m->Rad, m->Theta, &cx, &cy);
      p.X = m->Center.X + cx;
      p.Y = m->Center.Y + cy;
   }
   else
   {
      uint32_t ix = m->Qx;
      uint32_t iy = m->Qy;

      m->AccX = (uint16_t)(m->AccX + m->Rx); if (m->AccX >= m->N) { m->AccX = (uint16_t)(m->AccX - m->N); ix++; }
      m->AccY = (uint16_t)(m->AccY + m->Ry); if (m->AccY >= m->N) { m->AccY = (uint16_t)(m->AccY - m->N); iy++; }
      p.X = s_currentPosition.X + m->SignX * (int32_t)ix;
      p.Y = s_currentPosition.Y + m->SignY * (int32_t)iy;
   }
   m->NextPos = p;
   IdealAt(p, &m->SL1, &m->SR1);

   /* Speed at the middle of the segment (REQ-CoreLR-D-Motion-0020): the lower of
    * what acceleration allows from the entry (or the resume point) and what
    * braking to the exit -- or to a halt's virtual end, always at rest -- allows. */
   sMidQ8 = m->SQ8 + (m->SegLenQ8 >> 1);
   endQ8 = m->Len << 8;
   exitV = m->ExitV;
   if (m->HaltSeg >= 0)
   {
      uint32_t haltQ8 = (uint32_t)m->HaltSeg * m->SegLenQ8;

      if (haltQ8 < endQ8) { endQ8 = haltQ8; }
      exitV = 0;
   }
   v2a = V2Gain(m->EntryV, sMidQ8, m->RampStartQ8);
   v2d = V2Gain(exitV, endQ8, sMidQ8);
   v2 = (v2a < v2d) ? v2a : v2d;
   v = ISqrt32(v2);
   if (v > m->Cap) { v = m->Cap; }
   if (v < MOTION_VMIN_V16) { v = MOTION_VMIN_V16; }
   m->CurV = (uint8_t)v;                           /* remembered so Motion_Halt() can size its ramp */

   /* T = d * counts_per_s / (v * 250) = d * 8000 / v, with d carried in Q8 */
   m->T = (((uint32_t)m->SegLenQ8 * MOTION_COUNTS_PER_UM_UNIT) / v + 128u) >> 8;
   if (m->T < 1u) { m->T = 1u; }

   m->TL = CrossTime(m->CmdL, m->SubL, m->SL1, m->T);
   m->TR = CrossTime(m->CmdR, m->SubR, m->SR1, m->T);
   m->InSegment = 1;
}

/** The segment in flight is finished: the timeline and the distance advance by its duration and length, its end waypoint becomes the current position,
 *  and the move is over (Moving cleared) after the last one. */
CORELR_NOINLINE
static void EndSegment(void)
{
   MOTION_MOVE_PTR(m);
   m->TSeg += m->T;
   m->SQ8 += m->SegLenQ8;
   s_currentPosition = m->NextPos;
   m->SubL = m->SL1;
   m->SubR = m->SR1;
   m->K++;
   m->InSegment = 0;
   if (m->K == m->N)
   {
      m->Moving = 0;
   }
}

/** Offer the outbox to Stepper. @return 1 if it was accepted. */
static uint8_t SendOutbox(void)
{
   MOTION_MOVE_PTR(m);
   if (!Stepper_RequestMotion(m->OutCoilL, m->OutCoilR, m->OutHold, STEPPER_PEN_NONE))
   {
      return 0;
   }
   m->OutValid = 0;
#if CORELR_DEBUG_MISS_COUNT
   s_primed = 1; /* an entry of this move is now in the pipeline: if the timer goes idle, that is a starvation */
#endif
   return 1;
}

/** Move the pending entry to the outbox, held to the end of the timeline, and
 *  start a fresh timeline. */
static void FlushPending(void)
{
   MOTION_MOVE_PTR(m);
   m->OutCoilL = m->PendCoilL;
   m->OutCoilR = m->PendCoilR;
   /* "Held to the end of the timeline" -- but TSeg is the START of the segment in flight, so when a move is aborted mid-segment (InSegment) the
    * last event is *after* it and the difference is negative: a hold of 36 minutes once unsigned (found on the host, 2026-10-10). The ramp-complete
    * flush is at a segment boundary and never meets it; Motion_Abort() can be called anywhere. A hold of 0 is raised to the ISR's minimum by Stepper. */
   m->OutHold = (m->TSeg > m->PendTime) ? (m->TSeg - m->PendTime) : 0u;
   m->OutValid = 1;
   m->HavePending = 0;
   m->TSeg = 0;
   m->AnyEvent = 0;
}

/** @return the magnitude of v. Out of line: inlined at seven sites it cost ~136 bytes of flash. */
CORELR_NOINLINE
static uint32_t Mag(int32_t v)
{
   return (uint32_t)((v < 0) ? -v : v);
}

/**
 * Work out an arc from its block and its start point (@req{REQ-CoreLR-D-Motion-0022}): the two radii always, and
 * with `full` also the angles, the sweep and the length.
 *
 * @return 0 if the arc cannot be drawn: a vector or a radius reaching 2^21 um (2.1 m, the machine's reach --
 * nothing more is computed then; that bound keeps the squares, Trig's 2^22 limit and the 16-bit segment count
 * safe, as one OR of the four magnitudes); and, only with `full`, a radius under one motor step or no sweep
 * at all, which the caller does as a straight move to the end point.
 *
 * One function, by pointer, for the check, the queueing and the start of the move: passing three
 * `Motion_Position_t` by value to a helper cost more flash than the arithmetic did.
 */
CORELR_NOINLINE
static uint8_t ArcSolve(const Motion_Position_t *from, const PlannerBlock_t *blk, ArcGeo_t *g, uint8_t full)
{
   int32_t ax = from->X - blk->Center.X;
   int32_t ay = from->Y - blk->Center.Y;
   int32_t bx = blk->Target.X - blk->Center.X;
   int32_t by = blk->Target.Y - blk->Center.Y;

   if ((Mag(ax) | Mag(ay) | Mag(bx) | Mag(by)) >> MOTION_ARC_REACH_SHIFT)
   {
      return 0;
   }
   g->Rs = Motion_Hypot(ax, ay);
   g->Re = Motion_Hypot(bx, by);
   if (((g->Rs | g->Re) >> MOTION_ARC_REACH_SHIFT) != 0u)
   {
      return 0;
   }
   if (!full)
   {
      return 1;
   }
   if ((g->Rs < MOTION_STEP_UM) || (g->Re < MOTION_STEP_UM))
   {
      return 0;
   }
   g->ThetaS = Trig_Angle(ax, ay);
   if (memcmp(from, &blk->Target, sizeof *from) == 0)    /* the two coordinates are adjacent int32s, no padding */
   {
      g->Sweep = 0xFFFFFFFFUL;                 /* end == start exactly: a full circle */
   }
   else
   {
      uint32_t thetaE = Trig_Angle(bx, by);

      g->Sweep = (blk->Flags & PLAN_CW) ? (g->ThetaS - thetaE) : (thetaE - g->ThetaS);
      if (g->Sweep == 0u)
      {
         return 0;                              /* the end is radially in line with the start: a straight move */
      }
   }
   g->Len = Trig_ArcLength((g->Rs + g->Re) >> 1, g->Sweep);
   return 1;
}

/** Is `y` beyond the top margin (the travel zone has no other limit in Y)? */
#if CORELR_Y_UP
#define MOTION_Y_BEYOND(y, limit) ((y) > (limit))
#else
#define MOTION_Y_BEYOND(y, limit) ((y) < (limit))
#endif

/**
 * The travel zone (@req{REQ-CoreLR-D-Motion-0028}), in the drawing frame: X between the side margins of the motors' verticals, Y not beyond the top margin (with
 * CORELR_Y_UP: not above). One function for the test and the clamp, so the limits are worked out once, in one place (two helpers cost 300 bytes of flash).
 *
 * @param p      the position to test, drawing frame; moved into the zone when `clamp` is 1.
 * @param clamp  0: only test. 1: also move `*p` to the nearest point of the zone, axis by axis.
 * @return with clamp 0: 1 if `*p` is inside. With clamp 1: 1 if the zone is usable (`*p` is inside it afterwards), 0 if it is empty (`*p` unchanged).
 * Without a known geometry (no motor distance: not calibrated, so no move can be running) there is no zone to apply, and the answer is 1.
 * The zone is empty when the motor distance is under twice the side margin.
 */
CORELR_NOINLINE
static uint8_t TravelCheck(Motion_Position_t *p, uint8_t clamp)
{
   Calibration_KinematicsParams_t k = Calibration_GetKinematicsParams();
   int32_t xMin = MOTION_MARGIN_SIDE_UM - k.WorkOriginOffsetX;
   int32_t xMax = k.MotorDistanceUm - MOTION_MARGIN_SIDE_UM - k.WorkOriginOffsetX;
#if CORELR_Y_UP
   int32_t yLimit = k.WorkOriginOffsetY - MOTION_MARGIN_TOP_UM;
#else
   int32_t yLimit = MOTION_MARGIN_TOP_UM - k.WorkOriginOffsetY;
#endif

   if (k.MotorDistanceUm <= 0)
   {
      return 1;
   }
   if (clamp)
   {
      if (xMin > xMax)
      {
         return 0;
      }
      if (p->X < xMin) { p->X = xMin; }
      if (p->X > xMax) { p->X = xMax; }
      if (MOTION_Y_BEYOND(p->Y, yLimit)) { p->Y = yLimit; }
      return 1;
   }
   return (uint8_t)((p->X >= xMin) && (p->X <= xMax) && !MOTION_Y_BEYOND(p->Y, yLimit));
}

uint8_t Motion_TravelAllows(const Motion_Position_t *p)
{
   return TravelCheck((Motion_Position_t *)p, 0);       /* clamp 0 never writes through it */
}

uint8_t Motion_TravelClamp(Motion_Position_t *p)
{
   return TravelCheck(p, 1);
}

uint8_t Motion_QueueJog(Motion_Position_t target)
{
   Motion_Position_t end = Motion_GetTargetPosition();

   if (!Motion_TravelClamp(&target) || ((target.X == end.X) && (target.Y == end.Y)))
   {
      return 1;               /* nowhere to go, or nowhere further to go: nothing to queue, nothing to retry (no zero-length block) */
   }
   return Motion_QueueMove(target);
}

/**
 * Does the arc, drawn from `start`, leave the travel zone? Both end points are tested by the caller; what is left is the bulge: the four extreme points of
 * the circle (centre +-r along X and along Y) that lie ON the arc. Exact, in the unsigned 32-bit angle arithmetic of Trig_Angle() (turns Q32, counter-clockwise
 * from +X, so the extremes are at 0, 2^30, 2^31 and 3 * 2^30): the extreme at angle e is on the arc iff its distance from the start, in the arc's direction,
 * is within the sweep. @req{REQ-CoreLR-D-Motion-0028}.
 */
CORELR_NOINLINE
static uint8_t ArcLeavesZone(const Motion_Position_t *centre, uint32_t r, const ArcGeo_t *g, uint8_t clockwise)
{
   uint8_t k;
   uint32_t e = 0;                                 /* the extreme's angle: 0, 2^30, 2^31, 3 * 2^30 */

   for (k = 0; k < 4u; k++, e += 0x40000000UL)
   {
      uint32_t offset = clockwise ? (g->ThetaS - e) : (e - g->ThetaS);

      if (offset <= g->Sweep)
      {
         Motion_Position_t p = *centre;
         int32_t d = (k & 2u) ? -(int32_t)r : (int32_t)r;       /* k = 0, 1: +r; 2, 3: -r */

         if (k & 1u)
         {
            p.Y += d;
         }
         else
         {
            p.X += d;
         }
         if (!Motion_TravelAllows(&p))
         {
            return 1;
         }
      }
   }
   return 0;
}

CORELR_NOINLINE
uint8_t Motion_ArcCheck(const Motion_Position_t *target, int32_t i, int32_t j, uint8_t clockwise)
{
   Motion_Position_t start = Motion_GetTargetPosition();
   PlannerBlock_t blk;
   ArcGeo_t g;
   uint32_t d;

   if ((Mag(i) | Mag(j)) >> MOTION_ARC_REACH_SHIFT)
   {
      return 0;             /* also what keeps start + (i, j) from wrapping */
   }
   blk.Target = *target;
   blk.Center.X = start.X + i;
   blk.Center.Y = start.Y + j;
   if (!ArcSolve(&start, &blk, &g, 0))
   {
      return 0;
   }
   /* grbl's rule (REQ-CoreLR-F-GCodeInterpreter-0009): up to 5 um always; beyond, refused above 0.5 mm
    * or above 0.1 % of the radius. */
   d = (g.Rs > g.Re) ? (g.Rs - g.Re) : (g.Re - g.Rs);
   if (d > MOTION_ARC_RADIUS_FLOOR_UM)
   {
      if ((d > MOTION_ARC_RADIUS_CAP_UM) || ((d * MOTION_ARC_RADIUS_RATIO) > g.Rs))
      {
         return 0;
      }
   }
   /* The travel zone (REQ-CoreLR-D-Motion-0028): an arc lies wholly inside it or is refused -- the two ends, then the bulge. */
   if (!Motion_TravelAllows(&start) || !Motion_TravelAllows(target))
   {
      return 0;
   }
   blk.Flags = clockwise ? PLAN_CW : 0u;
   if (ArcSolve(&start, &blk, &g, 1) && ArcLeavesZone(&blk.Center, (g.Rs > g.Re) ? g.Rs : g.Re, &g, clockwise))
   {
      return 0;
   }
   return 1;
}

/** The speed ceiling of an arc of radius r: the centripetal limit v^2 <= a*r, in the units of
 *  MOTION_V2_Q10 (@req{REQ-CoreLR-D-Motion-0023}), between VMIN and VMAX. */
static uint8_t ArcCap(uint32_t r)
{
   uint32_t v;

   if (r >= MOTION_ARC_FREE_RADIUS_UM)
   {
      return (uint8_t)MOTION_VMAX_V16;
   }
   v = ISqrt32((r * MOTION_V2_Q10) >> 11);
   if (v < MOTION_VMIN_V16) { v = MOTION_VMIN_V16; }
   if (v > MOTION_VMAX_V16) { v = MOTION_VMAX_V16; }
   return (uint8_t)v;
}

CORELR_NOINLINE
uint8_t Motion_QueueArc(const Motion_Position_t *target, int32_t i, int32_t j, uint8_t clockwise)
{
   PlannerBlock_t *blk = PlanSlot();
   Motion_Position_t start;
   ArcGeo_t g;

   if (blk == 0)
   {
      return 0;
   }
   start = Motion_GetTargetPosition();
   blk->Target = *target;
   blk->Center.X = start.X + i;
   blk->Center.Y = start.Y + j;
   blk->Flags = (uint8_t)(PLAN_ARC | (clockwise ? PLAN_CW : 0u));
   blk->Cap = (uint8_t)MOTION_VMAX_V16;
   /* The geometry is worked out once here, for the planner's sake (length, ceiling) and again when the
    * block is executed, rather than carried: 16 bytes a block of RAM for ~10 ms of CPU in all. An
    * arc this machine cannot draw (the caller should have refused it with Motion_ArcCheck()) is
    * queued as the straight move it degenerates to, never as a malformed block. */
   if (ArcSolve(&start, blk, &g, 1))
   {
      blk->Len16 = (g.Len > 0xFFFFUL) ? 0xFFFFu : (uint16_t)g.Len;
      blk->Cap = ArcCap((g.Rs < g.Re) ? g.Rs : g.Re);
   }
   else
   {
      blk->Flags = 0;
   }
   PlanCommit(blk);
   return 1;
}

/** Add `step` to `*acc` the way the line interpolation does: the quotient `q`, plus one when the
 *  accumulated remainder reaches `n`. @return the step to take. */
static uint32_t Spread(uint16_t *acc, uint16_t rem, uint16_t n, uint32_t q)
{
   uint32_t a = (uint32_t)*acc + rem;

   if (a >= n)
   {
      a -= n;
      q++;
   }
   *acc = (uint16_t)a;
   return q;
}

/**
 * One step of one motor: its commanded count moves a count towards where the segment is going, and the next instant it
 * crosses a decision threshold is worked out. The two motors' versions were written out twice, 92 bytes of flash each;
 * this is the one copy.
 */
CORELR_NOINLINE
static uint32_t StepMotor(Move_t *m, uint8_t isL)
{
   int32_t *cmd = isL ? &m->CmdL : &m->CmdR;
   int32_t s0 = isL ? m->SubL : m->SubR;
   int32_t s1 = isL ? m->SL1 : m->SR1;

   *cmd += (s1 > s0) ? 1 : -1;
   return CrossTime(*cmd, s0, s1, m->T);
}

/**
 * One unit of the generator's work (@req{REQ-CoreLR-D-Motion-0014}): offer the outbox to Stepper; else take the next planned block, finish a halt ramp,
 * begin or end a segment, or compute the next step event of the segment (the earlier crossing of the two motors, kept the minimum delay after the last
 * one) and queue the entry it completes. Nothing is generated while an entry is refused, so the generator never runs ahead of the queue.
 * @return 1 when there is nothing to do (no move, no block, no pending entry); 0 otherwise, including a paused move (a paused job has not finished).
 * CORELR_NOINLINE: keeps this out of main()'s permanent frame -- see SwConfig.h.
 */
CORELR_NOINLINE
static uint8_t MotionStepOnce(void)
{
   MOTION_MOVE_PTR(m);
   uint32_t tFirst;
   uint8_t firstIsL;

   /* An entry Stepper refused last time goes first: nothing is generated
    * until it is queued, so the generator never runs ahead of the queue. */
   if (m->OutValid)
   {
      (void)SendOutbox();
      return 0;
   }

   if (!m->Moving)
   {
      /* Take the next planned move, unless paused. This is the only place a
       * planned block becomes a move, so the entry speed the reverse pass
       * computed is honoured exactly once (REQ-CoreLR-D-Motion-0013). The
       * timeline carries straight on into it: no entry is flushed between
       * chained moves. */
      if ((s_planCount > 0u) && !s_planHalted)
      {
         const PlannerBlock_t *blk = &s_plan[s_planHead];
         uint8_t entryV = blk->EntryV;
         uint8_t exitV;

         s_planHead = (uint8_t)((s_planHead + 1u) & (MOTION_PLANNER_DEPTH - 1u));
         s_planCount--;
         exitV = (s_planCount > 0u) ? s_plan[s_planHead].EntryV : 0u;
         StartMove(blk, entryV, exitV);
         Replan(); /* block 0's predecessor is now this move, so its geometry changed */
         return 0;
      }
      if (m->HavePending)
      {
         FlushPending(); /* nothing follows: the last entry is held to the end of the timeline */
         (void)SendOutbox();
         return 0;
      }
#if CORELR_DEBUG_MISS_COUNT
      s_primed = 0;
#endif
      return 1;
   }

   /* Halt (pause) ramp complete: the move stays Moving so that a resume
    * continues this very move, but nothing more is generated. The last entry is
    * flushed so the machine really stops, and the timeline starts afresh on resume.
    * Reported as *not* idle -- a paused job has not finished, and the caller must
    * not be told it can hand over the next GCode line (REQ-CoreLR-D-Motion-0012). */
   if (!m->InSegment)
   {
      if ((m->HaltSeg >= 0) && (m->K >= (uint16_t)m->HaltSeg))
      {
         if (m->HavePending)
         {
            FlushPending();
            (void)SendOutbox();
         }
#if CORELR_DEBUG_MISS_COUNT
         s_primed = 0; /* a paused machine running its queue dry is not a starvation */
#endif
         return 0;
      }
      if (m->K >= m->N)
      {
         m->Moving = 0;
         return 0;
      }
      BeginSegment();
   }

   /* The next step event of this segment: the earlier of the two motors' crossings.
    * Events are never closer together than the minimum delay the ISR can honour:
    * a later one is delayed to `previous + MIN` (REQ-CoreLR-D-Motion-0018, rule 3;
    * delay chosen over merging on the prototype: no entry below the minimum, 0 in
    * 7.7 M, against 161). One pushed past the segment's end waits for the next
    * segment, where it comes out at time 0, clamped the same way. */
   if (m->AnyEvent)
   {
      int32_t floorT = (int32_t)((m->LastEvent + STEPPER_DELAY_MIN_COUNTS) - m->TSeg);

      if (floorT > 0)
      {
         if ((m->TL != MOTION_NO_EVENT) && ((int32_t)m->TL < floorT)) { m->TL = (uint32_t)floorT; }
         if ((m->TR != MOTION_NO_EVENT) && ((int32_t)m->TR < floorT)) { m->TR = (uint32_t)floorT; }
      }
   }
   if ((m->TL != MOTION_NO_EVENT) && (m->TL > m->T)) { m->TL = MOTION_NO_EVENT; }
   if ((m->TR != MOTION_NO_EVENT) && (m->TR > m->T)) { m->TR = MOTION_NO_EVENT; }

   if ((m->TL == MOTION_NO_EVENT) && (m->TR == MOTION_NO_EVENT))
   {
      EndSegment();
      return 0;
   }

   firstIsL = (uint8_t)(m->TL <= m->TR);
   {
      uint32_t next = StepMotor(m, firstIsL);

      if (firstIsL)
      {
         tFirst = m->TL;
         m->TL = next;
      }
      else
      {
         tFirst = m->TR;
         m->TR = next;
      }
   }

   /* The pending entry's hold is the distance to this event; this event's bits become pending. */
   {
      uint32_t tAbs = m->TSeg + tFirst;

      m->OutCoilL = m->PendCoilL;
      m->OutCoilR = m->PendCoilR;
      m->OutHold = tAbs - m->PendTime;
      m->OutValid = 1;
      m->PendCoilL = ComputeCoilBits(m->CmdL, STEPPER_MOTOR_L_REVERSED);
      m->PendCoilR = ComputeCoilBits(m->CmdR, STEPPER_MOTOR_R_REVERSED);
      m->PendTime = tAbs;
      m->LastEvent = tAbs;
      m->AnyEvent = 1;
   }
   (void)SendOutbox();
   return 0;
}

/**
 * Advance the move in flight, keeping the step queue deep
 * (@req{REQ-CoreLR-D-Motion-0014}).
 *
 * One call used to emit at most one queue entry, which made the
 * achievable step rate a function of how often loop() happened to run
 * and let any slow pass starve the ISR -- the same defect found first
 * in the manual jog, where a frozen coil pattern was mistaken for the
 * motor losing torque.
 *
 * Plotting takes the opposite policy to the jog: fill the queue rather
 * than meter it. There is no stick to release here, so queued work is
 * never work we might regret -- a halt ramps down through the planner
 * and those queued entries are the ramp itself.
 *
 * Two bounds stop this from monopolising the pass: the queue depth
 * target, and a hard cap on iterations per call so the UI keeps its
 * cadence. An iteration is a step event, a segment boundary or a flush,
 * not a waypoint: a segment costs ~25 000 cycles (1.6 ms) and an event ~2 200.
 */
CORELR_NOINLINE
uint8_t Motion_Task(void)
{
   uint8_t idle;
   uint8_t emitted = 0u;

   do
   {
      idle = MotionStepOnce();
      emitted++;
   }
   while ((idle == 0u)
          && (Stepper_QueuedCount() < (uint8_t)MOTION_QUEUE_TARGET)
          && (emitted < (uint8_t)MOTION_STEPS_PER_TASK_MAX));

#if CORELR_DEBUG_MISS_COUNT
   if (idle == 0u)
   {
      if (s_primed && Stepper_IsIdle())
      {
         Stepper_CountStarvation(); /* one count per episode: the next enqueue re-primes */
         s_primed = 0;
      }
   }
#endif

   return idle;
}
