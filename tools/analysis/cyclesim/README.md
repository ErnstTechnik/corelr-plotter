# cyclesim — measuring the firmware's CPU cost without the board

A small cycle-counting ATmega328P interpreter (`avrsim.py`, datasheet cycle
table, no peripherals, no interrupts) that runs the **real compiled code**
-- `Motion.c` built with the Arduino flags -- against stub neighbours.
Written 2026-10-06 to answer "is waypoint generation CPU-bound?" before the
bench could (see `docs/bringup/Bringup-Status.md`).

## Why it can be trusted
Two independent checks, both reproducible:

1. `run_isqrt.py` -- `Motion_ISqrt` run in the simulator matches
   `math.isqrt` on 12 inputs, including 0 and 2^64-1.
2. `run_motion.py` -- the byte stream of queue entries (coil bits + hold)
   produced by the **simulated AVR** is identical to the one produced by an
   x86 `-m32` build of the same source, 600 entries, including the
   hysteresis and the dithered holds. Any wrong instruction semantics would
   show up here.

Cycle counts follow the ATmega328P table (LD/ST 2, LPM 3, CALL 4, RET 4,
taken branch 2, ...). They are **CPU cycles of the code alone**: the 4 kHz
Timer2 ISR, Timer0 and the LCD's blocking I2C are not included, so every
"sustainable rate" derived from them is an upper bound.

## Use
    # t_motion.c / run_motion.py: the REAL Motion.c (v2 event generator), CPU load of a 30 mm move; the cost per waypoint
    # of the v1 generator (33 300 cycles) is in docs/bringup/Bringup-Status.md and the git history before the port.
    ./build.sh t_motion.c t_motion ../../../firmware/src/Trig.c -mcall-prologues && python3 run_motion.py out/t_motion.bin   # Motion needs Trig since the arcs (extra flags to compare: add them after Trig.c)
    python3 profile.py out/t_motion.elf out/t_motion.bin       # per-function share
    ./build.sh t_cands.c t_cands && python3 run_cands.py       # sqrt alternatives
    ./build.sh t_isqrt.c t_isqrt && python3 run_isqrt.py       # simulator self-check
    # t_arc.c / run_arc.py: arcs (Motion_ArcCheck, Motion_QueueArc, generation) -- build line in t_arc.c's header; Trig.c is passed as an extra source:
    ./build.sh t_arc.c t_arc ../../../firmware/src/Trig.c -DRADIUS=20000L && python3 run_arc.py out/t_arc.bin
    # t_hypot.c / run_hypot.py: Motion_Hypot under either arithmetic (REQ-CoreLR-D-Motion-0024), build lines in its header:
    ./build.sh t_hypot.c t_hypot_48 ../../../firmware/src/Trig.c -DCORELR_MATH=CORELR_MATH_48 && python3 run_hypot.py out/t_hypot_48.bin
    # t_uart.c / run_uart.py: the UART receive and transmit interrupts (serial variant); t_link.c / run_link.py: one SerialLink_Task() pass per kind of line:
    ./build.sh t_uart.c t_uart ../../../firmware/src/Uart.c -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL && python3 run_uart.py out/t_uart.bin
    ./build.sh t_link.c t_link ../../../firmware/src/{SerialLink,Uart,GCodeParser,Trig,TextFmt}.c -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL && python3 run_link.py out/t_link.bin
    # t_jogcurve.c / run_jogcurve.py: one JogCurve_Displacement() call (the Control jog's response), tabulated or computed shape:
    ./build.sh t_jogcurve.c t_jog_table ../../../firmware/src/JogCurve.c ../../../firmware/src/Trig.c -DJOGCURVE_USE_TABLE=1 && python3 run_jogcurve.py out/t_jog_table.bin
    # t_isr.c measures the OLD 4 kHz tick; t_isr2.c the v2 Timer1 / Timer2 interrupts (build line in its header).

`t_motion.c` assumes a 1 m motor spacing and 700 mm reference cable
(`Calibration` has no real machine dimensions yet). `ISqrt`'s cost depends on
the magnitude of its input (3.2k cycles for 0, 11.2k for 2^64-1), so a
different machine moves the figures by ~15 %, not by a factor.

Opcodes the interpreter does not know raise `NotImplementedError` rather
than being skipped -- extend it if a future build emits one (e.g. `ELPM`,
`FMUL`).
