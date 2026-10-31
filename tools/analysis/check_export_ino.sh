#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# check_export_ino.sh -- does the sketch exported for the Arduino IDE (target export_ino) build to the SAME image as the CMake build?
usage() { cat <<'USAGE'
usage: check_export_ino.sh [-h|--help]

For each variant (serial, sd): exports the flat sketch (cmake/ExportIno.cmake), builds it with the real arduino-builder with exactly the flags the exported
platform.local.txt gives the IDE, and compares the .hex with the CMake build's, byte for byte. Then builds the SD export WITHOUT those flags and checks that it
stops at the #error of grbl_corelr.ino rather than at an anonymous "sketch too big". (REQ-CoreLR-D-Build-0010)
Needs the Arduino IDE 1.8.x for the reference builder (ide_build.sh -h). Exit status: the number of failed checks (2 if a build could not run).
Example: tools/analysis/check_export_ino.sh
USAGE
}
case "$1" in -h|--help) usage; exit 0;; esac
HERE=$(cd "$(dirname "$0")" && pwd); ROOT="$HERE/../.."; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; bad=0
for v in serial sd; do
  V=$(echo $v | tr a-z A-Z)
  cmake -DROOT="$ROOT" -DOUT="$T/export_$v" -DVARIANT=$v -P "$ROOT/cmake/ExportIno.cmake" >/dev/null || { echo "$v: the export failed"; exit 2; }
  "$HERE/build_size.sh" -v $v -b "$T/cmake_$v" >/dev/null 2>&1 || { echo "$v: the CMake build failed"; exit 2; }
  "$HERE/ide_build.sh" -k "$T/export_$v/grbl_corelr" -p "$T/export_$v/platform.local.txt" -b "$T/ide_$v" >/dev/null 2>&1 || { echo "$v: the IDE build of the export failed"; exit 2; }
  if cmp -s "$T/cmake_$v/corelr_$v.hex" "$T/ide_$v/grbl_corelr.ino.hex"; then echo "$v: the exported sketch builds to the IDENTICAL image"
  else echo "$v: DIFFERENT from the CMake build"; bad=$((bad+1)); fi
done
out=$("$HERE/ide_build.sh" -k "$T/export_sd/grbl_corelr" -C -b "$T/ide_noflag" 2>&1)
if echo "$out" | grep -q "needs -mcall-prologues"; then echo "sd without -mcall-prologues: stops at the #error that says why"
else echo "sd without -mcall-prologues: did NOT stop at the #error"; echo "$out" | tail -3; bad=$((bad+1)); fi
exit $bad
