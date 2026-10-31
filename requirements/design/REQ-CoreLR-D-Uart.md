# Design Requirements — Uart

Module: **Uart** — the serial port of the serial variant: a receive ring filled by interrupt, the real-time bytes taken out
of the stream there, and a transmit ring emptied by interrupt. **Serial variant only**: in the SD build `Uart.c` and `Uart.h` are empty.

Realizes: [`REQ-CoreLR-F-SerialControl-0005`, `-0007`](../functional/REQ-CoreLR-F-SerialControl.md) (128 bytes in flight; real-time
bytes honoured at any time), request **FW-02** of [`docs/protocol/SerialProtocol.md`](../../docs/protocol/SerialProtocol.md).

Origin: the contract's §3 and §4.2, written by the host side and approved 2026-10-08; implemented 2026-10-09.

## REQ-CoreLR-D-Uart-0001 — Our own driver, not `HardwareSerial` (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0001"></a>
*Applies to: serial image only*

Callers point at `Uart.h`, never at `Serial`. The Arduino core's `HardwareSerial` was **measured** at 732 bytes of flash and 175 of RAM
when it was unlinked from the SD build (`docs/bringup/Bringup-Status.md`, 2026-10-06), brings `Print` and `Stream`, and reads bytes only in
`loop()`: a `!` sent while a line is held back would wait behind it, and be dropped if its ring were full. The contract's decision
(§3, point 1 and 2), and the same trade the project already made for the LCD (`REQ-CoreLR-D-LcdI2c`, −2 074 bytes).

Both `HardwareSerial` and this module define `USART_RX_vect` and `USART_UDRE_vect`: nothing in the serial build may include
`<HardwareSerial.h>` or call `Serial`, or the link fails. `setup()` therefore no longer calls `Serial.begin()`.

## REQ-CoreLR-D-Uart-0002 — The receive ring holds 128 bytes, exactly (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0002"></a>
*Applies to: serial image only*

`UART_RX_SIZE` (`SwConfig.h`) = **128**, a power of two. UGS, bCNC and Candle hard-code 128 (127 for Candle) in their character
counting, and CNCjs and gSender can only *raise* their figure from `Bf:` (contract §4.5): a smaller ring would be overrun by a host
that counts correctly.

The indices are **free-running bytes** (0..255, wrapping); the slot is `index & 127` and the fill is `head − tail`. A ring whose
indices wrap with the slot number can hold only `SIZE − 1`, and a host that has sent exactly 128 bytes while the firmware was not
reading would overflow it by one; this one fills to 128 (test: 128 bytes in, no gap; the 129th is lost). `head` is written by the
interrupt only, `tail` by `loop()` only (and by `Uart_RxFlush()`, which does it under `cli()`); a one-byte access is atomic on AVR,
so neither side needs a critical section to use them.

## REQ-CoreLR-D-Uart-0003 — Real-time bytes leave the stream in the interrupt (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0003"></a>
*Applies to: serial image only*

`?` `!` `~` and Ctrl-X (0x18) set a flag (`UART_RT_STATUS`/`_HOLD`/`_RESUME`/`_CANCEL`) and are **not stored**; `Uart_TakeRealtime()` returns
and clears the flags atomically. Several of the same kind received before `loop()` looks count once (two `?` give one report; they
would give two in grbl, which is harmless). Bytes **0x7F..0xFF** are dropped there (0x80..0xFF are grbl's override commands, not
supported; contract §4.2). Everything else goes in the ring, control characters and spaces included: stripping is the line
assembler's job (`REQ-CoreLR-D-SerialLink-0002`), and a `?` inside a comment is still a real-time command, as in grbl.

The order of the real-time bytes among themselves, and relative to the data, is **not** kept: `!~` received within one `loop()` pass
is seen as both flags set, processed `!` first. A pass is about a millisecond (§ measured, `REQ-CoreLR-D-SerialLink-0009`).

## REQ-CoreLR-D-Uart-0004 — Lost data is marked where it was lost (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0004"></a>
*Applies to: serial image only*

Data is lost when the ring is full (a host that does not count), or when the hardware reports a framing error (the byte is garbage and
is dropped) or an overrun (an earlier byte was lost; the byte in the register is good and is kept). In each case the interrupt records
the ring's head as a **gap**; `Uart_RxAtGap()`, called before each `Uart_RxPop()`, returns 1 once, when the next byte is the first one after
the loss. The line assembler marks the line it is reading as damaged and answers it `error:11` instead of running it (contract §4.13):
a truncated or garbled coordinate would otherwise be a different, legal-looking one.

A flag read late would have marked the wrong line: the loop may see the fault several bytes after it happened, and the ring holds
whole lines between the loss and the reader. Only the **first** gap is kept until it has been read past; a second loss before that folds
into it (a host that overruns twice within a ring's length has other problems), and a flush forgets it.

## REQ-CoreLR-D-Uart-0005 — Transmission never blocks (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0005"></a>
*Applies to: serial image only*

`Uart_TxWrite()` / `Uart_TxWriteP()` put bytes in a **64-byte** ring (`UART_TX_SIZE`; the contract suggests 64) and enable the
data-register-empty interrupt, which sends them and switches itself off after the last. The caller checks `Uart_TxFree()` first; a byte
that does not fit is dropped — never waited for. `SerialLink` never starts a reply that does not fit whole
(`REQ-CoreLR-D-SerialLink-0003`), so the drop is a safety net, not a mechanism; the test shows it neither blocks nor overwrites.
`UCSR0B` is rewritten by both contexts, so starting the transmission does it under `cli()`.

## REQ-CoreLR-D-Uart-0006 — Baud rate (implemented 2026-10-09)
<a id="REQ-CoreLR-D-Uart-0006"></a>
*Applies to: serial image only*

115 200 baud, 8N1 (`UART_BAUD`), double-speed mode (`U2X0`): `UBRR = F_CPU / (8·baud) − 1`, rounded = 16, an actual **117 647 baud,
+2.1 %** (the Arduino core does the same). Without the double-speed mode it would be 111 111, −3.5 %. The error is computed at
compile time and a rate that cannot be made within 2.5 % is an `#error`. The test checks the register values and prints the figure.

## REQ-CoreLR-D-Uart-0007 — Cost, measured (2026-10-09)
<a id="REQ-CoreLR-D-Uart-0007"></a>
*Applies to: serial image only*

**Interrupts**, on the cycle simulator (`tools/analysis/cyclesim/t_uart.c`, `run_uart.py`; entry to `RETI`, plus 3 cycles for the vector's `JMP`):

| Handler | Cycles | µs at 16 MHz |
|---|---|---|
| receive, a data byte | 71 (+3) | 4.6 |
| receive, a real-time byte | 53 (+3) | 3.5 |
| receive, the ring is full (a gap is noted) | 79 (+3) | 5.1 |
| receive, a framing error | 58 (+3) | 3.8 |
| transmit, a byte (more to send) | 60 (+3) | 3.9 |
| transmit, the last byte | 64 (+3) | 4.2 |

At 115 200 baud a byte takes 85 µs (11 765 bytes/s): **a stream of data bytes costs 74 × 11 765 = 5.4 % of the CPU**, a full-duplex link
10 % at most (the replies are far shorter than the lines in practice). The longest handler is 82 cycles = **5.1 µs**: that is the most it
can delay the Timer1 step interrupt (`Stepper`), which has the higher priority and runs before a pending lower one. Against the 32 µs
minimum hold (`REQ-CoreLR-D-Stepper-0012`: the chain there was one Timer0 ISR + one TWI ISR + our own ~8 µs ≈ 23 µs, *estimated*; the TWI
interrupt has been gone since the LCD driver was replaced), this is one more ISR of the same order as Timer0's. **Bench M-7 remains**
(jitter of the step edges under real traffic).

**A lesson, measured**: with a *call* in the receive handler, even on its rare path, the compiler saved all 17 call-clobbered registers on
every byte (196 bytes of code, ~100 cycles); with the one-statement helper inlined (`MarkGap`, `always_inline`) it saves the 8 it uses.

**Flash and RAM** (Nano fqbn, serial variant, `tools/analysis/line_sizes.py` by source file): `Uart.c` **596 bytes** (both handlers, the rings' access, the
driver); RAM **200 bytes** (RX 128 + TX 64 + 8 of state). The contract estimated 250–350 bytes of flash.

## REQ-CoreLR-D-Uart-0008 — Verification (2026-10-09)
<a id="REQ-CoreLR-D-Uart-0008"></a>
*Applies to: serial image only*

`tests/host/test_uart.c`: the real `Uart.c` with the USART0 registers as variables and the two handlers called where the hardware
would call them — configuration (UBRR, `U2X0`, 8N1, which bits of `UCSR0B`); data in order; the four real-time bytes, coalescing, inside a line;
all 256 byte values (123 reach the ring); exactly 128 bytes, the 129th lost, the gap reported once and in the right place; 100 000 bytes through the
wrapping indices in bursts of random length; framing error, overrun, a loss on an empty ring, two losses, a flush with a gap pending; transmission,
a full ring (drops, never overwrites), 100 000 bytes interleaved. `tests/mutation/mutants/uart.txt`: 33 broken copies, **30 caught**; the 3 left are
**equivalent or untestable on the host**: `0x7E` dropped as if real-time (already caught earlier in the chain: equivalent), a flush that moves the
head instead of the tail (equivalent single-threaded, wrong under interrupts: the one case a host test cannot see; the code moves the tail),
and the bootloader leftovers not read at start (the registers have no side effects in the model: bench). A first run left a fourth alive,
a second loss replacing the first, and a test now pins where the gap is reported.

## REQ-CoreLR-D-Uart-0009 — 0x85, the jog cancel (implemented 2026-10-10)
<a id="REQ-CoreLR-D-Uart-0009"></a>
*Applies to: serial image only*

The receive interrupt gives 0x85 its own flag, `UART_RT_JOGCANCEL` (grbl's jog-cancel byte, `REQ-CoreLR-D-SerialLink-0011`); it is not stored in the ring. The other bytes of 0x80..0xFF -- grbl's override commands -- are still dropped with no flag.
Cost, measured on the cycle simulator (`run_uart.py`): a data byte 74 cycles (it was 71 + 3), a real-time byte 53; no change to the interrupt's budget in any way that matters (4.6 µs at 16 MHz). **Verified:** `test_uart.c` (the flag raised by 0x85 alone among the 256 byte values; 0x84, 0x86, 0x80, 0xFF flag-free and dropped); `tests/mutation/mutants/jogcancel_uart.txt`: 3, all caught.

