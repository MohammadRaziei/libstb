"""blend (modes, opacity), fill, alpha / with_alpha, hstack / vstack and Canvas.add_image."""

import pytest

import libstb as stb

np = pytest.importorskip("numpy")


def img(*rows):
    return stb.Image(np.array(rows, dtype=np.uint8))


def solid(w, h, c, v):
    return stb.Image(np.full((h, w, c), v, np.uint8))


def px(image, x=0, y=0):
    return image.numpy()[y, x].tolist()


# ------------------------------------------------------------------------ blend


@pytest.mark.parametrize(
    "mode,base,over,want",
    [
        ("normal", 200, 77, 77),
        ("multiply", 200, 128, 100),
        ("screen", 200, 128, 228),
        ("overlay", 64, 128, 64),
        ("overlay", 191, 128, 191),
        ("darken", 200, 100, 100),
        ("lighten", 200, 100, 200),
        ("add", 200, 100, 255),
        ("difference", 100, 200, 100),
    ],
)
def test_blend_modes_give_their_formula(mode, base, over, want):
    out = solid(1, 1, 3, base).blend(solid(1, 1, 3, over), mode=mode)
    assert px(out) == [want] * 3


def test_blend_takes_the_enum_or_its_name():
    base, over = solid(1, 1, 3, 200), solid(1, 1, 3, 128)
    assert px(base.blend(over, mode=stb.BlendMode.MULTIPLY)) == px(base.blend(over, mode="multiply"))
    for bad in ("Multiply", "", "plus"):
        with pytest.raises(ValueError):
            base.blend(over, mode=bad)
    with pytest.raises(TypeError):
        base.blend(over, mode=3)


def test_blend_opacity_and_errors():
    base, over = solid(1, 1, 3, 100), solid(1, 1, 3, 200)
    assert px(base.blend(over, opacity=0.0)) == [100] * 3
    assert px(base.blend(over, opacity=0.5)) == [150] * 3
    for bad in (-0.1, 1.5, float("nan")):
        with pytest.raises(ValueError):
            base.blend(over, opacity=bad)


def test_blend_returns_a_new_image_and_leaves_both_alone():
    base, over = solid(4, 4, 3, 100), solid(2, 2, 3, 200)
    out = base.blend(over, 1, 1, mode="multiply")
    assert out is not base
    assert base.numpy().min() == 100 and over.numpy().min() == 200
    assert px(out, 0, 0) == [100] * 3


def test_blend_position_clips_at_every_edge():
    base, over = solid(3, 3, 3, 0), solid(2, 2, 3, 9)
    for x, y, covered in [(2, 2, 1), (-1, -1, 1), (3, 0, 0), (0, -2, 0), (-5, -5, 0), (1, 1, 4)]:
        assert int((base.blend(over, x, y).numpy()[..., 0] == 9).sum()) == covered


# ------------------------------------------------------------------------- fill


def test_fill_whole_image_and_rectangle():
    a = solid(4, 3, 3, 0).fill("red")
    assert (a.numpy() == [255, 0, 0]).all()
    b = solid(4, 3, 3, 0).fill((1, 2, 3), 1, 1, 2, 5)
    assert px(b, 1, 1) == [1, 2, 3] and px(b, 2, 2) == [1, 2, 3]
    assert px(b, 0, 1) == [0, 0, 0] and px(b, 3, 2) == [0, 0, 0] and px(b, 1, 0) == [0, 0, 0]
    assert int((b.numpy()[..., 0] == 1).sum()) == 4


def test_fill_stores_the_colour_in_the_images_format():
    assert px(solid(1, 1, 1, 0).fill((255, 0, 0)))[0] == 76
    assert px(solid(1, 1, 2, 0).fill((255, 0, 0, 40))) == [76, 40]
    assert px(solid(1, 1, 3, 0).fill((1, 2, 3, 40))) == [1, 2, 3]
    assert px(solid(1, 1, 4, 0).fill("#0a141e80")) == [10, 20, 30, 128]
    assert px(solid(1, 1, 3, 0).fill(7)) == [7, 7, 7]


def test_fill_leaves_the_source_alone_and_rejects_bad_arguments():
    src = solid(2, 2, 3, 5)
    out = src.fill("white")
    assert out is not src and src.numpy().max() == 5
    with pytest.raises(ValueError):
        src.fill("white", 0, 0, -1, 2)
    with pytest.raises(ValueError):
        src.fill("not a colour")
    assert (src.fill("white", 9, 9, 3, 3).numpy() == 5).all()  # entirely outside


# ------------------------------------------------------------------------ alpha


def test_alpha_and_with_alpha_round_trip():
    mask = stb.Image(np.array([[[33], [44]]], np.uint8))
    rgb = stb.Image(np.array([[[1, 2, 3], [4, 5, 6]]], np.uint8))
    rgba = rgb.with_alpha(mask)
    assert rgba.format == stb.PixelFormat.RGBA
    assert px(rgba, 1, 0) == [4, 5, 6, 44]
    assert rgba.alpha().numpy().ravel().tolist() == [33, 44]
    assert solid(1, 1, 1, 7).with_alpha(solid(1, 1, 1, 9)).format == stb.PixelFormat.GRAY_ALPHA
    again = rgba.with_alpha(solid(2, 1, 1, 255))
    assert again.format == stb.PixelFormat.RGBA and px(again, 0, 0) == [1, 2, 3, 255]


def test_alpha_errors():
    for c in (1, 3):
        with pytest.raises(ValueError):
            solid(2, 2, c, 0).alpha()
    with pytest.raises(ValueError):
        solid(2, 2, 3, 0).with_alpha(solid(2, 2, 3, 0))  # mask not gray
    with pytest.raises(ValueError):
        solid(2, 2, 3, 0).with_alpha(solid(3, 2, 1, 0))  # wrong size


def test_a_mask_shows_the_overlay_through_blend():
    base = solid(3, 1, 3, 0)
    over = solid(3, 1, 3, 200).with_alpha(stb.Image(np.array([[[0], [128], [255]]], np.uint8)))
    out = base.blend(over)
    assert px(out, 0)[0] == 0 and 99 <= px(out, 1)[0] <= 101 and px(out, 2)[0] == 200


# ----------------------------------------------------------------------- stacks


def test_hstack_and_vstack_sizes_gaps_and_background():
    a, b = solid(2, 3, 3, 10), solid(3, 1, 3, 20)
    r = stb.Image.hstack([a, b], gap=2, background="white")
    assert (r.width, r.height) == (7, 3)
    assert px(r, 2, 0) == [255] * 3 and px(r, 4, 0) == [20] * 3 and px(r, 4, 1) == [255] * 3
    c = stb.Image.vstack((a, b), gap=1, background=(0, 0, 0), align="end")
    assert (c.width, c.height) == (3, 5)
    assert px(c, 0, 3) == [0, 0, 0] and px(c, 2, 4) == [20] * 3


@pytest.mark.parametrize("where,rows", [("start", [0, 1]), ("center", [1, 2]), (stb.Align.END, [3, 4])])
def test_stack_alignment(where, rows):
    tall, short = solid(1, 5, 1, 10), solid(1, 2, 1, 99)
    r = stb.Image.hstack([tall, short], background=0, align=where)
    got = [y for y in range(5) if r.numpy()[y, 1, 0] == 99]
    assert got == rows


def test_stack_formats_and_errors():
    g, ga, rgb = solid(1, 1, 1, 50), solid(1, 1, 2, 60), solid(1, 1, 3, 70)
    assert stb.Image.hstack([g, g]).format == stb.PixelFormat.GRAY
    assert stb.Image.hstack([g, ga]).format == stb.PixelFormat.GRAY_ALPHA
    assert stb.Image.hstack([g, rgb]).format == stb.PixelFormat.RGB
    mixed = stb.Image.hstack([g, ga, rgb])
    assert mixed.format == stb.PixelFormat.RGBA and px(mixed, 0) == [50, 50, 50, 255]
    with pytest.raises(ValueError):
        stb.Image.hstack([])
    with pytest.raises(ValueError):
        stb.Image.hstack([g], gap=-1)
    with pytest.raises(ValueError):
        stb.Image.hstack([g], align="middle")
    with pytest.raises(TypeError):
        stb.Image.hstack([g, "not an image"])


# ----------------------------------------------------------------------- canvas


def test_canvas_add_image_blends_in_order_with_mode_and_opacity():
    base, logo = solid(4, 4, 3, 200), solid(2, 2, 3, 128)
    c = stb.Canvas(base)
    item = c.add_image(logo, 0, 0).blend("multiply")
    assert isinstance(item, stb.CanvasImage)
    c.add_image(logo, 2, 2).opacity(0.5)
    c.add_image(solid(2, 2, 3, 255), -1, -1)
    out = c.finish()
    assert px(out, 1, 1)[0] == 100 and px(out, 3, 3)[0] == 164
    assert px(out, 0, 0)[0] == 255 and px(out, 3, 0)[0] == 200
    assert base.numpy().max() == 200


def test_canvas_image_errors_and_finished_canvas():
    c = stb.Canvas(solid(3, 3, 3, 0))
    item = c.add_image(solid(1, 1, 3, 77), 1, 1)
    with pytest.raises(ValueError):
        item.opacity(2.0)
    with pytest.raises(ValueError):
        item.blend("nope")
    out = c.finish()
    assert px(out, 1, 1)[0] == 77
    with pytest.raises(ValueError):
        item.blend("screen")
    with pytest.raises(ValueError):
        c.add_image(solid(1, 1, 3, 1))
