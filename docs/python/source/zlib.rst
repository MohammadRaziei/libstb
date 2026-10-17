zlib
====

zlib streams (RFC 1950) with libstb's own compressor, the one that writes PNG, and stb_image's
inflate. Both interoperate with python's :mod:`zlib` module and any other zlib implementation.

.. code-block:: python

   import libstb as stb

   packed = stb.zlib_compress(b"hello hello hello", level=6)   # bytes; level 0..9 like zlib (0 stores)
   data = stb.zlib_decompress(packed, max_bytes=64 * 1024 * 1024)

``zlib_decompress`` bounds its **output**: a tiny stream can expand a thousand-fold, so the
output buffer is grown in doublings and never allowed past ``max_bytes`` (default 512 MiB). A
stream that would produce more raises :class:`libstb.LimitError` instead of exhausting memory.
Nothing is reserved up front.

Speed, measured against python's zlib on one machine (source code, a binary, noisy image
pixels; measure on your data): ``zlib_compress`` at the default level was 1.2x to 1.9x faster
than zlib level 6 with output 0.4% to 3.3% larger. ``zlib_decompress`` is **not** faster than
zlib: equal on binary and image data, about 2.5x slower on very compressible text.

.. autofunction:: libstb.zlib_compress

.. autofunction:: libstb.zlib_decompress

.. autodata:: libstb.DEFAULT_ZLIB_LEVEL

.. autodata:: libstb.DEFAULT_ZLIB_MAX_BYTES
