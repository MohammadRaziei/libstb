"""CSS colours, Porter-Duff operators, Matrix.map_rect / map_points, Outline.arc_to / tight_bounds, exit cleanliness."""

import subprocess
import sys
import textwrap

import pytest

import libstb as stb

np = pytest.importorskip("numpy")


def px(r, g, b, a):
    return stb.Image(np.array([[[r, g, b, a]]], dtype=np.uint8))


# ------------------------------------------------------------------------------------------ CSS colours


def test_css_keywords_match_pillow():
    ImageColor = pytest.importorskip("PIL.ImageColor")
    from PIL import ImageColor as ic

    names = [n for n in ic.colormap if n.isalpha() and not n.startswith("#")]
    assert len(names) >= 148
    for name in names:
        want = ic.getrgb(name)[:3]
        img = stb.Image(np.zeros((1, 1, 3), np.uint8)).fill(name)
        assert tuple(int(v) for v in np.asarray(img)[0, 0]) == tuple(want), name


@pytest.mark.parametrize(
    "text, want",
    [
        ("rgb(255, 128, 0)", (255, 128, 0, 255)),
        ("rgb(255 128 0 / 50%)", (255, 128, 0, 128)),
        ("rgba(0, 0, 255, 0.25)", (0, 0, 255, 64)),
        ("hsl(120, 100%, 25%)", (0, 128, 0, 255)),
        ("hsl(0.5turn 100% 50%)", (0, 255, 255, 255)),
        ("hsl(90, 100%, 76.2745098039%)", (195, 255, 134, 255)),
        ("transparent", (0, 0, 0, 0)),
    ],
)
def test_css_functions_as_a_colour_argument(text, want):
    img = stb.Image(np.zeros((1, 1, 4), np.uint8)).fill(text)
    assert tuple(int(v) for v in np.asarray(img)[0, 0]) == want


@pytest.mark.parametrize("text", ["rgb(1,2)", "rgb(1 2,3)", "foo(1,2,3)", "rgb(1,2,3)x"])
def test_bad_css_functions_raise_value_error(text):
    with pytest.raises(ValueError):
        stb.Image(np.zeros((1, 1, 3), np.uint8)).fill(text)


# ------------------------------------------------------------------------------------ blend / composite


def test_exclusion_mode():
    base = stb.Image(np.array([[[200, 100, 100]]], dtype=np.uint8))
    over = stb.Image(np.array([[[128, 0, 0]]], dtype=np.uint8))
    assert int(np.asarray(base.blend(over, mode="exclusion"))[0, 0, 0]) == 127
    assert int(np.asarray(base.blend(over, mode=stb.BlendMode.EXCLUSION))[0, 0, 0]) == 127


@pytest.mark.parametrize(
    "op, want",
    [
        ("clear", (0, 0, 0, 0)),
        ("source", (10, 20, 30, 204)),
        ("destination", (200, 100, 50, 128)),
        ("source_in", (10, 20, 30, 102)),
        ("destination_out", (200, 100, 50, 26)),
        (stb.CompositeOp.SOURCE_OUT, (10, 20, 30, 102)),
    ],
)
def test_composite_operators(op, want):
    out = px(200, 100, 50, 128).blend(px(10, 20, 30, 204), op=op)
    assert tuple(int(v) for v in np.asarray(out)[0, 0]) == want


def test_composite_default_is_source_over_and_names_are_checked():
    a, b = px(200, 100, 50, 128), px(10, 20, 30, 204)
    assert np.array_equal(np.asarray(a.blend(b)), np.asarray(a.blend(b, op="source_over")))
    with pytest.raises(ValueError):
        a.blend(b, op="xor")
    with pytest.raises(TypeError):
        a.blend(b, op=3)


def test_canvas_image_composite():
    c = stb.Canvas(solid4(4, 1, 200))
    c.add_image(solid4(2, 1, 50), 1, 0).composite("clear")
    out = np.asarray(c.finish())
    assert out[0, :, 3].tolist() == [200, 0, 0, 200]


def solid4(w, h, v):
    return stb.Image(np.full((h, w, 4), v, np.uint8))


def test_fill_outline_with_clear_cuts_a_hole_by_coverage():
    base = solid4(20, 20, 255)
    circle = stb.Outline().add_circle(10, 10, 6)
    out = np.asarray(base.fill_outline(circle, (0, 0, 0), composite="clear"))
    assert out[10, 10, 3] == 0 and out[0, 0, 3] == 255 and out[19, 19, 3] == 255
    assert ((out[..., 3] > 0) & (out[..., 3] < 255)).sum() > 8
    plain = np.asarray(base.fill_outline(circle, (0, 0, 0)))
    assert plain[10, 10, 3] == 255            # the default is still "over"
    with pytest.raises(ValueError):
        base.fill_outline(circle, (0, 0, 0), composite="xor")


def test_canvas_outline_composite():
    c = stb.Canvas(solid4(20, 20, 255))
    c.add_outline(stb.Outline().add_circle(10, 10, 6)).fill((0, 0, 0)).composite("clear")
    out = np.asarray(c.finish())
    assert out[10, 10, 3] == 0 and out[0, 0, 3] == 255


# -------------------------------------------------------------------------------------------- geometry


def test_matrix_map_rect_and_points():
    assert stb.Matrix().scale(2, 3).map_rect(1, 2, 4, 6) == (2, 6, 8, 18)
    m = stb.Matrix().translate(3, 4).scale(2)
    pts = [(0.0, 0.0), (1.5, -2.0), (10.0, 10.0)]
    assert m.map_points(pts) == [m.map(x, y) for x, y in pts]
    assert m.map_points([]) == []


def test_arc_to_and_tight_bounds():
    o = stb.Outline().move_to(-1, 0).arc_to(1, 1, 0, False, True, 1, 0)
    x0, y0, x1, y1 = o.tight_bounds
    assert (x0, y0, x1, y1) == pytest.approx((-1, -1, 1, 0), abs=1e-4)
    assert o.current_point == (1, 0)
    assert stb.Outline().tight_bounds is None

    q = stb.Outline().move_to(0, 0).quad_to(50, 100, 100, 0)
    assert q.bounds == (0, 0, 100, 100)
    assert q.tight_bounds == pytest.approx((0, 0, 100, 50), abs=1e-4)

    flat = stb.Outline().move_to(0, 0).arc_to(0, 5, 0, False, True, 4, 4)
    assert flat.commands[-1] == ("L", 4, 4)
    same = stb.Outline().move_to(1, 1).arc_to(3, 3, 0, True, True, 1, 1)
    assert len(same.commands) == 1


# --------------------------------------------------------------------------------------- clean exit


def run(code):
    return subprocess.run([sys.executable, "-c", textwrap.dedent(code)], capture_output=True, text=True, timeout=120)


def test_exit_with_images_logging_bridge_and_dlpack_prints_no_leak_report():
    r = run(
        """
        import logging, numpy as np, libstb as stb
        class H(logging.Handler):
            def __init__(self, im):
                super().__init__(); self.im = im
            def emit(self, record): pass
        img = stb.Image(np.zeros((8, 8, 3), np.uint8))
        shared = np.from_dlpack(img)
        back = stb.Image.from_dlpack(shared)
        logging.getLogger("libstb").addHandler(H(img))
        logging.getLogger("libstb").warning("hello from the test")
        class N: pass
        n = N(); n.img = img; n.me = n
        """
    )
    assert r.returncode == 0, r.stderr
    assert "leaked" not in r.stderr, r.stderr


def test_logging_after_the_exit_hook_is_harmless():
    r = run(
        """
        import atexit, logging, libstb as stb
        stb.logging_bridge(True)
        atexit.register(lambda: logging.getLogger("libstb").warning("late"))   # runs BEFORE libstb's hook (LIFO)
        """
    )
    assert r.returncode == 0, r.stderr
    assert "leaked" not in r.stderr
