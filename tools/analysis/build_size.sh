#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# build_size.sh -- build the firmware with the project's CMake build and report flash and RAM against the Nano's limits.
usage() { cat <<'USAGE'
usage: build_size.sh [-h|--help] [-v serial|sd] [-b BUILD_DIR] [-s ROOT] [-D NAME[=VALUE]]... [-F FLAG]... [-L FLAG]... [-C] [-S] [-E] [-w]

Configures (once) and builds ONE firmware variant of the repository with CMake + Ninja and prints flash, RAM and what is left, against the Nano's
30720 bytes of flash (NOT the Uno's 32256) and 2048 bytes of RAM. The ELF stays in BUILD_DIR/firmware/ for fn_sizes.py, line_sizes.py and
stack_depth.py. Needs avr-gcc (cmake/avr-gcc.cmake says where it is looked for; AVR_TOOLCHAIN_DIR overrides).

  -v VARIANT     serial (default) or sd. A -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD/_SERIAL selects it too.
  -b BUILD_DIR   where the build goes and stays (default: $TMPDIR/corelr_build, or /tmp/corelr_build).
  -s ROOT        the repository root to build (default: two levels above this script). Use it for a copy or a git export.
  -D NAME[=V]    a define for every file of the build, repeatable. -D CORELR_MATH=CORELR_MATH_64 selects the 64-bit arithmetic; any other
                 NAME[=V] goes to CORELR_EXTRA_DEFS (e.g. -D CORELR_DEBUG_MISS_COUNT=1).
  -F FLAG        one more compiler flag for every file, repeatable (experiments: tools/analysis/flag_sweep.sh), e.g. -F -mrelax
  -L FLAG        one more linker flag, repeatable, e.g. -L -Wl,--relax
  -S             do NOT apply the size flags of cmake/size_flags.txt (CORELR_SIZE_FLAGS=OFF): the baseline of an evaluation (flag_truth_table.py)
  -E             also apply -fshort-enums (CORELR_SHORT_ENUMS=ON)
  -C             do NOT compile with -mcall-prologues (the project's setting): to measure what it saves.
  -w             also list the warnings (the project's own files are built with -Wall -Wextra).
  -h, --help     this text

Exit status: 0 built and fits, 1 built but too big for the Nano, 2 the build failed or the options were wrong.
Examples:
  tools/analysis/build_size.sh                                   the serial variant
  tools/analysis/build_size.sh -v sd                             the SD variant
  tools/analysis/build_size.sh -v sd -D CORELR_MATH=CORELR_MATH_64     what the 64-bit arithmetic costs
  tools/analysis/build_size.sh -b /tmp/before -s /tmp/head       a build of another tree, for fn_sizes.py to compare
USAGE
}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/corelr_env.sh"   # local.conf (AVR_BIN...) reaches cmake through the environment too
ROOT="$HERE/../.."; BUILD="${TMPDIR:-/tmp}/corelr_build"; VAR=""; CFG=(); EXTRA=(); XF=(); XL=(); WARN=0; CALLPRO=ON; SIZEF=ON; ENUMS=OFF
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0;;
    -v) VAR="$2"; shift 2;;
    -b) BUILD="$2"; shift 2;;
    -s) ROOT="$2"; shift 2;;
    -D) case "$2" in
          CORELR_MATH=CORELR_MATH_*) CFG+=("-DCORELR_MATH=${2#CORELR_MATH=CORELR_MATH_}");;
          CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD) VAR=sd;;
          CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL) VAR=serial;;
          CORELR_JOB_SOURCE=*) echo "build_size.sh: unknown job source '$2'" >&2; exit 2;;
          *) EXTRA+=("$2");;
        esac; shift 2;;
    -F) XF+=("$2"); shift 2;;
    -L) XL+=("$2"); shift 2;;
    -w) WARN=1; shift;;
    -C) CALLPRO=OFF; shift;;
    -S) SIZEF=OFF; shift;;
    -E) ENUMS=ON; shift;;
    *) echo "build_size.sh: unknown option '$1' (try -h)" >&2; exit 2;;
  esac
done
VAR="${VAR:-serial}"; case "$VAR" in serial|sd) ;; *) echo "build_size.sh: variant must be serial or sd" >&2; exit 2;; esac
[ -f "$ROOT/CMakeLists.txt" ] || { echo "build_size.sh: $ROOT/CMakeLists.txt not found" >&2; exit 2; }
command -v cmake >/dev/null && command -v ninja >/dev/null || { echo "build_size.sh: cmake and ninja are needed" >&2; exit 2; }
EXTRA_LIST=$(IFS=';'; echo "${EXTRA[*]}"); XF_LIST=$(IFS=';'; echo "${XF[*]}"); XL_LIST=$(IFS=';'; echo "${XL[*]}")
OUT=$(cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/avr-gcc.cmake" -DCORELR_CALL_PROLOGUES=$CALLPRO -DCORELR_SIZE_FLAGS=$SIZEF -DCORELR_SHORT_ENUMS=$ENUMS \
        -DCORELR_EXTRA_DEFS="$EXTRA_LIST" -DCORELR_EXTRA_COMPILE_FLAGS="$XF_LIST" -DCORELR_EXTRA_LINK_FLAGS="$XL_LIST" "${CFG[@]}" 2>&1 && cmake --build "$BUILD" --target corelr_$VAR 2>&1)
RC=$?
LINE=$(echo "$OUT" | sed -n "s/^-- corelr_$VAR: //p" | tail -1)
if [ $RC -ne 0 ] && [ -n "$LINE" ]; then : ; fi
if [ -z "$LINE" ] && [ $RC -ne 0 ]; then echo "$OUT" | grep -E "error|Error|undefined|CMake|TOO BIG" | head -20; echo "build FAILED" >&2; exit 2; fi
[ $WARN -eq 1 ] && echo "$OUT" | grep -E "warning" | head -30
FL=$(echo "$LINE" | sed -n 's/^flash \([0-9]*\) .*/\1/p'); RAM=$(echo "$LINE" | sed -n 's/.*RAM \([0-9]*\) .*/\1/p')
echo "variant : $VAR   math ${CFG[*]:-(default)}   defines ${EXTRA[*]:-(none)}   flags ${XF[*]:-(none)} ${XL[*]}$([ $CALLPRO = ON ] && echo '   -mcall-prologues')"
echo "flash   : $FL / 30720 bytes  ($(( 30720 - FL )) free)"
echo "RAM     : $RAM / 2048 bytes  ($(( 2048 - RAM )) left for stack)"
echo "ELF     : $BUILD/firmware/corelr_$VAR.elf"
echo "HEX     : $BUILD/corelr_$VAR.hex"
[ "$FL" -le 30720 ] || { echo "TOO BIG for the Nano by $(( FL - 30720 )) bytes"; exit 1; }
