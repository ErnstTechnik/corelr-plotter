# Third-party code

Everything here lives under `third_party/`, is never edited casually, and carries a `PROVENANCE.md` saying where it came from and what, if anything,
was changed. The coding standards of this project deliberately do **not** apply to it.

| Component | Where | Origin | Licence | Changes |
|---|---|---|---|---|
| sdfatlib (lower layer of the Arduino `SD` library 1.3.0) | `third_party/sdfat/` | William Greiman, via Arduino `SD` 1.3.0 | GPLv3 | two build switches by pure addition: `SDFAT_READ_ONLY`, `SDFAT_NO_SERIAL` — see its `PROVENANCE.md` |
| Arduino AVR core 1.8.8 (core, Nano variant pins, SPI) | `third_party/arduino-core/` | Arduino | LGPL 2.1 | none, byte-identical (manifest in its `PROVENANCE.md`) |

The reference projects `grbl` (GPLv3) and `Walldraw` informed the design but contribute no code to this repository.
