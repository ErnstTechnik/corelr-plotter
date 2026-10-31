# Design Requirements — SD Reader

Module: **SDReader** — background task that reads GCode lines from the
SD card into a queue for the interpreter. **SD variant only**: in the serial build `SDReader.h` and `SDReader.cpp` are empty (the whole of both is under
`#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD`, as `Uart` and `SerialLink` are under the serial one). Until 2026-10-27 only the pin checks of the `.cpp`
were, and the serial image carried **84 bytes of flash** of dead SD code (the constructor of an `SdFile` and its `Print::write`, found when the headers were made
conditional for the documentation, `REQ-CoreLR-D-Build-0013`); the SD image is unchanged.

Realizes: [`REQ-CoreLR-F-JobStorage-0001`, `-0002`](../functional/REQ-CoreLR-F-JobStorage.md),
[`REQ-CoreLR-F-GCodeInterpreter-0002`](../functional/REQ-CoreLR-F-GCodeInterpreter.md)
(comment syntax realized here)

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Implemented. Line reading (`-0001`..`-0003`) 2026-09-07, SD
file browser 2026-09-07, direct-`SdFile` access (`-0005`) 2026-09-10.

## REQ-CoreLR-D-SDReader-0001 — Background line reading
<a id="REQ-CoreLR-D-SDReader-0001"></a>
*Applies to: sd image only*

GCode files shall be read from the SD card line by line, in a background
task, into a queue. The reading task shall aim to keep the queue as full
as possible — this is how streaming (`REQ-CoreLR-F-JobStorage-0002`) is
achieved.

## REQ-CoreLR-D-SDReader-0002 — Comment stripping at read time
<a id="REQ-CoreLR-D-SDReader-0002"></a>
*Applies to: sd image only*

Comment stripping shall be performed at the SD-reading stage, not
deferred to the GCode interpreter:
- `;` comments: strip from `;` to end of line, including any trailing
  whitespace (tab, vtab, ...) back to the last meaningful character.
- Parenthesised comments: same convention as grbl.

**Rationale:** more efficient (avoids storing unused content in the
queue) and avoids overly long lines caused by trailing comments on a
command line. Realizes the comment syntax defined functionally in
`REQ-CoreLR-F-GCodeInterpreter-0002`.

## REQ-CoreLR-D-SDReader-0003 — Line buffer format
<a id="REQ-CoreLR-D-SDReader-0003"></a>
*Applies to: sd image only*

Each queued line shall be a fixed-size buffer, terminated with a `\0`
character.

```c
#define SDREADER_LINE_SIZE  48u
#define SDREADER_QUEUE_SIZE 3u

typedef char SDReader_Line_t[SDREADER_LINE_SIZE];

typedef struct
{
   uint8_t WriteIndex;
   uint8_t ReadIndex;
} SDReader_QueueIndices_t;

typedef struct
{
   SDReader_QueueIndices_t Indices;
   SDReader_Line_t Lines[SDREADER_QUEUE_SIZE];
} SDReader_LineQueue_t;
```

**Naming note (2026-09-06):** the field/type names above (and in the
code) were adapted from the original source note's illustrative example
(`GCLine_t`, `WIdx`/`RIdx`, `QueueHead_t`, `LinePool`) to names that
describe what they represent — the example names were not meant to be
taken as a mandated API. See the equivalent note in
`REQ-CoreLR-D-Stepper.md`, where the same issue was found and
fixed first.

## REQ-CoreLR-D-SDReader-0004 — SD card pins
<a id="REQ-CoreLR-D-SDReader-0004"></a>
*Applies to: sd image only*

The SD card uses the hardware SPI bus (`D11`=MOSI, `D12`=MISO,
`D13`=SCK) plus a chip-select pin on `D10` — moved off `Walldraw.ino`'s
original `D4`, which the motor allocation needs (see
`../../docs/hardware/PinMapping.md`). `D10` is also the Arduino-conventional
default SD CS pin, and using it settles the AVR requirement that `SS`
be an output in master mode.

Those three SPI pins are **fixed silicon**, not a convention: once
`SPI.begin()` sets `SPE` with `MSTR` the SPI unit overrides `DDRB` for
them, so neither the chip select nor any motor wire may be placed on
one. A wiring attempt that did exactly that was reverted on 2026-09-10;
the reasoning is recorded under "Rejected wiring" in `PinMapping.md`.

### Compile-time pin-allocation guard (added 2026-09-10)

`SDReader.cpp` shall reject, with `#error`, any pin configuration in
which the chip select or a motor wire lands on a hardware SPI pin
(`PIN_SPI_MOSI`/`MISO`/`SCK`), or in which a motor wire is the same pin
as `SDREADER_CS_PIN`. Active only for `CORELR_JOB_SOURCE_SD`: with the
serial variant no SPI peripheral is enabled and `D11`-`D13` are
ordinary GPIO.

Motivated by a real wiring mistake rather than by theory. Neither
failure announces itself: a CS on an SPI pin simply never selects the
card, and a motor wire on one is dead, which shows up as a motor that
buzzes instead of turning. Neither symptom points at the pin map.

`PIN_SPI_*` are used rather than the literals 11/12/13 because they are
the board variant's own definition, and rather than the Arduino core's
`MOSI`/`MISO`/`SCK` because those are `static const uint8_t` and
invisible to `#if`. Their definedness is asserted first: undefined
identifiers evaluate to 0 in `#if`, which would silently turn every
test into a comparison against pin 0 — a guard that passes for the
wrong reason is worse than none.

Verified by compiling five deliberately-broken configurations (CS on
`D12`, CS on `D11`, Motor R wire 4 on `D11`, Motor L wire 3 on `D13`,
Motor L wire 4 on the CS pin): all five rejected, and the real
allocation compiles silently.

## REQ-CoreLR-D-SDReader-0005 — Card access through `SdFile`, not `SD`/`File`
<a id="REQ-CoreLR-D-SDReader-0005"></a>
*Applies to: sd image only*


`SDReader` shall access the card through sdfatlib's `Sd2Card` /
`SdVolume` / `SdFile` classes, and shall not use the Arduino `SD`
library's `SDClass` or `File` wrapper. Those three classes shall be
instantiated by this module (a vendored copy lives in
`firmware/src/sdfat/`, see its `PROVENANCE.md`), because `SDClass`
keeps its own equivalents private.

The browser shall keep its current directory **open** across calls and
list it by rewinding that handle and reading raw 32-byte directory
entries (`SdFile::readDir()`), rather than re-opening the directory by
path and opening each entry as a file.

### Why (measured 2026-09-10)

The wrapper was not merely wasteful, it made the firmware **overflow
its stack** while browsing. Deepest call chains from `main()`, measured
on the linked image with `tools/analysis/stack_depth.py`:

| Regime | Before | After |
|---|---|---|
| Steady-state plotting | 174 B | 199 B |
| Opening the job file | 271 B | *(folded into the browser chain)* |
| **Browsing the SD card** | **389 B** | **259 B** |
| Heap in use while browsing | ~270 B | **0 B** |
| Static RAM | 1727 B | 1693 B |
| Flash | 29044 B | 27260 B |
| **Free RAM minus worst-case stack** | **−62 B (overflow)** | **+84 B** |

Three separate costs went away:

1. `SDClass::open()` builds a stack path buffer and ping-pongs two
   `SdFile` objects to walk it — a **120-byte frame**, paid on every
   single listing step because the directory was re-opened by path each
   time. Holding the directory open removes it from the listing path
   entirely; the only remaining path walk is "go up".
2. `File::openNextFile()` **opens every directory entry as a real file
   just to read its name** — a 113-byte frame — then wraps it in a
   heap-allocated `File`. But the name and the directory bit are
   already in the `dir_t` the volume cache just read, so `readDir()`
   alone suffices and nothing is opened.
3. `File`'s constructor `malloc()`s 106 + 29 bytes, and the browser
   held two `File`s open at once (the directory and the candidate
   entry). With the wrapper gone, **no `malloc`/`free` is linked into
   the image at all** — there is no heap to collide with the stack.

Flash fell by 1784 bytes as a side effect: `SD.cpp` and `File.cpp`
stop being compiled.

### Consequences accepted

- **`SDReader_OpenJob()` takes a file name in the browser's current
  directory, not an absolute path.** That is what its only caller has,
  and it means the job file is opened straight out of the open
  directory handle. A path-based variant would have re-introduced the
  path walk for no caller's benefit.
- **`SdFile` derives from `Print`, not `Stream`**, so there is no
  `readBytesUntil()`. `SDReader_Task()` reads the line itself, byte by
  byte, which is what `readBytesUntil()` did anyway (one fewer virtual
  call now). Two behaviours improved as a result: a line longer than
  the buffer has its tail consumed to the newline instead of being
  re-presented as a second bogus line, and the line is stripped **in
  place inside the queue slot**, removing a 48-byte raw-line buffer
  from `loop()`'s frame — which sits under every chain above.
  Verified on the host, 17 cases including the aliasing-sensitive ones,
  identical output to the two-buffer version.
- The directory-listing order is unchanged (same names, same
  formatter, same comparisons) — verified on the host against a
  `qsort` reference with a stubbed directory.
- One remaining path walk, on "go up" only: FAT does store a `..`
  entry, but `SdFile::open()` rejects the name (`make83Name()` permits
  a single `.`) and opening it by directory index would mishandle the
  cluster-0 "parent is root" convention, so the text path is re-walked
  from the root. Once per navigation action, never while listing.

## REQ-CoreLR-D-SDReader-0006 — Browser directory depth is bounded, and refuses rather than truncates
<a id="REQ-CoreLR-D-SDReader-0006"></a>
*Applies to: sd image only*


The browser shall track its current directory as a text path in a
fixed buffer of `SDREADER_PATH_SIZE` bytes, and shall **refuse to
descend** when the next component would not fit, rather than truncating
the path.

Refusing matters more than the bound itself: the path is what "go up"
re-walks from the card root (`REQ-CoreLR-D-SDReader-0005`), so a
silently truncated path would re-open a *different* directory than the
one the user is standing in. The pre-2026-09-10 code did truncate (an
`snprintf` into a temporary followed by a `strncpy`), and then handed
the truncated path to `SD.open()`.

Depth actually available at `SDREADER_PATH_SIZE` = 64 (since 2026-10-13; it was 32), by the rule `length + 1 + name < SDREADER_PATH_SIZE`:

| Directory name length | Levels below the root |
|---|---|
| 8 characters | **7** (`/DIRNAME8` x 7, 63 bytes used) |
| 12 characters (maximal 8.3) | **4** |

At 32 it was 3 and 2. The 32 bytes more are static RAM (`s_currentPath`); the worst stack is unchanged (334, `tools/analysis/stack_depth.py`; 338 with the card-loss recovery of `-0009`), the RAM-minus-stack margin goes from 290 to 258 bytes (253 with the recovery), the flash 29 782 -> 29 816 (29 880). On the machine (maintainer, 2026-10-13, with 32): three levels reachable, the fourth refused without truncation, a 40-file directory and a 35-file one at the deepest level browsed; with 64: `tools/generators/gen_sd_tree.py` writes the levels 1..7 (reachable, the last at 59 characters) and the level 8 (68, refused).

Accepted as sufficient for V1 (maintainer, 2026-09-10): a plotter's
card is not expected to nest job files more than a couple of levels
deep. The bound is a single constant, and raising it costs static RAM
1:1 because `s_currentPath` is a static buffer, not a stack one — 64
bytes would allow ~6-7 levels and take the worst-case stack margin from
+86 to +54.

Note this is *not* a limit introduced by moving to `SdFile`: the same
32-byte buffer bounded the previous implementation. What changed is the
failure mode, from silent truncation to a clean refusal.

## REQ-CoreLR-D-SDReader-0007 — Card status is reported, not swallowed (implemented 2026-09-10)
<a id="REQ-CoreLR-D-SDReader-0007"></a>
*Applies to: sd image only*


Card bring-up shall record **which stage failed** and expose it, so the
user interface can say what is wrong rather than showing the same
"nothing here" for three different faults.

`SDReader_Mount()` performs the three stages in order and stops at the
first failure, setting `SDReader_CardStatus()` to `ABSENT` (no card
answered), `NO_VOLUME` (card answers but carries no usable FAT volume),
`NO_ROOT` (volume mounted, root directory would not open) or `OK`. It
also resets the browser to the root, since any previously open handle
is invalid.

Found on real hardware, 2026-09-10: with no card inserted the browser
showed "No files found", indistinguishable from a card holding no
`.gc`/`.gco`/`.nc` file. The bring-up result was discarded, with only a
`@todo` in its place.

`SDReader_Mount()` is split out of `SDReader_Init()` so it can be
called again: that is how a card inserted after power-on becomes usable
without a reset, offered on the short press when the browser has
nothing to list (`REQ-CoreLR-D-LcdJoystickUI-0008`).

**It blocks for up to ~2 s on failure** -- sdfatlib's `SD_INIT_TIMEOUT`
while waiting for a card that never answers. Acceptable only because
every call is deliberate: start-up, or an explicit user request from a
screen that cannot be reached while a job runs
(`REQ-CoreLR-D-LcdJoystickUI-0012`). It shall never be polled; a 2 s
stall in `loop()` would freeze the display and starve the step queue.

## REQ-CoreLR-D-SDReader-0008 — Browse on raw FAT names, format once (implemented 2026-09-11)
<a id="REQ-CoreLR-D-SDReader-0008"></a>
*Applies to: sd image only*


The browser shall order and filter directory entries using the **raw
11-byte FAT name field**, and build the displayed "NAME.EXT" string
only once, for the entry finally selected.

### Why: this is the deepest call chain in the firmware

`FindEntryAtRank()` sits at the bottom of the worst-case stack path.
Formatting during the scan cost three 13-byte buffers (the floor, the
best so far, and the candidate). Two raw 11-byte ones do the same work,
and the display name goes straight into the caller's buffer -- every
caller provides the 13 bytes `"NAME.EXT\0"` needs, so a local
temporary would have put those bytes straight back onto the chain being
trimmed.

Frame 81 -> 65 bytes; worst-case stack 320 -> 304, margin 32 -> 54.
`CountEntries()` loses its name buffer outright.

### Why it is legal: the two orderings are identical

The raw field is fixed-width and space-padded, and a space (0x20) sorts
below every character valid in a short name -- exactly as the formatted
string's terminator and its `.` (0x2E) do. So `"AB"` precedes `"ABC"`
and `"AB"` precedes `"AB.TXT"` under both.

Verified rather than argued: 576 ordered pairs on the host, covering
names with and without an extension, differing lengths, and generated
`~1` forms. **Zero disagreements.** The browser's ordering was then
re-checked end to end against a `qsort` reference over a stubbed
directory, including the rank-beyond-the-list and
buffer-too-small refusals.

### The extension filter, kept readable

`SDREADER_BROWSER_EXT1..3` stay written the familiar way (`".gc"`,
`".gco"`, `".nc"`); `RawExtIs()` applies the upper-casing and the space
padding when comparing against the raw field. One property falls out of
that and is worth stating, because it is the same finding recorded
earlier: an extension longer than three characters can never match,
which is precisely why `".gcode"` had to become `".gco"`.

## REQ-CoreLR-D-SDReader-0009 — A card pulled (and put back) during a navigation: the browser starts again from the root (decided 2026-10-13, maintainer's proposal)
<a id="REQ-CoreLR-D-SDReader-0009"></a>
*Applies to: sd image only*


Found on the machine (maintainer, 2026-10-13): the card was taken out during a navigation, files were added on a PC, the card was put back -- and the browser was stuck. A card that has been pulled forgets its initialisation (SPI mode, the sector cache is stale): every read then fails. The browser kept its open directory handle and its path, listed nothing, and nothing brought it back.

Design. The scans (`CountEntries()`, the rank search, a directory or job open) look at the `Sd2Card` error code after they ran (`NoteCardError()`) and remember the loss (`s_cardLost`). They do **not** remount: they sit on the deepest call chain of the firmware (`REQ-CoreLR-D-SDReader-0005`), where the frames of `Sd2Card::init()` must not be added. `SDReader_BrowserRecover()`, called by the UI from a shallow place (the end of `RefreshBrowserEntry()`), remounts when a loss was noted -- `SDReader_Mount()`: the path is the root again, the directory handle is re-opened, `SDReader_CardStatus()` says whether the card answered -- and returns 1 once per loss; the UI then resets its index and refreshes again. With no card, the screen says "No SD card / SW retry" as at power-up (the remount blocks up to ~2 s, sdfatlib's init timeout, as the explicit retry does). Cost: +64 bytes of flash, +1 of RAM, the worst stack 334 -> 338 (the remount call).
**Verified:** `tests/host/test_sdreader.cpp` (the real `SDReader.cpp` against a fake of sdfatlib, `shim_sd/`): the card pulled and put back inside a directory, pulled and not put back, a job opened on a dead card, and no false alarm; `tests/mutation/mutants/sdreader.txt`, 15 mutants, all caught (the same test also covers the listing order, the depth bound to the byte, going up and the job's lines).
**Not covered here:** a card pulled **during a job**: `-0012`.

## REQ-CoreLR-D-SDReader-0010 — The browser's work per step: a rank cache (implemented 2026-10-13, maintainer's observation on the machine)
<a id="REQ-CoreLR-D-SDReader-0010"></a>
*Applies to: sd image only*


Found on the machine (maintainer, 2026-10-13, `TREE/MANY`, 40 files): the further along the list, the slower the display -- fluent at the start, slow at the end. The cause was the algorithm of `-0008`'s sort (a RAM-free partial selection sort): the entry of rank *k* needed *k* + 1 full scans of the directory (each pick is the floor of the next scan), the UI asked for the name and then the kind (twice that), and every call began with two more scans to count the directories and the files. At the 40th entry: about 85 scans of 40 entries, from the card, for one step of the joystick.

**Design.** Nothing about the listing is held in RAM (`-0005`), but a little is *remembered between two calls*: (1) the number of directories and of files of the current directory, found in **one** scan and kept until the directory changes; (2) the last entry asked for: its kind, its rank and its raw 11-byte name (`s_rcKind`, `s_rcRank`, `s_rcName`). `FindEntryAtRank()` then costs:

| request | scans |
|---|---|
| the same entry as the last (the UI asks the name, then the kind) | 0 |
| the next one (rank + 1) | 1: the smallest name **above** the remembered one |
| the previous one (rank - 1) | 1: the largest name **below** it |
| the last of its kind (the wrap-around of the list) | 1: the largest of all |
| any other (a jump, the first call in a directory) | rank + 1, each from the previous pick, as before |

One function (`ScanPass(kind, above, below, wantLargest, best)`) does the scan; the chain of the last row keeps its floor in `s_rcName`, because a scan must never compare against the buffer it writes its best name to (found by `test_sdreader`: the first version gave an empty name for every jump). The remembered entry and the counts are dropped (`InvalidateListing()`) when the directory changes (entering, going up) and when the card is mounted again (`-0009`); a failed search, or a card error during it, does not set the cache.
The formatting of the name ("NAME.EXT") moved out of the search into `FormatRawName()`, called after the search returned, and the raw name is found into the caller's own 13-byte buffer and formatted in place: the deepest call chain of the firmware lost the second 32-byte directory-entry buffer that a scan inside a search would have cost -- **worst stack 328 bytes (338 before this change)**.
**Cost:** +350 bytes of flash and +17 of RAM for the cache; +74 more of flash for the saturating counts (30 304, 416 free; RAM 1 474).
**Result, measured on the host** (`tests/host/test_sdreader.cpp`, counting the fake card's scans): forward through a 44-entry list, name and kind as the UI asks: **43 scans** (the first version: about 1 900), backward 42, the wrap to the last entry 1, the same entry again 0; any order of requests gives the same answers as the sorted list (400 random accesses). The time per step is then that of **one to two scans of the directory** whatever the rank.
**Verified:** `tests/mutation/mutants/sdreader.txt`, 29 mutants, all caught (the cache, its invalidation, the direction of each scan, the bounds, the wrap, a failed search).

## REQ-CoreLR-D-SDReader-0011 — The browser's limits, in one place (2026-10-13)
<a id="REQ-CoreLR-D-SDReader-0011"></a>
*Applies to: sd image only*


| Limit | Value | Why | What happens beyond |
|---|---|---|---|
| Path length | `SDREADER_PATH_SIZE` = **64** bytes (it was 32), i.e. a path of **63** characters at most: `length + 1 + name < 64` | static RAM, 1 byte per byte; the path is what "go up" re-walks | the descent is **refused**; the browser stays where it is (never a truncated path), the entry stays selected and **row 1 reads `Path too long`** until the next move (`SDReader_BrowserEnterRefused()`; found on the machine 2026-10-13: the refusal was silent) |
| Depth | **7** levels of 8-character names (`/DIRNAME8` x 7 = 63), **4** of maximal 8.3 names (12 characters) | the path buffer | idem; fewer levels with longer names, more with shorter ones |
| Entries per directory | **255 entries at most are listed** (directories first, then the files, in order; the `..` entry counts): the counts **saturate** at 255 | one byte for each count and each index | the entries beyond the 255th are not shown (silently); a rank-254 request then costs 255 scans, the "last of its kind" shortcut being off when the count saturated |
| Names | 8.3 **short names** only: a long name appears as its short alias (`PLOT~1.GCO`), `PLOT.GCODE` as `PLOT.GCO` | the vendored sdfatlib reads no long (VFAT) names | the file is listed under its alias (display-only long names are a recorded option, `-0008`) |
| Filter | files with the extension `.gc`, `.gco` or `.nc` (`SDREADER_BROWSER_EXT1..3`), directories always | the filter works on the raw 3-character extension field | other files are not listed |
| Order | directories first, then files, each by raw 8.3 name (byte order, upper case) | `-0008` | -- |
| Time per step | one to two scans of the directory (a scan of 40 entries is 3 sectors) whatever the rank (`-0010`); entering a directory or going up costs a path walk (about one open per level) | the cache | -- |
| Blocking | a remount (card absent: sdfatlib's init timeout, up to ~2 s) | `-0009` | the explicit retry (`SW`) and the recovery after a pulled card block the same way; no job can be running |
| Card pulled **during a job** | the job stops like a refused line (ramped stop, error state `E`, `SW` to acknowledge) (`-0012`) | the card error code after each line read | -- |
| Stack | worst case **328 bytes** (the browser listing at the deepest level), RAM + stack margin **244 bytes** (flash 30 374, **346 free**) | `tools/analysis/stack_depth.py` on the SD image | -- |

On the machine (maintainer, 2026-10-13): three levels reachable and the fourth refused with the 32-byte buffer; 40-file and 35-file directories browsed; the card pulled during a navigation (found the browser stuck, hence `-0009`); the 64-byte buffer, the rank cache and the recovery are host-verified and **to be tried** with `tools/generators/gen_sd_tree.py` (`docs/bringup/Bringup-Status.md`).

## REQ-CoreLR-D-SDReader-0012 — A card pulled DURING a job stops the job (implemented 2026-10-13)
<a id="REQ-CoreLR-D-SDReader-0012"></a>
*Applies to: sd image only*


The gap left open by `-0009`: `SDReader_Task()` read the job file with `SdFile::read()`, whose `-1` means both "end of the file" and "the card failed". A card pulled during a job therefore ended the current line, then the queue ran dry and `SDReader_JobIsExhausted()` said the file was done: **the job looked finished** (the pen lifted, the position stored) with the drawing half done and no message.

**Design.** After each line read, `SDReader_Task()` looks at the `Sd2Card` error code (cleared only by its `init()`): non-zero means the read failed on the card, not at the end of the file. It then sets `s_jobFailed` (and `s_cardLost`, so that the browser remounts on its next refresh, `-0009`), does **not** publish the line just read (it may be cut in the middle) and reads no more. `SDReader_JobFailed()` reports it until the next `SDReader_OpenJob()`. In the job state machine (`grbl_corelr.ino`, SD branch) a running job that reports a failure takes the **same ramped stop as a refused line** (`BeginRampedStop(JOB_ERRORSTOP)`): the head decelerates, the machine is in the error state (`E`), `SW` acknowledges, and the browser then finds the card pulled or put back. The lines already queued are dropped with the job.
**Not done:** the screen says `E` like for any error: it does not say that the card is the cause.
**Cost:** +48 bytes of flash (30 422, **298 free**), +1 of RAM, the worst stack unchanged (328). **Verified:** `test_sdreader` section 5b (the card pulled between two lines, and in the middle of a line: no half line reaches the parser; the failure is reported; the next job opens clean; a normal end of file is *not* reported as a failure), 5 mutants of `sdreader.txt` (39 in all, all caught; one noted as equivalent). The glue in the `.ino` (the stop) is not host-tested: tried on the machine right after (maintainer), the card pulled during a job.

## Open items
- **Read-only build of the vendored sdfatlib — done 2026-09-11**
  (maintainer). The write path was dead code -- this firmware only
  ever reads -- but cost **2612 bytes of flash**: `SdFile::write` 734,
  `SdFile::addCluster` 512, `SdVolume::fatPut` 324,
  `Sd2Card::writeBlock` 220, `SdFile::availableForWrite` 188,
  `SdFile::sync` 140, `SdVolume::cacheFlush` 86,
  `SdVolume::cacheMirrorBlockFlush` 72, plus what those dragged in.
  8.5% of a Nano image for code that never runs, at a moment when the
  image was at 99% and the next feature had nowhere to go.

  It survived `--gc-sections` because `SdFile` derives from `Print`,
  whose `write()` is virtual: the vtable references the override and
  the constructor references the vtable, so deleting the functions was
  not an option. The maintainer's suggestion is what made it easy --
  keep the functions, bracket out their *bodies*. Five bodies behind a
  single `SDFAT_READ_ONLY` switch, each returning what a failed or
  vacuous write should return; the private helpers behind them needed
  no edit at all, becoming unreferenced and dropped by the linker.

  Result **30494 -> 27882 bytes (90%)**, 2838 free where 226 were.
  RAM -4, stack margin 59.

  The diff is pure addition, zero deletion, 66 lines across three
  files, five untouched, CRLF preserved -- and reversible: setting the
  switch to 0 rebuilds at exactly 30494 bytes, which is the evidence
  that the bracketing is complete. Full record, including the CRLF trap
  that caught the first attempt, in `firmware/src/sdfat/PROVENANCE.md`.
- **`SDREADER_QUEUE_SIZE` 3 -> 2, 2026-09-11**, to fund
  `MOTION_PLANNER_DEPTH` (`REQ-CoreLR-D-Motion-0013`). The trade is
  favourable by construction: a queued line costs 48 bytes to hold
  *raw text*, a planner block 9 bytes to hold an *already-parsed move*,
  so the same RAM buys roughly five times the look-ahead. Measured net
  effect, taken together with raising the planner depth to 8: flash
  -58, static RAM -39, worst-case stack margin 12 -> 51 bytes.
  Two slots still cover the latency of one SD block read while a line
  is being consumed. **Going to 1 has not been measured** and would
  remove that cushion entirely -- the parser would stall on every
  block read. Worth revisiting only with a real measurement of SD read
  latency on the maintainer's card, which does not exist yet; the
  speculative figure (margin 99) is recorded here so nobody re-derives
  it, not as a recommendation.
- **Buffer/queue sizing — resolved 2026-09-07** (`SwConfig.h`):
  `SDREADER_LINE_SIZE` 64→48 and `SDREADER_QUEUE_SIZE` 8→4, then 4→3
  during the stack-depth investigation the same day (512→144 bytes
  total), fixing a real compile-time RAM overflow (2570/2048 bytes,
  125%). 48 bytes covers the V1 dialect's longest realistic line
  (`"N100 G1 X-1234.567 Y-1234.567 F1500 S255"`, ~41 chars incl. `\0`)
  with margin; a 3-entry queue is enough lookahead since SD reads are
  fast relative to how long one line takes to execute. Flagged for
  empirical re-check once real GCode files exist to measure against.
- The SD library's own internal sector buffer (~512 bytes) is a fixed
  cost of using the Arduino `SD` library and is not reachable from
  sketch-level code — not counted as a lever under this project's
  control, unlike the two sizes above.
- **The SD browser used to overflow the stack — resolved 2026-09-10**
  by `REQ-CoreLR-D-SDReader-0005` above (389 B of stack + ~270 B of
  heap against 321 B free, now 259 B of stack, no heap, 355 B free).
  Kept here as history because the diagnosis matters more than the
  fix: the compile-time RAM percentage never showed it, `-fstack-usage`
  could not either (it reports nothing under `-flto`, and never sees
  into library code), and the 2026-09-07 pass had measured this chain
  at ~260 B yet filed it as no more urgent than the steady-state one.
  It took walking the *linked image's* call graph, and adding the
  heap, to see that the two collide. `tools/analysis/stack_depth.py` exists so
  this is a routine check rather than a one-off investigation.
- **`SDREADER_BROWSER_EXT2` was ".gcode", which could never match —
  fixed 2026-09-10** by changing it to `".gco"`. Only FAT 8.3 short
  names are visible at this layer (a property of sdfatlib, not of the
  `SdFile` rewrite: `File::name()` used the same formatter), and a
  short name's extension field is 3 characters wide, so the filter as
  written silently hid exactly the files it was meant to accept. A file
  saved as "PLOT.GCODE" appears under its generated short name,
  typically "PLOT~1.GCO", which `".gco"` matches. Found and then
  re-verified by the host test with a stubbed directory.
- **Long (VFAT) file names — deferred, V1 stays on 8.3 short names**
  (decided by the maintainer 2026-09-10). Costed by measurement, not
  estimate: the name-buffer sizes were parameterised on a copy of the
  tree and flash / static RAM / worst-case stack re-measured at each
  size (`tools/analysis/stack_depth.py`). Two designs, very different in cost.

  (a) Long names used for ordering *and* display — `FindEntryAtRank`
  carries three name buffers (floor, best, candidate):

  | Name buffer | Flash | RAM | Worst-case stack | Margin |
  |---|---|---|---|---|
  | 13 (V1) | 27258 | 1691 | 271 | **+86** |
  | 24 | 27270 | 1702 | 304 | +42 |
  | 32 | 27248 | 1710 | 333 | +5 |
  | 64 | 27380 | 1742 | 313 | **−7** |
  | 256 (FAT max) | 27458 | 1934 | 289 | **−175** |

  ≈ **4.3 bytes of RAM per supported character**. The full 255-character
  FAT limit misses the budget by 175 bytes; even 32 leaves 5 bytes,
  which is not shippable. (The stack column is not monotonic — at 64 the
  compiler re-inlines and the deepest chain moves to a different path;
  the RAM column is the reliable trend.)

  (b) Long names for **display only**, ranking still done on the short
  name:

  | Name buffer | Flash | RAM | Worst-case stack | Margin |
  |---|---|---|---|---|
  | 13 (V1) | 27258 | 1691 | 271 | +86 |
  | 24 | 27302 | 1702 | 273 | **+73** |
  | 32 | 27302 | 1710 | 273 | +65 |

  ≈ **1 byte per character**, and the stack barely moves (271→273).
  What makes (b) possible: **a long name is never needed to open a
  file.** The short name sits in the same 8.3 entry, so
  `SdFile::open(dir, shortName)` still works, and the long name can be
  assembled straight into the display buffer the UI already owns as
  static state — so it costs static RAM only, not stack.

  Costs *not* in either table, because they need real implementation:
  - **`SdFile::readDir()` becomes unusable** — it deliberately filters
    out long-name entries (`DIR_IS_FILE_OR_SUBDIR`). It would have to be
    replaced by our own raw 32-byte scan reimplementing its skip rules
    (`DIR_NAME_FREE`, `DIR_NAME_DELETED`, `.`/`..`, volume ID), ~20
    lines duplicating library logic.
  - **VFAT assembly**: long-name entries *precede* the short entry in
    reverse sequence order, 13 UTF-16 characters each split across three
    fields (5+6+2), with a checksum over the short name to detect
    orphaned entries. Budget 150–250 bytes of flash beyond the ~44 the
    buffer widening alone showed.
  - **UTF-16 → ASCII down-conversion**, needing a policy for accented
    characters.
  - **Display**: the LCD line is 16 characters, so anything past 15
    needs truncation or scrolling — UI work, and
    `REQ-CoreLR-D-LcdJoystickUI-0011`'s frame buffer is exactly 2×16.
  - The O(n²) rescan open item (solved by `-0010`) gets worse: each of the `rank+1`
    directory rescans would also have to re-assemble long names.

  If it is ever taken up, take design (b) at 24 characters. But note
  what `".gco"` already bought: a `PLOT.GCODE` file **is** now visible
  and selectable, merely shown under its short name — so long-name
  support is cosmetic, to be weighed against simply naming job files
  within 8.3 on the PC, which costs nothing at all.
