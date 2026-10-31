#!/usr/bin/env python3
#
# test_req_refs.py
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
"""test_req_refs.py -- tools/analysis/req_refs.py (the requirement links of the API documentation, REQ-CoreLR-D-Build-0019), and the repository against it.

Two parts, as for test_doxygen.py.
  1. Always (self-test): the tool is run on FABRICATED trees (requirement files, sources, pages), one defect at a time, and must report exactly that defect: a heading without its
     anchor, a stale anchor, a requirement that no heading defines (in a Doxygen comment, an ordinary comment, a page, an @req{}), a plain identifier in a Doxygen comment or page, a range or
     a shorthand, an @req{} in an ordinary comment, a citation on a title line; and the rewriting (--add-anchors, --convert) on its exact output. This is the part that the mutants of
     tests/mutation/mutants/req_refs.txt break.
  2. The repository itself: every requirement heading has its anchor, every citation of firmware/ and docs/doxygen/ names a requirement that exists, and the Doxygen comments and pages use @req{}.

Usage:   test_req_refs.py [-h]
Exit status: the number of failed checks (0 = all pass).
"""
import contextlib, importlib.util, io, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
A1, A2 = "REQ-CoreLR-F-Foo-0001", "REQ-CoreLR-F-Foo-0002"
D1 = "REQ-CoreLR-D-LcdI2c-0001"          # an area name with digits


def load_tool():
    spec = importlib.util.spec_from_file_location("req_refs", os.path.join(REPO, "tools", "analysis", "req_refs.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


def spec(root, anchors=True, extra=""):
    """Requirement files: F-Foo with two requirements and D-LcdI2c with one."""
    a = lambda i: '<a id="%s"></a>\n' % i if anchors else ""
    write(root, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n%stext\n\n## %s -- two\n%smore\n%s" % (A1, a(A1), A2, a(A2), extra))
    write(root, "requirements/design/REQ-CoreLR-D-LcdI2c.md", "# Lcd\n\n## %s -- driver\n%stext\n" % (D1, a(D1)))


def tree(code="", page="", ino=None, **kw):
    """A fabricated root with the specs, firmware/src/Foo.c = `code`, docs/doxygen/p.md = `page`; -> its path (a TemporaryDirectory the caller keeps open)."""
    d = tempfile.TemporaryDirectory()
    spec(d.name, **kw)
    os.makedirs(os.path.join(d.name, "firmware", "app"))
    os.makedirs(os.path.join(d.name, "firmware", "config"))
    write(d.name, "firmware/src/Foo.c", code)
    write(d.name, "docs/doxygen/p.md", page)
    if ino is not None:
        write(d.name, "firmware/app/Foo.ino", ino)
    return d


def selftest(tool):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append("%s: got %r, want %r" % (label, got, want))

    def run(code="", page="", **kw):
        with tree(code, page, **kw) as _:
            pass
        d = tree(code, page, **kw)
        try:
            return [(w, k) for w, k, _ in tool.check(d.name)]
        finally:
            d.cleanup()

    # the clean cases
    expect("a clean tree", run("/** Realizes @req{%s}. */\nint a;\n/* plain %s */\n// plain %s\n" % (A1, A2, A1), "text @req{%s}\n" % A2), [])
    expect("an area name with digits", run("/** @req{%s} */\n" % D1), [])
    expect("an identifier in a string literal is not a citation", run('const char *s = "REQ-CoreLR-F-Foo-9999 /* REQ-CoreLR-F-Foo-9998 */";\n'), [])
    expect("an identifier that is not F or D is not a requirement", run("/** REQ-CoreLR-X-Foo-0001 */\n"), [])
    expect("a tool's .ino is read like a source", [(w.split(":")[0], k) for w, k in run(ino="/** REQ-CoreLR-F-Foo-0001 */\n")], [("firmware/app/Foo.ino", "plain")])

    d = tree()
    try:
        write(d.name, "firmware/src/Bar.cpp", "/** %s */\n" % A1)
        write(d.name, "firmware/config/Cfg.h", "/** %s */\n" % A1)
        write(d.name, "firmware/src/notes.txt", "/** %s */\n" % A1)
        write(d.name, "docs/doxygen/q.dox", "/** %s */\n" % A1)
        write(d.name, "docs/doxygen/q.html", "%s\n" % A1)
        expect("the files that are read: .cpp and config/*.h and .dox yes, .txt and .html no", sorted(w for w, k, _ in tool.check(d.name) if k == "plain"),
               ["docs/doxygen/q.dox:1", "firmware/config/Cfg.h:1", "firmware/src/Bar.cpp:1"])
    finally:
        d.cleanup()

    # anchors
    expect("no anchor", run(anchors=False), [("requirements/design/REQ-CoreLR-D-LcdI2c.md:3", "anchor"), ("requirements/functional/REQ-CoreLR-F-Foo.md:3", "anchor"),
                                              ("requirements/functional/REQ-CoreLR-F-Foo.md:6", "anchor")])
    d = tree()
    try:
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n\n<a id=\"%s\"></a>\n" % (A1, A1))
        expect("an anchor that is not on the next line", [(w, k) for w, k, _ in tool.anchor_problems(d.name) if "Foo.md" in w],
               [("requirements/functional/REQ-CoreLR-F-Foo.md:3", "anchor"), ("requirements/functional/REQ-CoreLR-F-Foo.md:5", "anchor")])
    finally:
        d.cleanup()
    expect("a stale anchor", [x for x in run(extra='\n<a id="REQ-CoreLR-F-Foo-0009"></a>\n') if x[1] == "anchor"], [("requirements/functional/REQ-CoreLR-F-Foo.md:11", "anchor")])
    d = tree()
    try:
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\n" % (A1, A2))
        expect("the anchor of another requirement", [(w, k) for w, k, _ in tool.anchor_problems(d.name)], [("requirements/functional/REQ-CoreLR-F-Foo.md:3", "anchor")])
    finally:
        d.cleanup()

    # what a citation must be
    expect("an unknown requirement in a Doxygen comment", [k for _, k in run("/** @req{REQ-CoreLR-F-Foo-0009} */\n")], ["unknown"])
    expect("an unknown requirement in an ordinary comment", [k for _, k in run("/* REQ-CoreLR-F-Foo-0009 */\n")], ["unknown"])
    expect("an unknown requirement after a statement", [k for _, k in run("int a; // REQ-CoreLR-F-Foo-0009\n")], ["unknown"])
    expect("an unknown requirement in a page", run(page="see @req{REQ-CoreLR-F-Foo-0009}\n"), [("docs/doxygen/p.md:1", "unknown")])
    expect("a plain identifier in a Doxygen comment", run("\n/** see %s */\n" % A1), [("firmware/src/Foo.c:2", "plain")])
    expect("a plain identifier in a trailing Doxygen comment", [k for _, k in run("int a; /**< %s */\n" % A1)], ["plain"])
    expect("a plain identifier in a /*! comment", [k for _, k in run("/*! %s */\n" % A1)], ["plain"])
    expect("a plain identifier in a page", run(page="line\nsee %s\n" % A1), [("docs/doxygen/p.md:2", "plain")])
    expect("an @req{} in an ordinary comment", run("/* @req{%s} */\n" % A1), [("firmware/src/Foo.c:1", "literal")])
    expect("an @req{} after a statement", [k for _, k in run("int a; // @req{%s}\n" % A1)], ["literal"])
    expect("an empty comment is not a Doxygen comment", run("/**/\n/* %s */\n" % A1), [])
    expect("two citations on a line", [k for _, k in run("/** %s and @req{%s} */\n" % (A1, A2))], ["plain"])

    # shorthand and ranges
    expect("a range in a Doxygen comment", [k for _, k in run("/** @req{%s}..@req{%s} */\n" % (A1, A2))], [])
    expect("a range written the short way", sorted(set(k for _, k in run("/** %s..0002 */\n" % A1))), ["shorthand"])
    expect("a shorthand with a comma", sorted(set(k for _, k in run("/** @req{%s}, -0002 */\n" % A1))), ["shorthand"])
    expect("a shorthand with a slash", sorted(set(k for _, k in run("/** @req{%s}/0002 */\n" % A1))), ["shorthand"])
    expect("a shorthand with a slash and a minus", sorted(set(k for _, k in run("/** @req{%s}/-0002 */\n" % A1))), ["shorthand"])
    expect("a shorthand with 'and'", sorted(set(k for _, k in run("/** @req{%s} and -0002 */\n" % A1))), ["shorthand"])
    expect("a shorthand in a page", sorted(set(k for _, k in run(page="@req{%s}, -0002\n" % A1))), ["shorthand"])
    expect("a shorthand in an ordinary comment is allowed", run("/* %s, -0002 */\n" % A1), [])
    expect("a shorthand in an ordinary comment is resolved", run("/* %s, -0009 */\n" % A1), [("firmware/src/Foo.c:1", "unknown")])
    expect("a range in an ordinary comment is resolved", run("/* %s..0009 */\n" % A1), [("firmware/src/Foo.c:1", "unknown")])
    expect("a year after a comma is no shorthand", run("/* %s, 2026 */\n" % A1), [])

    # titles
    expect("a requirement on a @defgroup title line", [k for _, k in run("/**\n * @defgroup G Title (@req{%s})\n */\n" % A1)], ["title"])
    expect("a requirement on a @name line", [k for _, k in run("/**\n * @name Name (@req{%s})\n */\n" % A1)], ["title"])
    expect("a requirement on a @page line", [k for _, k in run("/**\n * @page p Title @req{%s}\n */\n" % A1)], ["title"])
    expect("a requirement in the text after the title", run("/**\n * @defgroup G Title\n * Realizes @req{%s}.\n */\n" % A1), [])

    # the rewriting
    expect("a plain identifier", tool.rewrite("see %s." % A1), "see @req{%s}." % A1)
    expect("a range", tool.rewrite("%s..0002" % A1), "@req{%s}..@req{%s}" % (A1, A2))
    expect("a shorthand with a comma", tool.rewrite("%s, -0002" % A1), "@req{%s}, @req{%s}" % (A1, A2))
    expect("a shorthand with a slash", tool.rewrite("%s/0002" % A1), "@req{%s}/@req{%s}" % (A1, A2))
    expect("a shorthand with a slash and a minus", tool.rewrite("%s/-0002" % A1), "@req{%s}/@req{%s}" % (A1, A2))
    expect("a shorthand with 'and'", tool.rewrite("%s and -0002" % A1), "@req{%s} and @req{%s}" % (A1, A2))
    expect("a shorthand with a range", tool.rewrite("%s, -0002..0004" % A1), "@req{%s}, @req{%s}..@req{REQ-CoreLR-F-Foo-0004}" % (A1, A2))
    expect("a chain", tool.rewrite("%s, -0002, -0003" % A1), "@req{%s}, @req{%s}, @req{REQ-CoreLR-F-Foo-0003}" % (A1, A2))
    expect("a citation that is already an @req{} is left alone", tool.rewrite("@req{%s}" % A1), "@req{%s}" % A1)
    expect("an @req{} with a shorthand after it is written out", tool.rewrite("@req{%s}/0002" % A1), "@req{%s}/@req{%s}" % (A1, A2))
    expect("an @req{} followed by text keeps its brace", tool.rewrite("@req{%s} text" % A1), "@req{%s} text" % A1)
    expect("two citations", tool.rewrite("%s and %s" % (A1, A2)), "@req{%s} and @req{%s}" % (A1, A2))
    expect("text with no citation", tool.rewrite("nothing here, -0002"), "nothing here, -0002")
    d = tree("/** Realizes %s, -0002 */\n/* plain %s */\nconst char *s = \"%s\";\n" % (A1, A1, A1), "see %s\n" % A2, anchors=False)
    try:
        before = read(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md")
        with contextlib.redirect_stdout(io.StringIO()):
            rc_conv = tool.main(["--root", d.name, "--convert"])
        expect("--convert changes the Doxygen comment and the page, not the ordinary comment or the string",
               (read(d.name, "firmware/src/Foo.c"), read(d.name, "docs/doxygen/p.md")),
               ("/** Realizes @req{%s}, @req{%s} */\n/* plain %s */\nconst char *s = \"%s\";\n" % (A1, A2, A1, A1), "see @req{%s}\n" % A2))
        expect("--convert does not touch the requirement files", read(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md"), before)
        expect("--convert then check: the anchors are still missing", rc_conv, 3)
        with contextlib.redirect_stdout(io.StringIO()):
            rc_add = tool.main(["--root", d.name, "--add-anchors"])
        expect("--add-anchors writes the three anchors", rc_add, 0)
        text = read(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md")
        expect("the anchor is on the line after its heading", text.split("\n")[2:4], ["## %s -- one" % A1, '<a id="%s"></a>' % A1])
        with contextlib.redirect_stdout(io.StringIO()):
            tool.main(["--root", d.name, "--add-anchors"])
        expect("--add-anchors twice is the same as once", read(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md"), text)
        expect("the text around a heading is kept", text.replace('<a id="%s"></a>\n' % A1, "").replace('<a id="%s"></a>\n' % A2, ""), "# Foo\n\n## %s -- one\ntext\n\n## %s -- two\nmore\n" % (A1, A2))
    finally:
        d.cleanup()

    # the requirement files' own prose: citations that exist, no Doxygen command
    expect("a citation in the prose of a requirement file that exists", run(extra="see `%s` and %s\n" % (A1, A2)), [])
    expect("a citation in the prose that names nothing", run(extra="see `REQ-CoreLR-F-Foo-0009`\n"), [("requirements/functional/REQ-CoreLR-F-Foo.md:10", "spec")])
    expect("a plain citation in the prose is not a problem", [k for _, k in run(extra="see %s\n" % A1)], [])
    expect("a citation inside a code block of a requirement file is not read", run(extra="```\nREQ-CoreLR-F-Foo-0009\n```\n"), [])
    expect("a Doxygen command in a requirement file", [k for _, k in run(extra="the `\\if` block\n")], ["command"])
    expect("another command", [k for _, k in run(extra="a \\dot graph\n")], ["command"])
    expect("a backslash that is no command", run(extra="the bytes `\\r\\n`\n"), [])
    expect("a command in a code block is left alone", run(extra="```\n\\if x\n```\n"), [])
    expect("a command name that only starts like one", run(extra="\\iffy\n"), [])

    # the filter: Doxygen reads the requirement files through it
    src = "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\nsee `%s`, %s and [`%s`](x.md). Not `%s`.\n\n```\n`%s`\n```\n" % (A1, A1, A2, A2, D1, A1 + " (open)", A2)
    out = tool.filter_text(src, "REQ-CoreLR-F-Foo")
    expect("the filter keeps the number of lines", len(out.split("\n")), len(src.split("\n")))
    lines = out.split("\n")
    expect("the first heading gets the page id", lines[0], "# Foo {#REQ-CoreLR-F-Foo}")
    expect("a requirement heading gets its label", lines[2], "## %s -- one {#%s}" % (A1, A1))
    expect("the anchor line is blanked", lines[3], "")
    expect("a citation alone in a code span, a plain one and a link to a requirement become aliases", lines[4],
           "see @req{%s}, @req{%s} and @req{%s}. Not `%s (open)`." % (A2, A2, D1, A1))
    expect("a code block is left alone", lines[6:9], ["```", "`%s`" % A2, "```"])
    expect("a second top-level heading is not a page title", tool.filter_text("# A\n# B\n", "x"), "# A {#x}\n# B\n")
    expect("a heading that is not a requirement is left as it is, citations included", tool.filter_text("# T\n## see %s\n" % A1, "x"), "# T {#x}\n## see %s\n" % A1)
    expect("a heading with no anchor line below it", tool.filter_text("## %s -- one\ntext\n" % A1, "x"), "## %s -- one {#%s}\ntext\n" % (A1, A1))
    expect("a heading at the end of the file", tool.filter_text("## %s -- one" % A1, "x"), "## %s -- one {#%s}" % (A1, A1))
    expect("another anchor line is not blanked", tool.filter_text("## %s -- one\n<a id=\"%s\"></a>\n" % (A1, A2), "x"), "## %s -- one {#%s}\n<a id=\"%s\"></a>\n" % (A1, A1, A2))
    expect("a range in a link text", tool.link_citations("[`%s`..`0006`](x.md)" % A1), "@req{%s}..@req{REQ-CoreLR-F-Foo-0006}" % A1)
    expect("a link that is not to a requirement is left alone", tool.link_citations("[the file](x.md) and [%s and more](y.md)" % A1), "[the file](x.md) and [%s and more](y.md)" % A1)
    expect("a code span with other text is left alone", tool.link_citations("`%s (open)`" % A1), "`%s (open)`" % A1)
    expect("a code span with a chain", tool.link_citations("`%s, -0002`" % A1), "@req{%s}, @req{%s}" % (A1, A2))
    expect("text with no citation", tool.link_citations("nothing `here`"), "nothing `here`")
    expect("an alias that is already there", tool.link_citations("@req{%s}" % A1), "@req{%s}" % A1)
    expect("two spans on a line", tool.link_citations("`%s` or `%s`" % (A1, A2)), "@req{%s} or @req{%s}" % (A1, A2))
    expect("a plain citation after the last span", tool.link_citations("`x` then %s" % A1), "`x` then @req{%s}" % A1)
    expect("a plain citation before the first span", tool.link_citations("%s then `x`" % A1), "@req{%s} then `x`" % A1)
    expect("a plain citation after the last link", tool.link_citations("[the file](x.md) then %s" % A1), "[the file](x.md) then @req{%s}" % A1)
    expect("a plain citation before the first link", tool.link_citations("%s then [the file](x.md)" % A1), "@req{%s} then [the file](x.md)" % A1)
    expect("text after a code block is converted again", tool.filter_text("```\nx\n```\nsee %s\n" % A1, "x"), "```\nx\n```\nsee @req{%s}\n" % A1)
    expect("text inside a tilde block is left alone", tool.filter_text("~~~\nsee %s\n~~~\n" % A1, "x"), "~~~\nsee %s\n~~~\n" % A1)
    # the pictures of docs/hardware/images are found by their name (IMAGE_PATH) and copied into the documentation; a `file:` text is not a URL
    expect("a picture of the hardware images", tool.link_citations("![alt](../../docs/hardware/images/Geometry.svg)"), "![alt](Geometry.svg)")
    expect("a picture of the wiring images", tool.link_citations("![x](../../docs/hardware/images/wiring/overview.svg)"), "![x](overview.svg)")
    expect("a link to a picture", tool.link_citations("[`../../docs/hardware/images/Geometry.svg`](../../docs/hardware/images/Geometry.svg) is"), "[`../../docs/hardware/images/Geometry.svg`](Geometry.svg) is")
    expect("a picture reached from the design directory (one level up)", tool.link_citations("![x](../docs/hardware/images/a.svg)"), "![x](a.svg)")
    expect("a link to another document is left alone", tool.link_citations("[x](../../docs/hardware/PinMapping.md) and ![y](../../docs/other/a.svg)"), "[x](../../docs/hardware/PinMapping.md) and ![y](../../docs/other/a.svg)")
    expect("file: before a word is defused", tool.link_citations("see file:line here"), "see file\u200b:line here")
    expect("file: before a word in a heading is defused", tool.filter_text("# T\n## Refs (file:line)\n", "x"), "# T {#x}\n## Refs (file\u200b:line)\n")
    expect("a word that ends with file is no URL", tool.link_citations("a profile:line"), "a profile:line")
    expect("file: and a blank is no URL", tool.link_citations("the file: x"), "the file: x")
    expect("a code span is defused too (Doxygen links a URL lookalike there as well)", tool.link_citations("`file:line`"), "`file\u200b:line`")
    d = tree()
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--filter", os.path.join(d.name, "requirements", "functional", "REQ-CoreLR-F-Foo.md")])
        text = buf.getvalue()
        expect("--filter prints the file for Doxygen, with the page id taken from its name", (rc, text.split("\n")[0], "ok:" in text), (0, "# Foo {#REQ-CoreLR-F-Foo}", False))
    finally:
        d.cleanup()

    # applicability tags
    TS, TD = "*Applies to: serial image only*", "*Applies to: sd image only*"
    def tagged(tags, files=None):
        """A tree whose F-Foo requirements carry `tags` (a list of image or None, one for each requirement)."""
        d = tempfile.TemporaryDirectory()
        parts = ["# Foo\n\n"]
        for n, tag in enumerate(tags, 1):
            i = "REQ-CoreLR-F-Foo-%04d" % n
            parts.append("## %s -- r\n<a id=\"%s\"></a>\n%stext\n\n" % (i, i, ("*Applies to: %s image only*\n\n" % tag) if tag else ""))
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "".join(parts))
        for rel, text in (files or {}).items():
            write(d.name, rel, text)
        return d
    lines = ["## %s -- r" % A1, '<a id="%s"></a>' % A1, TS, "", "text"]
    expect("tag_of reads the line after the anchor", tool.tag_of(lines, 0), "serial")
    expect("tag_of: no tag", tool.tag_of(["## %s -- r" % A1, '<a id="%s"></a>' % A1, "text"], 0), None)
    expect("tag_of: no anchor, the tag follows the heading", tool.tag_of(["## %s -- r" % A1, TD], 0), "sd")
    expect("tag_of: the tag is not further down", tool.tag_of(["## %s -- r" % A1, '<a id="%s"></a>' % A1, "", TS], 0), None)
    expect("tag_of: a heading at the end of the file", tool.tag_of(["## %s -- r" % A1], 0), None)
    expect("common_tag: all the same", tool.common_tag(["## %s -- r" % A1, TS, "## %s -- r" % A2, TS]), "serial")
    expect("common_tag: one without", tool.common_tag(["## %s -- r" % A1, TS, "## %s -- r" % A2, "x"]), None)
    expect("common_tag: different", tool.common_tag(["## %s -- r" % A1, TS, "## %s -- r" % A2, TD]), None)
    expect("common_tag: the first without", tool.common_tag(["## %s -- r" % A1, "x", "## %s -- r" % A2, TS]), None)
    expect("common_tag: no requirement", tool.common_tag(["# Foo", "text"]), None)
    d = tagged(["serial", "serial"])
    try:
        expect("requirement_images: a file all serial", tool.requirement_images(d.name), {"REQ-CoreLR-F-Foo": "serial"})
        expect("a valid tag is no problem", tool.tag_problems(d.name), [])
    finally:
        d.cleanup()
    d = tagged(["serial", None])
    try:
        expect("requirement_images: a mixed file", tool.requirement_images(d.name)["REQ-CoreLR-F-Foo"], None)
    finally:
        d.cleanup()
    d = tagged([None], {"requirements/functional/REQ-CoreLR-F-Foo.md": "# Foo\n\n## %s -- r\n<a id=\"%s\"></a>\n*Applies to: both images*\ntext\n" % (A1, A1)})
    try:
        expect("a tag that is not valid", [(w.split(":")[1], k) for w, k, _ in tool.tag_problems(d.name)], [("5", "tag")])
    finally:
        d.cleanup()
    d = tagged([None], {"requirements/functional/REQ-CoreLR-F-Foo.md": "# Foo\n\n*Applies to: sd image only*\n\n## %s -- r\n<a id=\"%s\"></a>\ntext\n" % (A1, A1)})
    try:
        expect("a tag that is not after an anchor", [(w.split(":")[1], k) for w, k, _ in tool.tag_problems(d.name)], [("3", "tag")])
    finally:
        d.cleanup()
    d = tagged([None], {"requirements/functional/REQ-CoreLR-F-Foo.md": "# Foo\n\n## %s -- r\n<a id=\"%s\"></a>\ntext\n*Applies to: sd image only*\n" % (A1, A1)})
    try:
        expect("a tag two lines after the anchor", [(w.split(":")[1], k) for w, k, _ in tool.tag_problems(d.name)], [("6", "tag")])
    finally:
        d.cleanup()
    # a module that exists in one image only: its requirements carry the tag of that image
    mods = "# a comment\nfirmware/src/LcdI2c.h|sd # trailing\nthird_party/sdfat|sd\nfirmware/src/Other.c|nowhere\n\n"
    d = tree()
    try:
        write(d.name, "cmake/variant_modules.txt", mods)
        expect("module_images", tool.module_images(d.name), {"LcdI2c": "sd"})
        expect("a module of one image whose requirement has no tag", [(w, k) for w, k, _ in tool.tag_problems(d.name)], [("requirements/design/REQ-CoreLR-D-LcdI2c.md:3", "tag")])
        write(d.name, "requirements/design/REQ-CoreLR-D-LcdI2c.md", "# Lcd\n\n## %s -- driver\n<a id=\"%s\"></a>\n%s\ntext\n" % (D1, D1, TD))
        expect("a module of one image whose requirement has its tag", tool.tag_problems(d.name), [])
        write(d.name, "requirements/design/REQ-CoreLR-D-LcdI2c.md", "# Lcd\n\n## %s -- driver\n<a id=\"%s\"></a>\n%s\ntext\n" % (D1, D1, TS))
        expect("a module of one image whose requirement has the other image's tag", [k for _, k, _ in tool.tag_problems(d.name)], ["tag"])
        os.remove(os.path.join(d.name, "cmake", "variant_modules.txt"))
        expect("no list: no module check", tool.tag_problems(d.name), [])
        expect("module_images without a list", tool.module_images(d.name), {})
    finally:
        d.cleanup()
    # the filter for one image
    src = "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\n%s\n\ntext\n\n## %s -- two\n<a id=\"%s\"></a>\ntext\n" % (A1, A1, TS, A2, A2)
    out = tool.filter_text(src, "x", "sd").split("\n")
    expect("the heading of a requirement of the other image says so", out[2], "## %s -- one (serial image only: not in this image) {#%s}" % (A1, A1))
    expect("its tag line becomes a banner", out[4], "**Not part of the sd image.** It applies to the serial image only.")
    expect("the heading of a requirement that applies to both is as it was", out[8], "## %s -- two {#%s}" % (A2, A2))
    expect("a mixed file's title is as it was", out[0], "# Foo {#x}")
    out = tool.filter_text(src, "x", "serial").split("\n")
    expect("in its own image the heading is as it was", (out[2], out[4]), ("## %s -- one {#%s}" % (A1, A1), TS))
    out = tool.filter_text(src, "x").split("\n")
    expect("with no image nothing is marked", (out[2], out[4]), ("## %s -- one {#%s}" % (A1, A1), TS))
    whole = "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\n%s\n\n## %s -- two\n<a id=\"%s\"></a>\n%s\n" % (A1, A1, TD, A2, A2, TD)
    expect("a file whose requirements all apply to one image: its title says so in the other", tool.filter_text(whole, "x", "serial").split("\n")[0], "# Foo (sd image only: not in this image) {#x}")
    expect("and not in its own image", tool.filter_text(whole, "x", "sd").split("\n")[0], "# Foo {#x}")
    expect("--applies marks the title of a file that has no requirement", tool.filter_text("# Proto\n\ntext\n", "P", "sd", "serial").split("\n")[0], "# Proto (serial image only: not in this image) {#P}")
    expect("--applies is not marked in its own image", tool.filter_text("# Proto\n", "P", "serial", "serial"), "# Proto {#P}\n")
    expect("--applies wins over the tags", tool.filter_text(whole, "x", "sd", "serial").split("\n")[0], "# Foo (serial image only: not in this image) {#x}")
    expect("the line count is kept", len(tool.filter_text(src, "x", "sd").split("\n")), len(src.split("\n")))
    d = tree()
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--image", "sd", "--applies", "serial", "--filter", os.path.join(d.name, "requirements", "design", "REQ-CoreLR-D-LcdI2c.md")])
        expect("--filter with --image and --applies", (rc, buf.getvalue().split("\n")[0]), (0, "# Lcd (serial image only: not in this image) {#REQ-CoreLR-D-LcdI2c}"))
    finally:
        d.cleanup()
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        try:
            tool.main(["--image", "both", "--filter", "x"])
            bad = None
        except SystemExit as e:
            bad = e.code
    expect("an image that is neither serial nor sd is a usage error", bad, 2)

    # the items of the serial protocol
    PROTO = "# Protocol\n\n## 1. What\n\n## 4. Specification\n\n### 4.6 Error codes\n\n| # | Decision |\n|---|---|\n| D1 | y up |\n\n| # | Request |\n|---|---|\n| **FW-01** | flip |\n| **FW-03** | line |\n\n## 10. Points\n\n| P7 | the Z word |\n| P2 | jog |\n| P6 | jog contents |\n| M-1 | flash |\n\n```\n| P9 | in a block |\n```\n"
    known = {"1", "4", "4.6", "10", "D1", "FW-01", "FW-03", "P7", "P2", "P6", "M-1"}
    d = tree()
    try:
        write(d.name, "docs/protocol/SerialProtocol.md", PROTO)
        expect("the items that the protocol defines", tool.protocol_ids(d.name), known)
        write(d.name, "docs/protocol/SerialProtocol.md", PROTO + "\n| FW-01 | repeated |\n### 4.6 Again\n")
        expect("a later repetition defines nothing new", tool.protocol_ids(d.name), known)
        os.remove(os.path.join(d.name, "docs", "protocol", "SerialProtocol.md"))
        expect("no protocol, no item", tool.protocol_ids(d.name), set())
    finally:
        d.cleanup()
    expect("the label of a section has dashes", tool.proto_label("4.10"), "proto-4-10")
    expect("the label of a request", tool.proto_label("FW-03"), "proto-FW-03")
    expect("the label of a point", tool.proto_label("P7"), "proto-P7")
    expect("a request is written @proto{}", tool.proto_link("FW-03"), "@proto{FW-03}")
    expect("a section is written @protosec{label,text}", tool.proto_link("4.6"), "@protosec{4-6,4.6}")
    expect("a whole number is a section too", tool.proto_link("10"), "@protosec{10,10}")
    rw = lambda x: tool.proto_rewrite(x, known)
    expect("a request", rw("see FW-03."), "see @proto{FW-03}.")
    expect("a request that the protocol does not define", rw("see FW-09."), "see FW-09.")
    expect("a request inside a word is no request", rw("xFW-03 and FW-031"), "xFW-03 and FW-031")
    expect("a section after the file's name", rw("docs/protocol/SerialProtocol.md 4.6, FW-03"), "docs/protocol/SerialProtocol.md @protosec{4-6,4.6}, @proto{FW-03}")
    expect("a section with the word section", rw("`docs/protocol/SerialProtocol.md` section 4.6):"), "`docs/protocol/SerialProtocol.md` section @protosec{4-6,4.6}):")
    expect("a section with a section sign", rw("§4.6 and § 10"), "§@protosec{4-6,4.6} and § @protosec{10,10}")
    expect("a section that the protocol does not define", rw("§9.9"), "§9.9")
    expect("a point after the file's name", rw("SerialProtocol.md P7"), "SerialProtocol.md @proto{P7}")
    expect("a decision after the file's name", rw("SerialProtocol.md D1"), "SerialProtocol.md @proto{D1}")
    expect("a list of sections after the file's name", rw("SerialProtocol.md 4.6, 10, 4"), "SerialProtocol.md @protosec{4-6,4.6}, @protosec{10,10}, @protosec{4,4}")
    expect("the sections that follow an alias of a section", rw("@protosec{4-6,4.6}, 10, 1"), "@protosec{4-6,4.6}, @protosec{10,10}, @protosec{1,1}")
    expect("a number after an alias of a request is no section", rw("@proto{FW-01}, 10"), "@proto{FW-01}, 10")
    expect("a point in parentheses", rw("(P7) and (P2, x)"), "(@proto{P7}) and (@proto{P2}, x)")
    expect("points joined by a slash", rw("decided, P2/P6; done"), "decided, @proto{P2}/@proto{P6}; done")
    expect("a point with a letter", rw("closed, P7 (a)"), "closed, @proto{P7} (a)")
    expect("a G-code P word is no point", rw("G10 L20 P1 X0, P7 X"), "G10 L20 P1 X0, P7 X")
    expect("points after the word points", rw("points P2 and P6"), "points @proto{P2} and @proto{P6}")
    expect("a decision after the word", rw("decision D1 of"), "decision @proto{D1} of")
    expect("a request after the word", rw("request FW-01, FW-03"), "request @proto{FW-01}, @proto{FW-03}")
    expect("an alias that is already there is left alone", rw("@proto{FW-01} @protosec{4-6,4.6} @req{REQ-CoreLR-F-Foo-0001}"), "@proto{FW-01} @protosec{4-6,4.6} @req{REQ-CoreLR-F-Foo-0001}")
    expect("text with nothing to link", rw("a plain sentence, with 4.6 in it"), "a plain sentence, with 4.6 in it")
    # the filter for the protocol
    out = tool.filter_text(PROTO, "SerialProtocol", "sd", "serial", True, known).split("\n")
    expect("the title of the protocol", out[0], "# Protocol (serial image only: not in this image) {#SerialProtocol}")
    expect("a section is labelled", (out[2], out[6]), ("## 1. What {#proto-1}", "### 4.6 Error codes {#proto-4-6}"))
    expect("a decision's row gets its anchor", out[10], '| <a id="proto-D1"></a>D1 | y up |')
    expect("a request's row gets its anchor, bold kept", out[14], '| <a id="proto-FW-01"></a>**FW-01** | flip |')
    row = lambda x: [l for l in out if "| %s " % x in l or "|%s " % x in l or x in l.split("|")[1:2] or ("proto-%s" % x) in l]
    expect("a point's row", [l for l in out if "the Z word" in l], ['| <a id="proto-P7"></a>P7 | the Z word |'])
    expect("a measurement's row", [l for l in out if "flash" in l], ['| <a id="proto-M-1"></a>M-1 | flash |'])
    expect("a code block is left alone", [l for l in out if "in a block" in l], ["| P9 | in a block |"])
    expect("the line count is kept", len(out), len(PROTO.split("\n")))
    rep = tool.filter_text(PROTO + "| FW-01 | repeated |\n### 4.6 Again\nsee FW-03\n", "S", None, None, True, known).split("\n")
    expect("a repetition has no anchor, and cites the item", rep[-4], "| @proto{FW-01} | repeated |")
    expect("a repeated section has no label", rep[-3], "### 4.6 Again")
    expect("the protocol cites itself with links", rep[-2], "see @proto{FW-03}")
    expect("without --protocol nothing is labelled", tool.filter_text(PROTO, "S", None, None, False, known).split("\n")[6], "### 4.6 Error codes")
    expect("a requirement file cites the protocol with links", tool.filter_text("# R\n\nsee FW-01 and `docs/protocol/SerialProtocol.md` P7\n", "R", None, None, False, known).split("\n")[2],
           "see @proto{FW-01} and `docs/protocol/SerialProtocol.md` @proto{P7}")
    expect("a file that is not given the protocol's items links nothing", tool.filter_text("# R\n\nsee FW-01\n", "R").split("\n")[2], "see FW-01")
    # the lint on the code and the pages
    def prun(code="", page=""):
        d = tree(code, page)
        try:
            write(d.name, "docs/protocol/SerialProtocol.md", PROTO)
            return [(w, k) for w, k, _ in tool.check(d.name)]
        finally:
            d.cleanup()
    expect("a clean protocol citation", prun("/** @proto{FW-03} and @protosec{4-6,4.6} */\n/* plain FW-03 */\n", "see @proto{P7}\n"), [])
    expect("a plain request in a Doxygen comment", prun("/** see FW-03 */\n"), [("firmware/src/Foo.c:1", "plain")])
    expect("a plain section after the file's name", prun("/** docs/protocol/SerialProtocol.md 4.6 */\n"), [("firmware/src/Foo.c:1", "plain")])
    expect("a plain point in a page", prun(page="x\nsee (P7)\n"), [("docs/doxygen/p.md:2", "plain")])
    expect("a plain request in an ordinary comment is allowed", prun("/* see FW-03 and SerialProtocol.md 4.6 */\n"), [])
    expect("an unknown request in an ordinary comment", prun("/* see FW-09 */\n"), [("firmware/src/Foo.c:1", "unknown")])
    expect("an unknown request in a Doxygen comment", prun("/** @proto{FW-09} */\n"), [("firmware/src/Foo.c:1", "unknown")])
    expect("an unknown section", prun("/** @protosec{9-9,9.9} */\n"), [("firmware/src/Foo.c:1", "unknown")])
    expect("a section written @proto{}", prun("/** @proto{4.6} */\n"), [("firmware/src/Foo.c:1", "label")])
    expect("a section with the wrong label", prun("/** @protosec{4.6,4.6} */\n"), [("firmware/src/Foo.c:1", "label")])
    expect("an alias in an ordinary comment", prun("/* @proto{FW-03} */\n"), [("firmware/src/Foo.c:1", "literal")])
    expect("an item on a title line", [k for _, k in prun("/**\n * @defgroup G Title (@proto{FW-03})\n */\n")], ["title"])
    expect("an unknown item on a title line is both", sorted(k for _, k in prun("/**\n * @defgroup G Title (@proto{FW-09})\n */\n")), ["title", "unknown"])
    expect("an unknown request in the prose of a requirement file", [k for _, k in prun(page="")] , [])
    d = tree()
    try:
        write(d.name, "docs/protocol/SerialProtocol.md", PROTO)
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\nsee FW-09\n" % (A1, A1))
        expect("an unknown request in the prose of a requirement file", [(w.split(":")[1], k) for w, k, _ in tool.check(d.name)], [("5", "spec")])
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\nsee FW-01\n" % (A1, A1))
        expect("a known request in the prose of a requirement file", tool.check(d.name), [])
        os.remove(os.path.join(d.name, "docs", "protocol", "SerialProtocol.md"))
        write(d.name, "requirements/functional/REQ-CoreLR-F-Foo.md", "# Foo\n\n## %s -- one\n<a id=\"%s\"></a>\nsee FW-09\n" % (A1, A1))
        expect("with no protocol the prose of a requirement is not checked for its items", tool.check(d.name), [])
    finally:
        d.cleanup()
    d = tree("/** see FW-03, docs/protocol/SerialProtocol.md 4.6, (P7) */\n/* plain FW-03 */\n", "the page: FW-01\n")
    try:
        write(d.name, "docs/protocol/SerialProtocol.md", PROTO)
        with contextlib.redirect_stdout(io.StringIO()):
            tool.main(["--root", d.name, "--convert"])
        expect("--convert writes the protocol's aliases in the Doxygen comment and the page, not in the ordinary comment",
               (read(d.name, "firmware/src/Foo.c"), read(d.name, "docs/doxygen/p.md")),
               ("/** see @proto{FW-03}, docs/protocol/SerialProtocol.md @protosec{4-6,4.6}, (@proto{P7}) */\n/* plain FW-03 */\n", "the page: @proto{FW-01}\n"))
    finally:
        d.cleanup()

    # the documents of the repository that are pages of the documentation
    EP = "docs/protocol/EepromLayout.md"
    AL = "@docref{EepromLayout,%s}" % EP
    expect("a plain path is written as an alias", tool.docref_rewrite("see %s and (%s)." % (EP, EP)), "see %s and (%s)." % (AL, AL))
    expect("a path that is already an alias is left alone", tool.docref_rewrite("see " + AL), "see " + AL)
    expect("text with no path", tool.docref_rewrite("see docs/protocol/Other.md"), "see docs/protocol/Other.md")
    expect("the link of a page", tool.docref_link(EP), AL)
    expect("a path alone in a code span becomes the alias", tool.link_citations("see `%s` here" % EP), "see %s here" % AL)
    expect("a path in bold code", tool.link_citations("**`%s`**" % EP), "**%s**" % AL)
    expect("a path with other text in its code span is left alone", tool.link_citations("`%s (the layout)`" % EP), "`%s (the layout)`" % EP)
    expect("a plain path in the prose", tool.link_citations("see %s." % EP), "see %s." % AL)
    expect("the filter links the path in a requirement file", tool.filter_text("# R\n\nsee `%s`\n" % EP, "R").split("\n")[2], "see " + AL)
    expect("docref_problems: a plain path in a Doxygen comment", [(k) for _, k, _ in tool.docref_problems("see " + EP, True)], ["plain"])
    expect("docref_problems: a plain path in an ordinary comment is allowed", tool.docref_problems("see " + EP, False), [])
    expect("docref_problems: an alias", tool.docref_problems(AL, True), [])
    expect("docref_problems: an alias of a page that does not exist", [k for _, k, _ in tool.docref_problems("@docref{Nope,%s}" % EP, True)], ["unknown"])
    expect("docref_problems: an alias with another text", [k for _, k, _ in tool.docref_problems("@docref{EepromLayout,the layout}", True)], ["label"])
    expect("docref_problems: an alias in an ordinary comment", [k for _, k, _ in tool.docref_problems(AL, False)], ["literal"])
    d = tree("/** layout in %s */\n/* plain %s */\n" % (EP, EP), "see %s\n" % EP)
    try:
        expect("the lint of the code and the pages", [(w, k) for w, k, _ in tool.check(d.name)], [("firmware/src/Foo.c:1", "plain"), ("docs/doxygen/p.md:1", "plain")])
        with contextlib.redirect_stdout(io.StringIO()):
            tool.main(["--root", d.name, "--convert"])
        expect("--convert writes the alias in the Doxygen comment and the page, not in the ordinary comment", (read(d.name, "firmware/src/Foo.c"), read(d.name, "docs/doxygen/p.md")),
               ("/** layout in %s */\n/* plain %s */\n" % (AL, EP), "see %s\n" % AL))
    finally:
        d.cleanup()
    d = tree("/**\n * @defgroup G Title (%s)\n */\n" % AL)
    try:
        expect("a page on a title line", [k for _, k in [(w, k) for w, k, _ in tool.check(d.name)]], ["title"])
    finally:
        d.cleanup()

    # the command line
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = tool.main(["-h"])
    expect("-h", (rc, "req_refs.py" in buf.getvalue()), (0, True))
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        rc = tool.main(["--add-anchors", "--convert"])
    expect("--add-anchors and --convert together are a usage error", rc, 2)
    d = tree("/** see %s */\n" % A1)
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--root", d.name])
        expect("the status is the number of problems, and each is printed", (rc, "[plain]" in buf.getvalue(), "ok:" in buf.getvalue()), (1, True, False))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--root", d.name, "--convert"])
        expect("a clean tree says so", (rc, "ok:" in buf.getvalue()), (0, True))
    finally:
        d.cleanup()
    # the citations of the protocol that are not written in full: short, file-level, relative
    ids = {A1, A2, D1, "REQ-CoreLR-F-Foo-0003", "REQ-CoreLR-F-Foo-0004", "REQ-CoreLR-D-Bar-0001", "REQ-CoreLR-D-Bar-0002"}
    stems = {"REQ-CoreLR-F-Foo", "REQ-CoreLR-D-LcdI2c", "REQ-CoreLR-D-Bar"}

    def pl(lines, ctx=None):
        ctx = ctx or tool.ProtocolContext()
        found = []
        out = [tool.protocol_line(line, ctx, ids, stems, found, n) for n, line in enumerate(lines, 1)]
        return out, [(n, kind) for n, kind, _ in found]
    F1 = "REQ-CoreLR-F-Foo-0001"
    expect("a full citation is left as it is", pl(["see " + F1])[0], ["see " + F1])
    expect("a short citation is written in full", pl(["see F-Foo-0002 here"]), (["see REQ-CoreLR-F-Foo-0002 here"], []))
    expect("a short citation that names no requirement", pl(["see F-Foo-0009"]), (["see F-Foo-0009" if False else "see REQ-CoreLR-F-Foo-0009"], [(1, "unknown")]))
    expect("a full citation that names no requirement", pl(["see REQ-CoreLR-F-Foo-0009"]), (["see REQ-CoreLR-F-Foo-0009"], [(1, "unknown")]))
    expect("a short citation is not taken inside a full one", pl(["see " + D1]), (["see " + D1], []))
    expect("a file-level citation that exists", pl(["in REQ-CoreLR-D-Bar's scope"]), (["in REQ-CoreLR-D-Bar's scope"], []))
    expect("a file-level citation that names no file", pl(["in REQ-CoreLR-D-Nope's scope"]), (["in REQ-CoreLR-D-Nope's scope"], [(1, "unknown")]))
    expect("a full citation is not a file-level one", pl(["REQ-CoreLR-D-Bar-0001"])[1], [])
    expect("a relative citation takes the prefix of the citation before it", pl([F1 + " (see -0003)"]), ([F1 + " (see REQ-CoreLR-F-Foo-0003)"], []))
    expect("a relative citation after a short one", pl(["F-Foo-0001, `-0003`"])[0], ["REQ-CoreLR-F-Foo-0001, `REQ-CoreLR-F-Foo-0003`"])
    expect("the same-prefix shorthand of a full citation is not touched", pl([F1 + ", -0003"]), ([F1 + ", -0003"], []))
    expect("a relative citation after another relative one", pl([F1 + " (-0003, then -0004)"])[0], [F1 + " (REQ-CoreLR-F-Foo-0003, then REQ-CoreLR-F-Foo-0004)"])
    expect("a relative citation with nothing before it", pl(["a delay of -0003 s"]), (["a delay of -0003 s"], [(1, "relative")]))
    expect("... and it does not take the prefix of an earlier line", pl([F1, "later -0003"]), ([F1, "later -0003"], [(2, "relative")]))
    expect("a relative citation that resolves to nothing", pl([F1 + " (see -0077)"]), ([F1 + " (see -0077)"], [(1, "relative")]))
    expect("a number glued to a word is no relative citation", pl([F1 + " abc-0003 2026-10-0003"])[1], [])
    expect("a number of more digits is no relative citation", pl([F1 + " (-00031)"])[1], [])
    expect("a number after a slash is no relative citation", pl([F1 + " docs/-0003"])[1], [])
    table = ["| Command | Reference |", "|---|---|", "| G0 | " + F1 + " |", "| G1 | `-0003` |", "| G2 | -0002, F-Foo-0004 |", "text"]
    expect("a relative citation takes the prefix of the cell above", pl(table)[0][3], "| G1 | `REQ-CoreLR-F-Foo-0003` |")
    expect("... and tells nothing", pl(table)[1], [])
    expect("a relative citation in a table row takes the citation after it in the row", pl(["| a -0003 | " + F1 + " |"])[0], ["| a REQ-CoreLR-F-Foo-0003 | " + F1 + " |"])
    expect("... the first one after it", pl(["| a -0003 | F-Foo-0002 | " + F1 + " |"])[0], ["| a REQ-CoreLR-F-Foo-0003 | REQ-CoreLR-F-Foo-0002 | " + F1 + " |"])
    expect("the column above is the same column", pl(["| " + F1 + " | D-Bar-0001 |", "| x | -0002 |"])[0][1], "| x | REQ-CoreLR-D-Bar-0002 |")
    expect("the column above is not another column", pl(["| " + F1 + " | y |", "| x | -0002 |"]), (["| " + F1 + " | y |", "| x | -0002 |"], [(2, "relative")]))
    expect("a prose line ends a table", pl(["| " + F1 + " |", "prose", "| -0003 |"]), (["| " + F1 + " |", "prose", "| -0003 |"], [(3, "relative")]))
    expect("the column memory is the last citation of the column", pl(["| " + F1 + " |", "| D-Bar-0001 |", "| -0002 |"])[0][2], "| REQ-CoreLR-D-Bar-0002 |")
    expect("a cell without a citation does not forget the column", pl(["| " + F1 + " |", "| nothing |", "| -0003 |"])[0][2], "| REQ-CoreLR-F-Foo-0003 |")
    expect("a resolved relative citation is the column's context too", pl(["| " + F1 + " |", "| -0003 |", "| -0004 |"])[0][2], "| REQ-CoreLR-F-Foo-0004 |")
    expect("no table, no column", pl(["| " + F1 + " |", "", "| -0003 |"])[1], [(3, "relative")])
    expect("the line count and the other text are kept", pl(["", "x | y", " "])[0], ["", "x | y", " "])

    # the filter writes them as links
    def flt(text):
        return tool.filter_text(text, "SerialProtocol", None, "serial", True, set(), ids, stems).split("\n")
    out = flt("# P\n\nsee F-Foo-0002, and `-0003`, and `REQ-CoreLR-D-LcdI2c`; also REQ-CoreLR-D-Bar's scope\n")
    expect("the filter links a short, a relative and two file-level citations", out[2],
           'see @req{REQ-CoreLR-F-Foo-0002}, and @req{REQ-CoreLR-F-Foo-0003}, and \\ref REQ-CoreLR-D-LcdI2c "REQ-CoreLR-D-LcdI2c"; also \\ref REQ-CoreLR-D-Bar "REQ-CoreLR-D-Bar"\'s scope')
    row = tool.filter_text("# P\n\n| FW-01 | see F-Foo-0002, REQ-CoreLR-D-Bar and (P7) |\n| FW-01 | repeated F-Foo-0002 |\n", "P", None, "serial", True, {"FW-01", "P7"}, ids, stems).split("\n")
    expect("the first row of an item: its citations are links, the item itself is the target", row[2],
           '| <a id="proto-FW-01"></a>FW-01 | see @req{REQ-CoreLR-F-Foo-0002}, \\ref REQ-CoreLR-D-Bar "REQ-CoreLR-D-Bar" and (@proto{P7}) |')
    expect("a later row of the item is read as any other", row[3], "| @proto{FW-01} | repeated @req{REQ-CoreLR-F-Foo-0002} |")
    expect("a relative citation in the first row of an item", tool.filter_text("# P\n\n| FW-02 | -0003 | " + F1 + " |\n", "P", None, "serial", True, {"FW-02"}, ids, stems).split("\n")[2],
           '| <a id="proto-FW-02"></a>FW-02 | @req{REQ-CoreLR-F-Foo-0003} | @req{' + F1 + '} |')
    expect("a file-level citation of a file that does not exist is left alone", flt("# P\n\nsee REQ-CoreLR-D-Nope.\n")[2], "see REQ-CoreLR-D-Nope.")
    expect("a relative citation without context is left alone", flt("# P\n\na delay of -0003.\n")[2], "a delay of -0003.")
    expect("a code block is not read", flt("# P\n\n```\nF-Foo-0002\n```\n")[2:4], ["```", "F-Foo-0002"])
    expect("a table of the protocol: the line count is kept", len(flt("# P\n\n| a |\n|---|\n| " + F1 + " |\n| -0003 |\n")), 7)
    expect("a requirement file does not resolve the short form", tool.filter_text("# R\n\nsee F-Foo-0002\n", "R", None, None, False, set(), ids, stems).split("\n")[2], "see F-Foo-0002")
    expect("a requirement file does not link a file-level citation either", tool.filter_text("# R\n\nsee REQ-CoreLR-D-Bar\n", "R", None, None, False, set(), ids, stems).split("\n")[2], "see REQ-CoreLR-D-Bar")
    expect("without the requirements nothing is resolved", tool.filter_text("# P\n\nsee F-Foo-0002 and -0003\n", "P", None, "serial", True, set()).split("\n")[2], "see F-Foo-0002 and -0003")

    # the lint of the protocol
    def lint(protocol):
        d = tree()
        try:
            write(d.name, "docs/protocol/SerialProtocol.md", protocol)
            return [(w, k) for w, k, _ in tool.check(d.name)], [(w, k) for w, k, _ in tool.protocol_cite_problems(d.name, tool.defined(d.name))]
        finally:
            d.cleanup()
    expect("a protocol that cites well", lint("# P\n\nsee %s, F-Foo-0002, -0002 and REQ-CoreLR-D-LcdI2c\n" % A1), ([], []))
    expect("a short citation that names no requirement is an error", lint("# P\n\nsee F-Foo-0009\n")[0], [("docs/protocol/SerialProtocol.md:3", "unknown")])
    expect("a full citation that names no requirement is an error", lint("# P\n\nsee REQ-CoreLR-F-Foo-0009\n")[0], [("docs/protocol/SerialProtocol.md:3", "unknown")])
    expect("a file that does not exist is an error", lint("# P\n\nsee REQ-CoreLR-D-Nope\n")[0], [("docs/protocol/SerialProtocol.md:3", "unknown")])
    expect("a relative citation without context is not an error", lint("# P\n\nsee -0002\n"), ([], [("docs/protocol/SerialProtocol.md:3", "relative")]))
    expect("a code block of the protocol is not read", lint("# P\n\n```\nF-Foo-0009\n```\n"), ([], []))
    expect("a heading of the protocol is not read", lint("# P\n\n## 4.6 F-Foo-0009\n"), ([], []))
    expect("the lines are counted", lint("# P\n\nx\n\ny\nF-Foo-0009\n")[0], [("docs/protocol/SerialProtocol.md:6", "unknown")])
    expect("no protocol, nothing to say", [x for x in tool.check(tree().name) if "SerialProtocol" in x[0]], [])
    d = tree()
    try:
        write(d.name, "docs/protocol/SerialProtocol.md", "# P\n\nsee -0002 and -0003\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--root", d.name, "--unresolved"])
        expect("--unresolved lists them and is not a failure", (rc, buf.getvalue().splitlines()), (0, ["docs/protocol/SerialProtocol.md:3: -0002: no prefix to take from its context",
                                                                                                    "docs/protocol/SerialProtocol.md:3: -0003: no prefix to take from its context"]))
    finally:
        d.cleanup()
    many = "".join("/** %s */\n" % A1 for _ in range(45))
    d = tree(many)
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = tool.main(["--root", d.name])
        short = buf.getvalue().count("[plain]")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            tool.main(["--root", d.name, "--list"])
        expect("45 problems: 40 are printed, then a count; --list prints them all", (rc, short, "5 more" in buf.getvalue() or "more" in buf.getvalue(), buf.getvalue().count("[plain]")), (45, 40, False, 45))
    finally:
        d.cleanup()
    return failures


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    tool = load_tool()
    failures = selftest(tool)
    repo = tool.check(REPO)
    for where, kind, text in repo[:20]:
        failures.append("repository: %s: [%s] %s" % (where, kind, text))
    if len(repo) > 20:
        failures.append("repository: ... and %d more (tools/analysis/req_refs.py --list)" % (len(repo) - 20))
    for f in failures:
        print("FAIL", f)
    if not failures:
        print("ok: the checker notices each defect; every requirement has its anchor and every citation of the code and the pages is right")
    return len(failures)


if __name__ == "__main__":
    sys.exit(main())
