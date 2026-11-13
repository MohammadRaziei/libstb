#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "test_common.hpp"

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
    EXPECT_TRUE(stb::to_8u(e).empty());
    EXPECT_TRUE(stb::to_f32(stb::mat8u()).empty());
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

UTEST(libstb_mat_tests, test_to_f32_divides_by_255_and_keeps_the_shape) {
    const stb::image8u src(2, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{0, 51, 255, 102, 153, 204});
    const stb::mat32f f = stb::to_f32(src);
    ASSERT_EQ(2, f.width());
    ASSERT_EQ(1, f.height());
    ASSERT_EQ(3, f.channels());
    const float expect[6] = {0.0f, 51.0f / 255.0f, 1.0f, 102.0f / 255.0f, 153.0f / 255.0f, 204.0f / 255.0f};
    for (std::size_t i = 0; i < 6; ++i) EXPECT_EQ(expect[i], f.data()[i]);
}

UTEST(libstb_mat_tests, test_every_8_bit_value_survives_a_round_trip) {
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[std::size_t(i)] = std::uint8_t(i);
    const stb::image8u src(16, 16, stb::pixel_format::gray, all);
    const stb::image8u back = stb::to_8u(stb::to_f32(src));
    ASSERT_EQ(std::size_t(256), back.size());
    for (int i = 0; i < 256; ++i) EXPECT_EQ(int(all[std::size_t(i)]), int(back.data()[i]));
}

UTEST(libstb_mat_tests, test_to_8u_rounds_to_nearest_clamps_and_turns_nan_into_zero) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const std::vector<float> v = {0.0f, 1.0f, -0.5f, 1.5f, 0.5f / 255.0f * 0.99f, 0.5f / 255.0f * 1.01f,
                                  nan,  inf,  -inf,  0.5f};
    const stb::mat32f f(5, 2, 1, v);
    const stb::image8u out = stb::to_8u(f);
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
        const stb::mat32f f = stb::to_f32(src);
        EXPECT_EQ(stb::channel_count(fmt), f.channels());
        EXPECT_TRUE(stb::to_8u(f).format() == fmt);
    }
    ASSERT_THROWS(stb::to_8u(stb::mat32f(2, 2, 5)), std::invalid_argument);
    EXPECT_EQ(5, stb::to_f32(stb::mat8u(2, 2, 5)).channels());  // to_f32 takes any mat8u
}
