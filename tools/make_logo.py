#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The website's logo, from the appliance's own identity: the horn-of-plenty mark
# (app/assets/identity/amalthea.svg, drawn by src/brand.cpp) and the wordmark in
# Oxanium SemiBold (app/assets/Oxanium.ttf, SIL OFL), set as outlines.
#   docs/assets/logo-dark.svg   cream wordmark, for dark backgrounds (the site, GitHub dark)
#   docs/assets/logo-light.svg  ink wordmark, for light backgrounds (GitHub light)
#   docs/assets/mark.svg        the mark on a dark tile (favicon)
# Needs fontTools (pip install fonttools).
import re
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "assets"
mark_svg = (ROOT / "app/assets/identity/amalthea.svg").read_text()
polys = re.findall(r"<polygon[^>]*/>", mark_svg)
font = TTFont(ROOT / "app/assets/Oxanium.ttf")
if "fvar" in font:
    from fontTools.varLib.instancer import instantiateVariableFont
    font = instantiateVariableFont(font, {"wght": 600})
gs, cmap = font.getGlyphSet(), font.getBestCmap()
upm, cap = font["head"].unitsPerEm, font["OS/2"].sCapHeight or 700


def word(text, x0, baseline, cap_px, tracking):
    scale = cap_px / cap
    pen = SVGPathPen(gs)
    x = 0.0
    for ch in text:
        g = cmap[ord(ch)]
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + x, baseline))
        gs[g].draw(tp)
        x += gs[g].width * scale + tracking
    return pen.getCommands(), x - tracking


def logo(fill, name):
    d, w = word("AMALTHEA", 104, 60, 40, 9)
    width = int(104 + w + 4)
    body = "".join(polys).replace('fill="#f2ede2"', f'fill="{fill}"')
    (OUT / name).write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} 80" width="{width}" height="80">'
        f'<title>Amalthea</title><g transform="translate(0 0) scale(0.625)">{body}</g>'
        f'<path fill="{fill}" d="{d}"/></svg>\n')
    return width


w = logo("#f2ede2", "logo-dark.svg")
logo("#0a0f13", "logo-light.svg")
(OUT / "mark.svg").write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"><rect width="128" height="128" rx="22" fill="#0a0f13"/>'
    f'<g transform="translate(6 6) scale(0.906)">{"".join(polys)}</g></svg>\n')
print(f"logo: {w}x80")
