#!/bin/bash
#
# check_serial_files.sh
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
# check_serial_files.sh -- do the serial-variant GCode files (tools/generators/gen_serial_tests.py) do what they say, on the firmware's real parser?
usage() { cat <<'USAGE'
usage: check_serial_files.sh [-h|--help] [-D NAME=VALUE]...

Regenerates tests/bench/gcode_serial for the maintainer's machine (D 1065, L 800) in a temporary directory and compares it with the committed files; then runs every file through
tools/bench/gcode_check.sh --serial with that geometry (and the generator once more for a second geometry, D 1000 L 700, to see that the files do not depend on one machine):
  - 20..27, 60, 61 (accepted): no refused line (23 is G53, the machine frame);
  - 30..51 (refusal files): exactly one refused line, with the error number in the file's name (30_Refuse_01_... is error:1, 51_Refuse_33_... is error:33);
  - 49_Refuse_11_Line81: no parser refusal (error:11 is the LINK's), and exactly one line over the link's 80 significant characters.
Prints one line per file; exit status: the number of files that did not behave.
USAGE
}
case "$1" in -h|--help) usage; exit 0;; esac
DEFS=(); while [ $# -gt 0 ]; do case "$1" in -D) DEFS+=(-D "$2"); shift 2;; *) echo "check_serial_files.sh: unknown option '$1' (try -h)" >&2; exit 2;; esac; done
cd "$(dirname "$0")" || exit 2
case " ${DEFS[*]} " in *CORELR_Y_UP=0*) echo "serial files: skipped (written for Y up the wall)"; exit 0;; esac
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; bad=0
GEN=../../tools/generators/gen_serial_tests.py; CHK=../../tools/bench/gcode_check.sh
$GEN --geometry 1065,800 -o "$T/m" >/dev/null 2>&1 || { echo "gen_serial_tests.py failed"; exit 2; }
if diff -q -r "$T/m" ../../tests/bench/gcode_serial --exclude=README.md >/dev/null 2>&1 && diff -q "$T/m/README.md" ../../tests/bench/gcode_serial/README.md >/dev/null 2>&1; then echo "serial_files_generated ok"; else echo "serial_files_generated FAILED (tests/bench/gcode_serial is not gen_serial_tests.py --geometry 1065,800's output)"; bad=$((bad+1)); fi
$GEN --geometry 1000,700 -o "$T/o" >/dev/null 2>&1 || { echo "gen_serial_tests.py for 1000,700 failed"; bad=$((bad+1)); }
run() {   # dir geometry file -> sets OUT, ERRS (the count of `error:` lines), FIRST (the first number)
  OUT=$($CHK --serial "${DEFS[@]}" --limit 1000 --geometry "$2" "$1/$3" 2>&1)
  ERRS=$(echo "$OUT" | grep -c "error:"); FIRST=$(echo "$OUT" | grep -o "error:[0-9]*" | head -1)
}
for pair in "$T/m:1065,800" "$T/o:1000,700"; do
  D=${pair%%:*}; G=${pair##*:}
  for f in "$D"/*.gc; do
    n=$(basename "$f"); run "$D" "$G" "$n"
    case "$n" in
      49_*) if [ "$ERRS" = "0" ] && [ "$(echo "$OUT" | grep -c "over the serial link's 80")" = 1 ]; then r=ok; else r="FAILED ($ERRS refusals; $(echo "$OUT" | grep -c "over the serial link's 80") long lines)"; fi;;
      [345][0-9]_Refuse_*) want=$(echo "$n" | sed -E 's/^[0-9]+_Refuse_([0-9]+)_.*/\1/' | sed 's/^0*//')
            if [ "$ERRS" = "1" ] && [ "$FIRST" = "error:$want" ]; then r=ok; else r="FAILED ($ERRS refusals, first '$FIRST', expected one error:$want)"; fi;;
      *) if [ "$ERRS" = "0" ]; then r=ok; else r="FAILED ($ERRS refusals: $(echo "$OUT" | grep 'error:' | head -2 | tr '\n' ' '))"; fi;;
    esac
    [ "$r" = ok ] || bad=$((bad+1)); echo "serial_${n%.gc}[$G] $r"
  done
done
exit $bad
