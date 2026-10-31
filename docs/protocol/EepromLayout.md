# EEPROM layout

What CoreLR keeps in the ATmega328P's 1024 bytes of EEPROM, and how. Source of truth: `firmware/src/Calibration.c` (the structures, with the same pictures in
its Doxygen comments), `SwConfig.h` (`CALIBRATION_EEPROM_ADDRESS`, `CALIBRATION_POSITION_EEPROM_ADDRESS`, `CALIBRATION_POSITION_SLOTS`,
`CALIBRATION_POSITION_RING`). Requirements: `REQ-CoreLR-D-Calibration-0003` (the parameters), `-0006`, `-0007`, `-0008` (the position), `-0010` (the ring).
All multi-byte values are **little endian**, as the AVR stores them.

## 1. Map

```
 address    size   content
 ---------  -----  -----------------------------------------------------------------------
      0..30    31  calibration image (section 2)
     31..63    33  reserve (left erased; room for more parameters)
    64..1023   960  the position ring (section 3): 64 slots x 15 bytes = 960, to the very end
```

The first 64 bytes belong to the parameters and the reserve (`CALIBRATION_POSITION_EEPROM_ADDRESS` = 64; `Calibration.c` checks at build time that the image
fits below it and that the ring fits above it). Nothing the position keeper does ever writes below address 64: storing a position never rewrites the
installation parameters (checked by `test_position.c`).

## 2. The calibration image (address 0, 31 bytes)

Written by `Calibration_SetParams()` / `Calibration_MarkCalibrated()`, i.e. when the geometry, the pen angles or the drawing origin change -- rarely.
`eeprom_update_block()` rewrites only the bytes that differ.

```
 offset  size  field                    meaning
 ------  ----  -----------------------  -------------------------------------------------------------
    0      4   Magic = 0x43524C32       ASCII "CRL2": an erased or older image is rejected, factory defaults load
    4      4   MotorDistanceUm          distance between the two motors' cable exit points, um
    8      4   ReferencePositionX       reference position, X, internal frame (derived from D and Lref, never trusted from a caller)
   12      4   ReferencePositionY       reference position, Y, internal frame
   16      4   ReferenceCableLengthUm   Lref, the cable length at the reference position, um
   20      4   WorkOriginOffsetX        drawing-origin offset, X, internal frame (defaults to ReferencePositionX)
   24      4   WorkOriginOffsetY        drawing-origin offset, Y, internal frame
   28      1   PenUpAngle               servo angle, pen lifted
   29      1   PenDownAngle             servo angle, pen lowered
   30      1   Checksum                 additive sum of bytes 0..29
```

## 3. The position ring (address 64)

The machine's position -- the step counts last commanded to each motor and the position in the internal frame -- is kept where a power cut cannot make it
stale: it is stored after `CALIBRATION_POSITION_STORE_DELAY_MS` (2 s) of stillness, and **invalidated before the first step** of any movement.
Both events are *records appended to a ring*, so that no EEPROM cell is rewritten more than once every `SLOTS / 2` motion cycles.

```
 one slot, at 64 + 15 * k            (15 bytes with 24-bit fields, CORELR_MATH_48; 19 with the 32-bit fields of CORELR_MATH_64)
 offset  size  field      meaning
 ------  ----  ---------  ----------------------------------------------------------------------------------------
    0      1   Seq        sequence number, 8 bits, modulo 256 -- the COMMIT byte: written last
    1      1   Flags      bit 7     valid: 1 = a position follows, 0 = "the position is no longer known"
                          bits 6..0 magic = 0x55, a fixed 7-bit number (erased EEPROM reads 0x7F there: a slot never written is rejected)
    2      3   StepsL     24-bit signed   |  the payload: four fields, valid records only
    5      3   StepsR     24-bit signed   |  (in CORELR_MATH_64: four bytes each, int32)
    8      3   InternalX  um, internal frame
   11      3   InternalY  um, internal frame
   14      1   CRC-8      polynomial 0x07, MSB first, over Seq, Flags and -- for a valid record -- the payload
```

* A **valid record** is all fifteen bytes. An **invalidation** is `Seq`, `Flags` (valid = 0) and the CRC only; the slot's payload bytes are stale, neither written nor read.
* **Which record is the newest:** the one with the greatest `Seq` in serial-number arithmetic -- `(seq - best) & 255` in 1..127 means newer. That is exact while the live
  records span fewer than 128 numbers, which any ring of up to 128 slots guarantees; `Seq` and the slot index (a mask: the slot count is a power of two) wrap freely.
* **At startup** all slots are read; the newest record that passes the magic and CRC checks decides: a valid position -> the machine is calibrated and `Motion` is
  restored from it; an invalidation, or an empty ring -> uncalibrated.
* **Appending** always goes to the slot after the newest: payload and CRC as one block, then `Flags`, then `Seq`, and the write is complete (`eeprom_busy_wait`) before
  any step can follow. A power cut in the middle leaves a slot rejected by its CRC -- or, one chance in 256, accepted but carrying that slot's OLD `Seq`, the oldest of
  the ring, which cannot beat the newest valid record. A cut during an invalidation leaves the older position (still true: nothing has moved yet); during a store, an unknown
  position (the safe failure).
* **The count of slots** (`CALIBRATION_POSITION_SLOTS`, a power of two, 2..128): by default the most the EEPROM holds -- **64** in `CORELR_MATH_48` (960 bytes, exactly to the end),
  **32** in `CORELR_MATH_64` (19-byte slots, 608 bytes). Wear: 0.031 writes per cell per motion cycle with 64 slots (measured, `test_position.c`): ~3.2 million cycles at the
  ~100 000-write endurance of a cell, 64 times the single record's 50 000.
* **A field that does not fit 24 bits** (not possible in `CORELR_MATH_48`: build-time `_Static_assert`s) is not stored; the machine stays "unknown".
* **The magic** is a fixed 7-bit number, `POS_MAGIC` = 0x55, in the low bits of `Flags` (any value but 0x7F, which is what erased cells read). With the CRC-8 a slot is accepted from random bytes
  one time in 32 768 (half of those as a *valid* record): over 20 000 random rings of 64 slots, 0 to 18 came back calibrated (~0.1 %). The ring only ever holds erased cells or our own records.
  **The magic is part of the format:** if bits of `Flags` are ever given another meaning, the magic changes with them -- an old ring is then rejected (one recalibration), or an intermediate
  firmware erases the EEPROM first. `_Static_assert` checks it is 7 bits and not 0x7F.

### Example: the first slot after a store

`store(StepsL = -5, StepsR = 0x12345, InternalX = -8388608, InternalY = 8388607)` on a blank ring (`CORELR_MATH_48`), read back from address 64 (`test_position.c` compares these bytes with a
CRC-8 and a packing written independently of the firmware):

```
 Seq  Flags  StepsL    StepsR    InternalX InternalY CRC-8
 00   D5     FB FF FF  45 23 01  00 00 80  FF FF 7F D6
      |
      valid (0x80) | magic (0x55)
```

and the invalidation that follows goes to the next slot: `01 55`, the CRC of those two bytes (`Seq`, `Flags`) at offset 14, and nothing written in between.

### The SD variant

`CALIBRATION_POSITION_RING = 0` (**not** the default any more: since 2026-10-10 both variants keep the ring; the SD build holds its ~590 bytes thanks to `-mcall-prologues`, `cmake/Firmware.cmake`): the position is **one 18-byte record**, at the same address 64:

```
 offset  size  field      meaning
    0      1   Marker     0xA5 = valid, 0x00 = invalidated (written twice per motion cycle: it wears out first, ~50 000 cycles), 0xFF = erased
    1      4   StepsL     int32
    5      4   StepsR     int32
    9      4   InternalX  int32, um
   13      4   InternalY  int32, um
   17      1   Checksum   additive, over bytes 0..16
```

## 4. Moving between formats

A format or an address change costs **one recalibration** at the first start (the ring reads as empty; the old record is left where it was and nothing reads it). The
ring replaced the single record, and the address moved from 32 to 64, on 2026-10-10.
