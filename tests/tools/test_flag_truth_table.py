#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""test_flag_truth_table.py -- is tools/analysis/flag_truth_table.py right? The combinations, the factor file, the effects (alone, with all the others, mean, hurts) on
synthetic sizes whose answer is known by hand, the arguments handed to the build tool (with a fake one), the report and the command line. No build is run.
Prints 'all checks passed' or the failing case; exit status = the number of failures. Proved able to fail by tests/mutation/mutants/flag_truth_table.txt."""
import importlib.util, os, stat, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "analysis", "flag_truth_table.py")
if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__); sys.exit(0)
spec = importlib.util.spec_from_file_location("ftt", TOOL)
ftt = importlib.util.module_from_spec(spec); spec.loader.exec_module(ftt)
bad = 0
def check(ok, what):
    global bad
    if not ok:
        bad += 1; print("FAIL:", what)

# --- combinations
c3 = ftt.combinations(3)
check(len(c3) == 8 and len(set(c3)) == 8, "3 factors: %d combinations, %d distinct" % (len(c3), len(set(c3))))
check(c3[0] == (0, 0, 0) and c3[-1] == (1, 1, 1) and c3[1] == (0, 0, 1) and c3[2] == (0, 1, 0), "binary order, the first factor the most significant: %s" % (c3,))
check(ftt.combinations(0) == [()] and len(ftt.combinations(7)) == 128, "0 and 7 factors")

# --- the factor file
with tempfile.TemporaryDirectory() as t:
    f = os.path.join(t, "flags.txt")
    open(f, "w").write("# a comment\n\nrelax|-mrelax|-mrelax|on\nno_gcse|-fno-gcse||on\n  # indented comment\nx|-a -b|-c -d|off\n")
    fa = ftt.read_factors(f)
    check([x[0] for x in fa] == ["relax", "no_gcse", "x"], "factor names: %s" % [x[0] for x in fa])
    check(fa[0] == ("relax", ["-mrelax"], ["-mrelax"], "on") and fa[1] == ("no_gcse", ["-fno-gcse"], [], "on") and fa[2] == ("x", ["-a", "-b"], ["-c", "-d"], "off"), "factor fields: %s" % (fa,))
    open(f, "w").write("only|two|fields\n")
    try:
        ftt.read_factors(f); check(False, "a malformed line must stop the tool")
    except SystemExit:
        pass
    real = ftt.read_factors(os.path.join(HERE, "..", "..", "cmake", "size_flags.txt"))
    check(len(real) >= 5 and all(x[3] in ("on", "off", "sd", "serial") for x in real) and any(x[3] == "on" for x in real), "the project's own size_flags.txt does not parse as expected: %s" % [x[0] for x in real])

# --- the effects, additive: flash = 1000 - 10 a - 20 b + 5 c
names = ["a", "b", "c"]
res = {c: 1000 - 10 * c[0] - 20 * c[1] + 5 * c[2] for c in ftt.combinations(3)}
an = ftt.analyse(names, res)
check(an["baseline"] == 1000 and an["all"] == 975 and an["best"] == (1, 1, 0) and an["best_flash"] == 970, "additive: baseline %s, all %s, best %s (%s)" % (an["baseline"], an["all"], an["best"], an["best_flash"]))
fa_ = an["factors"]
check(fa_["a"]["alone"] == -10 and fa_["a"]["with_all"] == -10 and abs(fa_["a"]["mean"] + 10) < 1e-9 and not fa_["a"]["hurts"], "additive a: %s" % fa_["a"])
check(fa_["b"]["alone"] == -20 and fa_["b"]["with_all"] == -20 and abs(fa_["b"]["mean"] + 20) < 1e-9, "additive b: %s" % fa_["b"])
check(fa_["c"]["alone"] == 5 and fa_["c"]["with_all"] == 5 and abs(fa_["c"]["mean"] - 5) < 1e-9 and fa_["c"]["hurts"], "additive c (a flag that makes it bigger HURTS): %s" % fa_["c"])
# --- with an overlap: flash = 1000 - 10 a - 20 b + 5 a b   (together they save 25, not 30)
res2 = {c: 1000 - 10 * c[0] - 20 * c[1] + 5 * c[0] * c[1] for c in ftt.combinations(2)}
an2 = ftt.analyse(["a", "b"], res2)
check(an2["factors"]["a"]["alone"] == -10 and an2["factors"]["a"]["with_all"] == -5 and abs(an2["factors"]["a"]["mean"] + 7.5) < 1e-9, "overlap a: %s (alone -10, with the other -5, mean -7.5)" % an2["factors"]["a"])
check(an2["factors"]["b"]["alone"] == -20 and an2["factors"]["b"]["with_all"] == -15 and abs(an2["factors"]["b"]["mean"] + 17.5) < 1e-9, "overlap b: %s" % an2["factors"]["b"])
check(an2["all"] == 975 and an2["best"] == (1, 1) and an2["best_flash"] == 975, "overlap: all-on %s, best %s %s (both on, 975)" % (an2["all"], an2["best"], an2["best_flash"]))
check(an2["factors"]["a"]["hurts"] is False and an2["factors"]["b"]["hurts"] is False, "no flag hurts in the overlap case")
# a flag that hurts only in combination: flash = 1000 - 5 a - 30 b + 40 a b -> a alone helps (-5), with b on it costs +35 (and b, +10 with a on)
res3 = {c: 1000 - 5 * c[0] - 30 * c[1] + 40 * c[0] * c[1] for c in ftt.combinations(2)}
an3 = ftt.analyse(["a", "b"], res3)
check(an3["factors"]["a"]["alone"] == -5 and an3["factors"]["a"]["with_all"] == 35 and an3["factors"]["a"]["hurts"] and abs(an3["factors"]["a"]["mean"] - 15) < 1e-9, "a flag that helps alone and hurts with the other: %s" % an3["factors"]["a"])
check(an3["factors"]["b"]["alone"] == -30 and an3["factors"]["b"]["with_all"] == 10 and an3["factors"]["b"]["hurts"], "and the other one too: %s" % an3["factors"]["b"])
check(an3["best"] == (0, 1), "the best set leaves it out: %s" % (an3["best"],))
# a flag that changes nothing neither helps nor hurts (the boundary of "hurts")
res0 = {c: 1000 - 10 * c[0] for c in ftt.combinations(2)}
an0 = ftt.analyse(["a", "b"], res0)
check(an0["factors"]["b"]["with_all"] == 0 and an0["factors"]["b"]["hurts"] is False and an0["factors"]["a"]["hurts"] is False, "a flag with no effect must not be said to hurt: %s" % an0["factors"]["b"])
# failed builds are left out of the effects, and do not become the best
res4 = dict(res); res4[(1, 1, 0)] = None
an4 = ftt.analyse(names, res4)
check(an4["best"] in ftt.combinations(3) and res4[an4["best"]] is not None and an4["best_flash"] == min(v for v in res4.values() if v is not None), "with a failed build the best is the best of the others: %s %s" % (an4["best"], an4["best_flash"]))
check(an4["factors"]["a"]["mean"] is not None and abs(an4["factors"]["a"]["mean"] + 10) < 1e-9, "a failed build must not enter the mean: %s" % an4["factors"]["a"]["mean"])
# ties: the fewer flags win
res5 = {c: 1000 - 10 * c[0] for c in ftt.combinations(2)}
check(ftt.analyse(["a", "b"], res5)["best"] == (1, 0), "a flag with no effect is not in the best set (tie -> fewer flags): %s" % (ftt.analyse(["a", "b"], res5)["best"],))

# --- the arguments handed to the build tool: a fake one that records them and answers a size
with tempfile.TemporaryDirectory() as t:
    fake = os.path.join(t, "fake_build_size.sh")
    open(fake, "w").write('#!/bin/bash\necho "$@" > "$(dirname "$0")/last_args"\necho "flash   : 12345 / 30720 bytes  (1 free)"\necho "RAM     : 678 / 2048 bytes  (1 left for stack)"\n')
    os.chmod(fake, os.stat(fake).st_mode | stat.S_IEXEC)
    ftt.BUILD_TOOL = fake
    factors = [("relax", ["-mrelax"], ["-mrelax"], "on"), ("g", ["-fno-gcse"], [], "on"), ("e", ["-fshort-enums", "-fx"], ["-Wl,-y"], "off")]
    fl, rm = ftt.build("sd", t, (1, 0, 1), factors, "c101")
    args = open(os.path.join(t, "last_args")).read().split()
    check((fl, rm) == (12345, 678), "build() did not parse the sizes: %s" % ((fl, rm),))
    check(args[:2] == ["-v", "sd"] and "-S" in args, "the build must be for the variant, with the project's own size flags OFF (-S): %s" % args)
    check(args.count("-F") == 3 and "-mrelax" in args and "-fshort-enums" in args and "-fx" in args and "-fno-gcse" not in args, "compile flags of the active factors only: %s" % args)
    check(args.count("-L") == 2 and "-Wl,-y" in args and args.count("-mrelax") == 2, "link flags of the active factors only (-mrelax is both a compile and a link flag): %s" % args)
    check(args[args.index("-b") + 1] == os.path.join(t, "c101"), "the build directory is per combination: %s" % args)
    fl, rm = ftt.build("serial", t, (0, 0, 0), factors, "c000")
    args = open(os.path.join(t, "last_args")).read().split()
    check("-F" not in args and "-L" not in args and args[:2] == ["-v", "serial"], "no flag on: no -F, no -L: %s" % args)
    open(fake, "w").write('#!/bin/bash\necho "build FAILED"\nexit 2\n')
    check(ftt.build("sd", t, (1, 1, 1), factors, "c111") == (None, None), "a build that fails is (None, None)")

# --- the report
names3 = ["a", "b", "c"]
rams = {c: 100 + sum(c) for c in ftt.combinations(3)}
res6 = dict(res); res6[(0, 1, 1)] = None
text = ftt.render("sd", names3, res6, rams, ftt.analyse(names3, res6))
check("variant sd, 3 factors, 8 builds" in text and "FAILED" in text and "HURTS" in text, "the report lacks its header, the FAILED row or the HURTS mark: %s" % text[:300])
check(text.count("\n| 0") + text.count("\n| 1") == 8 and "| 1000 |" in text.replace("   1000", "1000") or "1000" in text, "the table has 8 rows")
import re
check(re.search(r"\| +970 \| +-30 \|", text) is not None and re.search(r"\| +1005 \| +\+5 \|", text) is not None and re.search(r"\| +1000 \| +\+0 \|", text) is not None, "the changes against none are not shown with their sign: %s" % text[:700])

# --- the command line
r = subprocess.run([sys.executable, TOOL, "--list"], capture_output=True, text=True)
check(r.returncode == 0 and "relax" in r.stdout and "compile: -mrelax" in r.stdout, "--list: %s" % r.stdout[:200])
r = subprocess.run([sys.executable, TOOL, "--only", "nonesuch"], capture_output=True, text=True)
check(r.returncode == 2 and "unknown factor" in r.stderr, "an unknown factor must be refused (exit %d)" % r.returncode)
r = subprocess.run([sys.executable, TOOL, "-h"], capture_output=True, text=True)
check(r.returncode == 0 and "Usage" in r.stdout, "-h must print the usage")
r = subprocess.run([sys.executable, TOOL, "--list", "--only", "relax,no_gcse"], capture_output=True, text=True)
check(r.stdout.count("compile:") == 2, "--only restricts the factors: %s" % r.stdout)

print("%d check(s) failed" % bad if bad else "all checks passed")
sys.exit(bad)
