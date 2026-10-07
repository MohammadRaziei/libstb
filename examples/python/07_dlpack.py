"""DLPack: hand pixels to numpy, jax, torch or cupy and back, without copying.

numpy is enough to run this. jax is used too if it is installed (pip install jax);
torch and cupy work the same way and are shown in comments.
"""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

y, x = np.mgrid[0:64, 0:96]
img = libstb.Image(np.dstack([
    (x * 255 // 95).astype(np.uint8),
    (y * 255 // 63).astype(np.uint8),
    np.full((64, 96), 128, np.uint8),
]))

# ---- Image -> framework --------------------------------------------------------------
# The layout is fixed: uint8, (height, width, channels), on the CPU.
view = np.from_dlpack(img)
print("numpy view    :", view.shape, view.dtype, "| shares memory:", np.shares_memory(view, img.numpy()))
if view.flags.writeable:               # numpy >= 2.1; older numpy imports DLPack views read-only
    view[0, 0] = (255, 255, 255)       # writable: the image sees the change
    print("write-through :", img.numpy()[0, 0].tolist())
else:
    print("write-through : numpy < 2.1 gives a read-only view (write through img.numpy() instead)")

try:
    independent = np.from_dlpack(img, copy=True)
    print("copy=True     :", "independent" if not np.shares_memory(independent, img.numpy()) else "shared")
except TypeError:
    print("copy=True     : needs numpy >= 2.1, skipped")

# Other frameworks take the same call:
#     t = torch.from_dlpack(img)            # torch.uint8, (H, W, C)
#     j = jax.numpy.from_dlpack(img)
#     c = cupy.from_dlpack(img)
# A model usually wants (C, H, W) floats in [0, 1]; do that on the framework side:
#     chw = torch.from_dlpack(img).permute(2, 0, 1).float() / 255
chw = np.from_dlpack(img).transpose(2, 0, 1).astype(np.float32) / 255
print("chw float32   :", chw.shape, chw.dtype, float(chw.max()))

try:
    import jax.numpy as jnp
except ImportError:
    jnp = None
    print("jax           : not installed, skipped")
else:
    j = jnp.from_dlpack(img)
    print("jax           :", j.shape, j.dtype, "| same pixels:", np.array_equal(np.asarray(j), img.numpy()))

# ---- framework -> Image --------------------------------------------------------------
# Any DLPack object holding uint8 (H, W) or (H, W, 1..4) pixels on the CPU.
# A writable, C-contiguous array is shared; anything else (read-only, strided) is copied.
shared = libstb.Image.from_dlpack(view)
print("from_dlpack   :", shared, "| shares memory:", np.shares_memory(shared.numpy(), view))

if jnp is not None:
    from_jax = libstb.Image.from_dlpack(j)   # jax arrays are read-only, so this one is a copy
    print("from jax      :", from_jax)

# A model's output is usually (C, H, W) float: go back to uint8 (H, W, C) first.
#     arr = (chw_tensor.clamp(0, 1) * 255).byte().permute(1, 2, 0).contiguous()
#     libstb.Image.from_dlpack(arr)
restored = np.ascontiguousarray((chw * 255).round().astype(np.uint8).transpose(1, 2, 0))
libstb.imwrite(OUT / "roundtrip.png", libstb.Image.from_dlpack(restored))
print("round trip    :", np.array_equal(restored, img.numpy()))

# Something that is not a DLPack object is a clear TypeError.
try:
    libstb.Image.from_dlpack(b"not a tensor")
except TypeError as e:
    print("TypeError     :", e)
