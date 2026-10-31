/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * evgen.h -- PROTOTYPE of the v2 step-event generator
 * (REQ-CoreLR-D-Motion-0018 .. -0020). Not part of the firmware build:
 * it exists to be measured on the host and on the cycle simulator before
 * its logic is moved into Motion.c.
 *
 * Integer only, no float, at most 32x32->64 products, so the same file
 * compiles for the host and for the ATmega328P.
 */
#ifndef EVGEN_H
#define EVGEN_H

#include <stdint.h>
#include "Motion.h"

/** One queue entry: apply these coil bits, then hold for `Hold` Timer1 counts (0.5 us). */
typedef struct
{
   uint8_t CoilL;
   uint8_t CoilR;
   uint32_t Hold;
} EvGen_Entry_t;

/** Diagnostic counters, host-side inspection only. */
typedef struct
{
   uint32_t Events;      /**< step events emitted (one or two motor steps each) */
   uint32_t Merged;      /**< events where both motors stepped inside the minimum delay */
   uint32_t Segments;
   uint32_t ShortHolds;  /**< entries emitted with a hold below the minimum delay */
} EvGen_Stats_t;

/** Seed with the commanded step counts and the matching drawing-frame position. */
void EvGen_Init(int32_t stepsL, int32_t stepsR, Motion_Position_t position);

/** Begin a straight move. Speeds in units of 0.25 mm/s (the "V16" unit). */
void EvGen_StartMove(Motion_Position_t target, uint8_t entryV, uint8_t exitV);

/** @return 1 and the next entry, or 0 when this move has no more events. */
uint8_t EvGen_Next(EvGen_Entry_t *out);

/** After the last move: emit the entry still pending, held to the end of the timeline. */
uint8_t EvGen_Flush(EvGen_Entry_t *out);

void EvGen_GetCommandedSteps(int32_t *stepsL, int32_t *stepsR);
const EvGen_Stats_t *EvGen_GetStats(void);

#endif
