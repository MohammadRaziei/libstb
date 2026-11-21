Images
======

``stb.Image`` is the C++ ``stb::image`` itself, not a wrapper. Every editing
operation returns a new ``Image``.

.. code-block:: python

   import libstb as stb

   img = stb.Image.open("photo.jpg", exif_orient=True)   # apply the EXIF orientation
   same = stb.Loader("photo.jpg").exif_orient().run()    # the same, with a Loader (as in C++)
   img2 = stb.Image.decode(open("photo.jpg", "rb").read())   # encoded bytes: decode, not open
   part = img.crop(10, 10, 200, 100)
   turned = img.rotate90()                              # counter-clockwise
   gray = img.convert(1)
   gray.write_jpg("gray.jpg", quality=80)

Functions
---------

.. autofunction:: libstb.imread
.. autofunction:: libstb.imwrite
.. autofunction:: libstb.imresize
.. autofunction:: libstb.iminfo

Classes
-------

.. autoclass:: libstb.Image
   :members:
   :special-members: __dlpack__, __dlpack_device__

.. autoclass:: libstb.Loader
   :members:

.. autoclass:: libstb.ImageInfo
   :members:
