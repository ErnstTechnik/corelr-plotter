# Vendored sdfatlib

These eight files are the **lower layer of the Arduino `SD` library**,
copied here on 2026-09-10. Five are byte-identical to upstream; three
carry deliberate, documented modifications (`SDFAT_READ_ONLY`,
2026-09-11, and `SDFAT_NO_SERIAL`, 2026-10-06 — see below).

| | |
|---|---|
| Source | Arduino `SD` library **version 1.3.0** (`author=Arduino, SparkFun`), directory `SD/src/utility/` |
| Upstream author | sdfatlib, Copyright (C) 2009 William Greiman |
| Licence | GPLv3 — the same licence CoreLR uses, so vendoring is unencumbered |
| Modifications | **two, deliberate**: the `SDFAT_READ_ONLY` and `SDFAT_NO_SERIAL` switches — see below. Three files touched by pure addition, five untouched. |

Unmodified, identical to upstream:

```
ebf2127a75f6fe7a80893c45cf2a82f2  FatStructs.h
0feb97602074d486b2f1f609f3f569b9  Sd2Card.cpp
b33e88e4d15b11d83a1ea851eb94588a  Sd2Card.h
e455d522d98c2b0e7cb603b7ca6d5ea0  Sd2PinMap.h
09a324bd145bed32a0cfb043afcf55b8  SdInfo.h
```

Modified, current checksums (upstream's in brackets):

```
1920893505d171035cb6b239a061d8b4  SdFat.h      [91a808a227671e7af7ce47f7260f694d]
19edc28bff7871c2267b853875de9c6a  SdFile.cpp   [dec01809be7810044180d6dda0ff18d6]
3a9bb7066c4977cc66c64604b3c68f7c  SdVolume.cpp [2cbf867bff9e63c213d6784751bb3645]
```

## Why these files are here rather than `#include <SD.h>`

The `SD` library is two stacked layers: `SDClass`/`File` on top
(`SD.cpp`, `File.cpp`) and sdfatlib underneath (this directory).
`SDReader` uses only the lower one — see
`REQ-CoreLR-D-SDReader-0005` for the measurements that led there.

Depending on the library while using only its lower half still cost
**73 bytes of RAM**: `SD.cpp` defines the global `SDLib::SD` object,
whose members carry a vtable (`SdFile` derives from `Print`), so it
needs dynamic initialisation, so its constructor is registered in
`.init_array` — which the linker keeps even under `--gc-sections`,
even though not one byte of its code is reachable any more. The only
way to reclaim those bytes is for `SD.cpp` to never be compiled, and
the Arduino IDE compiles every `.cpp` in a library it considers used.

Kept out deliberately: `SdFatUtil.h` (only `SD.h` included it) and
`SdFatmainpage.h` (Doxygen front page for the upstream project).

## First modification: `SDFAT_READ_ONLY` (2026-09-11)

### Why

The write path is dead code here -- this firmware only ever reads the
card -- but it cost **2612 bytes of flash**, measured: `SdFile::write`
734, `SdFile::addCluster` 512, `SdVolume::fatPut` 324,
`Sd2Card::writeBlock` 220, `SdFile::availableForWrite` 188,
`SdFile::sync` 140, `SdVolume::cacheFlush` 86,
`SdVolume::cacheMirrorBlockFlush` 72, plus what those dragged in. On an
Arduino Nano, whose bootloader leaves 30720 bytes, that was 8.5% of the
image for code that never runs -- and the image was at 99%, with no
room for the next feature.

It survives `--gc-sections` because `SdFile` derives from `Print`,
whose `write()` is virtual: the vtable references the override and the
constructor references the vtable, so the linker cannot see that the
chain is unreachable at runtime. **Deleting the functions is therefore
not an option** -- the vtable needs them to exist.

### What was done

`SDFAT_READ_ONLY`, defined once at the top of `SdFat.h`. Five function
*bodies* are bracketed by `#if SDFAT_READ_ONLY` / `#else` / `#endif`,
each returning what a failed or vacuous write should return:

| Function | Read-only behaviour |
|---|---|
| `SdFile::write(const void*, uint16_t)` | returns 0, nothing written |
| `SdFile::availableForWrite()` | returns 0, never any room |
| `SdFile::sync(uint8_t)` | returns true — nothing is ever dirty (the `isOpen()` guard above it is kept, so a closed file still reports failure) |
| `SdVolume::cacheFlush(uint8_t)` | returns true — the cache is never dirty. Still called from `cacheRawBlock()` on the **read** path, hence bracketed rather than removed |
| `SdVolume::fatPut(uint32_t, uint32_t)` | returns false — the FAT is never modified |

Nothing else is touched. `SdFile::write(uint8_t)` needed no edit: it
forwards to `write(buf, 1)` and inherits the 0. The private helpers
behind the write path (`addCluster`, `addDirCluster`, `writeBlock`,
`cacheMirrorBlockFlush`, `allocContiguous`, `freeChain`,
`cacheZeroBlock`, `Sd2Card::erase`) were left alone: with nothing left
calling them they became unreferenced and `--gc-sections` dropped them
by itself, which is visible in the symbol table.

### Properties of the diff, deliberately

- **Pure addition, zero deletion**: 40 lines in `SdFat.h`, 15 in
  `SdFile.cpp`, 11 in `SdVolume.cpp`. Five files untouched.
- **CRLF line endings preserved.** The upstream files use CRLF; an
  early attempt rewrote them as LF and `diff` then reported every line
  as changed, which defeats the whole point. Watch for this when
  editing: a `sed` anchored on `$` will not match, because the `\r`
  sits before the newline.
- **Reversible, and verified to be**: setting `SDFAT_READ_ONLY` to 0
  rebuilds at 30494 bytes -- byte for byte the size before the change
  -- while 1 gives 27882. That the escape hatch reproduces the old size
  exactly is the evidence that the bracketing is complete and affects
  nothing else.
- Every edit is findable by searching for `SDFAT_READ_ONLY`, so a
  future re-copy from a newer library stays mechanical: re-copy, then
  re-apply five brackets.

## Second modification: `SDFAT_NO_SERIAL` (2026-10-06)

### Why

`SdFile::ls()` and four helpers it calls — `printDirName()`,
`printFatDate()`, `printFatTime()`, `printTwoDigits()` — are a debugging
aid that prints to `Serial`. Nothing here calls them, so `--gc-sections`
does remove their code. **That is not enough**, and the reason is
worth recording: the linker decides which archive members to pull in
from the *undefined symbols* of the objects it has already taken, before
garbage collection. These five functions leave `Serial` undefined, so
the core's whole `HardwareSerial` was linked in — the `Serial` object
with its 64-byte receive and 64-byte transmit buffers, `write`, `read`,
`flush`, `available`, `peek`, two interrupt vectors, and the
`serialEventRun` that `main()` then called every pass — for the sake of
code that was discarded anyway.

Measured, Nano fqbn, on the day (nothing else changed):

| | before | after | |
|---|---|---|---|
| Flash | 29798 | **29066** | **−732 bytes** |
| Static RAM | 1712 | **1537** | **−175 bytes** |

The RAM is the larger prize: the stack margin goes from +40 to +215.
It had been misread for a month — the project's working notes said that the core
"allocates `Serial`'s buffers unconditionally on a UART board", which
is true of a sketch that *uses* `Serial` and not of one that merely
links a library that mentions it.

### What was done

`SDFAT_NO_SERIAL`, defined once in `SdFat.h` beside `SDFAT_READ_ONLY`.
It brackets two regions of `SdFile.cpp`: the whole definition of
`SdFile::ls()` (with its comment), and the contiguous block
`printDirName()` … `printTwoDigits()`. Declarations in `SdFat.h` are
left alone — an undefined member function that nobody calls links fine.

### Properties of the diff

- **Pure addition, zero deletion**: 17 lines in `SdFat.h`, 4 in
  `SdFile.cpp`. CRLF preserved (`file` reports CRLF for both).
- **Reversible, and verified to be**: setting `SDFAT_NO_SERIAL` to 0
  rebuilds at 29798 / 1712 — byte for byte the size before the change.
- Every edit is findable by searching for `SDFAT_NO_SERIAL`. A future
  re-copy from a newer library is: re-copy, then re-apply two brackets
  here and five for `SDFAT_READ_ONLY`.
- **If a future change really does want `Serial`** (the serial-control
  variant, `REQ-CoreLR-F-SerialControl`) it will link `HardwareSerial`
  itself, legitimately, and this switch is irrelevant to it.

## Rules for this directory

- **Do not reformat, rename or Doxygen-ify these files.** They are
  third-party code, kept as close to upstream as possible so that a
  future update is a re-copy plus a re-apply of the recorded brackets,
  not a merge. The CoreLR coding standards
  (`Arduino_Coding_Standards/`) do **not** apply here.
- If a change becomes unavoidable, record it in this file with its
  reason, keep it a bracketing edit behind a named macro, preserve the
  CRLF line endings, and refresh the checksums. That is exactly how
  `SDFAT_READ_ONLY` and `SDFAT_NO_SERIAL` were done — follow them.
- Everything CoreLR itself adds lives in `SDReader.cpp`/`.h`, outside
  this directory.
