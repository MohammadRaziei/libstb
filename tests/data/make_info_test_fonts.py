"""Regenerates libstb-info.ttc and libstb-info.woff (needs `pip install fonttools`).

Synthetic fonts for font::family_name / is_bold / is_italic / font::scan (issue 6 of the novasvg
list). The outlines are those of libstb-test.ttf; only the name, OS/2 and head tables differ.

  face  name table                                         OS/2 fsSelection   head.macStyle  weight / width
  0     id 1 "Info Sans", id 2 "Regular"                   regular (0x40)     0              400 / 5
  1     id 1 "Info Sans \U0001F600" (a non-BMP character),       bold (0x20)        1              700 / 4
        id 2 "Bold"; a French id 1 comes first and loses
  2     id 1 "Info Sans Light", id 16 "Info Sans"          italic (0x01)      2              300 / 5
        (the typographic family wins)
  3     Mac Roman platform only, "Caf\u00e9 Sans" (0x8E)       no OS/2 table      3 (bold italic)  none: 700 / 5
        (the flags come from head.macStyle)

libstb-info.woff is face 1 as a WOFF 1.0 file.
"""
import copy
import os

from fontTools.ttLib import TTCollection, TTFont

HERE = os.path.dirname(os.path.abspath(__file__))


def face(names, selection, mac_style, weight=400, width=5):
    font = TTFont(os.path.join(HERE, "libstb-test.ttf"))
    table = font["name"]
    table.names = []
    for (name_id, text, platform, encoding, language) in names:
        table.setName(text, name_id, platform, encoding, language)
    if selection is None:
        del font["OS/2"]
    else:
        font["OS/2"].fsSelection = selection
        font["OS/2"].usWeightClass = weight
        font["OS/2"].usWidthClass = width
    font["head"].macStyle = mac_style
    return font


WIN = (3, 1, 0x409)
faces = [
    face([(1, "Info Sans", *WIN), (2, "Regular", *WIN)], 0x40, 0, 400, 5),
    face([(1, "Info Sans FR", 3, 1, 0x040C), (1, "Info Sans \U0001F600", *WIN), (2, "Bold", *WIN)], 0x20, 1, 700, 4),
    face([(1, "Info Sans Light", *WIN), (1, "Info Sans L\u00e9ger", 3, 1, 0x040C),
          (2, "Italic", *WIN), (16, "Info Sans", *WIN), (17, "Light Italic", *WIN)], 0x01, 2, 300, 5),
    face([(1, "Caf\u00e9 Sans", 1, 0, 0), (2, "Bold Italic", 1, 0, 0)], None, 3),
]

collection = TTCollection()
collection.fonts = faces
collection.save(os.path.join(HERE, "libstb-info.ttc"))

woff = copy.deepcopy(faces[1])
woff.flavor = "woff"
woff.save(os.path.join(HERE, "libstb-info.woff"))
