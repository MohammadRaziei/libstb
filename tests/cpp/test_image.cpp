#include <atomic>
#include <cerrno>
#include <filesystem>
#include <thread>

#include "test_common.hpp"

using testutil::red_over_green;

UTEST(libstb_image_tests, test_info_reads_header) {
    const std::string d = red_over_green();
    stb::image_info i = stb::image_info::read(d.data(), d.size());
    ASSERT_EQ(1, i.width);
    ASSERT_EQ(2, i.height);
    ASSERT_EQ(3, i.channels());
}

UTEST(libstb_image_tests, test_decode_and_accessors) {
    const std::string d = red_over_green();
    stb::image img = stb::image::decode(d.data(), d.size());
    ASSERT_FALSE(img.empty());
    ASSERT_EQ(1, img.width());
    ASSERT_EQ(2, img.height());
    ASSERT_EQ(3, img.channels());
    ASSERT_EQ(3u, img.stride());
    ASSERT_EQ(6u, img.size_bytes());
    ASSERT_EQ(255, img(0, 0, 0));  // top: red
    ASSERT_EQ(255, img(0, 1, 1));  // bottom: green
    ASSERT_EQ(0, img(0, 1, 0));
    ASSERT_TRUE(img.data() == &img(0, 0, 0));
}

UTEST(libstb_image_tests, test_decode_converts_to_rgba_with_opaque_alpha) {
    const std::string d = red_over_green();
    stb::load_options o;
    o.format = stb::pixel_format::rgba;
    stb::image img = stb::image::decode(d.data(), d.size(), o);
    ASSERT_EQ(4, img.channels());
    ASSERT_EQ(8u, img.size_bytes());
    ASSERT_EQ(255, img(0, 0, 3));
    ASSERT_EQ(255, img(0, 1, 3));
}

UTEST(libstb_image_tests, test_decode_converts_to_gray) {
    const std::string d = red_over_green();
    stb::load_options o;
    o.format = stb::pixel_format::gray;
    stb::image img = stb::image::decode(d.data(), d.size(), o);
    ASSERT_EQ(1, img.channels());
    ASSERT_EQ(2u, img.size_bytes());
    ASSERT_TRUE(img(0, 0) > 0 && img(0, 0) < 255);
}

UTEST(libstb_image_tests, test_flip_swaps_rows) {
    const std::string d = red_over_green();
    stb::load_options o;
    o.flip = true;
    stb::image img = stb::image::decode(d.data(), d.size(), o);
    ASSERT_EQ(255, img(0, 0, 1));  // green now on top
    ASSERT_EQ(255, img(0, 1, 0));  // red at the bottom
}

// ---- error hierarchy -----------------------------------------------------

UTEST(libstb_image_tests, test_garbage_throws_decode_error) {
    const std::string d = "definitely not an image";
    ASSERT_THROWS(stb::image::decode(d.data(), d.size()), stb::decode_error);
    ASSERT_THROWS(stb::image_info::read(d.data(), d.size()), stb::decode_error);
}

UTEST(libstb_image_tests, test_truncated_and_empty_input_throw_decode_error) {
    std::string d = red_over_green();
    d.resize(d.size() - 3);  // header promises 6 bytes, only 3 present
    ASSERT_THROWS(stb::image::decode(d.data(), d.size()), stb::decode_error);
    ASSERT_THROWS(stb::image::decode(d.data(), 0), stb::decode_error);
    ASSERT_THROWS(stb::image::decode(nullptr, 0), stb::decode_error);
}

UTEST(libstb_image_tests, test_errors_form_one_hierarchy) {
    const std::string d = "nope";
    bool as_libstb_error = false, as_runtime_error = false, as_std_exception = false;
    try { stb::image::decode(d.data(), d.size()); } catch (const stb::error&) { as_libstb_error = true; }
    try { stb::image::decode(d.data(), d.size()); } catch (const std::runtime_error&) { as_runtime_error = true; }
    try { stb::image::decode(d.data(), d.size()); } catch (const std::exception&) { as_std_exception = true; }
    ASSERT_TRUE(as_libstb_error && as_runtime_error && as_std_exception);
}

UTEST(libstb_image_tests, test_bad_arguments_throw_invalid_argument) {
    const std::string d = red_over_green();
    stb::load_options o;
    ASSERT_THROWS(stb::image::decode(nullptr, 10), std::invalid_argument);
}

UTEST(libstb_image_tests, test_a_real_png_bomb_is_refused_from_its_header) {
    // A genuine bomb made by our own encoder: 6000 x 6000 RGBA of zeros (144 MB of pixels)
    // deflates about 1000 to 1, so the file is ~140 KB. Decoding it needs 144 MB, which the
    // caller's max_bytes forbids: the refusal must come from the header, before any pixel
    // memory is allocated (tests/python/test_png_bomb.py does the same past the 512 MiB default).
    std::vector<std::uint8_t> png;
    {
        stb::image zeros(6000, 6000, stb::pixel_format::rgba);
        std::fill(zeros.begin(), zeros.end(), std::uint8_t(0));
        png = zeros.to_png();
    }                                                   // the 144 MB image is gone before decoding
    ASSERT_TRUE(png.size() < 1024 * 1024);              // a file you would happily accept...
    ASSERT_TRUE(std::uint64_t(6000) * 6000 * 4 / png.size() > 500);   // ...at a ratio of ~1000

    const stb::image_info info = stb::image_info::read(png.data(), png.size());
    ASSERT_EQ(6000, info.width);
    ASSERT_EQ(6000, info.height);

    stb::load_options o;
    o.max_bytes = std::size_t(64) << 20;
    ASSERT_THROWS(stb::image::decode(png.data(), png.size(), o), stb::limit_error);   // not decode_error
    o.format = stb::pixel_format::gray;                                     // asking for gray: 36 MB, under the limit
    ASSERT_EQ(6000, stb::image::decode(png.data(), png.size(), o).width());           // so it is decoded
}

UTEST(libstb_image_tests, test_max_bytes_is_enforced_from_header) {
    const std::string d = red_over_green();  // decodes to 6 bytes
    stb::load_options o;
    o.max_bytes = 5;
    ASSERT_THROWS(stb::image::decode(d.data(), d.size(), o), stb::limit_error);
    o.max_bytes = 6;
    ASSERT_EQ(6u, stb::image::decode(d.data(), d.size(), o).size_bytes());
}

UTEST(libstb_image_tests, test_huge_declared_dimensions_rejected_without_allocating) {
    // Header claims 60000x60000 RGB (~10 GB) but carries no pixel data at all.
    const std::string d = "P6\n60000 60000\n255\n";
    ASSERT_THROWS(stb::image::decode(d.data(), d.size()), stb::limit_error);
    // Same story past STBI_MAX_DIMENSIONS: rejected by the max_bytes guard.
    const std::string e = "P6\n70000 70000\n255\n";
    ASSERT_THROWS(stb::image::decode(e.data(), e.size()), stb::limit_error);
}

// ---- image as a value type -----------------------------------------------

UTEST(libstb_image_tests, test_default_image_is_empty) {
    stb::image img;
    ASSERT_TRUE(img.empty());
    ASSERT_EQ(0, img.width());
    ASSERT_EQ(0u, img.size_bytes());
}

UTEST(libstb_image_tests, test_pixel_format_helpers) {
    using stb::pixel_format;
    ASSERT_EQ(1, stb::channel_count(pixel_format::gray));
    ASSERT_EQ(2, stb::channel_count(pixel_format::gray_alpha));
    ASSERT_EQ(3, stb::channel_count(pixel_format::rgb));
    ASSERT_EQ(4, stb::channel_count(pixel_format::rgba));
    ASSERT_FALSE(stb::has_alpha(pixel_format::gray));
    ASSERT_TRUE(stb::has_alpha(pixel_format::gray_alpha));
    ASSERT_FALSE(stb::has_alpha(pixel_format::rgb));
    ASSERT_TRUE(stb::has_alpha(pixel_format::rgba));
    for (int c = 1; c <= 4; ++c) {
        const pixel_format f = stb::pixel_format_from_channels(c);
        ASSERT_EQ(c, stb::channel_count(f));
        ASSERT_TRUE(f == stb::pixel_format_from_name(stb::pixel_format_name(f)));  // name round trip
    }
    ASSERT_THROWS(stb::pixel_format_from_channels(0), std::invalid_argument);
    ASSERT_THROWS(stb::pixel_format_from_channels(5), std::invalid_argument);
    ASSERT_THROWS(stb::pixel_format_from_name("RGB"), std::invalid_argument);  // lower case only
    ASSERT_THROWS(stb::pixel_format_from_name("cmyk"), std::invalid_argument);
}

UTEST(libstb_image_tests, test_an_image_reports_its_format) {
    ASSERT_TRUE(stb::pixel_format::rgba == stb::image(2, 2, stb::pixel_format::rgba).format());
    ASSERT_TRUE(stb::pixel_format::gray == stb::image().format());  // an empty image reports gray, 0 channels
    ASSERT_EQ(0, stb::image().channels());
}

UTEST(libstb_image_tests, test_constructors_validate) {
    stb::image img(3, 2, stb::pixel_format::rgba);
    ASSERT_EQ(24u, img.size_bytes());
    ASSERT_EQ(0, img(2, 1, 3));  // zero-filled
    ASSERT_THROWS(stb::image(0, 2, stb::pixel_format::rgb), std::invalid_argument);
    ASSERT_THROWS(stb::image(2, -1, stb::pixel_format::rgb), std::invalid_argument);
    ASSERT_THROWS(stb::image(2, 2, stb::pixel_format::rgb, std::vector<std::uint8_t>(11)), std::invalid_argument);
    stb::image ok(2, 2, stb::pixel_format::rgb, std::vector<std::uint8_t>(12, 7));
    ASSERT_EQ(7, ok(1, 1, 2));
}

UTEST(libstb_image_tests, test_copy_is_deep_and_move_transfers) {
    stb::image a = testutil::gradient(4, 3, 3);
    stb::image b = a.copy();
    b(0, 0, 0) = static_cast<std::uint8_t>(a(0, 0, 0) + 1);
    ASSERT_FALSE(testutil::same(a, b));
    stb::image c = std::move(b);
    ASSERT_FALSE(c.empty());
    ASSERT_EQ(4, c.width());
}

// ---- files ---------------------------------------------------------------

UTEST(libstb_image_tests, test_open_and_read_file) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_test_open.ppm";
    const std::string d = red_over_green();
    {
        FILE* f = std::fopen(path.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr);
        std::fwrite(d.data(), 1, d.size(), f);
        std::fclose(f);
    }
    stb::image img = stb::image::open(path);
    ASSERT_EQ(255, img(0, 0, 0));
    ASSERT_EQ(2, stb::image_info::read_file(path).height);
    std::filesystem::remove(path);
}

UTEST(libstb_image_tests, test_open_missing_file_throws_io_error) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_definitely_missing.png";
    ASSERT_THROWS(stb::image::open(path), stb::io_error);
    ASSERT_THROWS(stb::image_info::read_file(path), stb::io_error);
}

// ---- threads -------------------------------------------------------------

UTEST(libstb_image_tests, test_flip_is_per_call_and_thread_safe) {
    const std::string d = red_over_green();
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < 2000; ++i) {
                stb::load_options o;
                o.flip = ((i + t) % 2) == 1;
                stb::image img = stb::image::decode(d.data(), d.size(), o);
                const bool top_is_red = img(0, 0, 0) == 255;
                if (top_is_red == o.flip) ++bad;  // flipped => green on top
            }
        });
    }
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}

UTEST(libstb_image_tests, test_io_error_carries_the_errno) {
    const auto missing = std::filesystem::temp_directory_path() / "libstb_definitely_missing.png";
    std::filesystem::remove(missing);
    try {
        stb::image::open(missing);
        ASSERT_TRUE(false);  // must have thrown
    } catch (const stb::io_error& e) {
        ASSERT_EQ(ENOENT, e.code());
    }
}

// ---- ownership: move-only, explicit copy(), wrap() -----------------------

#include <memory>
#include <type_traits>

UTEST(libstb_image_tests, test_image_is_move_only_and_copies_are_explicit) {
    static_assert(!std::is_copy_constructible<stb::image>::value, "copies must be explicit");
    static_assert(!std::is_copy_assignable<stb::image>::value, "copies must be explicit");
    static_assert(std::is_nothrow_move_constructible<stb::image>::value, "");
    static_assert(std::is_nothrow_move_assignable<stb::image>::value, "");
    ASSERT_TRUE(true);
}

UTEST(libstb_image_tests, test_copy_is_deep_and_independent) {
    stb::image a(2, 2, stb::pixel_format::rgb);
    a(0, 0, 0) = 10;
    stb::image b = a.copy();
    ASSERT_TRUE(a.data() != b.data());
    ASSERT_EQ(10, b(0, 0, 0));
    b(0, 0, 0) = 99;
    ASSERT_EQ(10, a(0, 0, 0));
    ASSERT_TRUE(stb::image().copy().empty());
}

UTEST(libstb_image_tests, test_moved_from_image_is_empty) {
    stb::image a(2, 2, stb::pixel_format::rgb);
    const std::uint8_t* p = a.data();
    stb::image b = std::move(a);
    ASSERT_TRUE(a.empty());
    ASSERT_EQ(0, a.width());
    ASSERT_EQ(0u, a.size_bytes());
    ASSERT_TRUE(b.data() == p);  // the pixels moved, they were not copied
    stb::image c;
    c = std::move(b);
    ASSERT_TRUE(b.empty());
    ASSERT_TRUE(c.data() == p);
}

UTEST(libstb_image_tests, test_wrap_views_foreign_memory_without_copying) {
    std::vector<std::uint8_t> buf(2 * 2 * 3, 7);
    stb::image img = stb::image::wrap(2, 2, stb::pixel_format::rgb, buf.data());
    ASSERT_TRUE(img.data() == buf.data());
    img(1, 1, 2) = 9;
    ASSERT_EQ(9, buf[buf.size() - 1]);  // it is the same memory
    stb::image owned = img.copy();      // copy() detaches
    owned(0, 0, 0) = 1;
    ASSERT_EQ(7, buf[0]);
}

UTEST(libstb_image_tests, test_wrap_keeps_its_owner_alive_until_the_image_dies) {
    static bool freed;
    freed = false;
    static std::uint8_t px[4 * 4 * 1] = {};
    {
        stb::image moved;
        {
            std::shared_ptr<void> keep(px, [](void*) { freed = true; });
            stb::image img = stb::image::wrap(4, 4, stb::pixel_format::gray, px, std::move(keep));
            ASSERT_FALSE(freed);
            moved = std::move(img);  // the owner travels with the pixels
        }
        ASSERT_FALSE(freed);
    }
    ASSERT_TRUE(freed);
}

UTEST(libstb_image_tests, test_wrap_validates_its_arguments) {
    std::uint8_t px[4] = {};
    ASSERT_THROWS(stb::image::wrap(2, 2, stb::pixel_format::gray, nullptr), std::invalid_argument);
    ASSERT_THROWS(stb::image::wrap(0, 2, stb::pixel_format::gray, px), std::invalid_argument);
}

UTEST(libstb_image_tests, test_decoded_image_owns_stbs_buffer_and_survives_moves) {
    const std::string d = red_over_green();
    stb::image a = stb::image::decode(d.data(), d.size());
    stb::image b = std::move(a);
    ASSERT_EQ(255, b(0, 0, 0));
    ASSERT_TRUE(testutil::same(b, b.copy()));
}
