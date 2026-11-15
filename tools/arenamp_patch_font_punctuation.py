#!/usr/bin/env python3
"""Add ASCII fallback glyphs for punctuation missing from the shipped Pelagiad.
Requires fontTools. Run from the repository root; existing glyphs are preserved.
"""
from pathlib import Path
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
aliases = {0x00A0: 32, 0x00AB: 34, 0x00BB: 34, 0x201A: 39,
           0x201B: 39, 0x201E: 34, 0x201F: 34, 0x202F: 32,
           0x2039: 39, 0x203A: 39, 0x2212: 45}
for path in (root / 'files/mygui/Pelagiad.ttf', root / 'android/app/src/main/res/font/pelagiad.ttf'):
    font = TTFont(path, recalcTimestamp=False)
    for table in font['cmap'].tables:
        if table.isUnicode():
            for missing, fallback in aliases.items():
                if missing not in table.cmap and fallback in table.cmap:
                    table.cmap[missing] = table.cmap[fallback]
    font.save(path)
    print(path)
