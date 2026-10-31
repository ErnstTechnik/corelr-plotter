#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# gcode_check.sh -- what would the machine do with these GCode files? Runs them through the firmware's REAL parser, planner and generator (on the host).
usage() { cat <<'USAGE'
usage: gcode_check.sh [-h|--help] [--serial] [-D NAME[=VALUE]]... [--limit MM] [--geometry D,OX,OY|D,L] [--svg DIR] FILE.gc...

Builds tests/host/gcode_check.c against the firmware sources (the real GCodeParser, Motion and Trig; Stepper, Calibration and PenControl are
stubs) and runs every FILE through it. For each file it reports:
  - every line the parser refuses, with grbl's error number (the same lines would halt the machine);
  - the bounding box of everything the machine would traverse and of what it would draw (pen down), the ink length, the pen-up travel -- measured on
    the waypoints the generator really produces, not on the file's numbers;
  - whether the file STARTS with `M5` then `G0 X0 Y0` (`G90 G0 X0 Y0` is accepted: pen up, back to the origin), and ENDS pen up at the origin;
  - lines over 47 characters (the SD variant's line buffer, before comment stripping) or over 80 significant characters (the serial link's).
Exit status: the number of files that fail (0 = all fine).

  --limit MM   half-side of the square the drawing must stay in (default 100: a 20 x 20 cm square centred on the origin)
  --geometry D,OX,OY | D,L   the machine's geometry in mm (D,L: the motor distance and the calibration's reference cable length, the origin being where calibration leaves it), which decides the TRAVEL ZONE (REQ-CoreLR-F-Motion-0008): D the distance between the motors, OX the distance from the left
               motor's vertical to the drawing origin, OY how far BELOW the line joining the motors the origin is. Default 1000,500,489.898. A line outside the zone is refused (error:33).
  --serial     the serial variant's job rules: a program end (M2/M30) does not stop the check, the next line starts a new job; G92 / G10 L20 P1 move the origin (the zone follows).
               Without it the check is the SD variant's: the file ends at the first program end, and an origin command is refused (error:33).
  --svg DIR    also write DIR/NAME.gc.svg, a picture of the path (black: drawn, red dashes: pen-up travel; Y up as on the wall)
  -D NAME[=V]  a define for the firmware build, repeatable (e.g. -D CORELR_MATH=CORELR_MATH_64, -D CORELR_Y_UP=0)
  -h, --help   this text

Examples:   tools/bench/gcode_check.sh tests/bench/gcode/*.gc
            tools/bench/gcode_check.sh --svg /tmp/preview tests/bench/gcode/04_TestPattern.gc
USAGE
}
case "$1" in -h|--help) usage; exit 0;; esac
HERE=$(cd "$(dirname "$0")" && pwd); REPO="$HERE/../.."; FW="$REPO/firmware/src"; HT="$REPO/tests/host"; OUT="$HT/out"; mkdir -p "$OUT"
DEFS=(); ARGS=(); while [ $# -gt 0 ]; do case "$1" in --serial) ARGS+=("$1"); shift;; -D) DEFS+=(-D "$2"); shift 2;; --limit|--svg|--geometry) ARGS+=("$1" "$2"); shift 2;; -*) echo "gcode_check.sh: unknown option '$1' (try -h)" >&2; exit 2;; *) ARGS+=("$1"); shift;; esac; done
[ ${#ARGS[@]} -gt 0 ] || { usage >&2; exit 2; }
gcc -m32 -fpack-struct=1 -O1 -w "${DEFS[@]}" -I"$HT/shim" -I"$FW" -I"$REPO/firmware/config" "$HT/gcode_check.c" "$FW/GCodeParser.c" "$FW/Motion.c" "$FW/Trig.c" -lm -o "$OUT/gcode_check" 2> "$OUT/gcode_check.build.log" \
  || { echo "BUILD FAILED"; head -20 "$OUT/gcode_check.build.log"; exit 2; }
exec "$OUT/gcode_check" "${ARGS[@]}"
