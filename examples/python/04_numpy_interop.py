"""Sharing pixels with numpy, and explicit copies."""
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
