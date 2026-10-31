/*
 * Calibration.c
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
 * @file Calibration.c
 * @brief See Calibration.h.
 *
 * Storage technology (@req{REQ-CoreLR-D-Calibration-0003}): the
 * ATmega328P's internal EEPROM (avr/eeprom.h, pure C, no extra SRAM --
 * distinct from the SD card, which holds GCode jobs, not configuration).
 */

#include <stddef.h>
#include <avr/eeprom.h>
#include "Calibration.h"
#include "SwConfig.h"
#include "Motion.h"

/**
 * @brief On-disk (EEPROM) image: a magic number (detects "never saved"
 * blank/erased EEPROM, which reads back as 0xFF bytes) plus a checksum
 * (detects partial/corrupted writes), wrapping the actual payload.
 */
#define EEPROM_MAGIC 0x43524C32UL /* ASCII "CRL2", version-tagged. Was "CRL1" until 2026-10-06, when
                                   * IsCalibrated left the image (REQ-CoreLR-D-Calibration-0007): an
                                   * old image is therefore rejected and factory defaults load. */

/**
 * The calibration image, at EEPROM address CALIBRATION_EEPROM_ADDRESS (0), 31 bytes; the first 64 bytes of the EEPROM are the parameters and a reserve
 * (the position ring starts at CALIBRATION_POSITION_EEPROM_ADDRESS, @docref{EepromLayout,docs/protocol/EepromLayout.md}):
 * @verbatim
 *   offset size field
 *      0    4   Magic = 0x43524C32 ("CRL2")
 *      4    4   MotorDistanceUm        (Calibration_Params_t starts here: 26 bytes)
 *      8    4   ReferencePositionX
 *     12    4   ReferencePositionY
 *     16    4   ReferenceCableLengthUm
 *     20    4   WorkOriginOffsetX
 *     24    4   WorkOriginOffsetY
 *     28    1   PenUpAngle
 *     29    1   PenDownAngle
 *     30    1   Checksum               additive sum of bytes 0..29
 * @endverbatim
 * (the offsets are those of the AVR, whose structures are packed with no padding; the host test builds with -fpack-struct=1 to match.)
 */
typedef struct
{
   uint32_t Magic;                  /**< EEPROM_MAGIC: tells a saved image from blank cells or an older format */
   Calibration_Params_t Params;     /**< the parameters, as the structure is in RAM */
   uint8_t Checksum; /**< simple additive checksum over the fields above */
} EepromImage_t;

_Static_assert(sizeof(EepromImage_t) <= (CALIBRATION_POSITION_EEPROM_ADDRESS - CALIBRATION_EEPROM_ADDRESS),
               "the calibration image overruns the position record -- move CALIBRATION_POSITION_EEPROM_ADDRESS");

#if CALIBRATION_POSITION_RING
/**
 * @name The persisted position: a ring of slots
 *
 * Realizes @req{REQ-CoreLR-D-Calibration-0006}, @req{REQ-CoreLR-D-Calibration-0010}; the layout is in @docref{EepromLayout,docs/protocol/EepromLayout.md}. A ring of CALIBRATION_POSITION_SLOTS slots of its own, so that storing a position never rewrites the parameters above, and no EEPROM cell is rewritten more
 * than once in SLOTS/2 motion cycles. It is a **journal**: every change of state APPENDS a record to the oldest slot, and the newest valid record says what
 * the position is.
 *
 * @verbatim
 *   EEPROM (1024 bytes)                       one slot (15 bytes with 24-bit fields, 19 with 32-bit ones)
 *   +-------------------------+ 0              offset  size  field
 *   | calibration image (31)  |                  0      1    Seq     sequence number, 8 bits, modulo 256, the COMMIT byte (written last)
 *   | + reserve               |                  1      1    Flags   bit 7    valid: 1 = a position follows, 0 = "the position is no longer known"
 *   +-------------------------+ 64                                   bits 6..0 magic = POS_MAGIC (0x55), a fixed 7-bit number
 *   | slot 0                  |                                      (erased EEPROM reads 0x7F there: a slot never written is rejected)
 *   | slot 1                  |                  2      3    StepsL  24 bits signed, little endian   (these four: the payload, valid records only)
 *   |  ...   (64 slots)       |                  5      3    StepsR
 *   | slot 63                 |                  8      3    InternalX  (um, internal frame)
 *   +-------------------------+ 1024            11      3    InternalY
 *                                               14      1    CRC-8   poly 0x07, over Seq, Flags and, for a valid record, the payload
 * @endverbatim
 *
 * An invalidation is Seq, Flags (valid = 0) and the CRC only; the slot's payload bytes are stale and are neither written nor read.
 * The newest record is the one with the greatest Seq in serial-number arithmetic: `(seq - best) & 255` in 1..127 means newer. That is exact while the live
 * records span less than 128 numbers, so Seq and the slot index (a mask) wrap freely, and any ring of up to 128 slots works.
 *
 * Write order, the commit being Seq: payload and CRC (one block), Flags, then Seq. A power cut in between leaves a slot rejected by its CRC or -- one chance in 256
 * -- accepted but carrying the OLD Seq of that slot: the oldest of the ring, which cannot beat the newest valid record. The invalidation is complete
 * (eeprom_busy_wait) before any step can follow it.
 * @{
 */
#if CORELR_MATH == CORELR_MATH_64
#define POS_FIELD_BYTES 4u     /**< bytes of one stored field: any int32 can be a position */
#else
#define POS_FIELD_BYTES 3u     /**< bytes of one stored field: 24-bit signed: +-8.38 m, and CORELR_MATH_48 keeps every coordinate and length below 2^22 um (asserted below) */
#endif
#define POS_PAYLOAD_BYTES (4u * POS_FIELD_BYTES)   /**< the payload: StepsL, StepsR, InternalX, InternalY */
#define POS_OFF_SEQ       0u   /**< offset of the sequence number in a slot */
#define POS_OFF_FLAGS     1u   /**< offset of the flags byte */
#define POS_OFF_PAYLOAD   2u   /**< offset of the payload */
#define POS_OFF_CRC       (POS_OFF_PAYLOAD + POS_PAYLOAD_BYTES)   /**< offset of the CRC-8, the last byte of a slot */
#define POS_SLOT_BYTES    (POS_OFF_CRC + 1u)   /**< bytes of one slot */
#define POS_SLOTS         CALIBRATION_POSITION_SLOTS   /**< slots in the ring */
#define POS_FLAG_VALID    0x80u   /**< bit 7 of the flags: 1 = a position follows, 0 = the position is no longer known */
#define POS_MAGIC_MASK    0x7Fu   /**< bits 6..0 of the flags: the magic number */
/** The 7-bit magic number: any value but 0x7F (what erased cells read). It is part of the format: if bits are ever given another meaning, the magic changes with them
 *  (an old ring is then simply rejected -- one recalibration -- or an intermediate firmware erases the EEPROM first). */
#define POS_MAGIC         0x55u
#define POS_FLAGS(valid)  ((uint8_t)(((valid) ? POS_FLAG_VALID : 0u) | POS_MAGIC))   /**< the flags byte of a record, valid or not */

_Static_assert((POS_SLOTS & (POS_SLOTS - 1u)) == 0u, "CALIBRATION_POSITION_SLOTS must be a power of two (the index wraps with a mask)");
_Static_assert(POS_MAGIC <= POS_MAGIC_MASK && POS_MAGIC != POS_MAGIC_MASK, "POS_MAGIC: 7 bits, and not 0x7F, which erased EEPROM reads");
_Static_assert(POS_SLOTS >= 2u && POS_SLOTS <= 128u, "CALIBRATION_POSITION_SLOTS: 2..128 (the 8-bit sequence number tells the newest of at most 128 slots)");
_Static_assert(CALIBRATION_POSITION_EEPROM_ADDRESS + POS_SLOTS * POS_SLOT_BYTES <= 1024u, "the position ring does not fit the ATmega328P's 1024 bytes of EEPROM");
#if CORELR_MATH != CORELR_MATH_64
_Static_assert(CORELR_MATH_48_LIMIT_UM < (1L << 23), "a 48-bit coordinate does not fit a 24-bit stored field");
_Static_assert(LCDJOYSTICKUI_CAL_MAX_UM + (1L << MOTION_REACH_SHIFT) < (1L << 23), "an internal coordinate (an offset up to CAL_MAX plus the reach) does not fit a 24-bit stored field");
#endif
/** @} */

#else  /* the single record of REQ-CoreLR-D-Calibration-0006, for the SD variant: it has no flash to spare for the ring */
/**
 * The persisted position (@req{REQ-CoreLR-D-Calibration-0006}): a record of its
 * own, so that storing it never rewrites the parameters above.
 *
 * The marker is a value that is neither 0x00 (what Invalidate writes) nor
 * 0xFF (what blank EEPROM reads), and it is covered by the checksum, so a
 * marker byte half-written by a power cut cannot pass for a valid one.
 */
#define POSITION_MARKER_VALID   0xA5u
#define POSITION_MARKER_INVALID 0x00u

/**
 * The single record of the SD variant (CALIBRATION_POSITION_RING = 0), 18 bytes at CALIBRATION_POSITION_EEPROM_ADDRESS:
 * @verbatim
 *   offset size field
 *      0    1   Marker     0xA5 valid, 0x00 invalidated, 0xFF erased
 *      1    4   StepsL     int32
 *      5    4   StepsR     int32
 *      9    4   InternalX  int32, um
 *     13    4   InternalY  int32, um
 *     17    1   Checksum   additive, over bytes 0..16
 * @endverbatim
 * No padding on AVR (alignment 1); the host test builds with -fpack-struct=1 to match.
 */
typedef struct
{
   uint8_t Marker;
   int32_t StepsL;
   int32_t StepsR;
   int32_t InternalX; /**< position, internal frame -- stored rather than derived, see Calibration_StorePosition() */
   int32_t InternalY;
   uint8_t Checksum;  /**< additive, over everything above */
} PositionRecord_t;

#define POSITION_ADDR(field) ((void *)(CALIBRATION_POSITION_EEPROM_ADDRESS + offsetof(PositionRecord_t, field)))
#endif

/**
 * Forward declarations for every file-local helper, right after the
 * macros/types -- confirmed 2026-09-07 as a standing convention: function
 * bodies can then be ordered however reads best.
 */
static void LoadFactoryDefaults(void);
static void RecomputeReferencePosition(void);
static uint8_t ComputeChecksum(const EepromImage_t *image);
#if CALIBRATION_POSITION_RING
static uint8_t Crc8(uint8_t seq, uint8_t flags, const uint8_t *data, uint8_t length);
static uint8_t PositionSlotRead(uint8_t slot, uint8_t *outSeq, uint8_t *outFlags, uint8_t *payload);
static void PositionAppend(uint8_t valid, const int32_t *fields);
static uint8_t PositionFits(int32_t value);
#else
static uint8_t PositionChecksum(const PositionRecord_t *record);
#endif
static void Persist(void);

/** The calibration parameters in use: loaded by Calibration_Init(), written back to the EEPROM by Persist(). */
static Calibration_Params_t s_params;
/** 1 once the machine is calibrated: the parameters were valid at start-up and a valid position was found, or the user has just confirmed one; cleared whenever the position is no longer known. Not stored (@req{REQ-CoreLR-D-Calibration-0007}). */
static uint8_t s_isCalibrated = 0;
#if CALIBRATION_POSITION_RING
static uint8_t s_positionValid = 0; /**< the newest record of the ring is a valid position */
static uint8_t s_posHead = 0;       /**< the slot the newest record is in */
static uint8_t s_posSeq = 0;        /**< its sequence number */
#else
static uint8_t s_positionValid = 0; /**< mirrors the EEPROM marker: 1 = valid, 0 = invalid or unknown */
#endif

void Calibration_Init(void)
{
   uint8_t imageValid = 0;

   {
      EepromImage_t image;

      eeprom_read_block(&image, (const void *)CALIBRATION_EEPROM_ADDRESS, sizeof(image));
      if (image.Magic == EEPROM_MAGIC && image.Checksum == ComputeChecksum(&image))
      {
         s_params = image.Params;
         imageValid = 1;
      }
      else
      {
         LoadFactoryDefaults(); /* never saved yet, or corrupted -- fall back safely */
      }
   }

#if CALIBRATION_POSITION_RING
   /* The machine is calibrated iff its position is known, i.e. the NEWEST record of the position ring is a valid position
    * (REQ-CoreLR-D-Calibration-0007, -0010). A power cut during motion leaves the newest record an invalidation, and so costs a recalibration -- the safe
    * failure, by design (REQ-CoreLR-F-Calibration-0007). A blank ring (erased EEPROM) has no record: uncalibrated. */
   {
      uint8_t slot;
      uint8_t have = 0;
      uint8_t bestSeq = 0;

      s_posHead = (uint8_t)(POS_SLOTS - 1u);        /* nothing found: the next record goes to slot 0, sequence 0 */
      s_posSeq = 0xFFu;
      s_positionValid = 0;
      for (slot = 0; slot < (uint8_t)POS_SLOTS; slot++)
      {
         uint8_t seq;
         uint8_t flags;
         uint8_t payload[POS_PAYLOAD_BYTES];

         if (PositionSlotRead(slot, &seq, &flags, payload))
         {
            uint8_t ahead = (uint8_t)(seq - bestSeq);        /* serial-number arithmetic: newer if 1..127 ahead */

            if (!have || ((ahead >= 1u) && (ahead <= 127u)))
            {
               have = 1;
               bestSeq = seq;
               s_posHead = slot;
               s_posSeq = seq;
               s_positionValid = (uint8_t)((flags & POS_FLAG_VALID) != 0u);
            }
         }
      }
   }
#else
   /* The machine is calibrated iff its position is known, i.e. the
    * persisted position is valid (REQ-CoreLR-D-Calibration-0007). A power
    * cut during motion leaves it invalid, and so costs a recalibration --
    * the safe failure, by design (REQ-CoreLR-F-Calibration-0007). */
   {
      PositionRecord_t record;

      eeprom_read_block(&record, (const void *)CALIBRATION_POSITION_EEPROM_ADDRESS, sizeof(record));
      s_positionValid = (uint8_t)((record.Marker == POSITION_MARKER_VALID)
                                  && (record.Checksum == PositionChecksum(&record)));
   }
#endif
   s_isCalibrated = (uint8_t)(imageValid && s_positionValid);
}

void Calibration_GetParams(Calibration_Params_t *outParams)
{
   *outParams = s_params;
}

int32_t Calibration_GetMotorDistanceUm(void)
{
   return s_params.MotorDistanceUm;
}

int32_t Calibration_GetReferenceCableLengthUm(void)
{
   return s_params.ReferenceCableLengthUm;
}

Calibration_KinematicsParams_t Calibration_GetKinematicsParams(void)
{
   Calibration_KinematicsParams_t result;

   result.WorkOriginOffsetX = s_params.WorkOriginOffsetX;
   result.WorkOriginOffsetY = s_params.WorkOriginOffsetY;
   result.MotorDistanceUm = s_params.MotorDistanceUm;
   return result;
}

void Calibration_GetPenAngles(uint8_t *outUpAngle, uint8_t *outDownAngle)
{
   *outUpAngle = s_params.PenUpAngle;
   *outDownAngle = s_params.PenDownAngle;
}

/** Is this internal-frame point inside the travel zone (@req{REQ-CoreLR-F-Motion-0008}) of the geometry in s_params? The one predicate of the reference point
 *  (@req{REQ-CoreLR-D-Calibration-0011}) and of the drawing origin (-0012); the same margins as Motion's zone check. */
static uint8_t PointInsideZone(int32_t x, int32_t y)
{
   return (uint8_t)((x >= MOTION_MARGIN_SIDE_UM) && (x <= (s_params.MotorDistanceUm - MOTION_MARGIN_SIDE_UM)) && (y >= MOTION_MARGIN_TOP_UM));
}

uint8_t Calibration_SetParams(const Calibration_Params_t *params)
{
   uint8_t geometryChanged = (uint8_t)((params->MotorDistanceUm != s_params.MotorDistanceUm)
                                    || (params->ReferenceCableLengthUm != s_params.ReferenceCableLengthUm));
   Calibration_Params_t previous = s_params;

   s_params = *params;
   RecomputeReferencePosition(); /* always re-derived from D/Lref, never trusted from the caller
                                     (REQ-CoreLR-D-Calibration-0001) -- same absolute-over-
                                     relative principle as position/pen state elsewhere in the project */
   /* The reference position (where calibration ends, and the drawing origin defaults to) must be inside the travel zone, or the machine would be
    * calibrated at a place it may not go to (REQ-CoreLR-D-Calibration-0011). Only a change of geometry is judged: the pen angles and the drawing
    * origin leave it as it was. A refusal restores everything, derived fields included, and touches neither the EEPROM nor the calibrated flag. */
   if (geometryChanged && !PointInsideZone(s_params.ReferencePositionX, s_params.ReferencePositionY))
   {
      s_params = previous;
      return 0;
   }
   /* A change of installation geometry makes the stored step counts mean
    * something else: the position is no longer known
    * (REQ-CoreLR-D-Calibration-0007). Invalidated BEFORE the new values are
    * persisted, so no power cut can leave the new geometry next to a
    * position that was valid for the old one. Pen angles and the drawing
    * origin do not move the head and leave it alone. */
   if (geometryChanged)
   {
      Calibration_InvalidatePosition();
      s_isCalibrated = 0;
   }
   Persist();
   return 1;
}

void Calibration_Invalidate(void)
{
   Calibration_InvalidatePosition();
   s_isCalibrated = 0;
}

uint8_t Calibration_IsCalibrated(void)
{
   return s_isCalibrated;
}

void Calibration_MarkCalibrated(void)
{
   s_isCalibrated = 1;
   /* Drawing origin defaults to the reference position at the moment
    * calibration completes (REQ-CoreLR-D-Calibration-0002); it
    * is then stored independently and never re-derived automatically
    * again, so a later manual adjustment of the work origin persists. */
   s_params.WorkOriginOffsetX = s_params.ReferencePositionX;
   s_params.WorkOriginOffsetY = s_params.ReferencePositionY;
   Persist();
}

/** @brief Zero/factory-default parameters, matching Walldraw.ino's servo angles. */
static void LoadFactoryDefaults(void)
{
   s_params.MotorDistanceUm = 0;
   s_params.ReferencePositionX = 0;
   s_params.ReferencePositionY = 0;
   s_params.ReferenceCableLengthUm = 0;
   s_params.WorkOriginOffsetX = 0;
   s_params.WorkOriginOffsetY = 0;
   s_params.PenUpAngle = 60;   /* factory default, per Walldraw.ino reference */
   s_params.PenDownAngle = 95; /* factory default, per Walldraw.ino reference */
}

/**
 * @brief Recompute ReferencePositionX/Y from MotorDistanceUm/
 * ReferenceCableLengthUm (@req{REQ-CoreLR-D-Calibration-0001}): the
 * intersection of two Lref-radius arcs centred on each motor, in the
 * internal kinematics frame (motor L at the origin) --
 * (D/2, sqrt(Lref^2 - (D/2)^2)). Delegates the integer square root to
 * Motion rather than duplicating it.
 */
static void RecomputeReferencePosition(void)
{
   int32_t halfDistanceUm = s_params.MotorDistanceUm / 2;

   s_params.ReferencePositionX = halfDistanceUm;

#if CORELR_MATH == CORELR_MATH_64
   {
      uint64_t halfDistanceSquared = (uint64_t)((int64_t)halfDistanceUm * (int64_t)halfDistanceUm);
      uint64_t referenceLengthSquared = (uint64_t)((int64_t)s_params.ReferenceCableLengthUm * (int64_t)s_params.ReferenceCableLengthUm);

      if (referenceLengthSquared < halfDistanceSquared)
      {
         /* Misconfigured (Lref shorter than half the motor spacing): no
          * real intersection exists. Defensive fallback, not reachable
          * with a correctly measured machine. */
         s_params.ReferencePositionY = 0;
      }
      else
      {
         s_params.ReferencePositionY = (int32_t)Motion_ISqrt(referenceLengthSquared - halfDistanceSquared);
      }
   }
#else
   /*
    * CORELR_MATH_48 (REQ-CoreLR-D-Motion-0024): y = floor(sqrt(L^2 - h^2)) with no value wider than 32 bits, by
    * bisection on Motion_Hypot -- 22 steps, ~4 ms, once per calibration. The largest y with
    * floor(hypot(h, y)) < L satisfies y <= y* <= y + 1 (floor(hypot) is monotonic, and only y* itself can
    * reach L), and y* = y + 1 exactly when the residual L^2 - h^2 - y^2 reaches 2y + 1: known from the LOW
    * words alone, as the residual is far below 2^32. Lengths are saturated at the bound of this arithmetic.
    */
   uint32_t h = (uint32_t)halfDistanceUm;
   uint32_t L = (uint32_t)s_params.ReferenceCableLengthUm;
   uint32_t lo = 0;
   uint32_t hi;

   if (L > (uint32_t)CORELR_MATH_48_LIMIT_UM) { L = (uint32_t)CORELR_MATH_48_LIMIT_UM; }
   if (h > (uint32_t)CORELR_MATH_48_LIMIT_UM) { h = (uint32_t)CORELR_MATH_48_LIMIT_UM; }
   if (L < h)
   {
      s_params.ReferencePositionY = 0;   /* misconfigured, as above */
   }
   else
   {
      hi = L;
      while (lo < hi)
      {
         uint32_t mid = lo + ((hi - lo + 1u) >> 1);

         if (Motion_Hypot((int32_t)h, (int32_t)mid) < L)
         {
            lo = mid;
         }
         else
         {
            hi = mid - 1u;
         }
      }
      if ((L * L - h * h - lo * lo) >= ((lo << 1) + 1u))
      {
         lo++;
      }
      s_params.ReferencePositionY = (int32_t)lo;
   }
#endif
}

/** @brief Compute the additive checksum covering Magic+Params. */
static uint8_t ComputeChecksum(const EepromImage_t *image)
{
   const uint8_t *bytes = (const uint8_t *)image;
   uint8_t length = (uint8_t)(sizeof(*image) - sizeof(image->Checksum));
   uint8_t sum = 0;
   uint8_t i;

   for (i = 0; i < length; i++)
   {
      sum = (uint8_t)(sum + bytes[i]);
   }
   return sum;
}

/** @brief Write the current in-memory parameters to EEPROM (wear-friendly: eeprom_update_block only rewrites changed bytes). */
static void Persist(void)
{
   EepromImage_t image;

   image.Magic = EEPROM_MAGIC;
   image.Params = s_params;
   image.Checksum = ComputeChecksum(&image);

   eeprom_update_block(&image, (void *)CALIBRATION_EEPROM_ADDRESS, sizeof(image));
}

/** @brief Additive checksum over everything in the record before the checksum itself. */
#if CALIBRATION_POSITION_RING
/** CRC-8, polynomial 0x07, MSB first, bit by bit (a table would cost more flash than the speed is worth: a slot is 15 bytes), over Seq, Flags and then
 *  `length` bytes of payload. */
static uint8_t Crc8(uint8_t seq, uint8_t flags, const uint8_t *data, uint8_t length)
{
   uint8_t crc = 0;
   uint8_t bit;
   uint8_t at;

   for (at = 0; at < (uint8_t)(length + 2u); at++)
   {
      crc ^= (at == 0u) ? seq : (at == 1u) ? flags : data[at - 2u];
      for (bit = 0; bit < 8u; bit++)
      {
         crc = (uint8_t)((crc & 0x80u) ? ((uint8_t)(crc << 1) ^ 0x07u) : (uint8_t)(crc << 1));
      }
   }
   return crc;
}

/** @return 1 if `slot` holds a record: the magic reads POS_MAGIC (an erased slot reads 0xFF, magic 0x7F) and the CRC matches. Seq and Flags are returned, the payload copied out for a valid record. */
static uint8_t PositionSlotRead(uint8_t slot, uint8_t *outSeq, uint8_t *outFlags, uint8_t *payload)
{
   const uint8_t *base = (const uint8_t *)(CALIBRATION_POSITION_EEPROM_ADDRESS + (uint16_t)slot * POS_SLOT_BYTES);
   uint8_t seq = eeprom_read_byte(base + POS_OFF_SEQ);
   uint8_t flags = eeprom_read_byte(base + POS_OFF_FLAGS);
   uint8_t length = 0;

   *outSeq = seq;
   *outFlags = flags;
   if ((flags & POS_MAGIC_MASK) != POS_MAGIC)
   {
      return 0;
   }
   if ((flags & POS_FLAG_VALID) != 0u)
   {
      eeprom_read_block(payload, base + POS_OFF_PAYLOAD, POS_PAYLOAD_BYTES);
      length = (uint8_t)POS_PAYLOAD_BYTES;
   }
   return (uint8_t)(Crc8(seq, flags, payload, length) == eeprom_read_byte(base + POS_OFF_CRC));
}

/** @return 1 if `value` fits a stored field (always, for 4-byte fields). */
static uint8_t PositionFits(int32_t value)
{
#if POS_FIELD_BYTES == 4u
   (void)value;
   return 1;
#else
   return (uint8_t)((value >= -(1L << 23)) && (value < (1L << 23)));
#endif
}

/**
 * Append one record to the oldest slot of the ring. `fields` is StepsL, StepsR, InternalX, InternalY for a valid record, ignored (may be NULL) for an
 * invalidation. Payload and CRC, then Flags, then Seq -- the commit -- and the write is complete when this returns.
 * The fields are packed by bytes (the AVR, like the host, is little endian: a field's low bytes come first), never by a variable-count 32-bit shift,
 * which is a library call on this target.
 */
static void PositionAppend(uint8_t valid, const int32_t *fields)
{
   uint8_t slot = (uint8_t)((s_posHead + 1u) & (POS_SLOTS - 1u));
   uint8_t seq = (uint8_t)(s_posSeq + 1u);
   uint8_t flags = POS_FLAGS(valid);
   uint8_t *base = (uint8_t *)(CALIBRATION_POSITION_EEPROM_ADDRESS + (uint16_t)slot * POS_SLOT_BYTES);
   uint8_t body[POS_PAYLOAD_BYTES + 1u];          /* payload, then its CRC: written as one block, after which only Flags and Seq remain */
   uint8_t *out = body;
   uint8_t *dst = base + POS_OFF_PAYLOAD;
   uint8_t count = (uint8_t)(POS_PAYLOAD_BYTES + 1u);

   if (valid)
   {
      const uint8_t *in = (const uint8_t *)fields;
      uint8_t f;
      uint8_t b;

      for (f = 0; f < 4u; f++)
      {
         for (b = 0; b < 4u; b++, in++)
         {
            if (b < (uint8_t)POS_FIELD_BYTES)
            {
               *out++ = *in;
            }
         }
      }
   }
   body[POS_PAYLOAD_BYTES] = Crc8(seq, flags, body, valid ? (uint8_t)POS_PAYLOAD_BYTES : 0u);
   if (!valid)
   {
      out = &body[POS_PAYLOAD_BYTES];   /* an invalidation writes the CRC alone: the slot's payload bytes are stale and left alone (no wear) */
      dst += POS_PAYLOAD_BYTES;
      count = 1u;
   }
   else
   {
      out = body;
   }
   eeprom_update_block(out, dst, count);
   eeprom_update_block(&flags, base + POS_OFF_FLAGS, 1u);
   eeprom_update_block(&seq, base + POS_OFF_SEQ, 1u);        /* the commit */
   eeprom_busy_wait();                 /* complete before any step can follow (REQ-CoreLR-D-Stepper-0014) */
   s_posHead = slot;
   s_posSeq = seq;
   s_positionValid = valid;
}

uint8_t Calibration_IsPositionStored(void)
{
   return s_positionValid;
}

void Calibration_InvalidatePosition(void)
{
   if (!s_positionValid)
   {
      return; /* the common case, and the one on the motion hot path: a flag test */
   }
   PositionAppend(0, NULL);
}

void Calibration_StoreCurrentPosition(void)
{
   int32_t fields[4];

   Motion_GetPersistedState(&fields[0], &fields[1], &fields[2], &fields[3]);

   /* The newest record must say "unknown" while the new one is written, whatever state we were called in: a store over a still-valid record would otherwise
    * leave, if the power failed in the middle, an older position as the newest valid one (REQ-CoreLR-D-Calibration-0006). */
   Calibration_InvalidatePosition();

   if (PositionFits(fields[0]) && PositionFits(fields[1]) && PositionFits(fields[2]) && PositionFits(fields[3]))
   {
      PositionAppend(1, fields);
   }
   /* else: it cannot be stored in 24 bits (CORELR_MATH_48 never gets here: asserted above); the machine stays "unknown", the safe failure */
}

void Calibration_RestoreStoredPosition(void)
{
   if (s_positionValid)
   {
      uint8_t seq;
      uint8_t flags;
      uint8_t payload[POS_PAYLOAD_BYTES];
      int32_t f[4];

      if (!PositionSlotRead(s_posHead, &seq, &flags, payload))
      {
         return;                        /* cannot happen: the newest record was read and checked at startup, or just written */
      }
      {
         const uint8_t *in = payload;
         uint8_t *out = (uint8_t *)f;
         uint8_t field;
         uint8_t b;

         for (field = 0; field < 4u; field++)
         {
            uint8_t last = 0;

            for (b = 0; b < 4u; b++)
            {
               if (b < (uint8_t)POS_FIELD_BYTES)
               {
                  last = *in++;
                  *out++ = last;
               }
               else
               {
                  *out++ = (uint8_t)((last & 0x80u) ? 0xFFu : 0x00u);        /* sign extension */
               }
            }
         }
      }
      Motion_Restore(f[0], f[1], f[2], f[3]);
   }
}

#else
static uint8_t PositionChecksum(const PositionRecord_t *record)
{
   const uint8_t *bytes = (const uint8_t *)record;
   uint8_t sum = 0;
   uint8_t i;

   for (i = 0; i < (uint8_t)offsetof(PositionRecord_t, Checksum); i++)
   {
      sum = (uint8_t)(sum + bytes[i]);
   }
   return sum;
}

uint8_t Calibration_IsPositionStored(void)
{
   return s_positionValid;
}

void Calibration_InvalidatePosition(void)
{
   if (!s_positionValid)
   {
      return; /* the common case, and the one on the motion hot path: a flag test */
   }
   eeprom_update_byte((uint8_t *)POSITION_ADDR(Marker), POSITION_MARKER_INVALID);
   eeprom_busy_wait(); /* the write must be complete before any step can follow (REQ-CoreLR-D-Stepper-0014) */
   s_positionValid = 0;
}

void Calibration_StoreCurrentPosition(void)
{
   PositionRecord_t record;

   Motion_GetPersistedState(&record.StepsL, &record.StepsR, &record.InternalX, &record.InternalY);

   /* The marker must be invalid while the payload is written, whatever
    * state we were called in: a store over a still-valid record would
    * otherwise pass through a window with a valid marker and half-new data. */
   Calibration_InvalidatePosition();

   record.Marker = POSITION_MARKER_VALID;
   record.Checksum = PositionChecksum(&record);

   /* Payload and checksum first, the marker LAST: a power cut anywhere in
    * this sequence leaves the marker invalid (REQ-CoreLR-D-Calibration-0006). */
   eeprom_update_block(&record.StepsL, POSITION_ADDR(StepsL),
                       offsetof(PositionRecord_t, Checksum) - offsetof(PositionRecord_t, StepsL));
   eeprom_update_byte((uint8_t *)POSITION_ADDR(Checksum), record.Checksum);
   eeprom_update_byte((uint8_t *)POSITION_ADDR(Marker), POSITION_MARKER_VALID);
   s_positionValid = 1;
}

void Calibration_RestoreStoredPosition(void)
{
   PositionRecord_t record;

   if (s_positionValid)
   {
      eeprom_read_block(&record, (const void *)CALIBRATION_POSITION_EEPROM_ADDRESS, sizeof(record));
      Motion_Restore(record.StepsL, record.StepsR, record.InternalX, record.InternalY);
   }
}

#endif

void Calibration_GetWorkOrigin(int32_t *outX, int32_t *outY)
{
   *outX = s_params.WorkOriginOffsetX;
   *outY = s_params.WorkOriginOffsetY;
}

void Calibration_GetReferencePosition(int32_t *outX, int32_t *outY)
{
   *outX = s_params.ReferencePositionX;
   *outY = s_params.ReferencePositionY;
}

uint8_t Calibration_SetWorkOrigin(int32_t offsetX, int32_t offsetY)
{
   if (!PointInsideZone(offsetX, offsetY))           /* also refuses everything while the geometry is unknown (D = 0: the zone is empty) */
   {
      return 0;
   }
   s_params.WorkOriginOffsetX = offsetX;
   s_params.WorkOriginOffsetY = offsetY;
   Persist();
   return 1;
}
