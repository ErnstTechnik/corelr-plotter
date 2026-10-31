# Functional Requirements — Build

Sub-function: **Build** — what the project must be able to produce and how reproducibly. Not a machine behaviour: a property of the delivered project.

Origin: maintainer decision, 2026-10-10 ("Build outside the IDE") and 2026-10-11 (repository restructuring, local tool paths); see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-10-11)
See also: [`../design/REQ-CoreLR-D-Build.md`](../design/REQ-CoreLR-D-Build.md)

## REQ-CoreLR-F-Build-0001 — Self-sufficient build
<a id="REQ-CoreLR-F-Build-0001"></a>
A firmware image shall be buildable from this repository with a compiler toolchain and nothing else: no IDE, no reference project (`grbl`, `Walldraw`), no
library fetched at build time.

## REQ-CoreLR-F-Build-0002 — One image per job source
<a id="REQ-CoreLR-F-Build-0002"></a>
Each job source (SD card, serial link — `REQ-CoreLR-F-SerialControl-0003`) shall be a separate image built from the same sources, and each image shall be
flashable as it comes out of the build.

## REQ-CoreLR-F-Build-0003 — The image fits the board
<a id="REQ-CoreLR-F-Build-0003"></a>
A build shall fail, not merely warn, when an image exceeds the flash or the static RAM of the Arduino Nano (30 720 bytes of flash — not the Uno's 32 256 — and
2 048 bytes of RAM).

## REQ-CoreLR-F-Build-0004 — Reproducible figures
<a id="REQ-CoreLR-F-Build-0004"></a>
The size, stack and speed figures recorded in this project are those of a stated toolchain. A build with a different compiler shall say so, and a change of
build system shall be proven to produce the same image before the recorded figures are relied on again.

## REQ-CoreLR-F-Build-0005 — Machine-specific settings stay out of the repository
<a id="REQ-CoreLR-F-Build-0005"></a>
Everything that depends on the machine — where the toolchain is, which serial port the board is on — shall live in one file that is not versioned, with a
documented template; no source, script or tool shall contain a user's path.

## REQ-CoreLR-F-Build-0006 — A release is complete
<a id="REQ-CoreLR-F-Build-0006"></a>
A release shall carry, for every variant, the file to flash, the debug image and linker map from which the measurements can be redone, the size report, checksums, and the
toolchain and options used. It shall never carry an image of the EEPROM, which would erase the calibration.

## REQ-CoreLR-F-Build-0007 — The software version is written once and is the one of the delivery
<a id="REQ-CoreLR-F-Build-0007"></a>
The firmware shall carry one software version, written in one place, that is the version of the delivery tag on a release. A build between two releases shall say that it is a
development build. The version shall not depend on the history of the repository: two builds of the same sources give the same image (the images are compared byte for byte).
*Prompted by the maintainer's remark of 2026-10-31 (the version was in two places, tied to no tag; a git hash cannot be in an image that is itself in the repository).*
