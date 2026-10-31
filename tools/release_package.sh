#!/bin/bash
#
# release_package.sh
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
usage() { cat <<'USAGE'
usage: release_package.sh [-h] -v VERSION [-b BUILD_DIR] [-d DOC_DIST] [-o OUT_DIR]

Collects what a release carries (CONTRIBUTING.md, "Versions, changelog, releases") from a finished build into OUT_DIR, for each variant (serial, sd):
  corelr-VERSION-VARIANT.hex   the file to flash          corelr-VERSION-VARIANT.elf   with debug symbols (size, stack and function-size analyses)
  corelr-VERSION-VARIANT.map   the linker map             corelr-VERSION-VARIANT-ide.zip   the sketch exported for the Arduino IDE (target export_ino)
  corelr-VERSION-VARIANT-api.zip   the API documentation (target doc_package)
and, once: build-info.txt (commit, toolchain, size flags, CORELR_* settings, the size of each image) and SHA256SUMS (every file above, `sha256sum -c` checks them).
NEVER an .eep file: the EEPROM holds the calibration and the position, flashing an image of it would erase them. The script stops if one is present.

  -v VERSION   the version in the names (the CORELR_VERSION that CMake reads from firmware/config/Version.h; on a release, with no suffix, the tag is v<VERSION>)
  -b DIR       the AVR build directory, already built, with the target export_ino (default build/avr-default)
  -d DIR       where doc_package wrote its zips (default build/doxygen/dist)
  -o DIR       where the files go (default release-out, emptied first)
Exit status: 0 = the folder is complete; 1 = something was missing (named); 2 = bad usage.
Example: cmake --preset avr-default && cmake --build --preset avr-default && cmake --build --preset avr-default --target export_ino
         cmake --preset host && cmake --build --preset host --target doc_package && tools/release_package.sh -v <version>
USAGE
}

VERSION=; BUILD=build/avr-default; DOCS=build/doxygen/dist; OUT=release-out
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0;;
    -v) VERSION=$2; shift;;
    -b) BUILD=$2; shift;;
    -d) DOCS=$2; shift;;
    -o) OUT=$2; shift;;
    *) echo "release_package.sh: unknown option '$1' (try -h)" >&2; exit 2;;
  esac
  shift
done
[ -n "$VERSION" ] || { echo "release_package.sh: -v VERSION is required (try -h)" >&2; exit 2; }
case "$VERSION" in *[!0-9A-Za-z.+-]*|'') echo "release_package.sh: odd version '$VERSION'" >&2; exit 2;; esac

cd "$(dirname "$0")/.." || exit 2
rm -rf "$OUT"; mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)         # absolute: the zips are written from other folders
bad=0
need() { [ -f "$1" ] || { echo "release_package.sh: missing $1 ($2)" >&2; bad=$((bad + 1)); return 1; }; }

SIZE=$(sed -n 's/^CMAKE_SIZE:FILEPATH=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
[ -x "$SIZE" ] || { echo "release_package.sh: no avr-size in $BUILD/CMakeCache.txt: is $BUILD an AVR build?" >&2; exit 1; }

for v in serial sd; do
  P="corelr-$VERSION-$v"
  need "$BUILD/corelr_$v.hex" "cmake --build" && cp "$BUILD/corelr_$v.hex" "$OUT/$P.hex"
  need "$BUILD/corelr_$v.map" "cmake --build" && cp "$BUILD/corelr_$v.map" "$OUT/$P.map"
  need "$BUILD/firmware/corelr_$v.elf" "cmake --build" && cp "$BUILD/firmware/corelr_$v.elf" "$OUT/$P.elf"
  need "$DOCS/$P-api.zip" "cmake --build --preset host --target doc_package, with CORELR_VERSION = $VERSION" && cp "$DOCS/$P-api.zip" "$OUT/"
  if [ -d "$BUILD/ide_sketch/$v/grbl_corelr" ]; then
    ( cd "$BUILD/ide_sketch/$v" && zip -qrX "$OUT/$P-ide.zip" grbl_corelr ) || { echo "release_package.sh: cannot zip the $v export" >&2; bad=$((bad + 1)); }
  else
    echo "release_package.sh: missing $BUILD/ide_sketch/$v/grbl_corelr (cmake --build --target export_ino)" >&2; bad=$((bad + 1))
  fi
done

{
  echo "CoreLR $VERSION -- how these files were built"
  echo "commit:    $(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "compiler:  $("$(dirname "$SIZE")/avr-gcc" --version 2>/dev/null | head -n 1)"
  echo "size flags (cmake/size_flags.txt; 'on' = both images, 'sd' / 'serial' = that image only, 'off' = not applied):"
  grep -vE '^[[:space:]]*(#|$)' cmake/size_flags.txt | sed 's/^/  /'
  echo "settings (CMakeCache.txt of $BUILD):"
  grep -E '^CORELR_(MATH|SIZE_FLAGS|SHORT_ENUMS|CALL_PROLOGUES|FLASH_BYTES|RAM_BYTES|EXPECTED_GCC|DEBUG_[A-Z_]+|EXTRA_[A-Z_]+):' "$BUILD/CMakeCache.txt" | sed 's/^/  /'
  # CORELR_SHORT_ENUMS is a switch for a flag that the list marks 'off': when the list marks short_enums 'on' (it does since 2026-10-12), -fshort-enums is applied whatever the switch says
  SE=$(grep -E '^short_enums\|' cmake/size_flags.txt | awk -F'|' '{print $4}')
  echo "  note: the flags applied are the ones the list above marks 'on' (and those marked with the image's own name). CORELR_SHORT_ENUMS=ON would only add -fshort-enums when the list marks it 'off'; the list marks short_enums '${SE:-?}', so -fshort-enums is $([ "$SE" = on ] && echo 'APPLIED to both images, whatever the CORELR_SHORT_ENUMS line says' || echo 'applied only if CORELR_SHORT_ENUMS=ON')."
  for v in serial sd; do
    echo "image $v (avr-size, ATmega328P):"
    "$SIZE" -C --mcu=atmega328p "$BUILD/firmware/corelr_$v.elf" 2>/dev/null | grep -E '^(Program|Data):' | sed 's/^/  /'
  done
} > "$OUT/build-info.txt"

if ls "$OUT"/*.eep >/dev/null 2>&1 || unzip -l "$OUT"/*-ide.zip 2>/dev/null | grep -qi '\.eep$'; then
  echo "release_package.sh: an .eep file is in the release: it would erase the calibration" >&2; bad=$((bad + 1))
fi
( cd "$OUT" && sha256sum -- * > SHA256SUMS.tmp && mv SHA256SUMS.tmp SHA256SUMS )
echo "release_package.sh: $(ls "$OUT" | wc -l) file(s) in $OUT$([ $bad -gt 0 ] && echo ", $bad problem(s)")"
[ $bad -eq 0 ]
