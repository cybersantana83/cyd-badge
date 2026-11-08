#!/usr/bin/env python3
"""Gera fontes .vlw (formato "smooth font" do TFT_eSPI/LovyanGFX) a partir de um .ttf/.otf.

As fontes internas do firmware so tem ASCII. Com uma .vlw no SD o badge mostra
acentos (ç, ã, é...) e simbolos como o bullet (•).

Uso:
  python3 make_vlw.py Poppins-Bold.ttf 26 big.vlw
  python3 make_vlw.py Poppins-Regular.ttf 14 small.vlw --extra "→★"

Requer: pip install pillow
"""
import argparse
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_CHARS = (
    [chr(c) for c in range(0x20, 0x7F)]
    + [chr(c) for c in range(0xA0, 0x100)]
    + list("•–—…“”‘’€")
)


def build(ttf, size, out, extra=""):
    font = ImageFont.truetype(ttf, size)
    chars = sorted(set(DEFAULT_CHARS + list(extra)), key=ord)
    glyphs = []
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        if ch == " ":
            glyphs.append((ord(ch), 0, 0, adv, 0, 0, b""))
            continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            continue  # glifo ausente na fonte
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, anchor="ls", fill=255)
        glyphs.append((ord(ch), h, w, adv, -y0, x0, img.tobytes()))

    asc = -font.getbbox("d", anchor="ls")[1]
    desc = font.getbbox("p", anchor="ls")[3]

    with open(out, "wb") as f:
        f.write(struct.pack(">6i", len(glyphs), 11, size, 0, asc, desc))
        for code, h, w, adv, dy, dx, _ in glyphs:
            f.write(struct.pack(">7i", code, h, w, adv, dy, dx, 0))
        for g in glyphs:
            f.write(g[6])
    print(f"{out}: {len(glyphs)} glifos, {size}px")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ttf")
    ap.add_argument("size", type=int, help="altura em pixels")
    ap.add_argument("out")
    ap.add_argument("--extra", default="", help="caracteres extras para incluir")
    a = ap.parse_args()
    try:
        build(a.ttf, a.size, a.out, a.extra)
    except OSError as e:
        sys.exit(f"Erro abrindo fonte: {e}")
