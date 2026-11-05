#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "stb.h"
#include "utest/utest.h"

// utest.h has no exception macro: check the exact (base-or-derived) type.
#define ASSERT_THROWS(expr, ex)   \
    do {                          \
        bool caught_ = false;     \
        try {                     \
            (void)(expr);         \
        } catch (const ex&) {     \
            caught_ = true;       \
        }                         \
        ASSERT_TRUE(caught_);     \
    } while (0)

namespace testutil {

// Collects the records of the "libstb" logger tree at `at` and above while it lives (and puts the logger back
// as it was). A test that feeds libstb bad input on purpose uses it to say what libstb answered: with no
// handler, a warning would go to stderr and into the test output.
struct log_capture {
    struct entry {
        std::string name;
        stb::logging::level level;
        std::string message;
    };
    stb::logging::logger& lib = stb::logging::get_logger("libstb");
    stb::logging::level before;
    std::vector<entry> records;
    stb::logging::handler* handler;

    explicit log_capture(stb::logging::level at) : before(lib.get_level()) {
        lib.set_level(at);
        handler = &lib.add_handler(stb::logging::callback_handler([this](const stb::logging::record& r) {
            records.push_back({std::string(r.name), r.levelno, std::string(r.message)});
        }));
    }
    ~log_capture() {
        lib.remove_handler(*handler);
        lib.set_level(before);
    }
    log_capture(const log_capture&) = delete;
    log_capture& operator=(const log_capture&) = delete;

    int count(const char* name) const {
        int n = 0;
        for (const entry& e : records) n += e.name == name ? 1 : 0;
        return n;
    }
    const entry* first(const char* name) const {
        for (const entry& e : records)
            if (e.name == name) return &e;
        return nullptr;
    }
};

// Binary PPM (P6): the simplest format stb_image reads, so decode tests need
// no encoding step (and no binary fixtures).
inline std::string ppm(int w, int h, const std::vector<std::uint8_t>& rgb) {
    std::string s = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    s.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    return s;
}

// The format with this many channels, for tests that loop over 1..4.
inline stb::pixel_format fmt(int channels) { return stb::pixel_format_from_channels(channels); }

// 1x2 image: top row red, bottom row green.
inline std::string red_over_green() { return ppm(1, 2, {255, 0, 0, 0, 255, 0}); }

// Deterministic, non-flat pixels (defeats trivially-passing round trips).
inline stb::image gradient(int w, int h, int c) {
    stb::image img(w, h, fmt(c));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < c; ++k)
                img(x, y, k) = static_cast<std::uint8_t>((x * 31 + y * 17 + k * 53) & 0xFF);
    return img;
}

inline stb::image solid(int w, int h, int c, std::uint8_t v) {
    return stb::image(w, h, fmt(c), std::vector<std::uint8_t>(std::size_t(w) * h * c, v));
}

inline bool same(const stb::image& a, const stb::image& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::equal(a.begin(), a.end(), b.begin());
}

}  // namespace testutil
