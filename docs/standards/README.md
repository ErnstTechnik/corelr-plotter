# Arduino / Embedded C Coding Standards

A reusable set of coding conventions for Arduino/AVR (and embedded C
generally) projects. Extracted and refined during the CoreLR
wall-plotter firmware project (this repository applies them) but kept
deliberately free of anything CoreLR-specific, so this folder can be
copied wholesale into a new project's workspace.

Each rule states *what* to do, *why* — including any AVR-specific
measurement backing it, not just an assertion — and notes explicitly
where a project might reasonably choose differently.

## Files
- [STD-Naming.md](STD-Naming.md) — casing/prefix table for every kind
  of identifier, type suffixes, identifier length, visual
  unambiguity, header include guards. Anchored on BARR-C:2018,
  MISRA C:2012 and the Arduino core's observed conventions.
- [STD-FileOrganization.md](STD-FileOrganization.md) — language choice
  (C vs C++), file headers, function declaration order, centralized
  configuration headers, **exposing only what callers need** (private
  types in the `.c`, or a `module_priv.h`, and why `private` is not the
  point), ASCII-art diagrams for algorithms.
- [STD-MemoryAndPerformance.md](STD-MemoryAndPerformance.md) — caching
  dereferenced values, grouping module state into structs (with a
  measured AVR crossover point), and the atomicity/volatile caveat that
  goes with it.
- [STD-ModuleDesign.md](STD-ModuleDesign.md) — handle-based API for
  instantiable/reusable modules ("objects in C"), when *not* to use it,
  the `const`-typedef trap, transparent vs. opaque state, and measured
  AVR justification.

## How a project adopts these
Copy this folder into the new project's workspace as-is. In that
project's own top-level instructions file (e.g. its `CLAUDE.md`), add a
short pointer to it plus whatever is genuinely project-specific
(license choice, exact prefix used for identifiers/header guards,
Requirements folder layout if the project tracks formal requirements)
— don't duplicate the rules themselves there.

## Open items
- **Parameter-passing conventions** (by value vs. by pointer/reference,
  whether/when to expose a narrow getter for a subset of a larger
  struct instead of the whole thing) — flagged 2026-09-08 as a related
  but separate topic from `STD-MemoryAndPerformance.md`'s
  struct-grouping rule, deferred to its own discussion.
*(Closed 2026-09-08: include-guard prefixing — project prefix for
application headers, file name only for a reusable Arduino library;
configuration-macro prefixing — the consuming module's name; enumeration
member naming — `MODULENAME_<TAG>_<VALUE>`. All settled in
[STD-Naming.md](STD-Naming.md), and **applied to the whole CoreLR
codebase** the same day: ~130 identifiers renamed from the project
prefix to their owning module's name, verified byte-identical binary
before/after.)*

*(Closed 2026-09-08: type-suffix conventions — now settled in
[STD-Naming.md](STD-Naming.md), all typedefs end `_t` with the type's
nature in its name; `STD-ModuleDesign.md`'s examples updated to match.)*
