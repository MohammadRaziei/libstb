Threads
=======

A call with a lot of work in it (so far ``Image.resize`` and ``Resizer.resize``) shares that
work between threads of its own; a call with little work never starts one. The pixels are the
same whatever the number of threads. The number below is a ceiling, not a demand. By default it
is automatic: the CPUs the process may run on. ``LIBSTB_THREADS=auto|off|<n>`` in the
environment chooses the initial setting.

.. code-block:: python

   import libstb as stb

   stb.thread_count()              # e.g. 8
   with stb.threads(1):            # this thread only, nests
       small = img.resize(800, 600)
   stb.set_threads(2)              # the whole process; 0 is automatic

Set 1 when your program already runs one worker per CPU or calls libstb from a thread pool.

.. autofunction:: libstb.thread_count
.. autofunction:: libstb.set_threads
.. autofunction:: libstb.threads
