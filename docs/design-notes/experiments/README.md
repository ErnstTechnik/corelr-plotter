# Experiments kept, not built

Changes that were written, measured and **deliberately not adopted**, kept as patches so the
work is not lost and the decision can be revisited with the numbers in hand.

## `derive-position-from-steps.patch` (2026-10-07)

Stores only the motors' absolute step counts in the EEPROM position record and **derives X,Y
from them at start-up** — the maintainer's original decision for
`REQ-CoreLR-D-Calibration-0007`, set aside on 2026-10-06 because it cost flash, and retried
once the arithmetic had been optimised.

    git apply docs/design-notes/experiments/derive-position-from-steps.patch

(then adapt `tests/host/test_position.c`, which still names `Motion_Restore`/`Motion_GetPersistedState`.)

| | stored beside the counts (adopted) | derived (this patch) |
|---|---|---|
| EEPROM record | 18 bytes | 10 bytes |
| Flash | 28252 | **28948 (+696)** |
| Accuracy of the restored position | exact | worst **53 µm** over 1 400 positions (under one 54 µm step) |

`Motion_SeedFromSteps` alone is 774 bytes. The retry used 32-bit lengths (the circumference is
53·2048 + 1411, so a length is `n·53 + (n·1411 + 1024)/2048`), explicit 32×32→64 widening
multiplies, and the new Newton root — and it did not get smaller than the first attempt (772).
The formula needs one 64-bit product (≈ 2^43) and one 64-bit division, and AVR spends ~700 bytes
on that however it is written.

**Verdict:** not worth 696 bytes of flash to save 8 bytes of EEPROM, which is the one resource of
this chip the project does not use. Revisit only if flash stops mattering, or if the position is
ever wanted in a form that does not need the product (a lookup, or an iteration on `Motion_XYToLR`
and its numerical Jacobian, which was considered and not tried: a 2×2 solve is itself ~300 bytes).
