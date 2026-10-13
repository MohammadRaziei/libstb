Resizing
========

.. code-block:: python

   import libstb

   img.resize(320, 240)                    # default resizer
   img.resize(320, 240, "cubic")           # a filter by name
   r = libstb.Resizer("mitchell")
   img.resize(320, 240, r)

.. autoclass:: libstb.Resizer
   :members:
