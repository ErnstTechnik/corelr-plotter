#!/usr/bin/env python3
#
# test_image_symbols.py
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
"""test_image_symbols.py -- tests of tools/analysis/check_image_symbols.py (what the linked image holds against cmake/variant_modules.txt)

Part 1 gives the checker fabricated `avr-nm -C` and map-file text, one defect at a time (the part tests/mutation/mutants/image_symbols.txt breaks).
Part 2 checks the repository: every module of cmake/variant_modules.txt has a symbol pattern, and the pattern really matches a name of that module.
Part 3 runs the checker on the images of a build, when one exists (build/avr-default, or the directory in CORELR_BUILD_DIR): none is not a failure, it is said.

Usage:   test_image_symbols.py [-h]
Exit status: the number of failed checks (0 = all pass).
"""
import os, sys
if "-h" in sys.argv or "--help" in sys.argv:
    print(__doc__)
    sys.exit(0)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "analysis"))
import check_image_symbols as c

fails = 0
def check(name, got, want):
    global fails
    if got != want:
        fails += 1
        print("FAIL %s: got %r want %r" % (name, got, want))

E = [("firmware/src/SDReader.h", "sd"), ("firmware/src/SDReader.cpp", "sd"), ("firmware/src/Uart.h", "serial"), ("firmware/src/Uart.c", "serial"),
     ("firmware/src/SerialLink.h", "serial"), ("firmware/src/SerialLink.c", "serial"),
     ("third_party/sdfat", "sd"), ("third_party/arduino-core/libraries/SPI", "sd")]
BASE = "00000100 T main\n00000200 t Motion_Task\n00000352 W __vector_18\n00000352 W __vector_19\n00800100 b s_queue\n"
MAPBASE = "firmware/CMakeFiles/corelr_%s.dir/src/Motion.c.obj\nfirmware/libcorelr_%s_core.a(main.cpp.obj)\n"
def run(image, nm="", mp=""):
    v, _ = c.check(image, E, BASE + nm, (MAPBASE % (image, image)) + mp)
    return v

# --- a clean image
check("clean sd", run("sd"), [])
check("clean serial", run("serial"), [])
check("the unused vectors are not Uart", run("sd", "00000352 W __vector_20\n"), [])

# --- the SD image holding serial things
check("Uart symbol in sd", len(run("sd", "00000400 T Uart_Init\n")), 1)
check("SerialLink symbol in sd", len(run("sd", "00000400 t SerialLink_Task\n")), 1)
check("Uart object in sd", len(run("sd", "", "firmware/CMakeFiles/corelr_sd.dir/src/Uart.c.obj\n")), 1)
check("SerialLink object in sd", len(run("sd", "", "firmware/CMakeFiles/corelr_sd.dir/src/SerialLink.c.obj\n")), 1)
check("a .o object (not .obj) of Uart in sd", len(run("sd", "", "firmware/CMakeFiles/corelr_sd.dir/src/Uart.c.o\n")), 1)
check("a folder that merely ends like src is not src", run("sd", "", "firmware/CMakeFiles/corelr_sd.dir/oldsrc/Uart.c.obj\n"), [])
check("a name that merely ends like Uart is not Uart", run("sd", "00000400 T MyUart_Init\n", "firmware/CMakeFiles/corelr_sd.dir/src/MyUart.c.obj\n"), [])

# --- the serial image holding SD things
check("SDReader symbol in serial", len(run("serial", "00000400 T SDReader_Task\n")), 1)
check("SdFile method in serial", len(run("serial", "000004ec t SdFile::write(unsigned char)\n")), 1)
check("SdFile weak inline method in serial", len(run("serial", "000004ec W SdFile::availableForWrite()\n")), 1)
check("SdFile vtable in serial", len(run("serial", "00000090 V vtable for SdFile\n")), 1)
check("Sd2Card in serial", len(run("serial", "000004ec t Sd2Card::cardCommand(unsigned char, unsigned long)\n")), 1)
check("SdVolume in serial", len(run("serial", "000004ec t SdVolume::cacheFlush(unsigned char)\n")), 1)
check("SPI class in serial", len(run("serial", "000004ec t SPIClass::transfer(unsigned char)\n")), 1)
check("SDReader object in serial", len(run("serial", "", "firmware/CMakeFiles/corelr_serial.dir/src/SDReader.cpp.obj\n")), 1)
check("sdfat object in serial", len(run("serial", "", "firmware/CMakeFiles/corelr_serial.dir/__/third_party/sdfat/Sd2Card.cpp.obj\n")), 1)
check("SPI library object in serial", len(run("serial", "", "firmware/CMakeFiles/corelr_serial.dir/__/third_party/arduino-core/libraries/SPI/src/SPI.cpp.obj\n")), 1)
check("an SD name is no violation in the SD image", run("sd", "000004ec t SdFile::write(unsigned char)\n00000400 T SDReader_Task\n", "firmware/CMakeFiles/corelr_sd.dir/src/SDReader.cpp.obj\n"), [])
check("Uart in serial is no violation", run("serial", "00000400 T Uart_Init\n", "firmware/CMakeFiles/corelr_serial.dir/src/Uart.c.obj\n"), [])
check("two defects, two violations", len(run("serial", "00000400 T SDReader_Task\n000004ec t SdFile::write(unsigned char)\n")), 2)

# --- the list
try:
    c.check_patterns(E + [("firmware/src/NewModule.c", "sd")])
    check("an unknown module is refused", "accepted", "refused")
except c.Error:
    pass
try:
    c.check_patterns(E)
except c.Error as e:
    check("the fabricated list has patterns", str(e), "")
try:
    c.read_list(os.path.join(HERE, "no_such_file"))
    check("a missing list is an error", "read", "error")
except OSError:
    pass

import tempfile
with tempfile.TemporaryDirectory() as d:
    lst = os.path.join(d, "list.txt")
    open(lst, "w").write("firmware/src/Uart.c|serial\nfirmware/src/Foo.c|both\n")
    try:
        c.read_list(lst)
        check("an image that is neither sd nor serial is refused", "accepted", "refused")
    except c.Error:
        pass
    open(lst, "w").write("# comment\n\nfirmware/src/Uart.c | serial\nthird_party/sdfat|sd\n")
    check("the list is read, comments and blanks skipped, blanks around the fields stripped", c.read_list(lst),
          [("firmware/src/Uart.c", "serial"), ("third_party/sdfat", "sd")])
    # the whole of one image, with avr-nm replaced by a fabricated text
    elf, mp = os.path.join(d, "x.elf"), os.path.join(d, "x.map")
    open(elf, "w").write("")
    open(mp, "w").write(MAPBASE % ("serial", "serial") + "firmware/CMakeFiles/corelr_serial.dir/src/SDReader.cpp.obj\n")
    real = c.run_nm
    c.run_nm = lambda f: BASE + "000004ec t SdFile::write(unsigned char)\n"
    try:
        check("one image: the number of violations is returned", c.one("serial", elf, mp, E, False), 2)
        check("one image: the same through main", c.main(["--variant", "serial", "--elf", elf, "--map", mp]), 2)
        check("one image: the map is next to the ELF by default", c.main(["--variant", "serial", "--elf", elf]), 2)
        os.rename(mp, mp + ".away")
        check("one image: no map, it could not run", c.main(["--variant", "serial", "--elf", elf]), 2)
        os.rename(mp + ".away", mp)
        open(elf + ".gone", "w").close()
        check("one image: no ELF, it could not run", c.main(["--variant", "serial", "--elf", elf + ".nothing", "--map", mp]), 2)
        open(mp, "w").write(MAPBASE % ("sd", "sd"))
        check("one image: clean, and the other image's names do not count", c.one("sd", elf, mp, E, False), 0)
    finally:
        c.run_nm = real
    # both images of a build directory: the violations add up
    os.makedirs(os.path.join(d, "b", "firmware"))
    for i in ("sd", "serial"):
        open(os.path.join(d, "b", "firmware", "corelr_%s.elf" % i), "w").close()
    open(os.path.join(d, "b", "corelr_sd.map"), "w").write(MAPBASE % ("sd", "sd"))
    open(os.path.join(d, "b", "corelr_serial.map"), "w").write(MAPBASE % ("serial", "serial") + "firmware/CMakeFiles/corelr_serial.dir/src/SDReader.cpp.obj\n")
    c.run_nm = lambda f: BASE + ("00000400 T Uart_Init\n" if f.endswith("corelr_sd.elf") else "00000400 T SDReader_Task\n")
    try:
        check("both images: the violations add up (1 in sd, 2 in serial)", c.main(["--build-dir", os.path.join(d, "b")]), 3)
    finally:
        c.run_nm = real
check("no build: it could not run", c.main(["--build-dir", os.path.join(HERE, "no_such_build")]), 2)
check("--variant without --elf: it could not run", c.main(["--variant", "sd"]), 2)

# --- the repository's own list
entries = c.read_list(os.path.join(ROOT, "cmake", "variant_modules.txt"))
try:
    c.check_patterns(entries)
except c.Error as e:
    check("every module of cmake/variant_modules.txt has a pattern", str(e), "")
check("the list names both images", sorted({i for _, i in entries}), ["sd", "serial"])
SAMPLE = {"SDReader": "SDReader_Task", "sdfat": "SdFile::read()", "SPI": "SPIClass::begin()", "Uart": "Uart_Init", "SerialLink": "SerialLink_Task"}
import re
for module, name in SAMPLE.items():
    check("pattern of %s matches its own name" % module, bool(re.search(c.PATTERNS[module], name)), True)
check("a pattern for each module of the list is in the sample", sorted({c.module_of(p) for p, _ in entries if not p.endswith(".h")}), sorted(SAMPLE))

# --- the images of a build, when there is one
bd = os.environ.get("CORELR_BUILD_DIR") or os.path.join(ROOT, "build", "avr-default")
elfs = [os.path.join(bd, "firmware", "corelr_%s.elf" % i) for i in ("sd", "serial")]
if all(os.path.isfile(f) for f in elfs):
    check("the images of %s hold nothing of the other image" % bd, c.main(["--build-dir", bd]), 0)
else:
    print("note: no build in %s: part 3 skipped (cmake --build --preset avr-default)" % bd)

print("ok" if not fails else "%d failed" % fails)
sys.exit(fails)
