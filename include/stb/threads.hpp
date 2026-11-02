#pragma once

// How many threads one libstb call may use.

#include "stb/export.hpp"

namespace stb {

// Threads. Some operations (so far image::resize and resizer::resize) share their work between
// several threads when there is enough of it to pay for starting them. Every extra thread has to
// get a few hundred microseconds of work, so a thumbnail is always made on the calling thread and
// a photograph is split. The result never depends on the number of threads: it is byte for byte
// what one thread produces. Threads live only for the duration of the call (there is no pool,
// nothing is left running afterwards, and fork() is safe).
//
// The number below is a ceiling, not a demand: set_threads(8) does not make a small image use
// eight threads, it only allows a large one to.
//
//   thread_count()    the most threads, the calling thread included, that one operation started
//                     on the calling thread may use. Never less than 1.
//   set_threads(n)    for the whole process. n == 0: automatic (the default), as many threads as
//                     this process may run on at once: the CPUs it is allowed to use (CPU
//                     affinity) and, on Linux, a cgroup CPU limit such as `docker run --cpus=2`.
//                     That count is read once. n == 1: never use another thread. n >= 2: at most
//                     n. Returns false, changing nothing, for a negative n.
//   thread_scope(n)   the same for the calling thread only, for as long as the object lives:
//                     other threads are not affected. Scopes nest and take precedence over
//                     set_threads. Throws std::invalid_argument for a negative n. Create and
//                     destroy a scope on the same thread.
//
// LIBSTB_THREADS=auto | off | <n> in the environment chooses the initial setting ("off" is 1; an
// unrecognised value falls back to auto and prints one line on stderr). A program that already
// runs one worker per CPU, or calls libstb from a thread pool of its own, can set_threads(1) so
// that libstb does not add threads on top of those. All of these are thread-safe.
STB_API int thread_count() noexcept;
STB_API bool set_threads(int n) noexcept;

class STB_API thread_scope {
public:
    explicit thread_scope(int n);
    ~thread_scope();
    thread_scope(const thread_scope&) = delete;
    thread_scope& operator=(const thread_scope&) = delete;
    thread_scope(thread_scope&&) = delete;  // a scope belongs to the thread and block that made it
    thread_scope& operator=(thread_scope&&) = delete;

private:
    int previous_;
};

}  // namespace stb
