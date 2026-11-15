#!/usr/bin/env python3
"""ArenaMP UI8: "ArenaMP" wordmark for the login, registration and text-input
dialogs (files/vfs/textures/menu_arenamw.png, 2048x563, same canvas and
framing as the previous ArenaMW wordmark so the layouts need no change).

Small-caps composition: large "A" and "MP", smaller "RENA", polished gold
with a bevel, a dark rim and a soft warm glow, between two gold rules.
Rendered from Liberation Serif Bold (see FONT).

Usage: python3 tools/arenamp_make_logo.py files/vfs/textures/menu_arenamw.png
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
# Liberation Serif Bold (SIL OFL 1.1). Pelagiad's E/P read poorly at this
# size. The file ships with most Linux distributions; pass --font otherwise.
FONT = Path("/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf")
W, H = 2048, 563


def text_mask(parts, baseline, height_big, height_small, tracking):
    """parts: list of (text, big). Returns (mask, left, right)."""
    big = ImageFont.truetype(str(FONT), height_big)
    small = ImageFont.truetype(str(FONT), height_small)
    widths = []
    for text, is_big in parts:
        font = big if is_big else small
        for ch in text:
            widths.append(font.getlength(ch) + tracking)
    total = sum(widths) - tracking
    x = (W - total) / 2
    left = x
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    i = 0
    for text, is_big in parts:
        font = big if is_big else small
        for ch in text:
            d.text((x, baseline), ch, font=font, fill=255, anchor="ls")
            x += widths[i]
            i += 1
    return mask, left, x - tracking


def gold(mask_img, top, bottom):
    m = np.asarray(mask_img, dtype=np.float64) / 255.0
    ys = np.nonzero(m.max(axis=1) > 0.01)[0]
    y0, y1 = (ys.min(), ys.max()) if len(ys) else (0, H - 1)
    t = np.clip((np.arange(H) - y0) / max(1, (y1 - y0)), 0, 1)[:, None]
    stops = [(0.0, (255, 246, 196)), (0.38, (240, 204, 92)), (0.55, (204, 150, 40)),
             (0.70, (236, 196, 88)), (1.0, (150, 98, 26))]
    rgb = np.zeros((H, 1, 3))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        sel = (t >= t0) & (t <= t1)
        k = np.where(sel, (t - t0) / (t1 - t0), 0)
        for c in range(3):
            rgb[..., c] = np.where(sel, c0[c] + (c1[c] - c0[c]) * k, rgb[..., c])
    rgb = np.broadcast_to(rgb, (H, W, 3)).copy()

    # Bevel: light from the upper left, using the gradient of a blurred mask.
    blur = np.asarray(mask_img.filter(ImageFilter.GaussianBlur(6)), dtype=np.float64) / 255.0
    gy, gx = np.gradient(blur)
    shade = np.clip(-(gx * 0.7 + gy * 1.0) * 9.0, -1, 1)
    rgb = np.where(shade[..., None] > 0, rgb + (255 - rgb) * shade[..., None] * 0.55,
                   rgb * (1 + shade[..., None] * 0.45))
    return np.clip(rgb, 0, 255), m


def compose(out_path):
    letters, left, right = text_mask([("A", True), ("RENA", False), ("MP", True)],
                                     baseline=330, height_big=210, height_small=158, tracking=8)
    rules = Image.new("L", (W, H), 0)
    rd = ImageDraw.Draw(rules)
    pad = 36
    rd.rounded_rectangle([left - pad, 124, right + pad, 140], radius=6, fill=255)
    rd.rounded_rectangle([left - pad + 30, 368, right + pad - 30, 378], radius=5, fill=255)
    # Small diamonds at the rule ends, a quiet ornament.
    for cx, cy, r in ((left - pad - 18, 132, 12), (right + pad + 18, 132, 12),
                      (left - pad + 12, 373, 8), (right + pad - 12, 373, 8)):
        rd.polygon([(cx - r, cy), (cx, cy - r), (cx + r, cy), (cx, cy + r)], fill=255)

    shape = Image.fromarray(np.maximum(np.asarray(letters), np.asarray(rules)))
    # Each element gets its own gradient, so both rules look alike.
    rgb_l, m_l = gold(letters, None, None)
    top_rule = Image.fromarray((np.asarray(rules) * (np.arange(H)[:, None] < 250)).astype(np.uint8))
    bottom_rule = Image.fromarray((np.asarray(rules) * (np.arange(H)[:, None] >= 250)).astype(np.uint8))
    rgb_t, m_t = gold(top_rule, None, None)
    rgb_b, m_b = gold(bottom_rule, None, None)
    rgb = np.where(m_t[..., None] > 0, rgb_t, np.where(m_b[..., None] > 0, rgb_b, rgb_l))
    m = np.maximum(m_l, np.maximum(m_t, m_b))

    rim = np.asarray(shape.filter(ImageFilter.MaxFilter(9)), dtype=np.float64) / 255.0
    glow = np.asarray(shape.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(22)),
                      dtype=np.float64) / 255.0
    shadow = np.roll(np.asarray(shape.filter(ImageFilter.GaussianBlur(10)), dtype=np.float64) / 255.0,
                     (10, 6), axis=(0, 1))

    out = np.zeros((H, W, 4))

    def over(rgb_layer, alpha):
        nonlocal out
        a = alpha[..., None]
        oa = a + out[..., 3:4] * (1 - a)
        col = np.where(oa > 1e-6, (rgb_layer * a + out[..., :3] * out[..., 3:4] * (1 - a)) / np.maximum(oa, 1e-6), 0)
        out = np.concatenate([col, oa], axis=2)

    over(np.full((H, W, 3), (40.0, 22.0, 4.0)), np.clip(glow * 0.55, 0, 1))
    over(np.zeros((H, W, 3)), np.clip(shadow * 0.65, 0, 1))
    over(np.full((H, W, 3), (34.0, 20.0, 6.0)), np.clip(rim, 0, 1) * 0.92)
    over(rgb, m)

    final = np.concatenate([out[..., :3], out[..., 3:4] * 255.0], axis=2)
    img = Image.fromarray(np.clip(final + 0.5, 0, 255).astype(np.uint8), "RGBA")
    img.save(out_path, optimize=True)
    print("wrote", out_path)


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--font" in args:
        i = args.index("--font")
        FONT = Path(args[i + 1])
        del args[i:i + 2]
    compose(args[0] if args else "menu_arenamw.png")
