Images
======

``libstb.Image`` is the C++ ``stb::image`` itself, not a wrapper. Every editing
operation returns a new ``Image``.

.. code-block:: python

   import libstb

   img = libstb.Image.open("photo.jpg", orient=True)   # apply the EXIF orientation
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
.. autofunction:: libstb.exif_orientation

Classes
-------

.. autoclass:: libstb.Image
   :members:
   :special-members: __dlpack__, __dlpack_device__

.. autoclass:: libstb.ImageInfo
   :members:
