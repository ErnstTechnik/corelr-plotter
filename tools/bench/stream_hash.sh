#!/bin/bash
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# stream_hash.sh -- is a refactor of Motion behaviour-neutral? Compare the hash of EVERY step entry sent to Stepper.
usage() { cat <<'USAGE'
usage: stream_hash.sh [-h|--help] [-r GIT_REF] [-D NAME[=VALUE]]... [-C NAME[=VALUE]]...

test_motion and test_arc print "stream hash over N entries: HHHHHHHH": a hash of every {coil bits, hold} the real
generator sent to Stepper across all their scenarios (820 385 and 11 421 789 entries). This runs both tests on the code
at GIT_REF (default HEAD) and on your working tree, and says whether the hashes are identical. Identical = the refactor
changed no entry of any test, to the bit; it is the strongest check there is on the generator, and the standard every
flash trim of Motion is held to.

  -r GIT_REF     the reference (default HEAD): any commit, tag or branch of this repository
  -D NAME[=V]    a define for both builds, repeatable (e.g. -D CORELR_MATH=CORELR_MATH_64)
  -C NAME[=V]    a define for the working-tree build ONLY, repeatable. For a change that is meant to alter the stream, but in a
                 way a test can undo: -C MIRROR_Y makes the tests feed the tree the mirror image of every scenario
                 (tests/host/mirror_y.h), and the hashes are then identical if the Y flip changed nothing but the sign
  -h, --help     this text

Exit status: 0 identical, 1 different, 2 could not build or run.
USAGE
}
REF=HEAD; D=(); DC=()
while [ $# -gt 0 ]; do
  case "$1" in -h|--help) usage; exit 0;; -r) REF="$2"; shift 2;; -D) D+=(-D "$2"); shift 2;; -C) DC+=(-D "$2"); shift 2;;
    *) echo "stream_hash.sh: unknown option '$1' (try -h)" >&2; exit 2;; esac
done
HERE=$(cd "$(dirname "$0")" && pwd); TOP=$(cd "$HERE/../.." && git rev-parse --show-toplevel) || exit 2
REL=$(realpath --relative-to="$TOP" "$HERE/../..")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# Both trees use the CURRENT tests and scripts; only the firmware sources differ (REF's, or the working tree's).
mkdir -p "$T/ref" "$T/cur" "$T/x"
P=""; [ "$REL" != "." ] && P="$REL/"
git -C "$TOP" archive "$REF" "${P}firmware" "${P}third_party" | tar -x -C "$T/x" || { echo "stream_hash.sh: cannot export $REF" >&2; exit 2; }
mv "$T/x/${P}firmware" "$T/ref/firmware"; mv "$T/x/${P}third_party" "$T/ref/third_party"
cp -r "$HERE/../../firmware" "$T/cur/firmware"; cp -r "$HERE/../../third_party" "$T/cur/third_party"
for k in ref cur; do mkdir -p "$T/$k/tests"; cp -r "$HERE/../../tests/host" "$T/$k/tests/host"; rm -rf "$T/$k/tests/host/out"; done
rc=0
for t in test_motion test_arc; do
  a=$("$T/ref/tests/host/run_test.sh" "${D[@]}" $t 2>&1 | grep "stream hash") || true
  b=$("$T/cur/tests/host/run_test.sh" "${D[@]}" "${DC[@]}" $t 2>&1 | grep "stream hash") || true
  [ -n "$a" ] && [ -n "$b" ] || { echo "$t: no hash printed -- the current test did not build against one of the trees (an API changed between $REF and now?)"; rc=2; continue; }
  if [ "$a" = "$b" ]; then echo "$t: IDENTICAL   $b"; else echo "$t: DIFFERENT"; echo "   $REF : $a"; echo "   tree : $b"; [ $rc -eq 0 ] && rc=1; fi
done
exit $rc
