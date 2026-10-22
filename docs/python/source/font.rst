Fonts
=====

.. code-block:: python

   import libstb as stb

   font = stb.Font.open("font.ttf")
   stb.Font.open("font.ttf").render("Hi", 32).bitmap.write("hi.png")

   # no font file? estimate the size from fixed metric tables
   stb.estimate_text("Hello, world", 16, family="sans")   # TextSize(width, height, lines)

Right-to-left text (Persian, Arabic, Urdu, Hebrew) is shaped and reordered inside ``render``, ``measure`` and
``draw_text``: pass the text as typed. Letter shapes and ligatures (lam-alef, "الله", ...) come from the font's
GSUB table; a font without one but with the Arabic Presentation Forms in its cmap is shaped through those.
ZWNJ is honoured. Not done: GPOS. Marks (harakat) are placed by a simple rule, centred on their letter and
stacked above or below it, and kerning is the ``kern`` table only. ``Font.shape_glyphs`` returns the glyph
indices that shaping chose.

.. code-block:: python

   fa = stb.Font.open("Vazirmatn-Regular.ttf")
   img.draw_text("\u0633\u0644\u0627\u0645 \u062f\u0646\u06cc\u0627", fa, 32, 10, 10, color="black")

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
