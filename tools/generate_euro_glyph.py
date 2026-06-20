#!/usr/bin/env python3
"""
Generate a € (euro) glyph bitmap for all EPD fonts and append it to each
font .cpp file as the 96th character (index of 0x7F in the table).

Run from the repo root:
    python3 tools/generate_euro_glyph.py
"""

import math
import os
import re
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = os.path.join(os.path.dirname(__file__), "../epd12in48/src")
SYSTEM_FONT = "/System/Library/Fonts/Helvetica.ttc"

# (filename, table_name, width, height)
FONTS = [
    ("font24.cpp",                       "Font24_Table",                    17, 24),
    ("font48.cpp",                       "Font48_Table",                    34, 48),
    ("font64.cpp",                       "Font64_Table",                    46, 64),
    ("font24_google_anton.cpp",          "Font24_GoogleAnton_Table",        16, 24),
    ("font24_google_manrope.cpp",        "Font24_GoogleManrope_Table",      14, 24),
    ("font24_google_permanentmarker.cpp","Font24_GooglePermanentMarker_Table",16,24),
    ("font24_google_spacemono.cpp",      "Font24_GoogleSpaceMono_Table",    14, 24),
    ("font48_google_anton.cpp",          "Font48_GoogleAnton_Table",        30, 48),
    ("font48_google_manrope.cpp",        "Font48_GoogleManrope_Table",      28, 48),
    ("font48_google_permanentmarker.cpp","Font48_GooglePermanentMarker_Table",30,48),
    ("font48_google_spacemono.cpp",      "Font48_GoogleSpaceMono_Table",    28, 48),
    ("font64_google_anton.cpp",          "Font64_GoogleAnton_Table",        40, 64),
    ("font64_google_manrope.cpp",        "Font64_GoogleManrope_Table",      38, 64),
    ("font64_google_permanentmarker.cpp","Font64_GooglePermanentMarker_Table",40,64),
    ("font64_google_spacemono.cpp",      "Font64_GoogleSpaceMono_Table",    38, 64),
]


def render_euro(cell_w, cell_h):
    """Render € at cell_w x cell_h using Helvetica, return 1-bit pixel list."""
    scale = 4
    img_w, img_h = cell_w * scale, cell_h * scale

    # Find a font size where € fills the cell nicely
    for pt in range(img_h, 4, -1):
        try:
            fnt = ImageFont.truetype(SYSTEM_FONT, pt)
        except Exception:
            break
        bbox = fnt.getbbox("€")
        if bbox is None:
            continue
        gw = bbox[2] - bbox[0]
        gh = bbox[3] - bbox[1]
        if gw <= img_w * 0.92 and gh <= img_h * 0.90:
            break

    img = Image.new("L", (img_w, img_h), 0)
    draw = ImageDraw.Draw(img)

    bbox = fnt.getbbox("€")
    gw = bbox[2] - bbox[0]
    gh = bbox[3] - bbox[1]
    ox = (img_w - gw) // 2 - bbox[0]
    oy = (img_h - gh) // 2 - bbox[1]
    draw.text((ox, oy), "€", font=fnt, fill=255)

    # Downscale with threshold
    small = img.resize((cell_w, cell_h), Image.LANCZOS)
    threshold = 80
    pixels = [[1 if small.getpixel((x, y)) >= threshold else 0
               for x in range(cell_w)]
              for y in range(cell_h)]
    return pixels


def pixels_to_bytes(pixels, width, height):
    bpr = (width + 7) // 8
    result = []
    for row in pixels:
        padded = row + [0] * (bpr * 8 - width)
        for i in range(bpr):
            b = 0
            for bit in range(8):
                if padded[i * 8 + bit]:
                    b |= 0x80 >> bit
            result.append(b)
    return result


def format_bytes(data, bpr):
    lines = []
    for row_start in range(0, len(data), bpr):
        row = data[row_start:row_start + bpr]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in row) + ",")
    return "\n".join(lines)


def patch_font_file(filepath, table_name, width, height):
    pixels = render_euro(width, height)
    raw = pixels_to_bytes(pixels, width, height)
    bpr = (width + 7) // 8

    glyph_block = (
        "\n  // '\\x7f' (mapped from UTF-8 \xe2\x82\xac = € euro sign)\n"
        + format_bytes(raw, bpr)
        + "\n"
    )

    with open(filepath, "r", encoding="utf-8") as f:
        src = f.read()

    # Idempotent: skip if already patched
    if "euro sign" in src:
        print(f"  SKIP (already patched): {os.path.basename(filepath)}")
        return

    # Find the array's closing };  by locating the last hex byte then finding
    # the first }; after it. This avoids matching the struct initializer's };.
    last_hex = src.rfind("0x")
    if last_hex == -1:
        print(f"  ERROR: no hex data found in {os.path.basename(filepath)}")
        return

    idx = src.find("\n};", last_hex)
    if idx == -1:
        print(f"  ERROR: closing }}; not found after hex data in {os.path.basename(filepath)}")
        return

    patched = src[:idx] + glyph_block + src[idx:]
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(patched)

    total = len(raw)
    print(f"  OK ({width}x{height}, {bpr} bpr, {total} bytes): {os.path.basename(filepath)}")


def main():
    print("Generating € glyphs for all EPD fonts...\n")
    for fname, table_name, w, h in FONTS:
        path = os.path.join(FONT_DIR, fname)
        if not os.path.exists(path):
            print(f"  MISSING: {fname}")
            continue
        patch_font_file(path, table_name, w, h)
    print("\nDone.")


if __name__ == "__main__":
    main()
