#!/usr/bin/env python3
"""Build app assets: icon.ico + UI pngs from raw artwork."""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, 'assets')

def main():
    logo = Image.open(os.path.join(A, 'logo_raw.png')).convert('RGBA')
    print('logo:', logo.size)

    # ---- icon.ico (multi-size) ----
    w, h = logo.size
    side = min(w, h)
    sq = logo.crop(((w - side) // 2, (h - side) // 2,
                    (w - side) // 2 + side, (h - side) // 2 + side))
    # rounded corners look like a real app icon
    mask = Image.new('L', sq.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, sq.width, sq.height],
                                           radius=sq.width // 8, fill=255)
    sq.putalpha(mask)
    ico_path = os.path.join(A, 'icon.ico')
    sq.save(ico_path, sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                             (64, 64), (128, 128), (256, 256)])
    print('icon.ico: %.1f KB' % (os.path.getsize(ico_path) / 1024))

    # ---- sidebar logo (crisp 256px) ----
    ui_logo = sq.resize((256, 256), Image.LANCZOS)
    lp = os.path.join(A, 'logo_ui.png')
    ui_logo.save(lp, optimize=True)
    print('logo_ui.png: %.1f KB' % (os.path.getsize(lp) / 1024))

    # ---- dashboard banner 1784x300 + edge darkening for text overlay ----
    banner = Image.open(os.path.join(A, 'banner_raw.png')).convert('RGB')
    print('banner:', banner.size)
    target_w, target_h = 1784, 300
    scale = max(target_w / banner.width, target_h / banner.height)
    nw, nh = int(banner.width * scale + 0.5), int(banner.height * scale + 0.5)
    banner = banner.resize((nw, nh), Image.LANCZOS)
    x = (nw - target_w) // 2
    y = (nh - target_h) // 2
    banner = banner.crop((x, y, x + target_w, y + target_h))
    # darken left & right edges (score + grade text areas)
    dark = Image.new('L', (target_w, target_h), 0)
    px = dark.load()
    for i in range(target_w):
        f = 0.0
        edge = 620
        if i < edge:
            f = 0.62 * (1 - i / edge)
        elif i > target_w - edge:
            f = 0.62 * ((i - (target_w - edge)) / edge)
        v = int(255 * f)
        for j in range(target_h):
            px[i, j] = v
    black = Image.new('RGB', banner.size, (0, 0, 0))
    banner = Image.composite(black, banner, dark)
    bp = os.path.join(A, 'banner_ui.png')
    banner.save(bp, optimize=True)
    print('banner_ui.png: %.1f KB' % (os.path.getsize(bp) / 1024))

if __name__ == '__main__':
    main()
