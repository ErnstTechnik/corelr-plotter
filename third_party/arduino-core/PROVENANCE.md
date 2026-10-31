# Vendored Arduino AVR core

| | |
|---|---|
| Source | Arduino AVR Boards **1.8.8** (`arduino:avr`), as installed by the Arduino IDE 1.8.19 package manager |
| What | `cores/arduino/` (all of it), `variants/eightanaloginputs/pins_arduino.h` (the Nano's, which includes `variants/standard/pins_arduino.h`), `libraries/SPI/src/` (`SPI.cpp`, `SPI.h`, included by `sdfat/Sd2Card.cpp`) |
| Licence | **LGPL 2.1** (header of each file). Relinking is possible: the whole core is here and `cmake/` shows how it is built |
| Modifications | **none** — every file is byte-identical to the upstream one; the manifest below is the proof (`md5sum -c`) |
| Why vendored | so the firmware builds with nothing but a toolchain: the build no longer reads anything from the Arduino IDE's package folders. Date: 2026-10-11 |

What the firmware really uses of it: `main.cpp` (the `init()` + `setup()`/`loop()` driver), `wiring*.c` (the Timer0 `millis()` base, `analogRead`,
`pinMode`), `hooks.c`, `WInterrupts.c`, `Print.cpp`/`Stream.cpp` (base classes of sdfat's `SdFile`) and, through the linker's own choice, `HardwareSerial*.cpp`.
The rest is compiled into the core archive and dropped by the linker; it is vendored whole rather than trimmed, to keep the "byte-identical" claim simple.

Verify: `cd third_party/arduino-core && md5sum -c MANIFEST.md5`.
