#!/usr/bin/env python3
"""ArenaMP UI8: build files/mygui/arenamp_modern.skin.xml from the classic
skin files.

The modern skin is an *override* file. WindowManager loads it after core.xml
when [GUI] arenamp modern skin = true; MyGUI replaces resources that share a
name, so every window, list, button, scrollbar, slider, edit box and progress
bar picks up the new look without touching any layout. With the setting off,
the classic Morrowind skin stays exactly as it was.

What is overridden:
  * every skin piece that uses a Morrowind menu texture (window and dialog
    borders, thin box borders, caption blocks, button frames, bar tracks,
    scrollbar thumbs and arrows): same resource, same geometry, new texture
    from textures/ui/arenamw/skin (tools/arenamp_make_ui_skin.py);
  * MW_Button / MW_Button_RightPadding: a dark face with hover, pressed and
    selected states behind the bronze frame;
  * MW_TextEdit / MW_TextBoxEditWithBorder: a dark inset behind the text.

Run from the repository root after changing any classic skin file:
    python3 tools/arenamp_make_modern_skin.py
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MYGUI = ROOT / "files/mygui"
OUT = MYGUI / "arenamp_modern.skin.xml"
SKIN = "textures\\ui\\arenamw\\skin\\"

SOURCES = ["openmw_windows.skin.xml", "openmw_box.skin.xml", "openmw_button.skin.xml",
           "openmw_list.skin.xml", "openmw_progress.skin.xml", "openmw_hud_energybar.skin.xml",
           "openmw_edit.skin.xml"]

TEXTURES = {
    "menu_thick_border_top": "thick_top", "menu_thick_border_bottom": "thick_bottom",
    "menu_thick_border_left": "thick_left", "menu_thick_border_right": "thick_right",
    "menu_thick_border_top_left_corner": "thick_tl", "menu_thick_border_top_right_corner": "thick_tr",
    "menu_thick_border_bottom_left_corner": "thick_bl", "menu_thick_border_bottom_right_corner": "thick_br",
    "menu_thin_border_top": "thin_top", "menu_thin_border_bottom": "thin_bottom",
    "menu_thin_border_left": "thin_left", "menu_thin_border_right": "thin_right",
    "menu_thin_border_top_left_corner": "thin_tl", "menu_thin_border_top_right_corner": "thin_tr",
    "menu_thin_border_bottom_left_corner": "thin_bl", "menu_thin_border_bottom_right_corner": "thin_br",
    "menu_head_block_middle": "caption_mid", "menu_head_block_left": "caption_side",
    "menu_head_block_right": "caption_side", "menu_head_block_top": "clear",
    "menu_head_block_bottom": "clear", "menu_head_block_top_left_corner": "clear",
    "menu_head_block_top_right_corner": "clear", "menu_head_block_bottom_left_corner": "clear",
    "menu_head_block_bottom_right_corner": "clear",
    "menu_button_frame_top": "btn_top", "menu_button_frame_bottom": "btn_bottom",
    "menu_button_frame_left": "btn_left", "menu_button_frame_right": "btn_right",
    "menu_button_frame_top_left_corner": "btn_tl", "menu_button_frame_top_right_corner": "btn_tr",
    "menu_button_frame_bottom_left_corner": "btn_bl", "menu_button_frame_bottom_right_corner": "btn_br",
    "menu_bar_gray": "bar_gray",
    "omw_menu_scroll_center_v": "thumb_v", "omw_menu_scroll_center_h": "thumb_h",
    "omw_menu_scroll_up": "arrow_up", "omw_menu_scroll_down": "arrow_down",
    "omw_menu_scroll_left": "arrow_left", "omw_menu_scroll_right": "arrow_right",
}

RESOURCE = re.compile(r'(?P<indent>[ \t]*)<Resource type="(?P<type>\w+)" name="(?P<name>[^"]+)"(?P<attrs>[^>]*)>(?P<body>.*?)</Resource>', re.S)
TEXTURE = re.compile(r'texture="textures\\(?P<tex>[a-z_]+)\.dds"')

# Button face states: rows of button_face.png (see arenamp_make_ui_skin.py).
FACE_STATES = {"disabled": 5, "normal": 0, "highlighted": 1, "pushed": 2, "disabled_checked": 5,
               "normal_checked": 3, "highlighted_checked": 4, "pushed_checked": 2}


def face_basis(width, height):
    states = "\n".join('            <State name="%s" offset="0 %d 16 24"/>' % (name, row * 32)
                       for name, row in FACE_STATES.items())
    return ('        <!-- UI8: dark face with hover / pressed / selected states. -->\n'
            '        <BasisSkin type="SubSkin" offset="1 1 %d %d" align="Stretch">\n%s\n        </BasisSkin>\n'
            % (width - 2, height - 2, states))


ORNAMENTED = {"MW_Window": (256, 256), "MW_Window_NoCaption": (256, 256), "MW_Window_Pinnable": (256, 256),
              "MW_Dialog": (256, 54), "MW_DialogNoTransp": (256, 54)}
TRACK_FILLS = {"MW_Track_Red": "fill_health", "MW_Track_Blue": "fill_magicka", "MW_Track_Green": "fill_stamina"}
PROGRESS = {"MW_Progress_Red": "MW_Track_Red", "MW_Progress_Blue": "MW_Track_Blue",
            "MW_Progress_Green": "MW_Track_Green"}
HUD = "textures\\ui\\arenamw\\hud\\"


def ornaments(width, height, top_right=True):
    c = 24
    text = ('        <!-- UI9: gold filigree in the corners (no mouse input). -->\n'
            '        <Child type="Widget" skin="ArenaUI_CornerTL" offset="0 0 %d %d" align="Left Top"/>\n' % (c, c))
    if top_right:
        text += '        <Child type="Widget" skin="ArenaUI_CornerTR" offset="%d 0 %d %d" align="Right Top"/>\n' % (width - c, c, c)
    text += ('        <Child type="Widget" skin="ArenaUI_CornerBL" offset="0 %d %d %d" align="Left Bottom"/>\n'
             '        <Child type="Widget" skin="ArenaUI_CornerBR" offset="%d %d %d %d" align="Right Bottom"/>\n'
             % (height - c, c, c, width - c, height - c, c, c))
    return text


def progress_bar(name, track):
    # The Client keeps the classic 2 px inset: layouts put caption TextBoxes
    # *inside* these bars (stats window HBarT/MBarT/FBarT, ...), and MyGUI
    # parents such children to the Client. The bronze ends are squeezed into
    # 3 px so the fill still reaches the frame.
    return ('    <!-- UI9: HUD-style frame (dark glass, bronze ends) for every classic\n'
            '         progress bar: stats, encumbrance, charge, chargen. Classic\n'
            '         Client geometry, so captions inside the bars stay visible. -->\n'
            '    <Resource type="ResourceSkin" name="%s" size="64 12" texture="%sbar_back.png">\n'
            '        <Property key="TrackSkin" value="%s"/>\n'
            '        <Property key="TrackFill" value="1"/>\n'
            '        <BasisSkin type="SubSkin" offset="0 0 3 12" align="Left VStretch">\n'
            '            <State name="normal" offset="0 0 10 16"/>\n'
            '        </BasisSkin>\n'
            '        <BasisSkin type="SubSkin" offset="3 0 58 12" align="HStretch VStretch">\n'
            '            <State name="normal" offset="10 0 44 16"/>\n'
            '        </BasisSkin>\n'
            '        <BasisSkin type="SubSkin" offset="61 0 3 12" align="Right VStretch">\n'
            '            <State name="normal" offset="54 0 10 16"/>\n'
            '        </BasisSkin>\n'
            '        <Child type="Widget" skin="" offset="2 2 60 8" align="Stretch" name="Client"/>\n'
            '    </Resource>' % (name, HUD, track))


def track_fill(name, fill):
    return ('    <Resource type="ResourceSkin" name="%s" size="8 16" texture="%s%s.png">\n'
            '        <BasisSkin type="MainSkin" offset="0 0 8 16" align="Stretch">\n'
            '            <State name="normal" offset="0 0 8 16"/>\n'
            '        </BasisSkin>\n'
            '    </Resource>' % (name, HUD, fill))


def rewrite(match):
    text = match.group(0)
    name = match.group("name")
    if name in ORNAMENTED:
        width, height = ORNAMENTED[name]
        cut = text.rindex("</Resource>")
        # The pinnable window keeps its top-right corner for the pin button.
        return text[:cut] + ornaments(width, height, name != "MW_Window_Pinnable") + "    " + text[cut:]
    if name in TRACK_FILLS:
        return track_fill(name, TRACK_FILLS[name]).lstrip()
    if name in PROGRESS:
        return progress_bar(name, PROGRESS[name]).lstrip()
    if name in ("MW_Button", "MW_Button_RightPadding"):
        size = re.search(r'size="(\d+) (\d+)"', match.group("attrs"))
        width = 136  # both skins draw the frame in the first 136 px
        height = int(size.group(2))
        text = text.replace('<Resource type="ResourceSkin" name="%s"' % name,
                            '<Resource type="ResourceSkin" name="%s" texture="%sbutton_face.png"' % (name, SKIN), 1)
        text = text.replace('        <BasisSkin type="SimpleText"', face_basis(width, height) + '        <BasisSkin type="SimpleText"', 1)
        return text
    if name in ("MW_TextEdit", "MW_TextBoxEditWithBorder"):
        return text.replace('        <Child type="Widget" skin="MW_Box"',
                            '        <!-- UI8: dark inset behind the text. -->\n'
                            '        <Child type="Widget" skin="ArenaUI_Inset" offset="0 0 512 20" align="Stretch"/>\n'
                            '        <Child type="Widget" skin="MW_Box"', 1)

    def tex(m):
        mapped = TEXTURES.get(m.group("tex"))
        return 'texture="%s%s.png"' % (SKIN, mapped) if mapped else m.group(0)
    return TEXTURE.sub(tex, text)


def main():
    blocks = []
    for source in SOURCES:
        content = (MYGUI / source).read_text(encoding="utf-8")
        picked = []
        for match in RESOURCE.finditer(content):
            name = match.group("name")
            uses_menu_texture = any(t in match.group(0) for t in TEXTURES)
            if uses_menu_texture or name in ("MW_Button", "MW_Button_RightPadding", "MW_TextEdit",
                                             "MW_TextBoxEditWithBorder") \
                    or name in ORNAMENTED or name in PROGRESS:
                picked.append(match.group("indent") + rewrite(match).lstrip())
        if picked:
            blocks.append("    <!-- from %s -->\n%s" % (source, "\n".join(picked)))

    header = ('<?xml version="1.0" encoding="UTF-8"?>\n'
              '<!-- ArenaMP UI8: modern (TESO-like) skin overrides. GENERATED by\n'
              '     tools/arenamp_make_modern_skin.py from the classic skin files; do not\n'
              '     edit by hand. Loaded after core.xml only when\n'
              '     [GUI] arenamp modern skin = true; resources with the same name replace\n'
              '     the classic ones, so layouts and widget geometry are unchanged. -->\n'
              '<MyGUI type="Resource" version="1.1">\n'
              '    <Resource type="ResourceSkin" name="ArenaUI_Inset" size="8 8" texture="%sinset.png">\n'
              '        <Property key="NeedMouse" value="false"/>\n'
              '        <BasisSkin type="MainSkin" offset="0 0 8 8" align="Stretch">\n'
              '            <State name="normal" offset="0 0 8 8"/>\n'
              '        </BasisSkin>\n'
              '    </Resource>\n\n' % SKIN)
    for corner in ("TL", "TR", "BL", "BR"):
        header += ('    <Resource type="ResourceSkin" name="ArenaUI_Corner%s" size="24 24" texture="%scorner_%s.png">\n'
                   '        <Property key="NeedMouse" value="false"/>\n'
                   '        <BasisSkin type="MainSkin" offset="0 0 24 24" align="Stretch">\n'
                   '            <State name="normal" offset="0 0 24 24"/>\n'
                   '        </BasisSkin>\n'
                   '    </Resource>\n' % (corner, SKIN, corner.lower()))
    # UI9: window pin (the "fix this window" button of map/inventory/stats/magic).
    for name, tex in (("PinUp", "pin_up"), ("PinDown", "pin_down")):
        header += ('    <Resource type="ResourceSkin" name="%s" size="19 19" texture="%s%s.png">\n'
                   '        <BasisSkin type="MainSkin" offset="0 0 19 19" align="Stretch">\n'
                   '            <State name="normal" offset="0 0 19 19"/>\n'
                   '        </BasisSkin>\n'
                   '    </Resource>\n' % (name, SKIN, tex))
    header += "\n"
    OUT.write_text(header + "\n\n".join(blocks) + "\n</MyGUI>\n", encoding="utf-8")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
