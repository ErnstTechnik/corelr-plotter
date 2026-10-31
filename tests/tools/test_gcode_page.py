#!/usr/bin/env python3
#
# test_gcode_page.py
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
"""test_gcode_page.py -- is the GCode page of the documentation (docs/doxygen/gcode.md) what the firmware's parser does? (REQ-CoreLR-D-Build-0018)

The page lists, per image (`\\if serial` / `\\if sd` blocks, and what is outside them for both), tables of rows `| `a line of GCode` | `ok` | what it does |`. This test
builds tests/host/gcode_probe.c against the firmware's real GCodeParser.c, Motion.c and Trig.c for each image (-D CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_<SD|SERIAL>, as the firmware build
passes it) and checks, for each image:
  - every row: the parser answers what the page says (`ok`, `end` for a program end, `error:N`), the line being run from a fresh state;
  - completeness: the G and M words that the parser recognises (every number 0..99, tried alone and with an origin word and with a move, since `G10` and `G53` are refused alone) are exactly
    the G and M words of the `ok` / `end` rows, and the letters it recognises (A..Z) are exactly the letters of those rows: a word that the firmware gains or loses, and that the page does not
    follow, fails here;
  - the status table (`| `error:20` | @ref GCODEPARSER_ERR_UNSUPPORTED | ... |`): each macro exists in GCodeParser.h with that number, and every `error:N` of a row has a line there.
Two parts, as for test_doxygen.py: the self-test (always, no compiler: fabricated pages and a fabricated probe, one defect at a time) and the check of the real page (needs `gcc -m32`, like the host tests).

Usage:   test_gcode_page.py [-h] [--page FILE]
         --page FILE   the page to check (default: docs/doxygen/gcode.md)
Exit status: the number of failed checks (0 = all pass).
"""
import argparse, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
VARIANTS = ("serial", "sd")
ANSWER = re.compile(r"^(ok|end|error:\d+)$")
CODE = re.compile(r"^`([^`]+)`$")
FIRST_CODE = re.compile(r"`([^`]+)`")
IF = re.compile(r"^\\if (serial|sd)\s*$")
ENDIF = re.compile(r"^\\endif\s*$")
WORD = re.compile(r"(?<![A-Za-z])([GM])(\d+)(?![\d.])")
LETTER = re.compile(r"(?<![A-Za-z])([A-Z])-?\d")
STATUS_MACRO = re.compile(r"#define\s+(GCODEPARSER_ERR_\w+)\s+(\d+)u")
WORD_TEMPLATES = ("{w}", "{w} L20 P1 X0 Y0", "{w} G1 X0 Y0")     # G10 and G53 are refused alone; an unsupported word is refused whatever surrounds it


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def table_rows(text, variant):
    """-> (rows, status): the (line, answer) rows and the (number, macro) status rows of that image: what is outside any \\if block, and the block of that image."""
    rows, status = [], []
    active = [None]                                     # the \if stack: None = both images
    for raw in text.splitlines():
        m = IF.match(raw)
        if m:
            active.append(m.group(1))
            continue
        if ENDIF.match(raw):
            active.pop()
            continue
        if active[-1] not in (None, variant) or not raw.startswith("|"):
            continue
        c = cells(raw)
        if len(c) < 3 or not CODE.match(c[0]):
            continue
        first = FIRST_CODE.search(c[1])
        if c[1].startswith("@ref "):
            status.append((c[0].strip("`"), c[1][len("@ref "):].strip()))
        elif first and ANSWER.match(first.group(1)):
            rows.append((c[0].strip("`"), first.group(1)))
    return rows, status


def recognised(run_probe, variant):
    """-> (the G and M words, the letters) that the parser knows: not refused as unsupported (error:20) in any of the contexts."""
    lines, owners = [], []
    for kind, n in [("G", n) for n in range(100)] + [("M", n) for n in range(100)]:
        for t in WORD_TEMPLATES:
            lines.append(t.format(w="%s%d" % (kind, n)))
            owners.append("%s%d" % (kind, n))
    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    for c in letters:
        lines.append("%s3" % c)
        owners.append(c)
    answers = run_probe(variant, lines)
    known = set(o for o, a in zip(owners, answers) if a != "error:20")
    return set(o for o in known if len(o) > 1), set(o for o in known if len(o) == 1)


def page_words(rows):
    """-> (the G and M words, the letters) of the accepted rows (`ok`, `end`)."""
    words, letters = set(), set()
    for line, answer in rows:
        if answer in ("ok", "end"):
            words |= set("%s%d" % (k, int(n)) for k, n in WORD.findall(line))
            letters |= set(LETTER.findall(line))
    return words, letters


def check_variant(text, variant, run_probe, header):
    """-> the problems of the page for one image."""
    problems = []
    rows, status = table_rows(text, variant)
    if not rows:
        return ["%s: no row in the page" % variant]
    answers = run_probe(variant, [line for line, _ in rows])
    for (line, want), got in zip(rows, answers):
        if got != want:
            problems.append("%s: `%s` is answered %s, the page says %s" % (variant, line, got, want))
    words, letters = recognised(run_probe, variant)
    shown_words, shown_letters = page_words(rows)
    for w in sorted(words - shown_words):
        problems.append("%s: the parser knows %s, no accepted row of the page shows it" % (variant, w))
    for w in sorted(shown_words - words):
        problems.append("%s: an accepted row shows %s, which the parser does not know" % (variant, w))
    for c in sorted(letters - shown_letters):
        problems.append("%s: the parser knows the letter %s, no accepted row of the page shows it" % (variant, c))
    for c in sorted(shown_letters - letters):
        problems.append("%s: an accepted row shows the letter %s, which the parser does not know" % (variant, c))
    macros = dict(STATUS_MACRO.findall(header))
    listed = {}
    for code, macro in status:
        number = code[len("error:"):] if code.startswith("error:") else code
        listed[number] = macro
        if macro not in macros:
            problems.append("%s: the status table names %s, which GCodeParser.h does not define" % (variant, macro))
        elif macros[macro] != number:
            problems.append("%s: the status table gives %s as error:%s, GCodeParser.h says %s" % (variant, macro, number, macros[macro]))
    for line, answer in rows:
        if answer.startswith("error:") and answer[len("error:"):] not in listed:
            problems.append("%s: `%s` is answered %s, which the status table does not list" % (variant, line, answer))
    return problems


def check_page(text, header, run_probe):
    problems = []
    for variant in VARIANTS:
        problems += check_variant(text, variant, run_probe, header)
    return problems


def build_probe(variant, out_dir):
    """-> the path of the probe built for that image, or raises RuntimeError with the compiler's message."""
    exe = os.path.join(out_dir, "gcode_probe_" + variant)
    fw = os.path.join(REPO, "firmware", "src")
    cmd = ["gcc", "-m32", "-fpack-struct=1", "-O1", "-w", "-D", "CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_%s" % variant.upper(),
           "-I", os.path.join(REPO, "tests", "host", "shim"), "-I", fw, "-I", os.path.join(REPO, "firmware", "config"),
           os.path.join(REPO, "tests", "host", "gcode_probe.c"), os.path.join(fw, "GCodeParser.c"), os.path.join(fw, "Motion.c"), os.path.join(fw, "Trig.c"),
           "-lm", "-o", exe]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError("cannot build the probe for %s: %s" % (variant, done.stderr.strip()[:400]))
    return exe


def real_probe(out_dir):
    exes = {}

    def run(variant, lines):
        if variant not in exes:
            exes[variant] = build_probe(variant, out_dir)
        done = subprocess.run([exes[variant]], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=120)
        answers = done.stdout.split()
        if len(answers) != len(lines):
            raise RuntimeError("the probe answered %d lines for %d" % (len(answers), len(lines)))
        return answers
    return run


# ---------------------------------------------------------------------------------------------------------------- part 1: fabricated cases
PAGE = """\
# GCode
text

| Line | Answer | What it does |
|---|---|---|
| `G1 X10` | `ok` | a move |
| `G2 X1 I1` | `ok` | an arc |
| `G3 X1 I1` | `ok` | the other arc |
| `M3` | `ok` | pen down |
| `M30` | `end` | program end |
| `G10 L20 P1 X0 Y0` | `ok` | the origin |
| `G20` | `error:20` | inches |
| `X10` | `error:26` | no motion mode |

\\if serial
| Line | Answer | What it does |
|---|---|---|
| `G53 G0 X0` | `ok` | machine frame |
| `T3` | `ok` | a tool |
\\endif
\\if sd
| `G53 G0 X0` | `error:20` | not here |
\\endif

| Number | Macro | Meaning |
|---|---|---|
| `error:20` | @ref GCODEPARSER_ERR_UNSUPPORTED | a word that is not supported |
| `error:26` | @ref GCODEPARSER_ERR_NO_AXIS_WORDS | no axis |
"""
HEADER = "#define GCODEPARSER_ERR_UNSUPPORTED      20u\n#define GCODEPARSER_ERR_NO_AXIS_WORDS    26u\n"

# the fabricated parser: what it answers to each line, per image; a line that is not in the table is `ok` when its first word is one it knows, and an unsupported word otherwise
KNOWN = {
    "sd": {"G1 X10": "ok", "G2 X1 I1": "ok", "G3 X1 I1": "ok", "G2": "error:26", "M3": "ok", "M30": "end", "G10 L20 P1 X0 Y0": "ok", "G20": "error:20", "X10": "error:26", "G53 G0 X0": "error:20"},
    "serial": {"G1 X10": "ok", "G2 X1 I1": "ok", "G3 X1 I1": "ok", "G2": "error:26", "M3": "ok", "M30": "end", "G10 L20 P1 X0 Y0": "ok", "G20": "error:20", "X10": "error:26", "G53 G0 X0": "ok", "T3": "ok"},
}
WORDS = {"sd": {"G1", "G2", "G3", "G10", "M3", "M30"}, "serial": {"G0", "G1", "G2", "G3", "G10", "G53", "M3", "M30"}}
LETTERS = {"sd": set("GMXYILP"), "serial": set("GMXYILPT")}
NEEDS = {"G10": "L20", "G53": "G1"}          # like the real parser, which refuses G10 and G53 alone: the word is known, but only with its context


def fake_probe(extra=None):
    """A probe with the table, words and letters above (`extra`: {variant: {line: answer}, "words": {...}, "letters": {...}} added)."""
    extra = extra or {}

    def run(variant, lines):
        table = dict(KNOWN[variant])
        table.update(extra.get(variant, {}))
        words = WORDS[variant] | extra.get("words", set())
        letters = LETTERS[variant] | extra.get("letters", set())
        out = []
        for line in lines:
            m = re.match(r"^([GM])(\d+)( |$)", line)
            if line in table:
                out.append(table[line])
            elif m and "%s%d" % (m.group(1), int(m.group(2))) in words:
                need = NEEDS.get("%s%d" % (m.group(1), int(m.group(2))))
                out.append("ok" if need is None or need in line else "error:20")
            elif re.match(r"^[A-Z]3$", line) and line[0] in letters:
                out.append("ok")
            else:
                out.append("error:20")
        return out
    return run


def selftest():
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append("%s: got %r, want %r" % (label, got, want))

    # reading the page
    rows, status = table_rows(PAGE, "sd")
    expect("rows of the sd image: the common ones and its own block", [r[0] for r in rows], ["G1 X10", "G2 X1 I1", "G3 X1 I1", "M3", "M30", "G10 L20 P1 X0 Y0", "G20", "X10", "G53 G0 X0"])
    rows, status = table_rows(PAGE, "serial")
    expect("rows of the serial image: the common ones and its own block", [r[0] for r in rows], ["G1 X10", "G2 X1 I1", "G3 X1 I1", "M3", "M30", "G10 L20 P1 X0 Y0", "G20", "X10", "G53 G0 X0", "T3"])
    expect("the answers are read", [r[1] for r in rows], ["ok", "ok", "ok", "ok", "end", "ok", "error:20", "error:26", "ok", "ok"])
    expect("the status table", status, [("error:20", "GCODEPARSER_ERR_UNSUPPORTED"), ("error:26", "GCODEPARSER_ERR_NO_AXIS_WORDS")])
    expect("the header row is no row", [r for r in rows if r[0] == "Line"], [])
    expect("a row whose answer is no answer is skipped", table_rows("| `G1` | `maybe` | x |\n", "sd"), ([], []))
    expect("a row whose first cell is no code is skipped", table_rows("| G1 | `ok` | x |\n", "sd"), ([], []))
    expect("a row of two cells is skipped", table_rows("| `G1` | `ok` |\n", "sd"), ([], []))
    expect("text outside a table is not read", table_rows("`G1` `ok` x\n", "sd"), ([], []))
    expect("the answer is the first code span of its cell", table_rows("| `G1` | `ok` and `error:20` | x |\\n".replace("\\\\n", "\\n"), "sd")[0], [("G1", "ok")])

    # words and letters
    expect("the words of the accepted rows", page_words([("G1 X10", "ok"), ("M30", "end"), ("G20", "error:20"), ("G91 G1 X5", "ok")]),
           ({"G1", "M30", "G91"}, {"G", "X", "M"}))
    expect("a word with a fraction is not a word of its own", page_words([("X10.1", "ok")])[0], set())
    expect("a leading zero is the same word", page_words([("G01 X1", "ok")])[0], {"G1"})
    expect("a negative number is a letter's number", page_words([("G1 Z-1", "ok")])[1], {"G", "Z"})
    expect("a letter without a number is not a word", page_words([("G1 X", "ok")])[1], {"G"})
    w, l = recognised(fake_probe(), "serial")
    expect("what the fabricated serial parser knows", (sorted(w), sorted(l)), (sorted(WORDS["serial"]), sorted(LETTERS["serial"])))
    w, l = recognised(fake_probe(), "sd")
    expect("what the fabricated sd parser knows", (sorted(w), sorted(l)), (sorted(WORDS["sd"]), sorted(LETTERS["sd"])))

    # the whole check
    expect("a page that is true", check_page(PAGE, HEADER, fake_probe()), [])
    expect("a wrong answer", check_variant(PAGE, "sd", fake_probe({"sd": {"G20": "ok"}}), HEADER)[:1],
           ["sd: `G20` is answered ok, the page says error:20"])
    expect("the same row of the other image", check_variant(PAGE, "serial", fake_probe({"sd": {"G20": "ok"}}), HEADER), [])
    expect("a word that the parser gained", check_variant(PAGE, "sd", fake_probe({"words": {"G4"}}), HEADER), ["sd: the parser knows G4, no accepted row of the page shows it"])
    expect("a word of the page that the parser lacks", check_variant(PAGE.replace("`G20` | `error:20`", "`G21 X1` | `ok`"), "sd", fake_probe({"sd": {"G21 X1": "ok"}}), HEADER),
           ["sd: an accepted row shows G21, which the parser does not know"])
    expect("an M word that the parser gained", check_variant(PAGE, "sd", fake_probe({"words": {"M9"}}), HEADER), ["sd: the parser knows M9, no accepted row of the page shows it"])
    expect("a letter that the parser gained", check_variant(PAGE, "sd", fake_probe({"letters": {"Z"}}), HEADER), ["sd: the parser knows the letter Z, no accepted row of the page shows it"])
    expect("a letter of the page that the parser lacks", check_variant(PAGE.replace("`G20` | `error:20`", "`G1 Z5` | `ok`"), "sd", fake_probe({"sd": {"G1 Z5": "ok"}}), HEADER),
           ["sd: an accepted row shows the letter Z, which the parser does not know"])
    expect("a macro that is gone", check_variant(PAGE, "sd", fake_probe(), HEADER.replace("GCODEPARSER_ERR_NO_AXIS_WORDS", "GCODEPARSER_ERR_OTHER")),
           ["sd: the status table names GCODEPARSER_ERR_NO_AXIS_WORDS, which GCodeParser.h does not define"])
    expect("a macro with another number", check_variant(PAGE, "sd", fake_probe(), HEADER.replace("26u", "27u")),
           ["sd: the status table gives GCODEPARSER_ERR_NO_AXIS_WORDS as error:26, GCodeParser.h says 27"])
    expect("an answer that the status table does not list", check_variant(PAGE.replace("| `error:26` | @ref GCODEPARSER_ERR_NO_AXIS_WORDS | no axis |\n", ""), "sd", fake_probe(), HEADER),
           ["sd: `X10` is answered error:26, which the status table does not list"])
    expect("a page with no row", check_variant("text\n", "sd", fake_probe(), HEADER), ["sd: no row in the page"])
    expect("the other image's block is not read for this one", [x for x in check_page(PAGE, HEADER, fake_probe({"serial": {"T3": "error:20"}})) if x.startswith("sd:")], [])
    expect("a problem of the sd image only", check_page(PAGE, HEADER, fake_probe({"sd": {"G20": "ok"}}))[:1], ["sd: `G20` is answered ok, the page says error:20"])
    expect("a problem is named by its image", check_page(PAGE, HEADER, fake_probe({"serial": {"T3": "error:20"}})),
           ["serial: `T3` is answered error:20, the page says ok", "serial: an accepted row shows the letter T, which the parser does not know"])
    # the probe's own plumbing: a count that does not match
    try:
        done = subprocess.run([sys.executable, "-c", "print('ok')"], input="a\nb\n", capture_output=True, text=True)
        expect("the harness of the real probe counts its answers", len(done.stdout.split()), 1)
    except OSError as e:
        failures.append("python: %s" % e)
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--page", default=os.path.join(REPO, "docs", "doxygen", "gcode.md"))
    args = ap.parse_args(argv)
    if args.help:
        print(__doc__)
        return 0
    failures = selftest()
    with tempfile.TemporaryDirectory() as d:
        try:
            failures += ["page: " + p for p in check_page(read(args.page), read(os.path.join(REPO, "firmware", "src", "GCodeParser.h")), real_probe(d))]
        except (RuntimeError, OSError, subprocess.SubprocessError) as e:
            failures.append("page: %s" % e)
    for f in failures:
        print("FAIL", f)
    if not failures:
        print("ok: the page %s is what the parser of each image answers" % os.path.relpath(args.page, REPO))
    return len(failures)


if __name__ == "__main__":
    sys.exit(main())
