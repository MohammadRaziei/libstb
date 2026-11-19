"""Font.axes / with_variation / with_weight and the weight range of Font.scan.

libstb-variable-test.ttf is a subset of Open Sans (SIL OFL 1.1, tests/data/OFL-OpenSans.txt) that is still
variable. The C++ tests compare every glyph with static instances made by fontTools; here the Python side
is checked: names, types, errors and that the weight changes what is drawn.
"""

import math
import pathlib

import pytest

import libstb as stb
from libstb import Font

DATA = pathlib.Path(__file__).parent.parent / "data"
VARIABLE = DATA / "libstb-variable-test.ttf"
STATIC = DATA / "libstb-curves-test.ttf"


def test_axes_of_a_variable_font_and_of_a_static_one():
    assert Font.open(VARIABLE).axes == [
        stb.FontAxis("wght", 300.0, 400.0, 800.0),
        stb.FontAxis("wdth", 75.0, 100.0, 100.0),
    ]
    assert Font.open(STATIC).axes == []


def test_with_weight_sets_the_weight_and_leaves_the_original_alone():
    font = Font.open(VARIABLE)
    bold = font.with_weight(700)
    assert (font.weight, bold.weight) == (400, 700)
    assert font.with_weight(5000).weight == 800  # clamped to the axis
    assert bold.with_variation("wdth", 75).weight == 700  # chaining keeps what was set


def test_a_heavier_weight_is_wider_and_draws_more_ink():
    font = Font.open(VARIABLE)
    bold = font.with_weight(800)
    h = font.glyph_index("H")
    assert bold.glyph_info(h)[0] > font.glyph_info(h)[0]  # advance
    assert bold.measure("Hamburgefonstiv", 32).width > font.measure("Hamburgefonstiv", 32).width
    ink = lambda f: sum(bytes(memoryview(f.render("Hamburgefonstiv", 32).bitmap)))  # noqa: E731
    assert ink(bold) > ink(font)


def test_scan_reports_the_weight_range():
    (info,) = Font.scan(VARIABLE)
    assert (info.weight_min, info.weight, info.weight_max) == (300, 400, 800)
    (fixed,) = Font.scan(STATIC)
    assert fixed.weight_min == fixed.weight == fixed.weight_max


def test_a_static_font_is_returned_unchanged_by_with_weight_and_refuses_with_variation():
    font = Font.open(STATIC)
    assert font.with_weight(700).weight == font.weight
    with pytest.raises(ValueError):
        font.with_variation("wght", 700)


@pytest.mark.parametrize("tag,value", [("opsz", 12.0), ("wg", 700.0), ("wght", math.nan)])
def test_bad_arguments_raise_value_error(tag, value):
    with pytest.raises(ValueError):
        Font.open(VARIABLE).with_variation(tag, value)
