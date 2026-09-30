"""
runners.py: the single place that says what every (operation, library)
pair actually does.

bench_throughput.py (speed), bench_op_memory_one.py (peak memory),
bench_scaling.py (time vs. image size) and verify.py (correctness) all
build their callables here, so the four of them can never drift apart:
"parse a PNG with Pillow" means exactly one thing across the whole
suite.

The contract: prepare(op, lib, inputs) does every piece of setup that
is NOT the operation itself (importing the library, reading the file
into memory, building the library's own native image object from raw
pixels, choosing a resize target) and returns a zero-argument callable.
Only that callable is ever timed or memory-measured.

Operations, and what each one is fair to compare:

  info          Read width/height/channels from the header only, no
                pixel decoding. libstb.info vs Pillow (Image.open is
                lazy: .size and .mode come from the header). OpenCV and
                imageio have no header-only API, so they do not appear.

  decode        Encoded bytes in, uint8 pixel array out, in the file's
                own channel count. libstb.Image.open(bytes).array,
                Pillow (np.asarray of the opened image), OpenCV
                (imdecode, IMREAD_UNCHANGED), imageio (imread, with the
                extension given).

  load_file     The same, but starting from a path, so file I/O is
                included: what someone actually types.

  encode_*      Raw pixels in, the encoded file's bytes out, for png,
                jpg, bmp and tga. Settings are fixed and matched, never
                left to each library's own default (defaults differ on
                purpose: stb 8, Pillow 6, OpenCV 1 for the PNG level),
                and the output size is recorded next to the time,
                because a faster encoder that writes a bigger file is a
                trade, not a win.

  resize_*      down (half size) and up (double size), with a linear
                (triangle), cubic (Catmull-Rom) and, for down, a box
                filter. Notes that matter for reading the numbers:
                  - libstb, Pillow and scikit-image prefilter when
                    shrinking (the filter support scales with the
                    reduction). cv2.INTER_LINEAR / INTER_CUBIC do not:
                    they are faster and alias. INTER_AREA is OpenCV's
                    antialiasing counterpart and is what down_box uses.
                  - stb_image_resize2 resizes in linear light by
                    default (sRGB-correct). Pillow and OpenCV blend the
                    stored values directly. "libstb" here is
                    Resizer(filter, srgb=False), the like-for-like
                    pairing; "libstb_srgb" is the library's real
                    default, listed separately so the price of the
                    correct behaviour is visible, not hidden.
                Only 3-channel images take part: alpha resizing means
                different things in different libraries (premultiplied
                or not), which would not be a like-for-like comparison.

Everything runs single-threaded (cv2.setNumThreads(1)): libstb and
Pillow are single-threaded, and a benchmark where one contestant quietly
uses every core is measuring the machine, not the library.
"""
import io
import os

PNG_LEVEL = 6
JPEG_QUALITY = 90

# Above this many pixels scikit-image is skipped: it converts to float64
# and resizes in pure-NumPy/SciPy, so a single 2048x2048 cubic upscale
# takes many seconds and hundreds of MB. Reported as a gap, not hidden.
SKIMAGE_MAX_PIXELS = 1024 * 1024

FILTERS = ("linear", "cubic", "box")

RESIZE_OPS = {
    "resize_down_linear": ("down", "linear"),
    "resize_down_cubic": ("down", "cubic"),
    "resize_down_box": ("down", "box"),
    "resize_up_linear": ("up", "linear"),
    "resize_up_cubic": ("up", "cubic"),
}

# Ordered: this is also the order sections appear in the report.
OPS = {
    "info": ["libstb", "pillow"],
    "decode": ["libstb", "pillow", "opencv", "imageio"],
    "load_file": ["libstb", "pillow", "opencv", "imageio"],
    "encode_png": ["libstb", "pillow", "opencv", "imageio"],
    "encode_jpg": ["libstb", "pillow", "opencv", "imageio"],
    "encode_bmp": ["libstb", "pillow", "opencv", "imageio"],
    "encode_tga": ["libstb", "pillow", "opencv", "imageio"],
    **{op: ["libstb", "libstb_srgb", "pillow", "opencv", "skimage"] for op in RESIZE_OPS},
}

# These start from raw pixels, so they run once per image (on the png
# entry), not once per file format.
CANONICAL_OPS = {op for op in OPS if op.startswith("encode_") or op.startswith("resize_")}
# JPEG and BMP have no portable alpha; resize is only compared on 3-channel data.
RGB_ONLY_OPS = {"encode_jpg", "encode_bmp"} | set(RESIZE_OPS)


class Unavailable(Exception):
    """This library cannot do this operation (as opposed to: it failed)."""


def applies(op, entry):
    """Does this operation make sense on this corpus entry at all?"""
    if op in CANONICAL_OPS and not entry.get("canonical"):
        return False
    if op in RGB_ONLY_OPS and entry.get("raw_channels", entry["channels"]) != 3:
        return False
    return True


class Inputs:
    """Lazy, per-entry input data. Cheap to construct; reading happens
    on first use, always inside prepare(), never inside a timed call."""

    def __init__(self, entry):
        self.entry = entry
        self.path = entry["path"]
        self.fmt = entry["format"]
        self._data = None
        self._arr = None
        self._bgr = None

    @property
    def data(self):
        if self._data is None:
            with open(self.path, "rb") as f:
                self._data = f.read()
        return self._data

    @property
    def arr(self):
        """Raw pixels, HxWxC uint8, C-contiguous and writable (so
        libstb.Image can share them without a copy)."""
        if self._arr is None:
            import numpy as np
            raw = self.entry.get("raw")
            if not raw:
                raise Unavailable("this entry has no raw pixel file (only canonical png entries do)")
            self._arr = np.load(raw)
        return self._arr

    @property
    def bgr(self):
        """The same pixels in OpenCV's channel order (BGR / BGRA)."""
        if self._bgr is None:
            import numpy as np
            a = self.arr
            order = [2, 1, 0] if a.shape[2] == 3 else [2, 1, 0, 3]
            self._bgr = np.ascontiguousarray(a[..., order])
        return self._bgr

    @property
    def target(self):
        """(width, height) for the resize operations."""
        e = self.entry
        return e["width"], e["height"]


def _resize_target(op, inp):
    direction, _ = RESIZE_OPS[op]
    w, h = inp.target
    return (max(1, w // 2), max(1, h // 2)) if direction == "down" else (w * 2, h * 2)


# ------------------------------------------------------------- libstb --

def _libstb(op, inp, srgb=False):
    import libstb
    import numpy  # noqa: F401 - libstb imports it lazily on first .array; setup, not the operation

    if op == "info":
        data = inp.data
        return lambda: libstb.info(data)
    if op == "decode":
        data = inp.data
        return lambda: libstb.Image.open(data).array
    if op == "load_file":
        path = inp.path
        return lambda: libstb.load(path)
    if op.startswith("encode_"):
        img = libstb.Image(inp.arr)  # shares the array's pixels, no copy
        fmt = op[len("encode_"):]
        if fmt == "png":
            return lambda: img.to_png(compression=PNG_LEVEL)
        if fmt == "jpg":
            return lambda: img.to_jpg(quality=JPEG_QUALITY)
        if fmt == "bmp":
            return lambda: img.to_bmp()
        if fmt == "tga":
            return lambda: img.to_tga(rle=False)
    if op in RESIZE_OPS:
        img = libstb.Image(inp.arr)
        w, h = _resize_target(op, inp)
        _, filt = RESIZE_OPS[op]
        r = libstb.Resizer(filt, srgb=srgb)
        return lambda: img.resize(w, h, r)
    raise Unavailable(f"no libstb runner for {op}")


# ------------------------------------------------------------- Pillow --

def _pillow(op, inp):
    import numpy as np
    from PIL import Image

    if op == "info":
        data = inp.data

        def _info():
            im = Image.open(io.BytesIO(data))
            w, h = im.size
            return w, h, len(im.getbands())
        return _info
    if op == "decode":
        data = inp.data
        return lambda: np.asarray(Image.open(io.BytesIO(data)))
    if op == "load_file":
        path = inp.path

        def _load():
            with Image.open(path) as im:
                return np.asarray(im)
        return _load
    if op.startswith("encode_"):
        im = Image.fromarray(inp.arr)
        fmt = op[len("encode_"):]

        def _enc(name, **kw):
            def run():
                buf = io.BytesIO()
                im.save(buf, name, **kw)
                return buf.getvalue()
            return run
        if fmt == "png":
            return _enc("PNG", compress_level=PNG_LEVEL)
        if fmt == "jpg":
            return _enc("JPEG", quality=JPEG_QUALITY)
        if fmt == "bmp":
            return _enc("BMP")
        if fmt == "tga":
            return _enc("TGA")
    if op in RESIZE_OPS:
        im = Image.fromarray(inp.arr)
        w, h = _resize_target(op, inp)
        _, filt = RESIZE_OPS[op]
        resample = {"linear": Image.BILINEAR, "cubic": Image.BICUBIC, "box": Image.BOX}[filt]
        return lambda: im.resize((w, h), resample=resample)
    raise Unavailable(f"no Pillow runner for {op}")


# ------------------------------------------------------------- OpenCV --

def _opencv(op, inp):
    import cv2
    import numpy as np

    cv2.setNumThreads(1)

    if op == "decode":
        buf = np.frombuffer(inp.data, np.uint8)

        def _dec():
            out = cv2.imdecode(buf, cv2.IMREAD_UNCHANGED)
            if out is None:
                raise Unavailable(f"OpenCV cannot decode {inp.fmt}")
            return out
        return _dec
    if op == "load_file":
        path = inp.path

        def _load():
            out = cv2.imread(path, cv2.IMREAD_UNCHANGED)
            if out is None:
                raise Unavailable(f"OpenCV cannot read {inp.fmt}")
            return out
        return _load
    if op.startswith("encode_"):
        bgr = inp.bgr
        fmt = op[len("encode_"):]
        params = {
            "png": [cv2.IMWRITE_PNG_COMPRESSION, PNG_LEVEL],
            "jpg": [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY],
        }.get(fmt, [])

        def _enc():
            try:
                ok, out = cv2.imencode("." + fmt, bgr, params)
            except cv2.error as e:
                raise Unavailable(f"OpenCV cannot encode {fmt}") from e
            if not ok:
                raise Unavailable(f"OpenCV cannot encode {fmt}")
            return out.tobytes()
        return _enc
    if op in RESIZE_OPS:
        bgr = inp.bgr
        w, h = _resize_target(op, inp)
        direction, filt = RESIZE_OPS[op]
        interp = {
            "linear": cv2.INTER_LINEAR,
            "cubic": cv2.INTER_CUBIC,
            "box": cv2.INTER_AREA,
        }[filt]
        return lambda: cv2.resize(bgr, (w, h), interpolation=interp)
    raise Unavailable(f"OpenCV has no {op} (no header-only read)" if op == "info" else f"no OpenCV runner for {op}")


# ------------------------------------------------------------ imageio --

def _imageio(op, inp):
    import imageio.v3 as iio

    if op == "decode":
        data = inp.data
        ext = "." + inp.fmt
        return lambda: iio.imread(data, extension=ext)
    if op == "load_file":
        path = inp.path
        return lambda: iio.imread(path)
    if op.startswith("encode_"):
        arr = inp.arr
        fmt = op[len("encode_"):]
        ext = "." + fmt
        kw = {"png": {"compress_level": PNG_LEVEL}, "jpg": {"quality": JPEG_QUALITY}}.get(fmt, {})
        return lambda: iio.imwrite("<bytes>", arr, extension=ext, **kw)
    raise Unavailable(f"imageio has no {op}")


# ------------------------------------------------------- scikit-image --

def _skimage(op, inp):
    import numpy as np
    from skimage.transform import resize

    if op not in RESIZE_OPS:
        raise Unavailable("scikit-image is only benchmarked for resize")
    direction, filt = RESIZE_OPS[op]
    if filt == "box":
        raise Unavailable("scikit-image has no box filter")
    if inp.entry["pixels"] > SKIMAGE_MAX_PIXELS:
        raise Unavailable("skipped: above 1 MP scikit-image takes seconds per call")
    arr = inp.arr
    w, h = _resize_target(op, inp)
    order = {"linear": 1, "cubic": 3}[filt]
    shape = (h, w, arr.shape[2])
    aa = direction == "down"
    return lambda: resize(arr, shape, order=order, anti_aliasing=aa, preserve_range=True).astype(np.uint8)


# ------------------------------------------------------------ dispatch --

def prepare(op, lib, inp):
    """Do all setup for (op, lib) on `inp` and return the zero-argument
    callable to measure. Raises Unavailable if this library cannot do
    this operation, ImportError if it is not installed."""
    if op not in OPS:
        raise ValueError(f"unknown operation {op!r}")
    if lib == "libstb":
        return _libstb(op, inp, srgb=False)
    if lib == "libstb_srgb":
        return _libstb(op, inp, srgb=True)
    if lib == "pillow":
        return _pillow(op, inp)
    if lib == "opencv":
        return _opencv(op, inp)
    if lib == "imageio":
        return _imageio(op, inp)
    if lib == "skimage":
        return _skimage(op, inp)
    raise ValueError(f"unknown library {lib!r}")


def describe(result):
    """A small JSON-safe description of what an operation returned: its
    size in bytes for encoders, its shape for everything pixel-shaped.
    Recorded next to the time so the report can say what was produced."""
    if isinstance(result, (bytes, bytearray)):
        return {"out_bytes": len(result)}
    shape = getattr(result, "shape", None)
    if shape is not None:
        return {"out_shape": [int(s) for s in shape]}
    # libstb.Image and libstb.ImageInfo
    if hasattr(result, "width") and hasattr(result, "channels"):
        return {"out_shape": [int(result.height), int(result.width), int(result.channels)]}
    # PIL.Image.Image
    if hasattr(result, "size") and hasattr(result, "mode") and not callable(result.size):
        w, h = result.size
        return {"out_shape": [int(h), int(w), len(result.getbands())]}
    # (width, height, channels) from the Pillow info runner
    if isinstance(result, tuple) and len(result) == 3:
        w, h, c = result
        return {"out_shape": [int(h), int(w), int(c)]}
    return {}


def library_available(lib):
    """Is the library importable in this environment?"""
    mod = {"libstb": "libstb", "libstb_srgb": "libstb", "pillow": "PIL",
           "opencv": "cv2", "imageio": "imageio", "skimage": "skimage"}[lib]
    try:
        __import__(mod)
        return True
    except ImportError:
        return False


if __name__ == "__main__":
    for op, libs in OPS.items():
        print(f"{op:20s} {', '.join(libs)}")
    print(os.path.basename(__file__), "defines", len(OPS), "operations")
