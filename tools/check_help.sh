#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# check_help.sh -- the convention: EVERY tool in tools/ answers -h / --help with a usage text and exit status 0.
case "$1" in
  -h|--help) cat <<'USAGE'
usage: check_help.sh [-h|--help]

Runs every tool listed in tools/README.md with -h and checks that it prints something and exits 0. Run it after adding a tool
(and add the tool to the TOOLS list below). Exit status: the number of tools that fail.
USAGE
  exit 0;;
  "") ;;
  *) echo "check_help.sh: unknown option '$1' (try -h)" >&2; exit 2;;
esac
cd "$(dirname "$0")" || exit 2
TOOLS=(release_package.sh ../tests/tools/test_gcode_page.py ../tests/tools/test_check_mutants_union.py ../tests/tools/test_mutant_batch_checker.py ../tests/tools/test_mutant_batches.py check_mutants_union.py mutation_ci.sh ../tests/tools/test_flag_truth_table.py analysis/flag_truth_table.py ../tests/host/check_zone_files.sh ../tests/host/check_serial_files.sh generators/gen_serial_tests.py ../tests/cmake/test_check_size.sh ../tests/tools/test_tension_chart.py generators/gen_tension_chart.py ../tests/tools/test_wiring.py bench/serial_probe.py ../tests/tools/test_candle_spy.py bench/candle_spy.py analysis/check_halt_order.py analysis/check_image_symbols.py ../tests/tools/test_image_symbols.py analysis/doc_warnings.py analysis/req_refs.py ../tests/tools/test_req_refs.py analysis/check_version.py ../tests/tools/test_check_version.py ../tests/tools/test_doxygen.py ../tests/tools/test_variant_modules.py analysis/cable_scale.py ../tests/tools/test_cable_scale.py analysis/build_size.sh analysis/ide_build.sh analysis/compare_with_ide.sh analysis/flag_sweep.sh analysis/check_export_ino.sh analysis/fn_sizes.py analysis/line_sizes.py mutate.py bench/stream_hash.sh prototypes/hypot_bound.sh
       analysis/stack_depth.py check_help.sh check_links.py bench/gcode_check.sh generators/gen_gcode.py generators/gen_sd_tree.py generators/gen_geometry_svg.py generators/gen_wiring.py generators/gen_jogcurve.py generators/gen_branding_png.py
       ../tests/host/run_all.sh ../tests/host/run_test.sh
       analysis/cyclesim/build.sh analysis/cyclesim/avrsim.py analysis/cyclesim/profile.py analysis/cyclesim/run_arc.py analysis/cyclesim/run_jogcurve.py analysis/cyclesim/run_uart.py
       analysis/cyclesim/run_link.py analysis/cyclesim/run_cands.py analysis/cyclesim/run_hypot.py analysis/cyclesim/run_isqrt.py analysis/cyclesim/run_motion.py analysis/cyclesim/run_halt.py)
bad=0
for t in "${TOOLS[@]}"; do
  case "$t" in *.py) cmd=(python3 "$t" -h);; *) cmd=(bash "$t" -h);; esac
  out=$("${cmd[@]}" 2>&1); rc=$?
  if [ $rc -eq 0 ] && [ -n "$out" ]; then printf "ok    %s\n" "$t"; else printf "FAIL  %s (exit %d)\n" "$t" $rc; bad=$((bad+1)); fi
done
# every executable script and python file must be in the list
for f in $(ls ../tests/cmake/*.sh ../tests/tools/*.py *.sh *.py analysis/*.sh analysis/*.py bench/*.sh bench/*.py generators/*.py prototypes/*.sh analysis/cyclesim/*.sh analysis/cyclesim/*.py ../tests/host/*.sh 2>/dev/null); do
  printf '%s\n' "${TOOLS[@]}" | grep -qx "$f" || { echo "MISSING from TOOLS: $f"; bad=$((bad+1)); }
done
exit $bad
