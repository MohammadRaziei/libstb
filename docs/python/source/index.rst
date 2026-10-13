libstb Python API
=================

**libstb** gives Python the `stb <https://github.com/nothings/stb>`_ single-header
libraries through a nanobind extension: image decoding and encoding (PNG, JPEG, BMP,
TGA), resizing with selectable filters, image editing, EXIF orientation and TrueType
font rendering. There are no system dependencies and no Python dependencies; numpy and
Pillow are optional.

.. code-block:: python

   import libstb

   img = libstb.Image.open("photo.png")
   small = img.resize(320, 240, "cubic")
   small.write("photo.jpg")               # format from the extension

   pixels = libstb.imread("photo.png")    # ndarray, needs numpy

Installation
------------

.. code-block:: bash

   pip install libstb

.. toctree::
   :maxdepth: 1

   Overview <self>
   image
   resizer
   font
   simd
   errors
