#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# hypot_bound.sh -- build and run hypot_bound.c (the bound under CORELR_MATH_48's Motion_Hypot).
case "$1" in -h|--help) echo "usage: hypot_bound.sh [-h|--help] [-n CASES] [-s SEED]"; echo "Compiles tools/prototypes/hypot_bound.c and runs it; the options are hypot_bound's own (hypot_bound.sh -h shows them via the binary):"; echo; ;; esac
HERE=$(cd "$(dirname "$0")" && pwd); B="${TMPDIR:-/tmp}/hypot_bound"
gcc -O2 -o "$B" "$HERE/hypot_bound.c" -lm || exit 2
exec "$B" "$@"
