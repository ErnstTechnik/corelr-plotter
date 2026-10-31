# Coding Standard — Module Design

## Instantiable modules use a handle as first parameter

A module that is **reusable or instantiable** — i.e. it can meaningfully
exist more than once (two UART channels, three PID controllers, several
sensor instances) — exposes its state as a **handle** passed as the
**first parameter of every one of its functions**. The handle is a
typedef for a pointer to the module's state struct.

This is deliberately "objects in C": the handle is the instance, the
functions are its methods, and the module's code exists once no matter
how many instances there are.

> **Naming:** the examples below follow
> [STD-Naming.md](STD-Naming.md) — everything is prefixed with the
> **module** name (`MotorDriver_`), every typedef ends in `_t`, and the
> nature of the type lives in its name (`_State_t` for the instance
> struct, `_Handle_t` / `_ConstHandle_t` for the handles). The include
> guard is shown in its *application-header* form (`FOO_MOTORDRIVER_H`,
> project-prefixed); were this module shipped as a reusable Arduino
> library, the guard would be `MOTORDRIVER_H` — file name only. See
> that file's include-guard section for why the two differ.

### Scope: when this rule does *not* apply
A module that is inherently a **singleton** — there is exactly one of
the thing, physically or logically — keeps plain module-scope `static`
state and does **not** take a handle. Examples: a single timer-driven
step queue, the one LCD on the board, the one SD-card reader. Adding a
handle there costs flash at every call site and buys nothing (measured
below).

Applying the rule to a singleton is the common over-engineering
mistake; applying it to a genuinely multi-instance module is the common
under-engineering mistake (which usually degenerates into copy-pasted
modules, or an index-into-internal-pool API — measurably worse, see
below).

### Header pattern

```c
#ifndef FOO_MOTORDRIVER_H
#define FOO_MOTORDRIVER_H

#include <stdint.h>

/**
 * @brief MotorDriver instance state.
 *
 * Defined in this header (not opaque) so a caller can allocate an
 * instance statically, without any heap -- see the allocation note
 * below. Treat every field as PRIVATE: only MotorDriver_*() functions
 * may read or write them. A caller reaching into a field directly is a
 * bug, even though the compiler permits it.
 */
typedef struct
{
   uint16_t StepCount;
   uint8_t  CoilBits;
   uint8_t  Reversed;
} MotorDriver_State_t;

/** @brief Handle to one MotorDriver instance. */
typedef MotorDriver_State_t *MotorDriver_Handle_t;

/** @brief Read-only handle -- see the `const` note below. */
typedef const MotorDriver_State_t *MotorDriver_ConstHandle_t;

/**
 * @brief Initialize one instance. Must be called before any other
 * function on this handle.
 * @param handle instance to initialize (must not be NULL).
 * @param reversed 1 to invert this instance's direction.
 * @return 1 on success, 0 on invalid argument.
 */
uint8_t MotorDriver_Init(MotorDriver_Handle_t handle, uint8_t reversed);

/** @brief Advance this instance by one step. */
uint8_t MotorDriver_Step(MotorDriver_Handle_t handle, int8_t direction);

/** @brief Read this instance's absolute step count (does not modify it). */
uint16_t MotorDriver_GetStepCount(MotorDriver_ConstHandle_t handle);

#endif /* FOO_MOTORDRIVER_H */
```

### Caller pattern — static allocation, no heap

```c
/* Two instances, both statically allocated -- no malloc anywhere. */
static MotorDriver_State_t s_motorL;
static MotorDriver_State_t s_motorR;

void Setup(void)
{
   MotorDriver_Init(&s_motorL, 0);
   MotorDriver_Init(&s_motorR, 1); /* this one runs reversed */
}

void StepBoth(void)
{
   MotorDriver_Step(&s_motorL, +1);
   MotorDriver_Step(&s_motorR, -1); /* same code, different instance */
}
```

The module's `.c` contains no `static` instance state at all — only the
functions. That is what makes it reusable: nothing to duplicate, nothing
to rename, no fixed maximum instance count baked in.

### The `const` trap — why two handle typedefs

Hiding the pointer inside the typedef breaks `const` in a way that is
easy to miss:

```c
/* WRONG -- does not mean what it looks like: */
uint16_t GetCount(const MotorDriver_Handle_t handle);
/*   expands to: MotorDriver_State_t *const handle
 *   = a CONSTANT POINTER to a MUTABLE struct.
 *   The function can still modify every field. */

/* RIGHT -- a genuinely read-only view: */
uint16_t GetCount(MotorDriver_ConstHandle_t handle);
/*   expands to: const MotorDriver_State_t *handle */
```

Because `const` applied to a pointer-typedef binds to the *pointer*,
not the pointee, a single handle typedef cannot express "read-only
instance". Declare **both** typedefs up front (as in the header above)
and use the `const` variant on every function that only reads — it
documents intent at the call site and lets the compiler enforce it.

Verified 2026-09-08 with avr-gcc: the `const MotorDriver_Handle_t`
form compiles **with no error and no warning** while assigning to a
struct member, whereas the `MotorDriver_ConstHandle_t` form
correctly fails with *"assignment of member ... in read-only object"*.
The trap is silent — the compiler will not warn you that your `const`
did nothing.

### Transparent vs. opaque state struct
Two options, with a clear embedded default:

- **Transparent** (struct defined in the header, as above) — the
  caller can allocate an instance statically. **Default choice for
  embedded**, because it needs no heap: the caller must know
  `sizeof()`, which requires the full definition. Cost: encapsulation
  is by convention (a documented "private" comment), not enforced by
  the compiler.
- **Opaque** (only `typedef struct MotorDriver_State MotorDriver_State_t;`
  in the header — an incomplete type, using a struct tag named per
  [STD-Naming.md](STD-Naming.md), i.e. the typedef minus `_t`; full
  definition in the `.c`) — real compiler-enforced encapsulation, but
  the caller can no longer allocate, so the module must hand out
  instances from an internal fixed pool
  (`MotorDriver_Acquire()`/`MotorDriver_Release()`) or use `malloc` —
  the latter being normally unacceptable on a small MCU (fragmentation,
  unbounded failure mode).

Pick transparent unless there is a specific reason for the pool, and
say which one was chosen in the module's header comment — so it reads
as a decision, not an accident.

### NULL-handle checking
State a per-project policy rather than deciding case by case. Either:
- **Validate** (`if (handle == NULL) { return 0; }` at the top of every
  public function — needs `#include <stddef.h>` in the implementation
  file for `NULL`, which `<stdint.h>` alone does not provide) — costs a
  few bytes of flash per function, catches caller bugs at runtime; or
- **Contract** (documented "must not be NULL", no runtime check) —
  zero cost, relies on review/testing.

A reasonable middle ground on a flash-constrained target: validate in
functions reachable from an untrusted/complex path, contract-only in
hot inner functions, and document which convention each module follows.

### Concurrency caveat
A handle-based API is **reentrant across different instances** — two
contexts may safely call the same function on two *different* handles.
It is **not** automatically safe on the *same* handle: two contexts
sharing one instance need exactly the same per-field concurrency
analysis as any other shared state (see
[STD-MemoryAndPerformance.md](STD-MemoryAndPerformance.md)'s atomicity
caveat). The handle pattern organizes state; it does not synchronize
it.

### Measured justification (avr-gcc 7.3.0, `-Os`, ATmega328P)
Struct of 5 fields, function accessing 4 of them:

| Variant | Function body | Per call site |
|---|---|---|
| Handle (pointer) | **42 B** | +4 B (two `ldi` to load the address) |
| Singleton, static struct | 44 B | 0 |
| Singleton, separate statics | 56 B | 0 |
| Multi-instance via index into internal pool | **52 B** | 0 |

- **For a genuinely instantiable module, the handle is the cheapest
  option**: 42 B vs 52 B for the index-into-pool alternative, because
  an index costs a multiply by `sizeof(struct)` on every access while a
  pointer *is* the already-computed address. It also avoids a
  compile-time-fixed pool size.
- The handle's function body is marginally *smaller* than a static-struct
  singleton's (42 vs 44) — the caller supplies the base address in a
  register pair, so the callee skips loading it. But each call site pays
  those 4 bytes back, so **beyond a single call site a singleton is
  overall smaller** — which is precisely why this rule is scoped to
  instantiable modules only.


## Pass a struct by value only while it fits the return registers

Take and return a struct **by value** as long as it fits in the
target's argument/return registers; **by pointer** above that.

On avr-gcc the boundary is **8 bytes** (returns travel in `r18`–`r25`,
arguments in `r8`–`r25`). Check it for any other target rather than
carrying the number over — it is a property of the ABI, not a
universal.

### Below the threshold, a pointer is a pessimism

An 8-byte struct is handed over entirely in registers. Forcing a
pointer makes the caller *materialise* the object in memory just to
take its address, turning a register-to-register hand-off into a store,
an address computation and a load.

Measured (CoreLR, 2026-09-11): `Motion_XYToLR()` takes an 8-byte
position and returns an 8-byte pair of cable lengths. Disassembly shows
no hidden pointer and no memory copy — both travel in registers.

### Above it, the copy is real

Above the register budget the ABI passes a **hidden pointer** to
caller-allocated space and copies the whole struct. A 26-byte
parameter block compiled to exactly that:

```
ldi r18, 0x1A          ; 26
movw r26, r24          ; hidden destination pointer from the caller
ld r0, Z+ / st X+, r0 / dec r18 / brne   ; copy all 26 bytes
```

plus 26 bytes of caller stack for the temporary.

### But do not overstate the saving — measure before claiming one

Converting that 26-byte getter to a pointer on CoreLR gained **126
bytes of flash and zero bytes of stack**. With `-flto` the compiler
could already see that the getter only returned a static, so it inlined
it and read just the field the caller wanted: the copy visible in the
standalone symbol had already been optimised away at every real call
site.

So the rule earns its place on **robustness**, not on a measured stack
saving: the moment the function grows past what the inliner will
swallow, or a build drops LTO, the copy comes back — at every call
site, silently. Write the API so it never depends on that.

### The corollary pays better than the rule

**When a caller wants one field, give it a narrow getter** rather than
the whole struct. Five of CoreLR's seven callers of a 26-byte getter
read a single field. `Calibration_GetPenAngles(&up, &down)` and
`Calibration_GetMotorDistanceUm()` say what the caller needs, cost
nothing to copy, and make the dependency visible in the signature.

### How this combines with grouping a module's state

[`STD-MemoryAndPerformance.md`](STD-MemoryAndPerformance.md) says to
group a module's persistent state into one struct once three or more
fields are touched together, because avr-gcc then loads the base
address once and reaches the rest with 2-byte `LDD`/`STD` offsets.

The two rules meet, and reinforce each other, at the API boundary. A
module whose state is grouped can hand out a **pointer** to it, and the
caller reading several fields gets exactly the same cheap
base-plus-offset addressing the module enjoys internally — with no copy
at all. By value it would have copied the whole block first, only to
address it by the same offsets afterwards.

For an instantiable module the two rules converge on one shape: the
handle *is* the pointer to the grouped struct, so
[the handle pattern](#instantiable-modules-use-a-handle-as-first-parameter)
above already follows this rule by construction.

