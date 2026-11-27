#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "conftest.hpp"

// mat<T> (mat8u, mat32f, mat32i), the image types built on it, and the conversions between 8 bits and floats.
// The expected values are written out here, not taken from the library.

namespace {

static_assert(std::is_same<stb::image, stb::image8u>::value, "image is another name for image8u");
static_assert(std::is_same<stb::image32f, stb::mat32f>::value, "image32f is another name for mat32f");
static_assert(std::is_same<stb::mat8u, stb::mat<std::uint8_t>>::value, "mat8u is mat<uint8_t>");
static_assert(std::is_same<stb::mat32i, stb::mat<std::int32_t>>::value, "mat32i is mat<int32_t>");
static_assert(std::is_base_of<stb::mat8u, stb::image8u>::value, "an image8u is a mat8u");
static_assert(!std::is_constructible<stb::mat32f, const stb::mat32f&>::value, "copies are explicit");
static_assert(std::is_nothrow_move_constructible<stb::mat32f>::value, "moving never throws");
static_assert(std::is_nothrow_move_assignable<stb::mat32f>::value, "moving never throws");
static_assert(std::is_nothrow_move_constructible<stb::image8u>::value, "moving never throws");
static_assert(std::is_same<stb::mat32f::value_type, float>::value, "value_type is the cell type");

struct counting_owner {
    explicit counting_owner(int* r) : released(r) {}
    ~counting_owner() { ++*released; }
    int* released;
};

}  // namespace

UTEST(libstb_mat_tests, test_a_new_mat_is_zero_filled_and_knows_its_shape) {
    const stb::mat32f m(5, 3, 2);
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(5, m.width());
    EXPECT_EQ(3, m.height());
    EXPECT_EQ(5, m.cols());
    EXPECT_EQ(3, m.rows());
    EXPECT_EQ(2, m.channels());
    EXPECT_EQ(std::size_t(10), m.stride());
    EXPECT_EQ(std::size_t(30), m.size());
    EXPECT_EQ(std::size_t(30 * sizeof(float)), m.size_bytes());
    for (const float v : m) EXPECT_EQ(0.0f, v);
}

UTEST(libstb_mat_tests, test_one_channel_is_the_default_and_more_than_four_channels_are_fine) {
    EXPECT_EQ(1, stb::mat8u(2, 2).channels());
    const stb::mat32f many(2, 2, 6);
    EXPECT_EQ(6, many.channels());
    EXPECT_EQ(std::size_t(24), many.size());
    ASSERT_THROWS(many.format(), std::invalid_argument);  // 6 channels are not a pixel_format
    EXPECT_TRUE(stb::mat32f(2, 2, stb::pixel_format::rgb).format() == stb::pixel_format::rgb);
}

UTEST(libstb_mat_tests, test_an_empty_mat_has_no_cells) {
    const stb::mat32f e;
    EXPECT_TRUE(e.empty());
    EXPECT_EQ(0, e.width());
    EXPECT_EQ(0, e.channels());
    EXPECT_EQ(std::size_t(0), e.size());
    EXPECT_TRUE(e.begin() == e.end());
    EXPECT_TRUE(e.copy().empty());
    EXPECT_TRUE(stb::cast<std::uint8_t>(e).empty());
    EXPECT_TRUE(stb::cast<float>(stb::mat8u()).empty());
}

UTEST(libstb_mat_tests, test_a_list_of_rows_makes_a_one_channel_mat) {
    const stb::mat32f k = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    ASSERT_EQ(3, k.width());
    ASSERT_EQ(2, k.height());
    EXPECT_EQ(1, k.channels());
    EXPECT_EQ(1.0f, k(0, 0));
    EXPECT_EQ(3.0f, k(2, 0));  // (x, y): column 2 of row 0
    EXPECT_EQ(4.0f, k(0, 1));
    EXPECT_EQ(6.0f, k(2, 1));
    ASSERT_THROWS((stb::mat32f{{1.0f, 2.0f}, {3.0f}}), std::invalid_argument);  // ragged
    ASSERT_THROWS((stb::mat32f{{}}), std::invalid_argument);                    // no columns
}

UTEST(libstb_mat_tests, test_element_access_is_row_major_and_interleaved) {
    stb::mat32f m(3, 2, 2);
    m(2, 1, 1) = 7.5f;
    m(0, 0, 0) = -1.0f;
    // (x=2, y=1, c=1) of a 3x2 two-channel mat: row 1 starts at 6, pixel 2 at +4, channel 1 at +1
    EXPECT_EQ(7.5f, m.data()[11]);
    EXPECT_EQ(-1.0f, m.data()[0]);
    EXPECT_TRUE(m.row(1) == m.data() + 6);
    const stb::mat32f& c = m;
    EXPECT_EQ(7.5f, c(2, 1, 1));
}

UTEST(libstb_mat_tests, test_fill_sets_every_value) {
    stb::mat32i m(3, 3);
    m.fill(-4);
    for (const std::int32_t v : m) EXPECT_EQ(-4, v);
}

UTEST(libstb_mat_tests, test_bad_sizes_throw) {
    ASSERT_THROWS(stb::mat32f(0, 4), std::invalid_argument);
    ASSERT_THROWS(stb::mat32f(4, -1), std::invalid_argument);
    ASSERT_THROWS(stb::mat32f(4, 4, 0), std::invalid_argument);
    ASSERT_THROWS(stb::mat32f(2, 2, 3, std::vector<float>(11)), std::invalid_argument);
    ASSERT_THROWS(stb::mat32f::wrap(2, 2, 1, nullptr), std::invalid_argument);
    ASSERT_THROWS(stb::mat32f((std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)(), 4), std::invalid_argument);
}

UTEST(libstb_mat_tests, test_a_vector_is_adopted_without_changing_the_values) {
    const std::vector<float> v = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, -3.0f};
    const stb::mat32f m(3, 1, 2, v);
    ASSERT_EQ(std::size_t(6), m.size());
    for (std::size_t i = 0; i < v.size(); ++i) EXPECT_EQ(v[i], m.data()[i]);
}

UTEST(libstb_mat_tests, test_a_move_empties_the_source) {
    stb::mat32f a(4, 4, 4);
    a(1, 1, 2) = 0.5f;
    const float* p = a.data();
    stb::mat32f b(std::move(a));
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(0, a.width());
    EXPECT_EQ(0, a.channels());
    EXPECT_TRUE(b.data() == p);
    EXPECT_EQ(0.5f, b(1, 1, 2));
    stb::mat32f c;
    c = std::move(b);
    EXPECT_TRUE(b.empty());
    EXPECT_TRUE(c.data() == p);
    EXPECT_EQ(4, c.width());
}

UTEST(libstb_mat_tests, test_copy_owns_its_own_values) {
    stb::mat32f a(2, 2);
    a(0, 0) = 1.0f;
    stb::mat32f b = a.copy();
    EXPECT_TRUE(b.data() != a.data());
    EXPECT_EQ(1.0f, b(0, 0));
    b(0, 0) = 9.0f;
    EXPECT_EQ(1.0f, a(0, 0));
}

UTEST(libstb_mat_tests, test_wrap_views_memory_and_releases_the_keep_alive_with_the_last_owner) {
    std::vector<float> mem = {0.1f, 0.2f, 0.3f, 0.4f};
    int released = 0;
    {
        auto owner = std::make_shared<counting_owner>(&released);
        stb::mat32f w = stb::mat32f::wrap(2, 2, 1, mem.data(), owner);
        owner.reset();
        EXPECT_EQ(0, released);
        EXPECT_TRUE(w.data() == mem.data());
        w(1, 1) = 4.0f;
        EXPECT_EQ(4.0f, mem[3]);  // a view: the write went to the caller's memory
        stb::mat32f own = w.copy();
        EXPECT_TRUE(own.data() != mem.data());
        stb::mat32f moved(std::move(w));
        EXPECT_EQ(0, released);  // moving keeps it alive
    }
    EXPECT_EQ(1, released);
}

UTEST(libstb_mat_tests, test_an_image_is_a_mat_and_a_mat_can_become_an_image) {
    stb::image8u img(3, 2, stb::pixel_format::rgb);
    const stb::mat8u& as_mat = img;
    EXPECT_EQ(3, as_mat.channels());
    EXPECT_EQ(std::size_t(18), as_mat.size());
    stb::mat8u plain(2, 2, 4, std::vector<std::uint8_t>(16, 7));
    stb::image8u from_mat(std::move(plain));
    EXPECT_TRUE(from_mat.format() == stb::pixel_format::rgba);
    EXPECT_EQ(7, int(from_mat(1, 1, 3)));
    EXPECT_TRUE(plain.empty());
}

// ---------------------------------------------------------------- 8 bits <-> float

UTEST(libstb_mat_tests, test_cast_to_float_divides_by_255_and_keeps_the_shape) {
    const stb::image8u src(2, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{0, 51, 255, 102, 153, 204});
    const stb::mat32f f = stb::cast<float>(src);
    ASSERT_EQ(2, f.width());
    ASSERT_EQ(1, f.height());
    ASSERT_EQ(3, f.channels());
    const float expect[6] = {0.0f, 51.0f / 255.0f, 1.0f, 102.0f / 255.0f, 153.0f / 255.0f, 204.0f / 255.0f};
    for (std::size_t i = 0; i < 6; ++i) EXPECT_EQ(expect[i], f.data()[i]);
}

UTEST(libstb_mat_tests, test_cast_is_a_method_of_image_and_a_function_of_any_mat) {
    const stb::image8u src(2, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{0, 51, 255, 102, 153, 204});
    const stb::mat32f by_method = src.cast<float>();
    const stb::mat32f by_function = stb::cast<float>(src);
    const stb::mat32f of_the_base = stb::cast<float>(static_cast<const stb::mat8u&>(src));
    ASSERT_EQ(std::size_t(6), by_method.size());
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(by_function.data()[i], by_method.data()[i]);
        EXPECT_EQ(by_function.data()[i], of_the_base.data()[i]);
    }
    const stb::image8u back = by_method.size() == 6 ? stb::cast<std::uint8_t>(by_method) : stb::image8u();
    EXPECT_EQ(int(src.format()), int(back.format()));
}

// cast<To> for every pair of integer and float types: read as value / max, written as round(clamp(value * max, lowest, max))
UTEST(libstb_mat_tests, test_cast_between_unsigned_integers_scales_by_the_maxima) {
    const stb::mat8u a(3, 1, 1, std::vector<std::uint8_t>{0, 100, 255});
    const stb::mat<std::uint16_t> b = stb::cast<std::uint16_t>(a);
    ASSERT_EQ(std::size_t(3), b.size());
    EXPECT_EQ(0, int(b.data()[0]));
    EXPECT_EQ(100 * 257, int(b.data()[1]));
    EXPECT_EQ(65535, int(b.data()[2]));
    const stb::image8u back = stb::cast<std::uint8_t>(b);  // every 8-bit value survives 8 -> 16 -> 8
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(int(a.data()[i]), int(back.data()[i]));
    const stb::mat<std::uint16_t> w(5, 1, 1, std::vector<std::uint16_t>{0, 128, 129, 32768, 65535});
    const stb::image8u n = stb::cast<std::uint8_t>(w);  // a value / 257: 128 is 0.498, 129 is 0.502, 32768 is 127.5019
    EXPECT_EQ(0, int(n.data()[0]));
    EXPECT_EQ(0, int(n.data()[1]));
    EXPECT_EQ(1, int(n.data()[2]));
    EXPECT_EQ(128, int(n.data()[3]));
    EXPECT_EQ(255, int(n.data()[4]));
    const stb::mat<std::uint32_t> big = stb::cast<std::uint32_t>(a);
    EXPECT_EQ(std::uint32_t(4294967295u), big.data()[2]);
}

UTEST(libstb_mat_tests, test_cast_of_an_integer_to_float_and_back_for_16_and_32_bits) {
    const stb::mat<std::uint16_t> w(3, 1, 1, std::vector<std::uint16_t>{0, 1234, 65535});
    const stb::mat32f f = stb::cast<float>(w);
    EXPECT_EQ(0.0f, f.data()[0]);
    EXPECT_NEAR(1234.0 / 65535.0, double(f.data()[1]), 1e-7);
    EXPECT_EQ(1.0f, f.data()[2]);
    const stb::mat<std::uint16_t> again = stb::cast<std::uint16_t>(f);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(int(w.data()[i]), int(again.data()[i]));
    const stb::mat<std::int32_t> i32(3, 1, 1, std::vector<std::int32_t>{0, 1000000, 2147483647});
    const stb::mat<double> d = stb::cast<double>(i32);  // in double precision: nothing is lost
    EXPECT_EQ(0.0, d.data()[0]);
    EXPECT_NEAR(1000000.0 / 2147483647.0, d.data()[1], 1e-15);
    EXPECT_EQ(1.0, d.data()[2]);
    const stb::mat<std::int32_t> i32b = stb::cast<std::int32_t>(d);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(i32.data()[i], i32b.data()[i]);
}

UTEST(libstb_mat_tests, test_cast_of_a_signed_type_keeps_the_sign_and_clamps_at_the_unsigned_zero) {
    const stb::mat<std::int16_t> s(4, 1, 1, std::vector<std::int16_t>{-32768, -16384, 0, 32767});
    const stb::mat32f f = stb::cast<float>(s);
    EXPECT_NEAR(-32768.0 / 32767.0, double(f.data()[0]), 1e-6);  // a hair below -1
    EXPECT_NEAR(-16384.0 / 32767.0, double(f.data()[1]), 1e-6);
    EXPECT_EQ(0.0f, f.data()[2]);
    EXPECT_EQ(1.0f, f.data()[3]);
    const stb::mat<std::int16_t> again = stb::cast<std::int16_t>(f);  // the round trip is exact, -32768 included
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(int(s.data()[i]), int(again.data()[i]));
    const stb::image8u u = stb::cast<std::uint8_t>(s);  // negative values are below black
    EXPECT_EQ(0, int(u.data()[0]));
    EXPECT_EQ(0, int(u.data()[1]));
    EXPECT_EQ(0, int(u.data()[2]));
    EXPECT_EQ(255, int(u.data()[3]));
    const stb::mat<std::int16_t> from_float = stb::cast<std::int16_t>(stb::mat32f(3, 1, 1, std::vector<float>{-2.0f, 0.5f, 3.0f}));
    EXPECT_EQ(-32768, int(from_float.data()[0]));  // clamped at the lowest value
    EXPECT_EQ(16384, int(from_float.data()[1]));   // 16383.5 rounds away from zero
    EXPECT_EQ(32767, int(from_float.data()[2]));
}

UTEST(libstb_mat_tests, test_cast_turns_nan_into_zero_and_clamps_in_every_integer_type) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const stb::mat32f f(4, 1, 1, std::vector<float>{nan, -1.0f, 2.0f, 0.5f});
    const stb::mat<std::uint16_t> u = stb::cast<std::uint16_t>(f);
    EXPECT_EQ(0, int(u.data()[0]));
    EXPECT_EQ(0, int(u.data()[1]));
    EXPECT_EQ(65535, int(u.data()[2]));
    EXPECT_EQ(32768, int(u.data()[3]));
    const stb::mat<std::int32_t> i = stb::cast<std::int32_t>(f);
    EXPECT_EQ(0, i.data()[0]);
    EXPECT_EQ(-2147483647, i.data()[1]);  // -1.0 is minus white (-max); only below it the value is clamped at the lowest
    EXPECT_EQ(2147483647, i.data()[2]);
    const stb::mat<std::int32_t> below = stb::cast<std::int32_t>(stb::mat<double>(1, 1, 1, std::vector<double>{-1.5}));
    EXPECT_EQ(-2147483647 - 1, below.data()[0]);
}

UTEST(libstb_mat_tests, test_cast_keeps_the_size_and_the_channels_and_copies_the_same_type) {
    stb::mat<std::uint16_t> m(5, 3, 3);
    for (std::size_t i = 0; i < m.size(); ++i) m.data()[i] = std::uint16_t(i * 100);
    const stb::mat<std::uint16_t> same = stb::cast<std::uint16_t>(m);
    EXPECT_EQ(5, same.width());
    EXPECT_EQ(3, same.height());
    EXPECT_EQ(3, same.channels());
    EXPECT_TRUE(same.data() != m.data());
    for (std::size_t i = 0; i < m.size(); ++i) EXPECT_EQ(int(m.data()[i]), int(same.data()[i]));
    EXPECT_TRUE(stb::cast<std::uint16_t>(stb::mat<std::uint8_t>()).empty());
    const stb::mat<double> d = stb::cast<double>(stb::mat32f(2, 1, 1, std::vector<float>{0.25f, 0.75f}));
    EXPECT_EQ(0.25, d.data()[0]);
    EXPECT_EQ(0.75, d.data()[1]);
}

UTEST(libstb_mat_tests, test_cast_to_the_same_type_is_a_copy) {
    const stb::image8u src(2, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{0, 51, 255, 102, 153, 204});
    const stb::image8u same = src.cast<std::uint8_t>();
    ASSERT_EQ(src.size(), same.size());
    EXPECT_EQ(int(src.format()), int(same.format()));
    EXPECT_TRUE(same.data() != src.data());
    for (std::size_t i = 0; i < src.size(); ++i) EXPECT_EQ(int(src.data()[i]), int(same.data()[i]));
    const stb::mat32f f(2, 1, 1, std::vector<float>{0.25f, 0.75f});
    const stb::mat32f g = stb::cast<float>(f);
    EXPECT_TRUE(g.data() != f.data());
    EXPECT_EQ(0.25f, g.data()[0]);
    EXPECT_EQ(0.75f, g.data()[1]);
    EXPECT_TRUE(stb::cast<float>(stb::mat32f()).empty());
    EXPECT_TRUE(stb::cast<std::uint8_t>(stb::mat8u()).empty());
}

UTEST(libstb_mat_tests, test_every_8_bit_value_survives_a_round_trip) {
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[std::size_t(i)] = std::uint8_t(i);
    const stb::image8u src(16, 16, stb::pixel_format::gray, all);
    const stb::image8u back = stb::cast<std::uint8_t>(stb::cast<float>(src));
    ASSERT_EQ(std::size_t(256), back.size());
    for (int i = 0; i < 256; ++i) EXPECT_EQ(int(all[std::size_t(i)]), int(back.data()[i]));
}

UTEST(libstb_mat_tests, test_cast_to_uint8_rounds_to_nearest_clamps_and_turns_nan_into_zero) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const std::vector<float> v = {0.0f, 1.0f, -0.5f, 1.5f, 0.5f / 255.0f * 0.99f, 0.5f / 255.0f * 1.01f,
                                  nan,  inf,  -inf,  0.5f};
    const stb::mat32f f(5, 2, 1, v);
    const stb::image8u out = stb::cast<std::uint8_t>(f);
    const int expect[10] = {0, 255, 0, 255,
                            0,    // just under half a step rounds down
                            1,    // just over half a step rounds up
                            0, 255, 0,
                            128};  // 0.5 * 255 = 127.5 rounds up
    ASSERT_EQ(std::size_t(10), out.size());
    for (std::size_t i = 0; i < 10; ++i) EXPECT_EQ(expect[i], int(out.data()[i]));
}

UTEST(libstb_mat_tests, test_the_conversion_keeps_every_pixel_format_and_refuses_more_than_four_channels) {
    for (const stb::pixel_format fmt : {stb::pixel_format::gray, stb::pixel_format::gray_alpha, stb::pixel_format::rgb,
                                        stb::pixel_format::rgba}) {
        const stb::image8u src(3, 2, fmt);
        const stb::mat32f f = stb::cast<float>(src);
        EXPECT_EQ(stb::channel_count(fmt), f.channels());
        EXPECT_TRUE(stb::cast<std::uint8_t>(f).format() == fmt);
    }
    ASSERT_THROWS(stb::cast<std::uint8_t>(stb::mat32f(2, 2, 5)), std::invalid_argument);
    EXPECT_EQ(5, stb::cast<float>(stb::mat8u(2, 2, 5)).channels());  // cast<float> takes any mat8u
}
