#!/usr/bin/env python3
#
# check_image_symbols.py
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
"""check_image_symbols.py -- does a firmware image contain anything that belongs to the OTHER image? (REQ-CoreLR-D-Build-0014)

cmake/variant_modules.txt lists what belongs to ONE image (SDReader and the SD library for `sd`, Uart and SerialLink for `serial`). The build leaves the others out and
the sources are guarded, but nothing looked at what the LINKED image holds: on 2026-10-27 the serial image was found to carry 86 bytes of the SD card library (the
virtual methods of SdFile, kept alive by a vtable, and a constructor stub) that nothing called, and no size tool had noticed, because every one of them compares an
image with another one, not with what it should contain.

For each image this reads the linker's map file and the ELF's symbols and reports, for every entry of the list that belongs to the OTHER image:
  - an object file of that module or library that was linked (map file);
  - a defined symbol that belongs to it (avr-nm, demangled): the functions by name, the C++ classes with their vtables.
Every defined symbol counts, weak ones too (the inline methods of a class are weak). The interrupt vectors are not looked at: the unused ones are weak aliases of the
default handler (`W __vector_18/19` in the SD image, where Uart's two vectors are not used, is right).
Every entry of the list that is a source file or a directory needs a pattern in PATTERNS below (a new module needs one: the check says so and stops).

Usage:   check_image_symbols.py [-h] [--build-dir DIR] [--variant sd|serial --elf FILE [--map FILE]] [-v]
  --build-dir DIR   a CMake build of both images (default build/avr-default); the files are DIR/firmware/corelr_<image>.elf and DIR/corelr_<image>.map
  --variant, --elf  check one ELF only (and its map, with --map)
  -v                list the modules checked and the counts for each image, also when all is clean
Needs avr-nm (AVR_BIN of the environment or local.conf; auto-detected).
Example:  cmake --build --preset avr-default && tools/analysis/check_image_symbols.py
Exit status: the number of violations found (0 = both images clean); 2 if it could not run (no build, no avr-nm, a module without a pattern).
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))

# What names belong to each module or library of cmake/variant_modules.txt, by the last component of its path without the extension (SDReader.h and SDReader.cpp: SDReader).
# Matched on the DEMANGLED name (avr-nm -C), so a class, its methods and its vtable ("vtable for SdFile") are caught.
PATTERNS = {
    "SDReader":   r"^SDReader_",
    "sdfat":      r"\b(Sd2Card|SdFile|SdVolume|SdFat|SdBaseFile)(::|\b)",
    "SPI":        r"\bSPI(Class|Settings)\b|^SPI\b",
    "Uart":       r"^Uart_",
    "SerialLink": r"^SerialLink_",
}
SOURCE = (".c", ".cpp", ".cc", ".S")


class Error(Exception):
    pass


def read_list(path):
    """cmake/variant_modules.txt -> [(path, image)]"""
    out = []
    for line in open(path, encoding="utf-8"):
        if not line.strip() or line.lstrip().startswith("#") or "|" not in line:
            continue
        p, image = (x.strip() for x in line.split("|", 1))
        if image not in ("sd", "serial"):
            raise Error("%s: the image of '%s' must be sd or serial, not '%s'" % (path, p, image))
        out.append((p, image))
    return out


def module_of(path):
    return os.path.splitext(os.path.basename(path.rstrip("/")))[0]


def of_other_image(entries, image):
    return [p for p, i in entries if i != image]


def check_patterns(entries):
    """Every entry that is a directory or a source file has a pattern (headers are checked through their module's source)."""
    for p, _ in entries:
        if p.endswith(".h"):
            continue
        if module_of(p) not in PATTERNS:
            raise Error("no symbol pattern for '%s' (module '%s'): add one to PATTERNS in check_image_symbols.py" % (p, module_of(p)))


def object_hits(map_text, others):
    """The object files of the map that come from a module or library of the other image."""
    objs = set(re.findall(r"[^\s()]+\.(?:obj|o)\b", map_text))
    hits = []
    for p in others:
        if p.endswith(".h"):
            continue
        rel = p[len("firmware/"):] if p.startswith("firmware/") else p
        for o in sorted(objs):
            if p.endswith(SOURCE):
                if o.endswith("/" + rel + ".obj") or o.endswith("/" + rel + ".o"):
                    hits.append(o)
            elif "/" + rel.rstrip("/") + "/" in o:
                hits.append(o)
    return hits


def symbol_hits(nm_text, others):
    """The defined symbols (avr-nm -C output) that belong to a module or library of the other image."""
    pats = sorted({PATTERNS[module_of(p)] for p in others if not p.endswith(".h")})
    hits = []
    for line in nm_text.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3 or len(parts[1]) != 1:
            continue
        for pat in pats:
            if re.search(pat, parts[2]):
                hits.append(parts[2])
                break
    return hits


def check(image, entries, nm_text, map_text):
    """-> (violations, number of symbols examined)"""
    others = of_other_image(entries, image)
    v = ["object linked: " + o for o in object_hits(map_text, others)]
    v += ["symbol: " + s for s in symbol_hits(nm_text, others)]
    return v, sum(1 for l in nm_text.splitlines() if len(l.split(None, 2)) == 3)


def run_nm(elf):
    import corelr_env
    nm = corelr_env.avr_tool("avr-nm")
    try:
        return subprocess.check_output([nm, "-C", "--defined-only", elf], text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        raise Error("avr-nm failed on %s: %s" % (elf, e))


def one(image, elf, mp, entries, verbose):
    for f in (elf, mp):
        if not os.path.isfile(f):
            raise Error("%s not found: build both images first (cmake --build --preset avr-default)" % f)
    v, n = check(image, entries, run_nm(elf), open(mp, encoding="utf-8", errors="replace").read())
    others = [p for p in of_other_image(entries, image) if not p.endswith(".h")]
    if v:
        print("%-6s %d violation(s): what belongs to the other image is in this one" % (image, len(v)))
        for x in v:
            print("         " + x)
    elif verbose:
        print("%-6s clean: %d symbols examined, none of %s" % (image, n, ", ".join(module_of(p) for p in others)))
    else:
        print("%-6s clean" % image)
    return len(v)


def main(argv):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build", "avr-default"))
    ap.add_argument("--variant", choices=("sd", "serial"))
    ap.add_argument("--elf")
    ap.add_argument("--map")
    ap.add_argument("-v", action="store_true")
    try:
        a = ap.parse_args(argv)
    except SystemExit:
        return 2
    if a.help:
        print(__doc__)
        return 0
    try:
        entries = read_list(os.path.join(ROOT, "cmake", "variant_modules.txt"))
        check_patterns(entries)
        if a.variant or a.elf:
            if not (a.variant and a.elf):
                raise Error("--variant and --elf go together")
            return one(a.variant, a.elf, a.map or os.path.splitext(a.elf)[0] + ".map", entries, a.v)
        total = 0
        for image in ("sd", "serial"):
            total += one(image, os.path.join(a.build_dir, "firmware", "corelr_%s.elf" % image),
                         os.path.join(a.build_dir, "corelr_%s.map" % image), entries, a.v)
        return total
    except Error as e:
        print("check_image_symbols: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
