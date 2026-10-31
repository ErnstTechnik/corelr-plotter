#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# usage: build_avr.sh [cflags...]  -> out/t_evgen_cyc.{elf,bin}; then: python3 run_cyc.py
set -e
HERE=$(cd "$(dirname "$0")" && pwd); FW=$HERE/../../../firmware; mkdir -p "$HERE/out"
. "$HERE/../../lib/corelr_env.sh"; BIN=${AVR_BIN:-$(dirname "$(command -v avr-gcc)")}
$BIN/avr-gcc -mmcu=atmega328p -DF_CPU=16000000L -Os -std=gnu11 -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects \
   -I"$FW/src" -I"$FW/config" -I"$HERE" "$@" "$HERE/t_evgen_cyc.c" "$HERE/evgen.c" "$FW/src/Motion.c" -Wl,--gc-sections -o "$HERE/out/t_evgen_cyc.elf"
$BIN/avr-objcopy -O binary -j .text -j .data "$HERE/out/t_evgen_cyc.elf" "$HERE/out/t_evgen_cyc.bin"
$BIN/avr-size "$HERE/out/t_evgen_cyc.elf" | tail -1
