#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# ide_build.sh -- the REFERENCE build: the firmware compiled by the real arduino-builder (the binary the Arduino IDE runs), for the Nano.
# The project's own build is CMake (tools/analysis/ide_build.sh, cmake/); this one exists to prove that the two give the same image
# (tools/analysis/compare_with_ide.sh) and to measure what the IDE would. It reports flash and RAM against the Nano's limits. The build directory is kept, so fn_sizes.py / line_sizes.py /
# stack_depth.py can read the .elf afterwards.
usage() { cat <<'USAGE'
usage: ide_build.sh [-h|--help] [-b BUILD_DIR] [-s SRC_DIR] [-k SKETCH_DIR [-p PLATFORM_LOCAL_TXT]] [-S] [-E] [-D NAME[=VALUE]]... [-w] [-C]

Builds the firmware (firmware/ + third_party/, staged into a flat sketch folder for arduino-builder, without what cmake/variant_modules.txt gives to the other image) with fqbn arduino:avr:nano:cpu=atmega328 (30720 bytes of flash, NOT the Uno's 32256) and prints
flash, RAM and the free amounts.

  -b BUILD_DIR   where the build goes and stays (default: $TMPDIR/corelr_build, or /tmp/corelr_build).
                 The ELF is BUILD_DIR/grbl_corelr.ino.elf.
  -s ROOT        the repository root to build (default: two levels above this script). Use it to build a copy,
                 a git export, or an experiment without touching the tree.
  -D NAME[=V]    a preprocessor define for the whole build, repeatable: -D CORELR_MATH=CORELR_MATH_64
                 -D CORELR_DEBUG_MISS_COUNT=1   (the selectors in SwConfig.h are all #ifndef-guarded)
  -C             do NOT compile with -mcall-prologues (the project's build setting, as in cmake/Firmware.cmake); to measure what it saves.
  -k SKETCH_DIR  build this ready-made flat sketch folder (the folder holding grbl_corelr.ino, e.g. one exported by the export_ino target) instead of
                 staging the repository; the flags that platform.local.txt would give are passed here the same way (-C: without them)
  -p FILE        with -k: take the build settings from this platform.local.txt (the exported one) instead of the project's own list: what the IDE would do with that file
  -S             do NOT apply the size flags of cmake/size_flags.txt (the project applies those marked `on`, REQ-CoreLR-D-Build-0012)
  -E             also apply the `off` flag of that file, -fshort-enums (CORELR_SHORT_ENUMS in CMake)
  -w             also list the warnings that come from the project's own files.
  -h, --help     this text.

Environment (defaults are this machine's snap install of Arduino IDE 1.8.19):
  (in the environment or in local.conf, see local.conf.example)
  ARDUINO_ROOT   /snap/arduino/current        ARDUINO_PKGS   ~/snap/arduino/current/.arduino15/packages
  ARDUINO_LIBS   ~/snap/arduino/current/Arduino/libraries

Exit status: 0 built and fits, 1 built but too big for the Nano, 2 the build failed or the options were wrong.
Examples:
  tools/analysis/ide_build.sh                                   the default build
  tools/analysis/ide_build.sh -D CORELR_MATH=CORELR_MATH_64     what the 64-bit arithmetic costs
  tools/analysis/ide_build.sh -b /tmp/before -s /tmp/head   a build of another tree, for fn_sizes.py to compare
USAGE
}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../.."; KIT=""; PLT=""; BUILD="${TMPDIR:-/tmp}/corelr_build"; DEFS=(); WARN=0; CALLPRO=1; SIZEF=1; ENUMS=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0;;
    -b) BUILD="$2"; shift 2;;
    -s) SRC="$2"; shift 2;;
    -k) KIT="$2"; shift 2;;
    -p) PLT="$2"; shift 2;;
    -D) DEFS+=("-D$2"); shift 2;;
    -w) WARN=1; shift;;
    -C) CALLPRO=0; shift;;
    -S) SIZEF=0; shift;;
    -E) ENUMS=1; shift;;
    *) echo "ide_build.sh: unknown option '$1' (try -h)" >&2; exit 2;;
  esac
done
. "$HERE/../lib/corelr_env.sh"   # local.conf: ARDUINO_ROOT, ARDUINO_PKGS, ARDUINO_LIBS
ROOT="${ARDUINO_ROOT:-/snap/arduino/current}"
PKGS="${ARDUINO_PKGS:-$HOME/snap/arduino/current/.arduino15/packages}"
LIBS="${ARDUINO_LIBS:-$HOME/snap/arduino/current/Arduino/libraries}"
[ -x "$ROOT/arduino-builder" ] || { echo "ide_build.sh: $ROOT/arduino-builder not found (set ARDUINO_ROOT)" >&2; exit 2; }
[ -n "$KIT" ] || [ -f "$SRC/firmware/app/grbl_corelr.ino" ] || { echo "ide_build.sh: $SRC/firmware/app/grbl_corelr.ino not found" >&2; exit 2; }
# the variant: the -D CORELR_JOB_SOURCE given, else the default of SwConfig.h (a flag marked `sd` or `serial` in the list is for that variant only)
VARIANT=sd
case " ${DEFS[*]} " in
  *CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL*) VARIANT=serial;;
  *CORELR_JOB_SOURCE=*) VARIANT=sd;;
  *) grep -Eq '^#define CORELR_JOB_SOURCE +CORELR_JOB_SOURCE_SERIAL' "$SRC/firmware/config/SwConfig.h" 2>/dev/null && VARIANT=serial;;
esac
# What belongs to the other image is not staged (cmake/variant_modules.txt, REQ-CoreLR-D-Build-0014: the one list the CMake build and the IDE export read too; a tree
# that predates it, built with -s, has no such list and is staged whole).
OTHER=(); LIST="$SRC/cmake/variant_modules.txt"
if [ -f "$LIST" ]; then while IFS='|' read -r p v; do p=$(echo $p); v=$(echo $v); case "$p" in ''|'#'*) continue;; esac; [ "$v" != "$VARIANT" ] && OTHER+=("$p"); done < "$LIST"; fi
belongs_to_other() { local o; for o in "${OTHER[@]}"; do case "$1" in "$o"|"$o"/*) return 0;; esac; done; return 1; }
# arduino-builder wants ONE flat sketch folder: stage the split firmware (app, src, config) into it, sdfat under src/ so that
# the firmware's "sdfat/SdFat.h" resolves through -I<stage>/src. A stop-gap until the CMake build replaces this script.
# (the stage must NOT sit under a path that begins with BUILD: arduino-builder then treats the sketch as its own output and sees only the .ino)
if [ -n "$KIT" ]; then STAGE=$(cd "$KIT" && pwd); [ -f "$STAGE/grbl_corelr.ino" ] || { echo "ide_build.sh: $STAGE/grbl_corelr.ino not found" >&2; exit 2; }
else
STAGEROOT="$(dirname "$BUILD")/stage-$(basename "$BUILD")"; STAGE="$STAGEROOT/grbl_corelr"; rm -rf "$STAGEROOT"; mkdir -p "$STAGE/src"
for f in "$SRC"/firmware/app/* "$SRC"/firmware/src/* "$SRC"/firmware/config/*; do belongs_to_other "${f#$SRC/}" || cp "$f" "$STAGE/"; done
belongs_to_other third_party/sdfat || cp -r "$SRC/third_party/sdfat" "$STAGE/src/sdfat"
fi
mkdir -p "$BUILD" "$BUILD.cache"
# -mcall-prologues is the project's build setting (cmake/Firmware.cmake does the same); the sketch
# lives or dies by it on the SD variant. build.extra_flags reaches the sketch, the core and the libraries alike.
FLAGS=("${DEFS[@]}"); [ -z "$KIT" ] && FLAGS+=("-I$STAGE/src"); [ $CALLPRO -eq 1 ] && FLAGS+=(-mcall-prologues -DCORELR_CALL_PROLOGUES=1)
# the size flags, from the one list CMake reads too (the link flags go to the IDE's link command through compiler.c.elf.extra_flags)
SF="$HERE/../../cmake/size_flags.txt"; LINKF=()
if [ -f "$SF" ]; then
  while IFS='|' read -r n c l st; do
    case "$n" in ''|'#'*) continue;; esac
    if { { [ "$st" = on ] || [ "$st" = "$VARIANT" ]; } && [ $SIZEF -eq 1 ]; } || { [ "$n" = short_enums ] && [ $ENUMS -eq 1 ]; }; then FLAGS+=($c); LINKF+=($l); fi
  done < "$SF"
fi
PREFS=(); [ ${#FLAGS[@]} -gt 0 ] && PREFS=("-prefs=build.extra_flags=${FLAGS[*]}")
[ ${#LINKF[@]} -gt 0 ] && PREFS+=("-prefs=compiler.c.elf.extra_flags=${LINKF[*]}")
if [ -n "$PLT" ]; then   # the exported platform.local.txt, line by line, as the IDE reads it
  PREFS=(); while IFS= read -r l; do case "$l" in '#'*|'') ;; *=*) PREFS+=("-prefs=$l");; esac; done < "$PLT"
fi
OUT=$(cd "$STAGE" && "$ROOT/arduino-builder" -compile -logger=human -warnings=all \
  -hardware "$ROOT/hardware" -hardware "$PKGS" -tools "$ROOT/hardware/tools/avr" -tools "$ROOT/tools-builder" -tools "$PKGS" \
  -built-in-libraries "$ROOT/libraries" -libraries "$LIBS" -fqbn=arduino:avr:nano:cpu=atmega328 \
  "${PREFS[@]}" -build-path "$BUILD" -build-cache "$BUILD.cache" grbl_corelr.ino 2>&1)
RC=$?
FL=$(echo "$OUT" | sed -n 's/^Sketch uses \([0-9]*\) bytes.*/\1/p'); RAM=$(echo "$OUT" | sed -n 's/^Global variables use \([0-9]*\) bytes.*/\1/p')
if [ $RC -ne 0 ] || [ -z "$FL" ]; then echo "$OUT" | grep -E "error|Error|undefined|In function" | head -20; echo "build FAILED" >&2; exit 2; fi
[ $WARN -eq 1 ] && echo "$OUT" | grep -E "$STAGE/.*warning" | head -30
echo "defines : ${DEFS[*]:-(none)}$([ $CALLPRO -eq 1 ] && echo '  -mcall-prologues')$([ $SIZEF -eq 1 ] && echo '  +size flags')$([ $ENUMS -eq 1 ] && echo '  -fshort-enums')"
echo "flash   : $FL / 30720 bytes  ($(( 30720 - FL )) free)"
echo "RAM     : $RAM / 2048 bytes  ($(( 2048 - RAM )) left for stack)"
echo "ELF     : $BUILD/grbl_corelr.ino.elf"
echo "HEX     : $BUILD/grbl_corelr.ino.hex"
[ "$FL" -le 30720 ] || { echo "TOO BIG for the Nano by $(( FL - 30720 )) bytes"; exit 1; }
