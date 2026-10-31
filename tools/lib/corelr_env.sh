# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# corelr_env.sh -- source it: `. "$HERE/../lib/corelr_env.sh"`. Loads local.conf (see local.conf.example) and finds the AVR tools.
# Exports AVR_BIN (the directory of avr-gcc & co; may stay empty = use the PATH), and any other KEY of local.conf not already in the environment.
CORELR_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
if [ -f "$CORELR_ROOT/local.conf" ]; then
  while IFS= read -r _l || [ -n "$_l" ]; do
    case "$_l" in ''|'#'*) continue;; esac
    _k=${_l%%=*}; _v=${_l#*=}; _k=$(echo "$_k" | tr -d ' \t'); _v=$(echo "$_v" | sed 's/^[ \t]*//; s/[ \t]*$//')
    case "$_k" in [A-Za-z_]*) ;; *) continue;; esac
    case "$_v" in "~"|"~/"*) _v="$HOME${_v#\~}";; esac
    [ -n "${!_k}" ] || export "$_k=$_v"
  done < "$CORELR_ROOT/local.conf"
fi
if [ -z "$AVR_BIN" ] && ! command -v avr-gcc >/dev/null 2>&1; then
  for _d in /snap/arduino/current/hardware/tools/avr/bin "$HOME"/snap/arduino/*/.arduino15/packages/arduino/tools/avr-gcc/*/bin "$HOME"/.arduino15/packages/arduino/tools/avr-gcc/*/bin; do
    [ -x "$_d/avr-gcc" ] && export AVR_BIN="$_d" && break
  done
fi
# avr_tool NAME -> the path to use for an AVR tool
avr_tool() { if [ -n "$AVR_BIN" ]; then echo "$AVR_BIN/$1"; else echo "$1"; fi; }
