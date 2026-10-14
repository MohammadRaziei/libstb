Resizing
========

.. code-block:: python

   import libstb as stb

   img.resize(320, 240)                    # default resizer
   img.resize(320, 240, "cubic")           # a filter by name
   r = stb.Resizer("mitchell")
   img.resize(320, 240, r)

.. autoclass:: libstb.Resizer
   :members:
