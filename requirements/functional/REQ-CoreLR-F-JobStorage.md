# Functional Requirements — Job Storage

Sub-function: **JobStorage** — load GCode job files from the SD card.

Origin: see workspace `Traceability.md` (not part of this repository).
Status: Approved (2026-09-06)
See also: [`../Design/REQ-CoreLR-D-SDReader.md`](../design/REQ-CoreLR-D-SDReader.md)

## REQ-CoreLR-F-JobStorage-0001 — Load from SD
<a id="REQ-CoreLR-F-JobStorage-0001"></a>
*Applies to: sd image only*

The system shall load and execute GCode job files stored on an SD card.

## REQ-CoreLR-F-JobStorage-0002 — Streaming
<a id="REQ-CoreLR-F-JobStorage-0002"></a>
*Applies to: sd image only*

Job files larger than available RAM shall be supported, by reading and
executing content progressively rather than loading the whole file at
once.

## Open items
None beyond what is tracked in the linked Design file (buffer/queue
sizing, resolved 2026-09-07). See also
[`REQ-CoreLR-F-SerialControl-0003`](REQ-CoreLR-F-SerialControl.md):
SD-sourced jobs and live-serial-sourced jobs are mutually exclusive,
selected at compile time (`CORELR_JOB_SOURCE` in `SwConfig.h`) — never
both linked into the same firmware image.
