# Design Requirements — Stepper

Module: **Stepper** — a single, unified queue of combined
motor-output-plus-pen-action entries, drained by a 4kHz ISR that
applies each entry to the physical motor pins via compile-time
per-wire set/clear macros.

Realizes: [`REQ-CoreLR-F-Actuation-0001`, `-0002`](../functional/REQ-CoreLR-F-Actuation.md),
[`REQ-CoreLR-F-PenControl-0004`](../functional/REQ-CoreLR-F-PenControl.md)
(pen/motion synchronization)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: **Settled design, 2026-09-06**, after five rounds of code
review (per-motor two queues → unified queue with runtime pin
resolution → compile-time pin macros with a packed 32-bit entry →
absolute pen state instead of a toggle → auto-derived port/bit,
board-target guard, moved out of the shared header into
`Stepper.c`). Timer2 is now configured (see below). Only
the queue depth remains open.

## Why a single unified queue (not one per motor)
Two independent per-motor queues could silently desynchronize (nothing
guaranteed both `Enqueue()` calls for the same waypoint succeeded
together). A single combined queue, one entry per combined motor
state, makes this structurally impossible: there is exactly one
write/read index pair.

## Combined entry structure (32 bits)

## REQ-CoreLR-D-Stepper-0001 — Step entry structure
<a id="REQ-CoreLR-D-Stepper-0001"></a>

> **Layout kept, unit changed 2026-10-06** — see `-0010`: the hold is now in Timer1 counts (0.5 µs), range 8.19 ms.
Each queue entry packs, in 4 bytes:
- **1 byte** — both motors' coil bits combined (4 wires each; bit
  layout: bits 0-3 = Motor L wires 0-3, bits 4-7 = Motor R wires 0-3).
  Two motors of 4 wires each fit exactly one byte — storing them as two
  separate bytes (an earlier revision's approach) wasted one.
- **14 bits** — hold duration in ISR ticks (0-16383, ~4.1s at 4kHz),
  packed into the low 14 bits of a `uint16_t`. Far more headroom than
  an 8-bit hold count (~64ms) — e.g. the pen settling delay
  (`REQ-CoreLR-F-PenControl-0003`, ~300ms ≈ 1200 ticks) now fits in a
  **single** entry instead of being split into ~5.
- **2 bits** — a `Stepper_PenAction_t` value (`NONE`=0/`UP`=1/`DOWN`=2),
  stored **directly as the actual target state**, packed into the top 2
  bits of that same `uint16_t`. Confirmed 2026-09-06: an earlier attempt
  used a single toggle bit (direction inferred by flipping a tracked
  baseline) — rejected on defensive-programming grounds, since a
  skipped or reordered entry would leave that baseline silently wrong
  forever after. Storing the absolute state instead means every entry
  is self-describing: applying it always produces the correct result
  regardless of history. This is the same principle already applied to
  position (`REQ-CoreLR-D-Motion-0002`/`-0004`: absolute
  transform per waypoint, never delta-accumulated) — applied
  consistently here too.
The entry is therefore **3 bytes**. It carried a 4th reserved byte
until 2026-09-10, kept so the array stride would be a power of two --
letting the compiler index with a shift instead of a multiply. Dropped
on maintainer review: with RAM the binding constraint (85% used), 16
bytes of queue matter more than that multiply, which on AVR is itself
just a shift plus an add. Measured: **-17 bytes of RAM for +4 bytes of
flash**.

```c
typedef struct
{
   uint8_t  MotorCoilBits;   /* bits 0-3 = Motor L wires, bits 4-7 = Motor R wires */
   uint16_t HoldTicksAndPen; /* bits 0-13 = hold ticks (0-16383);
                                bits 14-15 = Stepper_PenAction_t, stored directly */
   uint8_t  Reserved;        /* always 0 for now; explicit headroom, not padding */
} Stepper_Entry_t; /* __attribute__((packed)), 4 bytes */
```

**Naming note:** the original source note's `PortD`/`StepperJob_t`/
`QueueHead_t`/`JobPool` names were illustrative examples in
the original source note, not a mandated API — every field/type name
here describes what it actually holds.

## REQ-CoreLR-D-Stepper-0002 — Queue structure
<a id="REQ-CoreLR-D-Stepper-0002"></a>
```c
typedef struct
{
   uint8_t WriteIndex;
   uint8_t ReadIndex;
} Stepper_QueueIndices_t;

typedef struct
{
   Stepper_QueueIndices_t Indices;
   Stepper_Entry_t Entries[STEPPER_QUEUE_SIZE];
} Stepper_Queue_t;
```
A single instance exists (not one per motor). Filled only by the
enqueuing API below; drained only by the ISR — single-producer/
single-consumer, no locking required.

## REQ-CoreLR-D-Stepper-0003 — Enqueuing API and hold-splitting
<a id="REQ-CoreLR-D-Stepper-0003"></a>
```c
uint8_t Stepper_RequestMotion(uint8_t lCoilBits, uint8_t rCoilBits,
                                       uint32_t holdTicks, Stepper_PenAction_t penAction);
uint8_t Stepper_RequestPenOnly(Stepper_PenAction_t penAction, uint32_t holdTicks);
```
The public API keeps explicit `Stepper_PenAction_t` (`NONE`/`UP`/`DOWN`)
and separate per-motor coil bytes — callers (`Motion`,
`PenControl`) reason per-motor; packing into one coil byte is this
module's own internal storage optimization, invisible to callers. The
`Stepper_PenAction_t` value itself is stored **unchanged** (not
re-encoded as a toggle) — see -0001.

`holdTicks` takes a wider type than the entry can hold; the module
splits any request exceeding `STEPPER_MAX_HOLD_TICKS` into multiple
consecutive entries with the same coil bits, with `penAction` set only
on the **first** split entry (subsequent split entries carry `NONE`).

`Stepper_RequestPenOnly()` reuses the **last successfully
enqueued coil bits** (tracked internally) — this is how a pen-only
command (no motion) is represented.

### Resumable backlog (revised 2026-09-08, maintainer review)
A request whose split doesn't fully fit the queue in one call is no
longer an all-or-nothing failure. Once its first chunk lands, the
request is **accepted** — the function returns 1 — even if the tail
doesn't fit yet:
- The unenqueued remainder (coil bits + tick count) becomes a
  one-request-deep **pending backlog**, tracked internally
  (`s_pendingCoilBits`/`s_pendingTicksRemaining`).
- `Stepper_Task()` (new, called every `loop()` iteration like
  every other module's `_Task()`) opportunistically drains this backlog
  as soon as queue space frees up, without needing another `Enqueue*()`
  call to come in.
- A **new** request is never interleaved with a still-draining backlog
  from an **older** one (they may carry different coil bits — physically
  different motor states, which cannot be mixed): any pending backlog
  is drained first; if it does not fully drain, the new request is
  rejected outright (0, nothing enqueued), exactly like an ordinary
  queue-full.
- Returning 1 means "durably registered for eventual execution", not
  "physically complete" — `Stepper_IsEmpty()` was widened to
  mean "ring buffer empty *and* no backlog pending" for a caller that
  needs to know when everything has truly finished.

**Rationale:** an earlier "all-or-nothing" fix was considered first
(pre-check whether the *whole* split fits before writing any of it) and
rejected: it would make a request longer than the queue's total
capacity fail forever, no matter how many times it's retried — a real
concern once `G4` (dwell, `REQ-CoreLR-F-GCodeInterpreter`, deferred)
needs multi-second-to-minutes holds far exceeding what even a fully
empty queue can hold in one go. The accept-then-drain-in-background
model handles that case correctly: genuine incremental progress instead
of a request that can never fit atomically.

**Concurrency note:** `s_pendingCoilBits`/`s_pendingTicksRemaining` are
touched only from main-loop context (`Enqueue*()`, `DrainPending()`,
`Stepper_Task()`) — never from the ISR. `Stepper_Tick()`
(the ISR) uses a private, ring-buffer-only emptiness check instead of
the public `Stepper_IsEmpty()`, specifically because
`s_pendingTicksRemaining` (`uint32_t`) is not atomically readable from
an ISR on 8-bit AVR.

This module keeps **no** pen-state tracking variable at all (neither
enqueue-side nor ISR-side) — every entry says exactly what the target
state is, so there is nothing to desynchronize.

## Pen/motion synchronization

## REQ-CoreLR-D-Stepper-0004 — Pen action application (realizes REQ-CoreLR-F-PenControl-0004)
<a id="REQ-CoreLR-D-Stepper-0004"></a>

> **Superseded 2026-10-06 (implemented the same day)** by `REQ-CoreLR-D-PenControl-0003`: the pen action is no longer raised as a flag and polled from `loop()`; the step ISR hands it straight to the servo driver, which `PenControl` now owns on Timer2.
When the ISR makes an entry current whose stored `Stepper_PenAction_t`
is not `NONE`, it raises a `volatile` pending-action flag carrying that
value **as-is** — it does **not** call the servo library directly from
interrupt context (`Servo` manages its own timer and should not be
driven from inside another ISR). A separate `PenControl` task,
polled from `loop()`, consumes that flag and performs the actual servo
write. This guarantees the pen physically moves only once the motion
stream has reached the exact point it was commanded at.

## Physical pin drive: compile-time per-wire macros (not runtime resolution)

## REQ-CoreLR-D-Stepper-0005 — Compile-time set/clear macros, auto-derived per wire
<a id="REQ-CoreLR-D-Stepper-0005"></a>
`HwConfig.h` states only the raw pin number per wire (e.g.
`STEPPER_MOTOR_L_PIN0 = 2`) — it is a pure hardware-facts header, with no
register-level knowledge and no AVR dependency (confirmed 2026-09-06).
Everything below is **private to `Stepper.c`**
(minimum-scope principle — nothing else in the project needs
register-level pin access):

**Board-target guard**, first, so the wrong target fails loudly rather
than silently deriving a wrong port:
```c
#if !(defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__))
#error "assumes the ATmega328(P) PORTD(0-7)/PORTB(8-13) pin layout (Arduino Uno/Nano)"
#endif
```
This checks the actual MCU, not a specific board name — it covers both
Uno and Nano (same chip, same standard pin-to-port mapping) while
rejecting Mega, Leonardo, Due, ESP32, etc., whose pin-to-port mapping
(or absence of AVR-style `PORTx`/`DDRx` registers entirely) would make
everything below simply wrong.

**Per-wire port/DDR/bit, derived automatically** from the raw pin
number via `#if`, not written out by hand — removing the possibility
of a human pairing the wrong port with a pin (an earlier revision's
manually-written association could only be caught *after the fact* by
an `#error` check; this makes the mistake structurally impossible):
```c
#if !(STEPPER_MOTOR_L_PIN0 >= 0 && STEPPER_MOTOR_L_PIN0 <= 13)
#error "STEPPER_MOTOR_L_PIN0 must be a PORTD (0-7) or PORTB (8-13) pin"
#endif
#if STEPPER_MOTOR_L_PIN0 < 8
#define MOTOR_L_WIRE0_PORT PORTD
#define MOTOR_L_WIRE0_DDR  DDRD
#define MOTOR_L_WIRE0_BIT  STEPPER_MOTOR_L_PIN0
#else
#define MOTOR_L_WIRE0_PORT PORTB
#define MOTOR_L_WIRE0_DDR  DDRB
#define MOTOR_L_WIRE0_BIT  (STEPPER_MOTOR_L_PIN0 - 8)
#endif
/* ...repeated per wire, 8 wires total */
```
The upper bound is **13, not 15**: on the Uno/Nano, `PORTB` only
exposes bits 0-5 as Arduino pins (`D8`-`D13`) — `PB6`/`PB7` are the
crystal, not GPIO. A pin number of 14 or 15 is actually `A0`/`A1`
(`PORTC`) in Arduino's extended numbering, which this scheme does not
handle — allowing the range up to 15 would have silently derived an
invalid `PORTB` bit for such a pin.

**Generic `SET()`/`CLR()`/`DDR_SET()` macros**, expressed only in terms
of the per-wire `_PORT`/`_DDR`/`_BIT` macros above — no register name
duplicated across the three of them, so they never need editing when a
wire moves port (only its pin number does, and the `#if` above then
picks the right port/DDR/bit for it automatically):
```c
#define MOTOR_L_WIRE0_SET()     (MOTOR_L_WIRE0_PORT |= (uint8_t)(1u << MOTOR_L_WIRE0_BIT))
#define MOTOR_L_WIRE0_CLR()     (MOTOR_L_WIRE0_PORT &= (uint8_t)~(1u << MOTOR_L_WIRE0_BIT))
#define MOTOR_L_WIRE0_DDR_SET() (MOTOR_L_WIRE0_DDR  |= (uint8_t)(1u << MOTOR_L_WIRE0_BIT))
```

The bit index is still a compile-time constant on a low I/O register,
so avr-gcc emits the atomic single-cycle `sbi`/`cbi` instruction for
each — no runtime pin/port resolution, no cached-pointer RAM (stronger
than an earlier revision's runtime-cached-pointer approach, itself safe
only because it ran inside the ISR).

## REQ-CoreLR-D-Stepper-0006 — Tick sequencing
<a id="REQ-CoreLR-D-Stepper-0006"></a>

> **Superseded 2026-10-06 (implemented the same day)** by `-0011`: no fixed tick any more; the ISR runs once per queue entry, on Timer1.
On each 4kHz tick, `Stepper_Tick()` shall, in order:
1. Apply the entry that became current at the *previous* tick, via the
   8 per-wire SET/CLR macros (-0005).
2. Check whether the current entry's hold counter has reached zero.
3. If so, dequeue the next entry; if its stored pen action is not
   `NONE`, raise the pending-action flag with that value (-0004).
4. If the queue is empty, keep holding the current entry's coil bits —
   the pen action does not re-fire in this case.

**Revised 2026-09-09 — this module no longer owns the timer or the
interrupt vector.** `Stepper_Tick()` is called from
[`IsrDispatch`](REQ-CoreLR-D-IsrDispatch.md), which owns the timer
configuration and the vector, and dispatches motion **first** on every
tick (`REQ-CoreLR-D-IsrDispatch-0002`). This module's `_Init()`
therefore no longer configures Timer2, and the ISR frequency tunable
moves with the timer (`ISRDISPATCH_FREQ_HZ`).

**Rationale:** owning the vector also made this module the firmware's
de-facto clock provider, so any unrelated module needing a time base had
to depend on it — see `REQ-CoreLR-D-IsrDispatch-0001`. The sequencing
above is unchanged; only who calls it changed.

## REQ-CoreLR-D-Stepper-0007 — Stop mechanism (V1)
<a id="REQ-CoreLR-D-Stepper-0007"></a>
`REQ-CoreLR-F-Actuation-0002` (stop behaviour) is realized, for V1, by
simply halting `Stepper_RequestMotion`/`EnqueuePenOnly`. The
last enqueued entry runs to completion and the motors then hold their
last commanded position. Reused by `REQ-CoreLR-D-GCodeParser-0005` to
halt a job on an invalid line.

**When the gate may be closed (found on the bench, 2026-10-10).** The gate refuses *every* motion request, including the entries of the deceleration ramp that
`Motion_Halt()` plans (`REQ-CoreLR-D-Motion-0012`), and `Motion_IsHaltRampDone()` becomes true only once Motion has *enqueued* the last of them. Closing the gate
together with `Motion_Halt()` therefore stops the machine **dead** at the last entry already queued and leaves the ramp unfinishable: the job state never left
HALTING / CANCELLING / ERRORSTOP. The gate is closed **at the end of the ramp**, never before (`REQ-CoreLR-D-Motion-0026`, `grbl_corelr.ino`, checked by
`tools/analysis/check_halt_order.py`).

## Safety invariant: read-modify-write on shared I/O ports

## REQ-CoreLR-D-Stepper-0008 — RMW safety invariant (documentation requirement)
<a id="REQ-CoreLR-D-Stepper-0008"></a>
Per-wire `sbi`/`cbi` writes (-0005) are genuinely atomic (single
instruction, compile-time-constant bit), so they need no additional
protection at all. The remaining RMW-safety concern is narrower than in
earlier revisions, but still real for the one pin sharing a port with
motor wires that has **no** hardware override: the SD chip-select on
`D10` (`PORTB` bit 2). This must always be toggled via
`digitalWrite()`/`pinMode()` (never a raw register write) — Arduino's
`digitalWrite()` already wraps its own read-modify-write in a
`cli()`/`sei()` critical section specifically to guard against an ISR
firing mid-sequence, which is what makes the SD library's own CS
toggling safe against this module's ISR. Hardware peripheral pins
sharing these ports (`D0`/`D1` = UART, `D11`-`D13` = SPI) are
additionally protected at the hardware level once enabled.

**If this invariant is ever broken** (a future change adds a raw
`PORTB`/`PORTD` read-modify-write anywhere outside a `sbi`/`cbi`-style
single-bit macro), the hazard becomes real and must be re-analyzed.

## Cross-context state (why each one is or isn't protected)
- `s_pendingPenAction`: written by the ISR, read-and-cleared by
  `Stepper_TakePendingPenAction()` from `loop()` context —
  **needs** a real `cli()`/`sei()` critical section (a naive
  read-then-clear could drop an action the ISR sets in between).
- `Indices.ReadIndex`/`WriteIndex`: each is written from exactly one
  context (`ReadIndex` only by the ISR, `WriteIndex` only by
  `Enqueue*()`); a cross-context read of a single byte is atomic on AVR
  (no tearing) — no section needed.
- No pen-state tracking variable exists anywhere in this module (-0001,
  -0004) — every entry stores the actual target state directly, so
  there is nothing to keep in sync across contexts for that concern.

## Timer2 configuration (confirmed 2026-09-06) — superseded 2026-10-06 by `-0011`; Timer2 is now owned by `IsrDispatch` at 1 kHz, see `REQ-CoreLR-D-IsrDispatch-0003`
Arduino Uno and Nano both run their ATmega328P off a fixed 16MHz
external crystal (`F_CPU = 16000000`, set by the Arduino build system
from the selected board — not software-configurable). Timer2, CTC mode,
prescaler 32, `OCR2A = 124` divides `F_CPU` for
`STEPPER_ISR_FREQ_HZ` (4kHz) **exactly**, no rounding:
`16000000 / (32 × 125) = 4000`. Guarded at compile time
(`Stepper.c`): a `#error` if `F_CPU` isn't 16MHz, a
`#error` if the prescaler/frequency combination doesn't divide evenly,
and a `#error` if the computed `OCR2A` doesn't fit in 8 bits — so a
future change to `STEPPER_ISR_FREQ_HZ` that breaks the exact
division fails the build instead of silently drifting off-frequency.

## REQ-CoreLR-D-Stepper-0009 — Adjacent entries with identical coil bits shall be merged (maintainer proposal 2026-09-09, implemented 2026-09-10)
<a id="REQ-CoreLR-D-Stepper-0009"></a>

> **To be removed (decided 2026-10-06, prototype).** In the prototype every entry follows a step event, so no two adjacent entries share coil bits (0 in 7.7 M); the only ones that could are the pieces of a split hold, which are already full. The merge no longer pays for its code and is **deleted** at implementation, not kept. (Not to be confused with the junction look-ahead, `REQ-CoreLR-D-Motion-0013`, which is unaffected.)
Two consecutive queue entries carrying the **same** `MotorCoilBits` are
physically indistinguishable from a single entry holding those bits for
the sum of their hold times. Enqueuing both wastes a queue slot (the
queue is only `STEPPER_QUEUE_SIZE` = 16 entries) and an ISR dequeue,
for no motion.

```
   before merging                 after merging

   [coils=X, T1, NONE]            [coils=X, T1+T2, NONE]
   [coils=X, T2, NONE]

   two slots, identical output    one slot, identical output
```

The module shall therefore **not** append an entry whose coil bits equal
those of the entry already at the tail of the queue; it shall instead
add the new hold time to that entry.

### Condition 1 (mandatory): never move or drop a pen action
An entry carries a `Stepper_PenAction_t` that fires when the ISR reaches
it. **A merge shall be performed only when the incoming entry's action
is `NONE`.** Enumerating every case shows why that single test is both
necessary and sufficient:

| Tail entry | Incoming entry | Merge? | Why |
|---|---|---|---|
| `NONE` | `NONE` | **yes** | nothing to preserve |
| `UP`/`DOWN` | `NONE` | **yes** | the tail's action stays at the start of the merged entry, exactly where it already was |
| `NONE` | `UP`/`DOWN` | **no** | the action would fire `T1` ticks early |
| `UP`/`DOWN` | `UP`/`DOWN` | **no** | one of the two actions would be lost — the field holds a single value |

Firing early would break the pen/motion synchronisation that
`REQ-CoreLR-F-PenControl-0004` exists to guarantee, and which is the
whole reason pen actions travel inside the queue instead of being
applied at parse time. (In practice case 4 should not arise:
`PenControl`'s request functions are idempotent. The test blocks it
regardless — a merge must never depend on a caller's discipline.)

### Condition 2: the internal hold-field limit shall not refuse a merge
An earlier draft of this requirement said to skip the merge when
`T1+T2` exceeds `STEPPER_MAX_HOLD_TICKS`. **That was wrong** (corrected
2026-09-09 on maintainer review): the public contract takes a
**32-bit** `holdTicks`, and the 14-bit packing is an internal storage
detail the caller cannot see and must not be penalised for.

The correct behaviour is to **fill the tail entry to capacity and split
the remainder**, exactly as `EnqueueSplit()` already does for an
oversized request:

```
   tail = [coils=X, T1]      incoming: coils=X, T2

   room = STEPPER_MAX_HOLD_TICKS - T1
   tail becomes [coils=X, T1 + min(T2, room)]
   if T2 > room: append the remaining (T2 - room) through the normal
                 split path, as further entries with the same coil bits
```

This never refuses a merge, and it uses the room already available in
the tail slot instead of wasting it — strictly better than appending a
fresh entry while the tail still has capacity.

### Placement: queue-side, not producer-side
The merge belongs in `Stepper_RequestMotion()`/`_RequestPenOnly()`
rather than in `Motion`, because:
- it is an invariant of the queue ("no two adjacent entries with
  identical coil bits"), not of any one producer;
- it then benefits **every** producer — `Motion`'s waypoints,
  `PenControl`'s pen-only entries, and `LcdJoystickUI`'s Adjustment-jog
  raw steps — with one implementation.

**Concurrency, and why the race is benign.** Merging modifies the
*last written* slot, which the ISR may eventually dequeue. Guard:
perform the merge only when that slot is **not** the next one to be read
(`lastWritten != Indices.ReadIndex`; `ReadIndex` is a single byte, so
atomically readable from main-loop context — see `-0008`). With that
guard the ISR must still fully consume at least one intervening entry,
i.e. at least one hold period (≥250µs at 4kHz), before it can reach the
slot — orders of magnitude more than the few instructions the merge
takes.

If the ISR nonetheless advances between our read of `ReadIndex` and our
write, the write lands on an already-dequeued slot. That is **harmless,
not corrupting**: the ISR copies the entry into `s_currentEntry` when it
dequeues, so the slot's contents no longer matter, and `WriteIndex` has
not advanced so the slot will be overwritten by the next append anyway.
The only consequence is a missed merge. No critical section is therefore
required.

### Which regime are we in? — starving vs. saturated, and how to settle it
Open question raised by the maintainer 2026-09-09: is `loop()` more
likely to **starve** the queue (motion stutters because entries are not
produced fast enough) or to be **blocked** waiting for the queue to
accept? It matters because merging pays off differently in each, and
because the concurrency guard above only bites in one of them.

**Closed-form answer (computed 2026-09-09).** Production is one entry
per `loop()` iteration (`Motion_Task()` yields one waypoint per call);
consumption is one entry per `holdTicks`, i.e.
`STEPPER_ISR_FREQ_HZ / (MOTION_GRID_UM / VMAX)` = **800 entries/s** at
maximum speed:

| `loop()` duration | Production | Regime |
|---|---|---|
| ~11.5 ms — today, full 32-char LCD redraw per iteration | 87 /s | **starved**, 0.11× |
| ~0.51 ms — after the frame buffer (`REQ-CoreLR-D-LcdJoystickUI-0011`) | 1961 /s | **saturated**, 2.45× |

So **both regimes exist, and the LCD fix flips us from one to the
other.** The maintainer's expectation ("we will mostly be waiting for the
stepper to accept, rather than starving it") is correct — for the state
*after* `-0011` is implemented, which is the state that matters. Today
the queue is severely starved and motion would visibly stutter, which
is the same finding that motivated `-0011` in the first place.

**Convenient consequence for the guard.** The concurrency guard
(`lastWritten != ReadIndex`) blocks precisely when merging is least
useful: with a full queue (saturated) `lastWritten` is far from
`ReadIndex`, so the guard passes and merging works; with a nearly empty
queue (starved) the guard may block, but in that regime the goal is to
*produce* more entries, not to compact them.

**Cost of the merge itself** is a few byte comparisons and one 16-bit
add — on the order of 1-2 µs at 16 MHz, against a 250 µs tick. Not a
factor in either regime; recorded so it is not re-litigated.

**How to confirm on real hardware.** Note that a scope/logic-analyser
approach is *not* available: the board has zero spare pins
(`../../docs/hardware/PinMapping.md`). Proposed instead, in order of value:

1. **Closed-form first** (above) — it is exact given `loop()`'s
   duration, and it already predicts the regime. The only uncertain
   input is that duration.
2. **Software counters shown on the LCD** — the cheap, pin-free way to
   measure the uncertain input and confirm the regime directly:
   - `loop()` iterations per second (production rate);
   - `Stepper_RequestMotion()` refusals per second (**saturation**
     indicator);
   - count of "queue empty while a segment is still in progress" seen
     by `Tick()` (**starvation** indicator — the one that corresponds
     to visible stutter).
   Roughly 6 bytes of counters plus a diagnostic screen, and it
   distinguishes the two regimes unambiguously. To be added as a
   temporary bring-up aid, not a permanent feature.
3. **Simulation — not recommended here.** It would only re-derive the
   closed form while adding a model to distrust; its uncertain input
   (real `loop()` duration, real I²C timing) can come only from
   measurement anyway.

### Consequence for the grid-size decision
This mechanism **removes the argument** that
`REQ-CoreLR-D-Motion-0006`'s rejected alternative (a finer
`53/sqrt(2)` grid) wastes ~30% of queue entries on axial moves: those
entries are exactly the ones merging eliminates. That rejection now
rests only on the remaining grounds recorded there (no `sqrt(2)`
rational approximation, and the exact "grid = one motor step"
statement) — see the correction noted in that requirement.

Measured 2026-09-09, merging is worthwhile **regardless** of grid
choice: even with the grid at exactly one motor step, 4.5–12% of
waypoints (depending on line angle) produce no coil-bit change at all,
purely through step-count rounding.

## v2 architecture — event-driven stepping (2026-10-06)

**Status of this section: approved in principle by the maintainer on
2026-10-06; values marked *proposed* are awaiting confirmation.**
It replaces the fixed 4 kHz tick of `-0006` and the Timer2 section above.
The decision, and why, in short:

| | fixed tick (v1) | event-driven (v2) |
|---|---|---|
| step-time resolution | 250 µs (4 kHz) | **0.5 µs** (Timer1, prescaler 8) |
| ISR frequency | 4 000/s, always | one per queue entry: ≤ ~800/s at full speed |
| ISR cost | ~125 cycles/tick = **3.1 % CPU** (measured, `tools/analysis/cyclesim/t_isr.c`) | ~0.5 % *(estimate — to be measured)* |
| a 16 kHz fixed tick | 12.5 % CPU *before* any DDA (measured) | — |
| position/phase state in the ISR | none | none |

The fixed-tick alternative (a per-motor DDA in the ISR at 16 kHz) was
proposed first and **rejected on the measured ISR cost** above. The
variable-period timer is the maintainer's proposal, 2026-10-06.

Everything that decides *when* a motor steps is computed by `Motion`, in
the main loop, ahead of time (`REQ-CoreLR-D-Motion-0018`). The ISR does
nothing but apply the next entry and program the next compare.

## REQ-CoreLR-D-Stepper-0010 — Event entry: layout unchanged, unit changed (proposed 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Stepper-0010"></a>
An entry says: *at the instant you are popped, apply these coil bits and
this pen action, then hold for this long before popping the next one.*

The layout of `-0001` is **kept byte for byte** — 3 bytes: combined coil
bits, a 14-bit hold field, a 2-bit pen action. Only the **unit of the
hold changes**, from ISR ticks to **Timer1 counts** (0.5 µs).

| | v1 | v2 |
|---|---|---|
| hold unit | 250 µs | **0.5 µs** |
| field range | 16383 ticks = 4.1 s | 16383 counts = **8.19 ms** |
| `STEPPER_QUEUE_SIZE` | 16 | 16 *(unchanged — RAM neutral)* |

A longer hold is split across entries with identical coil bits
(`-0012`). At full speed a motor steps every ≥ 2.5 ms, so an entry
almost always carries its whole hold; slow moves and the 300 ms pen
settle use several. This costs queue slots only when the machine is
slow or still, which is exactly when entries are consumed rarely.

*(A 16-bit hold in a 4-byte entry was the alternative: +16 bytes of RAM
for a range the field does not need. Rejected on the RAM budget — the
stack margin is +42 bytes.)*

The public API keeps `Stepper_RequestMotion(l, r, hold, pen)` and
`Stepper_RequestPenOnly(pen, hold)`, with `hold` now in Timer1 counts
and still 32 bits wide; the resumable backlog of `-0003` is unchanged.

## REQ-CoreLR-D-Stepper-0011 — Timer1 compare-driven ISR (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Stepper-0011"></a>
`Stepper` owns **Timer1** and its compare-A vector. Timer1 runs
free (normal mode, prescaler 8, so 1 count = 0.5 µs at 16 MHz) and is
never reset; the compare register is always advanced **relative to its
own previous value**, never to the current counter:

```
ISR(TIMER1_COMPA_vect)
   pop next entry                  -- none queued: disarm, see -0013
   apply its coil bits             -- the 8 per-wire SET/CLR of -0005, unchanged
   hand its pen action over        -- REQ-CoreLR-D-PenControl-0003
   OCR1A += entry.hold             -- relative: ISR latency does not accumulate
   late check                      -- see below
```

**Why relative.** `OCR1A = TCNT1 + hold` would add the interrupt latency
to every interval, so the error would accumulate along the move.
`OCR1A += hold` makes every event land at its nominal instant plus its
own, non-accumulating, latency — and this is what makes the step-time
resolution equal the timer's, not the ISR's.

**Late check.** If the ISR ran so late that the new compare point is
already behind the counter, the compare would not fire again for a whole
counter wrap (32.8 ms) — a stalled motor. The check is done in
**unsigned** arithmetic on the time elapsed since the previous compare:

```
elapsed = (uint16_t)(TCNT1 - previousCompare);
if ((uint16_t)(elapsed + STEPPER_LATE_MARGIN_COUNTS) >= entry.hold)
    OCR1A = TCNT1 + STEPPER_LATE_MARGIN_COUNTS;      /* fire at once */
```

It is valid for any hold below 32768 counts, which the 14-bit field
guarantees. A late event is counted (diagnostic), not hidden.

**The ISR keeps no position or phase state.** The coil bits come
ready-made from `Motion`, which derives them from its absolute step
count (`REQ-CoreLR-D-Motion-0007`). There is therefore nothing in the ISR
to desynchronise from `Motion`, and no periodic consistency check or
recalibration is needed (maintainer's question, 2026-10-06).

Compile-time guards, carried over from `IsrDispatch-0003`'s three: the
clock is 16 MHz, the prescaler divides it exactly, and the resulting
count period is a whole number of nanoseconds.

## REQ-CoreLR-D-Stepper-0012 — Delay limits and the split rule (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Stepper-0012"></a>
Two limits bound every entry's hold:

| Limit | Value (*proposed*) | Meaning |
|---|---|---|
| `STEPPER_DELAY_MIN_COUNTS` | **64** (32 µs) | shortest hold the ISR can honour |
| `STEPPER_DELAY_MAX_COUNTS` | **16000** (8 ms) | longest hold put in one entry |

**Minimum.** The next compare must still lie in the future when the ISR
has finished. Worst chain: one queued Timer0 ISR + one queued TWI ISR
(the LCD) before ours, then our own ~8 µs — about 23 µs *(estimate from
reading the code, to be checked on the bench)*. 32 µs leaves margin, and
the late check above is the net under it. `Motion` is responsible for
never asking for less (`REQ-CoreLR-D-Motion-0018`, rule 3); `Stepper`
clamps up to the minimum and counts it as a diagnostic.

**Maximum, and the remainder.** A hold above the maximum is split. The
trap, raised by the maintainer: splitting greedily can leave a
remainder **smaller than the minimum**, which is itself unusable
(a delay of `MAX + 5` becomes `MAX` then `5`). The rule is therefore:

```
while (D > MAX):
    piece = MAX
    if (D - piece < MIN):  piece = D - MIN        /* leave a usable remainder */
    emit(piece);  D -= piece
emit(D)                                           /* here D >= MIN */
```

Example, with `MAX = 65535` and `MIN = 64`: `65540` becomes `65476 + 64`,
not `65535 + 5`. The rule needs `MAX >= 2*MIN`, checked at compile time.
`penAction` stays on the **first** piece only, as in `-0003`.

## REQ-CoreLR-D-Stepper-0013 — Arming, disarming, and the enqueue race (approved 2026-10-06, implemented 2026-10-06)
<a id="REQ-CoreLR-D-Stepper-0013"></a>
The compare interrupt is **armed only while there is something to pop**.
When the ISR finds the queue empty it disarms the interrupt and clears a
`volatile uint8_t armed` flag. The producer arms it again when it
enqueues:

```
write the entry; advance WriteIndex          /* publish first */
cli
if (!armed && queue not empty)               /* test INSIDE the section */
{
    OCR1A = TCNT1 + STEPPER_START_DELAY_COUNTS;   /* >= MIN */
    clear the pending flag; enable OCIE1A; armed = 1;
}
sei
```

Publishing first and testing inside the critical section closes the
race both ways: if the ISR saw the queue empty before the publish, it
has already cleared `armed` and the producer arms; if the ISR pops the
entry between publish and test, the producer sees the queue empty and
does not arm a timer with nothing to pop.

**Starvation.** An empty queue at a compare is a *normal end of motion*
if the producer has finished, and a **starvation** if the producer still
has work. The ISR cannot tell them apart, so the counter shown on the
LCD (`miss`) is counted on the **producer** side: `Motion` increments it
when it finds the timer disarmed while its own move is still in
progress. Starvation is not a stuttering catastrophe in v2 — it is a
pause, since entries carry durations and not repeated bits — but it
shifts the timeline, so it must be rare: a matter of queue sizing and of
`Motion`'s feeding target, to be checked on the bench.

**Atomicity.** `OCR1A`, `TCNT1` and the other 16-bit timer registers go
through the shared temporary register: they are touched only inside the
ISR or with interrupts disabled.

## REQ-CoreLR-D-Stepper-0014 — Motion starts only after the position is marked invalid (proposed 2026-10-06; implemented 2026-10-06)
<a id="REQ-CoreLR-D-Stepper-0014"></a>
`REQ-CoreLR-F-Calibration-0007` requires the persisted position to be
invalid **before the first step** that could move a motor. The single
place where a motion entry becomes visible to the ISR is
`Stepper_RequestMotion()`, so it is also the single place that enforces
this: when the entry's coil bits differ from the last enqueued ones, it
calls `Calibration_InvalidatePosition()` **before** publishing the entry.

That function is idempotent and costs a flag test when the position is
already invalid, which it is for every call but the first of a movement
episode. For the first one it writes one EEPROM byte and **waits for it
to complete** (≈ 3.4 ms, `REQ-CoreLR-D-Calibration-0006`) — the timer is
not armed until it returns, so no step can precede the invalidation.
Pen-only entries do not change the coil bits and never trigger it.

*Dependency note:* this makes `Stepper` call into `Calibration`, a
layering step down from a pure actuation module. It is accepted as the
only mechanism that makes the ordering structural rather than a
convention every caller must remember — the adjustment jog drives
`Stepper` directly and would otherwise have to remember it too.
*(Proposed; the alternative is a registered callback, at the cost of two
bytes of RAM and an indirect call per enqueue.)*

### Implementation notes, 2026-10-06 (Timer1 ISR)
- **Measured** on the cycle simulator (`tools/analysis/cyclesim/t_isr2.c`): the compare-A
  ISR costs **166–177 cycles (10.4–11 µs)** when it pops an entry, 70 when it
  finds the queue empty and disarms. At the worst-case 800 events/s that is
  **0.9 % of the CPU**, against the 0.5 % estimated above, and against
  3.1 % for the 4 kHz tick it replaces (12.5 % had it gone to 16 kHz). The cost
  is eight branches for the eight coil wires; masked port writes would be
  shorter but break the single-bit-write invariant of `-0008`.
- **The minimum delay is tighter than estimated.** The ISR itself is 11 µs,
  not 8: with a queued Timer0 and TWI ISR ahead of it the worst chain is
  ~26 µs against the 32 µs minimum. The late guard (`-0011`) is the net under
  that, and `s_lateCount` is the instrument; to be checked on the bench.
- **Verified on the host** (`tests/host/test_stepper_isr.c`: the real
  `Stepper.c` with Timer1 simulated count by count and a random interrupt
  latency): every event lands within the latency of its nominal instant and the
  error does not accumulate (worst 15 counts at 20, 29 at 40; last-entry error
  5 to 11); a latency far above the hold (150 counts against 64) cannot stall
  the timer; idle/arm/idle five times, with `Stepper_IsIdle()` false until the
  last hold has expired; the split rule on 399 936 requests (every piece within
  [64, 16000], sum exact, `MAX+5` never leaves a remainder below `MIN`); the
  300 ms pen settle through the backlog in 38 entries, pen action fired once,
  at the first entry's instant; the ports carry the entry's bits through the
  real wire macros; the position is invalidated before the entry is visible,
  and not for a pen-only entry. Five deliberately broken copies are each
  caught (absolute instead of relative compare; no late guard; greedy split;
  arm before publish; invalidate after publish).
- The merge of `-0009` is **deleted**, as decided. `Stepper_TakePendingPenAction()`
  and `Stepper_Tick()` are gone.
- **Starvation is counted by `Motion`** (`s_primed` + `Stepper_IsIdle()`), as
  specified. `s_primed` is cleared when the generator reports idle and when a
  halt ramp completes, so neither a normal end nor a pause counts.
- **Transitional:** the v1 generator still computes holds in 250 µs ticks; it
  multiplies by `MOTION_V1_TICK_COUNTS` (500) at its one call site, to go with
  the port to the event generator. The adjustment jog already computes its holds
  directly in counts, without the dither.

## REQ-CoreLR-D-Stepper-0015 — The step-starvation counter is a build option, off by default (implemented 2026-10-08)
<a id="REQ-CoreLR-D-Stepper-0015"></a>
Origin: maintainer, 2026-10-08, who saw `miss:` on the jog screens read 0 for good and proposed a configuration macro.

`Stepper_StarvedCount()`, `_ResetStarvedCount()` and `_CountStarvation()` (the counter of `-0013`), the producer's detection
in `Motion_Task()` (`s_primed`, one byte, and the test of the timer being idle), the `miss:N` display on the Control jog
(row 1, `jog    miss:N`) and the Adjustment jog (row 1, `SW=ok   miss:N`), and the two resets in `LcdJoystickUI`, all sit behind
**`CORELR_DEBUG_MISS_COUNT`** (`SwConfig.h`, group `SwConfig_Debug`; `-DCORELR_DEBUG_MISS_COUNT=1` overrides it). The default is
**0**: the counter was the bring-up instrument of the "jog got slower" regression (`docs/bringup/Bringup-Status.md`), cured by the v2
event generator, and has read 0 on the bench since. With it off the Control jog's row 1 shows the cable lengths, like the
status screen, and the Adjustment jog's a plain `SW=ok`. Turn it on to look for starvation again after retuning
`VMAX`/`ACCEL` or the queue depth.

**Measured** (Nano fqbn, whole image): off **30 000 bytes of flash, 1 348 of RAM**; on 30 050 and 1 350, i.e. the option is worth
**−50 bytes of flash and −2 of RAM**, and the "on" build is byte-for-byte the build before the option existed. Small, and
recorded as such.

**Verified**: all ten host tests pass under every combination of `CORELR_MATH_48`/`_64` and `CORELR_DEBUG_MISS_COUNT` 0/1
(`EXTRA_DEFS=-DCORELR_DEBUG_MISS_COUNT=1 tests/host/run_all.sh CORELR_MATH_48`); the hash of every entry sent to `Stepper`
is the same with the counter on and off (`3a04d9a6`, `a9cfe556`); `test_ui` checks what each jog screen shows in either case.

## Open items
- **Should a queued entry's hold time be capped, to bound halt latency?
  Analysed 2026-09-10 (maintainer question) -- conclusion: no cap
  for V1.** The concern is sound: `STEPPER_MAX_HOLD_TICKS` lets a single
  entry carry 16383 ticks (4.1 s), and a halt lets the queue finish
  rather than flushing it (`REQ-CoreLR-D-Motion-0012`), so a long hold
  sitting in the queue delays the stop. Three findings say a cap is not
  the right lever:
  1. **Capping per entry does not bound the latency.** A long request is
     *split* across entries (`EnqueueSplit()`), which preserves the
     total time. Only the granularity changes, and granularity would
     only help if the tail could be discarded -- which V1 deliberately
     does not do, because discarding queued entries is what would cost
     the machine its position.
  2. **A long hold means the machine is not moving.** A hold is by
     definition an unchanged coil pattern, so motion stops on schedule
     regardless. What a long queued hold delays is only the *pen lift*,
     during which the machine is already stationary.
  3. **Merging cannot accumulate a long hold while moving.** The merge
     (`REQ-CoreLR-D-Stepper-0009`) only absorbs an entry whose coil bits
     match the tail, i.e. a step that moves neither motor. A grid step
     is 0.99 motor steps, so at most a couple of consecutive grid steps
     can fail to move a motor -- long merges only form while genuinely
     dwelling.

  Today the only long hold any caller produces is the 300 ms pen settle
  (1200 ticks, one entry). Should a bounded pen-lift latency ever be
  wanted, the lever is a **direct servo write at standstill** -- what
  `PenControl_PreviewAngle()` already does for the pen wizard -- since
  synchronising the pen to motion is moot when nothing is moving. Not a
  hold cap.
- **Hold-field width: 6 bits instead of 14?** (maintainer idea,
  2026-09-10 — parked for discussion, not analysed.) The entry would
  shrink further, keeping the public 32-bit `holdTicks` and letting the
  split/backlog mechanism cover long holds. The maintainer's own reservation,
  recorded as stated: it would make the queue block far more often,
  since a 6-bit field caps one entry at 63 ticks (~16 ms at 4 kHz) and
  the pen settle time alone (300 ms ≈ 1200 ticks) would then need ~20
  entries out of 16 — i.e. it could not be expressed at all without
  relying on the backlog. Worth quantifying against the merge
  mechanism (`-0009`) before deciding, since merging pushes in the
  opposite direction (fewer, longer entries).

- **Queue depth — resolved 2026-09-07** (`STEPPER_QUEUE_SIZE` in
  `SwConfig.h`): 128 entries (512 bytes) was cut to 32 (128 bytes) as
  part of fixing a real compile-time RAM overflow (2570/2048 bytes,
  125%, with the LCD UI's menu state machine added). 32 entries is
  still several tens of milliseconds of lookahead at any realistic
  hold-tick count — comfortably more than one loop() iteration (SD
  read + LCD I2C write + parsing) should take — but this is reasoning,
  not a hardware measurement; flagged for empirical re-check once real
  timing is observable on hardware, same as every other placeholder
  timing constant in this project.
