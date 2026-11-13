#!/usr/bin/env python3
"""Pre-visualiza o badge no computador, sem precisar gravar no SD.

Le o mesmo config.json e as mesmas fontes .vlw do cartao e gera um PNG por
pagina, mais uma folha com todas. O layout replica o do firmware (aproximado).

Uso:
  python3 preview.py ../sd_card              # raiz do cartao SD (contendo /badge)
  python3 preview.py ../sd_card --scale 2 --out previews

Requer: pip install pillow qrcode
"""
import argparse
import json
import math
import os
import struct
import unicodedata

import qrcode
from PIL import Image, ImageDraw, ImageFont


def hex2rgb(h, default):
    if not h:
        return default
    h = h.lstrip("#")
    if len(h) != 6:
        return default
    return tuple(int(h[i : i + 2], 16) for i in (0, 2, 4))


class VLW:
    def __init__(self, path):
        data = open(path, "rb").read()
        count, _, size, _, asc, desc = struct.unpack(">6i", data[:24])
        self.glyphs = {}
        ptr = 24 + count * 28
        max_asc, max_desc = asc, desc
        for i in range(count):
            code, h, w, adv, dy, dx, _ = struct.unpack(">7i", data[24 + i * 28 : 52 + i * 28])
            bmp = data[ptr : ptr + w * h]
            ptr += w * h
            self.glyphs[code] = (h, w, adv, dy, dx, bmp)
            if code > 0xFF or (0x20 < code < 0xA0 and code != 0x7F):
                max_asc = max(max_asc, dy)
                max_desc = max(max_desc, h - dy)
        self.ascent = max_asc
        self.height = max_asc + max_desc
        self.space = self.height * 2 // 7

    def width(self, text):
        return sum(self.glyphs.get(ord(c), (0, 0, self.space))[2] for c in text)

    def draw(self, img, text, x, y, color):
        base = y + self.ascent
        for c in text:
            g = self.glyphs.get(ord(c))
            if not g:
                x += self.space
                continue
            h, w, adv, dy, dx, bmp = g
            if w and h:
                mask = Image.frombytes("L", (w, h), bmp)
                img.paste(Image.new("RGB", (w, h), color), (x + dx, base - dy), mask)
            x += adv


class FallbackFont:
    """Aproxima as fontes internas (sem acentos) quando nao ha .vlw."""

    def __init__(self, px, bold):
        name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
        try:
            self.f = ImageFont.truetype(f"/usr/share/fonts/truetype/dejavu/{name}", px)
        except OSError:
            self.f = ImageFont.load_default()
        a, d = self.f.getmetrics()
        self.ascent, self.height = a, a + d

    def width(self, text):
        return int(self.f.getlength(fold(text)))

    def draw(self, img, text, x, y, color):
        ImageDraw.Draw(img).text((x, y + self.ascent), fold(text), font=self.f, fill=color, anchor="ls")


def fold(s):
    s = s.replace("•", "|").replace("…", "...")
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if 32 <= ord(c) < 127)


class Badge:
    def __init__(self, root, cfg):
        self.root = root
        s = cfg.get("settings", {})
        self.s = s
        rot = s.get("rotation", 1) & 3
        self.W, self.H = (320, 240) if rot in (1, 3) else (240, 320)
        self.theme = dict(
            bg=hex2rgb(s.get("bg"), (13, 17, 23)),
            fg=hex2rgb(s.get("fg"), (230, 237, 243)),
            muted=hex2rgb(s.get("muted"), (139, 148, 158)),
            accent=hex2rgb(s.get("accent"), (245, 166, 35)),
        )
        self.fonts = {}
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")
        for role, key, px, bold, emb in (("big", "font_big", 24, True, "big"), ("med", "font_medium", 17, True, "medium"), ("small", "font_small", 13, False, "small")):
            p = self.path(s.get(key, ""))
            if not (p and os.path.exists(p)):
                p = os.path.join(here, emb + ".vlw")  # mesmas fontes embutidas no firmware
            self.fonts[role] = VLW(p) if os.path.exists(p) else FallbackFont(px, bold)

    def path(self, p):
        return os.path.join(self.root, p.lstrip("/")) if p else ""

    def wrapped(self, img, text, x, y, maxw, role, color, max_lines=3, center=False, draw=True):
        if not text:
            return y
        f = self.fonts[role]
        lh = f.height + 2
        lines, line = [], ""
        for para in text.split("\n"):
            words = []
            for word in para.split(" "):
                if not word:
                    continue
                if f.width(word) > maxw:
                    piece = ""
                    for ch in word:
                        if piece and f.width(piece + ch) > maxw:
                            cut = -1
                            for k in range(len(piece) - 1, len(piece) // 3, -1):
                                if piece[k] in "/._-":
                                    cut = k + 1
                                    break
                            if 0 < cut < len(piece):
                                words.append(piece[:cut]); piece = piece[cut:]
                            else:
                                words.append(piece); piece = ""
                        piece += ch
                    words.append(piece)
                else:
                    words.append(word)
            for word in words:
                test = f"{line} {word}" if line else word
                if f.width(test) > maxw and line:
                    lines.append(line)
                    line = word
                else:
                    line = test
            lines.append(line)
            line = ""
        lines = [l for l in lines if l]
        if len(lines) > max_lines:
            lines = lines[:max_lines]
            lines[-1] += "..."
        for l in lines:
            lx = x - f.width(l) // 2 if center else x
            if draw:
                f.draw(img, l, lx, y, color)
            y += lh
        return y

    def measure(self, text, maxw, role, n):
        return self.wrapped(None, text, 0, 0, maxw, role, None, n, draw=False)

    def fit(self, text, maxw):
        if self.fonts["med"].width(text) <= maxw: return "med"
        if self.fonts["small"].width(text) <= maxw: return "small"
        return "med"

    def image(self, img, p, x, y, maxw=None, maxh=None, scale=1.0):
        fp = self.path(p)
        if not p or not os.path.exists(fp):
            return False
        im = Image.open(fp).convert("RGBA")
        if scale != 1.0:
            im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
        if maxw and maxh:
            im = im.crop((0, 0, min(im.width, maxw), min(im.height, maxh)))
        img.paste(im, (x, y), im)
        return True

    def colors(self, p):
        t = self.theme
        return (hex2rgb(p.get("bg"), t["bg"]), hex2rgb(p.get("fg"), t["fg"]),
                hex2rgb(p.get("muted"), t["muted"]), hex2rgb(p.get("accent"), t["accent"]))

    def avatar(self, img, p, x, y, size, bg, accent):
        d = ImageDraw.Draw(img)
        r = size // 2
        cx, cy = x + r, y + r
        src = p.get("image_px", p.get("image_size", 100))
        if not self.image(img, p.get("image", ""), x, y, size, size, size / src):
            d.ellipse((x, y, x + size, y + size), fill=accent)
            name = fold(p.get("name", ""))
            ini = "".join(w[0].upper() for w in name.split()[:2]) or "?"
            f = self.fonts["big"]
            f.draw(img, ini, cx - f.width(ini) // 2, cy - f.height // 2, bg)
            return
        if p.get("image_shape", "circle") != "square":
            for j in range(size):
                dy = j + 0.5 - r
                h = math.sqrt(max(0, r * r - dy * dy))
                left = int(r - h)
                if left > 0:
                    d.line((x, y + j, x + left - 1, y + j), fill=bg)
                    d.line((x + size - left, y + j, x + size - 1, y + j), fill=bg)
            for k in range(-1, 3):
                d.ellipse((cx - r - k, cy - r - k, cx + r + k, cy + r + k), outline=accent)
        else:
            for k in range(3):
                d.rectangle((x - 1 - k, y - 1 - k, x + size + k, y + size + k), outline=accent)

    def header(self, img, label, accent, color):
        ImageDraw.Draw(img).rectangle((0, 0, self.W, 33), fill=accent)
        f = self.fonts["med"]
        f.draw(img, label, 12, 17 - f.height // 2, color)

    def qrbox(self, img, data, x, y, size):
        ImageDraw.Draw(img).rounded_rectangle((x, y, x + size - 1, y + size - 1), 6, fill="white")
        pad = 8
        w = size - 2 * pad
        q = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_L, border=0)
        q.add_data(data)
        q.make(fit=True)
        m = q.get_matrix()
        n = len(m)
        t = w // n
        off = (w - n * t) // 2
        d = ImageDraw.Draw(img)
        for j, row in enumerate(m):
            for i, v in enumerate(row):
                if v:
                    px, py = x + pad + off + i * t, y + pad + off + j * t
                    d.rectangle((px, py, px + t - 1, py + t - 1), fill="black")

    def render(self, p, idx, total):
        bg, fg, muted, accent = self.colors(p)
        img = Image.new("RGB", (self.W, self.H), bg)
        W, H = self.W, self.H
        t = p.get("type", "profile")
        if t == "profile":
            name, title, sub = p.get("name", ""), p.get("title", ""), p.get("subtitle", "")
            if p.get("layout") == "side" and W > H:
                size, ax = 110, 14
                tx = ax + size + 16; tw = W - tx - 8
                th = self.measure(name, tw, "big", 2) + 4 + 13 + self.measure(title, tw, "med", 2) + 4 + self.measure(sub, tw, "small", 3)
                avail = H - 18
                self.avatar(img, p, ax, (avail - size) // 2, size, bg, accent)
                y = max(6, (avail - th) // 2)
                y = self.wrapped(img, name, tx, y, tw, "big", fg, 2) + 4
                ImageDraw.Draw(img).rectangle((tx, y, tx + 39, y + 2), fill=accent)
                y = self.wrapped(img, title, tx, y + 13, tw, "med", accent, 2) + 4
                self.wrapped(img, sub, tx, y, tw, "small", muted, 3)
            else:
                portrait = H > W
                mw = W - 20
                sl = 3 if portrait else 2
                hn, ht, hs = self.measure(name, mw, "big", 2), self.measure(title, mw, "med", 2), self.measure(sub, mw, "small", sl)
                gap = 14 if portrait else 8
                th = hn + (2 + ht if ht else 0) + (4 + hs if hs else 0)
                avail = H - 18 - 8
                size = 140 if portrait else 96
                if size + gap + th > avail:
                    size = max(64, avail - gap - th)
                y = 6 + max(0, (avail - size - gap - th) // 2)
                self.avatar(img, p, (W - size) // 2, y, size, bg, accent)
                y += size + gap
                y = self.wrapped(img, name, W // 2, y, mw, "big", fg, 2, True)
                if ht: y = self.wrapped(img, title, W // 2, y + 2, mw, "med", accent, 2, True)
                if hs: self.wrapped(img, sub, W // 2, y + 4, mw, "small", muted, sl, True)
        elif t in ("qr", "vcard"):
            if t == "vcard":
                esc = lambda v: "".join("\\" + c if c in ";:,\\" else c for c in v)
                data = "MECARD:" + "".join(f"{k}:{esc(p[f])};" for k, f in (("N", "name"), ("ORG", "org"), ("TEL", "phone"), ("EMAIL", "email"), ("URL", "url"), ("NOTE", "note_card")) if p.get(f)) + ";"
                label = p.get("label", "Contato")
            else:
                data = p.get("url") or p.get("data", "")
                label = p.get("label", "")
            self.header(img, label, accent, hex2rgb(p.get("header_fg"), (255, 255, 255)))
            handle, note = p.get("handle", ""), p.get("note", "")
            if W > H:
                qs = H - 34 - 10 - 18
                qx, qy = 10, 40
                self.qrbox(img, data, qx, qy, qs)
                tx = qx + qs + 12; tw = W - tx - 6
                hf = self.fit(handle, tw)
                th = self.measure(handle, tw, hf, 3) + 6 + self.measure(note, tw, "small", 5)
                y = qy + max(0, (qs - th) // 2)
                y = self.wrapped(img, handle, tx, y, tw, hf, fg, 3) + 6
                self.wrapped(img, note, tx, y, tw, "small", muted, 5)
            else:
                tw = W - 16
                hf = self.fit(handle, tw)
                hh, hn = self.measure(handle, tw, hf, 2), self.measure(note, tw, "small", 3)
                avail = H - 34 - 18
                qs = min(W - 12, avail - hh - hn - 18) if p.get("layout") == "big" else min(W - 32, avail - hh - hn - 26)
                tot = qs + 10 + hh + (4 + hn if hn else 0)
                y = 34 + max(6, (avail - tot) // 2)
                self.qrbox(img, data, (W - qs) // 2, y, qs)
                y = self.wrapped(img, handle, W // 2, y + qs + 10, tw, hf, fg, 2, True)
                self.wrapped(img, note, W // 2, y + 4, tw, "small", muted, 3, True)
        elif t == "image":
            self.image(img, p.get("image", ""), 0, 0, W, H)
            if p.get("caption"):
                ImageDraw.Draw(img).rectangle((0, H - 42, W, H - 17), fill=bg)
                self.wrapped(img, p["caption"], W // 2, H - 38, W - 16, "small", fg, 1, True)
        elif t == "text":
            self.header(img, p.get("label", ""), accent, hex2rgb(p.get("header_fg"), (255, 255, 255)))
            y = self.wrapped(img, p.get("title", ""), 12, 48, W - 24, "med", fg, 2) + 6
            self.wrapped(img, p.get("body", ""), 12, y, W - 24, "small", muted, 8)

        if self.s.get("show_dots", True) and total > 1 and not p.get("hide_dots"):
            d = ImageDraw.Draw(img)
            x0 = W // 2 - (total - 1) * 6
            for i in range(total):
                r = 3 if i == idx else 2
                cx, cy = x0 + i * 12, H - 9
                d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=accent if i == idx else muted)
        return img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sd_root", help="pasta que representa a raiz do cartao SD")
    ap.add_argument("--out", default="preview")
    ap.add_argument("--scale", type=int, default=2)
    a = ap.parse_args()

    cfg = json.load(open(os.path.join(a.sd_root, "badge", "config.json"), encoding="utf-8"))
    badge = Badge(a.sd_root, cfg)
    pages = cfg.get("pages", [])
    os.makedirs(a.out, exist_ok=True)
    frames = []
    for i, p in enumerate(pages):
        im = badge.render(p, i, len(pages))
        im = im.resize((im.width * a.scale, im.height * a.scale), Image.NEAREST)
        fn = os.path.join(a.out, f"page_{i + 1:02d}_{p.get('type', 'page')}.png")
        im.save(fn)
        frames.append(im)
        print(fn)

    cols = 2 if len(frames) > 1 else 1
    rows = (len(frames) + cols - 1) // cols
    fw, fh = frames[0].size
    gap = 16
    sheet = Image.new("RGB", (cols * fw + (cols + 1) * gap, rows * fh + (rows + 1) * gap), (40, 40, 40))
    for i, f in enumerate(frames):
        sheet.paste(f, (gap + (i % cols) * (fw + gap), gap + (i // cols) * (fh + gap)))
    sheet.save(os.path.join(a.out, "all_pages.png"))
    print(os.path.join(a.out, "all_pages.png"))


if __name__ == "__main__":
    main()
