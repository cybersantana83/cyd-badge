#!/usr/bin/env python3
"""Prepara imagens para o badge (JPEG baseline, tamanho certo para a tela).

O decodificador do ESP32 nao le JPEG progressivo e nao redimensiona, entao a
imagem precisa chegar no SD ja no tamanho final.

Uso:
  python3 prepare_image.py foto.png avatar.jpg --avatar 100     # recorte quadrado central 100x100
  python3 prepare_image.py banner.png banner.jpg --screen        # preenche 320x240
  python3 prepare_image.py logo.png icon.png --icon 48            # icone PNG 48x48 com transparencia

Requer: pip install pillow
"""
import argparse

from PIL import Image, ImageOps


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--avatar", type=int, metavar="PX", help="quadrado PX x PX (padrao do firmware: 100)")
    g.add_argument("--screen", action="store_true", help="tela inteira 320x240")
    g.add_argument("--portrait", action="store_true", help="tela inteira 240x320")
    g.add_argument("--icon", type=int, metavar="PX", help="icone PNG quadrado")
    ap.add_argument("--quality", type=int, default=90)
    a = ap.parse_args()

    img = ImageOps.exif_transpose(Image.open(a.src))

    if a.icon:
        img = img.convert("RGBA")
        img.thumbnail((a.icon, a.icon), Image.LANCZOS)
        canvas = Image.new("RGBA", (a.icon, a.icon), (0, 0, 0, 0))
        canvas.paste(img, ((a.icon - img.width) // 2, (a.icon - img.height) // 2), img)
        canvas.save(a.dst, "PNG", optimize=True)
        print(f"{a.dst}: {a.icon}x{a.icon} PNG")
        return

    size = (a.avatar, a.avatar) if a.avatar else (320, 240) if a.screen else (240, 320)
    img = ImageOps.fit(img.convert("RGB"), size, Image.LANCZOS, centering=(0.5, 0.4))
    img.save(a.dst, "JPEG", quality=a.quality, progressive=False, optimize=True, subsampling=0)
    print(f"{a.dst}: {size[0]}x{size[1]} JPEG baseline")


if __name__ == "__main__":
    main()
