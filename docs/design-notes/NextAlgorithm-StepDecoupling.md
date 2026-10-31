# Next algorithm — decoupling motor steps from Cartesian waypoints

Written 2026-09-18, to be implemented **on its own git branch** off
the code of that day.

> **Superseded 2026-10-06.** The 16 kHz fixed-tick
> DDA described below was **rejected** on a measured number: the ISR at
> 4 kHz costs ~125 cycles per tick (3.1 % of the CPU, `tools/analysis/cyclesim/t_isr.c`),
> so 16 kHz is 12.5 % **before** the DDA is added. The maintainer's
> variable-period Timer1 replaces it — step events precomputed by `Motion`,
> an ISR that runs once per event — specified in
> `requirements/design/REQ-CoreLR-D-Stepper-0010…0014` and
> `REQ-CoreLR-D-Motion-0018…0020`. The diagnosis in section 1 and the
> case for coarse waypoints in section 2 stand; the 4 kHz/8 kHz/16 kHz
> jitter table does not apply to the new design. Kept for the reasoning.

Figures marked *measured* come from the bench or from host simulation of
the real integer code. Everything else is a design estimate and must be
confirmed.

---

## 1. Why the current pipeline has hit its floor

Today one queue entry is produced per Cartesian waypoint, and carries
both motors' coil bits plus a hold:

```
  Motion_Task()                              ISR (4 kHz, Timer2)
  ------------                               -------------------
  one 6 um Cartesian grid step
    -> Motion_XYToLR()   2x ISqrt(uint64)
    -> quantize to steps, hysteresis
    -> ONE queue entry: coil bits + hold  ->  pop when hold expires
                                              apply both motors' bits
```

Two consequences, both now measured:

**A motor can only step at a waypoint boundary.** Its step instants are
therefore quantized to the waypoint period (278 µs at `VMAX`), which
leaves **7.2 / 9.6 / 8.1 % RMS jitter** (X / Y / 45°, *measured*)
between consecutive steps of one motor. That is what the maintainer hears as
residual growl. The L/R adjustment jog, which dithers its own holds to
1/256 tick, has 0 % and is audibly silent — that contrast is what
located the cause.

**The Cartesian grid is the throughput cost.** Driving the jitter down
by subdividing the grid cost 4× the waypoints each time, and the last
step (1/4 → 1/8) produced an *open regression*: the Cartesian jog got
**slower**, not faster, despite `VMAX` rising. See
`docs/bringup/Bringup-Status.md` — the hypothesis is that waypoint generation
became CPU-bound at 3583 waypoints/s ≈ **7167 `ISqrt(uint64_t)` per
second** on a 16 MHz ATmega328P.

Both levers inside the current architecture have been measured and are
exhausted:

| lever | measured result |
|---|---|
| finer grid (1/4 → 1/8) | 13.5 % → 7.2 %, but doubled CPU and caused the regression |
| faster fixed ISR (4 → 16 kHz) | ~1 point of 13.5 — the 1/256 hold dither already does that job |
| variable-period timer | ~1 point, same reason |

---

## 2. The proposed algorithm

Separate the two jobs the waypoint currently does at once — *where the
head goes* and *when each motor steps*.

```
  Motion_Task()                          ISR
  ------------                           ---
  one COARSE Cartesian step              per-motor DDA:
    (~4 motor steps, ~215 um)              spread dL steps of L and
    -> Motion_XYToLR()  2x ISqrt           dR steps of R evenly across
    -> ONE segment entry:                  the segment's duration,
         dL, dR (signed), duration         each on its own tick
```

The Cartesian interpolation keeps its job: deciding the path. The ISR
gains a new one: deciding *when* within the segment each motor moves.

### What this buys

**Jitter stops depending on the Cartesian grid.** A motor's step instant
is quantized to the ISR tick, not the waypoint:

| ISR | quantization | jitter on a 2.47 ms step interval |
|---|---|---|
| 4 kHz | 250 µs | 10.1 % — *no better than today* |
| 8 kHz | 125 µs | 5.1 % |
| **16 kHz** | **62 µs** | **2.5 %** |

**Note the first row.** A DDA at the current 4 kHz does **not** improve
anything — it trades a 278 µs boundary for a 250 µs one. The DDA only
pays off with a finer time base. This corrects the impression left by
the previous session's note, where the DDA was described as the fix on
its own.

**The CPU cost collapses**, which is what makes the faster ISR
affordable and should also settle the open regression:

| Cartesian grid | waypoints/s at `VMAX` | `ISqrt`/s |
|---|---|---|
| current, 1/8 step (6 µm) | 3583 | 7167 |
| 1 step (53.7 µm) | 400 | 801 |
| **4 steps (215 µm)** | **100** | **200** |
| 8 steps (429 µm) | 50 | 100 |

A 36× reduction at 4 steps. Path accuracy is unaffected on straight
lines (the line *is* straight between waypoints); on arcs the sagitta at
215 µm on a 50 mm radius is ~0.1 µm, far below everything else.

### Recommended first configuration

- Cartesian grid **4 motor steps**
- Timer2 ISR raised **4 kHz → 16 kHz** (prescaler 8, `OCR2A` 124 —
  `16e6/(8·125)` is exact, and the existing `#error` guards in
  `IsrDispatch.c` check exactly this)
- Per-motor DDA in the ISR

Keep the `Servo` library for now. A variable-period Timer1 remains
available later if 2.5 % still growls — the maintainer's observation stands
that the pen only takes two positions, so `Servo` can be replaced to
free the only 16-bit timer, which would also recover flash.

---

## 3. What it touches

- **`Stepper` entry format.** Today 3 bytes: combined coil bits,
  14-bit hold, 2-bit pen (`REQ-CoreLR-D-Stepper-0001`…`0009`). It
  becomes a *segment*: signed `dL`, `dR`, a duration, and the pen field.
  The adjacent-entry merge (`REQ-CoreLR-D-Stepper-0009`) must be
  rethought — merging segments is not the same operation.
- **Coil bits move into the ISR.** The phase comes from the absolute
  step count (`Motion_StepToCoilBits`, a `PROGMEM` 4-phase table), so
  the ISR must now track each motor's step count and derive the bits
  itself. Keep deriving from the absolute count — never a tracked
  toggle; that principle has already caught two defects.
- **The hysteresis stays** (`REQ-CoreLR-D-Motion-0015`), still at
  waypoint rate, still one step per waypoint per motor — but that clamp
  must be relaxed, since a coarse waypoint legitimately moves several
  steps now. The reason for the clamp was that a multi-phase jump in one
  *entry* is ambiguous in direction; with a DDA the ISR emits the steps
  one at a time, so the hazard disappears. **Verify this explicitly.**
- **`SysTick`** is derived from the ISR frequency by macro with three
  `#error` guards. 16 kHz / 100 = 160 — check it against
  `SYSTICK_TICKS_MAX`.
- **Hold-field range.** At 16 kHz the 14-bit field covers 1.02 s; the
  pen settle is 300 ms, so it still fits, but the `EnqueueSplit`
  backlog path must be re-checked.
- **The adjustment jog** drives `Stepper` directly and must keep
  working — it is the one mode confirmed good on hardware, and it is the
  reference against which smoothness is judged.

## 4. Budgets to respect

| | now | limit |
|---|---|---|
| Flash | 28182 (91 %) | 30720 — **the tighter one** |
| Static RAM | 1710 (83 %) | 2048 |
| Worst-case stack | 296 | 338 free → **+42** |

Arcs (`G2`/`G3`) are still unwritten and will cost flash. The ISR grows
in this change; the stack margin is measured by
`tools/analysis/stack_depth.py` over an `avr-objdump -d` of the linked `.elf`
(roots `main` and `__vector_7`).

## 5. Order of work

1. **First, settle the open regression** (`docs/bringup/Bringup-Status.md`): read
   `miss:` on the Cartesian jog screen, which was added for exactly
   this. It tells you whether the 1/8 grid starved the queue. Knowing
   the answer shapes the grid choice above.
2. Branch.
3. ISR to 16 kHz alone, rescaling the per-tick constants
   (`MOTION_VMAX_V16`, `MOTION_VMIN_V16`, `MOTION_JUNCTION_JERK_V16`,
   `MOTION_ACCEL_NM_PER_TICK2`). Verify the machine still behaves —
   this is a pure rescale and should change nothing audible.
4. Coarsen the Cartesian grid. Expect the jitter to get *worse* at this
   point; that is expected, the DDA has not landed yet.
5. The DDA itself.
6. Re-measure jitter, `miss:`, flash, RAM, stack.
