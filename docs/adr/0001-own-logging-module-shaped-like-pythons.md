# ADR-0001: libstb has its own small logging module, shaped like Python's `logging`

**Status:** Accepted (recorded after the code was written, from the design as it stands)
**Date:** 2026-11-05
**Deciders:** Mohammad Raziei (maintainer)

## Context

libstb is a shared C++ library used from C++ and, through nanobind, from Python. It has a few things
to tell its user: a bad `LIBSTB_SIMD` or `LIBSTB_THREADS` value that fell back to `auto`, and (see
[ADR-0003](0003-logging-policy.md)) what it decided on a call. Before this, such a message was a
`fprintf(stderr)` that a program could neither silence nor redirect.

The constraints: no new dependency (the library is zero-dependency and ships in a 1 MB wheel), no
symbol that leaks into a program that links its own logging library, thread safety, and logging must
never make the call that logs fail.

## Decision

`<stb/logging.hpp>` is a small copy of Python's `logging`: the same level names and numbers (10, 20,
30, 40, 50), a tree of dotted logger names (`libstb`, `libstb.simd`), handlers (`stream_handler`,
`file_handler`, `callback_handler`, `null_handler`, or a class derived from `handler`), a formatter
with `%(field)s` patterns, propagation to parent loggers, and a "last resort" that still prints
warnings and worse to stderr when no handler exists anywhere. `LIBSTB_LOG` sets the level of `libstb`
at start. The idea of a family of platform sinks (syslog, Android, Windows debug output) comes from
[aixlog](https://github.com/badaix/aixlog) (MIT); no code was copied from it, so `NOTICE.md` is unchanged. Loggers live for the whole process, a handler writes one record at a time, and an exception
from a handler is swallowed.

## Options Considered

### Option A: our own, shaped like Python's `logging` (chosen)
| Dimension | Assessment |
|-----------|------------|
| Complexity | Medium: about 570 lines in `src/util/logging.cpp` plus tests |
| Dependencies | None |
| Familiarity | High for the Python half of the audience: the names are the ones they know |

**Pros:** no dependency; the Python bridge ([ADR-0002](0002-python-logging-bridge.md)) maps level numbers
and names one to one; the same mental model in both languages.
**Cons:** we maintain it; far fewer features than spdlog.

### Option B: depend on an existing library (spdlog, for example)
**Pros:** mature, fast, many sinks. **Cons:** a dependency for a library whose selling point is having
none; its symbols and its `fmt` copy would have to be hidden so that it links next to the user's own;
its model (sinks, patterns) does not map onto Python's.

### Option C: keep `fprintf(stderr)`
**Pros:** nothing to build. **Cons:** cannot be silenced or redirected, and no Python integration.

### Option D: one global callback (`set_log_callback`)
**Pros:** tiny. **Cons:** no levels per area, no tree, one consumer only; a program that wants a file
and the console has to write its own fan-out.

## Trade-off Analysis

The library needs very little logging, so almost any option would do for C++ alone. What decides it is
the Python side: only A and D give a clean bridge, and only A lets `LIBSTB_LOG` and the Python level mean
the same thing as in every other Python library.

## Consequences

- Easier: silencing, redirecting and filtering libstb's messages in either language.
- Harder: every feature a user expects from a logging library has to be asked for here.
- Revisit: a structured (key/value) record, if a user needs one; for now the message is text.

## Action Items
1. [x] `stb::logging` with tests (`tests/cpp/test_logging.cpp`)
2. [x] Guide section "Logging"
