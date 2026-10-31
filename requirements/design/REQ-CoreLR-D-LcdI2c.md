# Design Requirements — LcdI2c

Module: **LcdI2c** — a character LCD (HD44780) behind a PCF8574 I²C backpack: `LcdI2c_Init()`, `LcdI2c_SetCursor()`,
`LcdI2c_PutChar()`, over a polled TWI master. Plain C, no interrupt, no buffer, no object. It replaces the Arduino
`Wire` library and `LiquidCrystal_PCF8574`, which `LcdJoystickUI` used until 2026-10-07 — one of the project's
"own small libraries" (a general-purpose utility gets its own `.h`/`.c` pair).

Realizes: [`REQ-CoreLR-F-UserInterface-0001`](../functional/REQ-CoreLR-F-UserInterface.md) (the display) and, by
what it frees, [`REQ-CoreLR-F-Actuation-0001`](../functional/REQ-CoreLR-F-Actuation.md) (the step timer's
latency) and the flash budget that `REQ-CoreLR-F-GCodeInterpreter-0003` (arcs) needs.

Origin: maintainer, 2026-10-07 ("go on with the LCD optimisation", translated), the replacement having been on record since
the optimisation pass of 2026-10-06 (`REQ-CoreLR-D-Motion.md`, "Still to take").
Status: Implemented (2026-10-07), verified on the host; **not yet on the bench**.

## REQ-CoreLR-D-LcdI2c-0001 — Our own driver, not `Wire` and `LiquidCrystal_PCF8574`
<a id="REQ-CoreLR-D-LcdI2c-0001"></a>
The display shall be driven by this module, in C, and the two libraries shall not be linked.

**Why, measured.** Whole-image builds (Arduino Nano, `arduino-builder`, 2026-10-07), the arc build before and after
the replacement:

| | before | after | |
|---|---|---|---|
| flash | 32 932 | **30 858** | **−2 074** |
| static RAM | 1 619 | **1 350** | **−269** (17 of them a buffer `LcdJoystickUI` no longer needs) |

What went: Wire's interrupt-driven TWI state machine and its vector (574), `endTransmission` (462), the rest of
`TwoWire` and `twi_*` (~700), `micros()` (74, which Wire's timeouts used), the library and a static constructor
(~500), and 160 bytes of buffers for a master that sends five bytes at a time. This module's own three functions
are **216 bytes**. The 574-byte TWI interrupt was also the longest in the latency chain of
`REQ-CoreLR-D-Stepper-0012`.

**What it is.** The protocol the library used, nothing more: a character or an instruction is **one transaction**,
START, `SLA+W`, four data bytes `{hi|E, hi, lo|E, lo}` (the two nibbles each strobed on E, RS and the backlight bit
in every byte), STOP — the backpack's usual wiring: P0 RS, P1 RW (low), P2 E, P3 backlight (on), P4…P7 D4…D7. The
~90 µs a byte takes on the bus is longer than the controller's 37 µs instruction time, so no delay is needed between
ordinary instructions; only the power-up steps and *clear* wait, and only `LcdI2c_Init()` issues those.

**Scope.** One or two lines (`LCDJOYSTICKUI_ROWS` ≤ 2, `#error` otherwise: the row offsets of a four-line display
differ). No *clear*, *home*, cursor styles, shifts or custom characters are provided: nothing uses them, and an
unused function is a function someone will one day link.

## REQ-CoreLR-D-LcdI2c-0002 — Every wait is bounded, and a failed call releases the bus
<a id="REQ-CoreLR-D-LcdI2c-0002"></a>
`Wire`'s waits had no timeout (its default timeout is off), so a bus held by a fault froze the main loop — and with
it the refill of the step queue — for ever. Here:

- no wait exceeds `LCDI2C_TIMEOUT_LOOPS` polls (`SwConfig.h`; 2400 = **~1.2 ms**: a poll is 8 cycles, counted in the
  disassembly, and a byte takes 90 µs, so the bound is 13 byte-times);
- every call returns 1 if each byte was acknowledged, 0 if not, and a call that fails still sends the STOP, which is
  also what releases the bus after a NACK; if even the STOP does not complete, the TWI unit is switched off and the
  next call switches it on;
- after a NACK of the address nothing more is sent (the next valid operation is a STOP — the model of the bus
  flags a data byte there as a protocol error).

The caller decides what a failure means. `LcdJoystickUI` leaves the cell dirty and offers it again on the next call
(`REQ-CoreLR-D-LcdJoystickUI-0011`); with no display on the bus that costs 110 µs a call (start and a NACKed
address).

## REQ-CoreLR-D-LcdI2c-0003 — Initialisation by instruction, with the datasheet's waits
<a id="REQ-CoreLR-D-LcdI2c-0003"></a>
`LcdI2c_Init()`: TWI at `LCDJOYSTICKUI_I2C_HZ` (100 kHz, the PCF8574's own limit; `TWBR` 72), the internal pull-ups
on SDA and SCL as `Wire` enabled them; 50 ms; the 8-bit function-set nibble `0x3` three times with 4.5 ms and
200 µs waits (the datasheet's 4.1 ms and 100 µs, rounded up), the switch to 4 bits (`0x2`), function set (two lines,
5×8), display on, **clear (1.6 ms)**, entry mode (increment). Blocks **~60 ms** (59.5 ms in the model). It returns 1
if the display acknowledged throughout; the caller may ignore it.

The two 200 µs waits are redundant at 100 kHz — the bus time between strobes is at least 290 µs — and are kept so
that a faster bus does not silently break the 100 µs rule; that is the one deliberate equivalent mutant of the test.

## REQ-CoreLR-D-LcdI2c-0004 — Known limitation: a failure that splits a byte garbles the display
<a id="REQ-CoreLR-D-LcdI2c-0004"></a>
Measured on the model (`tests/host/test_lcdi2c.cpp`, section 6): a NACK of the **first or second** data byte of a
call is harmless (the next call completes it); a NACK of the **third or fourth** leaves the controller holding half a
byte, after which it pairs the wrong nibbles and the display is garbled. **`LcdI2c_Init()` recovers it, from either
nibble phase** (its three `0x3` nibbles put the controller back in 8-bit mode whatever it was holding — the model
had to be corrected to say so). `Wire` and the library had the same weakness.

Not handled yet, on purpose, and recorded: a recovery needs a blocking ~60 ms re-initialisation and a way to tell
`LcdJoystickUI` to mark the whole frame dirty, for a fault (noise on SDA/SCL near the stepper wiring) that has not
been seen. If the bench shows garbled displays, the fix is: a failed `Write()` sets a flag, the next call re-runs the
initialisation, and the driver reports it so the UI re-sends the frame.

## Verification
`tests/host/test_lcdi2c.cpp` runs the real `LcdI2c.c` against `lcd_model.h`: a TWI unit, a PCF8574 and an HD44780
with its **timing rules** on a virtual clock (a delay or a bus byte advances it; a strobe that arrives while the
controller is busy is counted). Results:

| | |
|---|---|
| init | state after init: 4-bit, two lines, display on, cleared, increment, backlight on; first strobe at 50.3 ms (≥ 40 ms required); no strobe too soon; start/stop balanced |
| text | both rows; the cursor lands at every column of both rows; **one character = 470 µs** on the bus (47 clocks; protocol arithmetic) |
| the row end | after the last visible column the address counter runs on into memory that is not shown (`0x10`), **not** to the next row |
| 200 075 random cursor moves and characters | 0 strobes while the controller was busy |
| faults | absent display (110 µs, 3 polls, bus released); a NACK of each of the 4 data bytes; a stuck bus (2 401 polls, the bound); a STOP that never completes (the unit is switched off); each followed by a successful call |
| the limitation above | measured, and `Init` recovers it |

**Mutation testing** — eighteen deliberately broken copies of the driver: nibbles swapped, E never falling, RS missing,
three init waits cut, STOP skipped after a failure, NACKs ignored, the read bit in `SLA`, an unbounded wait, a wrong
row offset, 400 kHz, no pull-ups, a one-line function set, decrementing entry mode, a stuck-STOP guard removed.
**Seventeen caught**; the survivor is the equivalent mutant of `-0003`. Two were caught only after the model was
tightened (it now rejects a data byte after an address NACK, and a hang counts as a catch).

`tests/host/test_ui.cpp` runs the real `LcdJoystickUI.cpp` (`#include`d, so its frame buffer is visible)
against the same model: the glass equals the frame after start-up and on all seven screens under random changes
(122 checks); **no bus traffic at all in 300 calls with nothing changing**; at most 2 transactions and **0.94 ms** of
bus time in any one call; a change spanning the last cell of row 0 and the first of row 1 lands in place;
padding, clipping and replacement of longer text by shorter; the glass converges after 200 injected transient faults.
Nine broken copies of the UI: eight caught (one only after a content check was added), the ninth an equivalent mutant
under the benign fault set (a character that failed to land leaves the physical cursor where it was).

## REQ-CoreLR-D-LcdI2c-0005 — User-defined characters (CGRAM), loaded from flash (decided 2026-10-13)
<a id="REQ-CoreLR-D-LcdI2c-0005"></a>
The display of the maintainer's machine carries the **A00 (Japanese) character ROM** (T09_LcdRom, 2026-10-13): no up or down arrow, nothing that reads as a pen. The controller lets the program define 8 characters of 5 x 8 dots (codes 0..7, in its CGRAM). `LcdI2c_DefineCharP(code, rows)` writes one: the CGRAM address command (`0x40 | code << 3`), then the eight row bytes read **from flash** (`pgm_read_byte`), the five low bits of each being the dots of the row, top row first. It leaves the controller's address counter in CGRAM: the caller sets the cursor (a DDRAM address) before its next character -- the UI does, its first write after `Init()` being a cursor move. Call it after `LcdI2c_Init()` (the clear of the init would not touch CGRAM, but a re-initialised controller would have lost nothing either: CGRAM survives a clear, not a power cycle). Use codes 1..7: a code 0 is the string terminator in C. Costs the Send of 9 bytes per glyph (about 4 ms on the bus, once at start-up). **Verified:** `tests/host/test_lcdi2c.cpp` (the model of the controller now has a CGRAM: the eight rows land in the right glyph, the data pointer returns to DDRAM at the next address command, a second glyph does not disturb the first), with mutants.
