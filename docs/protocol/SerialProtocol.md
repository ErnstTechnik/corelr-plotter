# CoreLR serial protocol

**Status: APPROVED by the maintainer, 2026-10-08; revised 2026-10-11 to what is built** (revision history at the end of the document).
Increments 1–3 (§7.1) and FW-12…FW-14 are implemented and were run on the machine with Candle 1.1.9 and `tools/bench/serial_probe.py` (bench of 2026-10-10 and 2026-10-12); §7.2 says where each request stands.
Written 2026-10-08 by the host side; changes go through the maintainer. This file is the **contract** between the two sides of the serial link:

- the **host side** (this document's author) writes it;
- the **firmware side** (`firmware/`) reads it and implements it, and reports its measurements
  back through the maintainer. The host side never edits the firmware; whatever the protocol needs from it is listed in
  §7 *Requests to the firmware*.

Every figure says where it comes from. **"Measured"** means measured in this project (the source is named); **"estimate"**
means not measured, with the reasoning given. No estimate is to be read as a measurement — §8 lists what the firmware side
is asked to measure, and what decision each measurement settles.

Specifies the link for `REQ-CoreLR-F-SerialControl-0001`, `-0004`…`-0014` (the functional decisions taken while writing
it and since), and `REQ-CoreLR-F-GCodeInterpreter-0012`…`-0018`.

---

## 1. What the protocol has to live with

| Constraint | Value | Source |
|---|---|---|
| Target | Arduino Nano, ATmega328P 16 MHz, 2048 B RAM, 30 720 B flash, one UART (D0/D1 through the Nano's USB-serial bridge) | `README.md` |
| SD variant | 30 000 B flash (720 free), 1 350 B static RAM **at approval**; **30 152 / 30 720 (568 free)** on 2026-10-11, with the size flags and the settable origin | measured, `Bringup-Status.md` (2026-10-11) |
| Serial variant vs SD | removes ~5.9 KB of SD code; `HardwareSerial` costs **732 B flash, 175 B RAM** | the 5.9 KB is the maintainer's figure from the brief, not re-measured; the 732/175 were **measured** when `HardwareSerial` was unlinked from the SD build (`Bringup-Status.md`, 2026-10-06) |
| Serial variant flash | **estimate at approval: ≈ 5–6 KB free.** Measured: **25 680 / 30 720 (5 040 free)** on 2026-10-09 with the link alone; **27 582 (3 138 free)** on 2026-10-11 with `Z`, `$J=`, the zone, the settable origin and the size flags (the link's own cost is ≈ 2.4 KB, §3) | measured, `REQ-CoreLR-D-SerialLink-0009`, `Bringup-Status.md`. With `G53`, `$H`, `$#`, `WCO`, FW-14 and `M9`/`T` (§7.2): **29 062 / 30 720 (1 658 free)**, RAM 939 B (1 109 left for the stack), built and measured by the host side with `build_size.sh` on 2026-10-12; the image flashed on the machine was 29 062 bytes |
| GCode accepted | the table of §6.4: `G0`–`G3` (`I`/`J` only), `G17` `G21` `G54` `G94`, `G90` `G91`, `G92` `G10 L20 P1`, `G53` (serial), `M2` `M30`, `M3` `M5`, the `Z` pen word (serial); `F` `S` `N` tolerated and ignored; anything else is an invalid line | `REQ-CoreLR-F-GCodeInterpreter-0001`…`-0018` |
| Invalid line | halts the job into the error state; today only the LCD's `SW` acknowledges it | `-0007`, `REQ-CoreLR-D-GCodeParser-0006` |
| Reach and travel zone | a target ≥ 2^21 µm (2 097.152 mm) in X or Y is an invalid line; so is a target outside the **travel zone** (20 mm from each motor's vertical, 50 mm below the line joining the motors; an arc: any point of it); arcs also by grbl's radius rule | `-0011`, `-0016`, `-0009` |
| Line length | **at approval:** the parser copied at most 47 characters and truncated silently beyond. **Now:** the parser reads the line in place, in any length (`REQ-CoreLR-D-GCodeParser-0013`); the link accepts **80 significant characters** and refuses a longer line (§4.3) | `D-GCodeParser-0013`, `D-SerialLink-0002` |
| Parser answers | `OK` (executed: queued in the planner), `BUSY` (not attempted, offer the same line again later), `ERROR` (no cause given) | `GCodeParser.h` |
| When `BUSY` | planner full (**8 blocks** in both variants since 2026-10-13: `MOTION_PLANNER_DEPTH` is a RAM lever, and 8 is the recommended value); for `M3`/`M5`/`Z`/`M2`/`M30`/`G92`/`G10`, until the whole pipeline is drained | `REQ-CoreLR-D-GCodeParser-0007`, `SwConfig.h` |
| Job states | `IDLE`, `RUNNING`, `HALTING`→`PAUSED`, `CANCELLING`→`IDLE`, `ERRORSTOP`→`ERROR`; ramped halt, resume restores the pen | `grbl_corelr.ino` |
| Reset on port open | opening the port toggles DTR, which resets the Nano | Nano hardware |

## 2. Decisions already taken (maintainer, 2026-10-08)

| # | Decision | Consequence here |
|---|---|---|
| D1 | GCode **`+Y` points up the wall**; the firmware flips Y once, in its drawing-frame transform | GCode, status reports, LCD and joystick all share one frame (§7, FW-01) |
| D2 | A **grbl 1.1 subset**, enough for existing free senders to drive the machine | option A below |
| D3 | The **DTR reset is accepted**, as with grbl; the host waits for the welcome line | §4.1 |
| D4 | A serial **job is implicit**: it starts with the first line received while idle; an **uncalibrated** machine refuses motion | §4.10, §4.11 |

The maintainer has also said (2026-10-08) that, as far as possible, the PC side will be an **existing free sender on Linux**
rather than a tool of our own, and that test GCode files will be written when that comes. This makes grbl compatibility
the main requirement on the protocol, not a bonus.

## 3. Options compared

All three keep the same machine behaviour; they differ in what the link looks like.

| | **A — grbl 1.1 subset** | **B — grbl-like, minimal** | **C — own compact protocol** |
|---|---|---|---|
| Replies | `ok`, `error:N`, status `<…>`, `[MSG:]`, `$$` `$G` `$I` `$X` | `ok`, `error:N`, `?` only | single bytes / short codes |
| Flow control | character counting (grbl's) **and** send-and-wait both work | send-and-wait only | own credit scheme |
| Real-time bytes (`?` `!` `~` Ctrl-X) | intercepted **in the receive interrupt**, never queued behind a line | read in `loop()`, in order: stuck behind a `BUSY` line | either |
| UART | own small driver (as `LcdI2c` replaced `Wire`) | Arduino `HardwareSerial` | own driver |
| Free senders (UGS, CNCjs, bCNC, Candle, gSender) | **yes**, within §6's limits | only the simplest "send, wait for ok" scripts | **none** |
| Firmware flash, **estimate** | **1.3–1.8 KB** | 0.9–1.1 KB | 0.6–0.9 KB |
| Firmware RAM, **estimate** | ~280 B (RX 128, TX 64, line 81, state) | ~260 B (`HardwareSerial` 175 + line) | ~200 B |

How the flash estimates are built (none measured; M-2 measures them):

| Item | A | B | C | Reasoning |
|---|---|---|---|---|
| UART | 250–350 | 732 (measured) | 250–350 | an RX ISR with a ring and real-time interception, a TX ring emptied by the UDRE interrupt; `HardwareSerial` also brings `Print`/`Stream` |
| Line assembly (strip spaces/comments, length check) | 150 | 150 | 100 | `SDReader`'s `StripComments` disappears with the SD code |
| `ok` / `error:N` | 60 | 60 | 20 | |
| Status report | 250–300 | 150 | 100 | needs a µm → `123.456` fixed-point formatter in `TextFmt` |
| `$G`, `$I`, `$$`, `$X`, `[MSG:]` strings | 400–700 | — | — | mostly flash strings; `$$` is the variable part (§6.3) |
| Job/state changes, `error:9` lock-out | 150 | 100 | 100 | |

**Recommendation: A.** In the serial variant flash stops being the binding constraint (≈ 5–6 KB free, estimate), so the
extra ~0.5 KB over B buys the whole ecosystem of existing senders — which is what the maintainer now intends to use. Two
points decide against B regardless of cost:

1. **Real-time latency.** With `HardwareSerial`, bytes are only read in `loop()`, in order. While a line is held `BUSY`
   (an `M5` waits for the pipeline to drain: seconds), a `!` sent by the user sits behind it — and if the 64-byte ring is
   full it is dropped. grbl intercepts real-time bytes in the receive interrupt for exactly this reason; so must we.
2. `HardwareSerial` (732 B, measured) is *larger* than the driver we need (250–350 B, estimate): the project already did
   the same trade for the LCD (`REQ-CoreLR-D-LcdI2c`, −2 074 B measured).

C is the cheapest and the cleanest, but no existing program speaks it; it only makes sense with a tool of our own, which
is no longer the plan.

**Outcome (2026-10-11): A was built, and measured.** The link costs about **2.4 KB** of flash (`Uart.c` 596, `SerialLink.c` 1 484, `TextFmt` +300 for the µm formatter), against the **1.3–1.8 KB** estimated for A:
the UART is 596 against 250–350, and the report and the `$` texts are larger. RAM: the rings are 128 + 64 bytes and 8 of state (**200**); static RAM of the whole serial image was 785 B at the time. Source: `REQ-CoreLR-D-SerialLink-0009`
and `REQ-CoreLR-D-Uart-0007`. The decision to take A stands; the estimates above are kept as the record of what was known when it was taken.

---

## 4. Specification (option A)

Notation: `⏎` is the line end the firmware **sends**, `\r\n` as in grbl. Received lines may end with `\n`, `\r` or `\r\n` (§4.3).

### 4.1 Physical link and start-up
- **115 200 baud, 8N1**, no hardware or XON/XOFF flow control (grbl's defaults; every sender's default for grbl).
- Opening the port resets the Nano (D3). The bootloader runs first, then `setup()`; bytes sent meanwhile are lost.
  The host must send nothing until it has received the welcome line, and must treat a welcome line received at any
  other time as **"the machine has restarted"** (every un-acknowledged line is lost; the job is over).
- Welcome line, sent once `setup()` has finished (after `IsrDispatch_Init()`):
  ```
  ⏎Grbl 1.1h ['$' for help]⏎
  ```
  exactly grbl's format, since senders detect grbl by it. The machine's real identity is in `$I` (§6.2). If the machine is
  not calibrated, it is followed by `[MSG:Not calibrated: use the LCD menu]⏎` (§4.11).
- Time from DTR to the welcome line: **to be measured** (M-3). Hosts should wait at least that long plus margin.

### 4.2 Two kinds of incoming bytes
| Bytes | Handling |
|---|---|
| `?` (0x3F), `!` (0x21), `~` (0x7E), Ctrl-X (0x18) | **real-time**: acted upon in the receive interrupt (a flag for `loop()`), never stored, never acknowledged, accepted at any time — even in the middle of a line |
| 0x85 | **jog cancel** (grbl's own byte): a real-time flag, acted upon when the machine is idle and a `$J=` jog is moving (§6.1); ignored otherwise, never stored |
| 0x7F, and 0x80–0xFF except 0x85 | dropped in the interrupt (grbl's override commands; not supported, §9) |
| everything else | line data, stored in the receive ring (128 bytes) |

A `?`, `!` or `~` inside a comment is still a real-time command — grbl behaves the same, and senders strip comments.

### 4.3 Lines
- A line ends at `\n`, at `\r`, or at `\r\n` — **one** end of line: a `\n` received right after a `\r` is dropped.
  Without that rule a `\r\n` sender would get two replies per line (the second for an empty line) and a counting host
  would lose step; grbl itself has this weakness. Candle 1.1.9 ends lines with `\r` alone, UGS and CNCjs with `\n`.
- An empty line (or one empty once stripped) is answered `ok`, as in grbl.
- While a line is assembled, the firmware drops: spaces and tabs, `(…)` and `;…` comments, other control characters.
  Letters may be lower case. **The length limit applies to what remains**: **80 significant characters** (grbl's
  `LINE_BUFFER_SIZE`), see FW-03 for why not 47.
- A line exceeding the limit is consumed to its end and answered `error:11` (and treated as an invalid line, §4.6). It is
  **never truncated** — a truncated coordinate is a different, legal-looking coordinate.

### 4.4 Replies to a line
Exactly **one** reply per received line, in order:

| Parser result | Reply | When |
|---|---|---|
| `OK` | `ok⏎` | as soon as the line is **accepted** (queued in the planner, or the pen action queued) — not when it has been drawn |
| `BUSY` | *nothing yet* | the line is kept and offered again; its reply comes when it is finally accepted or refused |
| `ERROR` | `error:N⏎` (§4.6) | at once |
| refused because of the machine's state | `error:9⏎` | alarm (error state, uncalibrated) or LCD jog, §4.7 |

`BUSY` is therefore never visible on the link: it is simply a reply that has not come yet. This is grbl's behaviour too
(its `ok` waits for a planner slot).

### 4.5 Flow control
Two host strategies work unchanged, both standard in grbl senders:

- **Character counting** (grbl's own, and what every surveyed sender does, §11): the host keeps the sum of the lengths
  (including the `\n`) of the lines sent but not yet answered at or below the **receive-buffer size**. The firmware's ring
  **must be 128 bytes**: UGS (128), bCNC (128) and Candle (127) hard-code it, and CNCjs (120) and gSender (100) can only
  *raise* their figure from the status report's `Bf:`, never lower it — so advertising a smaller buffer in `[OPT:…]` or
  `Bf:` would not protect a smaller ring (survey, §11). The line currently held by the firmware has already left the ring,
  so the host's count is conservative.
- **Send and wait**: one line, wait for its reply, next line. Simplest, slower.

Why counting matters, **estimate** to be checked by M-5: a USB-serial bridge adds a few ms of latency per round trip
(host-side USB scheduling and the bridge's latency timer), so send-and-wait caps the rate at the order of 100 lines/s;
short segments at full speed can need about that, and the planner would starve. With counting, lines wait in
the ring on top of the planner's blocks (4 at approval, **8 now**: `Bf:` reads 8 at rest), and the link itself (115 200 baud ≈ 11 500 bytes/s, ≈ 400 lines/s of 25
characters) is far from the limit.

Neither overflow nor starvation needs anything more: the ring cannot overflow as long as the host counts (it never has
more than 128 bytes in flight), and real-time bytes do not go into the ring.

### 4.6 Error codes
grbl's numbers, so senders show a meaningful text. Requires the parser to say *why* it refused a line (FW-04).

| Cause in CoreLR | `error:N` | grbl's text for N |
|---|---|---|
| a letter with no number after it, a malformed number | `2` | Bad number format |
| a character that is not a word letter (e.g. a line starting with a digit) | `1` | Expected command letter |
| unsupported `G`/`M` value (`G4`, `G20`, `M0`, `G1.5`, …), unknown word (`R`, `Q`, …; `Z`, `T` and `M9` too in the SD variant), `G53` with no `G0`/`G1` on the line or modal, `G10` other than `L20 P1` | `20` | Unsupported command |
| `G92`/`G10` on a line with a motion word, an arc offset, a pen word or a `Z` | `21` | Two G-code commands using axis words |
| coordinates before any `G0`…`G3` has been seen | `26` | No axis words … modal state |
| arc mode without an end point or without `I`/`J` | `26` / `35` | … / arc requires an in-plane offset |
| `I`/`J` on a `G0`/`G1` line; an `M` word and a `Z` word on one line; `L`/`P` without `G10` | `36` | Unused value words |
| target out of reach (`-0011`) **or outside the travel zone** (`-0016`; for an arc, any point of it, but the arc keeps `34`); a `G92`/`G10`/`G53` target that would leave the zone | `33` | Motion command target is invalid |
| arc radii inconsistent (`-0009`) | `34` | Arc radius value is invalid |
| line too long | `11` | Line overflow |
| `$` command not supported | `3` | Invalid statement |
| `$` command not allowed in this state | `8` | Not idle |
| G-code, `$J=`, `$H` while in alarm, uncalibrated, or with an LCD mode that moves the machine | `9` | G-code lock |
| `$J=` or `$H` during a job (`Run`, `Hold`) | `8` | Not idle |

**Outside a job** (`Idle`), an invalid line is answered `error:N` and nothing else happens: no job starts, the state
stays `Idle` (P12, grbl's behaviour). There is no drawing to protect, and senders send lines of their own that CoreLR
cannot accept (Candle's restoration after an abort, §11.1). A counting host may already have sent the next lines; they
are executed as usual — the sender, which pauses on the error, decides (Candle sends `!` at once).

**A limit of "outside a job", found on the machine (2026-10-12).** A line accepted while `Idle` *starts a job* (D4), and that job stays `Run` until its move is done. A refusal that arrives **while it still runs** is therefore "during a job": it halts it and leaves the machine in `Alarm`, and the next lines are `error:9`. Candle's post-Abort restoration did exactly that: the `G53` line (accepted, a move) started the job, the `G92` waited for the move to end, and the third line, `G54 G17 G21 G90 G94 M9 T0`, was refused (`M9`, `T0`): `ok`, `ok`, `error:20`, `error:9` and `Alarm`. **Treatment taken:** `M9` and `T<n>` are accepted and ignored in the serial variant (`REQ-CoreLR-F-GCodeInterpreter-0019`, as grbl does), so that line is `ok`. **Not general:** any other line a sender sends that CoreLR refuses, right behind an accepted move, still stops the job. The alternative that was considered, "an error does not stop a job that only holds restoration lines", was not taken.

**During a job**, after an invalid-line error (every row above except `3`, `8`, `9`) the machine does what it does today
(`-0007`): ramped stop, pen up, error state, reported as **`Alarm`** (§4.7). Lines already in flight are each answered
`error:9`, which keeps a counting host's arithmetic right. The error is acknowledged by **`$X`** or **Ctrl-X** over the link, **or** by
`SW` on the LCD as today (P8); the next line then starts a new job from the machine's current position.

Note the difference from grbl, which reports `error:N` and carries on: we stop, because a skipped line spoils a drawing
(`-0007`). grbl's own alarm state has exactly the needed semantics (G-code locked out until `$X`), so senders already
know how to show it. The senders agree with stopping anyway: on `error:N`, UGS, CNCjs and gSender **pause** the stream,
bCNC **stops** it, Candle sends `!` and asks the user (survey, §11). A user who then presses "resume" in the sender
gets `error:9` for each line until `$X`: noisy, but nothing moves.

**What a sender does after an error from the LCD or during a job (bench, 2026-10-10, Candle 1.1.9).** The machine reports `Alarm`. Candle, which does not know the job is over, stays in its "processing file" state:
*Unlock*, *Open*, the file's *Reset* and *Send* are greyed, and *Pause* and *Abort* do nothing (its Abort waits for `Hold`, and `Alarm` is not a state its abort handles). **The way out is the *Reset* button of the Control panel**
(the icon beside *Unlock*, not the file group's *Reset*): it sends Ctrl-X, which acknowledges the error and brings the welcome line. Disconnecting and reconnecting does the same. Reporting `Hold:0` instead of `Alarm` until the
error is acknowledged would let Abort finish; it was **not** chosen (maintainer, 2026-10-10), because it would change what every other sender sees. (Read in upstream Candle's `frmmain.cpp`; only the Reset button was tried on 1.1.9.)

**`ALARM:N` is never sent.** Entering the alarm state is visible only through the status report. bCNC treats an
`ALARM:` line as the reply to a pending line and would lose count (survey, §11).

### 4.7 States
| CoreLR | Reported | Lines accepted? |
|---|---|---|
| `IDLE`, calibrated | `Idle` | yes (starts a job) |
| `IDLE`, **not calibrated** | `Alarm` | `$` commands only; G-code → `error:9` |
| `RUNNING` | `Run` | yes |
| `HALTING` (ramping down after `!`) | `Hold:1` | received and queued, not executed until resumed |
| `PAUSED` | `Hold:0` | same |
| `CANCELLING` (after Ctrl-X) | `Hold:1` | ring flushed, see §4.8 |
| `ERRORSTOP`, `ERROR` | `Alarm` | `$` commands only; G-code → `error:9` |
| an LCD mode that moves the machine (jogs, calibration wizards), or the machine **idle but still moving** (a `$J=` jog, `$H`, "Return home") | `Jog` | G-code and `$J=`/`$H` from an LCD mode → `error:9`; a chained `$J=` while jogging is accepted |

`Hold:0` / `Hold:1` mean what they mean in grbl: hold complete (ready to resume) / hold in progress. The sub-state is
always sent: Candle reports a bare `Hold` as "Unknown" (survey). CNCjs and gSender have no `Jog` state constant; they will
show it as an unknown state, which is acceptable for a mode entered from the machine's own LCD.

**Risk to check with gSender:** on connection it sends Ctrl-X when the machine reports `Alarm` (survey). After an
invalid line that Ctrl-X acknowledges the error (P8), so the alarm clears; but an *uncalibrated* machine stays `Alarm`
after a Ctrl-X, so gSender might reset in a loop. To be tried before gSender is recommended (P5).

### 4.8 Real-time commands
| Byte | Effect | Same as |
|---|---|---|
| `?` | one status report (§5), at the next `loop()` pass | — |
| `!` | feed hold: ramped stop, then pen up | LCD "Pause" |
| `~` | resume: pen back down if it was, then motion | LCD "Resume" |
| Ctrl-X | cancel: ramped stop, pen up, receive ring and held line discarded, parser modal state reset (`G90`, motion mode `G0`, pen up); in the error state it also **acknowledges the error** (P8); then the **welcome line is sent again**. Bytes received between the Ctrl-X and the welcome line are dropped. **The motion mode after a Ctrl-X is `G0`** (P16, FW-14), as grbl's own after a reset (`gc_init()`); `G90`, pen up as before | LCD "Cancel" (+ `SW` acknowledgment in error) |

**Cancel from the LCD during a serial job ends in `Alarm`, not `Idle`** (decided by the maintainer, 2026-10-11; `REQ-CoreLR-D-SerialLink-0007`): the host does not know the cancel happened and goes on streaming, and in `Idle`
its next line would start drawing the rest of the file from the middle. The lines in flight are answered `error:9`; `$X`, Ctrl-X or `SW` acknowledges. (Candle's way out: §4.6.)

`!` is ignored unless the state is `Run`; `~` is ignored unless it is `Hold:0` (hold complete), as the LCD's own
pause/resume already behave (`grbl_corelr.ino`).

grbl's Ctrl-X is an immediate reset that loses the position (`ALARM:3`). Ours ramps down first, so the position stays
valid and the machine stays calibrated; senders only see a welcome line, which is what they wait for after a Ctrl-X.
Candle sends a Ctrl-X at every connection by default, and gSender when it gets no answer within 500 ms (survey).

### 4.9 Coordinates
**Revised 2026-10-11** (`REQ-CoreLR-F-SerialControl-0013`, `REQ-CoreLR-D-Motion-0030`; committed 2026-10-11; host-verified, then verified on the machine, §7.2): two readings of the position, as in grbl.
- **G-code is written in the drawing frame** (WPos): origin = the *drawing origin* (`REQ-CoreLR-F-Calibration-0006`, `-0010`), **Y up** (D1), millimetres with three decimals (1 µm, the internal unit).
  The drawing origin is the calibration reference point unless the user moved it: from the LCD (*Set origin*, *Origin = calib.*) or by **`G92 X Y`** / **`G10 L20 P1 X Y`** (`REQ-CoreLR-F-GCodeInterpreter-0017`), which make the *current position*
  that point; the values are absolute whatever `G90`/`G91` says.
- **`MPos` is the machine frame**: origin at the calibration reference point, +Y up. **`WCO`** is the work offset, **WPos = MPos − WCO**. `G53` (below) reads its coordinates in the machine frame.
- `WCO` is sent in the status report (§5) and `$#` answers `[G92:x,y,0.000]` with it, so that a sender (Candle) shows the drawing position. **Candle's *Restore origin* does not go to zero** (bench, 2026-10-12): it sends the head's **current** machine and work positions, `G53 G90 G0 X<m> Y<m> Z0` then `G92 X<w> Y<w> Z0`, so it moves nothing and only re-labels where the head already is (its use is after a controller reset); the pen rises because the line has `Z0` under `G53`. *(The earlier capture, `G53 G90 G0 X0 Y0 Z0`, only looked like "go to zero" because the head was at 0,0.)* To put the origin on the calibration point: type `G53 G0 X0 Y0` then `G92 X0 Y0`, or use the LCD's *Origin = calib.*
- **`G53`** (serial variant only): for the one line it is on, X, Y, Z are machine-frame and absolute whatever `G90`/`G91` says; it needs `G0` or `G1` (on the line or modal, else `error:20`); **`Z ≥ 0` lifts the pen, `Z < 0` lowers it**
  (grbl's machine Z is 0 at the top); `G53 G0 Z0` alone is Candle's *Safe position*: the pen rises, nothing moves; the target must lie in the travel zone (`error:33`). *(Before the revision this section said "no work offsets, `WCO` never
  sent": that was true while the origin could not be moved by the user.)*
- grbl reports three axes; we report `Z` as `0.000` (§6.1).
- **`Z0` means two opposite things, by context** (the only place in the dialect where a word's meaning depends on a prefix):

  | line | `Z0` | `Z > 0` | `Z < 0` | rule |
  |---|---|---|---|---|
  | `G1 Z..` (drawing frame) | pen **down** (on the paper) | up | down | `Z ≤ 0` down — a **project convention, still open** (P7); under `G91` the sign of the move decides, `Z0` changes nothing |
  | `G53 G0 Z..` (machine frame) | pen **up** | up | **down** | grbl's machine Z is 0 at the top and negative below: `Z ≥ 0` up, `Z < 0` down |

  So `G0 Z0` lowers the pen and `G53 G0 Z0` lifts it (Candle's *Safe position*). Both are documented in `REQ-CoreLR-F-GCodeInterpreter-0015` and `-0018`.

### 4.10 Jobs (D4)
- The first G-code line accepted while `Idle` starts a job (`RUNNING`, LCD shows `R`).
- The job becomes `Idle` again as soon as the whole pipeline is empty and no line is held — grbl's own Run→Idle rule.
  The existing position keeper then stores the position after its stillness delay (`CALIBRATION_POSITION_STORE_DELAY_MS`,
  2 s), as for any other stop; no new timer is needed.
- Reaching `Idle` does **not** lift the pen (P1, decided): the pen rises on `M5`, `M2`/`M30`, `!`, Ctrl-X, an error or a
  reset. A user typing `M3`, then `G1 X10` by hand, keeps the pen down in between.

### 4.11 Uncalibrated machine (D4)
Reported `Alarm`; G-code is refused with `error:9`; `$X` is refused too (`error:9` preceded by
`[MSG:Not calibrated: use the LCD menu]`), since unlocking would let the machine move from an unknown position.
Calibration is done on the LCD only (calibration from the PC is out of scope).

### 4.12 LCD and joystick during a serial job
The status screen is **the same as for an SD job** (P9): the same `I`/`R`/`P`/`E`/`U` tags, the same position; nothing
added. Pause, resume and cancel stay available from the joystick, and the host sees their effect in the status report.
Moving LCD modes are excluded while a serial job runs (FW-08).

### 4.13 Faults
| Fault | What happens | Who copes |
|---|---|---|
| cable unplugged mid-job | the firmware finishes the lines it has accepted, then goes `Idle` (§4.10). It cannot see the USB disconnect (DTR is not readable on the Nano) | host: on reconnection it sees the reset (D3): the job is lost, restart it from the start (`-0007`'s rule: no resume from a line) |
| board reset (port reopened by another program, brown-out) | welcome line; job lost; position recovered from EEPROM if it was valid (`REQ-CoreLR-F-Calibration-0007`), otherwise `Alarm` uncalibrated | host: any welcome line not asked for ⇒ abort the stream and report |
| byte lost or corrupted | most corruptions give an invalid line ⇒ `error:N`, job stops. A corrupted *digit* gives a wrong but valid coordinate, undetected — the same as grbl. **No checksum in V1**: see P3 | — |
| line never answered | cannot happen while the firmware runs (§4.4); a host time-out means the firmware hung or reset | host: time-out ⇒ `?`; no status ⇒ reconnect |
| ring overflow (host not counting) | bytes dropped; the damaged line ⇒ `error:N` | host bug |

---

## 5. Status report
Answer to `?`, one line:
```
<Run|MPos:12.345,-67.890,0.000|FS:0,0>⏎
```
- State per §4.7.
- `MPos`: the **executed** position (`Motion_GetComputedPosition()`, the step-exact one), **machine frame** (§4.9), Y up. *(At approval: the drawing frame. Revised 2026-10-11 with `WCO`.)*
- `WCO:x,y,0.000` follows `FS:` in the **first** report, in **every tenth**, and **whenever the offset changed** since the last one sent (grbl's rule, so a sender that polls fast learns of a `G92` at once).
  A report with `WCO` is about **90 characters** (the transmit ring is 128 bytes for it, `SwConfig.h`; 7.7 ms at 115 200 baud).
- `FS:0,0`: grbl always sends a feed/speed field; CoreLR's speed does not come from `F` (`-0006`), so it reports 0.
- **Three axes, always.** UGS's position regex needs three values, bCNC raises an `IndexError` on two, Candle mis-parses
  them (survey). `Z` is `0.000`.
- `Bf:<free planner blocks>,<free receive-ring bytes>` — **sent** (P10): a standard grbl 1.1 field; CNCjs and gSender read
  it (to *raise* their buffer figure only), the others display it. On the bench it shows planner starvation and ring
  filling without a debug build. ~30 bytes of flash, estimate.

Example: `<Run|MPos:12.345,-67.890,0.000|Bf:2,87|FS:0,0>⏎` — and, with the offset: `<Idle|MPos:12.345,-67.890,0.000|Bf:8,128|FS:0,0|WCO:30.000,-30.000,0.000>⏎`

**Program end over the link.** In the serial variant, `M2`/`M30` is answered `[MSG:Pgm End]⏎ok⏎`, as grbl 1.1 does:
Candle 1.1.9 relies on that message to tell the end of a program from an ordinary acknowledgment (§11.1).
- With `CORELR_DEBUG_JOBSTATE` (bench builds only) a `|D:…` field is appended; not for use with a sender.
- **No `Ov:` field.** Candle (and bCNC once an override is touched) send override bytes whenever the reported `Ov`
  differs from their slider; absent, nothing to correct. The bytes would be dropped anyway (§4.2).

**Rule for every line the firmware sends** other than a reply: it must not contain the substrings `ok` or `error` —
Candle takes any line containing either as the reply to a pending line (survey). The texts in this document comply.

## 6. `$` commands
`$` lines go through the receive ring like G-code (they are answered `ok`/`error:N` in order).

### 6.1 Supported
| Command | Reply | Notes |
|---|---|---|
| `$G` | `[GC:G1 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]⏎ok⏎` | the motion mode (`G0`…`G3`), distance mode and pen (`M3`/`M5`) are real; the rest are the fixed values that describe this machine (mm, one coordinate system, XY plane) |
| `$I` | `[VER:1.1h.20261011:CoreLR]⏎[OPT:,8,128]⏎ok⏎` | **mandatory**: UGS refuses to connect without a `[VER:` answer, gSender polls `$I` at connection (survey). grbl's format: the **protocol revision date** (this document's, not the build's — it changes only when the protocol does: **20261011** since this revision; printed by the firmware since FW-13), then a free string naming the machine; `OPT` flags empty (UGS reads only the flags), **8** planner blocks (the macro `MOTION_PLANNER_DEPTH`; 4 in an SD build, which has no link), **128** receive bytes |
| `$X` | `[MSG:Caution: Unlocked]⏎ok⏎` | acknowledges the error state (as `SW`); refused when uncalibrated (§4.11) |
| `$$` | the minimal settings list, §6.3, then `ok⏎` | read-only |
| `$` | the help line, listing every supported command, then `ok⏎` | |
| `$#` | `[G54:0.000,0.000,0.000]⏎[G92:<WCO>]⏎[TLO:0.000]⏎ok⏎` | the offsets, in grbl's format; `G92` carries the `WCO` of §4.9 (`D-SerialLink-0012`) |
| `$J=…` | `ok⏎` or `error:N⏎` | **jog from the PC** (increment 3, `REQ-CoreLR-F-SerialControl-0012`). The body goes through the same parser: `G21`, `G90`/`G91`, `X`/`Y`, `F` and `Z` (both ignored); a motion word, an arc offset or a pen word is `error:20`; a target out of reach or zone is `33`; `$J=` alone is `error:3`. Accepted when calibrated and idle **or already jogging** (a continuous jog is a chain of small `$J=`); `error:9` uncalibrated, in alarm, or with an LCD mode that moves the machine; `error:8` during a job. When the planner is full the line is kept and its reply comes with its acceptance. A jog **starts no job** and never changes the modal state. State `Jog` while it moves |
| `$H` | `ok⏎` | **Candle's *Home***: no limit switches here, so it lifts the pen if it is down and moves to the **drawing origin** like the LCD's *Return home* (`REQ-CoreLR-F-SerialControl-0014`; gated like `$J=`; replies `ok` when accepted, not when the move is done, and the state reads `Jog`, not grbl's `Home`) |

### 6.2 Not supported (answered `error:3`)
`$N`, `$C` (check mode), `$SLP`, `$RST`, and any `$n=value` write — CoreLR has no grbl settings to write; its parameters are set on the LCD. (`$J=`, `$#` and `$H` were on this list at approval; they are now supported, §6.1.)

`$G` and lines carrying only tolerated words (Candle sends a bare `S<n>` after every reset) must be accepted at any time,
including mid-stream: Candle polls `$G` about once a second **while streaming**, CNCjs and gSender every 500 ms when idle
(survey).

### 6.3 `$$`
UGS, CNCjs, Candle and gSender send `$$` at connection. The only setting whose absence was traced to an effect is **`$13`**
(UGS: units become "unknown"; what follows from that was not traced). Proposal, every value fixed in flash and true of the
machine:

| Setting | Value | Meaning in grbl | Why |
|---|---|---|---|
| `$10` | `3` | status report shows `MPos` and `Bf:` | describes §5 |
| `$13` | `0` | millimetres | **needed** by UGS |
| `$20` `$21` `$22` | `0` | soft limits, hard limits, homing: off | true: the reach check is a GCode error, not a soft limit; no switches; no homing |
| `$32` | `0` | laser mode off | true |

About 60 bytes of flash (estimate). Rates, accelerations and travels (`$110`…`$132`) are left out: no surveyed sender was
found to need them, and CoreLR's figures are Cartesian limits of a cable machine that grbl's per-axis settings would
misdescribe. To be revisited if a sender complains in the bench tests.

---

### 6.4 Command set per build variant
The serial and SD variants are separate builds (`REQ-CoreLR-F-SerialControl-0003`). GCode that may appear **in a file**
is accepted by both; what only the **link** uses exists only in the serial build, and is compiled out of the SD build
(`REQ-CoreLR-F-GCodeInterpreter-0013`) — the SD variant is the one short of flash (720 bytes free at approval, 568 on 2026-10-11).

✔ accepted · ∅ accepted, no effect · ✘ invalid line (job halts, `-0007`) · — not present in that build

| Command | Effect on CoreLR | Serial | SDCard | Reference |
|---|---|:-:|:-:|---|
| **GCode — motion** | | | | |
| `G0`, `G1` | straight move (same speed for both, `-0006`) | ✔ | ✔ | `F-GCodeInterpreter-0001` |
| `G2`, `G3` with `I`/`J` | arc, clockwise / counter-clockwise, Y up | ✔ | ✔ | `-0003`, D1 |
| `G2`, `G3` with `R` | radius form, deferred (`error:20`) | ✘ | ✘ | `-0003` |
| `G17` | XY plane, the only one | ∅ | ∅ | `-0003` |
| `G18`, `G19` | other planes | ✘ | ✘ | `-0003` |
| `G90`, `G91` | absolute / relative | ✔ | ✔ | `-0001` |
| **GCode — pen and program** | | | | |
| `M3` / `M5` | pen down / pen up | ✔ | ✔ | `-0001` |
| `Z` word | pen: `Z ≤ 0` down, `Z > 0` up under `G90`; under `G91` the sign of the move, `Z0` nothing; waits for the planned moves like `M3`/`M5`; with an `M` word on the line: `error:36`. **The threshold is a project convention, still open (P7)** | ✔ | ✘ (`error:20`) | `-0015`, P7 |
| `M2`, `M30` | **program end**: pen up, end of job, modal state back to its job-start values | ✔ | ✔ | **new**, P4, `-0012` |
| `M0`, `M6` | programmed pause for a pen change, deferred | ✘ | ✘ | `-0008` |
| `M9`, `T<n>` | coolant off, tool select: accepted and ignored (there is no coolant and one pen); `M7`/`M8` stay `error:20` | ∅ | ✘ (`error:20`) | **new**, `-0019`, P17 |
| **GCode — accepted, nothing to do** | | | | |
| `G21` | millimetres, the only unit | ∅ | ∅ | **new**, P4, `-0012` |
| `G54` | the only coordinate system | ∅ | ∅ | **new**, P4, `-0012` |
| `G94` | feed per minute (feed is ignored anyway) | ∅ | ∅ | **new**, P4, `-0012` |
| `F`, `S`, `N` words | ignored | ∅ | ∅ | `-0006` |
| `;…`, `(…)` comments | stripped (SD: when the file is read; serial: while the line is assembled) | ∅ | ∅ | `-0002` |
| **GCode — origin and machine frame** | | | | |
| `G92 X Y`, `G10 L20 P1 X Y` | the current position becomes (x, y): moves the drawing origin; absolute under `G91`; waits for the pipeline to drain; `Z` ignored | ✔ | ✔ | **new**, `-0017` |
| `G53` | one line in the **machine frame** (§4.9); needs `G0`/`G1`; Candle's *Restore origin* and *Safe position* | ✔ | ✘ (`error:20`) | **new**, `-0018`, P14. Without `G0`/`G1`: `error:20` (grbl says 30) — see P16 |
| **GCode — refused** | | | | |
| `G20` (inches), `G4`, `G28`, any other `G`/`M`, words `T`, `A`…; `Z` in the SD variant | | ✘ | ✘ | `-0004`, P7 |
| **Link only — real-time bytes** (§4.2, §4.8) | | | | |
| `?` | status report | ✔ | — | §5 |
| `!`, `~` | pause, resume | ✔ | — | §4.8 |
| Ctrl-X | cancel, welcome line again | ✔ | — | §4.8 |
| 0x80–0xFF | grbl overrides: dropped | ∅ | — | §4.2 |
| 0x85 | jog cancel: ramped stop, the rest of the jog dropped; idle and moving only | ✔ | — | §4.2, P2 |
| **Link only — `$` commands** (§6) | | | | |
| `$`, `$$`, `$G`, `$I`, `$X`, `$#` | help, settings, parser state, identity, unlock, offsets | ✔ | — | §6.1 |
| `$J=…` | jog from the PC | ✔ | — | §6.1, P2, P6 |
| `$H` | go to the drawing origin | ✔ | — | §6.1 |
| `$N`, `$C`, `$SLP`, `$RST`, `$n=value` | | `error:3` | — | §6.2 |

In the SD variant a line starting with `$` is simply not a GCode word: it is an invalid line like any other.

## 7. Requests to the firmware
Numbered so that the firmware side can answer item by item. None is implemented by the host side.

| # | Request | Why |
|---|---|---|
| **FW-01** | **Flip Y** once, in the drawing-frame transform (`REQ-CoreLR-F-Calibration-0006`): GCode `+Y` = up the wall. Everything in the drawing frame follows: GCode targets, `J`, status position, LCD `X,Y` display, and the joystick (stick up = pen up the wall). **Arcs**: a mirror reverses rotation — if arcs are generated in the internal (Y-down) frame, `G2`/`G3` must swap and `J` change sign; generating in the drawing frame and transforming each point avoids that. Acceptance test: `G2` (clockwise) from (0,0) to (20,0) with `I10` passes through drawing `+Y`, i.e. **above** the start on the wall, and `G3` passes below it. *(Corrected 2026-10-08, firmware side: this said `G3`. Run through the real parser, planner and generator, `G3 X20 Y0 I10` from (0,0) goes from Y = −9.999 mm to 0 — counter-clockwise from the left of the centre goes under it.)* Applies to the SD variant too. | D1, `REQ-CoreLR-F-GCodeInterpreter-0014` |
| **FW-02** | **Own UART driver**, not `HardwareSerial`: RX interrupt with a **128-byte ring**, real-time bytes (§4.2) intercepted in the interrupt as flags, 0x80–0xFF dropped; TX through a ring emptied by the UDRE interrupt so that `loop()` never blocks on sending (a 40-byte report is 3.5 ms at 115 200 baud). Size of the TX ring: the firmware's call (64 suggested). | §3, point 1 and 2 |
| **FW-03** | **Line assembly** from the ring: strip spaces, comments and control characters on the fly; **80** significant characters (a macro of its own, no longer `SDREADER_LINE_SIZE`; the parser's `upperLine` copy sized by it); overflow ⇒ consume to end of line, `error:11`. 47 is too short once a generator writes 6 decimals: `G2X123.456789Y234.567891I-12.345678J5.432109F1000` is 49 characters *after* stripping. | §4.3 |
| **FW-04** | **The parser says why**: `GCodeParser_ExecuteLine()` returns, or leaves readable, the grbl status number of §4.6. Returning the number itself costs about nothing (a different constant at each `return`); the SD variant keeps ignoring it. | §4.6 |
| **FW-05** | **Serial job source** in `grbl_corelr.ino`: implicit start and Run→Idle (§4.10); an invalid line while `Idle` answered `error:N` without entering the error state (P12); `error:9` lock-out in alarm/LCD jog; `$X` as `ERROR_ACK`; `!`/`~`/Ctrl-X as `JOB_HALT`/`JOB_RESUME`/`JOB_CANCEL` plus the flush and the welcome line; one reply per line (§4.4). | §4 |
| **FW-06** | **Status report** builder (§5) with a µm → `-123.456` formatter added to `TextFmt` (not `snprintf`: 1 372 B, `TextFmt.h`). | §5 |
| **FW-07** | **`$` commands** of §6.1, strings in flash. Needs read access to the parser's modal state for `$G` (it is file-local today). | §6 |
| **FW-08** | **LCD in the serial variant**: the "Load SD file" item removed (`#if CORELR_JOB_SOURCE`, already noted in `REQ-CoreLR-F-SerialControl`); moving LCD modes (jogs, calibration wizards) not enterable while a serial job is `Run`/`Hold`, and reported `Jog` while active; pause/resume/cancel from the LCD stay available and are visible to the host through the status. | §4.7 |
| **FW-09** | **Welcome line** at the end of `setup()` and after a cancel by Ctrl-X; the "not calibrated" message after it when relevant. | §4.1 |
| **FW-10** | **GCode scope, both variants**: accept `G21`, `G54`, `G94` as no-ops and `M2`/`M30` as program end (pen up after the pipeline has drained, as `M5`; end of job; modal state reset to `G90`, no motion mode, pen up). In the serial variant the next line starts a new job; in the SD variant the rest of the file is not executed. `G20` stays invalid. | P4, `REQ-CoreLR-F-GCodeInterpreter-0012` |
| **FW-11** | **Variant-specific code compiled out**: real-time bytes, `$` commands, status report, line assembly, the UART driver and later `$J=` exist only under `CORELR_JOB_SOURCE_SERIAL`; the SD build must not grow by any of them. Conversely the GCode of §6.4 is shared code, built in both. Check: the SD build's size before/after FW-01…FW-11 differs only by FW-01 (Y flip), FW-04 (error causes) and FW-10. | P4, P6, `REQ-CoreLR-F-GCodeInterpreter-0013` |

| **FW-12** | **`G53`, the machine frame and `$H`** (asked by the maintainer, 2026-10-11, after the host side found that Candle's post-abort restoration — `G53` then `G92` — left the origin shifted by the stop distance once `G92` was accepted: `G53` was refused, `G92` was not): `G53` as in §4.9; `MPos` in the machine frame with `WCO` (§5); `$#`; `$H`. | §4.6 risk, P13 |
| **FW-13** | `$I`'s date constant: `20261011`. | §6.1 |

| **FW-14** | **After a Ctrl-X the parser's motion mode is `G0`** (P16 (a), maintainer 2026-10-12), grbl's own state after a reset. Scope: **Ctrl-X over the link only** — an SD job's start, `M2`/`M30` and the SD variant keep "no motion mode" (a bare `X10 Y10` is still `error:26` there). Why: Candle's Abort sends `G21G53G90X..Y..Z..` with no `G0` (captured, §11.1) and `G53` needs one; with a mode set it moves the head back to where Candle last saw it, and the `G92` that follows is then exact. Effects to test: that line is accepted after a Ctrl-X and moves the head back; `$G` after a Ctrl-X reads `G0`; a bare coordinate line after a Ctrl-X is accepted. | §4.8, P16 |

### 7.1 Increments (P11)
| Increment | Contents | Variant | Can be checked |
|---|---|---|---|
| **1** | FW-01 (Y flip), FW-04 (error causes), FW-10 (`G21` `G54` `G94` `M2` `M30`) — shared code | SD (and serial later) | on the host (`tests/host`), then on the bench with an SD file |
| **2** | the serial link: FW-02, FW-03, FW-05…FW-09, FW-11 | serial | host tests of the line assembly and state machine; bench with a free sender (P5); M-1…M-6 |
| **3** | `$J=` jogging and 0x85 (P2, P6) | serial | bench with UGS or Candle |

**All three are done** (2026-10-11): 

Increment 1 is small, useful to the SD variant at once, and keeps a regression in the shared code apart from the new link
code. FW-11's size check (§7) is made at the end of increment 2.

### 7.2 Where each request stands (2026-10-11)
| # | Realized by | State |
|---|---|---|
| FW-01 | `REQ-CoreLR-D-Motion-0025` (`CORELR_Y_UP`) | done; bench: Y up confirmed with the maintainer's jog; the arc direction (§7 acceptance test) is `tests/bench/gcode_serial/21_Accept_Arcs.gc` |
| FW-02 | `REQ-CoreLR-D-Uart` | done; RX ring 128, TX ring 64 (**128 now, for the longer report with `WCO`**) |
| FW-03 | `REQ-CoreLR-D-SerialLink-0002`, `D-GCodeParser-0013` | done: 80 significant characters, in place, `error:11` |
| FW-04 | `REQ-CoreLR-D-GCodeParser-0011` | done: the result is grbl's number |
| FW-05…FW-09 | `REQ-CoreLR-D-SerialLink-0003`…`-0008` | done; bench with Candle and the probe |
| FW-10 | `REQ-CoreLR-D-GCodeParser-0011`/`-0012` | done |
| FW-11 | `REQ-CoreLR-D-SerialLink-0008` | done: the SD image is the same with or without the link |
| FW-12 | `REQ-CoreLR-F-GCodeInterpreter-0018`, `F-SerialControl-0013`/`-0014`, `D-Motion-0030`, `D-GCodeParser-0018`, `D-SerialLink-0012` | **done** (2026-10-11): 29 mutants caught; **+1 470 B of flash, +64 B of RAM** (transmit ring 128); **verified on the machine with Candle 1.1.9 (maintainer, 2026-10-12): `$#`, Zero XY, jog, Safe position, Home, the typed `G53`/`G92` pair, Abort** (`Bringup-Status.md`) |
| FW-14 | `GCodeParser_ResetByLink()`, `REQ-CoreLR-F-SerialControl-0015` | **done**, 6 mutants caught; on the machine: the head is exactly on the position Candle had read after an Abort (`serial_probe.py abort`: 0.000 mm, three runs) and, with Candle, positions identical before and after a Cancel |
| FW-13 | `SerialLink.c` | **done**: `$I` prints `20261011`, seen on the machine |
| M9, `T<n>` (not a request: found on the bench) | `REQ-CoreLR-F-GCodeInterpreter-0019` | **done**, serial variant only; 10 mutants caught; Candle's Abort now ends `Idle` |
| P2/P6 `$J=`, 0x85 | `REQ-CoreLR-D-SerialLink-0011`, `D-GCodeParser-0015`, `D-Uart-0009` | done; bench 2026-10-10 with the probe: 20 checks OK; Candle's own jog buttons not yet tried |
| P7 `Z` word | `REQ-CoreLR-F-GCodeInterpreter-0015`, `D-GCodeParser-0014` | done in the serial variant; **threshold open** |

## 8. Measurements requested from the firmware side
Each settles something; the threshold says what changes if it comes out differently. **Results as of 2026-10-11** in the last column; "host" is the host side's own reading of a log, "fw" the firmware side's figure.

| # | Measure | Decides | Result |
|---|---|---|---|
| M-1 | flash, static RAM, worst-case stack of the serial variant | whether the ≈ 5–6 KB estimate holds | **fw, 2026-10-09:** flash 25 680 / 30 720 (5 040 free), RAM 785 B (1 263 free), worst stack 300 B (+13 in an interrupt), margin ≈ 950 B (`D-SerialLink-0009`). **fw, 2026-10-11:** flash **27 582** (3 138 free) with `Z`, `$J=`, the zone, the origin and the size flags (`Bringup-Status.md`); RAM and stack not re-measured. **fw; measured by the host side with `build_size.sh`, 2026-10-12:** serial **29 052 / 30 720 (1 668 free)**, static RAM **939 B** (1 109 left for the stack; stack depth not re-measured); SD unchanged at 30 152 (568 free). The 1.5 KB fallback threshold of the original M-1 is 168 bytes away. **2026-10-12, with FW-14 and `M9`/`T`: 29 062 / 30 720 (1 658 free), RAM 939 B** (host side, `build_size.sh`; the same image was flashed) |
| M-2 | the flash of each FW item separately | §3's table | **fw:** `Uart.c` 596, `SerialLink.c` 1 484, `TextFmt` +300 (≈ 2.4 KB against 1.3–1.8 estimated); the sketch's serial glue is smaller than the SD line hand-off it replaces (766 against 860); `Z` + `$J=` + 0x85 + jog status: +810 (`D-SerialLink-0011`) |
| M-3 | time from DTR to the welcome line | the host's start-up wait | **host:** **1.680–1.681 s** (72 port openings, host clock, the probe's log `docs/bringup/ring70-2026-10-11.log`; calibrated machine, Nano's bootloader included). Wait at least 2 s |
| M-4 | latency of `!` and of `?` with the machine moving | that §4.2 does what it is for | **host, `?`:** round trip **6–9 ms** (508 reports while a `$J=` jog moved; one outlier of 54 ms in 1 587 over the whole log), host clock, USB bridge included. **`!`: not measured** |
| M-5 | sustained lines/s with 0.2 mm segments at full speed: does `Stepper` starve? | whether 128 bytes and the planner are enough | **partial, fw (probe `stream`, 2026-10-10):** the test card, 153 short-segment lines, every line acknowledged, 12 111 mm in 588.3 s = **20.6 mm/s** (ceiling 21.5); circle R 100 at 20.7; square at 20.9. **A file of 0.2 mm segments was not run**; the `miss:` counter was not read |
| M-6 | longest `loop()` pass with serial traffic | that sending never blocks | **fw, simulator (CPU part):** one `SerialLink_Task()` pass 0.44 ms for a 15-character `G1`, 0.73 ms for an arc, 0.46 ms for a report, 1.08 ms for an 80-digit line; the rest of the pass as before. Measured on the board: not done |
| M-7 | jitter of the step edges under serial traffic | the margin of `Stepper` against the UART interrupt | **fw, simulator:** receive ISR 71–79 cycles (+3 for the vector), transmit 60–64; longest 82 cycles = **5.1 µs**; a stream of data bytes ≈ 5.4 % of the CPU, a full-duplex link ≤ 10 % (`D-Uart-0007`). **Jitter on a scope: not done** |

**Still to measure on the bench:** M-4 for `!` (a logic analyser, or the host timing `!` to `Hold:1`); M-5 with 0.2 mm segments (`CORELR_DEBUG_MISS_COUNT=1`) — the generator for such a file is not written; the M-6 and M-7 figures on the board; the worst-case stack of the build with FW-12; FW-12 itself on the machine (`Bringup-Status.md`).

## 9. Not covered (deliberately)
- `F` and the overrides (0x80–0xFF, except 0x85): CoreLR's speed is its own (`-0006`).
- real homing (`$H` only goes to the drawing origin), `$C` check mode, `G4`/`G20`/`G28`, `M0`/`M6`, the `R` arc form: each follows `REQ-CoreLR-F-GCodeInterpreter`'s scope, not the protocol's.
- Calibration from the PC; SVG/image → GCode conversion.
- Line checksums (P3).

## 10. Points discussed
Decided by the maintainer, 2026-10-08:

| # | Question | Decision |
|---|---|---|
| P1 | Does an implicit job end lift the pen? | **No.** The pen rises on `M5`, `M2`/`M30`, `!`, Ctrl-X, error, reset (§4.10) |
| P2 | `$J=` jogging from the PC | **Not in the first increment**; a later one, serial only (§6.4) |
| P3 | Line checksums | **None in V1** |
| P4 | `G21`, `G54`, `G94`, `M2`, `M30` | **Accepted**, in **both** variants, since they are found in GCode files, not only on the link (§6.4, `REQ-CoreLR-F-GCodeInterpreter-0012`). Conversely, whatever only the link uses is compiled out of the SD variant (`-0013`) |
| P8 | Does Ctrl-X acknowledge the error state? | **Yes**, like `$X` and `SW` (§4.6, §4.8) |
| P9 | LCD during a serial job | **Same screen as an SD job**, nothing added (§4.12) |
| P10 | `Bf:` in the status report | **Yes** (§5), `$10=3` |
| P12 | An invalid line outside a job (`Idle`) | **`error:N` only**, the machine stays `Idle`; during a job, unchanged: stop and `Alarm` (§4.6). Raised by Candle 1.1.9's restoration lines after an abort (§11.1) |
| P11 | Increments | **1)** Y flip + error causes + P4 codes, shared code; **2)** the serial link; **3)** `$J=` (§7.1) |
| P6 | What `$J=` must accept | `G21`, `G90`/`G91`, `X`/`Y`, `F` (ignored), a `Z` word (ignored, Candle always sends one) — serial only, with P2's increment. **Done** (increment 3) |
| P13 | Cancel from the LCD during a serial job | **Ends in `Alarm`** (option A, maintainer, 2026-10-11; `D-SerialLink-0007`); Candle's way out is its Control-panel *Reset* (§4.6) |
| P14 | The machine frame, `G53`, `$H` | **Decided** (maintainer, 2026-10-12): `G53` in the serial variant, `MPos` in the machine frame with `WCO` in the report, `$#`, `$H` = go to the drawing origin (`F-SerialControl-0013`/`-0014`, `F-GCodeInterpreter-0018`, built). A protocol change: **MAJOR at release** (CHANGELOG). **Verified on the machine with Candle 1.1.9, 2026-10-12** |
| P16 | The `G53` Candle sends after an Abort has no motion word | **(a)** (maintainer, 2026-10-12): after a Ctrl-X the motion mode is `G0`, as grbl's after a reset (FW-14). What Candle sends, **captured** on the maintainer's Candle 1.1.9 (§11.1): `G21G53G90X..Y..Z..` (the head goes back to the position Candle last read), `G21G92X..Y..Z..` (the position is re-labelled as it was), then `G54 G17 G21 G90 G94 M9 T0` (`error:20`, harmless). Before the decision the first failed (`error:20`, no mode) and the second succeeded, shifting the drawing origin by the stop distance (≲ 5 mm, **estimate**: the ramp is 4.2 mm). Options (b) `G53` alone is a move, (c) leave it, were not taken. `M2`/`M30` are unchanged (grbl sets `G1` there; ours stays "none") |
| P17 | `M9`, `T<n>` | **Accepted and ignored in the serial variant** (found on the bench, 2026-10-12, as a consequence of P12's limit, §4.6; `REQ-CoreLR-F-GCodeInterpreter-0019`); the SD variant keeps them unsupported (no flash to spare). The general limit (a refused line right behind an accepted move still halts the job) **remains** |
| P15 | `$I`'s date | **`20261011`** (maintainer, 2026-10-11), FW-13 |

Still open:
- **P5 — Which free sender to bench first: decided, Candle** (maintainer, 2026-10-08), the Debian package
  `candle 1.1.9-5` installed on the maintainer's machine. The survey of §11 read Candle's *current* master, not 1.1.9: its
  behaviour is being re-checked against the 1.1.9 sources (§11.1).
- **P7 — Pen lifted by `Z`: half decided.** *Decided and built (2026-10-10, serial variant, `REQ-CoreLR-F-GCodeInterpreter-0015`):* the `Z` word lifts or lowers the pen (otherwise such files stop at their first pen lift). *Still open
  (maintainer, 2026-10-11):* **the threshold** (`Z ≤ 0` = pen on the paper is this project's convention, not yet checked against a real generator's output), and whether the SD variant ever gets it (it would not link: +156 bytes).

## 11. Sender survey
Read in the sources at their default branch, 2026-10-08, by a subagent: UGS `a47d34ac7e`, CNCjs
`747ffcd5f2`, bCNC `8bcaac0f0f`, Candle `c04e6f2565`, gSender `4b4292b822` (v1.6.4), grbl `gnea/grbl` `bfb67f0c79`. File and
line references are in Appendix A; entries marked *(analysis)* were inferred from code paths rather than read as a single
statement.

| | UGS | CNCjs | bCNC | Candle | gSender |
|---|---|---|---|---|---|
| Licence, stack | GPL-3, Java | MIT, Node.js | GPL-2, Python | GPL-3, Qt/C++ | GPL-3, Electron |
| Streaming | character counting (single-step mode optional) | counting | counting | counting | counting |
| Buffer assumed | 128, hard-coded | 120; raised from `Bf`, never lowered | 128, hard-coded | 127, hard-coded | 100; raised from `Bf`, never lowered |
| At connection | waits 2 s; `?` (10 × 1 s), `$I`, `$$`, `$G`; Ctrl-X only if unresponsive | nothing; `?` every 2 s without welcome; then `$$` | DTR toggle, 2 s, `\n\n` | Ctrl-X after 1 s (default), then `$$`, `$#` | `$I` every 800 ms (×7); Ctrl-X after 500 ms if silent, or if `Alarm`; `$$` |
| Detects grbl by | welcome starting `Grbl `, or `[VER:`; **no `$I` answer ⇒ connection fails** | welcome regex or any `<…>` report | line starting `Grbl` | `^GRBL` | any line containing `grbl` |
| Two-axis `MPos` | **fails** | fine | **fails** (`IndexError`) | **mis-parses** *(analysis)* | fine |
| States | Idle Run Hold Door Home Alarm Check Sleep Jog Tool | no `Jog` | all, `Hold:0/1` | bare `Hold`/`Door` ⇒ Unknown | as CNCjs |
| On `error:N` | pauses | pauses (`ignoreErrors` option) | stops | `!`, then asks | pauses |
| On `ALARM:N` | state Alarm, refuses to stream | logged | stops, **and counts it as a reply** | nothing special | event |
| `?` period | 200 ms | ≤ 250 ms | 125 ms | 40 ms | ≤ 250 ms |
| `$G` | at connection | ≤ 500 ms, idle | after Idle; 10 s when not running | ~1 s, **also while streaming** | as CNCjs |
| Bytes ≥ 0x80 on its own | no | no | overrides re-sent while `Ov` ≠ target *(analysis)* | overrides while `Ov` ≠ target | no |
| Jogging | `$J=G91…` | plain `G91`/`G0`/`G90` lines | `$J=G91 … F100000` | `$J=G21G91X…Y…Z…F…` | `$J=G21G91 …` |

What it changed in this proposal: the 128-byte ring is mandatory (§4.5); three axes in `MPos` and no `Ov` (§5);
`Hold:n` always with its sub-state (§4.7); `$I` mandatory (§6.1); no `ALARM:N` (§4.6); no `ok`/`error` inside other lines
(§5); `$G` and `S` accepted mid-stream (§6.1); the `$$` list (§6.3); P5, P6.

### 11.1 Candle 1.1.9 (the sender chosen for the bench, P5)
The maintainer's Candle is the Debian package `candle 1.1.9-5`. Upstream has no `v1.1.9` tag, so the Debian sources were read
(`candle_1.1.9.orig.tar.gz` + `candle_1.1.9-5.debian.tar.xz`, the 6 patches of `debian/patches/series` applied), by a
subagent, 2026-10-08. Line numbers are in the patched `src/frmmain.cpp` unless stated.

| | Candle 1.1.9 | Lines | Consequence for CoreLR |
|---|---|---|---|
| Streaming | character counting, 127 bytes; each line **upper-cased**, ended by **`\r` alone** | `frmmain.h:206`; 752–799; 2028–2040 | fits §4.3/§4.5 (`\r` is an end of line) |
| Comments | sent **unstripped** to the machine | 1736, 752 | the firmware strips them (§4.3) |
| Connection | Ctrl-X **as soon as the port is open** (no setting); every line dropped until one matches `^GRBL…`; no version check; **no `$$`, `$#` or `$I`** | 281, 738–746, 802–834, 2743–2745 | the DTR reset and the Ctrl-X overlap: the welcome line comes once the cancel of §4.8 is done |
| After a reset, `$H` or `$X` | sends `S<slider>` once (`S100` by default) | 1129–1144, 816, 2378 | `S` alone: accepted, no effect; `error:9` while in alarm (no dialog, see below) |
| `$G` | every second, **also while streaming** (not while paused or with its queue full) | 1392–1406 | accepted at any time (§6.1) |
| `?` | every 40 ms by default (Debian patch 0002), only once the previous report has arrived | 1408–1414 | M-4/M-6 must be measured at this rate |
| Status parsing | 1.1 format; **`MPos` with three values**; `WPos` never parsed (computed from `WCO`, else = `MPos`); states `Idle Alarm Run Home Hold:0 Hold:1 Queue Check Door Jog`; `Bf:` not parsed; `FS:` shown | 42–52, 869–882, 985–996, 1041–1097 | as §5; `Bf:` is harmless |
| `Ov:` | if present, Candle sends override bytes until it reads 100 | 1041–1062 | `Ov:` never sent (§5) |
| Reply detection | a line **containing** `ok` or `error` (case-sensitive) ends the pending command | 2706–2741 | §5's rule on unsolicited lines |
| Program end | after `M2`/`M30` acknowledged **without** `[Pgm End]`, its command buffer is cleared | 1102–1108, 1173–1177 | `[MSG:Pgm End]` sent (§5) |
| `error:N` on a **program** line | sends `!`, dialog; *Ignore* sends `~`; otherwise abort | 1273–1293 | as §4.6 |
| `error:N` on any **other** command | completes the command; no dialog, nothing stops | 1147–1162 | housekeeping errors are harmless |
| Abort (dialog or Stop) | **Captured 2026-10-12** (`tools/bench/candle_spy.py`, the maintainer's Candle 1.1.9, after the whole file had been acknowledged): `!` at t, Ctrl-X 77 ms later (it did not wait: the spy reported `Hold:0` at once), then, 47 ms after the welcome line, **three lines in this order**: `G21G53G90X0Y0Z0`, `G21G92X0Y0Z0`, `G54 G17 G21 G90 G94 M9 T0` (the values are Candle's last MPos / WPos), then `S0` | `restoreOffsets()` (the first two), `restoreParserState()` (the third) | The first two are a pair (go back, then re-label); **`G53` has no `G0`**, so it needs the motion mode of FW-14 (`G0` after a Ctrl-X). The third is `error:20` (`M9`, `T0`), harmless outside a job (P12). **Captured twice**: after the file was done (0 lines waiting), and **in the middle of a job** (`--ok-delay 1`: `!` with 10 lines still waiting for their `ok`, Ctrl-X 85 ms later, the same three lines 45 ms after the welcome, in the same order): the sequence does not depend on the job's state. Only 10 lines were in flight: Candle's 127-byte character counting at work. **Bench, 2026-10-12 (maintainer, Candle 1.1.9 on the real machine, then `serial_probe.py abort`):** with FW-14 the first two lines are `ok`, and the head is exactly on the position Candle had read (probe: difference 0.000 mm, three runs; Candle: positions identical before and after a Cancel). Before `M9`/`T` were accepted the third line was `error:20` and, being behind an accepted move, halted the implicit job: `Alarm`, `error:9` for the next line (P12's limit, §4.6); now `ok`, the machine ends `Idle`. |
| Spindle buttons | `M3 S<n>` / `M5` | 2374–2468 | pen down / pen up from Candle's buttons |
| Jogging | `$J=G21G91X…Y…Z…F…`, `Z` always present, 4 significant digits; continuous jog = chained small `$J=`; releasing sends nothing; Stop sends 0x85 | 3879–3910 | increment 3 (P6) |
| Probing, height map | off by default; when on, arcs are rewritten as segments with `Z` | 3165–3195, 3517–3700 | not to be used |

## Appendix A — Survey references (file:line at the commits of §11)
- **grbl**: `grbl/serial.h:26-27` (`RX_BUFFER_SIZE 128`); `report.c:446` (`[OPT:]` prints it); `doc/markdown/interface.md:419`
  (planner blocks, RX bytes); `report.c:533-536` (`Bf:` = free blocks, free RX bytes).
- **UGS** (`ugs-core/src/com/willwinder/universalgcodesender/`): `GrblUtils.java:62` (buffer 128), `:105` (`startsWith("Grbl ")`),
  `:586-599` (states), `:604` (3-group `MPos` regex); `communicator/GrblCommunicator.java:51` (counting);
  `firmware/grbl/GrblBuildOptions.java:41` (`[OPT:` flags only); `GrblControllerInitializer.java` (2 s wait, version, `$$`, `$G`;
  "Could not detect the GRBL version"); `utils/ControllerUtils.java:48` (2 s per-command time-out);
  `communicator/BufferedCommunicator.java:269-276` (pause on error); `utils/Settings.java:87` (200 ms poll);
  `GrblController.java:499` (`$J=`), `:304`, `:331` (0x85); `firmware/grbl/GrblFirmwareSettings.java:422-434` (`$13`).
- **CNCjs** (`src/server/controllers/Grbl/`): `GrblController.js:368` (120), `:588-590` (grow from `Bf`), `:542-557` (report
  counts as ready), `:640` (`ignoreErrors`, pause), `:685-696` (alarm logged), `:887` (2 s `?`), `:940` (`$G`), `:1013-1017`
  (`$$`), `:1155` (250 ms); `GrblLineParserResultStartup.js:3` (welcome regex); `src/server/lib/Sender.js:174`;
  `src/app/widgets/Axes/index.jsx:197-199` (jog lines).
- **bCNC** (`bCNC/`): `Sender.py:40` (128), `:851` (counting), `:498-515` (DTR, `\n\n`); `controllers/_GenericController.py:261-273`
  (`error:`/`ALARM:` stop and pop), `:289` (`Grbl`); `controllers/GRBL1.py:36` (`$J=`), `:39-60` (overrides), `:112-114`
  (`float(word[3])`).
- **Candle** (`src/candle/`): `frmmain.h:293` (127); `frmmain.cpp:96-110` (states), `:1933` (`MPos` regex), `:2219-2240` and
  `:5426-5437` (overrides from `Ov`), `:2501-2523` (error dialog), `:2663-2687` (connection), `:2718-2733` (`$G`, `S` on a
  1 s timer), `:3727-3728` (40 ms, reset on connection), `:4512`, `:4561`, `:4624`, `:5817-5826` (reply = contains `ok`/`error`),
  `:5831` (`^GRBL`), `:5957`, `:5965` (`$J=`).
- **gSender** (`src/server/`): `controllers/Grbl/GrblController.js:467` (100), `:727-733` (`$$`), `:755-762` (`$I`, Ctrl-X in
  alarm), `:784-786` (grow), `:880-910` (pause on error), `:1443-1451` (Ctrl-X after 500 ms), `:2205`, `:2212`, `:2215` (jog);
  `lib/Connection.js:18-19`, `:53` (detection).

### Revision history
| Date | What |
|---|---|
| 2026-10-08 | approved (D1–D4, P1–P12) |
| 2026-10-11 | brought to what is built: increments 1–3 done; `Z` (P7 half decided), `$J=`, 0x85, planner 8, `G92`/`G10`, travel zone (`error:33`), `error:21`, the 80-character line, LCD cancel → `Alarm` (P13), Candle's Abort/Reset finding, the measured figures of §8, `G53`/`WCO`/`$#`/`$H` (FW-12, P14), `$I` date (P15) |
| 2026-10-12 | P14 decided; FW-12 committed with its measured cost (29 052 B, 1 668 free, RAM 939 B); the two meanings of `Z0` made explicit (§4.9); P16 detailed with the three lines Candle sends and grbl's own modal defaults (read in `grbl/gcode.c`) |
| 2026-10-12 (later) | P16 decided (a), FW-14 requested; Candle's Abort captured twice on 1.1.9 (idle and mid-job): the order of the three lines is G53, G92, then the parser state |
| 2026-10-12 (bench) | FW-12, FW-13, FW-14 done and verified on the machine with Candle 1.1.9; `M9`/`T<n>` accepted (P17) after the bench showed an Abort leaving `Alarm`; the limit of "outside a job" written down (§4.6); Candle's *Restore origin* re-labels, it does not go to zero (§4.9); flash 29 062 / 1 658 free |
| 2026-10-29 | the planner depth of §1 corrected (8 in both variants, a RAM lever: the maintainer's amendment of `REQ-CoreLR-D-LcdJoystickUI-0028`); anchors for the links of the API documentation are made by its input filter, the file is unchanged otherwise |
