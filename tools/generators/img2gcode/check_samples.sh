#!/bin/bash
#
# check_samples.sh
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
# check_samples.sh -- does every GCode file img2gcode.py writes for the sample pictures pass the firmware's REAL parser, planner and travel zone?
# Generates the samples (make_test_images.py), runs img2gcode.py -s all on each, then tools/bench/gcode_check.sh on every .gc.
# usage: check_samples.sh [-h] [GEOMETRY]     GEOMETRY = D,OX,OY or D,L in mm (default 1065,800: the bench)
# Exit status: the number of files the check refuses, plus the number of files that were not written (0 = all fine).
case "$1" in -h|--help) sed -n '21,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0;; esac
HERE=$(cd "$(dirname "$0")" && pwd); GEO=${1:-1065,800}; PY="$HERE/.venv/bin/python"; [ -x "$PY" ] || PY="$HERE/.venv/Scripts/python.exe"
[ -x "$PY" ] || { echo "check_samples.sh: no .venv here (python3 -m venv .venv && .venv/bin/pip install -r requirements.txt)" >&2; exit 2; }
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT; "$PY" "$HERE/make_test_images.py" -o "$OUT/samples" >/dev/null || exit 2; bad=0
for img in "$OUT"/samples/*.png; do
  "$PY" "$HERE/img2gcode.py" "$img" -s all --geometry "$GEO" -o "$OUT/out" >/dev/null || bad=$((bad + 1))
done
for st in skeleton outline hatch; do for img in "$OUT"/samples/*.png; do n=$(basename "$img" .png); f="$OUT/out/$n/$st/$n.gc"
  if [ ! -f "$f" ]; then echo "NOT WRITTEN: $n/$st"; bad=$((bad + 1)); continue; fi
  "$HERE/../../bench/gcode_check.sh" --limit 400 --geometry "$GEO" "$f" >"$OUT/log" 2>&1 && echo "ok   $n/$st" || { echo "FAIL $n/$st"; cat "$OUT/log"; bad=$((bad + 1)); }
done; done
echo "$bad problem(s)"; exit $bad
