#!/usr/bin/env python3
"""Builds the anime theme's fonts for the Tab5 as VLW files (Processing's
smooth-font format, 8-bit alpha per pixel, which M5GFX draws anti-aliased).

Only the characters the firmware draws are kept: printable ASCII for the
Latin faces, and for the Japanese ones every non-ASCII character found in
the theme's sources (so a new caption in the code needs a re-run). The big
mincho sizes, used inside the icons, only carry the kanji the icons show.

The faces are Noto (SIL Open Font License 1.1, no reserved font names),
from https://github.com/google/fonts/tree/main/ofl:
    notosans/NotoSans[wdth,wght].ttf          -> NotoSans.ttf
    notosansmono/NotoSansMono[wdth,wght].ttf  -> NotoSansMono.ttf
    notoserifjp/NotoSerifJP[wght].ttf         -> NotoSerifJP.ttf
    notosansjp/NotoSansJP[wght].ttf           -> NotoSansJP.ttf

Usage (needs Pillow):
    python tools/make-fonts.py <folder with the four .ttf files> [--preview out.png]

Writes components/monitor-core/fonts/*.vlw; the component embeds them
(a font added here also goes in the component's CMakeLists.txt and in the
table in src/anime.cpp).
"""

import argparse
import pathlib
import re
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "components" / "monitor-core" / "fonts"

# Sources whose non-ASCII characters make the Japanese sets
SOURCES = [
    ROOT / "components" / "monitor-core" / "src" / "anime.cpp",
    ROOT / "components" / "monitor-core" / "src" / "anime_icons.cpp",
    ROOT / "components" / "monitor-core" / "src" / "anime_views.cpp",
    ROOT / "firmware" / "tab5" / "main" / "main.cpp",
]

# The kanji drawn inside the icons, at the big sizes only
ICON_KANJI = "処理並列圧縮警告質問異常待機完了"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# name, file, variation axes (Weight, Width), pixel size, horizontal scale,
# character set ("ascii", "jp" or "icon")
FONTS = [
    ("cond18",    "NotoSans.ttf",     (800, 62), 18,  1.0,  "ascii"),
    ("cond22",    "NotoSans.ttf",     (800, 62), 22,  1.0,  "ascii"),
    ("cond26",    "NotoSans.ttf",     (800, 62), 26,  1.0,  "ascii"),
    ("cond32",    "NotoSans.ttf",     (800, 62), 32,  1.0,  "ascii"),
    ("cond40",    "NotoSans.ttf",     (800, 62), 40,  1.0,  "ascii"),
    ("cond56",    "NotoSans.ttf",     (800, 62), 56,  1.0,  "ascii"),
    ("mono14",    "NotoSansMono.ttf", (500, 75), 14,  1.0,  "ascii"),
    ("mono16",    "NotoSansMono.ttf", (500, 75), 16,  1.0,  "ascii"),
    ("mono20",    "NotoSansMono.ttf", (500, 75), 20,  1.0,  "ascii"),
    ("gothic22",  "NotoSansJP.ttf",   (700,),    22,  1.0,  "jp"),
    ("gothic32",  "NotoSansJP.ttf",   (700,),    32,  1.0,  "jp"),
    ("mincho44",  "NotoSerifJP.ttf",  (900,),    44,  0.82, "jp"),
    ("mincho96",  "NotoSerifJP.ttf",  (900,),    96,  0.82, "icon"),
    ("mincho192", "NotoSerifJP.ttf",  (900,),    192, 0.82, "icon"),
]


def japanese_chars():
    chars = set()
    for path in SOURCES:
        if path.exists():
            chars.update(c for c in path.read_text(encoding="utf-8") if ord(c) >= 0x3000)
    return "".join(sorted(chars))


class Glyph:
    def __init__(self, code, w, h, adv, top, left, bitmap):
        self.code, self.w, self.h = code, w, h
        self.adv, self.top, self.left = adv, top, left
        self.bitmap = bitmap


def render(font, ch, xscale):
    adv = font.getlength(ch)
    size = font.size
    canvas = Image.new("L", (int(size * 3), int(size * 3)), 0)
    ox, oy = size, int(size * 2)
    ImageDraw.Draw(canvas).text((ox, oy), ch, font=font, fill=255, anchor="ls")
    box = canvas.getbbox()
    if box is None:   # a space
        return Glyph(ord(ch), 0, 0, round(adv * xscale), 0, 0, b"")
    img = canvas.crop(box)
    left, top = box[0] - ox, oy - box[1]
    if xscale != 1.0:
        w = max(1, round(img.width * xscale))
        img = img.resize((w, img.height), Image.LANCZOS)
        left = round(left * xscale)
    g = Glyph(ord(ch), img.width, img.height, round(adv * xscale), top, left, img.tobytes())
    if g.w > 255 or g.adv > 255 or not -128 <= g.left <= 127:
        sys.exit(f"{ch!r} at {size} px does not fit VLW's 8-bit metrics")
    return g


def build(fonts_dir, name, file, axes, size, xscale, charset, jp):
    font = ImageFont.truetype(str(fonts_dir / file), size)
    font.set_variation_by_axes(list(axes))
    chars = {"ascii": ASCII, "jp": " " + jp, "icon": " " + ICON_KANJI}[charset]
    missing = [c for c in chars if c not in " " and font.getmask(c).getbbox() is None]
    if missing:
        sys.exit(f"{name}: {file} has no glyph for {''.join(missing)}")
    glyphs = sorted((render(font, c, xscale) for c in set(chars)), key=lambda g: g.code)
    ascent, descent = font.getmetrics()
    data = struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for g in glyphs:
        data += struct.pack(">7i", g.code, g.h, g.w, g.adv, g.top, g.left, 0)
    for g in glyphs:
        data += g.bitmap
    return data, glyphs


def preview(path, sets):
    """A sheet with every font's characters, drawn from the glyph data"""
    rows = []
    for name, glyphs in sets:
        line_h = max((g.top for g in glyphs), default=0) + max((g.h - g.top for g in glyphs), default=0) + 8
        width = 24 + sum(g.adv for g in glyphs if g.code != 32) + 8 * len(glyphs)
        rows.append((name, glyphs, line_h, min(width, 2400)))
    sheet = Image.new("L", (max(r[3] for r in rows) + 160, sum(r[2] + 10 for r in rows)), 0)
    draw = ImageDraw.Draw(sheet)
    y = 0
    for name, glyphs, line_h, _ in rows:
        draw.text((4, y + 4), name, fill=120)
        base = y + max(g.top for g in glyphs) + 4
        x = 150
        for g in glyphs:
            if g.w and x + g.w < sheet.width:
                sheet.paste(Image.frombytes("L", (g.w, g.h), g.bitmap), (x + g.left, base - g.top))
            x += g.adv + 8
        y += line_h + 10
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("fonts", type=pathlib.Path, help="folder with the four Noto .ttf files")
    ap.add_argument("--preview", type=pathlib.Path, help="also write a PNG sheet of every font")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # the Japanese list, on any console

    jp = japanese_chars()
    OUT.mkdir(parents=True, exist_ok=True)
    sets = []
    for spec in FONTS:
        data, glyphs = build(args.fonts, *spec, jp)
        (OUT / f"{spec[0]}.vlw").write_bytes(data)
        sets.append((spec[0], glyphs))
        print(f"{spec[0]:10} {len(glyphs):4} glyphs {len(data):8,} bytes")
    print(f"Japanese characters: {jp}")
    if args.preview:
        preview(args.preview, sets)


if __name__ == "__main__":
    main()
