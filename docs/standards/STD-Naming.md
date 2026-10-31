# Coding Standard — Naming

## Standards this is anchored on

Rather than inventing a house style, these rules follow established
embedded-C practice, deviating only where a deviation is justified and
recorded:

- **BARR-C:2018** (Barr Group, *Embedded C Coding Standard*) — the
  de-facto naming reference for embedded C, written specifically to
  reduce defects. Source of: the `_t` typedef suffix, the `s_`/`g_`
  variable-scope prefixes, ALL_CAPS macros, and the ban on
  visually-ambiguous single-letter names.
- **MISRA C:2012** — safety-critical C. Relevant here for *identifier
  distinctness* (Rule 5.1 and the 5.x series) and Directive 4.5
  (identifiers in the same name space should be typographically
  unambiguous). MISRA deliberately does **not** mandate a casing style.
- **Arduino core, as observed** (verified 2026-09-08 by reading
  `hardware/arduino/avr/cores/arduino/` in the installed IDE, not from
  recollection): functions are `camelCase` starting lowercase
  (`digitalWrite`, `analogRead`, `delayMicroseconds`); macros/constants
  are `UPPER_SNAKE` (`INPUT_PULLUP`, `NOT_A_PIN`, `DEG_TO_RAD`);
  typedefs are `snake_case_t` (`tx_buffer_index_t`,
  `rx_buffer_index_t`). Arduino is itself internally inconsistent
  (`voidFuncPtr` has no `_t`; `byte`/`word`/`boolean` are bare legacy
  aliases), so it is a reference for *what an Arduino developer
  expects to read*, not a rulebook to copy wholesale.
- **ISO C / POSIX reserved namespaces** — an objective constraint, not
  a style preference: identifiers beginning with `_`, and (per POSIX.1)
  identifiers *ending in* `_t`, are reserved for the implementation.
  See the note under "Type suffixes" for why we use `_t` anyway.

> If formal MISRA/BARR-C compliance is ever claimed for a project,
> verify exact clause numbers against your own copy of the standard —
> the citations above identify the rules, they are not a substitute for
> the documents.

## The prefix rule

**Everything a module exposes is prefixed with that module's name** —
types, functions, macros, enum members, and the file names themselves.
Not the project name: the *module* name. The module that owns a symbol
is the one whose header you must include to use it, so the prefix
doubles as documentation of where the symbol comes from.

**Drop redundant repetition** between the prefix and the rest of the
name: the module name is already there, so don't say it twice.
`GCodeParser_Number_t`, not `GCodeParser_GCodeNumber_t`;
`Stepper_Queue_t`, not `Stepper_StepQueue_t`;
`Motion_Segment_t`, not `Motion_WaypointSegment_t`.
Same principle already applied to functions:
`Stepper_RequestMotion()`, not
`Stepper_EnqueueStepperMotion()`.

### Enumerations
The **type** follows the type rule (`Module_Noun_t`). The **members**
follow:

```
MODULENAME_<TAG>_<VALUE>
```

- `MODULENAME` — the owning module, uppercased.
- `<TAG>` — a short upper-snake tag naming the enumeration itself.
  **Omit it** when the module owns only one enumeration and the values
  are unambiguous on their own.
- `<VALUE>` — the value.

Every member of one enumeration shares a byte-identical prefix, so an
IDE's completion on that prefix lists exactly the valid values and
nothing else. A count sentinel, when one is needed, is
`MODULENAME_<TAG>_COUNT` and must be the **last** enumerator.

```c
/* tag needed: Stepper owns two enumerations */
typedef enum
{
   STEPPER_MOTOR_L = 0,
   STEPPER_MOTOR_R,
   STEPPER_MOTOR_COUNT      /* sentinel, last */
} Stepper_Motor_t;

typedef enum
{
   STEPPER_PEN_NONE = 0,    /* "ACTION" dropped: the type says PenAction */
   STEPPER_PEN_UP,
   STEPPER_PEN_DOWN
} Stepper_PenAction_t;

/* tag omitted: one enumeration, self-evident values */
typedef enum
{
   GCODEPARSER_OK = 0,
   GCODEPARSER_ERROR
} GCodeParser_Result_t;
```

Apply the redundancy-dropping rule to the tag as well: the module name
is already in the prefix, so `LCDJOYSTICKUI_MODE_SD_BROWSER`, not
`LCDJOYSTICKUI_UI_MODE_SD_BROWSER`.

### Macros in centralized configuration headers
A centralized configuration header (see
[STD-FileOrganization.md](STD-FileOrganization.md)) holds macros that
are *consumed* almost entirely by **other** modules, so "the module
name" is ambiguous — owner or consumer? **Use the consumer module's
name** (settled 2026-09-08):

```c
/* in SwConfig.h -- named for the module it configures, not for SwConfig */
#define STEPPER_QUEUE_SIZE     16u
#define GCODEPARSER_LINE_SIZE    48u
#define PENCONTROL_SETTLE_TIME_MS 300u
```

**Why the consumer:** a prefix's main job is disambiguation *at the
point of use*, and a tunable conceptually belongs to the module it
tunes — `STEPPER_QUEUE_SIZE` reads correctly inside
`Stepper.c`, whereas `SWCONFIG_STEPQUEUE_SIZE` would group
unrelated tunables under one uninformative prefix. The "which header do
I include?" question that the maintainer-prefix would answer is instead
answered once, structurally: a project has a small, fixed set of
configuration headers, and every module includes them.

These headers already group macros into Doxygen `@defgroup` sections
per consuming module, so the naming simply matches the organization
that is there anyway.

**Genuinely project-scoped macros keep the project prefix** (found
2026-09-08 while applying this rule to a real codebase — a third
category the consumer/owner question does not cover). Two kinds
qualify:
- **Build-variant selectors**, which configure the firmware as a whole
  rather than any one module: `CORELR_JOB_SOURCE`,
  `CORELR_JOB_SOURCE_SD`, `CORELR_JOB_SOURCE_SERIAL`.
- **Project-wide units and conversion constants** used by several
  unrelated modules: `CORELR_UM_PER_MM`.

The test is whether naming a single consuming module would be *wrong*
(not merely inconvenient): if two unrelated modules both legitimately
own the concept, or if the macro configures the build rather than a
module, it is project-scoped. Do not use this to dodge a real choice —
a macro with two consumers where one is clearly primary takes the
primary consumer's name.

**File-local macros take no prefix at all**, like file-local functions
and types: a macro defined and used only inside one `.c` is already
namespaced by its translation unit (e.g. `MOTOR_L_WIRE0_SET()`,
`TIMER2_OCR2A`, `WAYPOINT_GRID_UM`).

## Casing and prefixes — the table

| Kind | Style | Example |
|---|---|---|
| Type (any `typedef`) | `ModuleName_PascalCase_t` | `Stepper_Entry_t` |
| Public function | `ModuleName_PascalCase` | `Stepper_RequestMotion` |
| File-local (`static`) function | `PascalCase`, **no** prefix | `EnqueueSplit`, `DrainPending` |
| Macro / compile-time constant | `MODULENAME_UPPER_SNAKE` | `STEPPER_QUEUE_SIZE` |
| Enum member | `MODULENAME_UPPER_SNAKE` | `STEPPER_PEN_ACTION_NONE` |
| Struct / union member | `PascalCase` | `MotorCoilBits`, `WriteIndex` |
| Local variable, parameter | `camelCase` | `writeIndex`, `holdTicks` |
| File-scope `static` variable | `s_camelCase` | `s_queue`, `s_pendingPenAction` |
| Global (non-`static`) variable | `g_camelCase` — and avoid needing one | `g_systemTicks` |
| File-scope `const` / `PROGMEM` table | `k` + `PascalCase` | `kPhaseTable`, `kMenuLabels` |

File-scope variables (`s_`/`g_`/`k`) take no module prefix: they are
never visible outside their own translation unit, so there is nothing
to disambiguate — the `s_`/`g_`/`k` marker carries the useful
information (persistent state vs. local, mutable vs. constant).

### Why file-local symbols carry no module prefix
A `static` function, a type declared only in a `.c`, or a macro defined
only in a `.c` is already namespaced by its translation unit — the
prefix would add length (see the 31-character note below) with no
disambiguation benefit, and it makes the public/private split visible
at a glance: if you read `ModuleName_Something()`, it is part of the
module's API; if you read a bare `Something()`, it is internal. This
deviates from the "prefix everything" instinct deliberately.

*(Correction, 2026-09-08: an earlier draft of this file stated that
file-local symbols should also take the module prefix. That was wrong —
it contradicted the actual codebase and offered no benefit. The rule
above is the corrected one.)*

### Deviation from Arduino core style, and why
Arduino's own API is a single flat global namespace, where
`digitalWrite` needs no prefix. A modular application is different: the
`ModuleName_Function` form is what provides namespacing, and
`Stepper_RequestMotion` reads better than
`stepperJobQueue_enqueueMotion` once an underscore-separated prefix is
in play. **This is a conscious deviation**: match Arduino's casing when
writing something that extends the Arduino API surface (e.g. a library
meant to feel native to Arduino users); use the table above for
application modules.

## Type suffixes

**Every `typedef` ends in `_t`.** No `_s`, no `_e`, no `_fn`. This
matches BARR-C:2018, the Arduino core's own typedefs, and every
existing type in this codebase — one rule, no exceptions to remember.

**The nature of the type lives in its *name*, not in the suffix:**

| Purpose | Pattern | Example |
|---|---|---|
| Value struct | `Module_Noun_t` | `Motion_Position_t` |
| Instance state struct (handle target) | `Module_State_t` | `MotorDriver_State_t` |
| Handle (pointer to instance) | `Module_Handle_t` | `MotorDriver_Handle_t` |
| Read-only handle | `Module_ConstHandle_t` | `MotorDriver_ConstHandle_t` |
| Enumeration | `Module_Noun_t` | `Stepper_PenAction_t` |
| Function pointer | `Module_...Callback_t` / `..._Fn_t` | `Timer_ExpiryCallback_t` |

This closes the open question left by
[STD-ModuleDesign.md](STD-ModuleDesign.md): a handle and a value struct
are told apart by `Handle` in the name, **not** by `_t` vs `_s`. Using
`_t` to mean "struct" in one place and "pointer to struct" in another
is precisely the ambiguity to avoid — and the reason a reader can never
be sure whether `Foo_t x;` allocates or aliases.

**Struct tags:** omit the tag entirely (`typedef struct { ... } Foo_t;`)
unless one is genuinely required — a self-referencing struct, or an
opaque type. When required, name the tag identically to the typedef
minus `_t`:
```c
typedef struct FooNode FooNode_t;   /* forward, for self-reference */
struct FooNode
{
   FooNode_t *Next;
};
```

**On `_t` being POSIX-reserved:** POSIX.1 does reserve the `_t` suffix
in the global namespace, so a strict reading forbids it in application
code. We use it anyway, deliberately: it is universal in embedded C
(including the Arduino core and the C standard library itself), the
module-name prefix makes an actual collision with a future
implementation type effectively impossible, and abandoning it would
make our code *less* familiar to every embedded developer for a purely
theoretical gain. Recorded here as a known, intentional deviation
rather than an oversight.

## Header include guards

The guard always matches the **file name**, uppercased, no separators,
`_H` suffix — on the `#ifndef`/`#define` pair *and* the closing
`#endif`'s comment. The only question is whether a project prefix is
added, and that depends on what is being built (settled 2026-09-08):

**Application / project header** — prefix with the project name:
```c
/* Stepper.h, inside project CoreLR */
#ifndef CORELR_STEPPER_H
#define CORELR_STEPPER_H
...
#endif /* CORELR_STEPPER_H */
```
This is the **one documented exception** to the module-name-only prefix
rule, and it is accepted reluctantly: a guard is the only identifier
that must be unique across *every* header the compiler ever sees,
including third-party libraries we do not control. A library shipping
its own `MotorDriver.h` guarded `MOTORDRIVER_H` would silently collide
— and the failure mode is nasty: one of the two headers simply does not
get included, producing a confusing error in a *third* file. The
project prefix costs nothing in readability (a guard never appears in
normal code) and removes the risk entirely.

**Reusable Arduino library header** — file name only, no prefix:
```c
/* MotorDriver.h, shipped as an Arduino library */
#ifndef MOTORDRIVER_H
#define MOTORDRIVER_H
...
#endif /* MOTORDRIVER_H */
```
A library has no enclosing project to name, and its own name *is* its
namespace — users write `#include <MotorDriver.h>`. Adding a prefix
here would be redundant, and uniqueness is already the library
ecosystem's concern (two libraries cannot share a name in the Arduino
library registry).

**Why state any of this explicitly:** a mismatched or abbreviated guard
is easy to introduce by copy-pasting a header and forgetting to update
it, and easy to miss in review — the file still compiles perfectly on
its own.

## Identifier length

MISRA C:2012 Rule 5.1 requires external identifiers to be distinct, and
notes that C90 guarantees only **31 significant characters** for
identifiers with external linkage (C99 raised this to 63).

**Practical rule:** keep external identifiers **≤ 31 characters where
it costs nothing**, but do not sacrifice clarity for it. Modern
toolchains (including avr-gcc) impose no practical limit, so exceeding
31 is a portability/compliance concern, not a defect.

**Known deviations in this codebase: none, as of 2026-09-09.** The list
was 12 identifiers (longest 38) before the module-renaming pass, then 13,
then emptied entirely — see the note just below for how, and why the
count went *up* before it went to zero.

### Prefer concise module names — a measured consequence of the prefix rule
Because every identifier carries its module's name, identifier length is
a direct function of module-name length: a long module name pushes
*all* of its identifiers toward the 31-character limit at once.

Measured progression on a real codebase (CoreLR, 2026-09-08 → 09),
counting external identifiers over 31 characters:

| Step | Count |
|---|---|
| Short project prefix everywhere (`CORELR_`) | 12 |
| Switched to **module-name** prefixes (this rule) | 13 |
| `StepperJobQueue` (15 chars) → `Stepper` (7) | 9 |
| `CalibrationStorage` (18) → `Calibration` (11) | 2 |
| `WaypointPipeline` (16) → `Motion` (6) | 0 |

Two things this shows:

1. **Adopting the module-prefix rule made it worse before it made it
   better** (12 → 13). That is inherent: a project prefix is chosen once
   and kept short, whereas module names are chosen for descriptiveness
   and are usually longer. The rule is still right — it buys real
   disambiguation — but it puts a cost on verbose module names that a
   project prefix did not.

2. **Shortening the three longest module names took the count to zero**,
   each rename removing every one of that module's deviations at a
   stroke: `Stepper_TakePendingPenAction` is 28 where
   `StepperJobQueue_TakePendingPenAction` was 36. No identifier had to be
   shortened individually except two over-verbose constants
   (`LCDJOYSTICKUI_JOG_MAX_SPEED_UM_MS` → `..._JOG_MAX_SPEED`).

Prefer a short, precise module name over a descriptive-but-long one —
not for typing convenience, but because the choice is multiplied across
the module's entire API surface. A good test: if the module name repeats
information the *function* names already carry (`StepperJobQueue_...`
whose functions are all about queueing anyway), the excess belongs in
neither.

## Visual unambiguity (MISRA C:2012 Directive 4.5)
Never use as a standalone identifier: `l` (lowercase L — reads as `1`),
`O` or `o` (read as `0`), `I` (reads as `l`/`1`). Avoid pairs that
differ only by such characters, or only by case, within the same
overlapping scope.

## Abbreviations
Spell words out in public API names; abbreviate only where the short
form is more standard in context than the long one (`Um` for
micrometres, `Lcd`, `Sd`, `Ui`, `Isr`, `Ms` for milliseconds).
Abbreviate consistently — never `Cnt` in one function and `Count` in
another for the same concept.

## Open items

### Tracked elsewhere
- **Parameter-passing conventions** — [README.md](README.md)'s open
  items; naming does not decide them.
