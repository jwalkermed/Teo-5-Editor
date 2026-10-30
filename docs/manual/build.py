# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The TEO-5 Editor project
"""Builds the user's guide HTML from content.html.
Usage: build.py VERSION OUTDIR   (OUTDIR/img/*.jpg are the screenshots; writes OUTDIR/manual.html)
packaging/make-manual.sh renders the screenshots and prints the result to PDF."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
VERSION, OUT = sys.argv[1], sys.argv[2]
W, H = 3400, 1106  # rendered image size


def shot(name, caption, x0=0.0, x1=1.0, y0=0.0, y1=1.0, width="100%"):
    """A screenshot cropped to fractions of the full image."""
    fw, fh = x1 - x0, y1 - y0
    ratio = (fw * W) / (fh * H)
    return f'''<figure style="width:{width}">
  <div class="crop" style="aspect-ratio:{ratio:.4f}">
    <img src="img/{name}.jpg" style="width:{100 / fw:.3f}%;left:{-x0 / fw * 100:.3f}%;top:{-y0 / fh * 100:.3f}%">
  </div>
  <figcaption>{caption}</figcaption>
</figure>'''


def img(name, caption, width="60%"):
    return f'<figure style="width:{width}"><img class="plain" src="img/{name}.jpg"><figcaption>{caption}</figcaption></figure>'


body = open(os.path.join(HERE, "content.html")).read()
body = body.replace("{{VERSION}}", VERSION)
# {{shot name|caption|x0|x1|y0|y1|width}} and {{img name|caption|width}}
body = re.sub(r"\{\{shot ([^}]*)\}\}", lambda m: shot(*[
    float(v) if i in (2, 3, 4, 5) else v for i, v in enumerate(m.group(1).split("|"))]), body)
body = re.sub(r"\{\{img ([^}]*)\}\}", lambda m: img(*m.group(1).split("|")), body)

page = f'''<!doctype html>
<html><head><meta charset="utf-8"><title>TEO-5 Editor User's Guide</title>
<style>
@page {{ size: Letter; margin: 0.7in 0.75in 0.75in 0.75in;
        @bottom-center {{ content: counter(page); font: 9pt Helvetica; color: #666; }} }}
body {{ font: 10.5pt/1.45 "Helvetica Neue", Helvetica, Arial, sans-serif; color: #1d1f22; }}
h1 {{ font-size: 21pt; margin: 0 0 6pt; color: #12161c; break-before: page; border-bottom: 2px solid #2f6db5;
      padding-bottom: 4pt; }}
h1.first {{ break-before: avoid; }}
h2 {{ font-size: 13.5pt; margin: 16pt 0 4pt; color: #1f4f88; break-after: avoid; }}
h3 {{ font-size: 11pt; margin: 12pt 0 2pt; break-after: avoid; }}
p {{ margin: 4pt 0 6pt; }}
ul, ol {{ margin: 3pt 0 7pt; padding-left: 17pt; }}
li {{ margin: 1.5pt 0; }}
b, strong {{ color: #0f1215; }}
.k {{ font-family: "Helvetica Neue", Helvetica; font-weight: 600; font-size: 9pt; letter-spacing: .04em;
      background: #e9edf2; border: 1px solid #c9d1db; border-radius: 3px; padding: 0 3pt; white-space: nowrap; }}
figure {{ margin: 8pt auto 10pt; break-inside: avoid; }}
.crop {{ position: relative; overflow: hidden; width: 100%; border-radius: 4px; border: 1px solid #0d0f12; }}
.crop img {{ position: absolute; display: block; }}
img.plain {{ width: 100%; display: block; border-radius: 4px; border: 1px solid #0d0f12; }}
figcaption {{ font-size: 8.8pt; color: #555; margin-top: 3pt; text-align: center; }}
table {{ border-collapse: collapse; width: 100%; margin: 4pt 0 8pt; font-size: 9.6pt; break-inside: auto; }}
th, td {{ text-align: left; vertical-align: top; padding: 3pt 6pt; border-bottom: 1px solid #dde2e8; }}
th {{ background: #eef2f6; font-weight: 600; }}
tr {{ break-inside: avoid; }}
td:first-child {{ white-space: nowrap; font-weight: 600; }}
.note {{ background: #fff6e6; border-left: 3px solid #e8a33a; padding: 5pt 8pt; margin: 7pt 0; break-inside: avoid; }}
.tip {{ background: #eaf3ff; border-left: 3px solid #2f6db5; padding: 5pt 8pt; margin: 7pt 0; break-inside: avoid; }}
.cover {{ text-align: center; padding-top: 1.1in; }}
.cover h1 {{ border: none; font-size: 34pt; margin-bottom: 2pt; break-before: avoid; }}
.cover .sub {{ font-size: 14pt; color: #444; }}
.cover .ver {{ font-size: 11pt; color: #666; margin: 6pt 0 26pt; }}
.cover .small {{ font-size: 8.8pt; color: #666; margin-top: 1.4in; line-height: 1.5; }}
.toc {{ columns: 2; column-gap: 28pt; font-size: 10pt; }}
.toc li {{ break-inside: avoid; }}
</style></head>
<body>
{body}
</body></html>'''
open(os.path.join(OUT, "manual.html"), "w").write(page)
print("wrote manual.html")
