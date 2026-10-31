#!/usr/bin/env python3
#
# test_doxygen.py
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
"""test_doxygen.py -- the `doc` target (REQ-CoreLR-D-Build-0013): does it produce what the requirement says, and can this test notice when it does not?

Two parts.
  1. Always (selftest): tools/analysis/doc_warnings.py (the classifier of the Doxygen log) and the checker below are run on FABRICATED logs and output trees, one defect at
     a time, and must report exactly that defect. This is the part the mutants of tests/mutation/mutants/doxygen.txt break.
  2. With --doc-dir DIR (the output of `cmake --build <dir> --target doc`, build/doxygen by default; DIR/serial and DIR/sd, one documentation per image): the real output
     is checked, for each variant:
       - html/index.html exists, is the main page and says which variant it documents;
       - the HTML run's own log (doxygen-html.log) exists and has no markup warning either (the documentation is made by two runs of the one template: the inventory run, EXTRACT_ALL = NO, whose
         log is the one below, and the HTML run, EXTRACT_ALL = YES, so that the callers that have no comment yet are in the graphs);
       - the warning log has no MARKUP warning (a comment Doxygen could not read); undocumented items and incomplete @param lists are gaps, not errors, and are
         only counted (tools/analysis/doc_warnings.py lists them);
       - the modules of the other image have no page (SDReader in `serial`; Uart and SerialLink in `sd`), and the variant is documented and the other one is not: GCodeParser_ExecuteJog() exists in the serial image only, RedrawSdBrowser() in the SD image only; and the
         64-bit arithmetic's Motion_ISqrt(), which no shipped image contains, is in neither;
       - the diagrams of the comments (`\\dot`: the job state machine, the SD browser, the serial link) are in their pages, unless the log says that `dot` was missing
         (the target is then usable without them: those warnings are counted, not errors);
       - every link of the pages stays inside the documentation and goes to a file that is there, and nothing is loaded from the network (a documentation that is zipped and delivered
         must be complete; a link to an http address as such, such as the Doxygen credit, is information);
       - no absolute path of the machine that built it (the repository's, or any /home/...) in the HTML;
       - what applies to one image only is marked as not part of the other: the title of the page of a requirement file whose requirements all carry that image's tag
         (`*Applies to: serial image only*`), and of the protocol (the serial link's), says "not in this image" in the other documentation and only there;
       - the page of a module (Motion_8c.html) has the call graph and the caller graph of its functions (unless dot is missing), the list of what a function references and, for a variable, what references it
         (the Doxyfile's CALL_GRAPH, CALLER_GRAPH, REFERENCES_RELATION and REFERENCED_BY_RELATION);
       - the compiler's definitions of the target (F_CPU, ARDUINO..., __AVR__, __AVR_ATmega328P__) come from the one list the build reads (cmake/TargetDefs.cmake) and are given to Doxygen
         (PREDEFINED), so that it sees the code under the same #if as the compiler; nothing is copied into the Doxyfile or Firmware.cmake;
       - the layout of the EEPROM (EepromLayout.html) is a page of both documentations, never marked as one image's, and the code that cites it (Calibration_8c.html) links to it;
       - the items of the serial protocol cited in the comments (@proto{}) are links to a page that exists and holds their anchor (the code page GCodeParser_8h.html);
       - the requirements chapter (requirements.html) has three levels: it links to "Functional requirements" and "Design requirements" (requirements_functional.html, requirements_design.html),
         each of which links to one page for each file of requirements/functional and requirements/design; and the requirement links of a code page (Motion_8h.html: @req{} in its comments) point to a page that exists and holds
         the anchor (REQ-CoreLR-D-Build-0019);
       - the GCode page (gcode.html, REQ-CoreLR-D-Build-0018) is the page of this image: "Only in this image" in `serial`, "Not in this image" in `sd`;
       - the hardware chapter (hardware.html): it shows the wiring pictures and the two pictures of the generators, all copied into html/, the SD module's picture in the SD
         documentation only; the wiring pictures still carry their Fritzing credit, and the page names the licence (CC BY-SA 3.0) and each author of the parts;
       - the gaps (undocumented items, incomplete @param lists) are those of docs/doxygen/gaps_<variant>.txt: a new one fails, a fixed one still listed fails (the list can only
         shrink), unless the file was written by another Doxygen version than the one that ran: then the differences are printed as notes;
     and DIR/index.html links to both.
  3. With --package-dir DIST (with --doc-dir and --package-version): the release packages of the target `doc_package` (REQ-CoreLR-D-Build-0016): one corelr-<version>-<variant>-api.zip
     per image that holds, under a directory of the same name, exactly the files of that image's html/ (index.html included, the warning log not), and a SHA256SUMS that
     lists each zip once with its true checksum.

Usage:   test_doxygen.py [-h] [--doc-dir DIR] [--doxygen-version V] [--package-dir DIST --package-version V]
Exit status: the number of failed checks (0 = all pass).
"""
import argparse, contextlib, glob, hashlib, importlib.util, io, os, re, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# per variant: (page, symbol, why) that must be IN the documentation, i.e. in that page, as a documented member; and the ones that must NOT be
SERIAL_ONLY = ("GCodeParser_8h.html", "GCodeParser_ExecuteJog", "declared under `#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL`: the serial image only")
SD_ONLY = ("LcdJoystickUI_8cpp.html", "RedrawSdBrowser", "defined under `#if LCDJOYSTICKUI_HAS_SD`: the SD image only")
# the pages of the modules of the other image: left out (their headers are not guarded by `#if`, cmake/Doxygen.cmake excludes them)
# "pages": third-party pages that must be there (the Arduino core is in both images; the SPI library and sdfat in the SD image only: third_party/CMakeLists.txt)
# the hardware chapter (docs/doxygen/hardware.md): the wiring pictures embed Fritzing parts (CC BY-SA 3.0: the credit must travel with the pictures and name their authors), the
# SD module's picture is the SD image's only
# the GCode page (docs/doxygen/gcode.md, whose tables tests/tools/test_gcode_page.py checks against the parser): it is there, and it is the page of this image (its \\if blocks work)
GCODE_PAGE = "gcode.html"
# the requirements chapter (docs/doxygen/requirements.md and the files of requirements/): the page is there, and a requirement cited as @req{} in a comment is a link to an anchor that exists
REQUIREMENTS_PAGE = "requirements.html"
# three levels: Requirements -> Functional requirements | Design requirements -> one page for each file of requirements/functional | requirements/design (their id is the file's name)
TITLE_DIV = re.compile(r'<div class="title">(.*?)</div>', re.S)
NOT_HERE = "not in this image"
REQ_PAGES = (("requirements_functional.html", "functional"), ("requirements_design.html", "design"))
CITING_PAGE = "Motion_8h.html"
# the items of the serial protocol cited as @proto{} / @protosec{} are links to the labels that the input filter gives the protocol's definitions
PROTO_CITING_PAGE = "GCodeParser_8h.html"
# the layout of the EEPROM (docs/protocol/EepromLayout.md) is a page of both documentations, and the code that cites it (@docref{}) links to it
EEPROM_PAGE = "EepromLayout.html"
EEPROM_CITING_PAGE = "Calibration_8c.html"
# what a function and a variable show (maintainer's request, 2026-10-29): the graph of the calls that a function makes and the graph of its callers (both need dot), the functions and variables
# that it references, and, for a variable, the functions that reference it
XREF_PAGE = "Motion_8c.html"
XREF_TEXTS = (("call graph for this function", True, "the call graph of a function"), ("caller graph for this function", True, "the caller graph of a function"), ("References", False, "the list of what a function references"), ("Referenced by", False, "the list of what references a variable"))
PROTO_LINK = re.compile(r'href="([^"#]*)#(proto-[A-Za-z0-9-]+)"')
REQ_LINK = re.compile(r'href="([^"#]+)#(REQ-CoreLR-[FD]-[A-Za-z0-9]+-\d{4})"')
HARDWARE_PAGE = "hardware.html"
WIRING_COMMON = ("overview.svg", "motors.svg", "pen_servo.svg", "joystick.svg", "lcd_i2c.svg")
OTHER_PICTURES = ("Geometry.svg", "Tension.svg")
HARDWARE_CREDITS = ("Fritzing", "CC BY-SA", "Althaus", "Brendan Howell", "Stefan Hermann", "SparkFun", "Richard Bruneau", "Knorig")

VARIANTS = {
    "serial": {"present": (SERIAL_ONLY,), "absent": (SD_ONLY,), "pages": ("Arduino_8h.html",),
               "no_pages": ("SDReader_8h.html", "SDReader_8cpp.html", "Sd2Card_8h.html", "SPI_8h.html"),
               "wiring": WIRING_COMMON, "no_wiring": ("sd_card.svg",), "gcode": "Only in this image", "no_gcode": "Not in this image"},
    "sd": {"present": (SD_ONLY,), "absent": (SERIAL_ONLY,), "pages": ("Arduino_8h.html", "Sd2Card_8h.html", "SPI_8h.html"),
           "no_pages": ("Uart_8h.html", "Uart_8c.html", "SerialLink_8h.html", "SerialLink_8c.html"),
           "wiring": WIRING_COMMON + ("sd_card.svg",), "no_wiring": (), "gcode": "Not in this image", "no_gcode": "Only in this image"},
}
# the vendored code each image reads (cmake/Doxygen.cmake), and the .dox files that group it (docs/doxygen/)
THIRD_PARTY_CORE = ("third_party/arduino-core/cores/arduino", "third_party/arduino-core/variants/eightanaloginputs")
THIRD_PARTY_DIRS = {"serial": THIRD_PARTY_CORE, "sd": THIRD_PARTY_CORE + ("third_party/arduino-core/libraries/SPI/src", "third_party/sdfat")}
THIRD_PARTY_DOX = {"serial": ("third_party.dox",), "sd": ("third_party.dox", "third_party_sd.dox")}
# in neither variant
NOT_SHIPPED = (
    ("Motion_8h.html", "Motion_ISqrt", "declared under `#if CORELR_MATH == CORELR_MATH_64`: the 64-bit arithmetic, which no shipped image uses"),
)
MAIN_PAGE_TEXT = "API reference"
# `} __attribute__((packed)) Entry_t;` (Stepper.c): without `__attribute__(x)=` in PREDEFINED Doxygen made a structure called __attribute__ and lost the name Entry_t
KEYWORD_PAGES = ("struct____attribute____.html",)
STRUCT_PAGES = (("structEntry__t.html", "Entry_t, which follows an __attribute__"),)
# per variant: the pages that hold a `\dot` diagram of that image's code (grbl_corelr.ino's job state machine; the serial link; the SD browser)
GRAPH_PAGES = {"serial": ("grbl__corelr_8ino.html", "SerialLink_8c.html"), "sd": ("grbl__corelr_8ino.html", "LcdJoystickUI_8cpp.html")}
GRAPH_MARK = "dot_inline_dotgraph"


def load_tool():
    spec = importlib.util.spec_from_file_location("doc_warnings", os.path.join(REPO, "tools", "analysis", "doc_warnings.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def load_req_tool():
    spec = importlib.util.spec_from_file_location("req_refs", os.path.join(REPO, "tools", "analysis", "req_refs.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def documented(text, symbol):
    """Is `symbol` a documented MEMBER of that page (its "Function Documentation" heading), not just named in somebody's description?"""
    return re.search(r'<h2 class="memtitle">(?:(?!</h2>).)*\b%s\b[^<]*</h2>' % re.escape(symbol), text, re.S) is not None


def member(symbol):
    return '<h2 class="memtitle"><span class="permalink"><a href="#a1">&#9670;&#160;</a></span>%s()</h2>' % symbol


LINK_ATTR = re.compile(r'(?:href|src|data)="([^"]*)"')
REMOTE_RESOURCE = re.compile(r'<(?:script|img|iframe|object|embed|source)\b[^>]*\b(?:src|data)="https?://|<link\b[^>]*\bhref="https?://', re.I)
EXTERNAL_LINK = re.compile(r"(?:https?:|mailto:|javascript:|data:)", re.I)


def link_problems(html):
    """-> the problems of the links of the pages of `html` (a documentation that is zipped and delivered must be complete): a link that leaves the directory (`..`, an absolute path, a
    `file:` URL, any other scheme than http, https, mailto), one that goes to a file that is not there, and a page, script, picture or style sheet that is loaded from the network.
    Links to an http(s) address as such (the Doxygen credit, the licence of a picture) are information, not part of the documentation."""
    problems = []
    root = os.path.realpath(html)
    for here, _, files in os.walk(html):
        for name in sorted(files):
            if not name.endswith(".html"):
                continue
            page = os.path.relpath(os.path.join(here, name), html)
            text = read(os.path.join(here, name))
            if REMOTE_RESOURCE.search(text):
                problems.append("%s loads a resource from the network" % page)
            for u in sorted(set(LINK_ATTR.findall(text))):
                if EXTERNAL_LINK.match(u):
                    continue
                path = u.split("#")[0].split("?")[0]
                if not path:
                    continue
                target = os.path.realpath(os.path.join(here, path))
                if re.match(r"[A-Za-z][A-Za-z0-9+.-]*:", path) or not (target == root or target.startswith(root + os.sep)):     # (an absolute path is outside the root too)
                    problems.append("%s has a link that leaves the documentation: %s" % (page, u))
                elif not os.path.exists(target):
                    problems.append("%s has a dead link: %s" % (page, u))
    return problems


def check_doc_dir(doc_dir, tool, variant, repo=REPO):
    """-> the list of problems (empty = fine) of the output of one variant: <doc_dir>/html/ and <doc_dir>/doxygen-warnings.log."""
    problems = []
    html = os.path.join(doc_dir, "html")
    index = os.path.join(html, "index.html")
    if not os.path.isfile(index):
        problems.append("no html/index.html")
    elif MAIN_PAGE_TEXT not in read(index):
        problems.append("html/index.html is not the main page (no '%s')" % MAIN_PAGE_TEXT)
    elif "%s variant" % variant not in read(index):
        problems.append("html/index.html does not say that it documents the %s variant" % variant)
    log = os.path.join(doc_dir, "doxygen-warnings.log")
    skipped = 0
    if not os.path.isfile(log):
        problems.append("no doxygen-warnings.log")
    else:
        with open(log, encoding="utf-8", errors="replace") as f:
            for module, cls, where, text in tool.parse(f):
                if cls == "nodot":
                    skipped += 1
                if cls == "markup":
                    problems.append("markup warning (%s) %s: %s" % (module, where, text.replace("\n", " ").strip()))
    hlog = os.path.join(doc_dir, "doxygen-html.log")
    if not os.path.isfile(hlog):
        problems.append("no doxygen-html.log (the warnings of the HTML run, which documents every member)")
    else:
        with open(hlog, encoding="utf-8", errors="replace") as f:
            for module, cls, where, text in tool.parse(f):
                if cls == "markup":
                    problems.append("markup warning of the HTML run (%s) %s: %s" % (module, where, text.replace("\n", " ").strip()))
    for page, symbol, why in VARIANTS[variant]["present"]:
        p = os.path.join(html, page)
        if not os.path.isfile(p):
            problems.append("%s is missing (%s)" % (page, symbol))
        elif not documented(read(p), symbol):
            problems.append("%s is not documented in %s: %s" % (symbol, page, why))
    for page, symbol, why in VARIANTS[variant]["absent"] + NOT_SHIPPED:
        p = os.path.join(html, page)
        if os.path.isfile(p) and documented(read(p), symbol):
            problems.append("%s is documented in %s but must not be: %s" % (symbol, page, why))
    hw = os.path.join(html, HARDWARE_PAGE)
    if not os.path.isfile(hw):
        problems.append("%s is missing (the hardware chapter)" % HARDWARE_PAGE)
    else:
        text = read(hw)
        for credit in HARDWARE_CREDITS:
            if credit not in text:
                problems.append("%s does not credit '%s' (the wiring pictures are CC BY-SA 3.0: the credit and the authors must be on the page)" % (HARDWARE_PAGE, credit))
        for picture in VARIANTS[variant]["wiring"] + OTHER_PICTURES:
            if 'data="%s"' % picture not in text:
                problems.append("%s does not show %s" % (HARDWARE_PAGE, picture))
        for picture in VARIANTS[variant]["no_wiring"]:
            if 'data="%s"' % picture in text:
                problems.append("%s shows %s, which belongs to the other image" % (HARDWARE_PAGE, picture))
    for picture in VARIANTS[variant]["wiring"] + OTHER_PICTURES:
        path = os.path.join(html, picture)
        if not os.path.isfile(path):
            problems.append("%s is not in html/ (a picture of the hardware chapter)" % picture)
        elif picture in VARIANTS[variant]["wiring"] and "Fritzing" not in read(path):
            problems.append("%s has lost its Fritzing credit (CC BY-SA 3.0 obliges it)" % picture)
    for picture in VARIANTS[variant]["no_wiring"]:
        if os.path.exists(os.path.join(html, picture)):
            problems.append("%s is in html/ but belongs to the other image" % picture)
    gc = os.path.join(html, GCODE_PAGE)
    if not os.path.isfile(gc):
        problems.append("%s is missing (the GCode page)" % GCODE_PAGE)
    else:
        text = read(gc)
        if VARIANTS[variant]["gcode"] not in text:
            problems.append("%s has no '%s' section: it is not the page of the %s image" % (GCODE_PAGE, VARIANTS[variant]["gcode"], variant))
        if VARIANTS[variant]["no_gcode"] in text:
            problems.append("%s has a '%s' section, which belongs to the other image" % (GCODE_PAGE, VARIANTS[variant]["no_gcode"]))
    if not os.path.isfile(os.path.join(html, REQUIREMENTS_PAGE)):
        problems.append("%s is missing (the requirements chapter)" % REQUIREMENTS_PAGE)
    if os.path.isfile(os.path.join(html, REQUIREMENTS_PAGE)):
        top = read(os.path.join(html, REQUIREMENTS_PAGE))
        for page, sub in REQ_PAGES:
            if 'href="%s"' % page not in top:
                problems.append("%s does not link to %s (the requirements hierarchy)" % (REQUIREMENTS_PAGE, page))
            if not os.path.isfile(os.path.join(html, page)):
                problems.append("%s is missing (the requirements hierarchy)" % page)
                continue
            level = read(os.path.join(html, page))
            for path in sorted(glob.glob(os.path.join(repo, "requirements", sub, "*.md"))):
                stem = os.path.splitext(os.path.basename(path))[0]
                if 'href="%s.html"' % stem not in level:
                    problems.append("%s does not link to the page of %s" % (page, stem))
                elif not os.path.isfile(os.path.join(html, stem + ".html")):
                    problems.append("%s.html is missing (the page of requirements/%s/%s.md)" % (stem, sub, stem))
    # what applies to the other image only is marked as not part of this one: the title of a page whose requirements all carry the tag of the other image (and the protocol, the serial link's)
    if os.path.isdir(os.path.join(repo, "requirements")):
        images = dict(load_req_tool().requirement_images(repo))
        if os.path.isfile(os.path.join(repo, "docs", "protocol", "SerialProtocol.md")):
            images["SerialProtocol"] = "serial"
        if os.path.isfile(os.path.join(repo, "docs", "protocol", "EepromLayout.md")):
            images["EepromLayout"] = None                                # the position ring and the calibration exist in both images: never marked
        for stem, image in sorted(images.items()):
            pp = os.path.join(html, stem + ".html")
            if not os.path.isfile(pp):
                continue                                              # said above for the requirement files
            m = TITLE_DIV.search(read(pp))
            marked = m is not None and NOT_HERE in m.group(1)
            want = image is not None and image != variant
            if want and not marked:
                problems.append("%s.html: it applies to the %s image only, its title must say that it is not part of this image" % (stem, image))
            elif marked and not want:
                problems.append("%s.html: its title says that it is not part of this image, and it is" % stem)
    citing = os.path.join(html, CITING_PAGE)
    if os.path.isfile(citing):
        links = REQ_LINK.findall(read(citing))
        if not links:
            problems.append("%s has no link to a requirement (@req{} in its comments)" % CITING_PAGE)
        for target, ident in links[:5]:
            tp = os.path.join(html, target)
            if not os.path.isfile(tp):
                problems.append("%s links to %s, which is missing" % (CITING_PAGE, target))
            elif 'id="%s"' % ident not in read(tp):
                problems.append("%s links to %s#%s, and that page has no such anchor" % (CITING_PAGE, target, ident))
    xp = os.path.join(html, XREF_PAGE)
    if not os.path.isfile(xp):
        problems.append("%s is missing (a module's source page)" % XREF_PAGE)
    else:
        text = read(xp)
        for needle, needs_dot, what in XREF_TEXTS:
            if needle not in text and not (needs_dot and skipped):
                problems.append("%s lacks %s ('%s')" % (XREF_PAGE, what, needle))
    if not os.path.isfile(os.path.join(html, EEPROM_PAGE)):
        problems.append("%s is missing (the layout of the EEPROM)" % EEPROM_PAGE)
    ec = os.path.join(html, EEPROM_CITING_PAGE)
    if os.path.isfile(ec) and 'href="%s"' % EEPROM_PAGE not in read(ec):
        problems.append("%s has no link to %s (@docref{} in its comments)" % (EEPROM_CITING_PAGE, EEPROM_PAGE))
    pc = os.path.join(html, PROTO_CITING_PAGE)
    if os.path.isfile(pc):
        plinks = PROTO_LINK.findall(read(pc))
        if not plinks:
            problems.append("%s has no link to the protocol (@proto{} in its comments)" % PROTO_CITING_PAGE)
        for target, ident in plinks[:5]:
            tp = os.path.join(html, target or PROTO_CITING_PAGE)
            if not os.path.isfile(tp):
                problems.append("%s links to %s, which is missing" % (PROTO_CITING_PAGE, target))
            elif 'id="%s"' % ident not in read(tp):
                problems.append("%s links to %s#%s, and that page has no such anchor" % (PROTO_CITING_PAGE, target, ident))
    for page in KEYWORD_PAGES:
        if os.path.exists(os.path.join(html, page)):
            problems.append("%s exists: a compiler keyword taken for a structure (`__attribute__(x)=` in PREDEFINED)" % page)
    for page, why in STRUCT_PAGES:
        if not os.path.isfile(os.path.join(html, page)):
            problems.append("%s is missing (%s)" % (page, why))
    for page in VARIANTS[variant]["pages"]:
        if not os.path.isfile(os.path.join(html, page)):
            problems.append("%s is missing (third-party code of this image)" % page)
    for page in VARIANTS[variant]["no_pages"]:
        if os.path.exists(os.path.join(html, page)):
            problems.append("%s is documented but belongs to the other image" % page)
    if skipped == 0:
        for page in GRAPH_PAGES[variant]:
            p = os.path.join(html, page)
            if os.path.isfile(p) and GRAPH_MARK not in read(p):
                problems.append("%s has no diagram (a \\dot of the comments), and the log does not say that dot was missing" % page)
            elif not os.path.isfile(p):
                problems.append("%s is missing (it holds a diagram)" % page)
    found = link_problems(html)
    problems += found[:8]
    if len(found) > 8:
        problems.append("... and %d more problems with links" % (len(found) - 8))
    leaked = 0
    for root, _, files in os.walk(html):
        for name in files:
            if name.endswith((".html", ".js", ".css", ".svg", ".xml")):
                text = read(os.path.join(root, name))
                if repo in text or "/home/" in text:
                    leaked += 1
                    if leaked <= 3:
                        problems.append("an absolute path of the build machine in %s" % os.path.relpath(os.path.join(root, name), html))
    if leaked > 3:
        problems.append("... and %d more files with an absolute path" % (leaked - 3))
    if os.path.isdir(html):
        problems += graph_problems(html, repo)
        problems += protocol_page_problems(html)
    return problems


def doxygen_name(filename):
    """-> how Doxygen spells a file name in the names of its pages and pictures: `_` doubled, `.` written `_8`."""
    return filename.replace("_", "__").replace(".", "_8")


def graph_problems(html, repo):
    """-> the problems of the call and caller graphs of one output: each is an SVG (the maintainer's setting, 2026-10-31), and none of ours is CUT -- Doxygen draws a node of a graph that stops at the
    limit with a red frame, which says that not every call is shown (DOT_GRAPH_MAX_NODES, docs/doxygen/Doxyfile.in). The graphs of the vendored code are not ours to fix: a `main()` that reaches the
    whole Arduino core can exceed any limit."""
    third = set()
    for root, _, files in os.walk(os.path.join(repo, "third_party")):
        third.update(doxygen_name(x) + "_" for x in files)
    problems = []
    cut = []
    for name in sorted(os.listdir(html)):
        if "graph" not in name:
            continue
        if name.endswith((".png", ".gif", ".jpg")):
            problems.append("%s: a graph that is not an SVG" % name)
        elif name.endswith("graph.svg") and 'stroke="red"' in read(os.path.join(html, name)) and not any(name.startswith(x) for x in third):
            cut.append(name)
    problems += ["%s: a call graph that was cut (a red frame): raise DOT_GRAPH_MAX_NODES in docs/doxygen/Doxyfile.in" % x for x in cut[:5]]
    if len(cut) > 5:
        problems.append("... and %d more graphs that were cut" % (len(cut) - 5))
    return problems


def protocol_page_problems(html):
    """-> the problems of the page of the serial protocol: a requirement cited in its short form (`D-GCodeParser-0013`) or by the name of a file (`REQ-CoreLR-D-LcdI2c`) that is not a link. The
    input filter writes them in full and links them (tools/analysis/req_refs.py --protocol); one that is plain text slipped through. (A relative citation, `-0006`, with no context is left plain on
    purpose: it is the file's owner's to write in full, `req_refs.py --unresolved`.)"""
    path = os.path.join(html, "SerialProtocol.html")
    if not os.path.isfile(path):
        return []
    text = re.sub(r"<a\b[^>]*>.*?</a>", " ", read(path), flags=re.S)
    text = re.sub(r"<[^>]*>", " ", text)
    problems = []
    for found in sorted(set(re.findall(r"(?<![\w-])[FD]-[A-Za-z0-9]+-\d{4}(?![\w-])", text))):
        problems.append("SerialProtocol.html: %s is cited and is not a link" % found)
    for found in sorted(set(re.findall(r"REQ-CoreLR-[FD]-[A-Za-z0-9]+(?![A-Za-z0-9]|-\d)", text))):
        problems.append("SerialProtocol.html: %s is cited and is not a link" % found)
    return problems


def check_graph_settings(repo):
    """-> the problems of the graph settings of the template: SVG, interactive (a clickable node: the maintainer's decision, 2026-10-31), and a node limit that is written, not Doxygen's default."""
    path = os.path.join(repo, "docs", "doxygen", "Doxyfile.in")
    text = read(path) if os.path.isfile(path) else ""
    def value(key):
        found = re.search(r"^%s\s*=\s*(\S+)" % key, text, re.M)
        return found.group(1) if found else None
    problems = []
    if value("DOT_IMAGE_FORMAT") != "svg":
        problems.append("the Doxyfile's DOT_IMAGE_FORMAT is not svg")
    if value("INTERACTIVE_SVG") != "YES":
        problems.append("the Doxyfile's INTERACTIVE_SVG is not YES (the maintainer asked for clickable graphs)")
    nodes = value("DOT_GRAPH_MAX_NODES")
    if nodes is None or not nodes.isdigit() or int(nodes) < 250:
        problems.append("the Doxyfile's DOT_GRAPH_MAX_NODES is not written, or below 250 (the largest graph of ours, loop(), has about 210 nodes)")
    if value("MAX_DOT_GRAPH_DEPTH") != "0":
        problems.append("the Doxyfile's MAX_DOT_GRAPH_DEPTH is not 0 (a depth limit cuts graphs: the maintainer wants every call shown)")
    return problems


def ungrouped(repo, variant):
    """-> the vendored files Doxygen reads for that image that no `@file <path>` block of the .dox files puts in a group."""
    text = ""
    for name in THIRD_PARTY_DOX[variant]:
        path = os.path.join(repo, "docs", "doxygen", name)
        if os.path.isfile(path):
            text += read(path)
    missing = []
    for d in THIRD_PARTY_DIRS[variant]:
        full = os.path.join(repo, d)
        for name in sorted(os.listdir(full)) if os.path.isdir(full) else ():
            if name.endswith((".h", ".c", ".cpp")) and "@file %s/%s\n" % (d, name) not in text:
                missing.append("%s/%s" % (d, name))
    return missing


def check_gaps(doc_dir, tool, variant, repo, version, notes):
    """-> the problems (a new gap, a gap that is gone, no baseline) of one variant against docs/doxygen/gaps_<variant>.txt. With another Doxygen version than the file's the
    differences are appended to `notes` instead (tool.compare)."""
    baseline = os.path.join(repo, "docs", "doxygen", "gaps_%s.txt" % variant)
    log = os.path.join(doc_dir, "doxygen-warnings.log")
    if not os.path.isfile(baseline):
        return ["no docs/doxygen/gaps_%s.txt (tools/analysis/doc_warnings.py --gaps LOG > it)" % variant]
    if not os.path.isfile(log):
        return []                                             # check_doc_dir says so
    with open(log, encoding="utf-8", errors="replace") as f:
        found = tool.parse(f)
    diffs, more = tool.compare(found, read(baseline), version, repo)
    notes += ["%s: %s" % (variant, x) for x in more]
    return diffs


def check_target_defs(repo):
    """-> the problems of how Doxygen is given the compiler's definitions of the target: the list is cmake/TargetDefs.cmake, which the build (Firmware.cmake) and the documentation (Doxygen.cmake,
    the Doxyfile's PREDEFINED) both read, and nothing is copied (so that Doxygen sees the code under the same #if as the compiler)."""
    def txt(rel):
        path = os.path.join(repo, rel)
        return read(path) if os.path.isfile(path) else None
    defs, fw, dx, dfile = (txt(x) for x in ("cmake/TargetDefs.cmake", "cmake/Firmware.cmake", "cmake/Doxygen.cmake", "docs/doxygen/Doxyfile.in"))
    if None in (defs, fw, dx, dfile):
        return ["a file of the target definitions is missing (cmake/TargetDefs.cmake, Firmware.cmake, Doxygen.cmake, docs/doxygen/Doxyfile.in)"]
    problems = []
    target = re.search(r"set\(CORELR_TARGET_DEFS ([^)]*)\)", defs)
    compiler = re.search(r"set\(CORELR_TARGET_COMPILER_DEFS ([^)]*)\)", defs)
    mcu = re.search(r"set\(CORELR_MCU (\S+)\)", fw)
    if not target or "F_CPU=" not in target.group(1) or "ARDUINO_ARCH_AVR" not in target.group(1):
        problems.append("cmake/TargetDefs.cmake does not define CORELR_TARGET_DEFS with F_CPU and ARDUINO_ARCH_AVR")
    if not compiler or "__AVR__" not in compiler.group(1):
        problems.append("cmake/TargetDefs.cmake does not define CORELR_TARGET_COMPILER_DEFS with __AVR__")
    elif mcu and "__AVR_%s__" % mcu.group(1).upper() not in compiler.group(1).upper():
        problems.append("cmake/TargetDefs.cmake: CORELR_TARGET_COMPILER_DEFS does not hold the macro of the MCU %s (__AVR_%s__)" % (mcu.group(1), mcu.group(1).upper()))
    if "${CORELR_TARGET_DEFS}" not in fw:
        problems.append("cmake/Firmware.cmake does not use ${CORELR_TARGET_DEFS}")
    if "F_CPU=" in fw:
        problems.append("cmake/Firmware.cmake spells a target definition out (F_CPU=): it must come from cmake/TargetDefs.cmake")
    if "TargetDefs.cmake" not in fw or "TargetDefs.cmake" not in dx:
        problems.append("Firmware.cmake and Doxygen.cmake must both include TargetDefs.cmake")
    if not re.search(r"string\(JOIN \" \" CORELR_DOC_TARGET_DEFS \$\{CORELR_TARGET_DEFS\} \$\{CORELR_TARGET_COMPILER_DEFS\}\)", dx):
        problems.append("cmake/Doxygen.cmake does not build CORELR_DOC_TARGET_DEFS from both lists")
    predefined = re.search(r"^PREDEFINED\s*=(.*)$", dfile, re.M)
    if not predefined or "@CORELR_DOC_TARGET_DEFS@" not in predefined.group(1):
        problems.append("the Doxyfile's PREDEFINED does not hold @CORELR_DOC_TARGET_DEFS@")
    elif "F_CPU" in predefined.group(1):
        problems.append("the Doxyfile's PREDEFINED spells a target definition out (F_CPU): it must come from cmake/TargetDefs.cmake")
    return problems


def check_attribute_lines(repo):
    """-> the lines of our sources that hold a bare `__attribute__((...))` and nothing else (the attribute of the function that follows). Doxygen's code browser, which does not preprocess,
    reads such a line as a call and attributes the rest of the function to a call of ITSELF: the function then references itself in the call graph and loses its real callees (found on
    BlockShape(), 2026-10-31). The macro of SwConfig.h (CORELR_NOINLINE) does not do it, and expands to the same attribute for the compiler."""
    problems = []
    for sub_dir in ("firmware/src", "firmware/app"):
        folder = os.path.join(repo, sub_dir)
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            if not name.endswith((".c", ".cpp", ".h", ".ino")):
                continue
            for number, line in enumerate(read(os.path.join(folder, name)).splitlines(), 1):
                if re.match(r"\s*__attribute__\(\(.*\)\)\s*$", line):
                    problems.append("%s/%s:%d: a bare __attribute__ line (Doxygen would draw the next function calling itself): write CORELR_NOINLINE, or put the attribute on the line of the declaration" % (sub_dir, name, number))
    return problems


def check_all(base, tool, repo=REPO, version=None, notes=None):
    """-> the problems of both variants under `base` (each prefixed with its name) and of the landing page. With `version` (the Doxygen that wrote the logs) the gaps are
    compared with the baseline files of the repository too; the differences that are only notes (another version) are appended to `notes`."""
    problems = []
    notes = [] if notes is None else notes
    for variant in VARIANTS:
        problems += ["%s: %s" % (variant, x) for x in check_doc_dir(os.path.join(base, variant), tool, variant, repo)]
        if version is not None:
            problems += ["%s: %s" % (variant, x) for x in check_gaps(os.path.join(base, variant), tool, variant, repo, version, notes)]
        problems += ["%s: %s is read by Doxygen but not grouped in docs/doxygen/third_party*.dox" % (variant, x) for x in ungrouped(repo, variant)]
    if version is not None and os.path.isfile(os.path.join(repo, "docs", "doxygen", "Doxyfile.in")):       # (a fabricated repository of a case has no template)
        problems += check_target_defs(repo)
        problems += check_attribute_lines(repo)
        problems += check_graph_settings(repo)
    landing = os.path.join(base, "index.html")
    if not os.path.isfile(landing):
        problems.append("no index.html linking the two variants")
    else:
        text = read(landing)
        problems += ["index.html does not link to %s/html/index.html" % v for v in VARIANTS if "%s/html/index.html" % v not in text]
    return problems


def package_name(version, variant):
    return "corelr-%s-%s-api" % (version, variant)


def html_files(html):
    """-> the sorted paths (with '/') of the files under `html`, relative to it."""
    found = []
    for root, _, files in os.walk(html):
        found += [os.path.relpath(os.path.join(root, f), html).replace(os.sep, "/") for f in files]
    return sorted(found)


def check_package(dist, base, version):
    """-> the problems of the release packages in `dist` (target doc_package): per variant one corelr-<version>-<variant>-api.zip that holds, under a directory of the same
    name, exactly the files of <base>/<variant>/html (and nothing else, not the warning log); and a SHA256SUMS that names each zip once with its checksum."""
    problems = []
    sums = {}
    sums_file = os.path.join(dist, "SHA256SUMS")
    if not os.path.isfile(sums_file):
        problems.append("no SHA256SUMS")
    else:
        for line in read(sums_file).splitlines():
            parts = line.split("  ", 1)
            if len(parts) != 2:
                problems.append("SHA256SUMS: a line that is not '<hash>  <name>': %r" % line)
            elif parts[1] in sums:
                problems.append("SHA256SUMS: %s is listed twice" % parts[1])
            else:
                sums[parts[1]] = parts[0]
    expected = [package_name(version, v) + ".zip" for v in VARIANTS]
    for name in sums:
        if name not in expected:
            problems.append("SHA256SUMS lists %s, which is no package of this version" % name)
    for v in VARIANTS:
        top = package_name(version, v)
        path = os.path.join(dist, top + ".zip")
        if not os.path.isfile(path):
            problems.append("%s.zip is missing" % top)
            continue
        with open(path, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        if top + ".zip" not in sums:
            problems.append("SHA256SUMS does not list %s.zip" % top)
        elif sums[top + ".zip"] != digest:
            problems.append("SHA256SUMS: the checksum of %s.zip is not its checksum" % top)
        with zipfile.ZipFile(path) as z:
            names = sorted(n for n in z.namelist() if not n.endswith("/"))
            if any(not n.startswith(top + "/") for n in names):
                problems.append("%s.zip: an entry outside the directory %s/" % (top, top))
            inside = sorted(n[len(top) + 1:] for n in names if n.startswith(top + "/"))
            want = html_files(os.path.join(base, v, "html"))
            if inside != want:
                problems.append("%s.zip does not hold the files of %s/html: missing %s, more %s" %
                                (top, v, sorted(set(want) - set(inside))[:3], sorted(set(inside) - set(want))[:3]))
            elif "index.html" in inside and z.read(top + "/index.html") != open(os.path.join(base, v, "html", "index.html"), "rb").read():
                problems.append("%s.zip: index.html is not the one of the documentation" % top)
    return problems


# ---------------------------------------------------------------------------------------------------------------- part 1: fabricated cases
LOG = """\
firmware/src/Motion.h:186: warning: The following parameters of Motion_SetOrigin(uint8_t what, int32_t x, int32_t y) are not documented:
  parameter 'x'
  parameter 'y'
firmware/src/Motion.c:12: warning: Member Foo(void) (function) of file Motion.c is not documented.
firmware/src/Motion.c:15: warning: Member BAR (macro definition) of file Motion.c is not documented.
firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found
firmware/config/SwConfig.h:48: warning: explicit link request to 'define' could not be resolved
warning: Tag 'HTML_TIMESTAMP' at line 70 of file 'Doxyfile' has become obsolete.
         To avoid this warning please remove this line from your configuration file or upgrade it using "doxygen -u"
"""


def selftest(tool):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append("%s: got %r, want %r" % (label, got, want))

    # classification of one warning
    expect("undocumented", tool.classify("warning: Member Foo(void) (function) of file Motion.c is not documented."), "undocumented")
    expect("compound undocumented", tool.classify("warning: Compound Foo is not documented."), "undocumented")
    expect("incomplete (plural)", tool.classify("warning: The following parameters of f(int a, int b) are not documented:"), "incomplete")
    expect("incomplete (singular)", tool.classify("warning: The following parameter of f(int a) is not documented:"), "incomplete")
    expect("unknown tag", tool.classify("warning: Unsupported xml/html tag <tag> found"), "markup")
    expect("unresolved link", tool.classify("warning: explicit link request to 'if' could not be resolved"), "markup")
    expect("a documented thing that is ignored", tool.classify("warning: found documented #define EEPROM_MAGIC but ignoring it because ENABLE_PREPROCESSING is NO."), "markup")
    expect("param that names nothing", tool.classify("warning: argument 'z' of command @param is not found in the argument list of f(int a)"), "markup")

    # the whole log: classes, modules, continuation lines
    found = tool.parse(LOG.splitlines(True))
    expect("number of warnings", len(found), 6)
    expect("classes", [c for _, c, _, _ in found], ["incomplete", "undocumented", "undocumented", "markup", "markup", "markup"])
    expect("modules", [m for m, _, _, _ in found], ["Motion", "Motion", "Motion", "Uart", "SwConfig", "(doxygen)"])
    expect("the place of a warning", found[0][2], "firmware/src/Motion.h:186")
    expect("the parameter lines stay with their warning", "parameter 'y'" in found[0][3], True)
    expect("a blank line is not a warning", len(tool.parse(["\n", "  \n"])), 0)
    summary, total = tool.table(found)
    expect("totals", (total["undocumented"], total["incomplete"], total["markup"]), (2, 1, 3))
    expect("the table has a row per module", [l.split()[0] for l in summary.splitlines()], ["module", "(doxygen)", "Motion", "SwConfig", "Uart", "TOTAL"])
    rows = {l.split()[0]: l.split()[1:] for l in summary.splitlines()[1:]}
    expect("Motion row", rows.get("Motion"), ["2", "1", "0", "0", "0"])
    expect("Uart row", rows.get("Uart"), ["0", "0", "1", "0", "0"])
    expect("TOTAL row", rows.get("TOTAL"), ["2", "1", "3", "0", "0"])
    ext = tool.parse(["third_party/sdfat/SdFat.h:10: warning: documented symbol 'X' was not declared or defined.\n",
                      "/abs/build/third_party/arduino-core/cores/arduino/WString.h:4: warning: no uniquely matching class member found for\n",
                      "   void String::remove(unsigned int index)\n", "Possible candidates:\n",
                      "firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found\n"])
    expect("a warning in third_party is external, whatever it says", [(m, c) for m, c, _, _ in ext], [("(third_party)", "external"), ("(third_party)", "external"), ("Uart", "markup")])
    expect("the lines that follow a warning belong to it", ("void String::remove" in ext[1][3], "candidates" in ext[1][3]), (True, True))
    expect("a third-party warning is not a markup error", tool.table(ext)[1]["markup"], 1)
    expect("a path that only contains the word is not third-party", tool.parse(["firmware/src/my_third_party_x.c:1: warning: Unsupported xml/html tag <X> found\n"])[0][1], "markup")
    expect("a path under another directory's third_party is third-party", tool.parse(["/x/y/third_party/sdfat/SdFat.h:1: warning: Unsupported xml/html tag <X> found\n"])[0][1], "external")
    expect("a line that starts with 'warn' but is not a warning continues the one before",
           len(tool.parse(["firmware/src/Uart.h:7: warning: x\n", "warnings that follow are candidates\n"])), 1)
    expect("a config message is a warning of its own", [m for m, _, _, _ in tool.parse(["firmware/src/Uart.h:7: warning: x\n", "warning: Tag 'A' is obsolete\n"])], ["Uart", "(doxygen)"])
    expect("a missing dot is a class of its own", tool.classify("warning: ignoring \\dot command because HAVE_DOT is not set"), "nodot")
    expect("another ignored diagram command", tool.classify("warning: ignoring \\dotfile command because HAVE_DOT is not set"), "nodot")
    nodot_log = ["firmware/app/grbl_corelr.ino:128: warning: ignoring \\dot command because HAVE_DOT is not set\n"] * 2
    nd_summary, nd_total = tool.table(tool.parse(nodot_log))
    expect("the skipped diagrams are counted", (nd_total["nodot"], nd_total["markup"]), (2, 0))
    expect("the TOTAL row carries them", nd_summary.splitlines()[-2].split()[-2], "2")
    expect("and the table says why", "2 diagram(s) skipped" in nd_summary and "graphviz" in nd_summary, True)
    expect("a table without skipped diagrams has no such line", "skipped" in tool.table(found)[0], False)

    # the command line: --check returns the number of markup warnings, nothing else
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, "w.log")
        with open(log, "w") as f:
            f.write(LOG)
        out = os.path.join(d, "out.txt")
        stdout, sys.stdout = sys.stdout, open(out, "w")
        try:
            rc_plain = tool.main([log])
            rc_check = tool.main(["--check", log])
            rc_list = tool.main(["--list", "markup", "--check", log])
            with contextlib.redirect_stderr(open(os.devnull, "w")):
                rc_missing = tool.main([os.path.join(d, "absent.log")])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        expect("without --check the status is 0", rc_plain, 0)
        expect("--check counts the markup warnings", rc_check, 3)
        expect("--list with --check", rc_list, 3)
        expect("an unreadable log is a usage error", rc_missing, 2)
        listing = read(out)
        expect("--list markup shows the markup warnings only", ("Unsupported xml/html tag" in listing, "Member Foo" in listing), (True, False))
        clean = os.path.join(d, "clean.log")
        with open(clean, "w") as f:
            f.write("firmware/src/Motion.c:12: warning: Member Foo(void) (function) of file Motion.c is not documented.\n")
        stdout, sys.stdout = sys.stdout, open(os.devnull, "w")
        try:
            rc_clean = tool.main(["--check", clean])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        expect("gaps alone pass --check", rc_clean, 0)
        ext_only = os.path.join(d, "ext.log")
        with open(ext_only, "w") as f:
            f.write("third_party/sdfat/SdFat.h:10: warning: documented symbol 'X' was not declared or defined.\n")
        stdout, sys.stdout = sys.stdout, open(os.devnull, "w")
        try:
            rc_ext = tool.main(["--check", ext_only])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        expect("third-party warnings alone pass --check", rc_ext, 0)

    # the version of the Doxygen that runs: asked to the program, "" when there is none or it fails
    with tempfile.TemporaryDirectory() as d:
        def fake(name, body):
            path = os.path.join(d, name)
            with open(path, "w") as f:
                f.write("#!/bin/sh\n" + body + "\n")
            os.chmod(path, 0o755)
            return path
        which = tool.shutil.which
        try:
            tool.shutil.which = lambda name: None
            expect("no doxygen: no version", tool.doxygen_version(), "")
            tool.shutil.which = lambda name: fake("ok", "echo '1.2.3 (abcdef)'")
            expect("the first word of what doxygen prints", tool.doxygen_version(), "1.2.3")
            tool.shutil.which = lambda name: fake("bad", "echo 9.9.9; exit 3")
            expect("a doxygen that fails: no version", tool.doxygen_version(), "")
            tool.shutil.which = lambda name: fake("mute", "true")
            expect("a doxygen that prints nothing: no version", tool.doxygen_version(), "")
        finally:
            tool.shutil.which = which

    # the inventory of the gaps and its ratchet
    def run_main(argv):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(io.StringIO()):
            rc = tool.main(argv)
        return rc, buf.getvalue()

    gap_log = [
        "/the/repo/firmware/src/Motion.h:186: warning: The following parameters of Motion_SetOrigin(uint8_t what, int32_t x) are not documented:\n",
        "  parameter 'x'\n",
        "/the/repo/firmware/src/Motion.c:12: warning: Member Foo(void) (function) of file Motion.c is not documented.\n",
        "/the/repo/firmware/src/Motion.c:99: warning: Member Foo(void) (function) of file Motion.c is not documented.\n",
        "/the/repo/firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found\n",
        "/the/repo/third_party/sdfat/SdFat.h:10: warning: Member Z (function) of file SdFat.h is not documented.\n",
    ]
    mine = tool.parse(gap_log)
    expect("the inventory: sorted, from the root, no line number, no markup, nothing external, a doubled item twice",
           tool.gaps(mine, "/the/repo"),
           ["firmware/src/Motion.c: Member Foo(void) (function) of file Motion.c is not documented.",
            "firmware/src/Motion.c: Member Foo(void) (function) of file Motion.c is not documented.",
            "firmware/src/Motion.h: The following parameters of Motion_SetOrigin(uint8_t what, int32_t x) are not documented: parameter 'x'"])
    expect("a path outside the root is kept as it is", tool.relative("/other/Motion.c", "/the/repo"), "/other/Motion.c")
    expect("a path that only starts like the root is outside it", tool.relative("/the/repository/Motion.c", "/the/repo"), "/the/repository/Motion.c")
    expect("a relative path is kept", tool.relative("firmware/src/Motion.c", "/the/repo"), "firmware/src/Motion.c")
    expect("without a root nothing is stripped", tool.relative("/the/repo/Motion.c", None), "/the/repo/Motion.c")
    text = "# doxygen 1.15.0\n# a comment\n\n" + "\n".join(tool.gaps(mine, "/the/repo")) + "\n"
    expect("the baseline is read back", tool.read_baseline(text), ("1.15.0", tool.gaps(mine, "/the/repo")))
    expect("a baseline without a version line", tool.read_baseline("a: b\n")[0], "")
    expect("the same gaps", tool.compare(mine, text, "1.15.0", "/the/repo"), ([], []))
    more = tool.parse(gap_log + ["/the/repo/firmware/src/Stepper.c:5: warning: Member Bar (macro definition) of file Stepper.c is not documented.\n"])
    expect("a new gap fails", tool.compare(more, text, "1.15.0", "/the/repo"),
           (["new gap: firmware/src/Stepper.c: Member Bar (macro definition) of file Stepper.c is not documented."], []))
    fewer = tool.parse(gap_log[:3] + gap_log[4:])
    expect("a gap that is gone fails (one of the two Foo)", tool.compare(fewer, text, "1.15.0", "/the/repo"),
           (["gap fixed, remove it from the baseline: firmware/src/Motion.c: Member Foo(void) (function) of file Motion.c is not documented."], []))
    expect("another Doxygen version: the differences are notes", tool.compare(more, text, "1.9.1", "/the/repo"),
           ([], ["the baseline is for Doxygen 1.15.0, this log is Doxygen 1.9.1: not compared",
                 "new gap: firmware/src/Stepper.c: Member Bar (macro definition) of file Stepper.c is not documented."]))
    expect("another version and no difference: no note", tool.compare(mine, text, "1.9.1", "/the/repo"), ([], []))
    expect("a baseline with no version is another version", tool.compare(more, "a: b\n", "1.15.0", "/the/repo")[0], [])
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, "w.log")
        with open(log, "w") as f:
            f.writelines(x.replace("/the/repo/", "") for x in gap_log)      # the command line strips the real repository's root: relative paths here
        rc, out = run_main(["--gaps", "--doxygen-version", "9.9.9", log])
        expect("--gaps writes the file", (rc, out.splitlines()[0], len(out.splitlines()), out.splitlines()[-1].startswith("firmware/src/Motion.h: ")), (0, "# doxygen 9.9.9", 5, True))
        base = os.path.join(d, "gaps.txt")
        with open(base, "w") as f:
            f.write(out)
        rc, out2 = run_main(["--baseline", base, "--doxygen-version", "9.9.9", log])
        expect("--baseline, the same gaps", (rc, "the gaps are those of" in out2), (0, True))
        with open(log, "a") as f:
            f.write("firmware/src/Stepper.c:5: warning: Member Bar (macro definition) of file Stepper.c is not documented.\n")
        rc, out2 = run_main(["--baseline", base, "--doxygen-version", "9.9.9", log])
        expect("--baseline, a new gap: status = the number of differences", (rc, "new gap: firmware/src/Stepper.c" in out2), (1, True))
        rc, _ = run_main(["--baseline", base, "--doxygen-version", "1.0", log])
        expect("--baseline, another version: status 0", rc, 0)
        rc, _ = run_main(["--baseline", os.path.join(d, "absent.txt"), log])
        expect("--baseline, no such file: usage error", rc, 2)

        # the ratchet through the output check: a fabricated repository with its baseline, a fabricated output with its log
        r = os.path.join(d, "repo")
        os.makedirs(os.path.join(r, "docs", "doxygen"))
        out_dir = os.path.join(d, "out")
        os.makedirs(out_dir)
        with open(os.path.join(out_dir, "doxygen-warnings.log"), "w") as f:
            f.write("%s/firmware/src/Motion.c:12: warning: Member Foo(void) (function) of file Motion.c is not documented.\n" % r)
        notes = []
        expect("no baseline file", check_gaps(out_dir, tool, "sd", r, "1.15.0", notes), ["no docs/doxygen/gaps_sd.txt (tools/analysis/doc_warnings.py --gaps LOG > it)"])
        with open(os.path.join(r, "docs", "doxygen", "gaps_sd.txt"), "w") as f:
            f.write("# doxygen 1.15.0\nfirmware/src/Motion.c: Member Foo(void) (function) of file Motion.c is not documented.\n")
        expect("the gaps of the baseline", check_gaps(out_dir, tool, "sd", r, "1.15.0", notes), [])
        expect("the baseline of the other variant is not the one read", check_gaps(out_dir, tool, "serial", r, "1.15.0", notes),
               ["no docs/doxygen/gaps_serial.txt (tools/analysis/doc_warnings.py --gaps LOG > it)"])
        with open(os.path.join(out_dir, "doxygen-warnings.log"), "a") as f:
            f.write("%s/firmware/src/Motion.c:30: warning: Member Baz (macro definition) of file Motion.c is not documented.\n" % r)
        expect("a new gap", check_gaps(out_dir, tool, "sd", r, "1.15.0", notes), ["new gap: firmware/src/Motion.c: Member Baz (macro definition) of file Motion.c is not documented."])
        expect("another version: no problem, a note", (check_gaps(out_dir, tool, "sd", r, "1.9.1", notes), len(notes)), ([], 2))
        expect("the note names the variant", notes[0].startswith("sd: the baseline is for Doxygen 1.15.0"), True)
        os.remove(os.path.join(out_dir, "doxygen-warnings.log"))
        expect("no log: reported by the other check, not here", check_gaps(out_dir, tool, "sd", r, "1.15.0", notes), [])

    # the release packages, on a fabricated output
    def make_package(d, version="1.2.3", skip=None, extra=None, wrong_sum=False, drop_sum=None, dup_sum=False, outside=False, same_files=True):
        base = os.path.join(d, "doc")
        dist = os.path.join(d, "dist")
        os.makedirs(dist)
        lines = []
        for v in VARIANTS:
            html = os.path.join(base, v, "html")
            os.makedirs(os.path.join(html, "sub"))
            for rel, text in (("index.html", "main " + v), ("sub/a.svg", "svg")):
                with open(os.path.join(html, rel), "w") as f:
                    f.write(text)
            top = package_name(version, v)
            if v == skip:
                continue
            with zipfile.ZipFile(os.path.join(dist, top + ".zip"), "w") as z:
                z.writestr(top + "/", "")                         # the entry of a directory, as most zip tools write them
                z.writestr(top + "/sub/", "")
                z.writestr(top + "/index.html", "main " + v)
                if same_files:
                    z.writestr(top + "/sub/a.svg", "svg")
                if extra and v == extra[0]:
                    z.writestr(extra[1].replace("TOP", top), "x")
                if outside and v == "sd":
                    z.writestr("stray.txt", "x")
            with open(os.path.join(dist, top + ".zip"), "rb") as f:
                digest = hashlib.sha256(f.read()).hexdigest()
            if wrong_sum and v == "sd":
                digest = "0" * 64
            if v != drop_sum:
                lines.append("%s  %s.zip" % (digest, top))
                if dup_sum and v == "sd":
                    lines.append("%s  %s.zip" % (digest, top))
        with open(os.path.join(dist, "SHA256SUMS"), "w") as f:
            f.write("\n".join(lines) + "\n")
        return dist, base

    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d)
        expect("a good package", check_package(dist, base, "1.2.3"), [])
        expect("another version is another name", check_package(dist, base, "1.2.4"),
               ["SHA256SUMS lists corelr-1.2.3-serial-api.zip, which is no package of this version", "SHA256SUMS lists corelr-1.2.3-sd-api.zip, which is no package of this version",
                "corelr-1.2.4-serial-api.zip is missing", "corelr-1.2.4-sd-api.zip is missing"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, skip="sd")
        expect("a missing zip", check_package(dist, base, "1.2.3"), ["corelr-1.2.3-sd-api.zip is missing"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, wrong_sum=True)
        expect("a wrong checksum", check_package(dist, base, "1.2.3"), ["SHA256SUMS: the checksum of corelr-1.2.3-sd-api.zip is not its checksum"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, drop_sum="serial")
        expect("a zip that SHA256SUMS forgets", check_package(dist, base, "1.2.3"), ["SHA256SUMS does not list corelr-1.2.3-serial-api.zip"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, dup_sum=True)
        expect("a zip listed twice", check_package(dist, base, "1.2.3"), ["SHA256SUMS: corelr-1.2.3-sd-api.zip is listed twice"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d)
        os.remove(os.path.join(dist, "SHA256SUMS"))
        expect("no SHA256SUMS", check_package(dist, base, "1.2.3")[:1], ["no SHA256SUMS"])
        with open(os.path.join(dist, "SHA256SUMS"), "w") as f:
            f.write("not a checksum line\n")
        expect("a line that is no checksum", check_package(dist, base, "1.2.3")[0], "SHA256SUMS: a line that is not '<hash>  <name>': 'not a checksum line'")
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, outside=True)
        expect("an entry outside the top directory", check_package(dist, base, "1.2.3")[0],
               "corelr-1.2.3-sd-api.zip: an entry outside the directory corelr-1.2.3-sd-api/")
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, extra=("serial", "TOP/doxygen-warnings.log"))
        expect("a file that is not in the documentation (the warning log)", check_package(dist, base, "1.2.3"),
               ["corelr-1.2.3-serial-api.zip does not hold the files of serial/html: missing [], more ['doxygen-warnings.log']"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d, same_files=False)
        expect("a file of the documentation that the zip lacks", check_package(dist, base, "1.2.3"),
               ["corelr-1.2.3-serial-api.zip does not hold the files of serial/html: missing ['sub/a.svg'], more []",
                "corelr-1.2.3-sd-api.zip does not hold the files of sd/html: missing ['sub/a.svg'], more []"])
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d)
        with open(os.path.join(base, "sd", "html", "index.html"), "w") as f:
            f.write("main of another build")
        expect("a zip of another build", check_package(dist, base, "1.2.3"), ["corelr-1.2.3-sd-api.zip: index.html is not the one of the documentation"])

    # the command line: the package is looked for under the version it is given
    with tempfile.TemporaryDirectory() as d:
        dist, base = make_package(d)
        def cli(version):
            return output_failures(argparse.Namespace(doc_dir=base, doxygen_version="1.15.0", package_dir=dist, package_version=version), tool, [])
        right = [x for x in cli("1.2.3") if x.startswith("package: ")]
        wrong = [x for x in cli("1.2.4") if x.startswith("package: ")]
        expect("the package version of the command line", (right, len(wrong)), ([], 4))
        expect("no package asked, none checked", [x for x in output_failures(argparse.Namespace(doc_dir=base, doxygen_version="1.15.0", package_dir=None, package_version=None), tool, [])
                                                  if x.startswith("package: ")], [])

    # the output checker, one defect at a time on a fabricated tree of one variant
    def tree(d, variant="serial", index=True, log="", pages=None, extra=None, req_files=(), hlog=""):
        os.makedirs(os.path.join(d, "html"), exist_ok=True)
        if index:
            with open(os.path.join(d, "html", "index.html"), "w") as f:
                f.write("<h1>CoreLR plotter firmware: API reference</h1><p>the %s variant</p>" % variant)
        if log is not None:
            with open(os.path.join(d, "doxygen-warnings.log"), "w") as f:
                f.write(log)
        if hlog is not None:
            with open(os.path.join(d, "doxygen-html.log"), "w") as f:
                f.write(hlog)
        serial, sd = variant == "serial", variant == "sd"
        dot = '<img src="dot_inline_dotgraph_1.svg" alt="">'
        plink = '<a href="proto_page.html#proto-FW-03">FW-03</a>'
        good = {"GCodeParser_8h.html": (member("GCodeParser_ExecuteJog") if serial else "<p>GCodeParser_ExecuteJog() is serial only</p>") + plink,
                "proto_page.html": '<a id="proto-FW-03"></a>',
                EEPROM_PAGE: "<h1>EEPROM layout</h1>",
                EEPROM_CITING_PAGE: '<a href="%s">layout</a>' % EEPROM_PAGE,
                XREF_PAGE: " ".join(n for n, _, _ in XREF_TEXTS),
                "LcdJoystickUI_8cpp.html": member("RedrawSdBrowser") + dot if sd else "<p>RedrawSdBrowser() is SD only</p>",
                "grbl__corelr_8ino.html": dot,
                "Motion_8h.html": member("Motion_Hypot") + "<p>see Motion_ISqrt()'s note</p>" + '<a class="el" href="req_page.html#REQ-CoreLR-F-Motion-0001">x</a>',
                "req_page.html": '<a id="REQ-CoreLR-F-Motion-0001"></a>',
                REQUIREMENTS_PAGE: "".join('<a href="%s">x</a>' % page for page, _ in REQ_PAGES),
                "requirements_functional.html": "".join('<a href="%s.html">x</a>' % r for r in req_files if "-F-" in r),
                "requirements_design.html": "".join('<a href="%s.html">x</a>' % r for r in req_files if "-D-" in r),
                "structEntry__t.html": "<p>Entry_t</p>",
                GCODE_PAGE: "<h2>%s</h2>" % VARIANTS[variant]["gcode"],
                HARDWARE_PAGE: " ".join(HARDWARE_CREDITS) + "".join('<object data="%s"></object>' % x for x in VARIANTS[variant]["wiring"] + OTHER_PICTURES)}
        for picture in VARIANTS[variant]["wiring"]:
            good[picture] = "<svg><metadata>created with parts from Fritzing</metadata></svg>"
        for picture in OTHER_PICTURES:
            good[picture] = "<svg/>"
        if serial:
            good["SerialLink_8c.html"] = dot
        for page in VARIANTS[variant]["pages"]:
            good[page] = "<p>third-party code</p>"
        good.update(pages or {})
        for name, text in good.items():
            with open(os.path.join(d, "html", name), "w") as f:
                f.write(text)
        with open(os.path.join(d, "html", "dot_inline_dotgraph_1.svg"), "w") as f:       # the picture that the pages with a diagram show
            f.write("<svg/>")
        for r in req_files:
            if r + ".html" not in (pages or {}):                      # a page that the case gives is written already
                with open(os.path.join(d, "html", r + ".html"), "w") as f:
                    f.write("<p>%s</p>" % r)
        for name, text in (extra or {}).items():
            with open(os.path.join(d, "html", name), "w") as f:
                f.write(text)

    def problems_of(variant="serial", **kw):
        with tempfile.TemporaryDirectory() as d:
            tree(d, variant, **kw)
            return check_doc_dir(d, tool, variant, repo="/the/repo")

    def first(p, word):
        return (len(p), word in p[0] if p else False)

    for v in VARIANTS:
        expect("a good %s tree" % v, problems_of(v), [])
    expect("gaps in the log are not problems", problems_of(log="firmware/src/Motion.c:12: warning: Member Foo(void) (function) of file Motion.c is not documented.\n"), [])
    p = problems_of(index=False)
    expect("no index", [x for x in p if "index.html" in x], ["no html/index.html"])
    p = problems_of(extra={"index.html": "<h1>something else</h1>"})
    expect("an index that is not the main page", [x for x in p if "index.html" in x], ["html/index.html is not the main page (no '%s')" % MAIN_PAGE_TEXT])
    p = problems_of(extra={"index.html": "<h1>API reference</h1><p>the sd variant</p>"})
    expect("the index of the other variant", first(p, "serial variant"), (1, True))
    p = problems_of(log=None)
    expect("no log", p, ["no doxygen-warnings.log"])
    expect("a compiler keyword taken for a structure", problems_of(extra={"struct____attribute____.html": "<p>x</p>"}),
           ["struct____attribute____.html exists: a compiler keyword taken for a structure (`__attribute__(x)=` in PREDEFINED)"])
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", "structEntry__t.html"))
        expect("the structure that follows an attribute is lost", check_doc_dir(d, tool, "sd", repo="/the/repo"),
               ["structEntry__t.html is missing (Entry_t, which follows an __attribute__)"])
    # the requirements chapter and the links to it
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", REQUIREMENTS_PAGE))
        expect("no requirements chapter", check_doc_dir(d, tool, "sd", repo="/the/repo"), ["requirements.html is missing (the requirements chapter)"])
    expect("a code page that cites no requirement", problems_of("sd", pages={"Motion_8h.html": member("Motion_Hypot")}), ["Motion_8h.html has no link to a requirement (@req{} in its comments)"])
    expect("a link to a page that is missing", problems_of("sd", pages={"Motion_8h.html": '<a href="gone.html#REQ-CoreLR-F-Motion-0001">x</a>'}),
           ["Motion_8h.html links to gone.html, which is missing", "Motion_8h.html has a dead link: gone.html#REQ-CoreLR-F-Motion-0001"])
    expect("a link to an anchor that is not there", problems_of("sd", pages={"req_page.html": "<p>no anchor</p>"}),
           ["Motion_8h.html links to req_page.html#REQ-CoreLR-F-Motion-0001, and that page has no such anchor"])
    expect("a second link that is bad", problems_of("sd", pages={"Motion_8h.html": '<a href="req_page.html#REQ-CoreLR-F-Motion-0001">x</a><a href="gone.html#REQ-CoreLR-F-Motion-0002">y</a>'}),
           ["Motion_8h.html links to gone.html, which is missing", "Motion_8h.html has a dead link: gone.html#REQ-CoreLR-F-Motion-0002"])
    expect("a link to a requirement of the design files", problems_of("sd", pages={"Motion_8h.html": '<a href="req_page.html#REQ-CoreLR-D-LcdI2c-0001">x</a>'}),
           ["Motion_8h.html links to req_page.html#REQ-CoreLR-D-LcdI2c-0001, and that page has no such anchor"])
    # the links of the pages: the documentation is zipped and delivered, it must be complete
    def links(files):
        with tempfile.TemporaryDirectory() as d:
            for name, text in files.items():
                path = os.path.join(d, "html", name)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write(text)
            return link_problems(os.path.join(d, "html"))
    expect("links inside the directory", links({"a.html": '<a href="b.html#x">b</a><img src="p.svg"><a href="#top">t</a><a href="sub/c.html">c</a>', "b.html": "", "p.svg": "<svg/>", "sub/c.html": '<a href="../a.html">up</a>'}), [])
    expect("a link that leaves the directory", links({"a.html": '<a href="../../docs/x.svg">x</a>'}), ["a.html has a link that leaves the documentation: ../../docs/x.svg"])
    expect("a link that goes up and comes back is inside", links({"a.html": '<a href="sub/../b.html">x</a>', "b.html": ""}), [])
    expect("an absolute path", links({"a.html": '<a href="/usr/share/x.html">x</a>'}), ["a.html has a link that leaves the documentation: /usr/share/x.html"])
    expect("a file: URL", links({"a.html": '<a href="file:line">x</a>'}), ["a.html has a link that leaves the documentation: file:line"])
    expect("another scheme", links({"a.html": '<a href="ftp://x/y">x</a>'}), ["a.html has a link that leaves the documentation: ftp://x/y"])
    expect("a dead link", links({"a.html": '<a href="gone.html#x">x</a>'}), ["a.html has a dead link: gone.html#x"])
    expect("a dead picture", links({"a.html": '<object data="gone.svg"></object>'}), ["a.html has a dead link: gone.svg"])
    expect("a link with a query string", links({"a.html": '<a href="b.html?q=1">x</a>', "b.html": ""}), [])
    expect("an http link is information", links({"a.html": '<a href="https://www.doxygen.org/index.html">d</a><a href="http://x.y">e</a><a href="mailto:a@b.c">m</a>'}), [])
    expect("a script from the network", links({"a.html": '<script src="https://cdn.example/x.js"></script>'}), ["a.html loads a resource from the network"])
    expect("a style sheet from the network", links({"a.html": '<link rel="stylesheet" href="http://cdn.example/x.css">'}), ["a.html loads a resource from the network"])
    expect("a picture from the network", links({"a.html": '<img src="https://x/y.png">'}), ["a.html loads a resource from the network"])
    expect("a page in a subdirectory is read, and its links are relative to it", links({"sub/c.html": '<a href="d.html">d</a><a href="../nope.html">n</a>', "sub/d.html": ""}), ["sub/c.html has a dead link: ../nope.html"])
    expect("a page that is not .html is not read", links({"a.js": '<a href="gone.html">x</a>'}), [])
    expect("the same bad link on a page is told once", links({"a.html": '<a href="gone.html">x</a><a href="gone.html">y</a>'}), ["a.html has a dead link: gone.html"])
    many = "".join('<a href="gone%d.html">x</a>' % i for i in range(11))
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd", extra={"many.html": many})
        got = [x for x in check_doc_dir(d, tool, "sd", repo="/the/repo") if "dead link" in x or "more problems" in x]
        expect("eleven dead links: eight are told, then a count", (len(got), got[-1]), (9, "... and 3 more problems with links"))

    # what a function and a variable show
    def xref(**kw):
        return [x for x in problems_of("sd", **kw) if "lacks" in x and XREF_PAGE in x]
    expect("the page of a module has the call graph and the two lists", xref(), [])
    expect("no call graph", xref(pages={XREF_PAGE: "caller graph for this function References Referenced by"}), ["Motion_8c.html lacks the call graph of a function ('call graph for this function')"])
    expect("no caller graph", xref(pages={XREF_PAGE: "call graph for this function References Referenced by"}), ["Motion_8c.html lacks the caller graph of a function ('caller graph for this function')"])
    expect("neither graph", xref(pages={XREF_PAGE: "References Referenced by"}), ["Motion_8c.html lacks the call graph of a function ('call graph for this function')", "Motion_8c.html lacks the caller graph of a function ('caller graph for this function')"])
    expect("no list of what a function references", xref(pages={XREF_PAGE: "call graph for this function caller graph for this function Referenced by"}), ["Motion_8c.html lacks the list of what a function references ('References')"])
    expect("no list of what references a variable", xref(pages={XREF_PAGE: "call graph for this function caller graph for this function References"}), ["Motion_8c.html lacks the list of what references a variable ('Referenced by')"])
    expect("without dot there is no graph to ask for", xref(pages={XREF_PAGE: "References Referenced by"}, log="x.c:1: warning: ignoring \\dot command because HAVE_DOT is not set\n"), [])
    expect("without dot the lists are still asked for", xref(pages={XREF_PAGE: "nothing"}, log="x.c:1: warning: ignoring \\dot command because HAVE_DOT is not set\n"),
           ["Motion_8c.html lacks the list of what a function references ('References')", "Motion_8c.html lacks the list of what references a variable ('Referenced by')"])
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", XREF_PAGE))
        expect("the module page is missing", [x for x in check_doc_dir(d, tool, "sd", repo="/the/repo") if XREF_PAGE in x], ["Motion_8c.html is missing (a module's source page)"])

    # the layout of the EEPROM
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", EEPROM_PAGE))
        expect("no EEPROM layout page", [x for x in check_doc_dir(d, tool, "sd", repo="/the/repo") if EEPROM_PAGE in x and "missing" in x], ["EepromLayout.html is missing (the layout of the EEPROM)"])
    expect("a code page that does not link to the layout", [x for x in problems_of("sd", pages={EEPROM_CITING_PAGE: "<p>x</p>"}) if "EepromLayout" in x],
           ["Calibration_8c.html has no link to EepromLayout.html (@docref{} in its comments)"])
    with tempfile.TemporaryDirectory() as r, tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(r, "docs", "protocol"))
        os.makedirs(os.path.join(r, "requirements"))
        with open(os.path.join(r, "docs", "protocol", "EepromLayout.md"), "w") as f:
            f.write("# E\n")
        with tempfile.TemporaryDirectory() as d2:
            tree(d2, "sd", pages={"EepromLayout.html": '<div class="title">EEPROM layout</div>'})
            expect("the EEPROM layout is in both images: not marked in the sd one either", [x for x in check_doc_dir(d2, tool, "sd", repo=r) if "EepromLayout" in x], [])
        tree(d, "serial", pages={"EepromLayout.html": '<div class="title">EEPROM layout (sd image only: not in this image)</div>'})
        expect("the EEPROM layout is never marked as the other image's", [x for x in check_doc_dir(d, tool, "serial", repo=r) if "EepromLayout" in x],
               ["EepromLayout.html: its title says that it is not part of this image, and it is"])
    # the two runs of Doxygen: the inventory (doxygen-warnings.log, read above) and the HTML run (doxygen-html.log, which documents every member)
    expect("no log of the HTML run", problems_of(hlog=None), ["no doxygen-html.log (the warnings of the HTML run, which documents every member)"])
    expect("a markup warning of the HTML run", problems_of(hlog="firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found\n"),
           ["markup warning of the HTML run (Uart) firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found"])
    expect("a gap in the log of the HTML run is no problem", problems_of(hlog="firmware/src/Motion.c:12: warning: The following parameters of f(int a) are not documented:\n  parameter 'a'\n"), [])
    expect("a markup warning of the inventory run is told as before", problems_of(log="firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found\n"),
           ["markup warning (Uart) firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found"])

    # the page of the protocol: no requirement cited without a link
    def proto_run(page):
        with tempfile.TemporaryDirectory() as d:
            tree(d, "serial", pages={"SerialProtocol.html": page})
            return [x for x in check_doc_dir(d, tool, "serial", repo="/the/repo") if x.startswith("SerialProtocol.html") and "is cited and is not a link" in x]
    link = '<a class="el" href="REQ-CoreLR-D-GCodeParser.html#REQ-CoreLR-D-GCodeParser-0013">REQ-CoreLR-D-GCodeParser-0013</a>'
    page = '<a class="el" href="REQ-CoreLR-D-Uart.html">REQ-CoreLR-D-Uart</a>'
    expect("a protocol page whose citations are links", proto_run("<p>see " + link + " and " + page + " and F1, -0006</p>"), [])
    expect("a short citation that is plain text", proto_run("<p>see D-GCodeParser-0013 here</p>"), ["SerialProtocol.html: D-GCodeParser-0013 is cited and is not a link"])
    expect("a short citation in the text of a table cell", proto_run("<td>F-Motion-0001</td>"), ["SerialProtocol.html: F-Motion-0001 is cited and is not a link"])
    expect("a short citation told once", len(proto_run("<p>D-Uart-0001 D-Uart-0001</p>")), 1)
    expect("a short citation inside a full one is no short citation", proto_run("<p>REQ-CoreLR-D-GCodeParser-0013</p>"), [])
    expect("a file-level citation that is plain text", proto_run("<p>see REQ-CoreLR-D-LcdI2c's scope</p>"), ["SerialProtocol.html: REQ-CoreLR-D-LcdI2c is cited and is not a link"])
    expect("a full citation is not a file-level one", proto_run("<p>REQ-CoreLR-D-Uart-0007</p>"), [])
    expect("a file-level citation in a link", proto_run('<p><a href="x.html">REQ-CoreLR-D-Uart</a></p>'), [])
    expect("two links on one page", proto_run(link + " " + link + " " + page), [])
    expect("a plain citation after a link", proto_run(link + " D-Uart-0001"), ["SerialProtocol.html: D-Uart-0001 is cited and is not a link"])
    expect("a page that is not there is not checked", [x for x in problems_of("serial") if x.startswith("SerialProtocol.html")], [])

    # a fabricated repository from a dictionary of files
    def attr_repo(**files):
        r = tempfile.TemporaryDirectory()
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(r.name, rel)), exist_ok=True)
            with open(os.path.join(r.name, rel), "w") as f:
                f.write(text)
        return r

    # the call graphs: SVG, and none of ours cut
    def graph_run(files, repo="/the/repo"):
        with tempfile.TemporaryDirectory() as d:
            tree(d, "serial", pages=files)
            return [x for x in check_doc_dir(d, tool, "serial", repo=repo) if "graph" in x]
    expect("a whole graph is fine", graph_run({"Motion_8c_a1_cgraph.svg": "<svg>no frame</svg>"}), [])
    expect("a cut graph is told", graph_run({"Motion_8c_a1_cgraph.svg": '<svg><path stroke="red"/></svg>'}),
           ["Motion_8c_a1_cgraph.svg: a call graph that was cut (a red frame): raise DOT_GRAPH_MAX_NODES in docs/doxygen/Doxyfile.in"])
    expect("a cut caller graph is told too?", graph_run({"Motion_8c_a1_icgraph.svg": '<svg><path stroke="red"/></svg>'}),
           ["Motion_8c_a1_icgraph.svg: a call graph that was cut (a red frame): raise DOT_GRAPH_MAX_NODES in docs/doxygen/Doxyfile.in"])
    expect("a red frame in a picture that is not a graph is not a cut graph", graph_run({"Geometry.svg": '<svg><path stroke="red"/></svg>'}), [])
    expect("a graph in PNG is told", graph_run({"Motion_8c_a1_cgraph.png": "x"}), ["Motion_8c_a1_cgraph.png: a graph that is not an SVG"])
    with attr_repo(**{"third_party/arduino-core/cores/arduino/main.cpp": "", "third_party/other/grbl_corelr.ino": ""}) as r:
        expect("the cut graph of vendored code is not ours", graph_run({"main_8cpp_a1_cgraph.svg": '<svg><path stroke="red"/></svg>'}, repo=r), [])
        expect("a name with an underscore is spelled the Doxygen way", graph_run({"grbl__corelr_8ino_a1_cgraph.svg": '<svg><path stroke="red"/></svg>'}, repo=r), [])
        expect("a file whose name only begins like a vendored one is ours", len(graph_run({"main_8cppx_a1_cgraph.svg": '<svg><path stroke="red"/></svg>', "ma_a1_cgraph.svg": '<svg stroke="red"/>', "libmain_8cpp_a1_cgraph.svg": '<svg stroke="red"/>'}, repo=r)), 3)
    with attr_repo(**{"docs/doxygen/Doxyfile.in": "DOT_IMAGE_FORMAT = svg\nINTERACTIVE_SVG = YES\nDOT_GRAPH_MAX_NODES = 250\nMAX_DOT_GRAPH_DEPTH = 0\n"}) as r:
        expect("the graph settings of the template", check_graph_settings(r), [])
    for label, text, want in (
            ("png", "DOT_IMAGE_FORMAT = png\nINTERACTIVE_SVG = YES\nDOT_GRAPH_MAX_NODES = 250\nMAX_DOT_GRAPH_DEPTH = 0\n", "the Doxyfile's DOT_IMAGE_FORMAT is not svg"),
            ("static svg", "DOT_IMAGE_FORMAT = svg\nINTERACTIVE_SVG = NO\nDOT_GRAPH_MAX_NODES = 250\nMAX_DOT_GRAPH_DEPTH = 0\n", "the Doxyfile's INTERACTIVE_SVG is not YES (the maintainer asked for clickable graphs)"),
            ("default limit", "DOT_IMAGE_FORMAT = svg\nINTERACTIVE_SVG = YES\nMAX_DOT_GRAPH_DEPTH = 0\n", "the Doxyfile's DOT_GRAPH_MAX_NODES is not written, or below 250 (the largest graph of ours, loop(), has about 210 nodes)"),
            ("small limit", "DOT_IMAGE_FORMAT = svg\nINTERACTIVE_SVG = YES\nDOT_GRAPH_MAX_NODES = 100\nMAX_DOT_GRAPH_DEPTH = 0\n", "the Doxyfile's DOT_GRAPH_MAX_NODES is not written, or below 250 (the largest graph of ours, loop(), has about 210 nodes)"),
            ("depth", "DOT_IMAGE_FORMAT = svg\nINTERACTIVE_SVG = YES\nDOT_GRAPH_MAX_NODES = 250\nMAX_DOT_GRAPH_DEPTH = 3\n", "the Doxyfile's MAX_DOT_GRAPH_DEPTH is not 0 (a depth limit cuts graphs: the maintainer wants every call shown)")):
        with attr_repo(**{"docs/doxygen/Doxyfile.in": text}) as r:
            expect("the template: " + label, check_graph_settings(r), [want])

    # an attribute alone on its line makes Doxygen's call graph draw the function calling itself
    with attr_repo(**{"firmware/src/A.c": "CORELR_NOINLINE\nstatic void F(void)\n{\n}\nstatic inline __attribute__((always_inline)) void G(void) {}\nvoid H(void) __attribute__((cold))\n{\n}\n"}) as r:
        expect("the macro, and an attribute on the line of the declaration, are fine", check_attribute_lines(r), [])
    with attr_repo(**{"firmware/src/A.c": "int x;\n__attribute__((noinline))\nstatic void F(void)\n{\n}\n"}) as r:
        expect("a bare attribute line is told, with its place", [x.split(":")[0] + ":" + x.split(":")[1] for x in check_attribute_lines(r)], ["firmware/src/A.c:2"])
    with attr_repo(**{"firmware/src/B.cpp": "  __attribute__((noinline)) \nvoid F();\n"}) as r:
        expect("an indented attribute with trailing blanks is told too", len(check_attribute_lines(r)), 1)
    with attr_repo(**{"firmware/src/B.h": "__attribute__((packed))\n", "firmware/app/x.ino": "__attribute__((noinline))\nvoid f(){}\n", "firmware/src/notes.txt": "__attribute__((noinline))\n"}) as r:
        expect("headers and the sketch are read, other files are not", len(check_attribute_lines(r)), 2)
    with attr_repo(**{"firmware/src/A.c": "__attribute__((noinline))\nvoid f(){}\n"}) as r, tempfile.TemporaryDirectory() as d:
        for v in VARIANTS:
            os.makedirs(os.path.join(d, v))
            tree(os.path.join(d, v), v)
        os.makedirs(os.path.join(r, "docs", "doxygen"))
        with open(os.path.join(r, "docs", "doxygen", "Doxyfile.in"), "w") as f:
            f.write("PREDEFINED = @CORELR_DOC_TARGET_DEFS@\n")
        with open(os.path.join(d, "index.html"), "w") as f:
            f.write('<a href="serial/html/index.html">serial</a> <a href="sd/html/index.html">sd</a>')
        expect("check_all reads the attribute lines of the repository", len([x for x in check_all(d, tool, repo=r, version="1.15.0") if "bare __attribute__" in x]), 1)
        expect("check_all without a version does not", len([x for x in check_all(d, tool, repo=r) if "bare __attribute__" in x]), 0)
        expect("check_all reads the graph settings of the repository", len([x for x in check_all(d, tool, repo=r, version="1.15.0") if "the Doxyfile's" in x]), 4)
        expect("check_all without a version does not read them", len([x for x in check_all(d, tool, repo=r) if "the Doxyfile's" in x]), 0)

    # the compiler's definitions of the target, given to Doxygen from the one list
    def target_repo(**kw):
        files = {
            "cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS F_CPU=16000000L ARDUINO=10600 ARDUINO_AVR_NANO ARDUINO_ARCH_AVR)\nset(CORELR_TARGET_COMPILER_DEFS __AVR__ __AVR_ATmega328P__=1)\n",
            "cmake/Firmware.cmake": "include(TargetDefs.cmake)\nset(CORELR_MCU atmega328p)\nset(defs ${CORELR_TARGET_DEFS} X=1)\n",
            "cmake/Doxygen.cmake": 'include(TargetDefs.cmake)\nstring(JOIN " " CORELR_DOC_TARGET_DEFS ${CORELR_TARGET_DEFS} ${CORELR_TARGET_COMPILER_DEFS})\n',
            "docs/doxygen/Doxyfile.in": "PREDEFINED = @CORELR_DOC_TARGET_DEFS@ PROGMEM=\n",
        }
        files.update(kw)
        r = tempfile.TemporaryDirectory()
        for rel, text in files.items():
            if text is not None:
                os.makedirs(os.path.dirname(os.path.join(r.name, rel)), exist_ok=True)
                with open(os.path.join(r.name, rel), "w") as f:
                    f.write(text)
        return r
    with target_repo() as r:
        expect("the target definitions come from one list", check_target_defs(r), [])
    with target_repo(**{"cmake/TargetDefs.cmake": None}) as r:
        expect("a file is missing", len(check_target_defs(r)), 1)
    with target_repo(**{"cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS X=1)\nset(CORELR_TARGET_COMPILER_DEFS __AVR__ __AVR_ATmega328P__=1)\n"}) as r:
        expect("the list lacks F_CPU", check_target_defs(r), ["cmake/TargetDefs.cmake does not define CORELR_TARGET_DEFS with F_CPU and ARDUINO_ARCH_AVR"])
    with target_repo(**{"cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS F_CPU=1)\nset(CORELR_TARGET_COMPILER_DEFS __AVR__ __AVR_ATmega328P__=1)\n"}) as r:
        expect("the list lacks the architecture", check_target_defs(r), ["cmake/TargetDefs.cmake does not define CORELR_TARGET_DEFS with F_CPU and ARDUINO_ARCH_AVR"])
    with target_repo(**{"cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS ARDUINO_ARCH_AVR)\nset(CORELR_TARGET_COMPILER_DEFS __AVR__ __AVR_ATmega328P__=1)\n"}) as r:
        expect("the list lacks the clock", check_target_defs(r), ["cmake/TargetDefs.cmake does not define CORELR_TARGET_DEFS with F_CPU and ARDUINO_ARCH_AVR"])
    with target_repo(**{"cmake/Firmware.cmake": "include(TargetDefs.cmake)\nset(CORELR_MCU atmega328p)\nset(defs X=1)\n"}) as r, tempfile.TemporaryDirectory() as d:
        for v in VARIANTS:
            os.makedirs(os.path.join(d, v))
            tree(os.path.join(d, v), v)
        with open(os.path.join(d, "index.html"), "w") as f:
            f.write('<a href="serial/html/index.html">serial</a> <a href="sd/html/index.html">sd</a>')
        expect("check_all reads the target definitions of the repository", [x for x in check_all(d, tool, repo=r, version="1.15.0") if "TargetDefs" in x or "Firmware.cmake" in x],
               ["cmake/Firmware.cmake does not use ${CORELR_TARGET_DEFS}"])
        expect("check_all without a version does not read them", [x for x in check_all(d, tool, repo=r) if "Firmware.cmake" in x], [])
    with target_repo(**{"cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS F_CPU=1 ARDUINO_ARCH_AVR)\nset(CORELR_TARGET_COMPILER_DEFS __AVR_ATmega328P__=1)\n"}) as r:
        expect("the compiler's list lacks __AVR__", check_target_defs(r), ["cmake/TargetDefs.cmake does not define CORELR_TARGET_COMPILER_DEFS with __AVR__"])
    with target_repo(**{"cmake/TargetDefs.cmake": "set(CORELR_TARGET_DEFS F_CPU=1 ARDUINO_ARCH_AVR)\nset(CORELR_TARGET_COMPILER_DEFS __AVR__)\n"}) as r:
        expect("the compiler's list lacks the macro of the MCU", check_target_defs(r),
               ["cmake/TargetDefs.cmake: CORELR_TARGET_COMPILER_DEFS does not hold the macro of the MCU atmega328p (__AVR_ATMEGA328P__)"])
    with target_repo(**{"cmake/Firmware.cmake": "include(TargetDefs.cmake)\nset(CORELR_MCU atmega328p)\nset(defs X=1)\n"}) as r:
        expect("the build does not read the list", check_target_defs(r), ["cmake/Firmware.cmake does not use ${CORELR_TARGET_DEFS}"])
    with target_repo(**{"cmake/Firmware.cmake": "include(TargetDefs.cmake)\nset(CORELR_MCU atmega328p)\nset(defs ${CORELR_TARGET_DEFS} F_CPU=16000000L)\n"}) as r:
        expect("the build spells a definition out", check_target_defs(r), ["cmake/Firmware.cmake spells a target definition out (F_CPU=): it must come from cmake/TargetDefs.cmake"])
    with target_repo(**{"cmake/Firmware.cmake": "set(CORELR_MCU atmega328p)\nset(defs ${CORELR_TARGET_DEFS})\n"}) as r:
        expect("the build does not include the list", check_target_defs(r), ["Firmware.cmake and Doxygen.cmake must both include TargetDefs.cmake"])
    with target_repo(**{"cmake/Doxygen.cmake": 'string(JOIN " " CORELR_DOC_TARGET_DEFS ${CORELR_TARGET_DEFS})\ninclude(TargetDefs.cmake)\n'}) as r:
        expect("the documentation uses only one of the lists", check_target_defs(r), ["cmake/Doxygen.cmake does not build CORELR_DOC_TARGET_DEFS from both lists"])
    with target_repo(**{"cmake/Doxygen.cmake": 'string(JOIN " " CORELR_DOC_TARGET_DEFS ${CORELR_TARGET_DEFS} ${CORELR_TARGET_COMPILER_DEFS})\n'}) as r:
        expect("the documentation does not include the list", check_target_defs(r), ["Firmware.cmake and Doxygen.cmake must both include TargetDefs.cmake"])
    with target_repo(**{"docs/doxygen/Doxyfile.in": "PREDEFINED = PROGMEM=\n"}) as r:
        expect("the Doxyfile does not take the definitions", check_target_defs(r), ["the Doxyfile's PREDEFINED does not hold @CORELR_DOC_TARGET_DEFS@"])
    with target_repo(**{"docs/doxygen/Doxyfile.in": "PREDEFINED = @CORELR_DOC_TARGET_DEFS@ F_CPU=16000000L\n"}) as r:
        expect("the Doxyfile spells a definition out", check_target_defs(r), ["the Doxyfile's PREDEFINED spells a target definition out (F_CPU): it must come from cmake/TargetDefs.cmake"])
    with target_repo(**{"docs/doxygen/Doxyfile.in": "# PREDEFINED = @CORELR_DOC_TARGET_DEFS@\nPREDEFINED = PROGMEM=\n"}) as r:
        expect("a comment is not the setting", check_target_defs(r), ["the Doxyfile's PREDEFINED does not hold @CORELR_DOC_TARGET_DEFS@"])

    # the links to the protocol
    expect("no link to the protocol", [x for x in problems_of("sd", pages={"GCodeParser_8h.html": "<p>x</p>"}) if "protocol" in x], ["GCodeParser_8h.html has no link to the protocol (@proto{} in its comments)"])
    expect("a link to a page that is missing", [x for x in problems_of("sd", pages={"GCodeParser_8h.html": '<a href="gone.html#proto-FW-03">x</a>'}) if "GCodeParser_8h.html links" in x],
           ["GCodeParser_8h.html links to gone.html, which is missing"])
    expect("a link to an anchor that is not there", [x for x in problems_of("sd", pages={"proto_page.html": "<p>no anchor</p>"}) if "GCodeParser_8h.html links" in x],
           ["GCodeParser_8h.html links to proto_page.html#proto-FW-03, and that page has no such anchor"])
    expect("a link inside the page itself", [x for x in problems_of("sd", pages={"GCodeParser_8h.html": '<a href="#proto-FW-03" id="proto-FW-03">x</a>'}) if "GCodeParser_8h.html links" in x], [])
    expect("a link to a section, a point, a decision and a measurement that are not there", [x for x in problems_of("sd", pages={"GCodeParser_8h.html": '<a href="proto_page.html#proto-4-6">a</a><a href="proto_page.html#proto-P7">b</a><a href="proto_page.html#proto-D1">c</a><a href="proto_page.html#proto-M-1">d</a>'}) if "GCodeParser_8h.html links" in x],
           ["GCodeParser_8h.html links to proto_page.html#proto-%s, and that page has no such anchor" % i for i in ("4-6", "P7", "D1", "M-1")])
    expect("a second link that is bad", [x for x in problems_of("sd", pages={"GCodeParser_8h.html": '<a href="proto_page.html#proto-FW-03">x</a><a href="proto_page.html#proto-FW-99">y</a>'}) if "GCodeParser_8h.html links" in x],
           ["GCodeParser_8h.html links to proto_page.html#proto-FW-99, and that page has no such anchor"])
    # the requirements hierarchy
    with tempfile.TemporaryDirectory() as r, tempfile.TemporaryDirectory() as d:
        for sub, stem in (("functional", "REQ-CoreLR-F-A"), ("functional", "REQ-CoreLR-F-B"), ("design", "REQ-CoreLR-D-A")):
            os.makedirs(os.path.join(r, "requirements", sub), exist_ok=True)
            with open(os.path.join(r, "requirements", sub, stem + ".md"), "w") as f:
                f.write("# x\n")
        files = ("REQ-CoreLR-F-A", "REQ-CoreLR-F-B", "REQ-CoreLR-D-A")
        tree(d, "sd", req_files=files)
        expect("the three levels are there", check_doc_dir(d, tool, "sd", repo=r), [])
        os.remove(os.path.join(d, "html", "REQ-CoreLR-F-B.html"))
        expect("the page of a requirement file is missing", check_doc_dir(d, tool, "sd", repo=r), ["REQ-CoreLR-F-B.html is missing (the page of requirements/functional/REQ-CoreLR-F-B.md)", "requirements_functional.html has a dead link: REQ-CoreLR-F-B.html"])
        with open(os.path.join(d, "html", "requirements_design.html"), "w") as f:
            f.write("")
        expect("a level that does not link to the page of a file", [x for x in check_doc_dir(d, tool, "sd", repo=r) if "REQ-CoreLR-D-A" in x],
               ["requirements_design.html does not link to the page of REQ-CoreLR-D-A"])
        with open(os.path.join(d, "html", "requirements.html"), "w") as f:
            f.write('<a href="requirements_functional.html">x</a>')
        expect("the top page does not link to the design level", [x for x in check_doc_dir(d, tool, "sd", repo=r) if x.startswith("requirements.html")],
               ["requirements.html does not link to requirements_design.html (the requirements hierarchy)"])
        os.remove(os.path.join(d, "html", "requirements_functional.html"))
        expect("a level is missing", [x for x in check_doc_dir(d, tool, "sd", repo=r) if "requirements_functional.html is missing" in x],
               ["requirements_functional.html is missing (the requirements hierarchy)"])
        expect("the levels of the hierarchy, pinned", REQ_PAGES, (("requirements_functional.html", "functional"), ("requirements_design.html", "design")))
    # what applies to one image only
    with tempfile.TemporaryDirectory() as r, tempfile.TemporaryDirectory() as d:
        def req(sub, stem, tags):
            os.makedirs(os.path.join(r, "requirements", sub), exist_ok=True)
            body = "# x\n\n"
            for n, tag in enumerate(tags, 1):
                rid = "%s-%04d" % (stem, n)
                body += "## %s -- t\n<a id=\"%s\"></a>\n%s\ntext\n\n" % (rid, rid, ("*Applies to: %s image only*\n" % tag) if tag else "")
            with open(os.path.join(r, "requirements", sub, stem + ".md"), "w") as f:
                f.write(body)
        req("design", "REQ-CoreLR-D-Uart", ["serial", "serial"])
        req("design", "REQ-CoreLR-D-SDReader", ["sd"])
        req("design", "REQ-CoreLR-D-Motion", [None, "serial"])
        os.makedirs(os.path.join(r, "docs", "protocol"))
        with open(os.path.join(r, "docs", "protocol", "SerialProtocol.md"), "w") as f:
            f.write("# P\n")
        files = ("REQ-CoreLR-D-Uart", "REQ-CoreLR-D-SDReader", "REQ-CoreLR-D-Motion")
        titles = {"sd": {"REQ-CoreLR-D-Uart": "Uart (serial image only: not in this image)", "REQ-CoreLR-D-SDReader": "SD Reader", "REQ-CoreLR-D-Motion": "Motion", "SerialProtocol": "Protocol (serial image only: not in this image)"},
                  "serial": {"REQ-CoreLR-D-Uart": "Uart", "REQ-CoreLR-D-SDReader": "SD Reader (sd image only: not in this image)", "REQ-CoreLR-D-Motion": "Motion", "SerialProtocol": "Protocol"}}
        def pages(variant, **changes):
            t = dict(titles[variant])
            t.update(changes)
            return {k + ".html": '<div class="title">%s</div>' % v for k, v in t.items()}
        for v in VARIANTS:
            shutil_d = os.path.join(d, v)
            os.makedirs(shutil_d)
            tree(shutil_d, v, req_files=files, pages=pages(v))
        def check(v, **changes):
            with tempfile.TemporaryDirectory() as dd:
                tree(dd, v, req_files=files, pages=pages(v, **changes))
                return [x for x in check_doc_dir(dd, tool, v, repo=r) if "not part of this image" in x]
        expect("the pages of a serial and an sd documentation are marked as they should", (check("sd"), check("serial")), ([], []))
        expect("a page of the other image that does not say so", check("sd", **{"REQ-CoreLR-D-Uart": "Uart"}),
               ["REQ-CoreLR-D-Uart.html: it applies to the serial image only, its title must say that it is not part of this image"])
        expect("the words in the body of the page are not the title's", check("sd", **{"REQ-CoreLR-D-Uart": "Uart</div><p>not in this image</p><div>"}),
               ["REQ-CoreLR-D-Uart.html: it applies to the serial image only, its title must say that it is not part of this image"])
        expect("the same in the serial documentation", check("serial", **{"REQ-CoreLR-D-SDReader": "SD Reader"}),
               ["REQ-CoreLR-D-SDReader.html: it applies to the sd image only, its title must say that it is not part of this image"])
        expect("a page of its own image marked as not part of it", check("sd", **{"REQ-CoreLR-D-SDReader": "SD Reader (sd image only: not in this image)"}),
               ["REQ-CoreLR-D-SDReader.html: its title says that it is not part of this image, and it is"])
        expect("a mixed page is not marked", check("sd", **{"REQ-CoreLR-D-Motion": "Motion (serial image only: not in this image)"}),
               ["REQ-CoreLR-D-Motion.html: its title says that it is not part of this image, and it is"])
        expect("the protocol is the serial image's", check("sd", **{"SerialProtocol": "Protocol"}),
               ["SerialProtocol.html: it applies to the serial image only, its title must say that it is not part of this image"])
        expect("the protocol is not marked in the serial documentation", check("serial", **{"SerialProtocol": "Protocol (serial image only: not in this image)"}),
               ["SerialProtocol.html: its title says that it is not part of this image, and it is"])
        expect("the words of the mark, pinned", NOT_HERE, "not in this image")

    # the GCode page
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", GCODE_PAGE))
        expect("no GCode page", check_doc_dir(d, tool, "sd", repo="/the/repo"), ["gcode.html is missing (the GCode page)"])
    expect("the GCode page of the other image", problems_of("serial", pages={GCODE_PAGE: "<h2>Not in this image</h2>"}),
           ["gcode.html has no 'Only in this image' section: it is not the page of the serial image", "gcode.html has a 'Not in this image' section, which belongs to the other image"])
    expect("a GCode page with both sections", problems_of("sd", pages={GCODE_PAGE: "<h2>Not in this image</h2><h2>Only in this image</h2>"}),
           ["gcode.html has a 'Only in this image' section, which belongs to the other image"])
    expect("the sections of the GCode page, pinned", (VARIANTS["serial"]["gcode"], VARIANTS["serial"]["no_gcode"], VARIANTS["sd"]["gcode"], VARIANTS["sd"]["no_gcode"]),
           ("Only in this image", "Not in this image", "Not in this image", "Only in this image"))
    # the hardware chapter
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", HARDWARE_PAGE))
        expect("no hardware chapter", check_doc_dir(d, tool, "sd", repo="/the/repo"), ["hardware.html is missing (the hardware chapter)"])
    for credit in HARDWARE_CREDITS:
        expect("'%s' is not credited" % credit, problems_of("serial", pages={HARDWARE_PAGE: " ".join(x for x in HARDWARE_CREDITS if x != credit) + "".join(
            '<object data="%s"></object>' % x for x in WIRING_COMMON + OTHER_PICTURES)}),
            ["hardware.html does not credit '%s' (the wiring pictures are CC BY-SA 3.0: the credit and the authors must be on the page)" % credit])
    expect("a picture of the generators that the chapter does not show", problems_of("serial", pages={HARDWARE_PAGE: " ".join(HARDWARE_CREDITS) + "".join(
        '<object data="%s"></object>' % x for x in WIRING_COMMON)}), ["hardware.html does not show Geometry.svg", "hardware.html does not show Tension.svg"])
    expect("the pictures of the chapter, pinned", (WIRING_COMMON, OTHER_PICTURES, VARIANTS["sd"]["wiring"][-1], VARIANTS["serial"]["no_wiring"]),
           (("overview.svg", "motors.svg", "pen_servo.svg", "joystick.svg", "lcd_i2c.svg"), ("Geometry.svg", "Tension.svg"), "sd_card.svg", ("sd_card.svg",)))
    expect("the credits of the chapter, pinned (the licence, the project, and the authors of the Fritzing parts that the pictures embed)", HARDWARE_CREDITS,
           ("Fritzing", "CC BY-SA", "Althaus", "Brendan Howell", "Stefan Hermann", "SparkFun", "Richard Bruneau", "Knorig"))
    expect("a picture that the chapter does not show", problems_of("serial", pages={HARDWARE_PAGE: " ".join(HARDWARE_CREDITS) + "".join(
        '<object data="%s"></object>' % x for x in WIRING_COMMON[1:] + OTHER_PICTURES)}), ["hardware.html does not show overview.svg"])
    expect("the SD module's picture in the serial image's chapter", problems_of("serial", pages={HARDWARE_PAGE: " ".join(HARDWARE_CREDITS) + "".join(
        '<object data="%s"></object>' % x for x in WIRING_COMMON + ("sd_card.svg",) + OTHER_PICTURES)}),
        ["hardware.html shows sd_card.svg, which belongs to the other image", "hardware.html has a dead link: sd_card.svg"])
    expect("the SD module's picture file in the serial image", problems_of("serial", extra={"sd_card.svg": "<svg/>"}), ["sd_card.svg is in html/ but belongs to the other image"])
    expect("the SD image needs the SD module's picture", problems_of("sd", pages={HARDWARE_PAGE: " ".join(HARDWARE_CREDITS) + "".join(
        '<object data="%s"></object>' % x for x in WIRING_COMMON + OTHER_PICTURES)}), ["hardware.html does not show sd_card.svg"])
    expect("a wiring picture without its Fritzing credit", problems_of("serial", pages={"motors.svg": "<svg/>"}), ["motors.svg has lost its Fritzing credit (CC BY-SA 3.0 obliges it)"])
    expect("a picture of the generators needs no Fritzing credit", problems_of("serial", pages={"Tension.svg": "<svg/>"}), [])
    with tempfile.TemporaryDirectory() as d:
        tree(d, "serial")
        os.remove(os.path.join(d, "html", "overview.svg"))
        expect("a picture that was not copied", check_doc_dir(d, tool, "serial", repo="/the/repo"), ["overview.svg is not in html/ (a picture of the hardware chapter)", "hardware.html has a dead link: overview.svg"])
    p = problems_of("serial", extra={"SDReader_8h.html": "<p>the SD module</p>"})
    expect("the serial image documents the SD module", first(p, "SDReader_8h.html is documented but belongs to the other image"), (1, True))
    p = problems_of("sd", pages={"SerialLink_8c.html": "<p>the link</p>"})
    expect("the SD image documents the serial link", first(p, "SerialLink_8c.html is documented but belongs to the other image"), (1, True))
    # the modules that belong to one image only, written out here on purpose: the table above is what is being tested
    expect("modules left out of the serial image", sorted(VARIANTS["serial"]["no_pages"]),
           ["SDReader_8cpp.html", "SDReader_8h.html", "SPI_8h.html", "Sd2Card_8h.html"])
    expect("modules left out of the SD image", sorted(VARIANTS["sd"]["no_pages"]),
           ["SerialLink_8c.html", "SerialLink_8h.html", "Uart_8c.html", "Uart_8h.html"])
    for v in VARIANTS:
        for page in VARIANTS[v]["no_pages"]:
            p = problems_of(v, extra={page: "<p>a page of the other image</p>"})
            expect("%s may not have %s" % (v, page), first(p, "%s is documented but belongs to the other image" % page), (1, True))
    p = problems_of("serial", pages={"SerialLink_8c.html": "<p>no diagram</p>"})
    expect("a diagram is missing", first(p, "SerialLink_8c.html has no diagram"), (1, True))
    p = problems_of("sd", pages={"LcdJoystickUI_8cpp.html": member("RedrawSdBrowser")})
    expect("the SD browser's diagram is missing", first(p, "LcdJoystickUI_8cpp.html has no diagram"), (1, True))
    p = problems_of("serial", pages={"grbl__corelr_8ino.html": "<p>none</p>"})
    expect("the job state machine's diagram is missing", first(p, "grbl__corelr_8ino.html has no diagram"), (1, True))
    nodot_log = "firmware/app/grbl_corelr.ino:128: warning: ignoring \\dot command because HAVE_DOT is not set\n"
    expect("without dot, no diagram is expected", problems_of("serial", log=nodot_log, pages={"SerialLink_8c.html": "<p>none</p>", "grbl__corelr_8ino.html": "<p>none</p>"}), [])
    p = problems_of("serial", pages={"SerialLink_8c.html": ""}, extra={})
    expect("an empty page has no diagram either", first(p, "SerialLink_8c.html has no diagram"), (1, True))
    p = problems_of(log="firmware/src/Uart.h:7: warning: Unsupported xml/html tag <X> found\n")
    expect("a markup warning", first(p, "markup warning"), (1, True))
    p = problems_of("serial", pages={"GCodeParser_8h.html": "GCodeParser_ExecuteJog is only named here" + '<a href="proto_page.html#proto-FW-03">FW-03</a>'})
    expect("the serial image lacks its own code", first(p, "GCodeParser_ExecuteJog"), (1, True))
    p = problems_of("serial", pages={"GCodeParser_8h.html": member("MyGCodeParser_ExecuteJog") + member("GCodeParser_ExecuteJogging") + '<a href="proto_page.html#proto-FW-03">FW-03</a>'})
    expect("a longer name is not the symbol", first(p, "GCodeParser_ExecuteJog is not documented"), (1, True))
    p = problems_of("sd", pages={"LcdJoystickUI_8cpp.html": "nothing"})
    expect("the SD image lacks its own code (and so its diagram)", first(p, "RedrawSdBrowser is not documented"), (2, True))
    p = problems_of("serial", pages={"LcdJoystickUI_8cpp.html": member("RedrawSdBrowser")})
    expect("the serial image shows SD code", first(p, "RedrawSdBrowser is documented"), (1, True))
    p = problems_of("sd", pages={"GCodeParser_8h.html": member("GCodeParser_ExecuteJog") + '<a href="proto_page.html#proto-FW-03">FW-03</a>'})
    expect("the SD image shows serial code", first(p, "GCodeParser_ExecuteJog is documented"), (1, True))
    with tempfile.TemporaryDirectory() as d:
        tree(d, "sd")
        os.remove(os.path.join(d, "html", "LcdJoystickUI_8cpp.html"))
        p = check_doc_dir(d, tool, "sd", repo="/the/repo")
    expect("the page of the variant's own code is missing", p, ["LcdJoystickUI_8cpp.html is missing (RedrawSdBrowser)", "LcdJoystickUI_8cpp.html is missing (it holds a diagram)"])
    for v in VARIANTS:
        p = problems_of(v, pages={"Motion_8h.html": member("Motion_ISqrt") + '<a href="req_page.html#REQ-CoreLR-F-Motion-0001">x</a>'})
        expect("the 64-bit arithmetic shows in %s" % v, first(p, "Motion_ISqrt"), (1, True))
    p = problems_of(extra={"dir_1.html": "<a title='/the/repo/docs'>"})
    expect("the repository's path leaks", first(p, "absolute path"), (1, True))
    p = problems_of(extra={"dir_1.html": "<a title='/home/somebody/x'>"})
    expect("a home directory leaks", first(p, "absolute path"), (1, True))
    p = problems_of(extra={"a%d.js" % i: "/home/x" for i in range(5)})
    expect("a leak is reported for three files, the others are counted", (len(p), "2 more" in p[-1] if p else False), (4, True))

    for v in VARIANTS:
        for page in VARIANTS[v]["pages"]:
            with tempfile.TemporaryDirectory() as d:
                tree(d, v)
                os.remove(os.path.join(d, "html", page))
                p = check_doc_dir(d, tool, v, repo="/the/repo")
            expect("%s: %s is missing" % (v, page), p, ["%s is missing (third-party code of this image)" % page])
    expect("third-party pages of the serial image", sorted(VARIANTS["serial"]["pages"]), ["Arduino_8h.html"])
    expect("third-party pages of the SD image", sorted(VARIANTS["sd"]["pages"]), ["Arduino_8h.html", "SPI_8h.html", "Sd2Card_8h.html"])
    expect("the SPI library and sdfat are not in the serial image", [x for x in ("Sd2Card_8h.html", "SPI_8h.html") if x in VARIANTS["serial"]["no_pages"]], ["Sd2Card_8h.html", "SPI_8h.html"])

    # the grouping of the vendored files, on a fabricated repository
    with tempfile.TemporaryDirectory() as r:
        for dd in THIRD_PARTY_DIRS["sd"]:
            os.makedirs(os.path.join(r, dd))
        os.makedirs(os.path.join(r, "docs", "doxygen"))
        for dd, name in (("third_party/arduino-core/cores/arduino", "Print.h"), ("third_party/sdfat", "Sd2Card.cpp"), ("third_party/sdfat", "PROVENANCE.md")):
            with open(os.path.join(r, dd, name), "w") as f:
                f.write("x")
        with open(os.path.join(r, "docs", "doxygen", "third_party.dox"), "w") as f:
            f.write("/** @file third_party/arduino-core/cores/arduino/Print.h\n *  @ingroup G */\n")
        expect("a vendored file of the core that is grouped", ungrouped(r, "serial"), [])
        expect("a vendored file that is not grouped (sdfat, SD image)", ungrouped(r, "sd"), ["third_party/sdfat/Sd2Card.cpp"])
        with open(os.path.join(r, "docs", "doxygen", "third_party_sd.dox"), "w") as f:
            f.write("/** @file third_party/sdfat/Sd2Card.cpp\n *  @ingroup G */\n")
        expect("grouped in the SD file", ungrouped(r, "sd"), [])
        expect("the SD file is not read for the serial image", ungrouped(r, "serial"), [])
        with open(os.path.join(r, "docs", "doxygen", "third_party.dox"), "w") as f:
            f.write("/** @file third_party/arduino-core/cores/arduino/Print.hX\n *  @ingroup G */\n")
        expect("a name that only starts like the file is not the file", ungrouped(r, "serial"), ["third_party/arduino-core/cores/arduino/Print.h"])

    # both variants and the landing page
    with tempfile.TemporaryDirectory() as d:
        for v in VARIANTS:
            os.makedirs(os.path.join(d, v))
            tree(os.path.join(d, v), v)
        with open(os.path.join(d, "index.html"), "w") as f:
            f.write('<a href="serial/html/index.html">serial</a> <a href="sd/html/index.html">sd</a>')
        expect("a good output", check_all(d, tool, repo="/the/repo"), [])
        with open(os.path.join(d, "index.html"), "w") as f:
            f.write('<a href="serial/html/index.html">serial</a>')
        expect("a landing page that forgets a variant", check_all(d, tool, repo="/the/repo"), ["index.html does not link to sd/html/index.html"])
        os.remove(os.path.join(d, "index.html"))
        expect("no landing page", check_all(d, tool, repo="/the/repo"), ["no index.html linking the two variants"])
        with tempfile.TemporaryDirectory() as r:
            os.makedirs(os.path.join(r, "docs", "doxygen"))
            for v in VARIANTS:
                with open(os.path.join(r, "docs", "doxygen", "gaps_%s.txt" % v), "w") as f:
                    f.write("# doxygen 1.15.0\n")
            with open(os.path.join(d, "index.html"), "w") as f:
                f.write('<a href="serial/html/index.html">serial</a> <a href="sd/html/index.html">sd</a>')
            expect("the gaps of the output are those of the baselines", check_all(d, tool, repo=r, version="1.15.0"), [])
            with open(os.path.join(r, "docs", "doxygen", "gaps_sd.txt"), "a") as f:
                f.write("x.c: Member Q (function) of file x.c is not documented.\n")
            notes = []
            expect("a gap that is fixed, named by its variant", check_all(d, tool, repo=r, version="1.15.0"),
                   ["sd: gap fixed, remove it from the baseline: x.c: Member Q (function) of file x.c is not documented."])
            expect("another version: notes, no problem", (check_all(d, tool, repo=r, version="1.9.1", notes=notes), len(notes)), ([], 2))
            expect("without a version the baselines are not read", check_all(d, tool, repo=r), [])
            os.remove(os.path.join(d, "index.html"))
        os.remove(os.path.join(d, "sd", "doxygen-warnings.log"))
        p = check_all(d, tool, repo="/the/repo")
        # a vendored file that no .dox groups, through the output check
        with tempfile.TemporaryDirectory() as r:
            for dd in THIRD_PARTY_DIRS["sd"]:
                os.makedirs(os.path.join(r, dd))
            os.makedirs(os.path.join(r, "docs", "doxygen"))
            with open(os.path.join(r, "third_party", "sdfat", "Lost.h"), "w") as f:
                f.write("x")
            p = check_all(d, tool, repo=r)
        expect("the output check reports a vendored file that is not grouped", [x for x in p if "Lost.h" in x],
               ["sd: third_party/sdfat/Lost.h is read by Doxygen but not grouped in docs/doxygen/third_party*.dox"] )
        p = check_all(d, tool, repo="/the/repo")
        expect("a problem of one variant is named", [x for x in p if x.startswith("sd: ")], ["sd: no doxygen-warnings.log"])
    return failures


def output_failures(args, tool, notes):
    """-> the failures of the real output named by the command line (`args`: doc_dir, doxygen_version, package_dir, package_version); the notes go to `notes`."""
    version = args.doxygen_version if args.doxygen_version is not None else tool.doxygen_version()
    failures = ["output: " + p for p in check_all(os.path.abspath(args.doc_dir), tool, version=version, notes=notes)]
    if args.package_dir:
        failures += ["package: " + p for p in check_package(os.path.abspath(args.package_dir), os.path.abspath(args.doc_dir), args.package_version or "")]
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--doc-dir")
    ap.add_argument("--package-dir", help="the target doc_package's output (build/doxygen/dist), checked against --doc-dir; needs --package-version")
    ap.add_argument("--package-version", help="the version in the names of the packages (CORELR_VERSION)")
    ap.add_argument("--doxygen-version", help="the version that wrote the logs (default: asked to `doxygen --version`)")
    args = ap.parse_args(argv)
    if args.help:
        print(__doc__)
        return 0
    tool = load_tool()
    failures = selftest(tool)
    if args.doc_dir:
        notes = []
        failures += output_failures(args, tool, notes)
        for n in notes:
            print("note:", n)
    for f in failures:
        print("FAIL", f)
    if not failures:
        print("ok: the classifier and the checker notice each defect%s" % ("; the output of %s is clean" % args.doc_dir if args.doc_dir else ""))
    return len(failures)


if __name__ == "__main__":
    sys.exit(main())
