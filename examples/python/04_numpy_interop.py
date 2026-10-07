"""Sharing pixels with numpy and other frameworks, explicit copies, and no numpy at all."""
import numpy as np
import libstb

# Image(array) uses a writable C-contiguous uint8 array in place: no copy.
arr = np.zeros((4, 4, 3), dtype=np.uint8)
img = libstb.Image(arr)
arr[0, 0] = (255, 0, 0)
print("array write visible in image :", img.numpy()[0, 0].tolist())
img.numpy()[1, 1] = (0, 255, 0)
print("image write visible in array :", arr[1, 1].tolist())

# Arrays that cannot be shared are copied: read-only or strided ones.
flipped = arr[::-1]
img2 = libstb.Image(flipped)
arr[3, 3] = (9, 9, 9)
print("strided array is a copy      :", tuple(img2.numpy()[0, 0]) != (9, 9, 9))

# When you want an independent image, say so.
clone = img.copy()
arr[2, 2] = (7, 7, 7)
print("copy() is independent        :", tuple(clone.numpy()[2, 2]) == (0, 0, 0))

# DLPack: numpy, torch, jax and cupy can all take the pixels with no copy.
# The layout is fixed: uint8, (height, width, channels), on the CPU.
shared = np.from_dlpack(img)
print("DLPack shares memory         :", np.shares_memory(shared, img.numpy()))
# torch.from_dlpack(img) and jax.numpy.from_dlpack(img) work the same way.
# And back: any DLPack object holding uint8 (H, W, C) pixels becomes an Image.
back = libstb.Image.from_dlpack(shared)
print("from_dlpack shares memory    :", np.shares_memory(back.numpy(), shared))
# For another layout, convert on their side, e.g.
#     torch.from_dlpack(img).permute(2, 0, 1).float() / 255
print("DLPack copy=True independent :", not np.shares_memory(np.from_dlpack(img, copy=True), img.numpy()))

# Without numpy: memoryview is a zero-copy 3-D view, tobytes() is a copy.
view = memoryview(img)
print("memoryview shape             :", view.shape)
raw = img.tobytes()
print("tobytes length               :", len(raw), "(4 * 4 * 3 =", 4 * 4 * 3, ")")

# Any buffer works as input: bytearray, array.array, memoryview, ...
buf = bytearray(2 * 2 * 4)
rgba = libstb.Image(memoryview(buf).cast("B", (2, 2, 4)))
print("from a bytearray             :", rgba.width, rgba.height, rgba.channels)
