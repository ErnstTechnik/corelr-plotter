# Requirements {#requirements}
<!-- Copyright (C) 2026 ErnstTechnik and the CoreLR contributors -- GPL-3.0-or-later. Read by Doxygen only (docs/doxygen/Doxyfile.in). -->

The requirements of the project, as the comments of this documentation cite them: each `@req{REQ-CoreLR-...}` of a comment is a link to the heading of that requirement. They are the text of the
repository's `requirements/` directory, unchanged: what a requirement says is the decision; the code and the comments say how it is realised.

- \subpage requirements_functional
- \subpage requirements_design

The **functional** requirements say *what* the system must do, independent of the implementation (`REQ-CoreLR-F-<Area>-nnnn`), one page for each area. The **design** requirements say *how*
a function is realised (algorithms, data structures, timing, interfaces), each stating which functional requirement it realises (`REQ-CoreLR-D-<Module>-nnnn`), one page for each module.

The requirements are the same for both images, except those that carry a tag, *Applies to: serial image only* or *Applies to: sd image only*: in the documentation of the other image their title says "not in this image" (and so does the title of a page all of whose requirements are tagged), but they stay, so that every link keeps working and the reason for what the other image lacks stays readable. The contract of the serial link, which several of them
refer to, is the page `SerialProtocol`.
