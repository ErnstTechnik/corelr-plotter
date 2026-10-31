# evproto — prototype of the v2 step-event generator

`evgen.c` is the algorithm of `REQ-CoreLR-D-Motion-0018`…`-0020` as a
self-contained C file: integer only, nothing wider than a 32x32→64 product,
so the **same source** runs on the PC and on the cycle simulator. It is *not*
part of the firmware build. It reuses the real `Motion_XYToLR`,
`Motion_ISqrt` and `Motion_StepToCoilBits` from `Motion.c`; the v1 generator
in that file is used only as the comparison.

    ./build.sh [-DEV_H=n] [-DEV_MERGE_MODE=n] [-DEV_WAYPOINT_STEPS=n] [-DEV_MIN_COUNTS=n]
        # host build + the six measurements below (each setting is a -D)
    ./build_avr.sh [-D...] && python3 run_cyc.py      # cycles on the AVR simulator
    python3 ../cyclesim/profile.py out/t_evgen_cyc.elf out/t_evgen_cyc.bin

| # | What it measures | Against |
|---|---|---|
| 1 | step-interval jitter at constant `VMAX`, six directions | the real v1 stream, same metric |
| 2 | reversals on 10 mm of travel tangential to a motor | v1 |
| 3 | rest-to-rest 20 mm: duration, fastest step | v1 |
| 4 | 50 chained jog-sized moves | — |
| 5 | drift over 400 random moves | the ideal cable length |
| 6 | fuzz, 3000 moves of 0 to 1 m, random entry/exit speeds, 7.7 M entries: no double-phase jump, no hold below the minimum, end deviation, **fastest step interval** | the rate limit |

Settings the measurements chose (the defaults in `evgen.c`): tie guard
`EV_H = 16`, merge mode 1 (delay), `EV_MIN_COUNTS = 64`, 4-step waypoints.
Results and the reasoning are in `REQ-CoreLR-D-Motion-0018`.

The jitter metric is the one the earlier notes used: rms and max of
`|next interval - interval| / mean interval` over the middle 60 % of a
motor's steps. A motor with a handful of steps in a move (the near-tangent
motor at 45°: 6 to 8 steps) gives a meaningless figure and is not
reported as a result.

**Since the port** (`Motion.c` now contains this generator): the "v1" rows of `./build.sh` compare against the real
`Motion.c`, which is no longer v1, and their stub still multiplies the old 250 µs ticks. To re-run the comparison with v1,
build against a snapshot of the tree from before the port:
`git archive 170b505 grbl_corelr | tar -x -C /tmp/v1 && FW=/tmp/v1 ./build.sh` (an old flat tree: the folder was then called grbl_corelr, not firmware). The real, ported code is
measured by `tests/host/test_motion.c` instead.
