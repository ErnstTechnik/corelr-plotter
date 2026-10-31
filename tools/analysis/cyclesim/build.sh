#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# usage: build.sh [-h|--help] <harness.c> <name> [extra compiler args...]   -> out/<name>.{elf,bin} (+ out/<name>_host)
if [ "$1" = "-h" ] || [ "$1" = "--help" ] || [ $# -lt 2 ]; then
cat <<'USAGE'
usage: build.sh [-h|--help] HARNESS.c NAME [EXTRA ARGS...]

Builds a harness (t_*.c) together with the REAL Motion.c for the simulated ATmega328P, with the Arduino flags
(-Os -flto): out/NAME.elf and out/NAME.bin (what the simulator runs and profile.py reads), and an x86 -m32 build
out/NAME_host of the same source for a stream-equality check. EXTRA ARGS go to both compilers: more sources (Trig.c is
needed by anything that calls Motion), and -D defines, e.g. -DCORELR_MATH=CORELR_MATH_64.

  FW        environment: the firmware directory (default ../../../firmware: src/ and config/ inside)
  AVR_BIN   environment: the avr-gcc bin directory (default: the Arduino snap's)

Examples:   ./build.sh t_hypot.c t_hypot_48 ../../../firmware/src/Trig.c -DCORELR_MATH=CORELR_MATH_48
            python3 run_hypot.py out/t_hypot_48.bin
USAGE
exit 0; fi
# usage: build.sh <harness.c> <name>     -> out/<name>.{elf,bin} (+ out/<name>_host)
# Compiles the REAL Motion.c with the Arduino flags (-Os -flto), plus a host
# (x86, -m32) build of the same source for the stream-equality check.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
FW=${FW:-$HERE/../../../firmware}
. "$HERE/../../lib/corelr_env.sh"; BIN=${AVR_BIN:-$(dirname "$(command -v avr-gcc)")}
H=$1; N=$2; shift 2
mkdir -p "$HERE/out"
$BIN/avr-gcc -mmcu=atmega328p -DF_CPU=16000000L -Os -std=gnu11 -ffunction-sections -fdata-sections \
   -flto -fno-fat-lto-objects -I$FW/src -I$FW/config "$@" "$HERE/$H" $FW/src/Motion.c -Wl,--gc-sections -o "$HERE/out/$N.elf"
$BIN/avr-objcopy -O binary -j .text -j .data "$HERE/out/$N.elf" "$HERE/out/$N.bin"
$BIN/avr-size "$HERE/out/$N.elf" | tail -1
gcc -m32 -O1 -I"$HERE/host" -I$FW/src -I$FW/config "$@" "$HERE/$H" $FW/src/Motion.c -o "$HERE/out/${N}_host" 2>/dev/null || true
