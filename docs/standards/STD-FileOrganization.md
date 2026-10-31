# Coding Standard — File Organization

## Language
Prefer **C, not C++**, for project-owned source. C++ is acceptable only
where an imported third-party library requires it (e.g. many Arduino
hardware libraries — SD card, LCD, Servo — are C++-only). Isolate such
usage: give the C++-dependent file a plain-C, `extern "C"`-wrapped
public header, so the rest of the project never needs to know it is C++
underneath.

**Why choose C over C++ by default:** smaller, more predictable code
size on memory-constrained targets (no vtables/exceptions/RTTI unless a
library forces them in), and a simpler mental model of what the
compiler actually generates — valuable on a chip where every byte of
flash/RAM shows up directly in the build output. A project targeting a
larger MCU with RAM/flash to spare may reasonably choose differently;
state that choice explicitly if so, rather than drifting into a mix by
default.

## Environment
State the target IDE/toolchain and its entry-point convention
explicitly (e.g. Arduino IDE: the main source file is a `.ino`, which
auto-wraps `setup()`/`loop()` and auto-includes `Arduino.h` — unlike
separate `.c`/`.cpp` sketch files, which must include it explicitly if
they use any Arduino-core symbol directly).

## File headers
- A license header on every source file (this project's license,
  chosen once and applied consistently — e.g. GPLv3-style).
- A Doxygen-style `@file`/`@brief` header comment, stating what the
  file does, which requirements it realizes (if the project tracks
  those), and any settled design decisions worth recording right next
  to the code they affect — not only in a separate requirements
  document a reader of the code might not have open.

## Doxygen markup in prose
A comment is read twice: by a person, and by Doxygen, which turns some plain-looking text into markup. The traps found on the first documentation run of
this project, each of which made a warning:
- `#word` is a **link request** to a symbol called `word`: a preprocessor word in prose (`#define`, `#if`, `#error`, `#else`) is written `\#define`, or in a code span
  with backticks (`` `#define X` ``).
- `<word>` is an **HTML tag**: write `\<word\>` (or a code span) for a placeholder such as `<tag>`.
- `@brief @return ...` on a function that returns nothing, an `@param` that names no parameter, a `\ref` to nothing: Doxygen says so. Keep the build's documentation
  target at zero such warnings (it does not ask for a comment on everything: an undocumented item is a gap to list, not an error).

## Citing a requirement
The code says which requirement it realises, and the documentation turns each citation into a link to that requirement. CoreLR's rule (`tools/analysis/req_refs.py` checks it, `ctest`
fails otherwise; `REQ-CoreLR-D-Build-0019`):
- **In a comment that Doxygen reads** (`/** ... */`, `/**< ... */`, a documentation page) write `@req{REQ-CoreLR-F-Area-nnnn}`: the alias of the Doxyfile, a link to the requirement's
  heading. **One `@req{}` per requirement**, never `REQ-...-0001, -0006`, `-0001/-0003` or `REQ-...-0001..0003`: write `@req{A-0001}, @req{A-0006}` and `@req{A-0001}..@req{A-0003}`.
- **Not on the title line** of a `@defgroup`, `@page`, `@section` or `@name`: the alias does not work in a title. Cite it in the text that follows (`Realizes @req{...}.`).
- **In an ordinary comment** (`/* */`, `//`, a comment after a statement) Doxygen does not read the text: write the plain identifier, never `@req{}`, which would show as text.
- Each requirement heading carries an anchor on the line after it, `<a id="REQ-CoreLR-F-Area-nnnn"></a>` (invisible on GitHub, and what the link points to):
  `tools/analysis/req_refs.py --add-anchors` writes the anchor of a new requirement.
- **The serial protocol's items** (`FW-03`, `P7`, `D1`, `M-1`, and its numbered sections) are cited the same way: `@proto{FW-03}` in a comment that Doxygen reads, and `@protosec{4-6,4.6}` for a section (the label has dashes, the text keeps its dots: a dot ends a Doxygen label); the plain form stays in an ordinary comment. `req_refs` checks that the protocol defines the item, and `req_refs --convert` writes the aliases.
- **A document of the repository that is a page of the documentation** (today `docs/protocol/EepromLayout.md`) is cited by its path as `@docref{EepromLayout,docs/protocol/EepromLayout.md}` in a comment that Doxygen reads (the page, then the text, which is the path); the plain path stays in an ordinary comment. The pages are listed in `DOC_PAGES` of `tools/analysis/req_refs.py`.
- A citation of a requirement that does not exist (a typing error, a requirement that was removed) fails the check, in the code, the pages and the requirement files themselves.
- **Which image a requirement applies to.** A requirement with no tag applies to both images of the project. One that concerns a single image carries, on the line after its anchor, `*Applies to: serial image only*` or `*Applies to: sd image only*`; the documentation of the other image marks it as not part of it, without hiding it (links keep working). The tag of a module's requirements must match `cmake/variant_modules.txt` (`req_refs` checks it).
- The requirement files stay plain Markdown, as GitHub shows them: Doxygen reads them through `tools/analysis/req_refs.py --filter`, which makes the citations of one requirement by another links. Do not write a Doxygen command with its backslash in them, not even in a code span (the check `command`).

## Diagrams in comments
Draw a state machine or a flow once, where the code is, in the notation the documentation tool renders from the comment itself (Doxygen: `\dot ... \enddot`, Graphviz): it says
what the code does and is kept by whoever changes the code. Keep ASCII art for what a graph tool draws badly (speed profiles, geometry, bit layouts). **One representation per
diagram**: when a rendered diagram replaces an ASCII sketch, delete the sketch (two drawings of one thing drift apart). A diagram that needs a script in the browser, a remote
service or a Node toolchain to render is not worth its dependency in a firmware repository.

## Function declaration order
**Forward-declare every file-local (`static`) function** immediately
after a file's macros/type definitions, before any `static` variables
or function bodies. Function *definitions* can then be ordered however
reads best, with no risk of a definition needing to appear before its
first use.

**Why:** confirmed the hard way on a real project — an ISR was placed
directly after the `Init()` function that configured it, calling a
`Tick()` helper defined later in the same file; getting the order wrong
here is exactly the kind of mistake that either fails to compile or
(worse) silently compiles against an implicit declaration. A blanket
forward-declaration block right after the macros makes this class of
bug structurally impossible to introduce, rather than something to
remember to check for on every edit.

## Centralized configuration headers
Keep every `#define` tunable/pin-assignment in a small, fixed set of
headers, not scattered inline through the source. A clean split for
embedded projects:
- A **hardware-facts** header: pin assignments, fixed mechanical/
  electrical parameters. Zero dependency on the MCU's register API —
  states *what* is connected where, not *how* to drive it.
- A **software-tunables** header: buffer/queue sizes, timing constants,
  UI tuning — anything that is a *choice*, not a hardware fact.

Register-level derivation from a pin number (which port, which bit) is
itself an implementation detail of whichever module actually drives
that hardware — keep it private to that one file (minimum-scope
principle), not in the shared hardware-facts header, unless more than
one module genuinely needs register-level access to the same pin.

## Expose only what callers need — everything else stays private
A module's header is its **contract**. Only what a caller genuinely
needs shall appear in `module.h`:
- public function declarations;
- the types those functions take or return;
- types a caller must be able to declare an instance of.

Everything else — internal state structs, helper types, working
constants — shall be **out** of the public header. Three levels, in
order of preference:

| Where | Enforcement | Use when |
|---|---|---|
| In `module.c` | **Compiler**: the type cannot be named from another translation unit | The module is a single `.c` — the usual case |
| `module_priv.h` | **Convention**: the name signals intent, nothing prevents inclusion | The module spans several `.c` files that must share internals |
| Opaque type in `module.h` (`typedef struct Foo Foo_t;`) | **Compiler**, while still nameable in the API | The API must pass the type around without revealing it — implies an internal pool, since callers cannot then allocate it (see [STD-ModuleDesign.md](STD-ModuleDesign.md)) |

**On C vs C++ — this is not a C limitation.** Translation-unit
visibility behaves *identically* in both: a type declared only inside a
`.c`/`.cpp` cannot be named from any other unit, and that is enforced by
the compiler, not by convention. `private` would only apply if the
module were written as a C++ class; a project that has chosen C (see
"Language" above), and whose `.cpp` files exist only because an imported
library requires C++, never had `private` available and does not need
it. For completeness, C++ adds anonymous namespaces as a file-local
linkage mechanism equivalent to `static` — irrelevant where the module's
surface is `extern "C"`.

**Why this matters beyond tidiness:** an internal struct left in the
public header reads as permission. A caller seeing a complete queue
structure in `Stepper.h` may reasonably conclude that touching
`Indices.WriteIndex` is allowed — and single-producer/single-consumer
assumptions elsewhere in the module quietly depend on nobody doing that.
Removing it makes the contract unambiguous instead of relying on a
comment.

**How to tell whether a type is really public** — check both, because
checking only the first is misleading:
1. is the type *named* in another translation unit?
2. does it appear in a public function signature — including as a
   **parameter**, not only as a return type?

A type can be public purely through (2) while never being spelled
elsewhere: callers use its enum members or pass its values without ever
writing the type name.

## ASCII-art diagrams for non-trivial algorithms
Add an ASCII-art diagram or sequence directly in the comment above any
non-trivial algorithm, state machine, or packed data layout — bit
layouts, ring buffers, ISR tick sequences, multi-step splitting logic.
Keep it next to the code it describes, not only in a separate doc file,
so it cannot silently drift out of sync with the code unnoticed.

## Documentation language
Pick one consistent language for the whole project's documents, specs,
and comments (English is a reasonable default for anything that might
be shared beyond the original author) and stick to it — mixing
languages within a single codebase makes it harder to search and to
hand off.
