#!/usr/bin/env python3
"""ArenaMP UI8: textures for the modern (TESO-like) interface skin.

Dark glass panels, thin bronze rules, a dark button face with hover/pressed/
selected states, bronze scrollbar thumbs and arrows, glossy progress tracks,
and the frameless icon-button glow used by the map window.

The textures keep the geometry of the classic Morrowind pieces they replace
(4 px window borders, 2 px thin borders, 16x8 bar track, 512 px wide thin rule
for MW_HLine), so every existing layout keeps its measurements.

Consumed by files/mygui/arenamp_modern.skin.xml (only when
[GUI] arenamp modern skin = true) and files/mygui/arenamp_ui.skin.xml.

Usage: python3 tools/arenamp_make_ui_skin.py files/vfs/textures/ui/arenamw/skin
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

DARK = (8, 7, 6)
BRONZE_HI = (222, 194, 132)
BRONZE = (168, 138, 88)
BRONZE_DK = (96, 76, 46)
GOLD = (246, 220, 156)


def img(arr):
    """arr: float (H, W, 4) with rgb 0..255, alpha 0..1."""
    out = np.concatenate([arr[..., :3], arr[..., 3:4] * 255.0], axis=2)
    return Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8), "RGBA")


def rows_texture(width, rows):
    """Horizontal strip: one (rgb, alpha) per row, tiled across width."""
    arr = np.zeros((len(rows), width, 4))
    for y, (rgb, a) in enumerate(rows):
        arr[y, :, :3] = rgb
        arr[y, :, 3] = a
    return arr


def transpose(arr):
    return np.transpose(arr, (1, 0, 2)).copy()


# ---------------------------------------------------------------- borders

THICK_OUTER_FIRST = [(DARK, 0.85), (BRONZE_HI, 0.95), (BRONZE_DK, 0.90), (DARK, 0.35)]
THIN_OUTER_FIRST = [(BRONZE, 0.85), (DARK, 0.55)]


def border_set(prefix, outer_first, length=512):
    n = len(outer_first)
    top = rows_texture(length, outer_first)
    bottom = rows_texture(length, list(reversed(outer_first)))
    left = transpose(rows_texture(length, outer_first))
    right = transpose(rows_texture(length, list(reversed(outer_first))))
    out = {prefix + "_top": top, prefix + "_bottom": bottom,
           prefix + "_left": left, prefix + "_right": right}
    for name, fx, fy in (("tl", False, False), ("tr", True, False), ("bl", False, True), ("br", True, True)):
        c = np.zeros((n, n, 4))
        for y in range(n):
            for x in range(n):
                dx = n - 1 - x if fx else x
                dy = n - 1 - y if fy else y
                rgb, a = outer_first[min(dx, dy)]
                c[y, x, :3] = rgb
                c[y, x, 3] = a
        if n >= 4:
            # A bright gold stud where the bronze rules meet.
            sx = n - 2 if fx else 1
            sy = n - 2 if fy else 1
            c[sy, sx, :3] = GOLD
            c[sy, sx, 3] = 1.0
        out[prefix + "_" + name] = c
    return out


def caption_line():
    """16x16 tile: a bronze rule through the middle (the caption's side blocks)."""
    arr = np.zeros((16, 16, 4))
    for y, (rgb, a) in {8: (BRONZE, 0.25), 9: (BRONZE_HI, 0.90), 10: (DARK, 0.45)}.items():
        arr[y, :, :3] = rgb
        arr[y, :, 3] = a
    side = arr[:, :2].copy()
    return arr, side


def clear(w, h):
    return np.zeros((h, w, 4))


# ---------------------------------------------------------------- buttons

BUTTON_STATES = [
    # name, top colour, bottom colour, alpha top/bottom, accent row (y, rgb, a)
    ("normal", (36, 31, 24), (14, 12, 10), (0.86, 0.92), None),
    ("highlighted", (78, 61, 36), (28, 22, 14), (0.92, 0.94), (21, (214, 172, 98), 0.55)),
    ("pushed", (12, 10, 8), (44, 35, 22), (0.95, 0.95), None),
    ("checked", (96, 73, 38), (40, 30, 16), (0.92, 0.94), (0, (236, 206, 138), 0.55)),
    ("checked_hi", (122, 94, 50), (52, 40, 22), (0.94, 0.95), (0, (250, 226, 160), 0.70)),
    ("disabled", (24, 22, 20), (18, 16, 14), (0.60, 0.60), None),
]
BUTTON_STRIDE = 32  # rows per state region in the atlas; each region is 24 rows high


def button_face():
    arr = np.zeros((BUTTON_STRIDE * len(BUTTON_STATES), 16, 4))
    for i, (_, top, bottom, alpha, accent) in enumerate(BUTTON_STATES):
        y0 = i * BUTTON_STRIDE
        for y in range(24):
            t = y / 23
            arr[y0 + y, :, :3] = [top[c] + (bottom[c] - top[c]) * t for c in range(3)]
            arr[y0 + y, :, 3] = alpha[0] + (alpha[1] - alpha[0]) * t
        if accent:
            ay, rgb, a = accent
            arr[y0 + ay, :, :3] = rgb
            arr[y0 + ay, :, 3] = a
        # Replicate the edge rows into the 8-row gap so filtering never bleeds.
        arr[y0 + 24:y0 + BUTTON_STRIDE] = arr[y0 + 23]
    return arr


BUTTON_FRAME = [(DARK, 0.65), (BRONZE, 0.92), (BRONZE_DK, 0.35), (DARK, 0.0)]


def button_frame_set():
    out = border_set("btn", BUTTON_FRAME, length=32)
    # Soften the outermost corner pixel so buttons read slightly rounded.
    for name in ("btn_tl", "btn_tr", "btn_bl", "btn_br"):
        c = out[name]
        n = c.shape[0]
        cx = n - 1 if name.endswith("r") else 0
        cy = n - 1 if name[4] == "b" else 0
        c[cy, cx, 3] = 0.0
        sx = n - 2 if name.endswith("r") else 1
        sy = n - 2 if name[4] == "b" else 1
        c[sy, sx, :3] = BRONZE
        c[sy, sx, 3] = 0.92
    return out


# ---------------------------------------------------------------- bars, scroll

def bar_gray():
    shades = [255, 238, 222, 206, 188, 170, 148, 112]
    arr = np.zeros((8, 16, 4))
    for y, v in enumerate(shades):
        arr[y, :, :3] = v
        arr[y, :, 3] = 1.0
    return arr


def thumb_v():
    cols = [(DARK, 0.85), (BRONZE_DK, 1.0), (BRONZE, 1.0), (BRONZE_HI, 1.0), (GOLD, 1.0),
            (BRONZE_HI, 1.0), (BRONZE, 1.0), (BRONZE_DK, 1.0), (DARK, 0.85)]
    arr = np.zeros((16, len(cols), 4))
    for x, (rgb, a) in enumerate(cols):
        arr[:, x, :3] = rgb
        arr[:, x, 3] = a
    return arr


def arrow(direction):
    S, K = 10, 8
    m = Image.new("L", (S * K, S * K), 0)
    d = ImageDraw.Draw(m)
    pts = {"up": [(5, 2.2), (8.6, 7.4), (1.4, 7.4)], "down": [(5, 7.8), (8.6, 2.6), (1.4, 2.6)],
           "left": [(2.2, 5), (7.4, 1.4), (7.4, 8.6)], "right": [(7.8, 5), (2.6, 1.4), (2.6, 8.6)]}[direction]
    d.polygon([(x * K, y * K) for x, y in pts], fill=255)
    outline = m.filter(ImageFilter.MaxFilter(K + 1))
    mask = np.asarray(m.resize((S, S), Image.BOX), dtype=np.float64) / 255
    ring = np.asarray(outline.resize((S, S), Image.BOX), dtype=np.float64) / 255
    arr = np.zeros((S, S, 4))
    arr[..., :3] = DARK
    arr[..., 3] = ring * 0.85
    grad = np.linspace(0, 1, S)[:, None]
    for c in range(3):
        col = GOLD[c] * (1 - grad) + BRONZE[c] * grad
        arr[..., c] = np.where(mask > 0, col * mask + arr[..., c] * (1 - mask), arr[..., c])
    arr[..., 3] = np.maximum(arr[..., 3], mask)
    return arr


def plate(alpha):
    arr = np.zeros((8, 8, 4))
    arr[..., :3] = DARK
    arr[..., 3] = alpha
    return arr


ICON_STATES = ["normal", "highlighted", "pushed", "checked", "disabled"]


def icon_button_glow():
    """32x(5*40) atlas: transparent normal, soft gold glow on hover/press/selected."""
    S, stride = 32, 40
    arr = np.zeros((stride * len(ICON_STATES), S, 4))
    yy, xx = np.mgrid[0:S, 0:S]
    r = np.hypot((xx + 0.5 - S / 2) / (S / 2), (yy + 0.5 - S / 2) / (S / 2))
    soft = np.clip(1 - r, 0, 1) ** 1.6
    ring = np.clip(1 - np.abs(r - 0.86) / 0.07, 0, 1)
    for i, name in enumerate(ICON_STATES):
        y0 = i * stride
        a = np.zeros((S, S))
        rgb = np.zeros((S, S, 3))
        rgb[...] = BRONZE_HI
        if name == "highlighted":
            a = soft * 0.42
        elif name == "pushed":
            a = soft * 0.62
            rgb[...] = BRONZE
        elif name == "checked":
            a = np.maximum(soft * 0.38, ring * 0.75)
            rgb[...] = GOLD
        arr[y0:y0 + S, :, :3] = rgb
        arr[y0:y0 + S, :, 3] = a
    return arr


# ---------------------------------------------------------------- corner filigree

def corner_ornament():
    """24x24 top-left filigree (mirrored for the other corners): a gold corner
    plate, rules running along both edges, an inner quarter-arc and a stud."""
    S, K = 24, 8
    m = Image.new("L", (S * K, S * K), 0)
    d = ImageDraw.Draw(m)
    P = lambda pts: [(x * K, y * K) for x, y in pts]
    d.polygon(P([(0, 0), (12, 0), (0, 12)]), fill=255)
    d.line(P([(10, 1.6), (23, 1.6)]), fill=255, width=int(1.3 * K))
    d.line(P([(1.6, 10), (1.6, 23)]), fill=255, width=int(1.3 * K))
    d.arc([-15 * K, -15 * K, 15 * K, 15 * K], 0, 90, fill=255, width=int(1.4 * K))
    d.polygon(P([(10, 7.4), (12.6, 10), (10, 12.6), (7.4, 10)]), fill=255)
    # cut a small notch into the plate for a lighter, engraved look
    notch = Image.new("L", m.size, 0)
    ImageDraw.Draw(notch).polygon(P([(3.2, 3.2), (7.4, 3.2), (3.2, 7.4)]), fill=255)
    m = Image.fromarray(np.clip(np.asarray(m, dtype=np.int16) - np.asarray(notch, dtype=np.int16), 0, 255).astype(np.uint8))
    mask = np.asarray(m.resize((S, S), Image.BOX), dtype=np.float64) / 255
    rim = np.asarray(m.filter(ImageFilter.MaxFilter(2 * K + 1)).resize((S, S), Image.BOX), dtype=np.float64) / 255
    arr = np.zeros((S, S, 4))
    arr[..., :3] = DARK
    arr[..., 3] = rim * 0.8
    t = np.clip((np.arange(S)[:, None] + np.arange(S)[None, :]) / (2 * S - 2), 0, 1)
    gold = np.array(GOLD)[None, None, :] * (1 - t[..., None]) + np.array(BRONZE)[None, None, :] * t[..., None]
    arr[..., :3] = gold * mask[..., None] + arr[..., :3] * (1 - mask[..., None])
    arr[..., 3] = np.maximum(arr[..., 3], mask)
    return arr


def corner_set():
    tl = corner_ornament()
    return {"corner_tl": tl, "corner_tr": tl[:, ::-1].copy(), "corner_bl": tl[::-1, :].copy(),
            "corner_br": tl[::-1, ::-1].copy()}


# ---------------------------------------------------------------- pin button

def pin_button(pinned):
    """19x19 window pin: a dark disc with a bronze ring and a push-pin.
    Unpinned: the pin lies tilted and dim. Pinned: it stands upright, bright."""
    S, K = 19, 8
    yy, xx = np.mgrid[0:S * K, 0:S * K]
    r = np.hypot(xx + 0.5 - S * K / 2, yy + 0.5 - S * K / 2) / (S * K / 2)
    disc = (r <= 0.96).astype(np.float64)
    ring = ((r <= 0.96) & (r >= 0.80)).astype(np.float64)
    pin = Image.new("L", (S * K, S * K), 0)
    d = ImageDraw.Draw(pin)
    P = lambda pts: [(x * K, y * K) for x, y in pts]
    if pinned:
        d.ellipse(P([(6.2, 3.6), (12.8, 9.0)]), fill=255)            # head
        d.polygon(P([(7.6, 8.4), (11.4, 8.4), (10.2, 11.2), (8.8, 11.2)]), fill=255)
        d.line(P([(9.5, 11), (9.5, 15.6)]), fill=255, width=int(1.4 * K))  # needle
    else:
        d.ellipse(P([(9.2, 3.8), (14.6, 9.2)]), fill=255)
        d.polygon(P([(9.0, 8.0), (11.6, 10.4), (9.6, 12.0), (7.6, 10.2)]), fill=255)
        d.line(P([(8.2, 11.2), (4.6, 14.8)]), fill=255, width=int(1.4 * K))
    pin_m = np.asarray(pin, dtype=np.float64) / 255

    def down(a):
        return a.reshape(S, K, S, K).mean(axis=(1, 3))
    arr = np.zeros((S, S, 4))
    arr[..., :3] = (14, 12, 10)
    arr[..., 3] = down(disc) * 0.85
    ring_d = down(ring)
    col_ring = np.array(GOLD if pinned else BRONZE, dtype=np.float64)
    arr[..., :3] = arr[..., :3] * (1 - ring_d[..., None]) + col_ring * ring_d[..., None]
    arr[..., 3] = np.maximum(arr[..., 3], ring_d)
    pm = down(pin_m)
    col_pin = np.array(GOLD if pinned else (196, 176, 136), dtype=np.float64)
    arr[..., :3] = arr[..., :3] * (1 - pm[..., None]) + col_pin * pm[..., None]
    arr[..., 3] = np.maximum(arr[..., 3], pm)
    return arr


# ---------------------------------------------------------------- main

def main():
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    images = {}
    images.update(border_set("thick", THICK_OUTER_FIRST))
    images.update(border_set("thin", THIN_OUTER_FIRST))
    mid, side = caption_line()
    images["caption_mid"] = mid
    images["caption_side"] = side
    images["clear"] = clear(2, 2)
    images["button_face"] = button_face()
    images.update(button_frame_set())
    images["bar_gray"] = bar_gray()
    images["thumb_v"] = thumb_v()
    images["thumb_h"] = transpose(thumb_v())
    for direction in ("up", "down", "left", "right"):
        images["arrow_" + direction] = arrow(direction)
    images["inset"] = plate(0.42)
    images["icon_button"] = icon_button_glow()
    images.update(corner_set())
    images["pin_up"] = pin_button(False)
    images["pin_down"] = pin_button(True)
    for name, arr in images.items():
        path = target / (name + ".png")
        img(arr).save(path, optimize=True)
        print("wrote", path)


if __name__ == "__main__":
    main()
