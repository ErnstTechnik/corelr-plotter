# Design Requirements — Calibration & Reference Position

Module: **Calibration** — persistence mechanism for configurable
parameters, and the geometric method used to define the calibration
reference position.

Realizes: [`REQ-CoreLR-F-Calibration-0002`, `-0003`, `-0004`, `-0005`, `-0006`](../functional/REQ-CoreLR-F-Calibration.md)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Implemented (2026-09-07)

## REQ-CoreLR-D-Calibration-0001 — Reference position definition
<a id="REQ-CoreLR-D-Calibration-0001"></a>
The reference position used for calibration shall be defined as the
geometric intersection point of two circular arcs, each centred on one
motor's pivot point, both at an equal reference cable length
(`L = R = Lref`).

```
     Motor L (0,0)         Motor R (D,0)
          \                      /
           \ Lref          Lref /
            \                  /
             \                /
              \______________/
                 intersection
              (reference position,
               computed geometrically
               from D and Lref)
```

**Rationale:** this point is derived purely from the inter-motor distance
`D` and `Lref`, so it is reproducible without depending on an external
physical mark on the wall — realizes `REQ-CoreLR-F-Calibration-0004`.
In the internal kinematics frame (motor L at the origin), this point is
at `(D/2, sqrt(Lref² - (D/2)²))` — the same integer-square-root
primitive used by `Motion_XYToLR` for the X,Y→L,R transform
itself (see `REQ-CoreLR-D-Motion-0005`); this module calls
into `Motion` for that computation rather than duplicating it.

## REQ-CoreLR-D-Calibration-0002 — Drawing origin offset storage
<a id="REQ-CoreLR-D-Calibration-0002"></a>
Realizes `REQ-CoreLR-F-Calibration-0006`. `Calibration_Params_t`
gains two fields, `WorkOriginOffsetX`/`WorkOriginOffsetY` (signed µm, in
the internal kinematics frame) — the internal-frame position that the
user/GCode-facing `(0,0)` maps to. Defaulted to the reference position
(`-0001`) whenever calibration completes, but stored and persisted as
its own value, not re-derived from `D`/`Lref` on every use — leaving
room for it to later be adjusted independently of where the machine was
physically calibrated.

## REQ-CoreLR-D-Calibration-0003 — Storage technology & integrity check
<a id="REQ-CoreLR-D-Calibration-0003"></a>

> **Amended 2026-10-06:** the `IsCalibrated` byte is dropped from the image (`-0007`); the position is a separate record (`-0006`). The image is otherwise unchanged.
Configurable parameters (`REQ-CoreLR-F-Calibration-0002`) and the
persisted calibrated flag (`REQ-CoreLR-F-Calibration-0003`) are stored
in the ATmega328P's internal EEPROM (`avr/eeprom.h`), not the SD card —
distinct medium from GCode job storage, and avoids depending on a card
being present just to remember calibration.

Layout: a fixed-address image = `{ Magic (uint32_t), Params
(Calibration_Params_t), IsCalibrated (uint8_t), Checksum (uint8_t) }`.
- `Magic` is a version-tagged constant (`"CRL1"`); a blank/never-written
  EEPROM reads back as `0xFF` bytes, which can never match it, so a
  fresh chip is reliably detected and factory defaults are loaded.
- `Checksum` is a simple additive checksum over every preceding byte
  (Magic+Params+IsCalibrated), catching a partial/corrupted write (e.g.
  power loss mid-write). On mismatch, factory defaults are loaded, the
  same as a blank EEPROM.
- Writes use `eeprom_update_block()` (rewrites only the bytes that
  actually changed) rather than `eeprom_write_block()`, to reduce
  EEPROM wear across repeated calibration/config changes.

`ReferencePositionX/Y` are never trusted as caller-supplied input to
`Calibration_SetParams()` — they are always recomputed from the
given `MotorDistanceUm`/`ReferenceCableLengthUm` before persisting
(same absolute-over-relative principle applied elsewhere in the
project: a derived value is re-derived, never carried forward
untrusted).

## REQ-CoreLR-D-Calibration-0004 — Narrow getters for hot-path callers (resolved 2026-09-07)
<a id="REQ-CoreLR-D-Calibration-0004"></a>
`Calibration_GetParams()` returns the full 7-field
`Calibration_Params_t` by value, copied at every call site — most
callers only need 1-3 fields (`Motion_XYToLR()`, called at
least once per Bresenham grid step during motion, only reads
`WorkOriginOffsetX/Y`/`MotorDistanceUm`; `PenControl` only reads the two
pen angles). Two narrow getters were added instead of widening every
caller's own state:
- `Calibration_GetKinematicsParams()` returns a 3-field
  `Calibration_KinematicsParams_t` for `Motion_XYToLR()`.
- `Calibration_GetPenAngles(uint8_t*, uint8_t*)` returns the two
  pen angles via output parameters (no struct copy at all) for
  `PenControl`.

`Calibration_GetParams()` itself is unchanged and still the
right call for the LCD calibration wizards (`LcdJoystickUI.cpp`): a
fetch-mutate-set sequence genuinely needs the whole struct, and runs at
human-interaction pace, not once per motion tick.

**Rejected:** reading fields directly from EEPROM on every call instead
of caching in RAM — EEPROM reads are ~3.4µs/byte (serial), not free
inside a per-waypoint hot path, and the parameters change only on rare,
user-driven events (`SetParams`/`MarkCalibrated`), so a
stale-until-changed RAM copy is the right default.

## REQ-CoreLR-D-Calibration-0005 — Parameters cross the API by pointer (implemented 2026-09-11)
<a id="REQ-CoreLR-D-Calibration-0005"></a>

`Calibration_GetParams()` and `Calibration_SetParams()` shall take a
pointer, and a caller wanting a single field shall be given a narrow
getter rather than the whole block.

`Calibration_Params_t` is **26 bytes**, past avr-gcc's 8-byte
register-return budget, so by value the ABI passed a hidden pointer and
copied all 26 bytes at every call -- visible in the disassembly as
`ldi r18, 0x1A` followed by a copy loop -- plus 26 bytes of caller
stack for the temporary.

Narrow getters added: `Calibration_GetMotorDistanceUm()` and
`Calibration_GetReferenceCableLengthUm()`, joining the existing
`Calibration_GetPenAngles()` and `Calibration_GetKinematicsParams()`.
Five of the seven call sites read one field; only the two
fetch-mutate-set wizard sequences need the whole block.

### Measured, and honestly

Flash **-126 bytes**. Stack: **no change at all** -- `LcdJoystickUI_Task`'s
frame stayed at 62 bytes and the worst case at 304. With `-flto` the
compiler already saw that the getter only returned a static, inlined
it, and read just the field wanted, so the copy in the standalone
symbol had already been optimised away at every real call site.

The change is therefore justified by robustness rather than by a
measured stack saving: the copy returns the moment the function grows
past what the inliner will swallow, or a build drops LTO -- silently,
at every call site. The general rule is recorded, project-agnostic, in
`docs/standards/STD-ModuleDesign.md`.

Note the contrast that sets the threshold: `Motion_Position_t` and
`Motion_CableLengths_t` are 8 bytes and travel **entirely in
registers** -- `Motion_XYToLR()`'s disassembly shows no hidden pointer
and no copy. Converting those to pointers would be a pessimism, forcing
the caller to materialise in memory what is currently in registers.

## REQ-CoreLR-D-Calibration-0006 — Position record, invalid before valid (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Calibration-0006"></a>
Realizes `REQ-CoreLR-F-Calibration-0003`, `-0007`.

The persisted position is its own EEPROM record, **separate from the
calibration image** of `-0003`, so that storing a position never rewrites
the installation parameters and their checksum:

```
{ Marker (uint8_t), StepsL, StepsR, InternalX, InternalY (int32_t each), Checksum (uint8_t) }   /* 18 bytes */
```

- `Marker == POSITION_VALID` (a fixed non-trivial byte, **not** `0x00` or
  `0xFF`) **and** the additive checksum is correct ⇔ the position is
  valid. A blank EEPROM reads `0xFF` and is therefore invalid.
- **Invalidate** = write the marker alone. It is called before the first
  step of a movement episode (`REQ-CoreLR-D-Stepper-0014`), uses
  `eeprom_update_byte` (nothing is written if it is already invalid) and
  **waits for completion** (`eeprom_busy_wait`, ≤ 3.4 ms) before
  returning, so the invalidation is on the chip before any step.
- **Store** = update `StepsL`, `StepsR` and the checksum, and write the
  marker **last**. A power cut anywhere in the sequence leaves the marker
  invalid, which is the safe outcome.
- `eeprom_update_*` throughout, so a byte is rewritten only if it
  changed.
- `StepsL`/`StepsR` are the **absolute commanded step counts**
  (`Motion`'s `c_L`, `c_R`) — the cable lengths in steps, from the
  unreachable zero length. They are the motors' own truth, which a
  quantised `X,Y` would only approximate (maintainer, 2026-10-06:
  *store the steps, derive the position*). **Amended the same day:** the
  internal-frame position `InternalX/Y` is stored beside them for now,
  because deriving it cost 718 bytes of flash — see `-0007`'s open item.

A store is ~18 byte writes, ≈ 60 ms of blocking in the main loop —
acceptable only because the machine is stationary when it happens
(`-0008`). If motion is requested during it, the invalidation waits for
it, at most ~60 ms.

**Wear.** Two marker writes per movement episode (invalidate, validate);
the step bytes change only when the position does. At ~100 000 cycles of
endurance that is ~100 000 episodes. Rotating the record's address to
spread the wear is **deferred to the end of the project** (maintainer,
2026-10-06) and listed under the open items.

## REQ-CoreLR-D-Calibration-0007 — Restoring the position at startup (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Calibration-0007"></a>
At startup, if the calibration image is valid **and** the position
record is valid, the machine is calibrated and `Motion` is seeded from
the stored step counts (`Motion_SeedFromSteps(l, r)`); otherwise it is
not calibrated, whatever the old calibrated flag said.

- **`IsCalibrated` in the calibration image is dropped.** *Calibrated*
  now means *the position is valid*, which is a different fact with a
  different lifetime and lives in the position record.
- **The position is stored, in the internal frame**, and the drawing
  position is recovered by subtracting the drawing-origin offset in force.
  Storing the internal rather than the drawing position keeps it right if
  the offset is changed later. *(The maintainer's first decision was to derive
  X,Y from the counts and the calibration; that was implemented, verified
  on the host to 41 µm over 130 points, and **removed 2026-10-06** for its
  flash cost, 718 bytes. See the open item below.)*
- **`Motion`'s commanded counts are seeded with the stored ones
  exactly**, not re-quantised from the recomputed `X,Y`.
- Changing the installation parameters (`Calibration_SetParams`, i.e. the
  distance or the reference cable length) **invalidates** the position,
  since the counts no longer mean the same geometry. The "Calibrate
  machine" workflow already prefills its numbers from the stored values,
  so re-establishing the position is the adjustment jog and one
  confirmation.
- **This closes the defect found on the bench on 2026-10-06**: after a
  reboot the machine believed it was calibrated, `Motion` held position
  (0,0) with zero commanded steps against an ideal of ~13 000, and the
  hysteresis walked both motors one step per waypoint in the same
  direction, whatever the stick did (`../docs/bringup/Bringup-Status.md`).

## REQ-CoreLR-D-Calibration-0008 — When the position is stored (proposed 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Calibration-0008"></a>
Realizes `REQ-CoreLR-F-Calibration-0007`'s table.

**Stationary** means: `Motion` idle, the planner empty, and the step
timer disarmed (`REQ-CoreLR-D-Stepper-0013` — which is true exactly when
the last hold has expired). A store is attempted only while the record is
currently invalid, i.e. only after something moved.

| Situation | Trigger |
|---|---|
| manual jog | stationary for **2 s** (`SysTick`, a software tunable) |
| file mode | the job reaches its end and the motion has drained — immediately |

A paused, cancelled or errored job is stationary and is covered by the
2 s rule; a power cut while *paused* therefore costs a recalibration,
conservatively.

**Where the trigger lives** *(proposed)*: in the `loop()` orchestration
of `grbl_corelr.ino`, calling `Calibration_StorePosition()` with
`Motion_GetCommandedSteps()`. `Calibration` cannot do it itself without
depending on `Motion`, which already depends on `Calibration`.

### Implementation notes, 2026-10-06
- Record address `CALIBRATION_POSITION_EEPROM_ADDRESS` = 32, after the image
  area; a `_Static_assert` in `Calibration.c` fails the build if the image
  ever outgrows it. Marker `0xA5` valid, `0x00` invalid. Image magic bumped
  `"CRL1"` → `"CRL2"` (an old image is rejected: factory defaults, one
  re-entry of the installation parameters).
- **Verified on the host** (`tests/host/test_position.c`, the real
  `Calibration.c` and `Motion.c`): a power cut at **every byte write** of
  *invalidate, then store* — cleanly before, cleanly after, or leaving the
  byte `old&new` or `0xFF` — never restores a stale or mixed position (28
  cases); the restored machine does not catch up (9 coil changes per motor
  over a 600 µm jog, against ~100 for the defect); the forward kinematics
  round-trips within 41 µm over 130 points (one motor step = 53 µm). The
  test was checked to fail on three deliberately broken variants (marker
  written first; invalidate a no-op; restore disabled).
- **Cost, measured** (Nano fqbn, `arduino-builder`): flash 28182 →
  **29050 bytes (+868; 94 %)**, RAM 1710 → 1713, worst-case stack 296 → 293.
  The first version, which derived `X,Y` from the counts, was 29586: the
  forward kinematics alone was **718 bytes** (`Motion_SeedFromSteps` 530 +
  its 64-bit helper 188) — AVR has no 64-bit arithmetic, so every product
  is a library call with an eight-register argument set-up — and two
  rewrites saved only 54 and 40. Storing `InternalX/Y` instead removed it;
  grouping the glue in `Calibration` (callers make one call, no four-`int32`
  argument lists) took off a further ~340.
- The stationary test is `Stepper_IsIdle()` — nothing queued, no backlog,
  *and the last hold expired* — not `Stepper_IsEmpty()`, which is true while
  the last entry is still being held. In v2 it becomes "timer disarmed".

## REQ-CoreLR-D-Calibration-0009 — The Adjustment jog makes the machine uncalibrated at its first raw step (found 2026-10-10, implemented 2026-10-10)
<a id="REQ-CoreLR-D-Calibration-0009"></a>
The Adjustment jog -- step 3 of "Calibrate machine" -- moves the motors by raw steps while `Motion` does not follow. If the installation's distance and reference length were left unchanged, `Calibration_SetParams()` does not clear the calibrated flag, so abandoning step 3 (a long press) left a machine that reported *calibrated* with a position that was no longer where the head was --
and the position keeper would have **stored that wrong position two seconds later**, without any warning.
`Calibration_Invalidate()` (flag cleared, persisted position invalidated) is now called by the jog at the first raw step it queues (`if (Calibration_IsCalibrated())` before each accepted entry: a flag test, then nothing), so the machine is uncalibrated from then on, whatever way the mode is left; only the confirmation (`Calibration_MarkCalibrated()` + `ConfirmCalibration()`) brings it back.
An uncalibrated machine stores no position (`StorePositionNow()` requires `Calibration_IsCalibrated()`), and after a reset it comes back uncalibrated. **Verified:** `test_position.c` (invalidated: flag and stored position clear, still so after a reboot, confirming is the way back); `test_ui.cpp` 4l (a centred stick moves nothing and invalidates nothing; the first raw step invalidates once; a cancel leaves it uncalibrated); `tests/mutation/mutants/adjustment.txt`.

## REQ-CoreLR-D-Calibration-0010 — The position is kept in a ring: wear levelling of the EEPROM (decided with the maintainer 2026-10-10, implemented 2026-10-10)
<a id="REQ-CoreLR-D-Calibration-0010"></a>
Layout, pictures and the reasoning in detail: **`docs/protocol/EepromLayout.md`** (and the Doxygen of `Calibration.c`).

**The problem.** The persisted position was a single 18-byte record. Each motion cycle (an invalidation at the first step, a store after 2 s of stillness) wrote its marker byte **twice**, and an ATmega328P EEPROM cell lasts about 100 000 erase/write cycles (the datasheet's figure, per cell):
the marker would be worn out after ~50 000 cycles -- a jog session makes dozens of them in an afternoon, a job one. (It is the EEPROM, not the Flash: Flash is good for ~10 000.)

**The ring.** The first **64 bytes** of the EEPROM stay for the parameters and a reserve (the 31-byte calibration image at 0). From address 64 a **journal** of `CALIBRATION_POSITION_SLOTS` slots -- by default the most the EEPROM holds: **64 slots of 15 bytes = 960 bytes, to the very end**
(32 slots of 19 bytes with the 32-bit fields of `CORELR_MATH_64`). Every change of state *appends* a record to the oldest slot; the newest valid record says what the position is.
`slot = Seq | Flags | payload | CRC-8`: **`Seq` an 8-bit sequence number** in its own byte; **`Flags`**: bit 7 valid, **bits 6..0 the magic, a fixed 7-bit number (0x55)**; the payload **four 3-byte fields** (L steps, R steps, internal X, internal Y: 24-bit signed in `CORELR_MATH_48`;
4 bytes each in `_64`); the CRC-8 (poly 0x07) covers `Seq`, `Flags` and, for a valid record, the payload. An invalidation is `Seq`, `Flags` (valid = 0) and the CRC only (the stale payload is neither written nor read, no wear).
- **The magic** is any 7-bit value but 0x7F, which is what erased EEPROM reads in those bits: a slot never written is rejected by the magic alone. It is **part of the format**: if bits are ever given another meaning, the magic changes with them -- an old ring is rejected (one recalibration) or an intermediate firmware erases the EEPROM first (the maintainer's remark, 2026-10-10).
  The first design had a 1-bit magic and six free bits; the maintainer preferred a 7-bit magic, which brings random-garbage acceptance from 5.9 % to ~0.1 %.
- **The newest** is found at startup by reading the slots: the greatest `Seq` in serial-number arithmetic, `(seq - best) & 255` in 1..127 -- exact while the live records span fewer than 128 numbers, so the 8-bit counter and the slot index (a mask) wrap freely, and rings of up to 128 slots work. A valid newest record -> calibrated; an invalidation or a blank ring -> not.
- **Write order, the commit being `Seq`:** payload and CRC as one block, then `Flags`, then `Seq`; complete (`eeprom_busy_wait`) before any step can follow. A power cut in between leaves a slot rejected by its CRC -- or, one time in 256, accepted with the slot's OLD `Seq`, the oldest of the ring, which cannot beat the newest valid record.
  A cut during an invalidation leaves the older position, still true since nothing has moved; during a store, an unknown position (the safe failure). A store over a still-valid record invalidates first.
- **A value that does not fit 24 bits** is not stored (the machine stays "unknown"); in `CORELR_MATH_48` it cannot happen, and `_Static_assert`s say why: `CORELR_MATH_48_LIMIT_UM < 2^23`, and `CAL_MAX_UM + reach < 2^23`.
- **Both variants keep the ring** (`CALIBRATION_POSITION_RING = 1`, maintainer's decision 2026-10-10). It first stayed off in the SD variant (single record, the old code under `#else` at the same address 64, still there with `= 0`): the ring costs ~590 bytes of flash and the SD build had 126 free. What made room is the build setting **`-mcall-prologues`** (`tools/arduino/`, -1396 bytes) with the computed jog shape (-124): SD **29 608** with the ring (1 112 free; 29 074 without it), serial 26 474. **The SD build now depends on that flag**: without it the link fails ("too big"), nothing silently different. RAM +2 (1 343), worst stack 314 (300 without the ring; `stack_depth.py` now reads the flag's prologues), margin +391.
- **Migration:** the format and the address changed: the first start after the change reads an empty ring, "uncalibrated": **one recalibration**.

**Measured** (the EEPROM model of `test_position.c` counts the writes each cell really has): 20 000 cycles, an invalidation and a store of a position that changes each time -- with 64 slots the most worn cell has been written **625 times, 0.031 per cycle**, against 2.000 for the single record's marker:
**~3.2 million cycles at 100 000 writes a cell, 64 times the single record's 50 000**, exactly the 2 · cycles / slots the design promises (8 slots: x8, 16: x16, 32: x32 -- all tested). Nothing is written below address 64.
**Random garbage in the ring** (a 7-bit magic and a CRC-8: one slot in 32 768): 0 to 18 of 20 000 random rings came back calibrated (~0.1 %); the ring only ever holds erased cells or our own records.

**Verified** (`test_position.c`: the ring, the `_64` arithmetic and `CALIBRATION_POSITION_RING = 0`, the single record, and the SD variant's defines): **the layout byte for byte** (a store on a blank ring reads back `00 D5 FB FF FF 45 23 01 00 00 80 FF FF 7F D6`, compared with a CRC-8 and a packing written independently in the test; the invalidation `01 55 ... CRC`, its payload cells untouched);
a slot with a wrong magic and a correct CRC is no record, with the right one it is; 400 cycles with a reboot after each (the head round the ring 12 times, the sequence number 3 times); an invalidation as the newest record wins over older valid ones; the field extremes (±2^23 and the int32 range); a 25-bit value refused;
the **power-cut sweep at every byte, in the four torn-write modes, from 14 head positions** (after 0, 1, 2, 5, 31, 32, 33, 63, 64, 65, 127, 128, 129, 200 cycles: the wraps of the index at 32 cycles and of the sequence number at 128 are both crossed): 828 cases for an invalidation and a store, 468 for a store over a valid record -- never a mixed position, never the stale one once the invalidation has completed.
`tests/mutation/mutants/posring.txt`: 15 broken copies, all caught (the CRC, the magic -- not checked, one bit of it, not written --, what Flags the CRC covers, the choice of the newest, the head and the sequence number, the invalidation, the range check, the sign extension, the address). Not listed, equivalent or not testable deterministically: the invalidation also writing the stale payload, the write order, the polynomial.

## REQ-CoreLR-D-Calibration-0011 — The geometry is checked against the travel zone (implemented 2026-10-11)
<a id="REQ-CoreLR-D-Calibration-0011"></a>
Realizes: `REQ-CoreLR-F-Calibration-0009`. `Calibration_SetParams()` returns 1 if it stored the parameters and 0 if it refused them. It refuses when the **motor distance or the reference cable length changes** and the new reference position -- `(D/2, sqrt(Lref^2 - (D/2)^2))` in the internal frame, which
`RecomputeReferencePosition()` already derives -- is not inside the travel zone of `REQ-CoreLR-D-Motion-0028` (`D/2 >= side margin`, `refY >= top margin`; an `Lref` shorter than `D/2` has no intersection at all). A refusal leaves the previous geometry, the persisted image and the calibrated flag untouched. A call that leaves D and Lref as they are
(pen angles, drawing origin) is accepted whatever they are, so the pen wizard still works on a machine whose geometry predates a margin change. The LCD wizard (`REQ-CoreLR-D-LcdJoystickUI-0029`) does not leave the Lref step on a refusal. Tested by `tests/host/test_calzone.c` (the limits to the micrometre against a floating-point oracle, an exact 5-12-13 case, nothing changed by a refusal, an old out-of-zone image whose pen angle can still be set).


## REQ-CoreLR-D-Calibration-0012 — Setting the origin (implemented 2026-10-11)
<a id="REQ-CoreLR-D-Calibration-0012"></a>
Realizes: `REQ-CoreLR-F-Calibration-0010`. `uint8_t Calibration_SetWorkOrigin(int32_t offsetX, int32_t offsetY)` stores the two internal-frame coordinates of the drawing origin (`WorkOriginOffsetX/Y` of the image, already persisted by `Persist()`, whose bytes are only rewritten when they change) and returns 1; it
returns 0 and changes nothing when **the origin point is outside the travel zone** -- `side margin <= offsetX <= D - side margin` and `offsetY >= top margin` (the origin, being drawing (0, 0), sits at internal `(offsetX, offsetY)` in either Y convention) -- the same predicate, `PointInsideZone()`, from the same macros, as
`REQ-CoreLR-D-Calibration-0011` (which it refactored): while the geometry is unknown (`D = 0`) the zone is empty, so every origin is refused with no separate test. `void Calibration_GetReferencePosition(int32_t *x, int32_t *y)` is the reference point in the internal frame (for "Origin = calib."). The stored *position* is internal and does not change when the origin does. Tests: `tests/host/test_calzone.c` section 8 (the limits to the micrometre, the top margin, far below the motors, no geometry, a refusal leaving the stored origin and the EEPROM untouched, the origin surviving a reboot, "Calibrate machine" resetting a user-set one).


## Open items
**Functionally** none outstanding. L/R motor-selection UX for reaching
the reference position manually (`REQ-CoreLR-F-Calibration-0005`) is
decided in `REQ-CoreLR-D-LcdJoystickUI-0005` (X axis selects L/R, Y
axis jogs it).
- **EEPROM wear levelling for the position record** — rotating its
  address, to be done **last** (maintainer, 2026-10-06). The marker is
  the byte worn first; the arrangement needs a way to find the live slot
  at startup, which wants designing once, not twice.
- **Derive the position from the counts again** (maintainer, 2026-10-06: *try to reintroduce it after
  optimisation*) — **tried 2026-10-07, not adopted.** It would drop the 8 bytes of `InternalX/Y`. Retried with the
  optimised arithmetic (32-bit lengths, widening multiplies, the Newton root) it still costs **+696 bytes of flash**
  (`Motion_SeedFromSteps` is 774), against 718 the first time, for a worst error of 53 µm. The formula needs a 64-bit
  product and a 64-bit division, and AVR spends ~700 bytes on those however they are written. The one resource it would
  save, EEPROM, is the one this chip has to spare. Kept as `docs/design-notes/experiments/derive-position-from-steps.patch`, with the
  full comparison in `docs/design-notes/experiments/README.md`.
