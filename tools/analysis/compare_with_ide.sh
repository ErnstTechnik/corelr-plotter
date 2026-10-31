#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# compare_with_ide.sh -- is the CMake build the SAME firmware as the Arduino IDE's? Compare the .hex files byte for byte.
usage() { cat <<'USAGE'
usage: compare_with_ide.sh [-h|--help] [-D NAME[=VALUE]]...

Builds both variants (serial and sd) twice: with the project's CMake build and with the real arduino-builder (tools/analysis/ide_build.sh, the
reference), under the same settings, then compares the two .hex files with cmp. Identical = the CMake build, its flags, the vendored core and the link
order are exactly what the IDE did, so every size, stack and speed figure measured before the move still holds. This is the proof the CMake step
was held to (REQ-CoreLR-D-Build-0005); run it again after touching cmake/, third_party/ or the compiler.

  -D NAME[=V]   a define for both builds, as for build_size.sh (e.g. -D CORELR_MATH=CORELR_MATH_64)
  -h, --help    this text

Needs the Arduino IDE 1.8.x (snap) for the reference build, see ide_build.sh -h. Exit status: the number of variants whose images differ (2 if a build failed).
Example: tools/analysis/compare_with_ide.sh
USAGE
}
case "$1" in -h|--help) usage; exit 0;; esac
HERE=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; bad=0
for v in serial sd; do
  V=$(echo $v | tr a-z A-Z)
  "$HERE/build_size.sh" -v $v -b "$T/cmake" "$@" >/dev/null 2>&1 || { echo "$v: the CMake build failed"; exit 2; }
  "$HERE/ide_build.sh" -b "$T/ide_$v" -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_$V "$@" >/dev/null 2>&1 || { echo "$v: the IDE build failed"; exit 2; }
  if cmp -s "$T/cmake/corelr_$v.hex" "$T/ide_$v/grbl_corelr.ino.hex"; then
    echo "$v: IDENTICAL   ($(sha256sum < "$T/cmake/corelr_$v.hex" | cut -c1-16)…, $(grep -c . "$T/cmake/corelr_$v.hex") hex records)"
  else echo "$v: DIFFERENT"; bad=$((bad+1)); fi
done
exit $bad
