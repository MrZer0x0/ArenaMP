#!/usr/bin/env python3
"""ArenaMP UI9: contextual crosshair icons and the dynamic reticle.

Silver glyphs with a dark rim, like the sneak eye: the HUD tints them gold
(normal) or warm red (stealing, pickpocketing, trapped locks). Drawn at 8x and
downsampled, original artwork.

Reticle: a centre dot and four inward chevrons that the HUD moves outwards
(movement, bow/crossbow spread) and back in (aiming focus).

Usage: python3 tools/arenamp_make_crosshair.py files/vfs/textures/ui/arenamw/hud
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

K = 8
TOP = (252, 252, 254)
BOTTOM = (178, 180, 188)


def canvas(size):
    return Image.new("L", (size * K, size * K), 0)


def P(points):
    return [(x * K, y * K) for x, y in points]


def finish(mask_img, size, rim=1.6, shadow=True):
    mask = np.asarray(mask_img.resize((size, size), Image.BOX), dtype=np.float64) / 255.0
    rim_img = mask_img.filter(ImageFilter.MaxFilter(int(rim * 2 * K) | 1))
    ring = np.asarray(rim_img.resize((size, size), Image.BOX), dtype=np.float64) / 255.0
    out = np.zeros((size, size, 4))
    if shadow:
        sh = np.asarray(rim_img.filter(ImageFilter.GaussianBlur(2 * K)).resize((size, size), Image.BOX),
                        dtype=np.float64) / 255.0
        sh = np.roll(sh, (1, 1), axis=(0, 1))
        out[..., 3] = sh * 0.45
    # dark rim
    a = ring * 0.92
    out[..., :3] = out[..., :3] * (1 - a[..., None]) * 0 + np.array([16, 16, 20]) * (a[..., None] > 0)
    out[..., 3] = np.maximum(out[..., 3], a)
    # silver body
    t = np.linspace(0, 1, size)[:, None]
    body = np.array(TOP)[None, None, :] * (1 - t[..., None]) + np.array(BOTTOM)[None, None, :] * t[..., None]
    body = np.broadcast_to(body, (size, size, 3))
    m = mask[..., None]
    out[..., :3] = body * m + out[..., :3] * (1 - m)
    out[..., 3] = np.maximum(out[..., 3], mask)
    rgba = np.concatenate([out[..., :3], out[..., 3:4] * 255], axis=2)
    return Image.fromarray(np.clip(rgba + 0.5, 0, 255).astype(np.uint8), "RGBA")


def line(d, pts, w):
    d.line(P(pts), fill=255, width=int(w * K), joint="curve")
    r = w * K / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse([x * K - r, y * K - r, x * K + r, y * K + r], fill=255)


def arc(d, cx, cy, r, a0, a1, w, n=40):
    pts = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
            cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    line(d, pts, w)


# ------------------------------------------------------------------ icons (48 px)

def hand():
    """Open hand: take an item (red when it is someone else's)."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.rounded_rectangle(P([(13, 22), (35, 42)]), radius=7 * K, fill=255)       # palm
    for x, top in ((15, 9), (21, 6), (27, 7), (33, 11)):                        # fingers
        d.rounded_rectangle(P([(x - 2.6, top), (x + 2.6, 26)]), radius=2.6 * K, fill=255)
    d.polygon(P([(13, 30), (6, 22), (9, 19), (16, 25)]), fill=255)              # thumb
    return m


def talk():
    """Speech bubble with three dots: talk."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.rounded_rectangle(P([(6, 8), (42, 32)]), radius=9 * K, fill=255)
    d.polygon(P([(14, 30), (12, 42), (24, 31)]), fill=255)
    for x in (16, 24, 32):
        d.ellipse(P([(x - 2.6, 17.4), (x + 2.6, 22.6)]), fill=0)
    return m


def activate():
    """Pointing gauntlet-like chevron in a ring: generic use/activate."""
    m = canvas(48); d = ImageDraw.Draw(m)
    arc(d, 24, 24, 15, 0, 360, 3.4)
    d.polygon(P([(18, 15), (33, 24), (18, 33), (21, 24)]), fill=255)
    return m


def door():
    """Arched doorway: open / go through."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.pieslice(P([(11, 6), (37, 32)]), 180, 360, fill=255)
    d.rectangle(P([(11, 19), (37, 42)]), fill=255)
    hole = canvas(48); hd = ImageDraw.Draw(hole)
    hd.pieslice(P([(16, 11), (32, 27)]), 180, 360, fill=255)
    hd.rectangle(P([(16, 19), (32, 42)]), fill=255)
    out = np.clip(np.asarray(m, dtype=np.int16) - np.asarray(hole, dtype=np.int16), 0, 255).astype(np.uint8)
    m = Image.fromarray(out, "L"); d = ImageDraw.Draw(m)
    d.ellipse(P([(26.5, 27), (30.5, 31)]), fill=255)                            # handle
    d.rectangle(P([(6, 40), (42, 43)]), fill=255)                               # threshold
    return m


def chest():
    """Chest: open a container."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.rounded_rectangle(P([(7, 12), (41, 22)]), radius=5 * K, fill=255)         # lid
    d.rectangle(P([(7, 24), (41, 40)]), fill=255)                               # body
    cut = canvas(48); cd = ImageDraw.Draw(cut)
    cd.rectangle(P([(20.5, 20), (27.5, 29)]), fill=255)                         # lock plate gap
    out = np.clip(np.asarray(m, dtype=np.int16) - np.asarray(cut, dtype=np.int16), 0, 255).astype(np.uint8)
    m = Image.fromarray(out, "L"); d = ImageDraw.Draw(m)
    d.rectangle(P([(22, 21.5), (26, 27.5)]), fill=255)
    return m


def lockpick():
    """Padlock with a pick: locked door or container."""
    m = canvas(48); d = ImageDraw.Draw(m)
    arc(d, 20, 18, 7.5, 180, 360, 3.6)
    d.line(P([(12.5, 18), (12.5, 23)]), fill=255, width=int(3.6 * K))
    d.line(P([(27.5, 18), (27.5, 23)]), fill=255, width=int(3.6 * K))
    d.rounded_rectangle(P([(8, 22), (32, 40)]), radius=3 * K, fill=255)
    key = canvas(48); kd = ImageDraw.Draw(key)
    kd.ellipse(P([(18, 27), (22, 31)]), fill=255); kd.rectangle(P([(19, 30), (21, 35)]), fill=255)
    out = np.clip(np.asarray(m, dtype=np.int16) - np.asarray(key, dtype=np.int16), 0, 255).astype(np.uint8)
    m = Image.fromarray(out, "L"); d = ImageDraw.Draw(m)
    line(d, [(26, 36), (42, 14)], 2.8)                                         # the pick
    line(d, [(42, 14), (38, 10)], 2.4)
    return m


def probe():
    """Probe over a spark: trapped (shown in red)."""
    m = canvas(48); d = ImageDraw.Draw(m)
    line(d, [(10, 40), (30, 16)], 3.4)
    d.rounded_rectangle(P([(6, 36), (14, 44)]), radius=2 * K, fill=255)
    for ang in range(0, 360, 45):
        r0, r1 = (3, 9) if ang % 90 == 0 else (3, 6)
        line(d, [(34 + r0 * math.cos(math.radians(ang)), 12 + r0 * math.sin(math.radians(ang))),
                 (34 + r1 * math.cos(math.radians(ang)), 12 + r1 * math.sin(math.radians(ang)))], 2.2)
    return m


def book():
    """Open book: read."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.polygon(P([(5, 12), (23, 15), (23, 40), (5, 37)]), fill=255)
    d.polygon(P([(43, 12), (25, 15), (25, 40), (43, 37)]), fill=255)
    for y in (20, 26, 32):
        d.line(P([(9, y - 1), (19, y + 1)]), fill=0, width=int(1.4 * K))
        d.line(P([(39, y - 1), (29, y + 1)]), fill=0, width=int(1.4 * K))
    return m


def pouch():
    """Pouch: search a body."""
    m = canvas(48); d = ImageDraw.Draw(m)
    d.ellipse(P([(9, 16), (39, 44)]), fill=255)
    d.polygon(P([(17, 18), (31, 18), (35, 8), (13, 8)]), fill=255)
    tie = canvas(48); td = ImageDraw.Draw(tie)
    td.line(P([(15, 18.5), (33, 18.5)]), fill=255, width=int(2.4 * K))
    out = np.clip(np.asarray(m, dtype=np.int16) - np.asarray(tie, dtype=np.int16), 0, 255).astype(np.uint8)
    return Image.fromarray(out, "L")


# ------------------------------------------------------------------ reticle

def dot():
    m = canvas(8); d = ImageDraw.Draw(m)
    d.ellipse(P([(2, 2), (6, 6)]), fill=255)
    return m


def tick(direction):
    """12x12 chevron pointing at the screen centre."""
    m = canvas(12); d = ImageDraw.Draw(m)
    pts = {"top": [(2, 3.5), (6, 8), (10, 3.5)], "bottom": [(2, 8.5), (6, 4), (10, 8.5)],
           "left": [(3.5, 2), (8, 6), (3.5, 10)], "right": [(8.5, 2), (4, 6), (8.5, 10)]}[direction]
    line(d, pts, 2.0)
    return m


def main():
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    icons = {"ch_hand": hand(), "ch_talk": talk(), "ch_activate": activate(), "ch_door": door(),
             "ch_chest": chest(), "ch_lockpick": lockpick(), "ch_probe": probe(), "ch_book": book(),
             "ch_pouch": pouch()}
    for name, mask in icons.items():
        finish(mask, 48).save(target / (name + ".png"), optimize=True)
        print("wrote", target / (name + ".png"))
    finish(dot(), 8, rim=0.8, shadow=False).save(target / "ch_dot.png", optimize=True)
    for direction in ("top", "bottom", "left", "right"):
        finish(tick(direction), 12, rim=0.8, shadow=False).save(target / ("ch_tick_%s.png" % direction), optimize=True)
    print("wrote reticle")


if __name__ == "__main__":
    main()
