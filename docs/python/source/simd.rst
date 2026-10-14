SIMD backends
=============

The pixel kernels pick a backend at run time. All backends give identical output.
``LIBSTB_SIMD`` in the environment selects one for diagnosis; an unknown or unavailable
name prints one line on stderr and falls back to the best backend.

.. code-block:: python

   import libstb as stb

   stb.simd_backends()             # e.g. ['avx2', 'sse2', 'scalar']
   with stb.simd("scalar"):        # this thread only, nests
       out = img.convert(1)

.. autofunction:: libstb.simd_backends
.. autofunction:: libstb.simd_name
.. autofunction:: libstb.set_simd
.. autofunction:: libstb.simd
