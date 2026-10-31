#!/usr/bin/env python3
#
# check_halt_order.py
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
"""Does grbl_corelr.ino close Stepper's gate only AFTER the halt ramp is enqueued?

Why a text check: the sketch is the one file no host test can run (it is the Arduino entry point), and the rule it breaks is
cheap to break again. The ramp that stops the machine is made of motion entries; Motion_IsHaltRampDone() reports "done" once
Motion has *enqueued* the last of them (tests/host/test_motion.c, section 6b, proves a refused ramp never finishes). So
Stepper_HaltRequests() must not be called in BeginRampedStop(), together with Motion_Halt(); it belongs at the end of the ramp,
in the JOB_HALTING and JOB_ERRORSTOP cases of the ramp-completion switch. Bench, 2026-10-10: with the gate closed first, a pause
never finished, the cancel that followed could not be acknowledged, and the calibration was lost.

Usage:   check_halt_order.py [-h] [SKETCH]      (default: ../../firmware/app/grbl_corelr.ino, relative to this script)
Exit status: 0 = the order is right, 1 = it is not (the reason is printed), 2 = the code was not found where expected.
Example: tools/analysis/check_halt_order.py
"""
import argparse
import os
import re
import sys


def body(text, header):
    """The braces-balanced body that follows `header` (a regular expression)."""
    m = re.search(header, text)
    if not m:
        return None
    i = text.index("{", m.end())
    depth = 0
    for j in range(i, len(text)):
        depth += (text[j] == "{") - (text[j] == "}")
        if depth == 0:
            return text[i:j + 1]
    return None


def case_body(text, label):
    """The statements of `case label:` up to the next `break;`, inside the ramp-completion switch."""
    sw = body(text, r"if \(Motion_IsHaltRampDone\(\)\)\s*(?=\{)")
    if sw is None:
        return None
    m = re.search(r"case %s:(.*?)break;" % label, sw, re.S)
    return m.group(1) if m else None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog="Exit status: 0 right, 1 wrong, 2 code not found.")
    ap.add_argument("sketch", nargs="?", default=os.path.join(here, "..", "..", "firmware", "app", "grbl_corelr.ino"))
    text = open(ap.parse_args().sketch).read()
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)       # comments say why; only code counts
    ramped = body(code, r"static void BeginRampedStop\([^)]*\)")
    halting, errorstop = case_body(code, "JOB_HALTING"), case_body(code, "JOB_ERRORSTOP")
    if ramped is None or halting is None or errorstop is None:
        print("check_halt_order: BeginRampedStop() or the ramp-completion switch was not found", file=sys.stderr)
        return 2
    bad = []
    if "Stepper_HaltRequests" in ramped:
        bad.append("BeginRampedStop() closes the stepper's gate together with Motion_Halt(): the ramp can never be enqueued")
    for name, text_ in (("JOB_HALTING", halting), ("JOB_ERRORSTOP", errorstop)):
        if "Stepper_HaltRequests" not in text_:
            bad.append("the %s case of the ramp-completion switch does not close the gate" % name)
    for b in bad:
        print("check_halt_order: " + b, file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
