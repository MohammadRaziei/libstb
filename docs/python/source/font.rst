Fonts
=====

.. code-block:: python

   import libstb

   font = libstb.Font.open("font.ttf")
   libstb.Font.open("font.ttf").render("Hi", 32).bitmap.write("hi.png")

.. autoclass:: libstb.Font
   :members:

.. autoclass:: libstb.FontMetrics
   :members:

.. autoclass:: libstb.Glyph
   :members:

.. autoclass:: libstb.TextSize
   :members:

.. autoclass:: libstb.RenderedText
   :members:

.. autoclass:: libstb.Atlas
   :members:

.. autoclass:: libstb.AtlasGlyph
   :members:
