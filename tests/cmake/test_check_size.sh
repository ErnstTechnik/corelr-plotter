#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# test_check_size.sh -- does cmake/CheckSize.cmake measure and enforce the way the Arduino IDE does? (REQ-CoreLR-D-Build-0004)
# It feeds the script a fake `avr-size -A` report and checks the verdict. Proved able to fail by tests/mutation/mutants/checksize.txt.
usage() { echo "usage: test_check_size.sh [-h]   -- runs CheckSize.cmake against a fake avr-size; prints 'all checks passed' or the failing case. Exit: number of failures."; }
case "$1" in -h|--help) usage; exit 0;; esac
HERE=$(cd "$(dirname "$0")" && pwd); CS="$HERE/../../cmake/CheckSize.cmake"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# fake avr-size: prints the report held in $T/report, whatever the arguments
printf '#!/bin/sh\ncat "%s/report"\n' "$T" > "$T/avr-size"; chmod +x "$T/avr-size"
report() { # text data bss noinit eeprom
  printf 'x.elf  :\nsection   size      addr\n.data %s   8388864\n.text %s   0\n.bss %s   8388900\n.noinit %s   8388950\n.eeprom %s   8454144\n.comment 17 0\nTotal 1000\n' "$2" "$1" "$3" "$4" "$5" > "$T/report"; }
bad=0
check() { # name expected_rc flash_max ram_max  (report already written)
  out=$(cmake -DSIZE_TOOL="$T/avr-size" -DELF=x.elf -DNAME=t -DFLASH_MAX=$3 -DRAM_MAX=$4 -P "$CS" 2>&1); rc=$?
  [ "$rc" = "$2" ] || { echo "FAIL $1: exit $rc, expected $2: $out" | head -3; bad=$((bad+1)); return; }
  [ -z "$5" ] || echo "$out" | grep -q "$5" || { echo "FAIL $1: output lacks '$5': $out" | head -3; bad=$((bad+1)); }
}
report 1000 200 300 40 999                                  # flash = text+data = 1200 ; RAM = data+bss+noinit = 540 ; the .eeprom counts for neither
check "exactly at the flash limit passes"   0 1200 540 "flash 1200 / 1200 bytes (0 free), RAM 540 / 540 bytes (0 left"
check "one byte over the flash limit fails" 1 1199 540 "TOO BIG"
check "one byte over the RAM limit fails"   1 1200 539 "static RAM"
check "under both limits passes"            0 30720 2048 "flash 1200 / 30720 bytes (29520 free)"
report 0 0 0 0 0
check "an empty image passes"               0 100 100 "flash 0 / 100"
[ $bad -eq 0 ] && echo "all checks passed"
exit $bad
