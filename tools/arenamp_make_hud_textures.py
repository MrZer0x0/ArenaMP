#!/usr/bin/env python3
"""ArenaMP UI4: HUD textures in a dark-glass / bronze style (resource bars,
combat health bars, notification cards, horizontal compass band and the
three-state sneak eye).

Original artwork, drawn with Pillow + numpy. Shapes are drawn at 4x and
downsampled with premultiplied alpha, so edges stay smooth and no dark fringe
appears when MyGUI stretches the textures.

Every texture is consumed by files/mygui/openmw_hud_energybar.skin.xml or by
apps/openmw/mwgui/hud.cpp; nothing else references this folder.

Usage: python3 tools/arenamp_make_hud_textures.py files/vfs/textures/ui/arenamw/hud
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

K = 4  # supersampling factor

BRONZE_TOP = (214, 186, 124)
BRONZE_BOTTOM = (112, 88, 52)
GLASS_TOP = (16, 15, 13)
GLASS_BOTTOM = (4, 4, 5)


# ---------------------------------------------------------------- helpers

def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def premul_down(rgba, w, h):
    """rgba: float array (H*K, W*K, 4) in 0..1 straight alpha -> PIL image w x h."""
    a = rgba[..., 3:4]
    pre = np.concatenate([rgba[..., :3] * a, a], axis=2)
    pre = pre.reshape(h, rgba.shape[0] // h, w, rgba.shape[1] // w, 4).mean(axis=(1, 3))
    alpha = pre[..., 3:4]
    rgb = np.where(alpha > 1e-4, pre[..., :3] / np.maximum(alpha, 1e-4), 0.0)
    out = np.concatenate([rgb, alpha], axis=2)
    return Image.fromarray(np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGBA")


def mask_from_polygon(w, h, points, inset=0.0):
    img = Image.new("L", (w * K, h * K), 0)
    ImageDraw.Draw(img).polygon([(x * K, y * K) for x, y in points], fill=255)
    m = np.asarray(img, dtype=np.float64) / 255.0
    if inset > 0:
        r = int(round(inset * K)) * 2 + 1
        m = np.asarray(img.filter(ImageFilter.MinFilter(r)), dtype=np.float64) / 255.0
    return m


def vertical(h, top, bottom):
    """(H*K, 1, C) gradient."""
    t = np.linspace(0.0, 1.0, h * K)[:, None, None]
    return np.asarray(top, dtype=np.float64)[None, None, :] * (1 - t) + \
        np.asarray(bottom, dtype=np.float64)[None, None, :] * t


def over(dst, src):
    """Straight-alpha 'over' compositing of float RGBA arrays."""
    sa = src[..., 3:4]
    da = dst[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = np.where(oa > 1e-6, (src[..., :3] * sa + dst[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-6), 0)
    return np.concatenate([rgb, oa], axis=2)


def layer(mask, rgb, alpha=1.0):
    h, w = mask.shape
    col = np.broadcast_to(np.asarray(rgb, dtype=np.float64) / 255.0, (h, w, 3)) if np.ndim(rgb) == 1 \
        else np.broadcast_to(rgb / 255.0, (h, w, 3))
    return np.concatenate([col, (mask * alpha)[..., None]], axis=2)


def empty(w, h):
    return np.zeros((h * K, w * K, 4))


# ---------------------------------------------------------------- bars

def bar_back():
    """64x16 backplate with pointed ends. Skin: caps 10 px, client 8 3 48 10."""
    w, h = 64, 16
    hexagon = [(0, 8), (7.5, 1), (56.5, 1), (64, 8), (56.5, 15), (7.5, 15)]
    outer = mask_from_polygon(w, h, hexagon)
    inner = mask_from_polygon(w, h, hexagon, inset=1.0)
    out = empty(w, h)
    rim = vertical(h, BRONZE_TOP, BRONZE_BOTTOM)
    out = over(out, layer(outer, np.broadcast_to(rim, (h * K, w * K, 3)), 0.95))
    glass = vertical(h, GLASS_TOP, GLASS_BOTTOM)
    out = over(out, layer(inner, np.broadcast_to(glass, (h * K, w * K, 3)), 0.80))
    # Faint glass highlight just under the upper rim.
    hl = np.zeros_like(inner)
    hl[int(2.0 * K):int(3.0 * K), :] = 1.0
    out = over(out, layer(hl * inner, (255, 240, 205), 0.10))
    return premul_down(out, w, h)


def thin_back():
    """64x6 plate for the cast/weapon readiness bars. Skin: caps 3 px, client 2 1 60 4."""
    w, h = 64, 6
    shape = [(0, 3), (2.5, 0), (61.5, 0), (64, 3), (61.5, 6), (2.5, 6)]
    outer = mask_from_polygon(w, h, shape)
    inner = mask_from_polygon(w, h, shape, inset=0.75)
    out = empty(w, h)
    out = over(out, layer(outer, (150, 124, 78), 0.85))
    out = over(out, layer(inner, (6, 6, 6), 0.82))
    return premul_down(out, w, h)


def fill(top, mid, bottom, alpha=1.0):
    """8x16 fill: lit top edge, glossy upper half, darker base."""
    w, h = 8, 16
    rows = []
    for y in range(h * K):
        t = y / (h * K - 1)
        if t < 0.5:
            c = lerp(top, mid, t / 0.5)
        else:
            c = lerp(mid, bottom, (t - 0.5) / 0.5)
        if t < 0.45:  # gloss band
            c = lerp(c, (255, 255, 255), 0.16 * (1 - t / 0.45))
        if y < K:  # bright lip
            c = lerp(c, (255, 250, 235), 0.45)
        if y >= h * K - K:  # shadow lip
            c = lerp(c, (0, 0, 0), 0.35)
        rows.append(c)
    col = np.asarray(rows, dtype=np.float64)[:, None, :] / 255.0
    rgba = np.concatenate([np.broadcast_to(col, (h * K, w * K, 3)),
                           np.full((h * K, w * K, 1), alpha)], axis=2)
    return premul_down(rgba, w, h)


FILLS = {
    "fill_health": ((236, 104, 84), (176, 34, 26), (88, 14, 10)),
    "fill_magicka": ((120, 178, 255), (36, 92, 200), (14, 40, 104)),
    "fill_stamina": ((150, 222, 110), (50, 150, 44), (18, 72, 16)),
    "fill_cast": ((196, 228, 255), (96, 164, 236), (40, 92, 172)),
    "fill_weapon": ((255, 222, 146), (222, 160, 60), (128, 80, 20)),
    "fill_xp": ((255, 232, 156), (212, 166, 70), (118, 82, 24)),
    # Neutral fill, tinted in code (enemy red / ally green).
    "fill_tint": ((255, 255, 255), (214, 214, 214), (118, 118, 118)),
}


def fill_lag():
    # Pale "recent damage" trail shown behind combat fills.
    return fill((255, 240, 206), (236, 204, 156), (170, 132, 92), alpha=0.85)


def plate():
    """8x8 dark plate behind the overhead combat bars."""
    rgba = np.zeros((8 * K, 8 * K, 4))
    rgba[..., :3] = 6 / 255.0
    rgba[..., 3] = 0.66
    return premul_down(rgba, 8, 8)


def combat_frame():
    """64x12 bronze frame with a transparent interior. Skin: caps 4 px."""
    w, h = 64, 12
    outer = mask_from_polygon(w, h, [(0, 0), (64, 0), (64, 12), (0, 12)])
    hole = mask_from_polygon(w, h, [(1, 1), (63, 1), (63, 11), (1, 11)])
    ring = np.clip(outer - hole, 0, 1)
    rim = np.broadcast_to(vertical(h, (226, 198, 136), (120, 96, 58)), (h * K, w * K, 3))
    out = layer(ring, rim, 0.95)
    # Brighter end posts, like the clasps on the player bars.
    posts = np.zeros_like(ring)
    posts[:, :int(2 * K)] = 1.0
    posts[:, -int(2 * K):] = 1.0
    out = over(out, layer(posts * outer, (246, 222, 160), 0.90))
    return premul_down(out, w, h)


# ---------------------------------------------------------------- compass / toast

def smoothstep(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def compass_band():
    """256x32 band that fades out towards both ends (stretched to the compass)."""
    w, h = 256, 32
    xs = np.linspace(0, 1, w * K)
    ys = np.linspace(0, 1, h * K)
    end_fade = smoothstep(np.minimum(xs, 1 - xs) / 0.20)
    line_fade = smoothstep(np.minimum(xs, 1 - xs) / 0.32)
    vert = 0.78 - 0.30 * np.abs(ys - 0.5) * 2  # denser in the middle
    a = vert[:, None] * end_fade[None, :] * 0.62
    out = np.zeros((h * K, w * K, 4))
    out[..., :3] = np.broadcast_to(vertical(h, (18, 16, 13), (6, 6, 6)), (h * K, w * K, 3)) / 255.0
    out[..., 3] = a
    for y0, alpha in ((0, 0.85), (h * K - K, 0.60)):
        line = np.zeros((h * K, w * K))
        line[y0:y0 + K, :] = line_fade[None, :]
        out = over(out, layer(line, (214, 186, 124), alpha))
    return premul_down(out, w, h)


def compass_notch():
    """12x7 upward chevron that marks the heading in the compass centre."""
    w, h = 12, 7
    tri = mask_from_polygon(w, h, [(6, 0), (12, 7), (0, 7)])
    hole = mask_from_polygon(w, h, [(6, 3.2), (9.2, 7), (2.8, 7)])
    m = np.clip(tri - hole, 0, 1)
    out = layer(m, np.broadcast_to(vertical(h, (250, 228, 168), (170, 128, 60)), (h * K, w * K, 3)), 1.0)
    return premul_down(out, w, h)


def toast_back():
    """256x32 notification card: soft left fade, dark glass, bronze top rule."""
    w, h = 256, 32
    xs = np.linspace(0, 1, w * K)
    body = 0.18 + 0.60 * smoothstep(xs / 0.14)
    out = np.zeros((h * K, w * K, 4))
    out[..., :3] = np.broadcast_to(vertical(h, (22, 19, 15), (6, 6, 6)), (h * K, w * K, 3)) / 255.0
    out[..., 3] = body[None, :]
    rule = np.zeros((h * K, w * K))
    rule[0:K, :] = smoothstep(xs / 0.55)[None, :]
    out = over(out, layer(rule, (214, 186, 124), 0.80))
    base = np.zeros((h * K, w * K))
    base[h * K - K:, :] = smoothstep(xs / 0.55)[None, :]
    out = over(out, layer(base, (90, 72, 44), 0.55))
    return premul_down(out, w, h)


# ---------------------------------------------------------------- sneak eye

def bezier(p0, p1, p2, n=48):
    pts = []
    for i in range(n + 1):
        t = i / n
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    return pts


def eye_icon(openness):
    """64x64 silver eye (tinted in code). openness: 0 closed, 0.5 half, 1 open."""
    S = 64
    img = Image.new("L", (S * K, S * K), 0)
    d = ImageDraw.Draw(img)
    P = lambda pts: [(x * K, y * K) for x, y in pts]
    left, right, cy = (7, 32), (57, 32), 32
    lower = bezier(left, (32, 54), right)
    upper_ctl_y = 10  # open almond; the half-open state lowers a separate lid
    upper = bezier(left, (32, upper_ctl_y), right)

    if openness <= 0.01:
        # Closed lid: thick crescent + lashes.
        crescent = bezier(left, (32, 52), right) + list(reversed(bezier((9, 32), (32, 44), (55, 32))))
        d.polygon(P(crescent), fill=255)
        for x0, y0, x1, y1 in ((16, 40, 12, 48), (25, 43, 23, 52), (32, 44, 32, 53),
                               (39, 43, 41, 52), (48, 40, 52, 48)):
            d.line(P([(x0, y0), (x1, y1)]), fill=255, width=int(3.2 * K))
    else:
        # Almond outline (ring), iris clipped to the aperture.
        almond = upper + list(reversed(lower))
        d.polygon(P(almond), fill=255)
        hole = Image.new("L", img.size, 0)
        hd = ImageDraw.Draw(hole)
        inner_upper = bezier((12, 32), (32, upper_ctl_y + 5.5), (52, 32))
        inner_lower = bezier((12, 32), (32, 48), (52, 32))
        hd.polygon(P(inner_upper + list(reversed(inner_lower))), fill=255)
        iris = Image.new("L", img.size, 0)
        idr = ImageDraw.Draw(iris)
        idr.ellipse(P([(22, 22), (42, 42)]), fill=255)
        idr.ellipse(P([(28, 28), (36, 36)]), fill=0)          # pupil
        idr.ellipse(P([(34.5, 26.5), (38.5, 30.5)]), fill=255)  # glint
        a = np.asarray(img, dtype=np.float64) / 255
        hmask = np.asarray(hole, dtype=np.float64) / 255
        imask = np.asarray(iris, dtype=np.float64) / 255
        m = np.clip(a - hmask + imask * hmask, 0, 1)
        if openness < 0.99:
            # Half-open "suspicious" eye: keep only what lies below a lowered lid
            # and draw the lid itself as a heavy stroke across the eye.
            lid_curve = bezier(left, (32, 18 + (1 - openness) * 14), right)
            below = Image.new("L", img.size, 0)
            ImageDraw.Draw(below).polygon(P(lid_curve + [(64, 64), (0, 64)]), fill=255)
            m = m * (np.asarray(below, dtype=np.float64) / 255)
            stroke = Image.new("L", img.size, 0)
            ImageDraw.Draw(stroke).line(P(lid_curve), fill=255, width=int(5 * K), joint="curve")
            m = np.clip(m + np.asarray(stroke, dtype=np.float64) / 255, 0, 1)
        if openness >= 0.99:
            # Alert rays above the open eye.
            rays = Image.new("L", img.size, 0)
            rd = ImageDraw.Draw(rays)
            for x0, y0, x1, y1 in ((32, 8, 32, 2), (20, 11, 16, 5), (44, 11, 48, 5)):
                rd.line(P([(x0, y0), (x1, y1)]), fill=255, width=int(3.2 * K))
            m = np.clip(m + np.asarray(rays, dtype=np.float64) / 255, 0, 1)
        img = Image.fromarray((m * 255).astype(np.uint8), "L")

    mask = np.asarray(img, dtype=np.float64) / 255.0
    outline = np.asarray(img.filter(ImageFilter.MaxFilter(2 * K + 1)), dtype=np.float64) / 255.0
    shadow_img = img.filter(ImageFilter.MaxFilter(2 * K + 1)).filter(ImageFilter.GaussianBlur(2 * K))
    shadow = np.roll(np.asarray(shadow_img, dtype=np.float64) / 255.0, (2 * K, K), axis=(0, 1))
    out = empty(S, S)
    out = over(out, layer(shadow, (0, 0, 0), 0.50))
    out = over(out, layer(outline, (18, 18, 22), 0.95))
    silver = np.broadcast_to(vertical(S, (250, 250, 252), (176, 178, 186)), (S * K, S * K, 3))
    out = over(out, layer(mask, silver, 1.0))
    return premul_down(out, S, S)


# ---------------------------------------------------------------- UI9.3 shaped fills and round icon frames

BAR_HEXAGON = [(0, 8), (7.5, 1), (56.5, 1), (64, 8), (56.5, 15), (7.5, 15)]
THIN_HEXAGON = [(0, 3), (2.5, 0), (61.5, 0), (64, 3), (61.5, 6), (2.5, 6)]


def shaped_fill(colours, w, h, shape, inset, inner_top, inner_bottom):
    """A fill with exactly the glass shape of its backplate (same size, same
    3-slice cuts), so a full bar fills the pointed ends and never covers the
    bronze rim. The HUD reveals it from the left by clipping, see
    apps/openmw/mwgui/shapedbar.hpp."""
    top, mid, bottom = colours
    mask = mask_from_polygon(w, h, shape, inset=inset)
    rows = []
    for y in range(h * K):
        py = (y + 0.5) / K
        t = min(1.0, max(0.0, (py - inner_top) / max(0.001, inner_bottom - inner_top)))
        c = lerp(top, mid, t / 0.5) if t < 0.5 else lerp(mid, bottom, (t - 0.5) / 0.5)
        if t < 0.45:  # gloss band
            c = lerp(c, (255, 255, 255), 0.16 * (1 - t / 0.45))
        if py - inner_top < 1.0:  # bright lip under the rim
            c = lerp(c, (255, 250, 235), 0.40)
        if inner_bottom - py < 1.0:  # shadow lip
            c = lerp(c, (0, 0, 0), 0.30)
        rows.append(c)
    col = np.asarray(rows, dtype=np.float64)[:, None, :] / 255.0
    rgba = np.concatenate([np.broadcast_to(col, (h * K, w * K, 3)), mask[..., None]], axis=2)
    return premul_down(rgba, w, h)


SHAPED_FILLS = {
    "shape_health": "fill_health",
    "shape_magicka": "fill_magicka",
    "shape_stamina": "fill_stamina",
    "shape_xp": "fill_xp",
}
THIN_SHAPED_FILLS = {
    "thinshape_cast": "fill_cast",
    "thinshape_weapon": "fill_weapon",
}


def radial(size, inner, outer):
    """Distance field in output pixels, centred, at K supersampling."""
    c = size / 2.0
    ys, xs = np.mgrid[0:size * K, 0:size * K]
    d = np.hypot((xs + 0.5) / K - c, (ys + 0.5) / K - c)
    return d


def ring_mask(d, r0, r1):
    # Smooth 1-subpixel edges on both radii.
    a = np.clip((d - r0) * K + 0.5, 0, 1)
    b = np.clip((r1 - d) * K + 0.5, 0, 1)
    return a * b


ICON_SIZE = 52      # texture size; shown at 26x26 (2x for crisp edges)
ICON_OUTER = 25.8   # outer edge of the ring
ICON_INNER = 18.0   # icon window: the 18 px icon's corners (r = 12.7 px shown,
                    # 25.5 in texture units) always lie under the opaque ring


def icon_disc():
    """Dark glass disc behind the weapon/spell icon (only its window shows)."""
    S = ICON_SIZE
    d = radial(S, 0, 0)
    disc = ring_mask(d, -1.0, ICON_INNER + 0.5)
    glass = vertical(S, (26, 24, 20), (6, 6, 7))
    out = empty(S, S)
    out = over(out, layer(disc, np.broadcast_to(glass, (S * K, S * K, 3)), 0.86))
    return premul_down(out, S, S)


def icon_ring():
    """Opaque bronze medallion ring drawn above the icon. Everything of the
    square icon outside the round window lies under this ring, so the icon is
    seen as a full circle and never sticks out of the frame."""
    S = ICON_SIZE
    d = radial(S, 0, 0)
    ys = (np.mgrid[0:S * K, 0:S * K][0] + 0.5) / K
    out = empty(S, S)
    rim = ring_mask(d, ICON_INNER, ICON_OUTER)
    bronze = vertical(S, BRONZE_TOP, BRONZE_BOTTOM)
    out = over(out, layer(rim, np.broadcast_to(bronze, (S * K, S * K, 3)), 1.0))
    # Engraved groove in the middle of the ring.
    groove = ring_mask(d, 21.6, 22.6)
    out = over(out, layer(groove, (70, 52, 26), 0.85))
    # Lit outer edge on the upper half, darker lower half.
    upper = np.clip((S / 2.0 - ys) / (S / 2.0), 0, 1)
    lower = np.clip((ys - S / 2.0) / (S / 2.0), 0, 1)
    lit = ring_mask(d, 24.2, 25.4) * upper
    out = over(out, layer(lit, (255, 238, 196), 0.60))
    shade = ring_mask(d, ICON_INNER, ICON_OUTER) * lower
    out = over(out, layer(shade, (40, 28, 12), 0.25))
    # Bevel: dark line where the ring meets the icon window.
    bevel = ring_mask(d, ICON_INNER, ICON_INNER + 1.2)
    out = over(out, layer(bevel, (28, 20, 10), 0.95))
    return premul_down(out, S, S)


def shaped_images():
    images = {}
    for name, fill_name in SHAPED_FILLS.items():
        images[name] = shaped_fill(FILLS[fill_name], 64, 16, BAR_HEXAGON, 1.0, 2.0, 14.0)
    for name, fill_name in THIN_SHAPED_FILLS.items():
        images[name] = shaped_fill(FILLS[fill_name], 64, 6, THIN_HEXAGON, 0.75, 0.75, 5.25)
    images["icon_disc"] = icon_disc()
    images["icon_ring"] = icon_ring()
    return images


# ---------------------------------------------------------------- main

def main():
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    images = {
        "bar_back": bar_back(),
        "thin_back": thin_back(),
        "fill_lag": fill_lag(),
        "plate": plate(),
        "combat_frame": combat_frame(),
        "compass_band": compass_band(),
        "compass_notch": compass_notch(),
        "toast_back": toast_back(),
        "eye_hidden": eye_icon(0.0),
        "eye_suspicious": eye_icon(0.5),
        "eye_detected": eye_icon(1.0),
    }
    for name, colours in FILLS.items():
        images[name] = fill(*colours)
    images.update(shaped_images())
    # Optional names after the folder: write only those (e.g. the UI9.3 set
    # without touching textures that were refined by hand later).
    only = set(sys.argv[2:])
    for name, image in images.items():
        if only and name not in only:
            continue
        path = target / (name + ".png")
        image.save(path, optimize=True)
        print("wrote", path)


if __name__ == "__main__":
    main()
