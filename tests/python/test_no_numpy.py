"""libstb must work, and import cleanly, in an environment without numpy.

numpy is an optional extra: only `Image.numpy()`, `__array__` and `stb.imread()` need it. Each test runs in a subprocess with numpy made
unimportable (`sys.modules["numpy"] = None` makes `import numpy` raise
ImportError), so it does not matter that the rest of the test suite imports it.
"""
import subprocess
import sys
import textwrap

import pytest


def _run(body):
    code = "import sys\nsys.modules['numpy'] = None\n" + textwrap.dedent(body)
    return subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)


def test_import_and_core_api_work_without_numpy():
    r = _run("""
        import libstb as stb
        raw = bytearray(range(24))
        img = stb.Image(memoryview(raw).cast("B", shape=[2, 4, 3]))   # any buffer, no numpy
        assert img.shape == (2, 4, 3)
        png = img.to_png()
        assert stb.Image.open(png).shape == (2, 4, 3)
        assert stb.iminfo(png) == (4, 2, stb.PixelFormat.RGB)
        assert img.resize(2, 1).shape == (1, 2, 3)
        assert len(img.to_jpg()) > 0
    """)
    assert r.returncode == 0, r.stderr


def test_pixels_without_numpy():
    r = _run("""
        import libstb as stb
        raw = bytearray(range(24))
        img = stb.Image(memoryview(raw).cast("B", shape=[2, 4, 3]))
        assert img.tobytes() == bytes(range(24))
        mv = memoryview(img)
        assert mv.shape == (2, 4, 3) and mv.tobytes() == bytes(range(24))
    """)
    assert r.returncode == 0, r.stderr


def test_numpy_access_raises_a_clear_import_error():
    r = _run("""
        import libstb as stb
        img = stb.Image(memoryview(bytearray(12)).cast("B", shape=[2, 2, 3]))
        for access in (lambda: img.numpy(), lambda: stb.imread(img.to_png())):
            try:
                access()
            except ImportError as e:
                assert "libstb[numpy]" in str(e), e
            else:
                raise SystemExit("expected ImportError")
    """)
    assert r.returncode == 0, r.stderr


def test_using_libstb_does_not_import_numpy():
    # numpy may well be installed here; the point is that libstb never pulls it in by itself.
    r = subprocess.run(
        [sys.executable, "-c",
         "import sys, libstb as stb; "
         "img = stb.Image(memoryview(bytearray(12)).cast('B', shape=[2, 2, 3])); "
         "img.to_png(); img.resize(1, 1); img.tobytes(); memoryview(img); "
         "assert 'numpy' not in sys.modules, 'libstb imported numpy eagerly'"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_buffer_view_is_zero_copy_and_keeps_the_image_alive():
    import gc

    np = pytest.importorskip("numpy")  # numpy is installed here on purpose: this one is about the buffer view

    import libstb as stb

    src = np.arange(24, dtype=np.uint8).reshape(2, 4, 3).copy()
    img = stb.Image(src)                        # shares src's pixels
    mv = memoryview(img)
    assert mv.shape == (2, 4, 3) and mv.format == "B" and not mv.readonly
    a = np.asarray(img)                         # numpy reads the buffer: still no copy
    assert np.shares_memory(a, src)

    opened = stb.Image.open(stb.Image(src).to_png())
    view = memoryview(opened)
    expected = opened.tobytes()
    del opened
    gc.collect()                                # the view alone must keep the pixels alive
    assert view.tobytes() == expected


def test_imwrite_and_iminfo_work_without_numpy(tmp_path=None):
    import tempfile, os
    d = tempfile.mkdtemp()
    r = _run(f"""
        import libstb as stb
        buf = memoryview(bytearray(range(24))).cast("B", shape=[2, 4, 3])
        stb.imwrite({d!r} + "/a.png", buf)
        stb.imwrite({d!r} + "/a.jpg", stb.Image(buf), quality=50)
        assert stb.iminfo({d!r} + "/a.png") == (4, 2, stb.PixelFormat.RGB)
    """)
    assert r.returncode == 0, r.stderr
