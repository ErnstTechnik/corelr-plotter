#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# flag_sweep.sh -- which compiler and linker flags make the image smaller? Builds one variant once per candidate and prints flash and RAM against the baseline.
usage() { cat <<'USAGE'
usage: flag_sweep.sh [-h|--help] [-v serial|sd] [-w DIR] [-l] [-c NAME]... [-n 'NAME|COMPILE FLAGS|LINK FLAGS']...

Each candidate is built from scratch with tools/analysis/build_size.sh (so with the project's flags, -mcall-prologues included, plus the candidate's) and compared with the
baseline (the IDE's flags). This MEASURES; it does not judge: a flag that changes the code changes the timing, the stack and possibly the behaviour, so a smaller image is only a
candidate until the host tests, tools/analysis/stack_depth.py, tools/analysis/cyclesim and the machine have seen it.

  -v VARIANT   serial or sd (default sd: the one with the least room)
  -w DIR       work directory, kept (default $TMPDIR/flag_sweep): one build directory per candidate
  -l           list the built-in candidates and exit
  -c NAME      only this built-in candidate (repeatable); default: all of them
  -n 'NAME|CFLAGS|LFLAGS'   an extra candidate, flags separated by spaces ('|' separates the three fields; either flag field may be empty), e.g. -n 'relax|-mrelax|-mrelax'
  -h, --help   this text

Output: one line per candidate, `NAME  flash  delta  RAM  delta`, sorted as run; a build that fails to compile or link says FAILED (many size flags are not valid for avr-gcc 7.3 or for C).
Exit status: 0, or 2 for bad arguments. Takes about a minute per candidate.
Example: tools/analysis/flag_sweep.sh -c relax -c ltopart_one
USAGE
}
HERE=$(cd "$(dirname "$0")" && pwd)
VAR=sd; WORK="${TMPDIR:-/tmp}/flag_sweep"; ONLY=(); EXTRAC=(); LIST=0
while [ $# -gt 0 ]; do
  case "$1" in -h|--help) usage; exit 0;; -v) VAR="$2"; shift 2;; -w) WORK="$2"; shift 2;; -l) LIST=1; shift;; -c) ONLY+=("$2"); shift 2;; -n) EXTRAC+=("$2"); shift 2;;
    *) echo "flag_sweep.sh: unknown option '$1' (try -h)" >&2; exit 2;; esac
done
# name | compile flags | link flags.  -mrelax needs to reach the link as well (the LTO link does the code generation).
CANDIDATES=(
 "relax|-mrelax|-mrelax"
 "ltopart_one|-flto-partition=one|-flto-partition=one"
 "ltopart_none|-flto-partition=none|-flto-partition=none"
 "wholeprogram||-fwhole-program"
 "no_jump_tables|-fno-jump-tables|"
 "no_switch_conv|-fno-tree-switch-conversion|"
 "short_enums|-fshort-enums|"
 "accumulate_args|-maccumulate-args|"
 "strict_x|-mstrict-X|"
 "main_os_task|-mmain-is-OS_task|"
 "no_inline_small|-fno-inline-small-functions|"
 "no_inline_once|-fno-inline-functions-called-once|"
 "no_ipa_cp|-fno-ipa-cp|"
 "no_caller_saves|-fno-caller-saves|"
 "conserve_stack|-fconserve-stack|"
 "merge_constants|-fmerge-all-constants|"
 "no_ident|-fno-ident|"
 "no_rtti|-fno-rtti|"
 "no_cxa_atexit|-fno-use-cxa-atexit|"
 "no_split_wide|-fno-split-wide-types|"
 "ira_priority|-fira-algorithm=priority|"
 "no_ipa_icf|-fno-ipa-icf|"
 "no_peephole2|-fno-peephole2|"
 "no_reorder_fn|-fno-reorder-functions|"
 "no_guess_branch|-fno-guess-branch-probability|"
 "no_tree_pre|-fno-tree-pre|"
 "no_gcse|-fno-gcse|"
 "O2_for_comparison|-O2|"
 "Og_for_comparison|-Og|"
)
[ $LIST -eq 1 ] && { printf '%s\n' "${CANDIDATES[@]}" | awk -F'|' '{printf "  %-18s compile: %-34s link: %s\n",$1,$2,$3}'; exit 0; }
mkdir -p "$WORK"
run() { # name cflags lflags
  local name="$1" args=() f out
  for f in $2; do args+=(-F "$f"); done; for f in $3; do args+=(-L "$f"); done
  out=$("$HERE/build_size.sh" -v "$VAR" -b "$WORK/$name" "${args[@]}" 2>&1)
  FL=$(echo "$out" | sed -n 's/^flash   : \([0-9]*\) .*/\1/p'); RM=$(echo "$out" | sed -n 's/^RAM     : \([0-9]*\) .*/\1/p')
  [ -n "$FL" ]
}
run baseline "" "" || { echo "the baseline itself does not build"; exit 2; }
BF=$FL; BR=$RM
printf '%-20s %7s %7s %6s %6s\n' "candidate" "flash" "delta" "RAM" "delta"
printf '%-20s %7d %7s %6d %6s\n' "baseline" "$BF" "-" "$BR" "-"
for c in "${CANDIDATES[@]}" "${EXTRAC[@]}"; do
  IFS='|' read -r name cf lf <<< "$c"
  if [ ${#ONLY[@]} -gt 0 ] && ! printf '%s\n' "${ONLY[@]}" | grep -qx "$name" && ! printf '%s\n' "${EXTRAC[@]}" | grep -qxF "$c"; then continue; fi
  if run "$name" "$cf" "$lf"; then printf '%-20s %7d %+7d %6d %+6d\n' "$name" "$FL" $((FL-BF)) "$RM" $((RM-BR)); else printf '%-20s %7s\n' "$name" "FAILED"; fi
done
