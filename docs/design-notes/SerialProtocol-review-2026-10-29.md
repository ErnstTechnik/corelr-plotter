# Review of `docs/protocol/SerialProtocol.md` -- the gaps found by the firmware side (2026-10-29)

For the host side, which owns the file. **Nothing here has been changed in the protocol** except one line, at the maintainer's word: line 34 (the planner depth) was
corrected and a row was added to the revision history. Line numbers are those of the file as it was on 2026-10-29, before its items became links.

How it was read: against the firmware (`SerialLink.c`, `Uart.c`, `GCodeParser.c/.h`, `SwConfig.h`), against the requirements (`requirements/`), against the project's state, and with the
checks that the API documentation now runs on it (`tools/analysis/req_refs.py`). Four groups: what breaks the document (A), how it cites (B), what is out of date (C), and what was
compared and found right (D) so that nobody checks it again.

## A. Structure and markup
| # | Where | Gap | Suggested fix |
|---|---|---|---|
| A1 | lines 427-434, the table of section 7 | **The table is cut by blank lines**: line 429 (before the row `FW-12`, line 430) and line 432 (before the row `FW-14`, line 433). On GitHub and in the API documentation `FW-12`, `FW-13` and `FW-14` come out as loose paragraphs, not as rows of the table of requests. | remove the two blank lines |
| A2 | lines 458-459, the table of section 7.2 | `FW-14` is listed before `FW-13` | put `FW-13` before `FW-14` |
| A3 | lines 490-504, the table of section 10 | the points are not in order: `P12` comes before `P11`, `P15` after `P17` | sort them (`P1` to `P17`) |
| A4 | lines 507-512, "Still open:" | `P5` and `P7` are **bullets, not rows**, so the table of points has no row for them although the document cites them many times (lines 230, 265, 384, 398, 439, 462, 507-511, 537, 539, 591). `P5` says "decided, Candle" and is listed under "Still open". | give each point a row in the table (decided or open, with a column for the state), or at least a row for `P5` and `P7`; move `P5` out of "Still open" |
| A5 | line 563, the heading "Appendix A -- Survey references (file:line ...)" | the text `file:line` is read as a URL by Markdown processors (Doxygen writes a link that goes nowhere) | write it as `file:line` between backticks, or "file and line" |
| A6 | headings | numbering 1 to 11, 4.1 to 4.13, 6.1 to 6.4, 7.1 to 7.2, 11.1: no gap, no duplicate | none |

## B. How the requirements are cited
All the requirement identifiers that the file cites **exist** (40 full citations, none unknown). The forms are not uniform, and only the full form can be turned into a link
automatically (the API documentation links `REQ-CoreLR-F-Area-nnnn`):
| # | Form | Lines | What it looks like |
|---|---|---|---|
| B1 | **full** | 40 citations | `REQ-CoreLR-D-SerialLink-0002` |
| B2 | **short**, without `REQ-CoreLR-` | 32, 337, 376, 452, 457, 461, 462, 469, 470, 475, 500, 501 (18 citations) | `D-GCodeParser-0013`, `D-SerialLink-0002`, `F-SerialControl-0013`, `D-Motion-0030`... |
| B3 | **file-level**, no number | 84, 425, 451, 481 | `REQ-CoreLR-D-LcdI2c`, `REQ-CoreLR-F-SerialControl`, `REQ-CoreLR-D-Uart`, `REQ-CoreLR-F-GCodeInterpreter` |
| B4 | **relative**, the number alone, taking the prefix of the citation before it | 39 lines: 16, 17, 29-31, 178, 179, 194, 199, 252, 268, 291, 308, 371, 376-381, 383-387, 389-393, 395, 396, 398, 454, 455, 457, 480, 493, 501 | `` `-0006` ``, `` `REQ-...-0001`, `-0004`...`-0014` ``, `` `-0012`...`-0018` `` |
Suggested: write the **full** identifier everywhere (B2, B4), and for B3 either a number or "the requirements of file X". If the file stays as it is, the API documentation's input filter
will have to resolve B2 and B4 itself (planned for 2026-10-30).

## C. Out of date
| # | Where | What the file says | What is true now |
|---|---|---|---|
| C1 | line 34 | planner depth 8 in serial, 4 in SD | **corrected on 2026-10-29**: 8 in both (a RAM lever, 8 recommended), `MOTION_PLANNER_DEPTH 8u` in `SwConfig.h` |
| C2 | lines 26, 369 | SD variant "568 free" on 2026-10-11 | 30 524 / 30 720, **196 free** (images of 2026-10-27); date the figure or update it |
| C3 | line 28 | serial variant "27 582 (3 138 free)" on 2026-10-11 | 28 766 / 30 720, **1 954 free** (2026-10-27) |
| C4 | line 90 | "the rings are 128 + 64 bytes" | the transmit ring is **128** (`UART_TX_SIZE 128u`, with an `#error` below 128); the row of `FW-02` in 7.2 (line 451) already says "128 now". (The 64-byte ring of line 81 is `HardwareSerial`'s and is right.) |
| C5 | line 251 | "host-verified, **not yet run on the machine**" (section 4.9) | verified on the machine with Candle 1.1.9 on 2026-10-12, as the revision history says |
| C6 | line 461, the row of `P2/P6` | "Candle's own jog buttons not yet tried" | tried: Candle's jog, Zero XY, Home, Safe position and Abort are verified on the machine |
| C7 | lines 348-362, section 6.3 `$$` | "Proposal, every value fixed in flash", "About 60 bytes (estimate)" | **built**: `SerialLink.c` writes exactly `$10=3 $13=0 $20=0 $21=0 $22=0 $32=0` |
| C8 | line 447, the title "7.2 Where each request stands (2026-10-11)" | dated 2026-10-11 | the table holds items of 2026-10-12 (`FW-12` to `FW-14`, `M9`/`T`) |
| C9 | line 465, section 8 | "Results as of 2026-10-11" | the serial image's flash and RAM have moved (see C3) |
| C10 | line 507 | `P5` under "Still open", "decided" | see A4 |

## D. Compared with the firmware and found right
So that the host side need not check them again:
- **section 4.6, the error table**: every `error:N` against `GCODEPARSER_ERR_*` (`GCodeParser.h`) and `SERIALLINK_ERR_*` (`SerialLink.c`): 1, 2, 3, 8, 9, 11, 20, 21, 26, 33, 34, 35, 36.
  The causes were run through the real parser (`G2 X10 Y0` is 35, `G2` alone 26, `L1` and `P1` alone 36, `Z` with an `M` word 36, `G53` without a motion word 20, `Z`, `T` and `M9` in the SD
  build 20).
- **section 6.4, the command set per variant**: every row about a GCode line against the real parser of each image (`tests/tools/test_gcode_page.py` runs them): `G53`, `M9`, `T`, `Z`, the
  program end, `G92`/`G10`, the refused words.
- the welcome line (`Grbl 1.1h ['$' for help]`), `$I` (`[VER:1.1h.20261011:CoreLR]` and `[OPT:,8,128]`), the `$$` list, the state names (`Idle`, `Run`, `Hold:0`, `Hold:1`, `Jog`, `Alarm`),
  the 128-byte receive ring, the 80-character line (`error:11`), the `WCO` field sent in the first report, every tenth and when the offset changes.
- no personal name, no address and no path of a machine in the file; every file path it cites exists; every commit hash of the project that it cites exists (the hashes of the appendix are
  those of other projects).

## E. What the API documentation does with the file (for awareness)
The protocol is a page of the documentation, marked as the serial image's in the `sd` documentation. Its numbered sections get a label (`proto-4-6`), the **first** table row of each
`FW-nn`, `Dn`, `Pn` and `M-n` gets an anchor (so the repeated rows of 7.2 are references, not definitions; `P5` and `P7` get none: see A4), and the plain citations are linked where the form is
unambiguous. None of this changes the file. A1 breaks the table of requests in the documentation as it does on GitHub.

## F. Not checked
The sender survey (section 11) and its appendix (the code of grbl, Candle, UGS... at the cited commits), the measurements `M-1` to `M-7` (figures of the host and firmware sides), the bench
captures of Candle's Abort. They are records, not a specification of the firmware.

## G. What the firmware side changed afterwards (2026-10-31), for the file's owner
Nothing in `SerialProtocol.md` was edited for these; they are for the host side to take into the text.
- **`$I` now carries the software version** in the free text after the second colon: `[VER:1.1h.20261011:CoreLR <version>]` (`<x.y.z>-dev` between two releases, `<x.y.z>` on a release). The date (`FW-13`, `P15`) is **unchanged** on purpose, a sender may parse it; grbl and Candle treat what follows the second colon as free text. Rows to update: section 6.1 (line 333 of the file of 2026-10-29), and `FW-13`, which says the reply is `20261011` and no more. The version is written once, in `firmware/config/Version.h` (`REQ-CoreLR-D-Build-0020`); between two releases it ends in `-dev`.

## H. The relative citations that the documentation cannot link (2026-10-31), for the file's owner
The API documentation now links the short form (`D-GCodeParser-0013`), the file-level form (`REQ-CoreLR-D-LcdI2c`) and the relative form (`-0006`) wherever the context says which requirement is meant: the citation before it in the same line or table cell, else the first after it in the same table row, else the one above it in the same column. Nine relative citations have none of these, so they are left as plain text (`tools/analysis/req_refs.py --unresolved` lists them). **Write the full identifier** and they become links; the one each most likely means, from the requirement titles:
| Line | Now | Probably |
|---|---|---|
| 178 | `-0011`, `-0016` | `REQ-CoreLR-D-GCodeParser-0011` (reach), `REQ-CoreLR-D-GCodeParser-0016` (travel zone) |
| 179 | `-0009` | `REQ-CoreLR-D-GCodeParser-0009` (arc words and their validation) |
| 194, 199, 291, 371 | `-0007` | `REQ-CoreLR-F-GCodeInterpreter-0007` (invalid/unsupported line handling) |
| 308, 480 | `-0006` | `REQ-CoreLR-F-GCodeInterpreter-0006` (tolerated-but-ignored words: `F`) |
The rows of the table of section 6.4 are fine as they are (each takes the prefix of the row above in its column), and so are the other relative citations; the documentation shows them in full (`REQ-CoreLR-F-GCodeInterpreter-0003` where the file says `-0003`). The first row of each item (`FW-08`, `P13`, `M-1`...) used to be skipped: its citations are links now too. The file itself is unchanged.
