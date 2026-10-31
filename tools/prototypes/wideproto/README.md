# wideproto — the 48-bit "Wide" library that was NOT adopted (2026-10-08)

Kept so it is not re-tried. `Wide.c`/`Wide.h` do 32×32→64 products, a 45-bit root, a hypot and a Q32 division with **no 64-bit
type**, as pairs of 32-bit words built from 16-bit limbs in C; `test_wide.c` proves them exact on the host (20 M products,
20 M roots below 2^45, 10 M hypots, 5 M divisions).

**Result: larger, not smaller.** Dropped into the firmware in place of the 64-bit code it was **+394 bytes** (31 108 against
30 858 at the time): `Wide_Mul` alone 322 bytes against 116 for libgcc's hand-written `__umulsidi3` + helper, because avr-gcc
spends 8–10 bytes on every 32-bit C operation. What did pay is *never forming the wide value*: the residual method in
`Motion_Hypot` (`REQ-CoreLR-D-Motion-0024`), −260 bytes.

Build and check: `gcc -O1 -Wall test_wide.c Wide.c -lm -o test_wide && ./test_wide` (about 5 s). `test_wide -h` prints this note.
