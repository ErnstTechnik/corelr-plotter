#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# run_test.sh -- build and run ONE host test of the real firmware sources.
usage() { cat <<'USAGE'
usage: run_test.sh [-h|--help] [-q] [-D NAME[=VALUE]]... TEST

TEST is one of: test_motion test_position test_parser test_travel test_origin test_calzone test_trig test_arc test_arith test_stepper_isr test_pen
                test_lcdi2c test_ui test_uart test_textfmt test_seriallink test_jogcurve test_chain test_sdreader     (what each proves: see README.md)

  -D NAME[=V]   a define for the build, repeatable, e.g. -D CORELR_MATH=CORELR_MATH_64  -D CORELR_DEBUG_MISS_COUNT=1
  -q            print only the last line of the test's output (its verdict)
  -h, --help    this text

The test is compiled against ../../firmware (src, config) and ../../third_party relative to THIS script, so run it from a copy of the tree
(tools/mutate.py and tools/bench/stream_hash.sh do exactly that) to test a variant of the firmware. The binary goes to out/.
Exit status: the test's own (0 = all checks passed), or 2 if it did not build.
Examples:
  tests/host/run_test.sh test_arc
  tests/host/run_test.sh -q -D CORELR_MATH=CORELR_MATH_64 test_arith
USAGE
}
cd "$(dirname "$0")" || exit 2; mkdir -p out
D=(); Q=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0;;
    -q) Q=1; shift;;
    -D) D+=("-D$2"); shift 2;;
    -*) echo "run_test.sh: unknown option '$1' (try -h)" >&2; exit 2;;
    *) break;;
  esac
done
T="$1"; [ -n "$T" ] || { usage >&2; exit 2; }
G=../../firmware/src; I=(-I$G -I../../firmware/config -I../../third_party); CF=(-m32 -fpack-struct=1 -O1 -w "${D[@]}" -Ishim "${I[@]}" ${HOST_CFLAGS:-})
case "$T" in
  test_motion)      CC=(gcc "${CF[@]}" test_motion.c $G/Motion.c $G/Trig.c -lm);;
  test_position)    CC=(gcc "${CF[@]}" -DMOTION_MARGIN_SIDE_UM=-4000000L -DMOTION_MARGIN_TOP_UM=-4000000L test_position.c $G/Calibration.c $G/Motion.c $G/Trig.c -lm);;
  test_parser)      CC=(gcc "${CF[@]}" -DMOTION_MARGIN_SIDE_UM=-4000000L -DMOTION_MARGIN_TOP_UM=-4000000L test_parser.c $G/GCodeParser.c $G/Motion.c $G/Trig.c -lm);;   # the travel zone widened: this one is about lines, modal state and the reach
  test_calzone)     CC=(gcc "${CF[@]}" test_calzone.c $G/Calibration.c $G/Motion.c $G/Trig.c -lm);;
  test_origin)      CC=(gcc "${CF[@]}" -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL test_origin.c $G/GCodeParser.c $G/Motion.c $G/Trig.c -lm);;
  test_travel)      CC=(gcc "${CF[@]}" -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL test_travel.c $G/GCodeParser.c $G/Motion.c $G/Trig.c -lm);;
  test_trig)        CC=(gcc "${CF[@]}" test_trig.c $G/Trig.c -lm);;
  test_sdreader)    CC=(g++ -m32 -O1 -w -Ishim_sd "${D[@]}" -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD -Ishim "${I[@]}" test_sdreader.cpp $G/SDReader.cpp);;
  test_chain)       CC=(gcc "${CF[@]}" -Wno-unused-function -DMOTION_MARGIN_SIDE_UM=-4000000L -DMOTION_MARGIN_TOP_UM=-4000000L test_chain.c $G/Trig.c -lm);;
  test_arc)         CC=(gcc "${CF[@]}" -Wno-unused-function -DMOTION_MARGIN_SIDE_UM=-4000000L -DMOTION_MARGIN_TOP_UM=-4000000L test_arc.c $G/Trig.c -lm);;   # the travel zone out of the way: this test is about the arcs

  test_arith)       CC=(gcc "${CF[@]}" test_arith.c $G/Trig.c -lm);;
  test_stepper_isr) CC=(gcc "${CF[@]}" -Wno-unused-function test_stepper_isr.c);;
  test_pen)         CC=(gcc "${CF[@]}" -Wno-unused-function test_pen.c);;
  test_textfmt)     CC=(gcc "${CF[@]}" test_textfmt.c $G/TextFmt.c);;
  test_jogcurve)    CC=(gcc "${CF[@]}" -Wno-unused-function test_jogcurve.c $G/Motion.c $G/Trig.c -lm);;
  test_uart)        CC=(gcc "${CF[@]}" -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL -Wno-unused-function test_uart.c);;
  test_seriallink)  CC=(gcc "${CF[@]}" -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL -Wno-unused-function test_seriallink.c $G/SerialLink.c $G/Uart.c $G/GCodeParser.c $G/Motion.c $G/Trig.c $G/TextFmt.c -lm);;
  test_lcdi2c)      CC=(g++ -m32 -O1 -w "${D[@]}" -Ishim_lcd "${I[@]}" test_lcdi2c.cpp);;
  test_ui)          CC=(g++ -m32 -O1 -w "${D[@]}" -Ishim_lcd -Ishim_ui "${I[@]}" test_ui.cpp $G/TextFmt.c $G/JogCurve.c);;
  *) echo "run_test.sh: no test named '$T' (try -h)" >&2; exit 2;;
esac
"${CC[@]}" -o "out/$T" 2> "out/$T.build.log" || { echo "BUILD FAILED: $T"; head -20 "out/$T.build.log"; exit 2; }
if [ $Q -eq 1 ]; then out=$(./out/$T 2>&1); rc=$?; echo "$out" | tail -1; exit $rc; fi
./out/$T; exit $?
