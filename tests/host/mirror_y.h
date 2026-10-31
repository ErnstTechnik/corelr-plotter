/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* mirror_y.h -- run a host test of Motion in the MIRROR IMAGE of its own world, to prove the Y flip changed nothing else.
 *
 * REQ-CoreLR-D-Motion-0025 flips Y once, in Motion_XYToLR() (drawing +Y is up the wall). A scenario written for the old frame
 * (+Y down) must therefore produce, on the new firmware, the IDENTICAL stream of entries sent to Stepper if every position it
 * gives is mirrored (Y -> -Y), every arc offset J -> -J, and every arc direction reversed (a mirror turns clockwise into
 * counter-clockwise), and everything it reads back is mirrored again. This header does exactly that, with macros that wrap the
 * drawing-frame API of Motion; the test itself is untouched. Compiled in only with -DMIRROR_Y.
 *
 *   tools/bench/stream_hash.sh -C MIRROR_Y        old firmware on the plain tests, new firmware on the mirrored ones: same hash?
 *
 * Include it AFTER Motion.h (test_motion) or after #include "Motion.c" (test_arc): the wrappers call the real functions, and
 * the macros are defined last so that they affect the test's calls only. Motion_Restore / Motion_GetPersistedState are NOT
 * wrapped: they carry the INTERNAL frame, which the flip does not change (the stored position stays valid across it).
 * Checks of a mirrored run that read the firmware's internals (static state of Motion.c) see the new frame and may fail:
 * only the hash is meaningful there.
 */
#ifdef MIRROR_Y
static Motion_Position_t mirror_(Motion_Position_t p) { p.Y = -p.Y; return p; }
static uint8_t mir_QueueMove(Motion_Position_t t) { return Motion_QueueMove(mirror_(t)); }
static void mir_RequestLinearMove(Motion_Position_t t) { Motion_RequestLinearMove(mirror_(t)); }
static uint8_t mir_ArcCheck(const Motion_Position_t *t, int32_t i, int32_t j, uint8_t cw) { Motion_Position_t m = mirror_(*t); return Motion_ArcCheck(&m, i, -j, (uint8_t)!cw); }
static uint8_t mir_QueueArc(const Motion_Position_t *t, int32_t i, int32_t j, uint8_t cw) { Motion_Position_t m = mirror_(*t); return Motion_QueueArc(&m, i, -j, (uint8_t)!cw); }
static Motion_Position_t mir_GetComputedPosition(void) { return mirror_(Motion_GetComputedPosition()); }
static Motion_Position_t mir_GetTargetPosition(void) { return mirror_(Motion_GetTargetPosition()); }
static void mir_SetCurrentPosition(Motion_Position_t p) { Motion_SetCurrentPosition(mirror_(p)); }
static Motion_CableLengths_t mir_XYToLR(Motion_Position_t p) { return Motion_XYToLR(mirror_(p)); }
#define Motion_QueueMove mir_QueueMove
#define Motion_RequestLinearMove mir_RequestLinearMove
#define Motion_ArcCheck mir_ArcCheck
#define Motion_QueueArc mir_QueueArc
#define Motion_GetComputedPosition mir_GetComputedPosition
#define Motion_GetTargetPosition mir_GetTargetPosition
#define Motion_SetCurrentPosition mir_SetCurrentPosition
#define Motion_XYToLR mir_XYToLR
#endif
