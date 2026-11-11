"""Font.family_name / is_bold / is_italic / Font.scan / Font.system_fonts.

libstb-info.ttc (tests/data/make_info_test_fonts.py) has four faces whose name, OS/2 and head tables
differ: regular, bold (with a non-BMP character in the family and a French id 1 that must lose),
italic (id 1 "Info Sans Light", the typographic id 16 "Info Sans" wins), bold italic (Mac Roman names
only, no OS/2 table).
"""

import pathlib

import pytest

import libstb as stb
from libstb import Font

DATA = pathlib.Path(__file__).parent.parent / "data"
INFO = DATA / "libstb-info.ttc"
GRIN = "\U0001F600"


def test_scan_gives_one_entry_per_face_of_a_collection():
    faces = Font.scan(INFO)
    assert faces == [
        stb.FontFaceInfo(0, "Info Sans", False, False, 400, 5, "Info Sans"),
        stb.FontFaceInfo(1, "Info Sans " + GRIN, True, False, 700, 4, "Info Sans " + GRIN),
        stb.FontFaceInfo(2, "Info Sans", False, True, 300, 5, "Info Sans Light"),
        stb.FontFaceInfo(3, "Caf\u00e9 Sans", True, True, 700, 5, "Caf\u00e9 Sans"),
    ]


@pytest.mark.parametrize("make", [str, pathlib.Path, lambda p: p.read_bytes(), lambda p: bytearray(p.read_bytes())])
def test_scan_takes_a_path_or_bytes(make):
    assert len(Font.scan(make(INFO))) == 4


def test_an_opened_face_agrees_with_scan():
    for info in Font.scan(INFO):
        font = Font.open(INFO, index=info.index)
        assert (font.family_name, font.legacy_family_name, font.is_bold, font.is_italic, font.weight, font.width) == (
            info.family_name, info.legacy_family_name, info.bold, info.italic, info.weight, info.width)


def test_a_woff_file_is_read_like_the_font_it_holds():
    (info,) = Font.scan(DATA / "libstb-info.woff")
    assert info == stb.FontFaceInfo(0, "Info Sans " + GRIN, True, False, 700, 4, "Info Sans " + GRIN)
    font = Font.open(DATA / "libstb-info.woff")
    assert font.family_name == info.family_name and font.is_bold and not font.is_italic


def test_scan_refuses_what_is_not_a_font():
    with pytest.raises(stb.DecodeError):
        Font.scan(b"this is not a font at all")
    with pytest.raises(OSError):
        Font.scan(DATA / "no-such-font.ttf")


def test_the_sample_fonts_report_their_weight():
    root = pathlib.Path(__file__).parent.parent.parent / "data" / "fonts"
    (regular,) = Font.scan(root / "Vazirmatn-Regular.ttf")
    (bold,) = Font.scan(root / "Vazirmatn-Bold.ttf")
    assert (regular.family_name, regular.bold, regular.italic) == ("Vazirmatn", False, False)
    assert (bold.family_name, bold.bold, bold.italic) == ("Vazirmatn", True, False)


def test_system_fonts_of_given_folders():
    found = Font.system_fonts([DATA])
    info = [f for f in found if pathlib.Path(f.path).name == "libstb-info.ttc"]
    assert [(f.index, f.family_name, f.bold, f.italic) for f in info] == [
        (i.index, i.family_name, i.bold, i.italic) for i in Font.scan(INFO)
    ]
    assert found == sorted(found, key=lambda f: (f.path, f.index))
    for f in found:
        assert Font.open(f.path, index=f.index).family_name == f.family_name
    assert Font.system_fonts([DATA / "no-such-folder"]) == []


def test_system_fonts_of_this_machine_does_not_raise():
    for d in Font.system_font_dirs():
        assert pathlib.Path(d).is_dir()
    for f in Font.system_fonts():
        assert f.index >= 0
