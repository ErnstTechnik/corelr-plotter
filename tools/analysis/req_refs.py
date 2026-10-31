#!/usr/bin/env python3
#
# req_refs.py
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
"""req_refs.py -- do the code and the documentation cite the requirements correctly? (REQ-CoreLR-D-Build-0019)

In the API documentation (Doxygen) a requirement is a link: each heading `## REQ-CoreLR-F-Area-nnnn -- title` of requirements/functional/ and requirements/design/ carries an anchor on the next
line, `<a id="REQ-CoreLR-F-Area-nnnn"></a>` (invisible on GitHub), and a comment that Doxygen reads cites it with the alias `@req{REQ-CoreLR-F-Area-nnnn}` (Doxyfile: ALIASES, `req{1}=\\ref \\1`). A
comment that Doxygen does NOT read (an ordinary `/* */` or `//`, or a `//` after a statement) keeps the plain identifier: the alias would show as text there.

Checks (the default; exit status = the number of problems, 0 = clean):
  anchor      a requirement heading without its anchor, or an anchor that follows no requirement heading
  unknown     a requirement cited anywhere in firmware/ (any comment, `@req{}` or not) or in a page of docs/doxygen/ that no heading defines: a typing error, or a requirement that was removed
  plain       a requirement cited in plain text inside a comment that Doxygen reads (`/** */`, `/**< */`), or in a page of docs/doxygen/: write `@req{...}`
  title       a requirement on the title line of a `@defgroup`, `@page`, `@section` or `@name`: the alias does not work in a title; cite it in the text that follows
  shorthand   `REQ-...-0001, -0006`, `-0001/-0003` or `REQ-...-0001..0003` in such a comment: write one `@req{}` for each end (`@req{A}..@req{B}`)
  literal     `@req{...}` in a comment that Doxygen does not read: it would show as text
The same-prefix shorthand is also resolved in ordinary comments, to check that the numbers exist.

  command     a Doxygen command written with its backslash in a requirement file (a `\\if` or `\\dot` in a code span is read as the command once the headings carry labels): name the command without it
  tag         an `*Applies to: ...*` line that is not `*Applies to: serial image only*` or `*Applies to: sd image only*`, or not on the line after the anchor of a requirement heading; a requirement
              of a module that cmake/variant_modules.txt gives to one image only (REQ-CoreLR-D-<Module>.md) without the tag of that image
  protocol    the same for the items of the serial protocol (docs/protocol/SerialProtocol.md): an item cited that the protocol does not define (`unknown`), `FW-03`, `SerialProtocol.md 4.10`,
              `(P7)`, `decision D1`... in plain text in a Doxygen comment or page (`plain`: write `@proto{FW-03}`, or `@protosec{4-10,4.10}` for a section: the label has dashes, the text dots),
              an alias in an ordinary comment (`literal`), or whose label is not its text's (`label`)
  docref      the same for the documents of the repository that are pages of the documentation (docs/protocol/EepromLayout.md): the path in plain text in a Doxygen comment or page (`plain`: write
              `@docref{EepromLayout,docs/protocol/EepromLayout.md}`), an alias of a page that does not exist or with another text (`unknown`, `label`), in an ordinary comment (`literal`)
  spec        a requirement cited in the prose of a requirement file that no heading defines (the files keep plain text: they are read on GitHub too)

Applicability. Without a tag a requirement applies to both images. One that concerns one image only says so on the line after its anchor: `*Applies to: serial image only*` or
`*Applies to: sd image only*` (visible on GitHub). Doxygen reads the files through the filter for the image it documents (--image): in the other image the heading carries
"(serial image only: not in this image)", the tag line becomes "Not part of the sd image", and the title of a page whose requirements all carry the same tag says so too.

Other modes:
  --add-anchors   write the missing anchors in the requirement files (and nothing else)
  --convert       rewrite the plain citations of the Doxygen comments and pages into `@req{}` (shorthand and ranges are written out); then run the check
  --filter FILE   print FILE as Doxygen is to read it, and nothing else: this is the INPUT_FILTER of the requirement files (docs/doxygen/Doxyfile.in, FILTER_PATTERNS), which stay
                  as they are on GitHub. Same number of lines. The first heading gets the page id (the file's name), each requirement heading its identifier as a Doxygen label (the
                  anchor line below it is blanked: the label is the target of the links), and every citation in the prose, plain or alone in a code span, becomes `@req{}`
                  (not in a code block, not in another heading).
  --protocol      with --filter: the file is docs/protocol/SerialProtocol.md. Besides the labels and anchors of its items, the citations that are not written in full are resolved: the short form
                  (`D-GCodeParser-0013`), the name of a file (`REQ-CoreLR-D-LcdI2c`: a link to the page of that file) and the number alone (`-0006`), whose prefix is taken from its CONTEXT
                  and from nothing else: the citation before it in the same line or table cell, else the first after it in the same table row, else the one above it in the same column of the
                  same table. One with no such context stays plain text (a guess would be a wrong link): --unresolved lists them, for the file's owner to write in full. The lint reads the
                  protocol too: a citation, in any form, that names no requirement or no file is an `unknown` (an error).
  --unresolved    print the relative citations of the protocol that have no context, one per line (not a failure: they are not ours to write)
Usage:   req_refs.py [-h] [--root DIR] [--add-anchors | --convert] [--list] [--unresolved] [--image IMAGE] [--applies IMAGE] [--protocol] [--filter FILE]
         --root DIR   the repository's root (default: the one this tool is in)
         --list       print every problem with its place (the default prints the first 40)
         --image I    with --filter: the image that Doxygen documents (serial or sd): what applies to the other one only is marked as not part of it
         --applies I  with --filter: the file as a whole applies to that image only (the protocol: serial), whatever its requirements say; --filter takes the file, so it comes last
Example: tools/analysis/req_refs.py            # 0 = clean
         tools/analysis/req_refs.py --add-anchors && tools/analysis/req_refs.py --convert
         tools/analysis/req_refs.py --filter requirements/functional/REQ-CoreLR-F-Motion.md
"""
import argparse, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
ID = r"REQ-CoreLR-[FD]-[A-Za-z0-9]+-\d{4}"
HEADING = re.compile(r"^## (%s)\b" % ID)
ANCHOR = re.compile(r'^<a id="(%s)"></a>\s*$' % ID)
ANCHOR_ANYWHERE = re.compile(r'<a id="(%s)"></a>' % ID)
CITE = re.compile(r"(?P<at>@req\{)?(?P<prefix>REQ-CoreLR-[FD]-[A-Za-z0-9]+-)(?P<n>\d{4})(?P<range>\.\.(?P<m>\d{4}))?")
SHORT = re.compile(r"(?:\s*(?:,|, and| and)\s*-|/-?)(?P<n>\d{4})(?P<range>\.\.(?P<m>\d{4}))?")
TOKEN = re.compile(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|/\*.*?\*/|//[^\n]*", re.S)
TITLE = re.compile(r"@(?:defgroup|page|section|subsection|subsubsection|addtogroup|name)\b")
LINK = re.compile(r"\[([^\]]*)\]\(([^)]*)\)")
PROTOCOL = os.path.join("docs", "protocol", "SerialProtocol.md")
PROTO_HEAD = re.compile(r"^#{2,4} (\d+(?:\.\d+)*)\.? ")
PROTO_ROW = re.compile(r"^\|\s*(?:<a id=\"[^\"]*\"></a>)?(\**)(FW-\d{2}|P\d{1,2}|D\d|M-\d{1,2})(\**)(?=\s*\|)")
PROTO_ATOM = re.compile(r"@(?:req|proto|protosec|docref)\{[^}]*\}")
DOC_PAGES = {"docs/protocol/EepromLayout.md": "EepromLayout"}           # the documents of the repository that are pages of the API documentation, cited by their path
DOCREF_USE = re.compile(r"@docref\{(?P<page>[^,}]*),(?P<text>[^}]*)\}")
PROTO_USE = re.compile(r"@proto\{(?P<id>[^}]*)\}|@protosec\{(?P<label>[^,}]*),(?P<text>[^}]*)\}")
SECTION_ID = re.compile(r"^\d+(?:\.\d+)*$")
SHORT_CITE = re.compile(r"(?<![\w-])(?P<id>[FD]-[A-Za-z0-9]+-\d{4})(?![\w-])")                 # D-GCodeParser-0013: the identifier without REQ-CoreLR-
FILE_CITE = re.compile(r"REQ-CoreLR-(?P<file>[FD]-[A-Za-z0-9]+)(?![A-Za-z0-9]|-\d)")            # REQ-CoreLR-D-LcdI2c: a requirement file, no number
REL_CITE = re.compile(r"(?<![\w/.\-])-(?P<n>\d{4})(?![\w-])")                                  # -0006: the number alone, the prefix is that of the context
IMAGES = ("serial", "sd")
TAG = re.compile(r"^\*Applies to: (serial|sd) image only\*\s*$")
TAG_ANY = re.compile(r"^\*Applies to:")
SPAN = re.compile(r"(`+)(.+?)\1")
CHAIN = re.compile(r"^%s(?:\.\.\d{4})?(?:(?:\s*(?:,|, and| and)\s*-|/-?)\d{4}(?:\.\.\d{4})?)*$" % ID)
COMMAND = re.compile(r"\\(?:if|ifnot|else|elseif|endif|dot|enddot|ref|subpage|page|section|subsection|image|anchor|code|endcode|brief|param|return|file|defgroup|ingroup|name|see|note|todo|cite|link|endlink|mainpage|snippet|include|copydoc|msc|endmsc|startuml|enduml|htmlonly|endhtmlonly|latexonly|endlatexonly)\b")
FENCE = re.compile(r"^\s*(```|~~~)")
IMAGE_LINK = re.compile(r"\]\((?:\.\./)+docs/hardware/images/(?:wiring/)?([^)/\s]+)\)")
URL_LOOKALIKE = re.compile(r"(?<![A-Za-z0-9])(file):(?=[A-Za-z])")
CODE_SUFFIXES = (".c", ".h", ".cpp", ".ino")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def requirement_files(root):
    return sorted(glob.glob(os.path.join(root, "requirements", "functional", "*.md")) + glob.glob(os.path.join(root, "requirements", "design", "*.md")))


def page_files(root):
    return sorted(glob.glob(os.path.join(root, "docs", "doxygen", "*.md")) + glob.glob(os.path.join(root, "docs", "doxygen", "*.dox")))


def code_files(root):
    found = []
    for base in ("app", "src", "config"):
        for name in sorted(os.listdir(os.path.join(root, "firmware", base))) if os.path.isdir(os.path.join(root, "firmware", base)) else ():
            if name.endswith(CODE_SUFFIXES):
                found.append(os.path.join(root, "firmware", base, name))
    return found


def defined(root):
    """-> the set of requirement identifiers that a heading defines."""
    ids = set()
    for path in requirement_files(root):
        for line in read(path).splitlines():
            m = HEADING.match(line)
            if m:
                ids.add(m.group(1))
    return ids


def anchor_problems(root):
    """-> the problems of the anchors: (place, kind, text)."""
    found = []
    for path in requirement_files(root):
        lines = read(path).splitlines()
        rel = os.path.relpath(path, root)
        wanted = {}
        for i, line in enumerate(lines):
            m = HEADING.match(line)
            if m:
                wanted[i] = m.group(1)
                nxt = lines[i + 1] if i + 1 < len(lines) else ""
                a = ANCHOR.match(nxt)
                if not a or a.group(1) != m.group(1):
                    found.append(("%s:%d" % (rel, i + 1), "anchor", "%s has no anchor on the next line: <a id=\"%s\"></a>" % (m.group(1), m.group(1))))
        for i, line in enumerate(lines):
            a = ANCHOR_ANYWHERE.search(line)
            if a and (i - 1) not in wanted:
                found.append(("%s:%d" % (rel, i + 1), "anchor", "the anchor of %s follows no requirement heading" % a.group(1)))
    return found


def comments(text):
    """-> the comments of C text as (start offset, text, is_doc): a Doxygen comment is `/** ... */` (also `/**< */`) or `/*! */`; strings and character constants are skipped."""
    out = []
    for m in TOKEN.finditer(text):
        c = m.group(0)
        if c[0] in "\"'":
            continue
        is_doc = (c.startswith("/**") and not c.startswith("/**/")) or c.startswith("/*!")
        out.append((m.start(), c, is_doc))
    return out


def citations(text):
    """-> the citations of a text, in order: (offset, prefix + number, wrapped in @req{}, end offset, written as shorthand or range). A shorthand `, -0006` takes the prefix of the citation before it."""
    found = []
    for m in CITE.finditer(text):
        found.append((m.start(), m.group("prefix") + m.group("n"), bool(m.group("at")), m.end(), m.group("range") is not None))
        if m.group("range"):
            found.append((m.start(), m.group("prefix") + m.group("m"), False, m.end(), True))
        pos = m.end() + (1 if m.group("at") and text[m.end():m.end() + 1] == "}" else 0)      # past the closing brace of @req{...}
        while True:
            s = SHORT.match(text, pos)
            if not s:
                break
            found.append((pos, m.group("prefix") + s.group("n"), False, s.end(), True))
            if s.group("range"):
                found.append((pos, m.group("prefix") + s.group("m"), False, s.end(), True))
            pos = s.end()
    return found


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def proto_problems(text, known, is_doc):
    """-> (offset, kind, text) for the citations of the protocol's items in `text` (a comment, or a page when is_doc): see the module's docstring."""
    found = []
    if is_doc:
        for a, b, repl in proto_edits(text, known):
            found.append((a, "plain", "%s in a Doxygen comment or page: write %s" % (text[a:b], repl)))
    skip = [(m.start(), m.end()) for m in PROTO_ATOM.finditer(text)]
    for m in re.finditer(r"(?<![\w@{-])(FW-\d{2})(?!\d)", text):
        if m.group(1) not in known and not any(s_ <= m.start() < e for s_, e in skip):
            found.append((m.start(), "unknown", "%s is cited and the protocol does not define it" % m.group(1)))
    for m in PROTO_USE.finditer(text):
        if m.group("id") is not None:
            item = m.group("id")
            if item not in known:
                found.append((m.start(), "unknown", "%s is cited and the protocol does not define it" % item))
            elif SECTION_ID.match(item):
                found.append((m.start(), "label", "a section is written @protosec{%s,%s}" % (proto_label(item)[len("proto-"):], item)))
        else:
            item = m.group("text")
            if item not in known:
                found.append((m.start(), "unknown", "%s is cited and the protocol does not define it" % item))
            elif proto_label(item)[len("proto-"):] != m.group("label"):
                found.append((m.start(), "label", "the label of %s is %s" % (item, proto_label(item)[len("proto-"):])))
        if not is_doc:
            found.append((m.start(), "literal", "%s in a comment Doxygen does not read: it would show as text; write the plain item" % m.group(0)))
    return found


def comment_problems(root, ids, known=frozenset()):
    found = []
    for path in code_files(root):
        text = read(path)
        rel = os.path.relpath(path, root)
        for start, c, is_doc in comments(text):
            for off, kind, msg in docref_problems(c, is_doc):
                found.append(("%s:%d" % (rel, line_of(text, start + off)), kind, msg))
            for m in DOCREF_USE.finditer(c):
                line = c[c.rfind("\n", 0, m.start()) + 1:(c.find("\n", m.start()) if c.find("\n", m.start()) >= 0 else len(c))]
                if is_doc and TITLE.search(line):
                    found.append(("%s:%d" % (rel, line_of(text, start + m.start())), "title", "%s on the title line of a group or page: cite it in the text that follows" % m.group(0)))
            for off, kind, msg in proto_problems(c, known, is_doc):
                line = c[c.rfind("\n", 0, off) + 1:(c.find("\n", off) if c.find("\n", off) >= 0 else len(c))]
                found.append(("%s:%d" % (rel, line_of(text, start + off)), "title" if is_doc and TITLE.search(line) and kind != "unknown" else kind, msg))
            for m in PROTO_USE.finditer(c):
                line = c[c.rfind("\n", 0, m.start()) + 1:(c.find("\n", m.start()) if c.find("\n", m.start()) >= 0 else len(c))]
                if is_doc and TITLE.search(line):
                    found.append(("%s:%d" % (rel, line_of(text, start + m.start())), "title", "%s on the title line of a group or page: cite it in the text that follows" % m.group(0)))
            for off, ident, wrapped, end, loose in citations(c):
                where = "%s:%d" % (rel, line_of(text, start + off))
                if ident not in ids:
                    found.append((where, "unknown", "%s is cited and no heading defines it" % ident))
                if is_doc and TITLE.search(c[c.rfind("\n", 0, off) + 1:(c.find("\n", off) if c.find("\n", off) >= 0 else len(c))]):
                    found.append((where, "title", "%s on the title line of a group or page: cite it in the text that follows" % ident))
                elif is_doc and loose:
                    found.append((where, "shorthand", "%s: write a @req{} for each requirement, not a range or a shorthand" % ident))
                elif is_doc and not wrapped:
                    found.append((where, "plain", "%s in a Doxygen comment: write @req{%s}" % (ident, ident)))
                elif not is_doc and wrapped:
                    found.append((where, "literal", "@req{%s} in a comment Doxygen does not read: it would show as text; write the plain identifier" % ident))
    return found


def page_problems(root, ids, known=frozenset()):
    found = []
    for path in page_files(root):
        text = read(path)
        rel = os.path.relpath(path, root)
        for off, kind, msg in docref_problems(text, True) + proto_problems(text, known, True):
            found.append(("%s:%d" % (rel, line_of(text, off)), kind, msg))
        for off, ident, wrapped, end, loose in citations(text):
            where = "%s:%d" % (rel, line_of(text, off))
            if ident not in ids:
                found.append((where, "unknown", "%s is cited and no heading defines it" % ident))
            if loose:
                found.append((where, "shorthand", "%s: write a @req{} for each requirement, not a range or a shorthand" % ident))
            elif not wrapped:
                found.append((where, "plain", "%s in a Doxygen page: write @req{%s}" % (ident, ident)))
    return found


def docref_link(path):
    return "@docref{%s,%s}" % (DOC_PAGES[path], path)


def docref_edits(text):
    """-> the (start, end, replacement) that write the plain paths of the documents that are pages of the documentation as `@docref{Page,path}`, outside an alias."""
    skip = [(m.start(), m.end()) for m in PROTO_ATOM.finditer(text)]
    edits = []
    for path in DOC_PAGES:
        for m in re.finditer(re.escape(path), text):
            if not any(m.start() < e and s < m.end() for s, e in skip):
                edits.append((m.start(), m.end(), docref_link(path)))
    return sorted(edits)


def docref_rewrite(text):
    out, pos = [], 0
    for a, b, repl in docref_edits(text):
        out.append(text[pos:a])
        out.append(repl)
        pos = b
    out.append(text[pos:])
    return "".join(out)


def docref_problems(text, is_doc):
    """-> (offset, kind, text): a plain path of such a document in a Doxygen comment or page (`plain`), an alias of an unknown page or whose text is not the page's path (`unknown`, `label`), an alias in a
    comment that Doxygen does not read (`literal`)."""
    found = []
    if is_doc:
        for a, b, repl in docref_edits(text):
            found.append((a, "plain", "%s in a Doxygen comment or page: write %s" % (text[a:b], repl)))
    pages = {v: k for k, v in DOC_PAGES.items()}
    for m in DOCREF_USE.finditer(text):
        if m.group("page") not in pages:
            found.append((m.start(), "unknown", "%s: no such page (%s)" % (m.group("page"), ", ".join(sorted(pages)))))
        elif pages[m.group("page")] != m.group("text"):
            found.append((m.start(), "label", "the text of %s is %s" % (m.group("page"), pages[m.group("page")])))
        if not is_doc:
            found.append((m.start(), "literal", "%s in a comment Doxygen does not read: it would show as text; write the plain path" % m.group(0)))
    return found


def requirement_stems(root):
    """-> the names of the requirement files (REQ-CoreLR-D-Motion): the ids of their pages in the documentation."""
    return {os.path.splitext(os.path.basename(p))[0] for p in requirement_files(root)}


def protocol_atoms(segment):
    """-> the citations of a piece of the protocol, in order, as (offset, end, kind, prefix, number): kind `full` (REQ-CoreLR-D-X-0001, with its chain `, -0006` / `/-0003`: the same-prefix
    shorthand that the filter already knows), `short` (D-X-0001), `file` (REQ-CoreLR-D-X, no number) or `rel` (-0006: the prefix is the context's). prefix is `D-X-` (short form), None for rel."""
    atoms, covered = [], []
    for off, ident, wrapped, end, loose in citations(segment):
        atoms.append((off, end, "full", ident[len("REQ-CoreLR-"):-4], ident[-4:]))
        covered.append((off, end))
    for m in SHORT_CITE.finditer(segment):
        atoms.append((m.start(), m.end(), "short", m.group("id")[:-4], m.group("id")[-4:]))
    for m in FILE_CITE.finditer(segment):
        atoms.append((m.start(), m.end(), "file", m.group("file"), None))
    for m in REL_CITE.finditer(segment):
        if not any(a <= m.start() < b for a, b in covered):
            atoms.append((m.start(), m.end(), "rel", None, m.group("n")))
    return sorted(atoms)


class ProtocolContext:
    """What a relative citation (`-0006`) of the protocol may take its prefix from: the citation before it in the same piece of text (a line, or a cell of a table), else the first
    citation after it in the same row of a table, else the citation above it in the same column of the same table. Nothing else: a guess would be a wrong link."""
    def __init__(self):
        self.columns = {}


def protocol_line(line, ctx, ids, stems, found=None, number=0):
    """-> the line of the protocol with its short and relative citations written as full ones (`D-X-0001` and `-0001` become `REQ-CoreLR-D-X-0001`), so that the existing rules turn them
    into links; the same number of lines. A relative citation that has no context, or whose resolution names no requirement, is left as it is and told in `found` as
    (number, "relative", text); a citation that names no requirement is told as (number, "unknown", text)."""
    found = [] if found is None else found
    table = line.lstrip().startswith("|")
    if not table:
        ctx.columns = {}
        segments = [(0, line, 0)]
    else:
        parts = line.split("|")
        segments, at = [], 0
        for k, part in enumerate(parts):
            segments.append((k, part, at))
            at += len(part) + 1
    atoms = {k: protocol_atoms(part) for k, part, _ in segments}
    row_prefix = {}
    for k, _, _ in segments:                                                  # the first full or short citation of each cell: what an earlier relative one of the row may use
        for a in atoms[k]:
            if a[2] in ("full", "short"):
                row_prefix[k] = a[3]
                break
    edits = []
    for k, part, base in segments:
        prefix = None
        for off, end, kind, pre, n in atoms[k]:
            text = part[off:end]
            if kind in ("full", "short"):
                prefix = pre
                ident = "REQ-CoreLR-%s%s" % (pre, n)
                if ident not in ids:
                    found.append((number, "unknown", "%s is cited and no heading defines it" % ident))
                if kind == "short":
                    edits.append((base + off, base + end, ident))
            elif kind == "file":
                if "REQ-CoreLR-" + pre not in stems:
                    found.append((number, "unknown", "REQ-CoreLR-%s is cited and no requirement file has that name" % pre))
            else:
                use = prefix
                if use is None and table:
                    later = [row_prefix[j] for j in sorted(row_prefix) if j >= k]
                    use = later[0] if later else ctx.columns.get(k)
                ident = "REQ-CoreLR-%s%s" % (use, n) if use else None
                if ident in ids:
                    edits.append((base + off, base + end, ident))
                    prefix = use
                else:
                    found.append((number, "relative", "%s: no prefix to take from its context%s" % (text, "" if use is None else " (%s names no requirement)" % ident)))
        if table and prefix is not None:
            ctx.columns[k] = prefix
    for start, end, text in sorted(edits, reverse=True):
        line = line[:start] + text + line[end:]
    return line


def protocol_cite_problems(root, ids):
    """-> (place, kind, text) for the citations of docs/protocol/SerialProtocol.md that name no requirement (`unknown`), and, as kind `relative`, the relative ones that have no context. The
    first are errors; the second are told to the file's owner (they are not ours to write): see --unresolved."""
    path = os.path.join(root, PROTOCOL)
    found = []
    if not os.path.isfile(path):
        return found
    ctx, fenced, stems = ProtocolContext(), False, requirement_stems(root)
    for n, line in enumerate(read(path).split("\n"), 1):
        if FENCE.match(line):
            fenced = not fenced
        if fenced or FENCE.match(line) or line.startswith("#"):
            continue
        protocol_line(line, ctx, ids, stems, found, n)
    return [("%s:%d" % (PROTOCOL.replace(os.sep, "/"), n), kind, text) for n, kind, text in found]


def proto_label(item):
    """The Doxygen label of an item of the protocol: `proto-` and the item, its dots as dashes (a dot ends a label): 4.6 -> proto-4-6, FW-03 -> proto-FW-03."""
    return "proto-" + item.replace(".", "-")


def protocol_ids(root):
    """-> the set of the items that the protocol defines: its numbered sections (`4.6`), and the requests, decisions, points and measurements that open a table row (`FW-03`, `D1`, `P7`, `M-1`):
    the FIRST definition of each (a later table that repeats an item is not its definition)."""
    path = os.path.join(root, PROTOCOL)
    ids = set()
    if os.path.isfile(path):
        fenced = False
        for line in read(path).split("\n"):
            if FENCE.match(line):
                fenced = not fenced
            if fenced:
                continue
            h = PROTO_HEAD.match(line)
            r = PROTO_ROW.match(line)
            if h:
                ids.add(h.group(1))
            elif r:
                ids.add(r.group(2))
    return ids


def proto_link(item):
    return "@protosec{%s,%s}" % (proto_label(item)[len("proto-"):], item) if SECTION_ID.match(item) else "@proto{%s}" % item


def proto_edits(text, known):
    """-> the (start, end, replacement) that write the plain citations of the protocol's items as aliases, outside what is already an alias: `FW-03`; `§4.6`; an item after the file's name
    (`SerialProtocol.md 4.10`, `SerialProtocol.md P7`, `SerialProtocol.md` section 4.6); `(P7`, `P2/P6`, `, P16 (a)`; and the ids after `point`, `decision` or `request`. Only an item that the
    protocol defines (`known`) is taken."""
    skip = [(m.start(), m.end()) for m in PROTO_ATOM.finditer(text)]
    edits = []

    def add(a, b, item):
        if item in known and not any(a < e and s < b for s, e in skip) and not any(a < e and s < b for s, e, _ in edits):
            edits.append((a, b, proto_link(item)))

    for m in re.finditer(r"§\s?(\d+(?:\.\d+)*)(?![\d]|\.\d)", text):
        add(m.start(1), m.end(1), m.group(1))
    for m in re.finditer(r"(?<![\w@{-])(FW-\d{2})(?!\d)", text):
        add(m.start(1), m.end(1), m.group(1))
    for m in re.finditer(r"SerialProtocol\.md`?[ ,]+(?:section |§ ?)?(\d+(?:\.\d+)*(?![\d]|\.\d)|P\d{1,2}\b|D\d\b)", text):
        add(m.start(1), m.end(1), m.group(1))
        pos = m.end(1)
        while True:                                          # the items that follow in the same list: `SerialProtocol.md 4.4, 4.6, 4.7`
            q = re.match(r"(?:\}|\))?,\s*(\d+(?:\.\d+)*(?![\d]|\.\d)|P\d{1,2}\b|D\d\b)", text[pos:])
            if not q:
                break
            add(pos + q.start(1), pos + q.end(1), q.group(1))
            pos += q.end(1)
    for m in re.finditer(r"@protosec\{[^,}]*,[^}]*\}", text):          # the sections that follow an alias of a section in the same list: `@protosec{4-4,4.4}, 4.6, 4.7`
        pos = m.end()
        while True:
            q = re.match(r",\s*(\d+(?:\.\d+)*)(?![\d]|\.\d)", text[pos:])
            if not q:
                break
            add(pos + q.start(1), pos + q.end(1), q.group(1))
            pos += q.end(1)
    for m in re.finditer(r"\bP\d{1,2}(?:/P\d{1,2})+\b", text):
        for q in re.finditer(r"P\d{1,2}", m.group(0)):
            add(m.start() + q.start(), m.start() + q.end(), q.group(0))
    for m in re.finditer(r"(?:(?<=\()|(?<=, ))(P\d{1,2})\b(?=[,);]| \([a-z]\))", text):
        add(m.start(1), m.end(1), m.group(1))
    for m in re.finditer(r"\b(?:points?|decisions?|requests?) ((?:P|D|FW-)\d{1,2}(?:(?:,|\s+and|\s+or)\s+(?:P|D|FW-)\d{1,2})*)", text):
        for q in re.finditer(r"(?:P|D|FW-)\d{1,2}", m.group(1)):
            add(m.start(1) + q.start(), m.start(1) + q.end(), q.group(0))
    return sorted(edits)


def proto_rewrite(text, known):
    out, pos = [], 0
    for a, b, repl in proto_edits(text, known):
        out.append(text[pos:a])
        out.append(repl)
        pos = b
    out.append(text[pos:])
    return "".join(out)


def tag_of(lines, i):
    """-> the image that the requirement whose heading is lines[i] applies to alone (the tag line is the one after its anchor), or None."""
    j = i + 1
    if j < len(lines) and ANCHOR.match(lines[j]):
        j += 1
    m = TAG.match(lines[j]) if j < len(lines) else None
    return m.group(1) if m else None


def common_tag(lines):
    """-> the image that EVERY requirement of a file applies to alone, or None (some have no tag, or they differ, or there is no requirement)."""
    tags = [tag_of(lines, i) for i, line in enumerate(lines) if HEADING.match(line)]
    return tags[0] if tags and tags[0] and all(t == tags[0] for t in tags) else None


def requirement_images(root):
    """-> {file name without .md: the image that all its requirements apply to alone, or None}."""
    return {os.path.splitext(os.path.basename(p))[0]: common_tag(read(p).split("\n")) for p in requirement_files(root)}


def module_images(root):
    """-> {module: image} from cmake/variant_modules.txt: the modules that exist in one image only (an entry `firmware/src/<Module>.<h|c|cpp>|<image>`)."""
    path = os.path.join(root, "cmake", "variant_modules.txt")
    found = {}
    if os.path.isfile(path):
        for line in read(path).splitlines():
            line = line.split("#")[0].strip()
            if "|" in line:
                entry, image = (x.strip() for x in line.split("|", 1))
                if entry.startswith("firmware/src/") and image in IMAGES:
                    found[os.path.splitext(os.path.basename(entry))[0]] = image
    return found


def tag_problems(root):
    found = []
    modules = module_images(root)
    for path in requirement_files(root):
        lines = read(path).split("\n")
        rel = os.path.relpath(path, root)
        heads = {i for i, line in enumerate(lines) if HEADING.match(line)}
        ok_places = set()
        for i in heads:
            j = i + 2 if i + 1 < len(lines) and ANCHOR.match(lines[i + 1]) else i + 1
            ok_places.add(j)
        for n, line in enumerate(lines):
            if TAG_ANY.match(line):
                if not TAG.match(line):
                    found.append(("%s:%d" % (rel, n + 1), "tag", "an applicability tag is `*Applies to: serial image only*` or `*Applies to: sd image only*`"))
                elif n not in ok_places:
                    found.append(("%s:%d" % (rel, n + 1), "tag", "the tag must be on the line after the anchor of a requirement heading"))
        m = re.match(r"REQ-CoreLR-D-(.+)\.md$", os.path.basename(path))
        if m and m.group(1) in modules:
            for i in sorted(heads):
                if tag_of(lines, i) != modules[m.group(1)]:
                    found.append(("%s:%d" % (rel, i + 1), "tag", "%s: the module %s belongs to the %s image only (cmake/variant_modules.txt): add *Applies to: %s image only*" %
                                  (HEADING.match(lines[i]).group(1), m.group(1), modules[m.group(1)], modules[m.group(1)])))
    return found


def spec_problems(root, ids, known=frozenset()):
    """-> the citations in the prose of the requirement files that name no requirement (the heading's own identifier is the definition, not a citation)."""
    found = []
    for path in requirement_files(root):
        text = read(path)
        rel = os.path.relpath(path, root)
        fenced = False
        for n, line in enumerate(text.split("\n"), 1):
            if FENCE.match(line):
                fenced = not fenced
            if fenced:
                continue
            for c in COMMAND.finditer(line):
                found.append(("%s:%d" % (rel, n), "command", "%s: a Doxygen command in a requirement file; name it without the backslash" % c.group(0)))
            if HEADING.match(line) or ANCHOR_ANYWHERE.search(line):
                continue
            for m in re.finditer(r"(?<![\w@{-])(FW-\d{2})(?!\d)", line):
                if known and m.group(1) not in known:
                    found.append(("%s:%d" % (rel, n), "spec", "%s is cited and the protocol does not define it" % m.group(1)))
            for off, ident, wrapped, end, loose in citations(line):
                if ident not in ids:
                    found.append(("%s:%d" % (rel, n), "spec", "%s is cited and no heading defines it" % ident))
    return found


def check(root):
    ids = defined(root)
    known = protocol_ids(root)
    return (anchor_problems(root) + comment_problems(root, ids, known) + page_problems(root, ids, known) + spec_problems(root, ids, known) + tag_problems(root)
            + [x for x in protocol_cite_problems(root, ids) if x[1] != "relative"])


def file_links(text, stems):
    """-> the text with each citation of a requirement file by its name (`REQ-CoreLR-D-LcdI2c`, no number) written as a link to the page of that file."""
    return FILE_CITE.sub(lambda m: '\\ref %s "%s"' % (m.group(0), m.group(0)) if m.group(0) in stems else m.group(0), text)


def link_citations(line, known=None, stems=None):
    """-> the line of a requirement file as Doxygen is to read it: its citations written as @req{} (plain ones, alone in a code span, or alone as the text of a link: the alias does
    not work inside a code span nor inside a link's text). A requirement file must not name a Doxygen command with its backslash (`\\if`, `\\dot`...): once a heading has a label Doxygen reads it, even in a code span (check `command`)."""
    def plain(text):
        text = docref_rewrite(rewrite(text))
        text = proto_rewrite(text, known) if known else text
        return file_links(text, stems) if stems else text

    def spans(text):
        out, pos, after = [], 0, False
        for m in SPAN.finditer(text):
            out.append(plain_after(text[pos:m.start()], after))
            content = m.group(2).strip()
            if content in DOC_PAGES:
                out.append(docref_link(content))                       # `docs/protocol/EepromLayout.md` alone in a code span: the alias does not work inside one
            elif stems and content in stems:
                out.append(file_links(content, stems))                 # `REQ-CoreLR-D-LcdI2c` alone in a code span: the page of that requirement file
            else:
                out.append(rewrite(content) if CHAIN.match(content) else m.group(0))
            after = content.endswith("SerialProtocol.md")           # `docs/protocol/SerialProtocol.md` P7: the item after the code span is the protocol's
            pos = m.end()
        out.append(plain_after(text[pos:], after))
        return "".join(out)

    def plain_after(text, after):
        if not (after and known):
            return plain(text)
        prefix = "SerialProtocol.md`"
        return plain(prefix + text)[len(prefix):]
    line = IMAGE_LINK.sub(r"](\1)", line)                     # a picture of docs/hardware/images: Doxygen finds it by its name (IMAGE_PATH) and copies it into the documentation
    out, pos = [], 0
    for m in LINK.finditer(line):
        out.append(spans(line[pos:m.start()]))
        text = m.group(1).replace("`", "").strip()
        out.append(rewrite(text) if CHAIN.match(text) else m.group(0))       # a link to a requirement becomes the link of the requirement itself; any other link is left alone
        pos = m.end()
    out.append(spans(line[pos:]))
    return URL_LOOKALIKE.sub(lambda m: m.group(1) + "\u200b:", "".join(out))     # `file:line` in a text is taken for a URL of a file: Doxygen would write a link that goes nowhere; a zero-width space before the colon prevents it


def filter_text(text, stem, image=None, applies=None, protocol=False, known=None, ids=None, stems=None):
    """-> the requirement file as Doxygen is to read it (see --filter): the same number of lines. For `image` (serial or sd), what applies to the other image only is marked as not part of this one."""
    lines = text.split("\n")
    whole = applies or common_tag(lines)
    out, fenced, titled, i = [], False, False, 0
    labelled = set()
    ctx = ProtocolContext()

    def other(tag):
        return image is not None and tag is not None and tag != image

    while i < len(lines):
        line = lines[i]
        if FENCE.match(line):
            fenced = not fenced
        if fenced or FENCE.match(line):
            out.append(line)
            i += 1
            continue
        h = HEADING.match(line)
        t = TAG.match(line)
        ph = PROTO_HEAD.match(line) if protocol else None
        pr = PROTO_ROW.match(line) if protocol else None
        if ph and ph.group(1) not in labelled:
            labelled.add(ph.group(1))
            out.append("%s {#%s}" % (URL_LOOKALIKE.sub(lambda m: m.group(1) + "\u200b:", line), proto_label(ph.group(1))))
            i += 1
            continue
        if pr and pr.group(2) not in labelled:
            labelled.add(pr.group(2))
            if ids is not None:
                line = protocol_line(line, ctx, ids, stems or set())
            first, bar, rest = line[1:].lstrip().partition("|")        # the first cell is the item itself (its definition: no link); the cells after it are read as any other row
            out.append('| <a id="%s"></a>%s%s%s' % (proto_label(pr.group(2)), first, bar, link_citations(rest, known, stems)))      # the first definition of an item of the protocol: the target of its links
            i += 1
            continue
        if not titled and line.startswith("# "):
            out.append("%s%s {#%s}" % (line, " (%s image only: not in this image)" % whole if other(whole) else "", stem))
            titled = True
        elif h:
            tag = tag_of(lines, i)
            out.append("%s%s {#%s}" % (line, " (%s image only: not in this image)" % tag if other(tag) else "", h.group(1)))
            a = ANCHOR.match(lines[i + 1]) if i + 1 < len(lines) else None
            if a and a.group(1) == h.group(1):
                out.append("")                                   # the label of the heading is the target now: the anchor line would define it twice
                i += 1
        elif t:
            out.append("**Not part of the %s image.** It applies to the %s image only." % (image, t.group(1)) if other(t.group(1)) else line)
        elif line.startswith("#") or ANCHOR.match(line):          # another heading, or the anchor of a heading that is not the one above: as it is (a URL lookalike apart)
            out.append(URL_LOOKALIKE.sub(lambda m: m.group(1) + "\u200b:", line))
        else:
            if protocol and ids is not None:
                line = protocol_line(line, ctx, ids, stems or set())       # the short, relative and file-level citations of the protocol, written in full where the context says which
            out.append(link_citations(line, known, stems if protocol else None))
        i += 1
    return "\n".join(out)


def add_anchors(root):
    """Writes the missing anchors; -> the number of anchors written."""
    written = 0
    for path in requirement_files(root):
        lines = read(path).split("\n")
        out, i = [], 0
        while i < len(lines):
            out.append(lines[i])
            m = HEADING.match(lines[i])
            if m:
                nxt = lines[i + 1] if i + 1 < len(lines) else ""
                if not ANCHOR.match(nxt):
                    out.append('<a id="%s"></a>' % m.group(1))
                    written += 1
            i += 1
        if len(out) != len(lines):
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(out))
    return written


def rewrite(text):
    """-> the text with every plain citation written as @req{}: `A-0001..0003` becomes `@req{A-0001}..@req{A-0003}`, `A-0006, -0010` becomes `@req{A-0006}, @req{A-0010}`."""
    def one(prefix, n):
        return "@req{%s%s}" % (prefix, n)

    def repl(m):
        if m.group("at"):
            return m.group(0)
        out = one(m.group("prefix"), m.group("n"))
        if m.group("range"):
            out += ".." + one(m.group("prefix"), m.group("m"))
        return out
    out, pos = [], 0
    for m in CITE.finditer(text):
        if m.start() < pos:
            continue
        out.append(text[pos:m.start()])
        out.append(repl(m))
        pos = m.end()
        if m.group("at") and not m.group("range") and text[pos:pos + 1] == "}":
            out.append("}")                                  # the closing brace of the @req{...} that was already there
            pos += 1
        while True:
            s = SHORT.match(text, pos)
            if not s:
                break
            sep = text[pos:s.start("n")]
            out.append(sep[:-1] if sep.endswith("-") else sep)      # the separator, without the '-' that the identifier takes back
            out.append(one(m.group("prefix"), s.group("n")))
            if s.group("range"):
                out.append(".." + one(m.group("prefix"), s.group("m")))
            pos = s.end()
    out.append(text[pos:])
    return "".join(out)


def convert(root):
    """Rewrites the Doxygen comments of firmware/ and the pages of docs/doxygen/; -> the number of files changed."""
    changed = 0
    known = protocol_ids(root)
    for path in code_files(root):
        text = read(path)
        pieces, pos = [], 0
        for start, c, is_doc in comments(text):
            if is_doc:
                pieces.append(text[pos:start])
                pieces.append(proto_rewrite(docref_rewrite(rewrite(c)), known))
                pos = start + len(c)
        pieces.append(text[pos:])
        new = "".join(pieces)
        if new != text:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
            changed += 1
    for path in page_files(root):
        text = read(path)
        new = proto_rewrite(docref_rewrite(rewrite(text)), known)
        if new != text:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
            changed += 1
    return changed


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--root", default=REPO)
    ap.add_argument("--add-anchors", action="store_true")
    ap.add_argument("--convert", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--unresolved", action="store_true")
    ap.add_argument("--filter", metavar="FILE")
    ap.add_argument("--protocol", action="store_true")
    ap.add_argument("--image", choices=IMAGES)
    ap.add_argument("--applies", choices=IMAGES)
    args = ap.parse_args(argv)
    if args.help:
        print(__doc__)
        return 0
    if args.filter:
        sys.stdout.write(filter_text(read(args.filter), os.path.splitext(os.path.basename(args.filter))[0], args.image, args.applies, args.protocol, protocol_ids(args.root),
                                     defined(args.root) if args.protocol else None, requirement_stems(args.root) if args.protocol else None))
        return 0
    if args.unresolved:
        for where, kind, text in protocol_cite_problems(args.root, defined(args.root)):
            if kind == "relative":
                print("%s: %s" % (where, text))
        return 0
    if args.add_anchors and args.convert:
        print("req_refs.py: --add-anchors and --convert are two steps: run them one at a time", file=sys.stderr)
        return 2
    if args.add_anchors:
        print("%d anchor(s) written" % add_anchors(args.root))
    if args.convert:
        print("%d file(s) rewritten" % convert(args.root))
    problems = check(args.root)
    for where, kind, text in problems if args.list else problems[:40]:
        print("%s: [%s] %s" % (where, kind, text))
    if len(problems) > 40 and not args.list:
        print("... and %d more (--list)" % (len(problems) - 40))
    if not problems:
        print("ok: every requirement has its anchor, every citation names a requirement that exists, and the Doxygen comments use @req{}")
    return len(problems)


if __name__ == "__main__":
    sys.exit(main())
