# ADR-0003: What libstb logs, at which level, and what it never logs

**Status:** Accepted
**Date:** 2026-11-05
**Deciders:** Mohammad Raziei (maintainer), who left the choice of a policy open: this is the
conventional one for a library (the Python `logging` HOWTO's advice, applied to a C++ core).

## Context

With the logging module ([ADR-0001](0001-own-logging-module-shaped-like-pythons.md)) and its Python
bridge ([ADR-0002](0002-python-logging-bridge.md)) in place, the question is what the library writes.
Too little and a diagnosis (which SIMD backend ran, how many threads, what was decoded) needs a
debugger; too much and a server that processes a million images drowns in it.

## Decision

1. **A library never configures logging.** It adds no handler, sets no level, and prints nothing by
   itself, except that warnings and worse reach stderr when the program has configured nothing (the
   last resort of [ADR-0001](0001-own-logging-module-shaped-like-pythons.md)).
2. **DEBUG says what libstb decided or did**, one record per call, with sizes and counts and never
   with pixel data, file contents or paths. Never inside a loop over pixels, rows or glyphs, and never
   in code that is called in a tight loop (scopes, accessors, `thread_count()`).
3. **WARNING says libstb did something different from what was asked**, because of the input or the
   machine: an environment variable it ignored, a pattern it could not use, a coordinate it moved, a
   thread it could not start. It is raised once per call. Data-dependent quality issues (a glyph the font
   does not have) are DEBUG: a program that renders user text would otherwise warn on every request.
4. **No INFO, no ERROR, no CRITICAL.** A failure is an exception, and is never also a record (a failure
   reported twice is noise, and the caller decides what to log). INFO is for events of a running
   service; a library has none.
5. **No record from a worker thread of `util/parallel.hpp`.** The Python bridge takes the GIL, and the
   thread that started the workers may hold it while it waits for them.
6. **Loggers are named by area**: `libstb.decode`, `libstb.encode`, `libstb.resize`, `libstb.zlib`,
   `libstb.font`, `libstb.raster`, `libstb.simd`, `libstb.threads` (the functions in `src/util/log.hpp`).
7. **Messages are plain lowercase facts**, with the numbers in them (`resize 4000x3000 -> 800x600, 3
   channel(s): 4 thread(s) (ceiling 8)`), so that they are greppable and stable.

What is logged today:

| Logger | Level | When |
| --- | --- | --- |
| `libstb.decode` | DEBUG | an image was decoded (size, channels, input bytes, EXIF orientation applied) |
| `libstb.encode` | DEBUG | an image was encoded (format, size, output bytes) |
| `libstb.resize` | DEBUG | a resize (sizes, threads it will use, the ceiling) |
| `libstb.zlib` | DEBUG | compress or decompress (sizes, level or `max_bytes`) |
| `libstb.simd` | DEBUG | the backend chosen at first use, or set for the process |
| `libstb.simd` | WARNING | `LIBSTB_SIMD` names no available backend |
| `libstb.threads` | DEBUG | the thread ceiling set for the process |
| `libstb.threads` | WARNING | `LIBSTB_THREADS` is invalid, or a thread could not be started |
| `libstb.font` | DEBUG | glyphs the font lacks, when text is rendered |
| `libstb.raster` | WARNING | a NaN or infinite coordinate (the outline is ignored), a coordinate moved to the limit, an unusable dash pattern (the line is solid), a NaN stroke width or miter limit |

## Options Considered

### Option A: this policy (chosen)
**Pros:** silent by default; the useful facts are one switch away; no double reporting.
**Cons:** DEBUG is cheap but not free (below).

### Option B: log failures (ERROR) at the point they are thrown
**Pros:** a trace even when the caller swallows the exception. **Cons:** the same failure twice, with
the caller's log in between; a hostile upload would write an ERROR per request.

### Option C: log nothing but the environment warnings (the state before)
**Pros:** zero cost. **Cons:** "which backend ran" and "why one thread" are exactly the questions a
user has, and the answer was only in the debugger.

## Trade-off Analysis

The line between DEBUG and WARNING is "did libstb deviate from the request". It is a rule a
contributor can apply without asking: silent fallbacks are the one thing worth a warning, because the
result is not what the caller asked for and no exception told them.

## Consequences

- Easier: diagnosing a slow or odd run with `LIBSTB_LOG=debug` or `logging.getLogger("libstb").setLevel(DEBUG)`.
- Cost: a disabled DEBUG call is one level check; with the Python bridge on, every logged call crosses
  into Python (about 0.6 microseconds, [ADR-0002](0002-python-logging-bridge.md)).
- Tests: `tests/cpp/test_library_logging.cpp` and `tests/python/test_logging.py` pin the default
  silence, the one-record-per-call rule, the absence of records for failures, and each WARNING.
- Revisit: INFO, if libstb ever gains a long-running component.

## Action Items
1. [x] `src/util/log.hpp` and the calls listed above
2. [x] Tests
3. [ ] When a feature is added, say in its pull request which of these rules its logging follows
