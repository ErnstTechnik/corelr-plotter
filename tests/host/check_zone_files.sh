#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# check_zone_files.sh -- do the travel-zone GCode files (tools/generators/gen_gcode.py --limits) do what they say, on the firmware's real parser?
usage() { cat <<'USAGE'
usage: check_zone_files.sh [-h|--help] [-D NAME=VALUE]...

Writes the zone files for a sample geometry (motors 1000 mm apart, the origin 500 mm from the left motor and 489.898 mm below the line joining them), then for the same machine
given as D,L (1000 mm and a 700 mm calibration cable: the origin is where calibration leaves it, 489.897), and runs them through tools/bench/gcode_check.sh with that geometry: 10_Limits_Frame.gc must be accepted whole; 11_Refuse_Left.gc, 12_Refuse_Right.gc, 13_Refuse_Top.gc must each be refused at exactly one
line with error:33, and 14_Refuse_ArcBulge.gc at exactly one with error:34 (the arc's own number). Prints one line per file; exit status: the number of files that did not behave.
  -D NAME[=V]  a define for the firmware build, passed to gcode_check.sh
Proved able to fail by tests/mutation/mutants/zone_files.txt.
USAGE
}
case "$1" in -h|--help) usage; exit 0;; esac
cd "$(dirname "$0")" || exit 2
case " $* " in *CORELR_Y_UP=0*) echo "zone files: skipped (they are written for Y up the wall, the default; with CORELR_Y_UP=0 the sign of every Y would flip)"; exit 0;; esac
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; bad=0
# the two spellings of a geometry: D,OX,OY and D,L (the origin where "Calibrate machine" leaves it: OX = D/2, OY = floor(sqrt(L^2 - (D/2)^2)) in um = 489.897 for 1000, 700)
../../tools/generators/gen_gcode.py --limits 1000,500,489.897 --only-limits -o "$T/ref" >/dev/null 2>&1 || { echo "gen_gcode.py --limits failed"; exit 2; }
../../tools/generators/gen_gcode.py --limits 1000,700 --only-limits -o "$T/dl" >/dev/null 2>&1 || { echo "gen_gcode.py --limits D,L failed"; exit 2; }
if diff -r "$T/ref" "$T/dl" >/dev/null 2>&1; then echo "zone_D_L_form ok"; else echo "zone_D_L_form FAILED (--limits 1000,700 does not write what --limits 1000,500,489.897 writes)"; bad=$((bad+1)); fi
# a calibration whose reference is less than the top margin below the line is one "Calibrate machine" refuses: no files for it
../../tools/generators/gen_gcode.py --limits 1000,501 --only-limits -o "$T/refused" >/dev/null 2>&1 && { echo "zone_D_L_refused FAILED (--limits 1000,501: the reference 31.6 mm below the line was accepted)"; bad=$((bad+1)); } || echo "zone_D_L_refused ok"
for L in 500 400; do
  m=$(../../tools/generators/gen_gcode.py --limits 1000,$L --only-limits -o "$T/refused" 2>&1) && { echo "zone_D_L_nocable[$L] FAILED (a cable of $L mm between motors 1000 apart reaches no point: accepted)"; bad=$((bad+1)); } || { echo "$m" | grep -q "cannot reach" && echo "zone_D_L_nocable[$L] ok" || { echo "zone_D_L_nocable[$L] FAILED (refused, but not for the reason: $m)"; bad=$((bad+1)); }; }
done
# the origin of D,L to the micrometre, as the firmware derives it (D 1000, L 700: 489.897): the top edge is 50 mm above it, 439.897, and 439.898 is one micrometre beyond
printf 'M5\nG90 G0 X0 Y0\nG0 Y439.897\nG0 Y439.898\n' > "$T/edge.gc"
out=$(../../tools/bench/gcode_check.sh "$@" --limit 700 --geometry 1000,700 "$T/edge.gc" 2>&1)
if [ "$(echo "$out" | grep -c "error:")" = "1" ] && echo "$out" | grep -q "edge.gc:4: error:33"; then echo "zone_D_L_edge_to_the_um ok"; else echo "zone_D_L_edge_to_the_um FAILED: $out" | head -3; bad=$((bad+1)); fi
../../tools/generators/gen_gcode.py --limits 1000,520 --only-limits -o "$T/ok520" >/dev/null 2>&1 && echo "zone_D_L_520 ok" || { echo "zone_D_L_520 FAILED (a 520 mm cable puts the reference 143 mm below the line: fine)"; bad=$((bad+1)); }
for G in 1000,500,489.898 1000,700; do
  D=$T/$G; ../../tools/generators/gen_gcode.py --limits $G --only-limits -o "$D" >/dev/null 2>&1 || { echo "gen_gcode.py --limits $G failed"; exit 2; }
  out=$(../../tools/bench/gcode_check.sh "$@" --limit 700 --geometry $G "$D/10_Limits_Frame.gc" 2>&1); rc=$?
  if [ $rc -eq 0 ]; then echo "zone_10_Limits_Frame[$G] ok"; else echo "zone_10_Limits_Frame[$G] FAILED"; echo "$out" | head -4; bad=$((bad+1)); fi
  for pair in 11_Refuse_Left:33 12_Refuse_Right:33 13_Refuse_Top:33 14_Refuse_ArcBulge:34; do
    f=${pair%%:*}; want=${pair##*:}
    out=$(../../tools/bench/gcode_check.sh "$@" --limit 700 --geometry $G "$D/$f.gc" 2>&1)
    n=$(echo "$out" | grep -c "error:"); first=$(echo "$out" | grep -o "error:[0-9]*" | head -1)
    if [ "$n" = "1" ] && [ "$first" = "error:$want" ]; then echo "zone_$f[$G] ok"; else echo "zone_$f[$G] FAILED ($n refusals, first '$first', expected one error:$want)"; bad=$((bad+1)); fi
  done
done
exit $bad
