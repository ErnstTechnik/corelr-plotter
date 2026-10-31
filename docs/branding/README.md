# CoreLR branding

Logo of the CoreLR firmware: two cables run from the letters **L** and **R** (the two
motors, top corners) and meet at an orange pen point; the short line below is the ink.

| File | Use |
|---|---|
| `logo.svg`, `logo-512.png`, `logo-56.png`, `favicon-32.png` | Symbol alone (rounded blue square). Favicon, avatar, README badge; `logo-56.png` is the logo of the Doxygen header (shown at its own size, 56 px). |
| `logo-wordmark.svg`, `logo-wordmark.png` | Symbol + "CoreLR". Documentation headers, slides. |
| `logo-mono.svg` | One colour, no badge (black by default). Print, engraving, stickers. |
| `social-preview.svg`, `social-preview.png` | 1280 x 640, GitHub "Social preview" (repository Settings). |
| `machine-icon.svg`, `machine-icon-512.png`, `machine-icon-32.png` | 64 x 64 pictogram of the machine (two motors, two cables, the pen). Small illustrations, documentation. |
| `machine-schematic.svg`, `machine-schematic.png` | 640 x 440 schematic (PNG: 1280 x 880) of the machine: motors L and R, the distance D between the guide holes, the cable lengths lL and lR, the margins, the pen at the cables' intersection, the axes and gravity. Out of scale on purpose. For the documentation. |

## Colours

| Name | Hex | Where |
|---|---|---|
| CoreLR blue | `#2F7FDB` | badge, "Core" |
| Pen orange | `#F2711C` | pen point, "LR" |
| White | `#FFFFFF` | cables, letters and ink inside the badge |
| Navy | `#0F1B2D` | social-preview background only |
| Mist | `#C9D6E8` | social-preview tagline only |

The blue and the orange keep enough contrast on both white and near-black (`#0D1117`)
backgrounds, so the same files serve light and dark pages. The colours are defined once, in
the `<style>` block of each SVG.

## Fonts

The logo files reference no font: the "CoreLR" lettering is original (geometric letters drawn as
round-capped strokes, pen-drawn on purpose), and the tagline of `social-preview.svg` is Noto Sans
(SIL Open Font License 1.1) **converted to outlines**.

The exception is `machine-schematic.svg`: its labels are live text with `font-family: sans-serif`,
so that the schematic stays editable and its labels searchable; every system has such a font, and
the layout does not depend on the exact one. `machine-schematic.png` is affected: it shows the
sans-serif font of the machine that rendered it (Noto Sans, SIL OFL 1.1, on the maintainer's). The
other PNGs are not (their lettering is outlines).

## Usage

- Light or dark background: use the files as they are. For `logo-mono.svg`, change the one
  colour (`#000000` in the `<style>`) to white on a dark background.
- Minimum size: symbol 16 px (the L and R become a texture, the V and the orange dot stay
  readable); wordmark 120 px wide.
- Clear space: at least a quarter of the symbol's width on every side.
- Do not: stretch or rotate it, recolour the badge or the pen point, add shadows or
  gradients, put the wordmark's orange on an orange background, or place it on a busy photo
  without a plain backing.
- PNGs are rendered from the SVGs (transparent background, except the social preview);
  the SVGs are the source. After editing an SVG, regenerate the PNGs with
  `tools/generators/gen_branding_png.py` (librsvg through PyGObject; `-l` lists the table of
  PNG / source SVG / width, and a PNG name renders only that one). Never edit a PNG by hand.

## Licence

Copyright (C) 2026 ErnstTechnik and the CoreLR contributors.

The logo files in this directory (SVG and PNG) are licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
Forks, articles and talks may show it, with attribution (name the CoreLR project and link
to its repository) and an indication of any change. The firmware itself stays under GPLv3;
this licence covers only the logo.

The drawing is original; it reuses no mark or artwork of another project (grbl, Walldraw,
Arduino, Candle).
