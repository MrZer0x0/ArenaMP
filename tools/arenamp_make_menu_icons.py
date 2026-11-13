#!/usr/bin/env python3
"""ArenaMP PM1/IF1/WF1/HS1: draws the Player Menu section icons, the inventory
filter icons, the map waypoint flag and the small button icons of the Chat,
Group and Home pages (btn_*; shown at 18-20 px, so the shapes are bold).

Original artwork, drawn with Pillow in the gold "engraved" look of the
Morrowind interface: a warm gold gradient glyph with a dark outline and a
soft shadow on a transparent background. Every icon is drawn at 4x and
downsampled, so the edges stay smooth at the 64x64 output size.

Usage: python3 tools/arenamp_make_menu_icons.py files/vfs/textures/ui/arenamw
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 64
K = 4                      # supersampling factor
N = SIZE * K               # drawing canvas
GOLD_TOP = (246, 222, 160)
GOLD_BOTTOM = (156, 112, 48)
OUTLINE = (34, 22, 10)


def canvas():
    return Image.new("L", (N, N), 0)


def p(*xy):
    """Scales 0..64 design coordinates to the supersampled canvas."""
    return [v * K for v in xy]


# ---------------------------------------------------------------- glyphs

def chat():
    m = canvas(); d = ImageDraw.Draw(m)
    # Back bubble (left, larger) with a tail.
    d.rounded_rectangle(p(5, 9, 43, 35), radius=9 * K, fill=255)
    d.polygon(p(12, 33, 10, 44, 22, 34), fill=255)
    # Front bubble (right) cut out with a gap, then filled again.
    d.rounded_rectangle(p(23, 25, 60, 50), radius=9 * K, fill=0)
    d.rounded_rectangle(p(26, 28, 58, 48), radius=8 * K, fill=255)
    d.polygon(p(50, 46, 54, 57, 42, 47), fill=255)
    # Text lines engraved into the back bubble.
    for y in (16, 23):
        d.rounded_rectangle(p(12, y, 33 if y == 16 else 26, y + 3), radius=K, fill=0)
    # Dots in the front bubble.
    for x in (34, 42, 50):
        d.ellipse(p(x - 2.5, 35.5, x + 2.5, 40.5), fill=0)
    return m


def person(d, cx, top, scale, fill=255):
    r = 7 * scale
    d.ellipse(p(cx - r, top, cx + r, top + 2 * r), fill=fill)
    w = 13 * scale
    body_top = top + 2 * r + 2 * scale
    d.pieslice(p(cx - w, body_top, cx + w, body_top + 2 * w), 180, 360, fill=fill)


def group():
    m = canvas(); d = ImageDraw.Draw(m)
    # Two companions behind, the leader in front with a separating halo.
    person(d, 17, 12, 0.8)
    person(d, 47, 12, 0.8)
    halo = canvas(); hd = ImageDraw.Draw(halo)
    person(hd, 32, 15, 1.08)
    halo = halo.filter(ImageFilter.MaxFilter(4 * K + 1))
    m = ImageChops.subtract(m, halo)
    d = ImageDraw.Draw(m)
    person(d, 32, 17, 1.0)
    # Cut the bodies flat at the bottom edge.
    d.rectangle(p(0, 56, 64, 64), fill=0)
    return m


def players():
    m = canvas(); d = ImageDraw.Draw(m)
    # A roster: three rows, each a small head and a name line.
    for i, y in enumerate((8, 25, 42)):
        d.ellipse(p(6, y, 20, y + 14), fill=255)
        d.rounded_rectangle(p(25, y + 4, 58 - i * 6, y + 10), radius=3 * K, fill=255)
    return m


def stats():
    m = canvas(); d = ImageDraw.Draw(m)
    # A medal: ribbon, round seal and a star cut into it.
    d.polygon(p(20, 4, 30, 4, 36, 24, 26, 26), fill=255)
    d.polygon(p(44, 4, 34, 4, 28, 24, 38, 26), fill=255)
    d.ellipse(p(12, 22, 52, 62), fill=255)
    d.ellipse(p(16, 26, 48, 58), fill=0)
    d.ellipse(p(18, 28, 46, 56), fill=255)
    cx, cy, ro, ri = 32, 42.5, 11, 4.6
    star = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = ro if i % 2 == 0 else ri
        star += [cx + r * math.cos(a), cy + r * math.sin(a)]
    d.polygon(p(*star), fill=0)
    return m


def home():
    m = canvas(); d = ImageDraw.Draw(m)
    d.polygon(p(32, 5, 61, 30, 54, 30, 32, 11, 10, 30, 3, 30), fill=255)   # roof
    d.rectangle(p(44, 9, 51, 22), fill=255)                                 # chimney
    d.polygon(p(32, 15, 52, 32, 52, 58, 12, 58, 12, 32), fill=255)          # walls
    d.rounded_rectangle(p(27, 38, 37, 58), radius=4 * K, fill=0)            # door
    d.rectangle(p(16, 36, 23, 43), fill=0)                                  # window
    d.rectangle(p(41, 36, 48, 43), fill=0)
    return m


def decor():
    m = canvas(); d = ImageDraw.Draw(m)
    # Armchair seen from the front.
    d.rounded_rectangle(p(14, 6, 50, 36), radius=8 * K, fill=255)           # back
    d.rounded_rectangle(p(18, 10, 46, 32), radius=6 * K, fill=0)
    d.rounded_rectangle(p(20, 12, 44, 30), radius=5 * K, fill=255)          # cushion
    d.rounded_rectangle(p(5, 24, 17, 52), radius=5 * K, fill=255)           # arms
    d.rounded_rectangle(p(47, 24, 59, 52), radius=5 * K, fill=255)
    d.rounded_rectangle(p(15, 36, 49, 50), radius=4 * K, fill=255)          # seat
    d.rectangle(p(9, 52, 14, 60), fill=255)                                 # legs
    d.rectangle(p(50, 52, 55, 60), fill=255)
    return m


# ------------------------------------------------- inventory filters (IF1)

def rotated(m, degrees):
    return m.rotate(degrees, resample=Image.BICUBIC, center=(N / 2, N / 2))


def inv_all():
    m = canvas(); d = ImageDraw.Draw(m)
    for x, y in ((7, 7), (35, 7), (7, 35), (35, 35)):
        d.rounded_rectangle(p(x, y, x + 22, y + 22), radius=5 * K, fill=255)
    return m


def inv_weapon():
    m = canvas(); d = ImageDraw.Draw(m)
    # Upright sword, then turned 45 degrees.
    d.polygon(p(32, 3, 37, 10, 37, 40, 27, 40, 27, 10), fill=255)            # blade
    d.line(p(32, 10, 32, 37), fill=0, width=int(1.6 * K))                    # fuller
    d.rounded_rectangle(p(17, 40, 47, 46), radius=3 * K, fill=255)           # guard
    d.rectangle(p(29, 46, 35, 56), fill=255)                                 # grip
    d.ellipse(p(27, 54, 37, 63), fill=255)                                   # pommel
    return rotated(m, -45)


def inv_apparel():
    m = canvas(); d = ImageDraw.Draw(m)
    # Tunic: shoulders, short sleeves and a V neck, with a centre seam.
    d.polygon(p(22, 6, 42, 6, 58, 14, 62, 30, 51, 33, 49, 24, 50, 58, 14, 58, 15, 24, 13, 33, 2, 30, 6, 14), fill=255)
    d.polygon(p(24, 5, 40, 5, 32, 20), fill=0)                               # V neck
    d.line(p(32, 23, 32, 55), fill=0, width=int(1.6 * K))                    # seam
    return m


def inv_magic():
    m = canvas(); d = ImageDraw.Draw(m)
    # Round potion flask with a cork, and a four-point sparkle.
    d.ellipse(p(8, 22, 46, 60), fill=255)
    d.rectangle(p(21, 12, 33, 26), fill=255)                                 # neck
    d.rounded_rectangle(p(19, 6, 35, 13), radius=2 * K, fill=255)            # cork
    d.ellipse(p(13, 27, 41, 55), fill=0)
    d.chord(p(13, 27, 41, 55), 10, 170, fill=255)                            # liquid
    d.ellipse(p(17, 31, 23, 37), fill=255)                                   # glint
    sx, sy = 50, 14
    d.polygon(p(sx, sy - 11, sx + 3, sy - 3, sx + 11, sy, sx + 3, sy + 3, sx, sy + 11, sx - 3, sy + 3, sx - 11, sy, sx - 3, sy - 3), fill=255)
    return m


def inv_misc():
    m = canvas(); d = ImageDraw.Draw(m)
    # Coin pouch: round body, gathered neck, flared top and a tie.
    d.ellipse(p(8, 24, 56, 62), fill=255)
    d.polygon(p(24, 28, 40, 28, 37, 16, 27, 16), fill=255)                   # neck
    d.polygon(p(27, 17, 37, 17, 46, 6, 32, 10, 18, 6), fill=255)             # flared top
    d.rounded_rectangle(p(21, 18, 43, 23), radius=2 * K, fill=0)             # tie gap
    d.rounded_rectangle(p(23, 19, 41, 22), radius=1.5 * K, fill=255)         # tie
    d.line(p(40, 21, 47, 30), fill=255, width=int(2.5 * K))                  # tie end
    return m


def inv_keys():
    m = canvas(); d = ImageDraw.Draw(m)
    # Old-fashioned key: ring bow, shaft and two teeth, turned 45 degrees.
    d.ellipse(p(20, 2, 44, 26), fill=255)
    d.ellipse(p(26, 8, 38, 20), fill=0)
    d.rectangle(p(29, 24, 35, 60), fill=255)
    d.rectangle(p(35, 46, 44, 51), fill=255)
    d.rectangle(p(35, 54, 41, 59), fill=255)
    return rotated(m, -40)


# ------------------------------------------------- map waypoint flag (WF1)

def map_flag():
    m = canvas(); d = ImageDraw.Draw(m)
    # Pole from the bottom-left (the exact map point is the pole's foot),
    # a swallow-tailed banner and a round finial.
    d.rectangle(p(10, 6, 15, 62), fill=255)                                  # pole
    d.ellipse(p(8, 1, 17, 10), fill=255)                                     # finial
    d.polygon(p(15, 9, 58, 9, 48, 21, 58, 33, 15, 33), fill=255)             # banner
    d.ellipse(p(26, 15, 38, 27), fill=0)                                     # emblem ring
    d.ellipse(p(29, 18, 35, 24), fill=255)
    d.rounded_rectangle(p(5, 58, 21, 63), radius=2 * K, fill=255)            # foot
    return m


# ------------------------------------- Chat / Group / Home button icons (HS1)
#
# These are shown at 18-20 px next to a caption, so every shape is bold and
# keeps at most one or two cut-outs; thin details would just turn to mush.

def W(v):
    return int(v * K)


def bubble(d, box, tail, fill=255):
    d.rounded_rectangle(p(*box), radius=10 * K, fill=fill)
    d.polygon(p(*tail), fill=fill)


def ring(d, box, width, fill=255):
    d.ellipse(p(*box), outline=fill, width=W(width))


def arrow_head(d, tip, angle_deg, size, fill=255):
    a = math.radians(angle_deg)
    tx, ty = tip
    left = (tx - size * math.cos(a) + size * 0.7 * math.sin(a), ty - size * math.sin(a) - size * 0.7 * math.cos(a))
    right = (tx - size * math.cos(a) - size * 0.7 * math.sin(a), ty - size * math.sin(a) + size * 0.7 * math.cos(a))
    d.polygon(p(tx, ty, *left, *right), fill=fill)


def plus(d, cx, cy, r, t, fill=255):
    d.rectangle(p(cx - r, cy - t, cx + r, cy + t), fill=fill)
    d.rectangle(p(cx - t, cy - r, cx + t, cy + r), fill=fill)


def star_points(cx, cy, ro, ri, n=5, start=-90):
    pts = []
    for i in range(2 * n):
        a = math.radians(start + i * 180 / n)
        r = ro if i % 2 == 0 else ri
        pts += [cx + r * math.cos(a), cy + r * math.sin(a)]
    return pts


def key_mask(angle):
    m = canvas(); d = ImageDraw.Draw(m)
    d.ellipse(p(21, 2, 43, 24), fill=255)
    d.ellipse(p(27, 8, 37, 18), fill=0)
    d.rectangle(p(29, 22, 35, 60), fill=255)
    d.rectangle(p(35, 46, 45, 52), fill=255)
    d.rectangle(p(35, 55, 42, 60), fill=255)
    return rotated(m, angle)


# chat ---------------------------------------------------------------------

def btn_ooc():
    m = canvas(); d = ImageDraw.Draw(m)
    bubble(d, (4, 8, 60, 46), (14, 44, 10, 58, 28, 45))
    # "( )" - out of character - cut into the bubble.
    d.arc(p(12, 14, 30, 42), 110, 250, fill=0, width=W(5))
    d.arc(p(34, 14, 52, 42), -70, 70, fill=0, width=W(5))
    return m


def btn_rp():
    m = canvas(); d = ImageDraw.Draw(m)
    # Theatre mask: face, eye holes and a smile.
    d.chord(p(6, -14, 58, 60), 0, 180, fill=255)
    d.rectangle(p(6, 6, 58, 24), fill=255)
    d.ellipse(p(15, 17, 28, 27), fill=0)
    d.ellipse(p(36, 17, 49, 27), fill=0)
    d.arc(p(18, 26, 46, 48), 20, 160, fill=0, width=W(5))
    d.ellipse(p(26, 4, 38, 10), fill=255)
    return m


def btn_say():
    m = canvas(); d = ImageDraw.Draw(m)
    bubble(d, (4, 8, 60, 46), (14, 44, 10, 58, 28, 45))
    for x in (19, 32, 45):
        d.ellipse(p(x - 4.5, 22.5, x + 4.5, 31.5), fill=0)
    return m


def btn_whisper():
    m = canvas(); d = ImageDraw.Draw(m)
    # A small hollow bubble: quiet speech.
    d.rounded_rectangle(p(10, 14, 54, 44), radius=10 * K, outline=255, width=W(6))
    d.polygon(p(18, 42, 14, 56, 30, 43), fill=255)
    for x in (24, 32, 40):
        d.ellipse(p(x - 2.5, 26.5, x + 2.5, 31.5), fill=255)
    return m


def btn_shout():
    m = canvas(); d = ImageDraw.Draw(m)
    # Megaphone with sound waves.
    d.polygon(p(6, 24, 16, 24, 38, 10, 38, 50, 16, 36, 6, 36), fill=255)
    d.rectangle(p(16, 36, 22, 50), fill=255)
    d.arc(p(34, 18, 50, 42), -50, 50, fill=255, width=W(5))
    d.arc(p(38, 8, 62, 52), -50, 50, fill=255, width=W(5))
    return m


def btn_local():
    m = canvas(); d = ImageDraw.Draw(m)
    # Map pin: talk to the people around you.
    d.ellipse(p(14, 4, 50, 40), fill=255)
    d.polygon(p(16, 30, 48, 30, 32, 60), fill=255)
    d.ellipse(p(24, 14, 40, 30), fill=0)
    return m


def btn_global():
    m = canvas(); d = ImageDraw.Draw(m)
    d.ellipse(p(4, 4, 60, 60), fill=255)
    d.ellipse(p(19, 6, 45, 58), outline=0, width=W(4))
    d.rectangle(p(30, 5, 34, 59), fill=0)
    d.rectangle(p(5, 30, 59, 34), fill=0)
    d.arc(p(8, -6, 56, 28), 30, 150, fill=0, width=W(4))
    d.arc(p(8, 36, 56, 70), 210, 330, fill=0, width=W(4))
    return m


def btn_plain():
    m = canvas(); d = ImageDraw.Draw(m)
    # Quill: a feather, then turned.
    d.ellipse(p(20, 2, 44, 50), fill=255)
    d.polygon(p(24, 40, 40, 40, 32, 62), fill=255)
    d.line(p(32, 8, 32, 58), fill=0, width=W(3))
    return rotated(m, -35)


def btn_me():
    m = canvas(); d = ImageDraw.Draw(m)
    person(d, 30, 6, 1.35)
    d.rectangle(p(0, 58, 64, 64), fill=0)
    return m


def btn_do():
    # An eye (the lens where two circles overlap): describe what is seen.
    a = canvas(); ImageDraw.Draw(a).ellipse(p(2, -14, 62, 46), fill=255)
    b = canvas(); ImageDraw.Draw(b).ellipse(p(2, 18, 62, 78), fill=255)
    m = ImageChops.multiply(a, b)
    d = ImageDraw.Draw(m)
    d.ellipse(p(20, 18, 44, 46), fill=0)
    d.ellipse(p(24, 22, 40, 42), fill=255)
    d.ellipse(p(28, 26, 36, 34), fill=0)
    return m


def btn_try():
    m = canvas(); d = ImageDraw.Draw(m)
    d.rounded_rectangle(p(6, 6, 58, 58), radius=11 * K, fill=255)
    for x, y in ((19, 19), (45, 19), (32, 32), (19, 45), (45, 45)):
        d.ellipse(p(x - 5, y - 5, x + 5, y + 5), fill=0)
    return m


def btn_emoji():
    m = canvas(); d = ImageDraw.Draw(m)
    d.ellipse(p(4, 4, 60, 60), fill=255)
    d.ellipse(p(18, 18, 27, 29), fill=0)
    d.ellipse(p(37, 18, 46, 29), fill=0)
    d.arc(p(16, 22, 48, 50), 25, 155, fill=0, width=W(5))
    return m


def btn_color():
    m = canvas(); d = ImageDraw.Draw(m)
    # Painter's palette with a thumb hole and paint dabs.
    d.ellipse(p(3, 8, 61, 58), fill=255)
    d.ellipse(p(36, 38, 50, 52), fill=0)
    d.pieslice(p(30, 30, 66, 66), 90, 160, fill=0)
    for x, y in ((18, 22), (31, 16), (44, 22), (15, 36)):
        d.ellipse(p(x - 5, y - 5, x + 5, y + 5), fill=0)
    return m


def btn_pin():
    m = canvas(); d = ImageDraw.Draw(m)
    # Push pin: keep the chat open.
    d.rounded_rectangle(p(20, 4, 44, 12), radius=3 * K, fill=255)
    d.polygon(p(24, 12, 40, 12, 42, 32, 22, 32), fill=255)
    d.rounded_rectangle(p(12, 30, 52, 38), radius=3 * K, fill=255)
    d.polygon(p(29, 38, 35, 38, 32, 62), fill=255)
    return rotated(m, -30)


def btn_send():
    m = canvas(); d = ImageDraw.Draw(m)
    # Paper dart pointing right.
    d.polygon(p(4, 8, 62, 32, 4, 56, 14, 32), fill=255)
    d.polygon(p(14, 32, 62, 32, 24, 38), fill=0)
    return m


# group --------------------------------------------------------------------

def btn_create():
    m = canvas(); d = ImageDraw.Draw(m)
    # Banner on a pole with a plus: found a group.
    d.rectangle(p(8, 4, 14, 62), fill=255)
    d.polygon(p(14, 6, 56, 6, 48, 19, 56, 32, 14, 32), fill=255)
    plus(d, 30, 19, 8, 2.5, fill=0)
    return m


def btn_refresh():
    m = canvas(); d = ImageDraw.Draw(m)
    d.arc(p(8, 8, 56, 56), 200, 500, fill=255, width=W(8))  # 300 degrees
    arrow_head(d, (52, 14), -100, 16)
    return m


def person_with(sign):
    m = canvas(); d = ImageDraw.Draw(m)
    person(d, 24, 8, 1.15)
    d.rectangle(p(0, 58, 64, 64), fill=0)
    d.ellipse(p(34, 30, 64, 60), fill=0)
    if sign == "+":
        plus(d, 49, 45, 11, 3.5)
    else:
        d.rectangle(p(38, 41.5, 60, 48.5), fill=255)
    return m


def btn_invite():
    return person_with("+")


def btn_kick():
    return person_with("-")


def btn_leader():
    m = canvas(); d = ImageDraw.Draw(m)
    d.polygon(p(6, 18, 20, 32, 32, 10, 44, 32, 58, 18, 52, 48, 12, 48), fill=255)
    d.rounded_rectangle(p(10, 50, 54, 58), radius=2 * K, fill=255)
    for x, y in ((6, 16), (32, 8), (58, 16)):
        d.ellipse(p(x - 5, y - 5, x + 5, y + 5), fill=255)
    d.ellipse(p(28, 34, 36, 42), fill=0)
    return m


def btn_journal():
    m = canvas(); d = ImageDraw.Draw(m)
    # Open book.
    d.polygon(p(4, 12, 30, 16, 30, 56, 4, 52), fill=255)
    d.polygon(p(60, 12, 34, 16, 34, 56, 60, 52), fill=255)
    for y in (24, 32, 40):
        d.line(p(9, y, 25, y + 2), fill=0, width=W(2.5))
        d.line(p(55, y, 39, y + 2), fill=0, width=W(2.5))
    return m


def btn_topics():
    m = canvas(); d = ImageDraw.Draw(m)
    # Scroll with rolled ends.
    d.rectangle(p(14, 10, 50, 54), fill=255)
    d.rounded_rectangle(p(8, 4, 56, 14), radius=5 * K, fill=255)
    d.rounded_rectangle(p(8, 50, 56, 60), radius=5 * K, fill=255)
    for y in (22, 30, 38):
        d.rectangle(p(20, y, 44, y + 3), fill=0)
    return m


def btn_leave():
    m = canvas(); d = ImageDraw.Draw(m)
    # Door frame and an arrow walking out of it.
    d.rectangle(p(6, 4, 32, 60), fill=255)
    d.rectangle(p(12, 10, 26, 60), fill=0)
    d.rectangle(p(24, 28, 50, 36), fill=255)
    arrow_head(d, (62, 32), 0, 16)
    return m


def btn_disband():
    m = canvas(); d = ImageDraw.Draw(m)
    # A chain link broken in two.
    d.rounded_rectangle(p(2, 18, 30, 46), radius=12 * K, outline=255, width=W(7))
    d.rounded_rectangle(p(34, 18, 62, 46), radius=12 * K, outline=255, width=W(7))
    d.rectangle(p(22, 24, 42, 40), fill=0)
    for x0, y0, x1, y1 in ((28, 4, 30, 14), (36, 4, 34, 14), (28, 60, 30, 50), (36, 60, 34, 50)):
        d.line(p(x0, y0, x1, y1), fill=255, width=W(4))
    return m


def btn_accept():
    m = canvas(); d = ImageDraw.Draw(m)
    d.line(p(8, 34, 25, 51, 57, 14), fill=255, width=W(10), joint="curve")
    return m


def btn_decline():
    m = canvas(); d = ImageDraw.Draw(m)
    d.line(p(12, 12, 52, 52), fill=255, width=W(10))
    d.line(p(52, 12, 12, 52), fill=255, width=W(10))
    return m


def btn_voice():
    m = canvas(); d = ImageDraw.Draw(m)
    d.polygon(p(4, 22, 16, 22, 32, 8, 32, 56, 16, 42, 4, 42), fill=255)
    d.arc(p(28, 18, 46, 46), -55, 55, fill=255, width=W(5))
    d.arc(p(30, 6, 60, 58), -55, 55, fill=255, width=W(5))
    return m


# home ---------------------------------------------------------------------

def btn_rename():
    m = canvas(); d = ImageDraw.Draw(m)
    # Hanging signboard.
    d.rectangle(p(4, 4, 60, 9), fill=255)
    d.line(p(16, 9, 16, 22), fill=255, width=W(3))
    d.line(p(48, 9, 48, 22), fill=255, width=W(3))
    d.rounded_rectangle(p(6, 20, 58, 52), radius=5 * K, fill=255)
    d.rectangle(p(14, 30, 50, 34), fill=0)
    d.rectangle(p(14, 39, 40, 43), fill=0)
    return m


def btn_lock():
    m = canvas(); d = ImageDraw.Draw(m)
    d.arc(p(16, 4, 48, 40), 180, 360, fill=255, width=W(7))
    d.rectangle(p(16, 21, 23, 30), fill=255)
    d.rectangle(p(41, 21, 48, 30), fill=255)
    d.rounded_rectangle(p(8, 28, 56, 62), radius=6 * K, fill=255)
    d.ellipse(p(27, 36, 37, 46), fill=0)
    d.rectangle(p(30, 42, 34, 54), fill=0)
    return m


def btn_take():
    m = canvas(); d = ImageDraw.Draw(m)
    # An open box with an arrow out of it: guests may take items.
    d.polygon(p(4, 34, 60, 34, 54, 62, 10, 62), fill=255)
    d.polygon(p(4, 34, 14, 26, 20, 34), fill=255)
    d.polygon(p(60, 34, 50, 26, 44, 34), fill=255)
    d.rectangle(p(27, 16, 37, 42), fill=0)
    d.rectangle(p(29, 16, 35, 40), fill=255)
    arrow_head(d, (32, 2), -90, 15)
    return m


def btn_house():
    return home()


def btn_tavern():
    m = canvas(); d = ImageDraw.Draw(m)
    # Tankard with foam and a handle.
    d.rounded_rectangle(p(8, 16, 42, 60), radius=4 * K, fill=255)
    d.rounded_rectangle(p(38, 22, 60, 50), radius=9 * K, outline=255, width=W(6))
    for x in (13, 23, 33):
        d.ellipse(p(x - 7, 6, x + 9, 22), fill=255)
    d.rectangle(p(16, 28, 20, 52), fill=0)
    d.rectangle(p(28, 28, 32, 52), fill=0)
    return m


def btn_shop():
    m = canvas(); d = ImageDraw.Draw(m)
    # Merchant's scales.
    d.rectangle(p(29, 6, 35, 56), fill=255)
    d.rounded_rectangle(p(16, 54, 48, 61), radius=2 * K, fill=255)
    d.rectangle(p(6, 12, 58, 17), fill=255)
    d.ellipse(p(27, 2, 37, 12), fill=255)
    for cx in (12, 52):
        d.line(p(cx, 17, cx - 8, 36), fill=255, width=W(2.5))
        d.line(p(cx, 17, cx + 8, 36), fill=255, width=W(2.5))
        d.chord(p(cx - 12, 24, cx + 12, 48), 0, 180, fill=255)
    return m


def btn_guild():
    m = canvas(); d = ImageDraw.Draw(m)
    # Heraldic shield with a chevron.
    d.polygon(p(8, 6, 56, 6, 56, 30, 32, 62, 8, 30), fill=255)
    d.line(p(14, 40, 32, 22, 50, 40), fill=0, width=W(6))
    return m


def btn_coowner():
    a = key_mask(-35)
    b = key_mask(35)
    return ImageChops.lighter(a, b)


def btn_friend():
    m = canvas(); d = ImageDraw.Draw(m)
    person(d, 22, 10, 1.1)
    d.rectangle(p(0, 58, 64, 64), fill=0)
    # A heart beside the friend.
    d.ellipse(p(34, 4, 48, 18), fill=255)
    d.ellipse(p(46, 4, 60, 18), fill=255)
    d.polygon(p(35, 13, 59, 13, 47, 30), fill=255)
    return m


def btn_revoke():
    m = canvas(); d = ImageDraw.Draw(m)
    # Key under a "no entry" sign.
    k = key_mask(-40).resize((N * 3 // 4, N * 3 // 4), Image.LANCZOS)
    m.paste(k, (N // 8, N // 8))
    d.ellipse(p(2, 2, 62, 62), outline=255, width=W(6))
    d.line(p(13, 13, 51, 51), fill=0, width=W(12))
    d.line(p(12, 12, 52, 52), fill=255, width=W(6))
    return m


def btn_reset():
    m = canvas(); d = ImageDraw.Draw(m)
    # Broom: handle, binding and bristles.
    d.rectangle(p(29, 2, 35, 34), fill=255)
    d.rounded_rectangle(p(22, 30, 42, 38), radius=2 * K, fill=255)
    d.polygon(p(22, 38, 42, 38, 54, 62, 10, 62), fill=255)
    for x in (24, 32, 40):
        d.line(p(x, 44, x + (x - 32) * 0.4, 62), fill=0, width=W(2.5))
    return rotated(m, 25)


def btn_gift():
    m = canvas(); d = ImageDraw.Draw(m)
    d.rectangle(p(8, 24, 56, 34), fill=255)
    d.rectangle(p(12, 34, 52, 62), fill=255)
    d.rectangle(p(28, 24, 36, 62), fill=0)
    d.rectangle(p(30, 24, 34, 62), fill=255)
    d.ellipse(p(14, 6, 32, 24), outline=255, width=W(5))
    d.ellipse(p(32, 6, 50, 24), outline=255, width=W(5))
    return m


def btn_sell():
    m = canvas(); d = ImageDraw.Draw(m)
    # Price tag with a string hole.
    d.polygon(p(30, 4, 60, 4, 60, 34, 30, 62, 4, 36), fill=255)
    d.ellipse(p(44, 12, 54, 22), fill=0)
    d.ellipse(p(18, 26, 40, 48), outline=0, width=W(4))   # a coin on the tag
    return m


def btn_proceeds():
    m = canvas(); d = ImageDraw.Draw(m)
    # A stack of coins.
    for i, y in enumerate((44, 34, 24, 14)):
        d.ellipse(p(10, y, 54, y + 14), fill=255)
        d.ellipse(p(12, y - 2, 52, y + 10), fill=0)
        d.ellipse(p(12, y - 3, 52, y + 9), fill=255)
    d.ellipse(p(24, 12, 40, 18), fill=0)
    return m


ICONS = {
    "map_flag": map_flag,
    "inv_all": inv_all,
    "inv_weapon": inv_weapon,
    "inv_apparel": inv_apparel,
    "inv_magic": inv_magic,
    "inv_misc": inv_misc,
    "inv_keys": inv_keys,
    "menu_chat": chat,
    "menu_group": group,
    "menu_players": players,
    "menu_stats": stats,
    "menu_home": home,
    "menu_decor": decor,
}
BUTTON_ICONS = (
    "ooc", "rp", "say", "whisper", "shout", "local", "global", "plain", "me", "do", "try",
    "emoji", "color", "pin", "send",
    "create", "refresh", "invite", "kick", "leader", "journal", "topics", "leave", "disband",
    "accept", "decline", "voice",
    "rename", "lock", "take", "house", "tavern", "shop", "guild", "coowner", "friend", "revoke",
    "reset", "gift", "sell", "proceeds",
)
for _name in BUTTON_ICONS:
    ICONS["btn_" + _name] = globals()["btn_" + _name]


# ------------------------------------------------------------- rendering

def gradient():
    g = Image.new("RGB", (N, N))
    px = g.load()
    for y in range(N):
        t = y / (N - 1)
        c = tuple(int(GOLD_TOP[i] * (1 - t) + GOLD_BOTTOM[i] * t) for i in range(3))
        for x in range(N):
            px[x, y] = c
    return g


def render(mask):
    outline = mask.filter(ImageFilter.MaxFilter(2 * K + 1))
    shadow = outline.filter(ImageFilter.GaussianBlur(2 * K))
    shadow = ImageChops.offset(shadow, K, 2 * K)

    out = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    out = Image.alpha_composite(out, Image.merge("RGBA", (*Image.new("RGB", (N, N), (0, 0, 0)).split(),
                                                         shadow.point(lambda v: v * 0.55))))
    out = Image.alpha_composite(out, Image.merge("RGBA", (*Image.new("RGB", (N, N), OUTLINE).split(), outline)))
    gold = gradient()
    # Light bevel: brighten the upper-left edge of the glyph.
    bevel = ImageChops.subtract(mask, ImageChops.offset(mask, K, K)).filter(ImageFilter.GaussianBlur(K))
    gold = Image.composite(Image.new("RGB", (N, N), (255, 244, 214)), gold, bevel.point(lambda v: v * 0.7))
    out = Image.alpha_composite(out, Image.merge("RGBA", (*gold.split(), mask)))
    return out.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    for name, draw in ICONS.items():
        render(draw()).save(target / (name + ".png"), optimize=True)
        print("wrote", target / (name + ".png"))


if __name__ == "__main__":
    main()
