# Coding Standard — Memory & Performance

## Cache dereferenced/shared values in a local variable
Before reusing a dereferenced or shared value multiple times within a
function, cache it in a local variable first, rather than
re-dereferencing repeatedly:
```c
/* prefer: */
uint8_t idx = q->Indices.WriteIndex;
/* ... use idx multiple times ... */

/* over: */
/* ... use q->Indices.WriteIndex multiple times ... */
```
**Why:** defensive against a future concurrent-access hazard even where
not currently one — if `q->Indices.WriteIndex` could ever be modified
by another context (an ISR, another task) between two of your reads, a
cached local guarantees the whole function reasons about one
consistent value, not two different ones read at two different
moments.

## Group a module's own state into a struct — with a measured threshold
Store a module's persistent state (the fields it keeps between calls)
as fields of **one struct**, one static instance per module, rather
than as several independent `static` variables — **once the module
accesses three or more of those fields together in the same
function.** Below that, plain separate statics are at least as good.

**Why (measured on AVR/avr-gcc, 2026-09-08, `-Os`, not assumed):**
classic AVR addresses a `static`/global variable directly (`LDS`/`STS`,
a 4-byte instruction encoding the full 16-bit address) — there is no
"base pointer" overhead to begin with for a lone global, unlike a
machine where addressing a global might itself require a computed or
loaded address. Grouping fields into a struct only pays off once a
function touches enough of them that avr-gcc's own optimizer decides to
load the struct's base address into an index register (`X`/`Y`/`Z`)
*once*, then use the 2-byte `LD`/`ST` (offset 0) and `LDD`/`STD` (offset
1–63) instructions for the rest.

Measured example: a function writing 3 fields (`uint8_t`, `uint16_t`,
`uint8_t`) as 3 separate statics compiled to **24 bytes**; the exact
same 3 fields grouped as one struct compiled to **20 bytes** — and the
gap widens as more fields are touched together in one function. A
matching read-and-sum function measured 26 bytes (separate) vs. 22
bytes (struct). Below three fields touched together, direct addressing
is as good or better; forcing a struct at that point adds indirection
for no benefit.

**This is about accessing your *own* module's fields in place** (via
the module's single static instance) — *passing* that struct across an
API boundary is a separate question, settled in
[`STD-ModuleDesign.md`](STD-ModuleDesign.md): by value while it fits
the return registers, by pointer above. The two compose well — a
grouped struct handed out by pointer lets the caller read several
fields with the same cheap base-plus-offset addressing, and no copy.

**Caveat — `volatile` fields and atomicity are unaffected by grouping:**
if a module's struct mixes fields touched by an ISR (needing `volatile`,
and possibly a critical section on an 8-bit target when the field is
wider than a byte) with fields touched only from normal-context code,
grouping them into one struct does **not** change the atomicity rules
for any single field, and does **not** make partial/torn access to a
multi-byte field any safer. Reason about each field's actual
concurrent-access pattern individually, exactly as if it were still a
standalone variable — a struct is an organizational convenience here,
not a synchronization mechanism. When in doubt, verify by reading the
actual code that could run concurrently (e.g. the real ISR body), not
by assuming from the type's width or a comment's claim elsewhere.

## Do not use `printf`/`snprintf` — write the conversions you need

On an 8-bit AVR, a single `snprintf()`/`snprintf_P()` call links
avr-libc's whole formatted-output machinery. Measured on one real
project: **1372 bytes of flash** — `vfprintf` 948, `__ultoa_invert`
188, `snprintf_P` 96, `fputc` 96, two `strnlen` at 22 — to do nothing
but splice a few small integers and short strings together. On a
30 KB part that is 4.5% of the image for string concatenation.

Write the handful of conversions the project actually needs instead.
A useful shape, because it needs no buffer object and no hidden state:
each function takes the current write position and returns the new one,
so composing a line is a chain, and each keeps the destination
NUL-terminated and never writes at or past the capacity.

```c
uint8_t at = Fmt_StrP(line, 0,  sizeof(line), PSTR("L,R="));
at         = Fmt_Str (line, at, sizeof(line), left);
at         = Fmt_Char(line, at, sizeof(line), ',');
             Fmt_Str (line, at, sizeof(line), right);
```

Three things worth getting right the first time:

- **Size the integer conversion to the need.** A 16-bit
  divide-by-ten is markedly cheaper than a 32-bit one, and most
  displayed quantities fit. Widen as a *separate* function when
  something genuinely needs it, so the cheap path stays cheap.
- **Decide about truncation once.** For fixed-width display lines,
  silent clipping is the wanted behaviour and an error return would
  only be discarded at every call site. Say so in the header.
- **Say in the header that it replaces the standard library, and why,
  with the measured number.** Otherwise the next reader "simplifies"
  it back to `snprintf` and the 1372 bytes return with one call site.

The same applies to `float` support: pulling it in for one diagnostic
line is a comparable cost, and integer-only formatting is usually a
line or two of arithmetic.

## Open items
- ~~Parameter-passing conventions~~ — **settled 2026-09-11**, written up
  as "Pass a struct by value only while it fits the return registers"
  in [`STD-ModuleDesign.md`](STD-ModuleDesign.md), where it belongs
  (it is an API-shape rule). It closes the question this file raised,
  and the two rules compose: a module whose state is grouped can hand
  out a pointer to it, so a caller reading several fields gets the same
  base-plus-offset addressing with no copy at all.
