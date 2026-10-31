# Arduino Pin Mapping — CoreLR

Reference document (not a formal requirement) backing the pin
assignments used by `REQ-CoreLR-D-Stepper`,
`REQ-CoreLR-D-PenControl`, `REQ-CoreLR-D-LcdJoystickUI` and
`REQ-CoreLR-D-SDReader`. All values below are meant to be defined as
macros in `HwConfig.h` (see `docs/standards/STD-FileOrganization.md`), never hard-coded inline.

Target board: Arduino Uno / Nano (ATmega328P) — the same class of board
used by the `Walldraw` reference. Confirmed 2026-09-06: the physical
hardware is a laser-cut mounting plate holding a bare Arduino Uno, a
separate SD reader module, motors and servo — all connected by jumper
wires, **not** a fixed custom PCB (see `Walldraw/Wall Drawing Machine
2020安装调试说明.pdf`, installation photos). This means pin assignments
can be freely chosen/rewired; we are not locked into `Walldraw.ino`'s
original wiring.

## Why this needed a dedicated pass
Two 4-wire unipolar stepper motors need 8 output bits total. Hardware
constraints fix or conventionally reserve several pins regardless of our
choice:
- `D0`/`D1` (RX/TX) — fixed silicon UART pins, needed for
  `REQ-CoreLR-F-SerialControl` (deferred but confirmed) and general
  debug output.
- `A4`/`A5` (SDA/SCL) — fixed silicon I²C (TWI) pins, needed for the
  LCD (`REQ-CoreLR-D-LcdJoystickUI-0001`).
- `D11`/`D12`/`D13` (MOSI/MISO/SCK) — **fixed silicon**, not a
  convention: these are the ATmega328P's hardware SPI unit, which
  sdfatlib uses (`USE_SPI_LIB`). They cannot be reassigned, and once
  `SPE` is set the SPI unit takes over the pins' direction regardless
  of what `DDRB` says. Calling them "conventional" is what the
  2026-09-10 wiring conflict came down to (see the end of this file).
- `A0`–`A3` — needed as true analog inputs for the joystick (`A1`,`A2`)
  and conventionally kept together with the servo (`A0`) and joystick
  button (`A3`).

That leaves only **6 free bits in Port D** (`D2`–`D7`) and **3 free bits
in Port B** (`D8`–`D10`, since `D11`–`D13` are SPI) — 9 free bits total,
just enough for 8 motor-drive bits plus one spare (used here for the SD
chip-select). The board is therefore fully allocated, with no free pin.

**Contiguity does not matter, and the layout does not try for it**
(settled 2026-09-10). The ISR drives each motor wire with its own
single-cycle `sbi`/`cbi` on a compile-time-constant bit position — see
"Stepper ISR register writes" below — so whether a motor's four bits sit
in one nibble, or even in one port, makes no difference to the emitted
code. Earlier revisions of this document argued at length about which
motor should get "the clean nibble"; that argument belonged to the
masked whole-port write scheme which was tried and rejected, and it is
obsolete.

What the layout optimises for instead is the **wiring**: each motor's
`IN1`..`IN4` lands on four consecutive Arduino pins in that order, so a
ribbon from a ULN2003 board arrives in the same order it left. Motor R
takes `D2`-`D5`, Motor L takes `D6`-`D9`, and Motor L straddling the
`PORTD`/`PORTB` boundary between `D7` and `D8` is simply irrelevant.

## Pin table

| Pin | Function | Notes |
|---|---|---|
| `D0` | Serial RX | reserved, fixed silicon |
| `D1` | Serial TX | reserved, fixed silicon |
| `D2` | Motor R, `IN1` | `Stepper`, Port D bit 2 |
| `D3` | Motor R, `IN2` | `Stepper`, Port D bit 3 |
| `D4` | Motor R, `IN3` | `Stepper`, Port D bit 4 |
| `D5` | Motor R, `IN4` | `Stepper`, Port D bit 5 |
| `D6` | Motor L, `IN1` | `Stepper`, Port D bit 6 |
| `D7` | Motor L, `IN2` | `Stepper`, Port D bit 7 |
| `D8` | Motor L, `IN3` | `Stepper`, Port B bit 0 |
| `D9` | Motor L, `IN4` | `Stepper`, Port B bit 1 — Motor L straddles the port boundary, which costs nothing (see below) |
| `D10` | SD card chip-select | Port B bit 2. Also the SPI `SS` pin, which is the intended arrangement: it is the Arduino-conventional SD CS *and* it settles the AVR requirement that `SS` be an output in master mode |
| `D11` | SD card MOSI | hardware SPI, **fixed silicon** |
| `D12` | SD card MISO | hardware SPI, **fixed silicon** |
| `D13` | SD card SCK | hardware SPI, **fixed silicon** |
| `A0` | Pen servo signal | `PenControl`, matches `Walldraw.ino`'s `pen.attach(A0)` |
| `A1` | Joystick X (analog) | `LcdJoystickUI` |
| `A2` | Joystick Y (analog) | `LcdJoystickUI` |
| `A3` | Joystick button (`SW`) | `LcdJoystickUI`, digital input, pulled up |
| `A4` | LCD I²C SDA | fixed silicon |
| `A5` | LCD I²C SCL | fixed silicon |

**Every pin on an Uno/Nano is allocated — zero spare.** There is no
headroom for anything not already in this table (e.g. limit switches, a
status LED, a buzzer) without giving something up or moving to a board
with more I/O.

## Stepper ISR register writes (settled 2026-09-06)
Several earlier revisions tried, in turn: hand-derived masked writes
tied to this exact pin layout (brittle — a pin `#define` change would
silently break the shift arithmetic); a runtime-cached
`(port register pointer, bitmask)` pair per pin (correct and generic,
but a runtime lookup and RAM cost, giving up atomic single-instruction
writes); then a manually-written per-wire port/DDR/bit association
(correct, but a human could still pair the wrong port with a pin,
caught only after the fact by an `#error` check).

The settled design derives each wire's port/DDR/bit **automatically**
from its raw pin number via `#if`/`#else` (0-7 → `PORTD`, 8-13 →
`PORTB`, subtracting 8), so pairing the wrong port with a pin is
structurally impossible rather than merely checked for. The resulting
`SET()`/`CLR()`/`DDR_SET()` macros still bake the bit position in as a
**compile-time constant**, so avr-gcc emits the atomic single-cycle
`sbi`/`cbi` instruction for each — no runtime pin/port resolution, no
cached-pointer RAM.

**Scope, stated explicitly:** all of this (the derivation, the macros,
a board-target guard requiring `__AVR_ATmega328P__`/`__AVR_ATmega328__`)
lives entirely inside `Stepper.c`, not
`HwConfig.h` — nothing else in the project needs register-level
pin access (minimum-scope principle). `HwConfig.h` states only
the raw pin numbers in this table, with no AVR dependency of its own.
The board-target guard covers Uno *and* Nano (same chip, same pin
mapping) while rejecting Mega/Leonardo/Due/ESP32/etc., whose mapping
(or entire register model) differs.

See `requirements/design/REQ-CoreLR-D-Stepper.md` (`-0001` for
the packed 32-bit entry this feeds, `-0005` for the full derivation and
macro rationale, `-0008` for the narrowed read-modify-write safety
argument — now only relevant to the SD chip-select pin, which has no
hardware override and must stay behind `digitalWrite()`).

## Rejected wiring, 2026-09-10 — why both motors cannot be port-clean

A wiring attempt put Motor L on `D2,D3,D5,D6` and Motor R on
`D8,D9,D10,D11`, with SD chip-select on `D12`. Its appeal was that each
motor would lie entirely within one port — an advantage which, as the
section above now records, **is worth nothing**, since the ISR writes
each bit separately. It also cannot work. The board was rewired to the
table above instead. Recorded here so the idea is not re-invented:

- **CS on `D12` = MISO.** Once `SPI.begin()` sets `SPE` with `MSTR`,
  the SPI unit forces `PB4` to be an *input* driven by the card. A
  `digitalWrite()` on it does not reach the pin, so the card could
  never be selected and `Sd2Card::init()` would fail. Chip select must
  be an ordinary GPIO.
- **Motor R wire 4 on `D11` = MOSI.** Symmetrically, `PB3` belongs to
  the SPI unit, so writes to Port B bit 3 never reach the pin: the
  fourth coil wire is dead and the 4-phase pattern is silently wrong.
  The motor buzzes or turns erratically rather than failing outright,
  which is the unpleasant part.
- **Motor R wire 3 on `D10` = SS** would in fact have worked, but only
  by accident of ordering: `SPI.begin()` forces `SS` high only when it
  is not already an output, and `Stepper_Init()` runs before
  `SDReader_Init()`. Swapping those two calls in `setup()` would have
  broken that wire with no other symptom.

Two lessons, both recorded rather than re-learned. Calling
`D11`/`D12`/`D13` "conventional" SPI pins — as an earlier revision of
this file did — is what makes the trap easy to fall into; they are
fixed silicon, and the SPI unit overrides `DDRB` for them once `SPE` is
set. And the goal this layout was chasing was itself a phantom: pursuing
per-port cleanliness pushed the wiring onto `D8`-`D11` for a benefit the
implementation cannot use.


## Bench wiring guide

Everything above is *allocation* — the reasoning and the constraints.
This section is the same information arranged for a soldering iron:
connector by connector, with nothing to derive.

Pin numbers all come from `firmware/config/HwConfig.h`; if you change one
there, change it here too.

Both motors follow the same rule: `IN1`..`IN4` on four consecutive
pins, in order. Motor R is the lower group (`D2`-`D5`), Motor L the
upper one (`D6`-`D9`).

### Motor R — 28BYJ-48 via a ULN2003 driver board

| Driver board | Arduino | `HwConfig.h` | Motor wire colour |
|---|---|---|---|
| `IN1` | `D2` | `STEPPER_MOTOR_R_PIN0` | blue |
| `IN2` | `D3` | `STEPPER_MOTOR_R_PIN1` | pink |
| `IN3` | `D4` | `STEPPER_MOTOR_R_PIN2` | yellow |
| `IN4` | `D5` | `STEPPER_MOTOR_R_PIN3` | orange |
| `+`/`VCC` | **external 5 V**, not the Arduino's 5 V pin — see Power below | | |
| `−`/`GND` | common ground | | |

### Motor L — same driver board type

| Driver board | Arduino | `HwConfig.h` | Motor wire colour |
|---|---|---|---|
| `IN1` | `D6` | `STEPPER_MOTOR_L_PIN0` | blue |
| `IN2` | `D7` | `STEPPER_MOTOR_L_PIN1` | pink |
| `IN3` | `D8` | `STEPPER_MOTOR_L_PIN2` | yellow |
| `IN4` | `D9` | `STEPPER_MOTOR_L_PIN3` | orange |
| `+`/`VCC` | **external 5 V** | | |
| `−`/`GND` | common ground | | |

The motor's own 5-wire connector plugs straight into the ULN2003
board, so the colours above are informative rather than something to
wire by hand — they matter only if you are wiring a bare motor or a
different driver.

**`IN`n order is what the firmware assumes, and it is not arbitrary.**
`Motion.c`'s `kPhaseTable` is `{0x0C, 0x06, 0x03, 0x09}` with bit 0 =
`IN1`, verified bit-for-bit against
`Walldraw/Lib/libraries/TinyStepper_28BYJ_48` (`setOutputsForStep()`'s
four cases). So `PIN0`→`IN1`, `PIN1`→`IN2`, `PIN2`→`IN3`, `PIN3`→`IN4`.
Two useful consequences:

- Wiring `IN1..IN4` in **reverse** order only reverses the direction of
  rotation, which is fixable in software —
  `STEPPER_MOTOR_L_REVERSED` / `STEPPER_MOTOR_R_REVERSED` in
  `HwConfig.h` (currently `1` for L, `0` for R since 2026-10-10: the bench showed both motors turning the wrong way with the previous `0`/`1`).
- Wiring them **scrambled** (any order that is neither the given one nor
  its reverse) is *not* fixable in software: the coil sequence stops
  being a rotating field and the motor will judder in place. If a motor
  vibrates without turning, suspect this before suspecting the firmware.

### Pen servo

| Servo lead | Arduino | `HwConfig.h` |
|---|---|---|
| signal (orange/white) | `A0` | `PENCONTROL_SERVO_PIN` |
| `+` (red) | **external 5 V** | |
| `−` (brown/black) | common ground | |

`A0` is used as a plain digital output here; the `Servo` library drives
it by timer. Matches `Walldraw/Walldraw.ino`'s `pen.attach(A0)`.

### Joystick module (2 axes + push button)

| Module | Arduino | `HwConfig.h` |
|---|---|---|
| `VRx` | `A1` | `LCDJOYSTICKUI_X_PIN` |
| `VRy` | `A2` | `LCDJOYSTICKUI_Y_PIN` |
| `SW` | `A3` | `LCDJOYSTICKUI_SW_PIN` |
| `+5V` | Arduino `5V` (a joystick draws almost nothing) | |
| `GND` | common ground | |

`SW` needs **no external pull-up**: `LcdJoystickUI_Init()` configures
it as `INPUT_PULLUP` and treats `LOW` as pressed. The axes are read as
true analog inputs, centre assumed at `LCDJOYSTICKUI_ADC_CENTER` = 512
with a dead zone around it (`SwConfig.h`).

**Polarity.** What the analog value does when the stick moves depends on how the module is turned and wired, so it is a hardware setting per axis:
`LCDJOYSTICKUI_X_INVERT` and `LCDJOYSTICKUI_Y_INVERT` in `HwConfig.h`. The firmware's convention is that the stick read is **positive to the right (X) and down (Y)**; a flag at `1`
negates the reading of an axis whose unit reads the other way. Set to `1`/`1` on 2026-10-10 after the maintainer's bench observation (both jog axes inverted with `0`/`0`). It acts
everywhere the stick is read: the Control jog, the menus, the Adjustment jog, the calibration wizards. (The maintainer confirmed on 2026-10-10 that the menus go the right way with it.)

### LCD 2×16 with a PCF8574 I²C backpack

| Backpack | Arduino | Note |
|---|---|---|
| `SDA` | `A4` | fixed silicon (TWI) |
| `SCL` | `A5` | fixed silicon (TWI) |
| `VCC` | Arduino `5V` | |
| `GND` | common ground | |

**I²C address**: `HwConfig.h` sets `LCDJOYSTICKUI_I2C_ADDRESS` to
`0x3F`. PCF8574 backpacks ship as either `0x27` or `0x3F` depending on
the chip variant (`PCF8574` vs `PCF8574A`) and the solder jumpers. A
blank but backlit screen is the classic symptom of the wrong address —
change that one constant rather than the wiring.

### SD card module

| Module | Arduino | `HwConfig.h` | Note |
|---|---|---|---|
| `CS`/`SS` | `D10` | `SDREADER_CS_PIN` | the only one that is ours to choose |
| `MOSI`/`DI` | `D11` | — | fixed silicon |
| `MISO`/`DO` | `D12` | — | fixed silicon |
| `SCK`/`CLK` | `D13` | — | fixed silicon |
| `VCC` | `5V` or `3V3` per your module | | most breakout boards have a regulator and a level shifter and want 5 V |
| `GND` | common ground | | |

`MOSI`/`MISO`/`SCK` are **not** reassignable — see "Rejected wiring"
above for what happens if a motor wire or the chip select lands on one
of them.

### Power

Two 28BYJ-48 motors plus a servo draw far more than the Arduino's
on-board regulator can supply — each stepper holds roughly 200–300 mA
per energised phase pair, and this firmware keeps two phases energised
at all times (`kPhaseTable` is a 2-phase-on sequence, and a hold entry
simply keeps the last coil pattern). Feed the two ULN2003 boards and
the servo from a **separate 5 V supply**, and tie its ground to the
Arduino's. Powering them from the `5V` pin is the usual cause of resets
and erratic stepping mid-plot.

Nothing in the firmware measures or manages supply current, so this is
purely a wiring discipline.

### The Nano's own supply (stated by the maintainer, 2026-10-14)

The external 5 V supply also feeds the **Nano's `5V` pin**, and its ground is the Nano's `GND`: in SD mode the USB port is not connected, so the supply is the
Nano's only source. The LCD, the joystick and the SD module are powered from that `5V` pin (and so, indirectly, from the external supply); the boards and the servo
are fed directly from the supply. `docs/hardware/images/wiring/` draws exactly this, and `tools/generators/gen_wiring.py --verify` checks it.
When the USB cable *is* connected (serial variant, or a PC for flashing) while the supply is on, the two 5 V are **simply tied together on the Nano's `5V` pin**
(maintainer, 2026-10-14: the current state, which the maintainer finds unsatisfactory). Whether the Nano blocks the back-feed (a series diode between the USB `VBUS` and the `5V`
rail) depends on the board, and clones differ: it is to be read on the schematic of the board in use and measured, not assumed. Proposed, not built: a Schottky diode (1N5817) in series, anode on the supply, cathode on the `5V` pin, drawn in `docs/hardware/images/wiring/overview.svg`.

### Free pins

None. `D0`/`D1` are held for serial (`REQ-CoreLR-F-SerialControl`,
deferred), and every other pin is allocated above. Any new peripheral
would have to displace something — see "Why this needed a dedicated
pass" at the top of this file.
