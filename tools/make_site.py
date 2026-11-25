#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# AMALTHEA (as TONITOR's, NORILSK's and BIGORNA's): the website, in docs/ (GitHub Pages):
#   docs/index.html, screens.html, install.html, projects.html, apps.html   site/*.html in the common frame
#   docs/manual.html     MANUAL.md
#   docs/changelog.html  CHANGELOG.md
#   docs/assets/screens/ the Brick's screen as the appliance draws it (host test renders and device captures)
# The logo: tools/make_logo.py; the stylesheet and script: docs/css, docs/js, written by hand.
# The markdown is the little the documents use: headings, paragraphs, lists, tables, quotes, code, emphasis, links.
import hashlib
import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
REPO = "https://github.com/nzimas/amalthea"
# the stylesheet's address changes with it, so browsers never keep an old one
CSS_V = hashlib.sha256((DOCS / "css/site.css").read_bytes()).hexdigest()[:10]
LOGO_V = hashlib.sha256((DOCS / "assets/logo-dark.svg").read_bytes()).hexdigest()[:10]
BUILD = (ROOT / "app/BUILD_NUMBER").read_text().strip()


def slug(text):
    s = re.sub(r"<[^>]+>", "", text).lower()
    s = re.sub(r"[^\w\s-]", "", s, flags=re.U)
    return re.sub(r"\s+", "-", s.strip())


def inline(t):
    codes = []

    def keep(m):
        codes.append(f"<code>{html.escape(m[1])}</code>")
        return f"\0{len(codes) - 1}\0"
    t = re.sub(r"`([^`]+)`", keep, t)
    if not re.search(r"<[a-z/][^>]*>", t):
        t = html.escape(t, quote=False)

    def link(m):
        url = m[2]
        if not re.match(r"[a-z]+:|#", url):        # a file in the repository
            url = f"{REPO}/blob/main/{url}"
        return f'<a href="{html.escape(url)}">{m[1]}</a>'
    t = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, t)
    t = re.sub(r"<(https?://[^>]+)>", r'<a href="\1">\1</a>', t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"(?<![\w*])\*([^*\s][^*]*?)\*(?![\w*])", r"<i>\1</i>", t)
    t = t.replace(" -- ", " — ").replace(" --\n", " —\n")      # (the changelog's dashes)
    return re.sub(r"\0(\d+)\0", lambda m: codes[int(m[1])], t)


def to_html(md, toc):
    out, lines, i = [], md.splitlines(), 0

    def para(buf):
        if buf:
            out.append("<p>" + inline(" ".join(buf)) + "</p>")
            buf.clear()
    buf = []
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            para(buf)
            i += 1
        elif ln.startswith("```"):
            para(buf)
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j])) + "</code></pre>")
            i = j + 1
        elif m := re.match(r"(#{1,4})\s+(.*)", ln):
            para(buf)
            lv, text = len(m[1]), m[2]
            sid = slug(text)
            out.append(f'<h{lv} id="{sid}">{inline(text)}</h{lv}>')
            if lv in (2, 3):
                toc.append((lv, sid, re.sub(r"<[^>]+>", "", inline(text))))
            i += 1
        elif ln.startswith("|"):
            para(buf)
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [c.strip() for c in re.split(r"(?<!\\)\|", lines[i].strip())[1:-1]]
                if not all(re.fullmatch(r":?-+:?", c) for c in cells):
                    rows.append(cells)
                i += 1
            t = ["<table>", "<tr>" + "".join(f"<th>{inline(c)}</th>" for c in rows[0]) + "</tr>"]
            t += ["<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in rows[1:]]
            out.append("\n".join(t + ["</table>"]))
        elif ln.startswith(">"):
            para(buf)
            q = []
            while i < len(lines) and lines[i].startswith(">"):
                q.append(lines[i][1:].strip())
                i += 1
            q = [x for x in q if not re.fullmatch(r"\[!\w+\]", x)]
            out.append("<blockquote><p>" + inline(" ".join(q)) + "</p></blockquote>")
        elif re.match(r"\s*[-*] ", ln):
            para(buf)
            items, depth0 = [], len(ln) - len(ln.lstrip())
            while i < len(lines) and (re.match(r"\s*[-*] ", lines[i]) or (lines[i].startswith("  ") and lines[i].strip())):
                d = len(lines[i]) - len(lines[i].lstrip())
                if re.match(r"\s*[-*] ", lines[i]):
                    items.append([d > depth0, lines[i].strip()[2:]])
                else:
                    items[-1][1] += " " + lines[i].strip()
                i += 1
            h, open_sub = ["<ul>"], False
            for sub, text in items:
                if sub and not open_sub:
                    h[-1] = h[-1].removesuffix("</li>")
                    h.append("<ul>")
                    open_sub = True
                elif not sub and open_sub:
                    h.append("</ul></li>")
                    open_sub = False
                h.append(f"<li>{inline(text)}</li>")
            if open_sub:
                h.append("</ul></li>")
            out.append("\n".join(h + ["</ul>"]))
        elif re.match(r"\d+\. ", ln):                 # (a numbered list: its items, each perhaps run on)
            para(buf)
            items = []
            while i < len(lines) and (re.match(r"\d+\. ", lines[i]) or (lines[i].startswith("   ") and lines[i].strip())):
                if re.match(r"\d+\. ", lines[i]):
                    items.append(re.sub(r"^\d+\. ", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ol>\n" + "\n".join(f"<li>{inline(t)}</li>" for t in items) + "\n</ol>")
        elif ln.lstrip().startswith("<"):
            para(buf)
            out.append(ln)
            i += 1
        else:
            buf.append(ln.strip())
            i += 1
    para(buf)
    return "\n".join(out)


def page(title, current, body, desc, head=""):
    """every page: the menu down the left (above the content on a narrow screen), the content, the footer"""
    nav = [("./", "Home"), ("screens.html", "Screens"), ("install.html", "Install"), ("manual.html", "Manual"),
           ("changelog.html", "Changelog"), ("projects.html", "Other projects"), ("apps.html", "Apps"), (REPO, "GitHub")]
    links = "\n      ".join(f'<a href="{h}"' + (' aria-current="page"' if n == current else "") + f">{n}</a>"
                           for h, n in nav)
    donate = lambda cls, pad: f"""<form class="donate {cls}" action="https://www.paypal.com/cgi-bin/webscr" method="post" target="_top">
{pad}  <input type="hidden" name="cmd" value="_xclick">
{pad}  <input type="hidden" name="business" value="nzimas.lv@gmail.com">
{pad}  <input type="hidden" name="item_name" value="Support Amalthea">
{pad}  <input type="hidden" name="currency_code" value="EUR">
{pad}  <input type="image" src="https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif" name="submit"
{pad}         title="PayPal - The safer, easier way to pay online!" alt="Donate with PayPal button">
{pad}</form>"""
    return f"""<!doctype html>
<!-- SPDX-License-Identifier: MIT -- Amalthea website (generated by tools/make_site.py from site/, MANUAL.md, CHANGELOG.md) -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<title>{title}</title>
<meta name="description" content="{desc}">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{desc}">
<meta property="og:image" content="https://nzimas.github.io/amalthea/assets/screens/home.png">
<link rel="icon" href="assets/mark.svg?v={LOGO_V}" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Atkinson+Hyperlegible:wght@400;700&family=Oxanium:wght@500;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="css/site.css?v={CSS_V}">{head}
</head>
<body>
<div class="layout">
  <aside class="side">
    <div class="top">
      <a class="brand" href="./"><img src="assets/logo-dark.svg?v={LOGO_V}" alt="Amalthea" width="477" height="80"></a>
      <button class="menu-btn" type="button" aria-label="Menu" aria-controls="site-nav" aria-expanded="false"><span></span><span></span><span></span></button>
    </div>
    <nav id="site-nav" aria-label="Site">
      <button class="menu-close" type="button" aria-label="Close the menu">&times;</button>
      {links}
    </nav>
    <div class="menu-shade" hidden></div>
    {donate("donate-side", "    ")}
  </aside>
  <div class="content">
    <main>
{body}
    </main>
    <footer>
      <p>Amalthea build {BUILD}. Developed by <a href="https://nunozimas.com">Nuno Zimas</a>. Free software under the
        <a href="{REPO}/blob/main/LICENSE">GNU GPL v3</a>. Not affiliated with TRIMUI.</p>
      {donate("donate-foot", "      ")}
    </footer>
  </div>
</div>
<script src="js/menu.js?v={CSS_V}"></script>
</body>
</html>
"""


def fragments():
    src = ROOT / "site"
    pages = [
        ("index.html", "home.html", "Amalthea — a custom Linux distribution for the TRIMUI Brick", "Home",
         "Amalthea: a custom Linux distribution that replaces the TRIMUI Brick's firmware with a platform for "
         "musical instruments: shared engines, effects and generators, and new instruments with every build."),
        ("screens.html", "screens.html", "Amalthea on the Brick's screen", "Screens",
         "Amalthea on the TRIMUI Brick's screen: the Tracker, the FX rack and LIFEBLOOD."),
        ("install.html", "install.html", "Install Amalthea", "Install",
         "Install Amalthea Linux on a TRIMUI Brick over USB from macOS, Linux or Windows, update it, "
         "or go back to the stock system."),
        ("web-install.html", "web-install.html", "Install Amalthea from the browser (experimental)", "Install",
         "Experimental: install Amalthea Linux on a TRIMUI Brick from Chrome or Edge over WebUSB, or go back "
         "to the stock system."),
        ("projects.html", "projects.html", "Other projects by Nuno Zimas", "Other projects",
         "Firmware and instruments by Nuno Zimas for the M-VAVE FM-1, the Elektron Model:Cycles and Digitone II, "
         "the Ableton Move, the Akai MPC One and macOS."),
        ("apps.html", "apps.html", "Apps by Nuno Zimas", "Apps",
         "Music apps by Nuno Zimas for phones and tablets: Vascular, a touch instrument for iOS and Android."),
    ]
    js_v = hashlib.sha256((DOCS / "js/web-install.js").read_bytes() + (DOCS / "js/vendor/webadb.js").read_bytes()).hexdigest()[:10]
    for out, frag, title, cur, desc in pages:
        body = (src / frag).read_text().replace('src="js/web-install.js"', f'src="js/web-install.js?v={js_v}"')
        (DOCS / out).write_text(page(title, cur, body, desc))


def manual():
    md = (ROOT / "MANUAL.md").read_text()
    md = md.split("\n", 1)[1]                                     # (the title: the page has its own)
    md = md.replace("(README.md#installing)", "(install.html)")
    toc = []
    body = to_html(md, toc)
    t = "\n".join(f'<a href="#{s}"' + (' class="sub"' if lv == 3 else "") + f">{html.escape(n)}</a>" for lv, s, n in toc)
    intro = (f'<h1>Manual</h1>\n<nav class="toc" aria-label="Contents">\n{t}\n</nav>')
    (DOCS / "manual.html").write_text(page("Amalthea manual", "Manual", '<div class="manual">' + intro + "\n" + body + "</div>",
                                           "The Amalthea manual: every control of the Tracker, FX and LIFEBLOOD on the TRIMUI Brick."))


def changelog():
    md = (ROOT / "CHANGELOG.md").read_text()
    body = "<h1>Changelog</h1>\n" + to_html(md, [])
    (DOCS / "changelog.html").write_text(page("Amalthea changelog", "Changelog", body, "What changed in each Amalthea build."))


if __name__ == "__main__":
    fragments()
    manual()
    changelog()
    (DOCS / ".nojekyll").write_text("")
    print("site: docs/index.html, screens.html, install.html, web-install.html, manual.html, changelog.html, projects.html, apps.html")
