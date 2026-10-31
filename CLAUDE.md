# CoreLR — instructions for an AI assistant (and a summary for people)

This project is developed by its maintainer **with the assistance of Claude** (Anthropic's coding assistant): the maintainer decides and verifies on the
machine, the assistant proposes, implements and documents. This file is what the assistant reads at the start of a session; it is also the shortest
statement of how the project works. It does not replace [`CONTRIBUTING.md`](CONTRIBUTING.md) (governance) or the requirements (the decisions).

## What this is
Firmware for a **cable-driven wall plotter**: two stepper motors, **L** (left) and **R** (right), each paying out one cable; a pen raised and lowered by a servo;
an Arduino **Nano** (ATmega328P, 2 KB of RAM, 30 720 bytes of flash after the bootloader); a 2×16 I²C LCD and an analog joystick. Jobs come from an **SD card**
(variant `sd`) or from the **serial link**, a grbl 1.1 subset driven by Candle 1.1.9 (variant `serial`): one image per variant, chosen at build time
(`CORELR_JOB_SOURCE`). It is a from-scratch design: `grbl` was the algorithmic reference, `Walldraw` the hardware reference ([`docs/hardware/Walldraw.md`](docs/hardware/Walldraw.md)).

The current state (sizes, what is verified on the machine) is **not** repeated here, because it ages: see [`README.md`](README.md), [`CHANGELOG.md`](CHANGELOG.md)
and the dated journal [`docs/bringup/Bringup-Status.md`](docs/bringup/Bringup-Status.md).

## Naming
The two motors are **L** and **R** in code, comments, documentation and requirements — never A/B.

## Repository layout
```
firmware/      what is compiled for the AVR, and nothing else:  app/ (sketch, job state machine)  src/ (one .h + .c/.cpp per module)  config/ (HwConfig.h, SwConfig.h, Version.h)
third_party/   vendored code with its own licence and a PROVENANCE.md (sdfat, the Arduino AVR core)
requirements/  functional/ (REQ-CoreLR-F-*: WHAT)  design/ (REQ-CoreLR-D-*: HOW); README.md = index and traceability rule
docs/          hardware/ protocol/ bringup/ design-notes/ standards/ doxygen/ branding/
tests/         host/ (the real modules on the PC)  mutation/ (mutants)  bench/ (sketches and GCode for the machine)
tools/         analysis/ bench/ generators/ prototypes/ + mutate.py, check_help.sh, check_links.py; every tool answers -h; index in tools/README.md
```
Build: `cmake --preset avr-default && cmake --build --preset avr-default` (both variants). Tests: `cmake --preset host && ctest --preset host` (both arithmetics).
Toolchain and options: [`README.md`](README.md), [`requirements/design/REQ-CoreLR-D-Build.md`](requirements/design/REQ-CoreLR-D-Build.md).

## Working method
- **Measure, do not assume.** Flash, RAM, stack and cycles are measured with the tools of `tools/`; a claim about concurrency, atomicity or timing is checked against
  the code, not asserted from a general rule. The maintainer is a hard real-time embedded engineer and will check.
- **Discuss a design before coding it; requirements first.** A decision is written under `requirements/` (functional for what, design for how) before or with the code.
  Where a function has no requirement yet, study `grbl` for ideas and write our own requirement; do not copy its code.
- **Mutation testing** (`tools/mutate.py`, batches in `tests/mutation/mutants/`) for every tested behaviour: a test that cannot fail proves nothing.
- **Tools** live in `tools/`, answer `-h`, have an entry in `tools/README.md` and in `tools/check_help.sh`; none is left in a temporary directory.
- **Say what was not verified**: "host-verified, not on the machine" is a legitimate status; a claim without a measurement is not.
- Documentation in English. GPLv3 header on every source file; new files carry `Copyright (C) 2026 ErnstTechnik and the CoreLR contributors`.
  No personal name, e-mail address or home-directory path in any file, comment or document (use `~`, `$HOME` or a relative path).
- Commits follow Conventional Commits ([`CONTRIBUTING.md`](CONTRIBUTING.md)). Those made with the assistant end with `Co-Authored-By: Claude <noreply@anthropic.com>`
  (the model's exact name may follow). Nothing is pushed by the assistant.

## Hardware guard-rails (the machine is real)
- **Never flash** (`upload_*` targets) **and never open the serial port** without the maintainer's say-so: both reset the board, and a reset before the position is stored
  (2 s of stillness) loses the calibration.
- **Never ship an `.eep`**: it would overwrite the calibration in EEPROM.
- Pins (full table in [`docs/hardware/PinMapping.md`](docs/hardware/PinMapping.md)): motor R `D2–D5`, motor L `D6–D9`, SD chip-select `D10` with hardware SPI `D11–D13`, serial `D0/D1`,
  servo `A0`, joystick `A1–A3`, LCD I²C `A4/A5`. No spare digital pin. External 5 V is required for the motors and the servo.

## Conventions
Rules are in [`docs/standards/`](docs/standards/) (`STD-Naming`, `STD-FileOrganization`, `STD-MemoryAndPerformance`, `STD-ModuleDesign`). What CoreLR plugs in:
- Header guards `CORELR_<MODULE>_H`; every other identifier is prefixed with its **owning module** (`Stepper_PenAction_t`, `STEPPER_QUEUE_SIZE`). External identifiers ≤ 31 characters (MISRA 5.1).
- **C, not C++**; the imported libraries that need C++ (LCD, SD) are isolated behind C headers. One `.c`/`.cpp` per module; private types stay in the `.c`.
- Configuration is centralised: `HwConfig.h` (pins, fixed mechanical facts), `SwConfig.h` (tunables, queue sizes, the variant, the arithmetic `CORELR_MATH`), Doxygen `@defgroup` sections.
- Text is composed with `TextFmt`, never `snprintf` (one `snprintf_P` call costs 1 372 bytes of flash). Fixed LCD strings go through `PrintPadded_P()`/`PSTR()`; constant tables are `PROGMEM`.
- Size flags per variant: `cmake/size_flags.txt`; `-mcall-prologues` is mandatory (an `#error` checks it).
- Requirement identifiers: `REQ-CoreLR-F-<Area>-nnnn` and `REQ-CoreLR-D-<Module>-nnnn`; each design item says which functional requirement it realises.

## The serial protocol is split
The host side owns [`docs/protocol/SerialProtocol.md`](docs/protocol/SerialProtocol.md) (the contract and its requests `FW-nn`); the firmware side implements and reports.
An assistant working on the firmware does not write the host's half, and the other way round.

## Releases
A tag `vX.Y.Z` makes [`release.yml`](.github/workflows/release.yml) prepare a **draft** release per variant (`.hex`, `.elf`, `.map`, IDE export, API documentation, `build-info.txt`,
`SHA256SUMS`). The maintainer writes the notes — what was and was not verified on the machine — and publishes. Before a release: the mutation workflow runs first on the tag (by hand otherwise), and the size-flag
truth tables are redone (`tools/analysis/flag_truth_table.py`).
