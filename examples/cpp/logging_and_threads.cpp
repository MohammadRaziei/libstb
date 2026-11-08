// Logging, threads and SIMD: what libstb decided, and how to change it for one scope.
//
//   logging_and_threads
//
// You never have to set any of this: the fastest SIMD backend is picked at run time and threads are used only
// where the work pays for them. When you do want control (a library, a thread pool, a diagnosis), a scope
// changes ONE thread only, and the previous setting comes back when it ends.
#include <chrono>
#include <cstdio>
#include <iostream>

#include "stb.h"

// The sample pictures and fonts of the repository (data/README.md): the build sets the real path.
#ifndef LIBSTB_DATA_DIR
#define LIBSTB_DATA_DIR "data"
#endif

int main() {
    try {
        namespace lg = stb::logging;

        // 1. libstb is silent unless asked. At debug it says what it decided on each call (the backend, the
        //    threads, the sizes). Handlers are yours: a stream, a file, or a function that gets every record.
        lg::logger& lib = lg::get_logger("libstb");                       // children: "libstb.simd", ...
        lib.set_level(lg::level::debug);
        lib.add_handler(lg::stream_handler(stderr)).set_formatter(lg::formatter("%(levelname)s %(name)s: %(message)s"));
        int records = 0;
        lib.add_handler(lg::callback_handler([&](const lg::record&) { ++records; }));

        // 2. What this machine runs.
        std::cout << "simd " << stb::simd_name() << " (";
        for (const std::string& b : stb::simd_backends()) std::cout << ' ' << b;
        std::cout << " ), threads " << stb::thread_count() << '\n';

        stb::load_options rgba;
        rgba.format = stb::pixel_format::rgba;
        const stb::image big = stb::image::open(LIBSTB_DATA_DIR "/images/astronaut.png", rgba).resize(3000, 2000);

        const stb::image overlay = big.resize(2500, 1800);

        // One blend (it has SIMD kernels) and one resize (it can use several threads), timed.
        auto timed = [&](const char* what) {
            using clock = std::chrono::steady_clock;
            auto ms = [](clock::time_point t0) { return std::chrono::duration<double, std::milli>(clock::now() - t0).count(); };
            stb::image target = big.copy();                               // blend() changes the image it is called on
            auto t0 = clock::now();
            target.blend(overlay, 10, 10);
            const double blend_ms = ms(t0);
            t0 = clock::now();
            stb::image small = big.resize(750, 500);
            std::cout << what << ": blend " << blend_ms << " ms, resize " << ms(t0) << " ms | simd " << stb::simd_name() << '\n';
        };

        // 3. A scope changes this thread only; the old values come back when it ends.
        timed("automatic");
        {
            stb::thread_scope one_thread(1);                              // no worker threads
            stb::simd_scope scalar("scalar");                             // the portable code, no SIMD
            timed("1 thread, scalar");
        }
        timed("automatic again");

        // 4. Your own messages go through the same machinery.
        lg::logger& mine = lg::get_logger("myapp");
        mine.add_handler(lg::stream_handler(stderr)).set_formatter(lg::formatter("%(asctime)s %(levelname)-8s %(message)s"));
        mine.set_level(lg::level::info);
        mine.info("done: libstb said %d things at debug level", records);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
