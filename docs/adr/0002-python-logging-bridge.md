# ADR-0002: In Python, libstb's records go to the `logging` module through a bridge installed at import

**Status:** Accepted
**Date:** 2026-11-05
**Deciders:** Mohammad Raziei (maintainer)

## Context

A Python program already configures `logging` (handlers, formats, levels, often through a framework).
libstb's C++ logger ([ADR-0001](0001-own-logging-module-shaped-like-pythons.md)) knows nothing of it, so
without a bridge a Python user would have two places to configure, and libstb's warnings would bypass
their handlers.

## Decision

`import libstb` installs a `callback_handler` on the C++ logger `libstb`. It hands each record to
`logging.getLogger(record.name).log(levelno, message)`. Level numbers are the same in both worlds and
the dotted names become the same hierarchy, so nothing is translated. While the bridge is on, the C++
logger is opened to `debug` (unless `LIBSTB_LOG` is set, which keeps deciding what libstb emits), and
Python's level and handlers decide what is shown.

The only public function is `libstb.logging_bridge(enabled)`: `False` removes the handler and puts the
C++ level back, `True` installs it again. `libstb_py._log(name, level, message)` exists for the tests
and is not part of the API.

## Options Considered

### Option A: install automatically at import (chosen)
**Pros:** `logging.getLogger("libstb").setLevel(...)` just works; no ceremony; the default of every
Python library.
**Cons:** a cost on every logged call (below); a surprise for someone who expects the C++ output.

### Option B: only when the user calls `logging_bridge(True)`
**Pros:** no cost and no surprise by default. **Cons:** the first thing every user would have to learn;
without it a Python program sees libstb's warnings on stderr, outside its own handlers.

### Option C: expose the C++ logger (`set_level`, `add_handler`) to Python
**Pros:** one object to configure. **Cons:** a second configuration surface next to `logging`, with
its own levels and formatters, for no gain.

## Trade-off Analysis

The Python audience's expectation is decisive, so A. The price is that the C++ side cannot know
Python's level (there is no hook on `Logger.setLevel`), so it is held open at `debug` and Python
filters. Measured on a build with the bridge on and Python's level at WARNING, a logged call costs
about 2.5 microseconds (a PNG decode of 8x6 pixels: 5.5 to 8.0) and 5 microseconds for a tiny resize
(1.9 to 7.3); for an image of ordinary size that is far below one percent.

## Consequences

- Easier: one place to configure; tests with `caplog`.
- Harder: the C++ formatter is not used in Python (use `logging.Formatter`); the `pathname` and
  `lineno` fields of a record are those of the Python caller, because libstb does not record locations.
- Harder: the callback takes the GIL, so a record must never be written from a worker thread that a
  GIL-holding thread is waiting for (see [ADR-0003](0003-logging-policy.md), rule 5).
- At interpreter shutdown only `Py_IsInitialized` is checked (`Py_IsFinalizing` is not in the stable ABI).
- Revisit: a loop over tiny images where those microseconds count can call `logging_bridge(False)`; if
  that turns out to be common, sync the C++ level from Python's instead of keeping it open.

## Action Items
1. [x] `src/bindings/python/bind_logging.cpp`, `tests/python/test_logging.py`
2. [x] Guide, changelog and README mention
