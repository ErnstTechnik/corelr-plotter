#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# run_all.sh -- build and run every host test, and print one verdict line per test.
usage() { cat <<'USAGE'
usage: run_all.sh [-h|--help] [CORELR_MATH_48|CORELR_MATH_64]

Runs the host tests (run_test.sh for each) and prints the last line of each. Exit status = the number of tests that
failed or did not build.

  CORELR_MATH_xx   the arithmetic to test (REQ-CoreLR-D-Motion-0024); default: the one SwConfig.h selects.
                   BOTH must pass: run it twice.
  EXTRA_DEFS       environment: more defines, e.g. EXTRA_DEFS="-DCORELR_DEBUG_MISS_COUNT=1"
  -h, --help       this text

Examples:
  tests/host/run_all.sh CORELR_MATH_48 && tests/host/run_all.sh CORELR_MATH_64
  EXTRA_DEFS=-DCORELR_DEBUG_MISS_COUNT=1 tests/host/run_all.sh CORELR_MATH_48
USAGE
}
case "$1" in -h|--help) usage; exit 0;; -*) echo "run_all.sh: unknown option '$1' (try -h)" >&2; exit 2;; esac
cd "$(dirname "$0")" || exit 2
DEFS=(); [ -n "$1" ] && DEFS+=(-D "CORELR_MATH=$1")
for d in $EXTRA_DEFS; do DEFS+=(-D "${d#-D}"); done
echo "arithmetic: ${1:-as set in SwConfig.h}   extra: ${EXTRA_DEFS:-none}"
bad=0
for t in test_motion test_position test_parser test_travel test_origin test_calzone test_trig test_arc test_arith test_stepper_isr test_pen test_lcdi2c test_ui test_uart test_textfmt test_seriallink test_jogcurve test_chain test_sdreader; do
  line=$(./run_test.sh -q "${DEFS[@]}" $t 2>&1); rc=$?
  printf "%-18s %s\n" "$t" "$( [ $rc -eq 0 ] && echo "$line" || echo "FAILED (exit $rc)")"
  [ $rc -eq 0 ] || bad=$((bad+1))
done
# The GCode files to print (tests/bench/gcode, written by tools/generators/gen_gcode.py) must be accepted by the real parser, inside the 200 x 200 mm
# square; and each deliberately bad sample must be refused -- or the checker proves nothing.
out=$(../../tools/bench/gcode_check.sh "${DEFS[@]}" ../../tests/bench/gcode/*.gc 2>&1); rc=$?
printf "%-18s %s\n" "gcode_files" "$( [ $rc -eq 0 ] && echo "all files accepted ($(ls ../../tests/bench/gcode/*.gc | wc -l))" || echo "FAILED")"
[ $rc -eq 0 ] || { echo "$out" | grep -v "^   [a-z]"; bad=$((bad+1)); }
for f in gcode_samples/bad_*.gc; do
  ../../tools/bench/gcode_check.sh "${DEFS[@]}" "$f" >/dev/null 2>&1 && { printf "%-18s %s\n" "gcode_checker" "did NOT refuse $f"; bad=$((bad+1)); }
done
../../tools/bench/gcode_check.sh "${DEFS[@]}" gcode_samples/good_minimal.gc >/dev/null 2>&1 || { printf "%-18s %s\n" "gcode_checker" "refused the good sample"; bad=$((bad+1)); }
# The GCode files to print are what their generator writes (they must not be edited by hand).
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
../../tools/generators/gen_gcode.py -o "$T/default" >/dev/null 2>&1 && diff -q -r "$T/default" ../../tests/bench/gcode --exclude=preview >/dev/null 2>&1 \
  && printf "%-18s %s\n" "gcode_generated" "files = generator" || { printf "%-18s %s\n" "gcode_generated" "FAILED (tests/bench/gcode is not gen_gcode.py's output)"; bad=$((bad+1)); }
# The files written for the maintainer'"'"'s machine (D 1065, L 800) are what the generator writes.
../../tools/generators/gen_gcode.py --limits 1065,800 --only-limits -o "$T/machine" >/dev/null 2>&1 && diff -q -r "$T/machine" ../../tests/bench/gcode_machine --exclude=README.md >/dev/null 2>&1 \
  && printf "%-18s %s\n" "gcode_machine" "files = generator" || { printf "%-18s %s\n" "gcode_machine" "FAILED (tests/bench/gcode_machine is not gen_gcode.py --limits 1065,800's output)"; bad=$((bad+1)); }
# The travel-zone files (gen_gcode.py --limits) for a sample geometry: the big drawing is accepted whole, and each refusal file is refused at exactly one line, with the number it announces.
./check_zone_files.sh "${DEFS[@]}" > "$T/zone.out" 2>&1; rc=$?; cat "$T/zone.out" | sed 's/^/  /' | grep -v "^  zone_.*ok$" | head -8
printf "%-18s %s\n" "zone_files" "$( [ $rc -eq 0 ] && echo "the big drawing accepted whole; 4 refusal files refused once, with their number" || echo "FAILED")"; [ $rc -eq 0 ] || bad=$((bad+1))
# The serial-variant files (gen_serial_tests.py): the accepted ones run whole, each refusal file is refused once with the number in its name.
./check_serial_files.sh "${DEFS[@]}" > "$T/serialfiles.out" 2>&1; rc=$?; cat "$T/serialfiles.out" | sed 's/^/  /' | grep -v "^  serial_.* ok$" | head -8
printf "%-18s %s\n" "serial_files" "$( [ $rc -eq 0 ] && echo "files = generator; accepted files whole, 22 refusal files refused once, with their number" || echo "FAILED")"; [ $rc -eq 0 ] || bad=$((bad+1))
# The sketch cannot run on the host, but the order in which it closes Stepper's gate can be checked: after the halt ramp is enqueued, never with Motion_Halt().
../../tools/analysis/check_halt_order.py && printf "%-18s %s\n" "halt_order" "gate closes after the ramp" || { printf "%-18s %s\n" "halt_order" "FAILED (see tools/analysis/check_halt_order.py -h)"; bad=$((bad+1)); }
# The jog curve's table must be what its generator writes.
../../tools/generators/gen_jogcurve.py --check && printf "%-18s %s\n" "jogcurve_table" "table = generator" || { printf "%-18s %s\n" "jogcurve_table" "FAILED (JogCurveTable.h is not tools/generators/gen_jogcurve.py's output)"; bad=$((bad+1)); }
# The geometry picture the specification refers to (docs/hardware/images/Geometry.svg): its figures must still be the firmware's, and the committed file the generator's output.
../../tools/generators/gen_geometry_svg.py --verify -o /tmp/corelr_geometry_check.svg >/dev/null 2>&1 && cmp -s /tmp/corelr_geometry_check.svg ../../docs/hardware/images/Geometry.svg \
  && printf "%-18s %s\n" "geometry_svg" "picture = generator = firmware" || { printf "%-18s %s\n" "geometry_svg" "FAILED (stale picture, or the firmware's geometry changed)"; bad=$((bad+1)); }
# The cable-tension picture the hardware doc shows (docs/hardware/CableTension.md): the committed file must be the tool's output.
../../tools/generators/gen_tension_chart.py -D 1000 --svg-step 25 --mass-g 15 --svg /tmp/corelr_tension_check.svg --quiet >/dev/null 2>&1 && cmp -s /tmp/corelr_tension_check.svg ../../docs/hardware/images/Tension.svg \
  && printf "%-18s %s\n" "tension_svg" "picture = generator" || { printf "%-18s %s\n" "tension_svg" "FAILED (stale picture: see docs/hardware/CableTension.md)"; bad=$((bad+1)); }
exit $bad
