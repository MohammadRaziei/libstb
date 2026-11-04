"""Matrix, Path, Image.fill_path / stroke_path, Path.fill_mask / stroke_mask and Canvas.add_path."""

import pytest

import libstb as stb

np = pytest.importorskip("numpy")


def white(w, h):
    return stb.Image(np.full((h, w, 4), 255, np.uint8))


def px(image, x, y):
    return image.numpy()[y, x].tolist()


# ----------------------------------------------------------------------- Matrix


def test_matrix_default_is_identity_and_maps_points():
    m = stb.Matrix()
    assert m.is_identity
    assert m.map(3, -4) == (3, -4)
    assert tuple(stb.Matrix(2, 3, 5, 7, 11, 13)) == (2, 3, 5, 7, 11, 13)


def test_matrix_builder_calls_apply_to_the_point_first_and_chain():
    m = stb.Matrix().translate(10, 0).scale(2)
    assert m.map(1, 1) == pytest.approx((12, 2))
    assert m == stb.Matrix.scaling(2).then(stb.Matrix.translation(10, 0))


def test_matrix_inverse_and_singular():
    m = stb.Matrix().translate(5, -3).rotate(0.4).scale(2, 3)
    x, y = m.inverse().map(*m.map(7, 9))
    assert (x, y) == pytest.approx((7, 9), abs=1e-4)
    assert stb.Matrix.scaling(0, 1).inverse() is None


def test_matrix_rotation_turns_x_towards_y():
    x, y = stb.Matrix.rotation(1.57079632679).map(1, 0)
    assert (x, y) == pytest.approx((0, 1), abs=1e-5)


# ------------------------------------------------------------------------- Path


def test_path_builder_chains_and_reports_commands():
    p = stb.Path().move_to(1, 2).line_to(3, 4).quad_to(5, 6, 7, 8).cubic_to(1, 1, 2, 2, 3, 3).close()
    assert len(p) == 5
    assert p.commands == [("M", 1, 2), ("L", 3, 4), ("Q", 5, 6, 7, 8), ("C", 1, 1, 2, 2, 3, 3), ("Z",)]
    assert stb.Path(p.commands) == p


def test_path_commands_are_validated():
    with pytest.raises(ValueError):
        stb.Path([("X", 1, 2)])
    with pytest.raises(ValueError):
        stb.Path([("M", 1)])
    with pytest.raises(ValueError):
        stb.Path([()])


def test_path_empty_state_and_bounds():
    p = stb.Path()
    assert len(p) == 0 and p.bounds is None and p.current_point is None
    p.add_rect(10, 20, 30, 40)
    assert p.bounds == (10, 20, 40, 60)
    assert p.current_point == (10, 20)  # after close: where the contour started


def test_path_shapes_and_add_path_and_transform():
    a = stb.Path().add_rect(0, 0, 10, 10)
    b = stb.Path().add_path(a, stb.Matrix.translation(100, 50))
    assert b.bounds == (100, 50, 110, 60)
    assert a.transformed(stb.Matrix.translation(100, 50)) == b
    a.transform(stb.Matrix.translation(100, 50))
    assert a == b
    assert stb.Path().add_rounded_rect(0, 0, 20, 10, 100) == stb.Path().add_rounded_rect(0, 0, 20, 10, 10, 5)
    assert stb.Path().add_rect(0, 0, 0, 5).bounds is None


def test_path_dashed():
    p = stb.Path().move_to(0, 0).line_to(20, 0)
    d = p.dashed([5, 5])
    assert d.commands[0] == ("M", 0, 0) and d.commands[1] == ("L", 5, 0)
    assert p.dashed([]) == p and p.dashed([0, 0]) == p and p.dashed([-1, 2]) == p


# ----------------------------------------------------------------- drawing on an Image


def test_fill_path_returns_a_copy_with_the_inside_filled():
    img = white(20, 20)
    out = img.fill_path(stb.Path().add_rect(5, 5, 10, 10), (255, 0, 0))
    assert px(out, 10, 10) == [255, 0, 0, 255]
    assert px(out, 2, 2) == [255, 255, 255, 255]
    assert px(img, 10, 10) == [255, 255, 255, 255]  # the original is untouched


def test_fill_path_rule_matrix_blend_and_opacity():
    p = stb.Path().add_rect(2, 2, 20, 20).add_rect(8, 8, 8, 8)
    img = white(24, 24)
    assert px(img.fill_path(p, "black"), 12, 12) == [0, 0, 0, 255]
    assert px(img.fill_path(p, "black", rule="even_odd"), 12, 12) == [255, 255, 255, 255]

    moved = img.fill_path(stb.Path().add_rect(0, 0, 4, 4), (255, 128, 0), matrix=stb.Matrix.translation(8, 8), blend="multiply")
    assert px(moved, 9, 9) == [255, 128, 0, 255]
    assert px(moved, 2, 2) == [255, 255, 255, 255]

    half = img.fill_path(stb.Path().add_rect(0, 0, 24, 24), "black", opacity=0.5)
    assert 126 <= px(half, 5, 5)[0] <= 129


def test_stroke_path_draws_the_outline_only():
    out = white(30, 30).stroke_path(stb.Path().add_rect(5, 5, 20, 20), (0, 0, 255), 3)
    assert px(out, 15, 5) == [0, 0, 255, 255]
    assert px(out, 15, 15) == [255, 255, 255, 255]


def test_stroke_path_dashes_caps_and_joins():
    line = stb.Path().move_to(0, 5).line_to(24, 5)
    out = white(24, 10).stroke_path(line, "black", 2, dashes=[4, 4])
    assert px(out, 1, 4) == [0, 0, 0, 255] and px(out, 5, 4) == [255, 255, 255, 255]
    capped = white(30, 10).stroke_path(stb.Path().move_to(10, 5).line_to(20, 5), "black", 4, cap="square")
    assert px(capped, 8, 5) == [0, 0, 0, 255]
    stb_path = stb.Path().add_rect(10, 10, 20, 20)
    mitre = white(40, 40).stroke_path(stb_path, "black", 4, join="miter")
    bevel = white(40, 40).stroke_path(stb_path, "black", 4, join="bevel")
    assert px(mitre, 8, 8)[0] == 0 and px(bevel, 8, 8)[0] > 0


@pytest.mark.parametrize(
    "kwargs",
    [dict(rule="nope"), dict(blend="nope"), dict(opacity=2.0), dict(opacity=-1.0)],
)
def test_fill_path_rejects_bad_arguments(kwargs):
    with pytest.raises(ValueError):
        white(8, 8).fill_path(stb.Path().add_rect(0, 0, 4, 4), "red", **kwargs)


@pytest.mark.parametrize(
    "kwargs",
    [dict(width=0), dict(width=-1), dict(cap="nope"), dict(join="nope"), dict(miter_limit=0.5), dict(opacity=3.0)],
)
def test_stroke_path_rejects_bad_arguments(kwargs):
    kwargs.setdefault("width", 2)
    with pytest.raises(ValueError):
        white(8, 8).stroke_path(stb.Path().add_rect(0, 0, 4, 4), "red", **kwargs)


# ------------------------------------------------------------------------- masks


def test_fill_mask_is_the_coverage_as_a_gray_image():
    m = stb.Path().add_rect(2.5, 2, 4, 3).fill_mask(10, 8)
    a = m.numpy().reshape(8, 10)
    assert a[3, 2] == 128 and a[3, 3] == 255 and a[3, 6] == 128 and a[3, 7] == 0 and a[0, 0] == 0
    assert m.channels == 1


def test_stroke_mask_and_arguments():
    p = stb.Path().move_to(2, 10).line_to(18, 10)
    a = p.stroke_mask(20, 20, line_width=4).numpy().reshape(20, 20)
    assert a[9, 5] == 255 and a[7, 5] == 0 and a[10, 1] == 0
    with pytest.raises(ValueError):
        p.stroke_mask(20, 20, line_width=0)
    with pytest.raises(ValueError):
        p.fill_mask(0, 5)


# ------------------------------------------------------------------------ Canvas


def test_canvas_add_path_matches_fill_path_and_stroke_path():
    p = stb.Path().add_rect(6, 6, 18, 18)
    c = stb.Canvas(white(30, 30))
    c.add_path(p).fill((0, 255, 0)).stroke(4, (0, 0, 255)).join("round")
    out = c.finish()
    expected = white(30, 30).fill_path(p, (0, 255, 0)).stroke_path(p, (0, 0, 255), 4, join="round")
    assert np.array_equal(out.numpy(), expected.numpy())


def test_canvas_add_path_default_colour_no_fill_and_dash():
    c = stb.Canvas(white(30, 30))
    c.set_color((200, 0, 0))
    c.add_path(stb.Path().add_rect(0, 0, 10, 10))
    item = c.add_path(stb.Path().move_to(0, 0).line_to(20, 0)).no_fill().stroke(2).dash([4, 4]).transform(stb.Matrix.translation(5, 15))
    assert isinstance(item, stb.CanvasPath)
    out = c.finish()
    assert px(out, 5, 5) == [200, 0, 0, 255]
    assert px(out, 6, 15) == [200, 0, 0, 255]
    assert px(out, 11, 15) == [255, 255, 255, 255]


def test_canvas_path_checks_its_arguments_and_the_canvas_state():
    c = stb.Canvas(white(8, 8))
    item = c.add_path(stb.Path().add_rect(0, 0, 4, 4))
    with pytest.raises(ValueError):
        item.stroke(0)
    with pytest.raises(ValueError):
        item.miter_limit(0.5)
    with pytest.raises(ValueError):
        item.opacity(2)
    with pytest.raises(ValueError):
        item.rule("nope")
    c.finish()
    with pytest.raises(ValueError):
        item.fill("red")
    with pytest.raises(ValueError):
        c.add_path(stb.Path())
