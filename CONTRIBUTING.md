# Contributing to corelr-plotter

This document is the governance of the project: how work is organised, what a change must contain, how it is merged and released.
It follows [GitHub Flow](https://docs.github.com/en/get-started/using-github/github-flow), [Conventional Commits 1.0](https://www.conventionalcommits.org),
[Semantic Versioning 2.0](https://semver.org) and [Keep a Changelog](https://keepachangelog.com).

## The method (non-negotiable)
1. **Measure, never assume.** Flash, RAM, stack and cycles are measured with the tools of [`tools/`](tools/README.md), and the figures go into the change.
2. **Discuss a design decision before coding it**, then **record it**: functional needs in `requirements/functional/` (`REQ-CoreLR-F-*`), the chosen
   way to meet them in `requirements/design/` (`REQ-CoreLR-D-*`), general documentation in `docs/`. A decision that lives only in a chat or a PR thread is lost.
3. **A new test is put through mutation testing** (`tools/mutate.py`, batches in `tests/mutation/mutants/`): a test that cannot fail proves nothing.
4. **Every tool answers `-h`** and lives in `tools/` (`tools/check_help.sh` checks it); nothing useful is left in a temporary directory.
5. **Say what was not verified.** A change that has only been checked on the host says "host-verified, not on the machine".

## Branches
- **`main`** is always buildable and tested, protected, and every commit on it is releasable. There is no `develop` branch.
- **Work branches** are short-lived, created from `main`, named `<type>/<topic-in-kebab-case>`, with the same types as commits:
  `feat/`, `fix/`, `docs/`, `refactor/`, `test/`, `build/`, `perf/`, `chore/` — e.g. `feat/arc-support`, `build/cmake-ninja`.
- **Releases** are tags `vMAJOR.MINOR.PATCH` on `main`. A `release/x.y` branch is created only when an old version has to be maintained.
- **Local scratch branches** (the maintainer's own, kept on the local machine) are never pushed. Only a reviewed, squashed change reaches the remote.

## Commits
`type(scope): imperative summary, at most 72 characters`, scopes being the modules (`motion`, `stepper`, `parser`, `ui`, `calibration`, `serial`, `sd`,
`pen`, `uart`) or `build`, `tools`, `docs`, `tests`.
- A `feat` or `fix` body explains **why**, and gives the measurement when size, stack or speed moved (flash/RAM/stack before and after).
- `BREAKING CHANGE:` in the footer for anything that changes the EEPROM format, the serial protocol, the units or the wiring: it forces a MAJOR version
  because the user has to recalibrate or rewire.
- One logical change per commit; every commit builds and passes the tests.
- Between work branches and `main`, local commit messages may be informal: **the squash message is the one that counts** and follows the format above.

## Pull requests
- Every change to `main` goes through a pull request, the maintainer's own included, and is **squash-merged** (linear history, one commit per change).
  The PR title is the Conventional Commit line.
- A PR states: the requirement(s) it realises or amends (or why there is none), the documentation and requirement changes (made in the same PR), and the
  **size table** (flash / RAM / worst stack, before → after, both variants) for any change to `firmware/`.
- CI must be green. While there is a single maintainer, self-approval is accepted; bypassing CI is not.

## Continuous integration (blocks the merge)
Both variants built with the pinned toolchain (avr-gcc 7.3.0) in both arithmetics; flash ≤ 30 720 and RAM + worst stack within budget; the host tests in
`CORELR_MATH_48` and `CORELR_MATH_64`; `tools/check_help.sh`; `tools/check_links.py`; the documentation target (`doxygen_*` tests: no markup warning, both variants documented). A second job (`cmake-3-20`) does the builds and the host tests again with **CMake 3.20.6**, the minimum the project announces, configured by hand (the presets need CMake 3.25).
**Mutation testing** is too slow for every PR: it is the workflow `mutation` (`tools/mutation_ci.sh`), run by hand from the Actions tab and by the `release` workflow on a tag, and a clean run is a release criterion. The host tests do check, on every PR, that every mutant of every batch *can* run (`tests/tools/test_mutant_batches.py`).

## Protecting `main` (repository settings)
Pull request required; required checks; branch up to date before merge; linear history; no force-push; no deletion; conversations resolved.
Signed commits are **not** required for now.

## Versions, changelog, releases
- **MAJOR**: a change that forces a recalibration, alters the serial protocol or the wiring. **MINOR**: a compatible feature. **PATCH**: a fix.
- [`CHANGELOG.md`](CHANGELOG.md) keeps an `[Unreleased]` section updated by every PR.
- **Before a release, re-evaluate the size flags** (`cmake/size_flags.txt`, `REQ-CoreLR-D-Build-0012`): run `tools/analysis/flag_truth_table.py -v sd` and `-v serial` (every combination, about two minutes each), read what each flag brings alone, with the others and on average, drop the ones that no longer pay or that hurt, and keep the reports in `docs/design-notes/`. The first release (V1) is the first time this is owed.
- A **release** is a tag and a GitHub Release carrying, for **each variant** (`sd`, `serial`):
  - `corelr-<version>-<variant>.hex` — the file to flash;
  - `corelr-<version>-<variant>.elf` — with debug symbols: needed to reproduce the size, function-size and stack analyses (`tools/analysis/`);
  - `corelr-<version>-<variant>.map` — the linker map;
  - `corelr-<version>-<variant>-api.zip` — the API documentation of that image (`cmake --build --preset host --target doc_package`, then `build/doxygen/dist/`; `<version>` is `CORELR_VERSION` in `CMakeLists.txt`, bumped before the tag; `REQ-CoreLR-D-Build-0016`);
  - the size report (flash, RAM, worst stack) and `SHA256SUMS` for every file above;
  - the exact **toolchain and options** the build used (compiler version, flags, `CORELR_*` settings), so the build can be reproduced;
  - the release notes: what changed, **what was verified on the machine and what was not**, and whether the first flash costs a recalibration.
  Also `corelr-<version>-<variant>-ide.zip`, the sketch exported for the Arduino IDE (`export_ino`), and `build-info.txt`.
  **The workflow `release` does this** (`.github/workflows/release.yml`): pushing the tag `vX.Y.Z` builds, tests, collects the files with `tools/release_package.sh` and creates a **draft** release; the tag must equal `v` + `CORELR_VERSION`. The maintainer writes the notes and publishes the draft. "Run workflow" on a branch does the same without the release (the files are the artifact `release`). On a tag the workflow `mutation` runs first, and the release is prepared only if it passes (about an hour); "Run workflow" on a branch skips it.
  The source archive is generated by GitHub. **No `.eep` file is shipped**: the EEPROM holds the calibration and the position, and flashing an image of it would erase them.

## API documentation (Doxygen)
```
cmake --preset host && cmake --build --preset host --target doc      # or any other preset: no AVR toolchain is used
```
writes one documentation **per firmware image**, like the firmware: `build/doxygen/serial/html/index.html` and `build/doxygen/sd/html/index.html` (`--target doc_serial` or `doc_sd` for one; `build/doxygen/index.html` links both), each with its `doxygen-warnings.log`, then prints the warnings by module. It needs Doxygen (`sudo apt install doxygen`); `graphviz` is needed for the diagrams of the comments and the include graphs, but without it the targets still work (no diagrams, and the summary says so); without Doxygen the configuration still works and the target says what to install. The targets are **not** part of the default build, read the sources, write only under `build/`, and change no image. The settings and their reasons are in `REQ-CoreLR-D-Build-0013`, the Doxyfile is `docs/doxygen/Doxyfile.in`; **Doxygen 1.15.0 is the version it was written for** (`docs/TODO.md`).
A module or a library that exists in one image only (`SDReader`, `sdfat`, SPI; `Uart`, `SerialLink`) is listed in `cmake/variant_modules.txt` (the build, the IDE export, the reference build and the documentation read it: the other image does not compile it) **and**, for a module, is wholly under `#if CORELR_JOB_SOURCE == ...` in its header and its source, so that it is empty in the other image; `tests/tools/test_variant_modules.py` fails if the two disagree. The vendored code is read by Doxygen as it is and grouped in `docs/doxygen/third_party*.dox` (a new vendored file is added there; its warnings are counted apart, "external").
Diagrams in code comments are `\dot ... \enddot` (native, no script, no Node); no Mermaid in code or for Doxygen (it waits for a Doxygen 1.17 or later, `docs/TODO.md`), Mermaid is fine in the Markdown files. One representation per diagram: do not add a `\dot` next to an ASCII sketch of the same thing, replace it; ASCII art stays for what `dot` draws badly (speed profiles, geometry, bit layouts).
The generated pages are never committed (they go to the release as a zip, `REQ-CoreLR-D-Build-0016`).
A requirement that applies to one image only carries the tag `*Applies to: serial image only*` (or `sd`) under its heading; with no tag it applies to both (`requirements/README.md`).
An item of the serial protocol is cited as `@proto{FW-03}` (or `@protosec{4-6,4.6}` for a section) in the same comments.
A comment that Doxygen reads cites a requirement as **`@req{REQ-CoreLR-F-Area-nnnn}`** (a link to the requirement in the *Requirements* chapter; one per requirement, not on a title line; an ordinary comment keeps the plain identifier), and each requirement heading carries its invisible anchor: `docs/standards/STD-FileOrganization.md`, "Citing a requirement", checked by `ctest` (`req_refs`, `tools/analysis/req_refs.py --add-anchors` for a new requirement).
The page *GCode* (`docs/doxygen/gcode.md`) lists, per image, what the parser accepts and refuses, and **its tables are tests**: a change that adds, removes or re-numbers a G word, an M word, a letter or a status number must update the page in the same change, or `ctest` (`gcode_page`) fails and says which row or word (`REQ-CoreLR-D-Build-0018`).
A change that touches comments keeps the **markup** warnings at 0 in both documentations (`tools/analysis/doc_warnings.py --list markup build/doxygen/serial/doxygen-warnings.log`, and `sd/`; `ctest --preset host` fails on one). Undocumented items and incomplete `@param` lists are gaps that the same tool lists, and `docs/doxygen/gaps_serial.txt` / `gaps_sd.txt` hold the inventory: **a new function, macro or parameter without a comment fails `doxygen_output`** (document it; never add a line to the file), and a gap you closed must be removed from it (`tools/analysis/doc_warnings.py --gaps build/doxygen/serial/doxygen-warnings.log > docs/doxygen/gaps_serial.txt`, same for `sd`), so that the list only shrinks (`REQ-CoreLR-D-Build-0017`; it is compared only for the Doxygen version that wrote it, 1.15.0). A new `SwConfig.h` selector with a non-zero default that an `#if` tests goes into `EXPAND_AS_DEFINED` of the Doxyfile (see the requirement).

## Quality rules for code
Coding standards: [`docs/standards/`](docs/standards/README.md). C, not C++, except behind plain-C headers; GPLv3 header on every source file; English; identifiers
prefixed by their owning module; fixed strings in flash (`PSTR`); no `malloc`; no floating point on the hot path. `third_party/` is exempt and is changed only
with a note in its `PROVENANCE.md`.

## Conduct and security
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) applies to every space of the project. Defects that could make the machine move unexpectedly are reported
as described in [`SECURITY.md`](SECURITY.md), not in a public issue.
