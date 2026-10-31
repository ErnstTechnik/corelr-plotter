#!/bin/bash
#
# mutation_ci.sh
# Part of the CoreLR wall-plotter firmware project.
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
case "$1" in
  -h|--help) cat <<'USAGE'
usage: mutation_ci.sh [-h] [-l] [-j N] [-o DIR] [GROUP ...]

Runs the mutant batches the way the CI's `mutation` job does, and judges each batch by the UNION of its runs (tools/check_mutants_union.py): a batch is run once per
test, a mutant has to be killed by at least one of them. Each run's output (stdout and stderr) is kept in DIR/<group>__<run>.txt -- the CI uploads that folder.
All the batches of tests/mutation/mutants/ are wired (tests/tools/test_mutant_batches.py checks that each of them can load and run).

  -l      list the groups and the runs of each, then stop
  -j N    run N mutate.py processes at once (default 1; the CI uses the number of cores). The judgement stays in the order of the table
  -o DIR  where the outputs go (default ./mutation-out, git-ignored)
  GROUP   one or more of the names -l prints (default: all of them)

A group FAILS when a mutant is killed by no run (a gap), when an output is empty or unfinished (a batch that could not load, a run that died), or when the runs do not
list the same mutants. A mutant named EQUIVALENT... is accepted when nothing kills it, and one named REFUSED... when the build rejects it in every run.
Every group is run even if an earlier one failed. Slow: all groups take about 25 minutes on 4 cores with -j 4 (about an hour with -j 1).
Exit status: the number of groups that failed (0 = all clean).
Example:  tools/mutation_ci.sh -j 4 -o /tmp/mut origin travel_zone
USAGE
  exit 0;;
esac

cd "$(dirname "$0")/.." || exit 2          # the repository root: the batches and mutate.py are found from there
B=tests/mutation/mutants

# group | batch | the runs, "label=mutate.py arguments" separated by ';'. The arguments are those of the batch's own header (Run: ...), one run per test or setting,
# because a mutant has to be killed by AT LEAST ONE of them: the polarity flags of the joystick, the arithmetic, the variant, Y up or down, the table or the computed curve.
GROUPS_TABLE=(
  "checksize|checksize.txt|check_size=--test cmake/check_size --script tests/cmake/test_check_size.sh"
  "flag_truth_table|flag_truth_table.txt|run=--test tools/flag_truth_table --script tests/tools/test_flag_truth_table.py"
  "zone_files|zone_files.txt|run=--test tools/zone_files --script tests/host/check_zone_files.sh"
  "tension_chart|tension_chart.txt|run=--test tools/tension_chart --script tests/tools/test_tension_chart.py"
  "doxygen|doxygen.txt|run=--test doxygen --script tests/tools/test_doxygen.py"
  "gcode_page|gcode_page.txt|run=--test gcode_page --script tests/tools/test_gcode_page.py"
  "req_refs|req_refs.txt|run=--test req_refs --script tests/tools/test_req_refs.py"
  "check_version|check_version.txt|run=--test check_version --script tests/tools/test_check_version.py"
  "variant_modules|variant_modules.txt|run=--test variant_modules --script tests/tools/test_variant_modules.py"
  "image_symbols|image_symbols.txt|run=--test image_symbols --script tests/tools/test_image_symbols.py"
  "travel_zone|travel_zone.txt|test_travel=--test test_travel;test_calzone=--test test_calzone;test_ui=--test test_ui;test_travel_ydown=--test test_travel -D CORELR_Y_UP=0"
  "union_checker|union_checker.txt|run=--test union_checker --script tests/tools/test_check_mutants_union.py"
  "batch_checker|batch_checker.txt|run=--test batch_checker --script tests/tools/test_mutant_batch_checker.py"
  "origin|origin.txt|test_origin=--test test_origin;test_calzone=--test test_calzone;test_ui=--test test_ui;test_parser=--test test_parser;test_origin_ydown=--test test_origin -D CORELR_Y_UP=0"
  "adjustment|adjustment.txt|ui=--test test_ui --timeout 120"
  "calibration48|calibration48.txt|position=--test test_position -D CORELR_MATH=CORELR_MATH_48"
  "ctrlx_g0|ctrlx_g0.txt|link=--test test_seriallink"
  "display_cap|display_cap.txt|ui64=--test test_ui --timeout 120 -D CORELR_MATH=CORELR_MATH_64"
  "display_format|display_format.txt|ui=--test test_ui --timeout 120"
  "ergonomics|ergonomics.txt|ui=--test test_ui --timeout 120"
  "generator_hash|generator_hash.txt|arc=--test test_arc --hash;motion=--test test_motion --hash"
  "home|home.txt|ui=--test test_ui --timeout 120"
  "hypot48|hypot48.txt|arith=--test test_arith -D CORELR_MATH=CORELR_MATH_48;position=--test test_position -D CORELR_MATH=CORELR_MATH_48;trig=--test test_trig -D CORELR_MATH=CORELR_MATH_48"
  "jogcancel_uart|jogcancel_uart.txt|uart=--test test_uart --timeout 120"
  "jogcurve|jogcurve.txt|computed=--test test_jogcurve;table=--test test_jogcurve -D JOGCURVE_USE_TABLE=1"
  "joglink|joglink.txt|link=--test test_seriallink --timeout 120"
  "jogparser|jogparser.txt|parser=--test test_parser --timeout 120"
  "jogshape|jogshape.txt|jogcurve=--test test_jogcurve --timeout 120"
  "joystick|joystick.txt|x0y0=--test test_ui -D LCDJOYSTICKUI_X_INVERT=0 -D LCDJOYSTICKUI_Y_INVERT=0;x0y1=--test test_ui -D LCDJOYSTICKUI_X_INVERT=0 -D LCDJOYSTICKUI_Y_INVERT=1;x1y0=--test test_ui -D LCDJOYSTICKUI_X_INVERT=1 -D LCDJOYSTICKUI_Y_INVERT=0;x1y1=--test test_ui -D LCDJOYSTICKUI_X_INVERT=1 -D LCDJOYSTICKUI_Y_INVERT=1"
  "lcdi2c_cgram|lcdi2c_cgram.txt|lcdi2c=--test test_lcdi2c"
  "machine_frame|machine_frame.txt|link=--test test_seriallink"
  "parser_errors_end|parser_errors_end.txt|parser=--test test_parser"
  "parser_reach|parser_reach.txt|parser=--test test_parser"
  "penchar|penchar.txt|ui=--test test_ui"
  "posring|posring.txt|position=--test test_position --timeout 200"
  "sd_browser_ui|sd_browser_ui.txt|ui_sd=--test test_ui -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD"
  "sdreader|sdreader.txt|sdreader=--test test_sdreader"
  "seriallink|seriallink.txt|link=--test test_seriallink"
  "ternary|ternary.txt|ui=--test test_ui;ui_sd=--test test_ui -D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SD"
  "uart|uart.txt|uart=--test test_uart"
  "wiring|wiring.txt|run=--test tools/wiring --script tests/tools/test_wiring.py"
  "y_flip|y_flip.txt|arith_up=--test test_arith -D CORELR_Y_UP=1;position_up=--test test_position -D CORELR_Y_UP=1;ui_up=--test test_ui -D CORELR_Y_UP=1;arith_down=--test test_arith -D CORELR_Y_UP=0;position_down=--test test_position -D CORELR_Y_UP=0;ui_down=--test test_ui -D CORELR_Y_UP=0;origin_up=--test test_origin -D CORELR_Y_UP=1;origin_down=--test test_origin -D CORELR_Y_UP=0"
  "zword|zword.txt|parser=--test test_parser --timeout 120;parser_noz=--test test_parser --timeout 120 -D CORELR_GCODE_Z=0"
)

OUT=mutation-out; J=1; WANT=()
while [ $# -gt 0 ]; do
  case "$1" in
    -l) for g in "${GROUPS_TABLE[@]}"; do IFS='|' read -r name batch runs <<<"$g"; printf '%-18s %-24s %s\n' "$name" "$batch" "$(tr ';' '\n' <<<"$runs" | cut -d= -f1 | tr '\n' ' ')"; done; exit 0;;
    -o) [ -n "$2" ] || { echo "mutation_ci.sh: -o needs a folder" >&2; exit 2; }; OUT=$2; shift;;
    -j) case "$2" in ''|*[!0-9]*|0) echo "mutation_ci.sh: -j needs a number of processes, at least 1" >&2; exit 2;; esac; J=$2; shift;;
    -*) echo "mutation_ci.sh: unknown option '$1' (try -h)" >&2; exit 2;;
    *)  WANT+=("$1");;
  esac
  shift
done
for w in "${WANT[@]}"; do
  ok=0; for g in "${GROUPS_TABLE[@]}"; do [ "${g%%|*}" = "$w" ] && ok=1; done
  [ $ok = 1 ] || { echo "mutation_ci.sh: no group '$w' (try -l)" >&2; exit 2; }
done
mkdir -p "$OUT" || exit 2

selected() { [ ${#WANT[@]} -eq 0 ] && return 0; local w; for w in "${WANT[@]}"; do [ "$w" = "$1" ] && return 0; done; return 1; }

# phase 1: the runs to do (and the stale outputs of an earlier call removed: they must never be judged)
R_BATCH=(); R_ARGS=(); R_FILE=(); R_NAME=()
for g in "${GROUPS_TABLE[@]}"; do
  IFS='|' read -r name batch runs <<<"$g"
  selected "$name" || continue
  rm -f "$OUT/${name}__"*.txt
  IFS=';' read -ra RUNS <<<"$runs"
  for r in "${RUNS[@]}"; do
    R_BATCH+=("$batch"); R_ARGS+=("${r#*=}"); R_FILE+=("$OUT/${name}__${r%%=*}.txt"); R_NAME+=("${name}/${r%%=*}")
  done
done

# phase 2: run them, J at a time. mutate.py's exit status counts the mutants this ONE test missed: normal; the union decides
echo "=== ${#R_FILE[@]} run(s), $J at a time"
for i in "${!R_FILE[@]}"; do
  while [ "$(jobs -rp | wc -l)" -ge "$J" ]; do wait -n; done
  echo "  start ${R_NAME[$i]}"
  # shellcheck disable=SC2086   # the arguments are meant to split into words
  ( python3 tools/mutate.py ${R_ARGS[$i]} --batch "$B/${R_BATCH[$i]}" > "${R_FILE[$i]}" 2>&1 || true ) &
done
wait

# phase 3: judge each group by the union of its runs, in the order of the table
failed=0; summary=()
for g in "${GROUPS_TABLE[@]}"; do
  IFS='|' read -r name batch runs <<<"$g"
  selected "$name" || continue
  echo "=== $name ($batch)"
  files=(); IFS=';' read -ra RUNS <<<"$runs"
  for r in "${RUNS[@]}"; do files+=("$OUT/${name}__${r%%=*}.txt"); done
  if python3 tools/check_mutants_union.py "${files[@]}"; then summary+=("ok    $name"); else summary+=("FAIL  $name (exit $?)"); failed=$((failed + 1)); fi
done
echo; echo "=== summary"; printf '%s\n' "${summary[@]}"
exit $failed
