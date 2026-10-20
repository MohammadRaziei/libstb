Fonts
=====

.. code-block:: python

   import libstb as stb

   font = stb.Font.open("font.ttf")
   stb.Font.open("font.ttf").render("Hi", 32).bitmap.write("hi.png")

   # no font file? estimate the size from fixed metric tables
   stb.estimate_text("Hello, world", 16, family="sans")   # TextSize(width, height, lines)

.. autoclass:: libstb.Font
   :members:

.. autoclass:: libstb.Canvas
   :members:

.. autoclass:: libstb.CanvasText
   :members:

.. autoclass:: libstb.CanvasShape
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

Estimating without a font file
------------------------------

.. autofunction:: libstb.estimate_text

.. autofunction:: libstb.estimate_metrics
