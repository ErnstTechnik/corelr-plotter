#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# usage: build.sh [-DEV_H=n] [-DEV_MERGE_MODE=n] ...   -> out/test_evgen (host, runs it)
set -e
HERE=$(cd "$(dirname "$0")" && pwd); FW=${FW:-$HERE/../../../firmware}; mkdir -p "$HERE/out"
gcc -m32 -fpack-struct=1 -O1 -Wall -Wno-unused-function "$@" \
   -I"$HERE/../../analysis/cyclesim/host" -I"$FW/src" -I"$FW/config" -I"$HERE" \
   "$HERE/test_evgen.c" "$HERE/evgen.c" "$FW/src/Motion.c" -lm -o "$HERE/out/test_evgen"
"$HERE/out/test_evgen"
