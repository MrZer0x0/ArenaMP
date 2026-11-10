#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ArenaMP X053 - build a colour-capable MyGUI font for the chat.

WHY THIS EXISTS

OpenMoji-color-cbdt.ttf stores its glyphs as embedded colour PNG bitmaps in
CBDT/CBLC. Two independent properties of that file make it unusable as a plain
MyGUI ResourceTrueTypeFont:

  1. MyGUI rasterises through FreeType without FT_LOAD_COLOR and builds a
     single-channel alpha texture, so it has no code path that can carry colour;
  2. the font has exactly one bitmap strike, at 99x99 px, while its `glyf`
     outlines are empty. MyGUI asks FreeType for an arbitrary pixel size (18-20
     px for chat), no strike matches, FreeType falls back to the outline, and
     the outline is blank. The result on screen is nothing at all.

MyGUI *can* however render a ResourceManualFont, which is a plain texture plus
glyph rectangles. Manual-font glyphs are drawn as textured quads modulated by
the widget's text colour, so an RGBA atlas keeps its own colours as long as the
text colour is white. That is the path this script builds.

MyGUI has no font fallback: one widget uses exactly one font. So the atlas has
to contain the Latin/Greek/Cyrillic glyphs *and* the emoji, otherwise switching
the chat to the emoji font would lose every ordinary letter. The text glyphs are
rasterised white so that the usual #RRGGBB colour tags still tint them; only the
emoji carry baked-in colour.

USAGE

    python3 tools/arenamp_make_emoji_font.py \
        --text-font files/mygui/DejaVuLGCSansMono.ttf \
        --emoji-font files/mygui/OpenMoji-color-cbdt.ttf \
        --out-dir files/mygui \
        --size 18

Writes ArenaMPChatColor.png and ArenaMPChatColor.xml next to each other, then
add both to files/mygui/CMakeLists.txt and set, in settings.cfg:

    [Chat]
    menu font = ArenaMPChatColor
    emoji font = ArenaMPChatColor
"""

import argparse
import io
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

try:
    from fontTools.ttLib import TTFont
except ImportError:
    sys.exit("fontTools is required: pip install fonttools")


# Code point ranges OpenMW's own font resource covers, so replacing the chat
# font does not silently drop characters players already type today.
TEXT_RANGES = [
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x00FF),  # Latin-1 supplement
    (0x0100, 0x017F),  # Latin Extended-A
    (0x0384, 0x03CE),  # Greek
    (0x0400, 0x045F),  # Cyrillic
    (0x0490, 0x0491),  # Ukrainian ge
    (0x2010, 0x2027),  # punctuation, dashes, ellipsis
    (0x20A0, 0x20BF),  # currency
    (0x2116, 0x2116),  # numero
]

# The twenty quick-insert slots the Player Menu offers, plus a wider set that is
# cheap to carry and lets players paste emoji from elsewhere.
PALETTE = [
    0x263A, 0x2639, 0x1F60A, 0x1F609, 0x1F60B, 0x1F602, 0x1F622, 0x1F621,
    0x2665, 0x1F494, 0x1F44D, 0x1F44E, 0x2714, 0x274C, 0x2B50, 0x2694,
    0x1F37A, 0x1F525, 0x1F3B5, 0x1F4A4,
]

EXTRA_EMOJI = [
    0x2600, 0x2601, 0x2614, 0x2615, 0x2620, 0x2622, 0x2626, 0x262E, 0x2708,
    0x2728, 0x2744, 0x2757, 0x2764, 0x2795, 0x2796, 0x2B06, 0x2B07, 0x2705,
    0x1F600, 0x1F601, 0x1F603, 0x1F604, 0x1F605, 0x1F606, 0x1F607, 0x1F608,
    0x1F60D, 0x1F60E, 0x1F610, 0x1F614, 0x1F618, 0x1F61C, 0x1F620, 0x1F624,
    0x1F62D, 0x1F631, 0x1F632, 0x1F634, 0x1F637, 0x1F642, 0x1F644, 0x1F644,
    0x1F440, 0x1F44A, 0x1F44B, 0x1F44C, 0x1F44F, 0x1F450, 0x1F64F, 0x1F4AA,
    0x1F383, 0x1F384, 0x1F389, 0x1F38A, 0x1F396, 0x1F3AF, 0x1F3C6, 0x1F3E0,
    0x1F480, 0x1F48A, 0x1F4A1, 0x1F4A5, 0x1F4A9, 0x1F4B0, 0x1F4DA, 0x1F4E6,
    0x1F511, 0x1F512, 0x1F513, 0x1F52E, 0x1F531, 0x1F52B, 0x1F5E1, 0x1F6E1,
    0x1F408, 0x1F415, 0x1F40E, 0x1F41F, 0x1F432, 0x1F43A, 0x1F576, 0x1F9D9,
    0x1F354, 0x1F355, 0x1F356, 0x1F35E, 0x1F377, 0x1F37B, 0x1F347, 0x1F34E,
]


class Packer:
    """Row packer. Glyph counts here are in the hundreds, so simple is fine."""

    def __init__(self, width, padding=1):
        self.width = width
        self.padding = padding
        self.x = padding
        self.y = padding
        self.row_height = 0

    def place(self, w, h):
        if self.x + w + self.padding > self.width:
            self.x = self.padding
            self.y += self.row_height + self.padding
            self.row_height = 0
        position = (self.x, self.y)
        self.x += w + self.padding
        self.row_height = max(self.row_height, h)
        return position

    def height(self):
        return self.y + self.row_height + self.padding


def next_power_of_two(value):
    result = 1
    while result < value:
        result *= 2
    return result


def collect_text_codepoints():
    points = []
    for first, last in TEXT_RANGES:
        points.extend(range(first, last + 1))
    return points


def load_emoji_bitmaps(path, codepoints, target):
    """Return {codepoint: RGBA image scaled to `target` px}."""
    font = TTFont(path)
    if 'CBDT' not in font:
        sys.exit("%s has no CBDT table; this script only handles CBDT/CBLC "
                 "colour fonts such as OpenMoji-color-cbdt.ttf" % path)

    cmap = font.getBestCmap()
    strike = font['CBDT'].strikeData[0]
    result = {}
    for codepoint in codepoints:
        name = cmap.get(codepoint)
        if name is None or name not in strike:
            continue
        raw = getattr(strike[name], 'imageData', None)
        if not raw:
            continue
        try:
            image = Image.open(io.BytesIO(raw)).convert('RGBA')
        except Exception:
            continue
        result[codepoint] = image.resize((target, target), Image.LANCZOS)
    return result


def build(args):
    text_points = collect_text_codepoints()
    emoji_points = []
    for codepoint in PALETTE + EXTRA_EMOJI:
        if codepoint not in emoji_points:
            emoji_points.append(codepoint)

    face = ImageFont.truetype(args.text_font, args.size)
    ascent, descent = face.getmetrics()
    line_height = ascent + descent
    emoji_size = args.emoji_size or line_height

    emoji_images = load_emoji_bitmaps(args.emoji_font, emoji_points, emoji_size)
    missing = [cp for cp in PALETTE if cp not in emoji_images]
    if missing:
        print("warning: emoji font lacks %s" % ", ".join(hex(c) for c in missing))

    # --- measure everything first, then paint into one atlas ---
    entries = []  # (codepoint_or_tag, image, advance, bearing_y)

    for codepoint in text_points:
        char = chr(codepoint)
        mask = face.getmask(char, mode='L')
        width, height = mask.size
        try:
            advance = int(round(face.getlength(char)))
        except AttributeError:
            advance = face.getsize(char)[0]
        if width == 0 or height == 0:
            entries.append((codepoint, None, max(1, advance), 0))
            continue
        bbox = face.getbbox(char)
        glyph = Image.new('RGBA', (width, height))
        # White text so the usual MyGUI colour tags still tint ordinary letters.
        glyph.putalpha(Image.frombytes('L', mask.size, bytes(mask)))
        glyph.paste((255, 255, 255), (0, 0, width, height),
                    Image.frombytes('L', mask.size, bytes(mask)))
        entries.append((codepoint, glyph, max(1, advance), bbox[1] if bbox else 0))

    for codepoint in emoji_points:
        image = emoji_images.get(codepoint)
        if image is None:
            continue
        top = max(0, (line_height - emoji_size) // 2)
        entries.append((codepoint, image, emoji_size + 1, top))

    # MyGUI's own control glyphs.
    cursor = Image.new('RGBA', (2, line_height), (255, 255, 255, 255))
    block = Image.new('RGBA', (4, line_height), (255, 255, 255, 255))
    entries.append(('cursor', cursor, 2, 0))
    entries.append(('selected', block, 4, 0))
    entries.append(('selected_back', block.copy(), 4, 0))

    substitute = Image.new('RGBA', (max(4, args.size // 2), line_height))
    ImageDraw.Draw(substitute).rectangle(
        [0, 2, substitute.width - 1, line_height - 3], outline=(255, 255, 255, 255))
    entries.append(('substitute', substitute, substitute.width + 1, 0))

    width = args.atlas_width
    packer = Packer(width)
    placed = []
    for tag, image, advance, bearing in entries:
        if image is None:
            placed.append((tag, None, advance, bearing, (0, 0)))
            continue
        position = packer.place(image.width, image.height)
        placed.append((tag, image, advance, bearing, position))

    height = next_power_of_two(packer.height())
    atlas = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    for tag, image, advance, bearing, position in placed:
        if image is not None:
            atlas.paste(image, position)

    os.makedirs(args.out_dir, exist_ok=True)
    png_path = os.path.join(args.out_dir, args.name + '.png')
    xml_path = os.path.join(args.out_dir, args.name + '.xml')
    atlas.save(png_path)

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<MyGUI type="Resource" version="1.1">',
        '    <!-- Generated by tools/arenamp_make_emoji_font.py. Do not edit by hand.',
        '         Text glyphs are white so #RRGGBB colour tags still tint them;',
        '         emoji keep their own colours because MyGUI modulates the atlas',
        '         with the widget text colour, and white leaves colour unchanged. -->',
        '    <Resource type="ResourceManualFont" name="%s">' % args.name,
        '        <Property key="Source" value="%s.png"/>' % args.name,
        '        <Property key="DefaultHeight" value="%d"/>' % line_height,
        '        <Codes>',
    ]
    for tag, image, advance, bearing, position in placed:
        if image is None:
            continue
        index = tag if isinstance(tag, str) else str(tag)
        lines.append(
            '            <Code index="%s" coord="%d %d %d %d" advance="%d" bearing="0 %d"/>'
            % (index, position[0], position[1], image.width, image.height, advance, bearing))
    lines += ['        </Codes>', '    </Resource>', '</MyGUI>', '']

    with open(xml_path, 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(lines))

    glyphs = sum(1 for entry in placed if entry[1] is not None)
    print("%s: %dx%d, %d glyphs (%d emoji), line height %d"
          % (png_path, width, height, glyphs, len(emoji_images), line_height))
    print("%s written" % xml_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--text-font', required=True,
                        help='TTF supplying Latin/Greek/Cyrillic, normally '
                             'files/mygui/DejaVuLGCSansMono.ttf')
    parser.add_argument('--emoji-font', required=True,
                        help='CBDT colour font, e.g. OpenMoji-color-cbdt.ttf')
    parser.add_argument('--out-dir', default='files/mygui')
    parser.add_argument('--name', default='ArenaMPChatColor')
    parser.add_argument('--size', type=int, default=18, help='text pixel size')
    parser.add_argument('--emoji-size', type=int, default=0,
                        help='emoji pixel size, defaults to the text line height')
    parser.add_argument('--atlas-width', type=int, default=1024)
    build(parser.parse_args())


if __name__ == '__main__':
    main()
