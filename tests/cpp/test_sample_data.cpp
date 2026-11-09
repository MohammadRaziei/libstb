#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "test_common.hpp"

// The pictures and the font of data/ (data/README.md): real photographs, an RGBA picture and a TrueType font with
// GSUB and GPOS. tests/data/ has synthetic files for exact numbers; these find what hand-made ones cannot.

namespace {

struct picture {
    const char* name;
    int width, height, channels;
};

const picture pictures[] = {
    {"camera.png", 512, 512, 1},   {"astronaut.png", 512, 512, 3}, {"chelsea.png", 451, 300, 3},
    {"rocket.jpg", 640, 427, 3},   {"badge.png", 160, 160, 4},
};

std::string image_path(const char* name) { return std::string(LIBSTB_DATA_DIR) + "/images/" + name; }
stb::font regular() { return stb::font::open(std::string(LIBSTB_DATA_DIR) + "/fonts/Vazirmatn-Regular.ttf"); }
stb::font bold() { return stb::font::open(std::string(LIBSTB_DATA_DIR) + "/fonts/Vazirmatn-Bold.ttf"); }

bool same_pixels(const stb::image& a, const stb::image& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

// Mean absolute difference per pixel and channel as a fraction of 255: the pixel-wise measure of these tests.
double nmae(const stb::image& a, const stb::image& b) {
    double sum = 0;
    const std::size_t n = a.size_bytes();
    for (std::size_t i = 0; i < n; ++i) sum += std::abs(double(a.data()[i]) - double(b.data()[i]));
    return sum / double(n) / 255.0;
}

double std_dev(const stb::image& a) {
    double sum = 0, sq = 0;
    const std::size_t n = a.size_bytes();
    for (std::size_t i = 0; i < n; ++i) {
        const double v = a.data()[i];
        sum += v;
        sq += v * v;
    }
    const double mean = sum / double(n);
    return std::sqrt(sq / double(n) - mean * mean);
}

using U = std::vector<std::uint32_t>;
U clusters(const std::vector<stb::shaped_glyph>& g) {
    U out;
    for (const stb::shaped_glyph& s : g) out.push_back(s.cluster);
    return out;
}

const char* SALAM = "\u0633\u0644\u0627\u0645";   // س ل ا م
const char* DONYA = "\u062f\u0646\u06cc\u0627";   // د ن ی ا

}  // namespace

// ---- pictures -----------------------------------------------------------------------------------------------

UTEST(libstb_sample_data_tests, test_every_header_says_what_the_decoded_picture_is) {
    for (const picture& p : pictures) {
        const stb::image_info info = stb::image_info::read_file(image_path(p.name));
        ASSERT_TRUE(info.width == p.width && info.height == p.height && info.channels() == p.channels);
        const stb::image img = stb::image::open(image_path(p.name));
        ASSERT_TRUE(img.width() == p.width && img.height() == p.height && img.channels() == p.channels);
        ASSERT_EQ(std::size_t(p.width) * p.height * p.channels, img.size_bytes());
    }
}

UTEST(libstb_sample_data_tests, test_the_pictures_are_not_flat) {
    // a decoder that returned zeros or one colour would pass every other test of this file
    for (const picture& p : pictures) ASSERT_TRUE(std_dev(stb::image::open(image_path(p.name))) > 10.0);
}

UTEST(libstb_sample_data_tests, test_decoding_from_memory_is_decoding_from_the_file) {
    for (const picture& p : pictures) {
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream f(image_path(p.name), std::ios::binary);
            ASSERT_TRUE(f.good());
            const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            bytes.resize(raw.size());
            std::memcpy(bytes.data(), raw.data(), raw.size());
        }
        ASSERT_TRUE(same_pixels(stb::image::decode(bytes.data(), bytes.size()), stb::image::open(image_path(p.name))));
    }
}

UTEST(libstb_sample_data_tests, test_lossless_writers_give_back_every_pixel) {
    for (const picture& p : pictures) {
        const stb::image img = stb::image::open(image_path(p.name));
        for (int compression : {1, 9}) {
            const std::vector<std::uint8_t> png = img.to_png(compression);
            ASSERT_TRUE(same_pixels(stb::image::decode(png.data(), png.size()), img));
        }
        for (bool rle : {true, false}) {
            const std::vector<std::uint8_t> tga = img.to_tga(rle);
            ASSERT_TRUE(same_pixels(stb::image::decode(tga.data(), tga.size()), img));
        }
        if (img.channels() >= 3) {  // BMP holds colour: a gray picture would come back as RGB
            const std::vector<std::uint8_t> bmp = img.to_bmp();
            ASSERT_TRUE(same_pixels(stb::image::decode(bmp.data(), bmp.size()), img));
        }
    }
}

UTEST(libstb_sample_data_tests, test_the_jpeg_writer_keeps_the_picture_close) {
    // measured at worst 0.0154 at quality 50 and 0.0093 at quality 90
    for (const char* name : {"astronaut.png", "chelsea.png", "rocket.jpg"}) {
        const stb::image img = stb::image::open(image_path(name));
        for (auto [quality, ceiling] : {std::pair<int, double>{50, 0.02}, {90, 0.012}}) {
            const std::vector<std::uint8_t> jpg = img.to_jpg(quality);
            stb::load_options as_rgb;
            as_rgb.format = stb::pixel_format::rgb;
            const stb::image back = stb::image::decode(jpg.data(), jpg.size(), as_rgb);
            ASSERT_TRUE(nmae(back, img) < ceiling);
        }
    }
}

UTEST(libstb_sample_data_tests, test_resize_thumbnail_crop_and_turns_on_a_photograph) {
    const stb::image img = stb::image::open(image_path("chelsea.png"));   // 451 x 300
    for (const char* filter : {"nearest", "linear", "cubic", "bspline", "mitchell", "box", "auto"}) {
        const stb::image out = img.resize(97, 61, filter);
        ASSERT_TRUE(out.width() == 97 && out.height() == 61 && out.channels() == 3);
    }
    const stb::image thumb = img.thumbnail(100, 100);
    ASSERT_EQ(100, thumb.width());
    ASSERT_TRUE(std::abs(thumb.height() - 100.0 * 300 / 451) <= 1.0);
    ASSERT_TRUE(img.thumbnail(1000, 1000).width() == 451);   // it never enlarges

    const stb::image piece = img.crop(100, 50, 200, 120);
    ASSERT_TRUE(piece.width() == 200 && piece.height() == 120);
    for (int y : {0, 60, 119})
        for (int x : {0, 100, 199})
            for (int c = 0; c < 3; ++c) ASSERT_EQ(int(img(100 + x, 50 + y, c)), int(piece(x, y, c)));

    ASSERT_TRUE(same_pixels(img.rotate90(1).rotate90(-1), img));
    ASSERT_TRUE(same_pixels(img.rotate90(4), img));
}

UTEST(libstb_sample_data_tests, test_shrinking_then_growing_stays_close_to_the_original) {
    const stb::image img = stb::image::open(image_path("astronaut.png"));
    const stb::image there = img.resize(256, 256, "mitchell").resize(512, 512, "cubic");
    ASSERT_TRUE(nmae(there, img) < 0.025);   // measured 0.018 on the astronaut
}

UTEST(libstb_sample_data_tests, test_every_pixel_format_of_a_real_picture) {
    for (const picture& p : pictures) {
        stb::load_options opt;
        for (stb::pixel_format f : {stb::pixel_format::gray, stb::pixel_format::gray_alpha, stb::pixel_format::rgb,
                                    stb::pixel_format::rgba}) {
            opt.format = f;
            ASSERT_EQ(stb::channel_count(f), stb::image::open(image_path(p.name), opt).channels());
        }
    }
}

// ---- alpha: the one picture that has it -------------------------------------------------------------------

UTEST(libstb_sample_data_tests, test_the_badge_has_transparent_corners_and_a_soft_rim) {
    const stb::image badge = stb::image::open(image_path("badge.png"));
    ASSERT_EQ(0, int(badge(0, 0, 3)));
    ASSERT_EQ(0, int(badge(159, 0, 3)));
    ASSERT_EQ(0, int(badge(0, 159, 3)));
    ASSERT_EQ(0, int(badge(159, 159, 3)));
    int clear = 0, solid = 0, soft = 0;
    for (int y = 0; y < badge.height(); ++y)
        for (int x = 0; x < badge.width(); ++x) {
            const int a = badge(x, y, 3);
            (a == 0 ? clear : a == 255 ? solid : soft)++;
        }
    ASSERT_TRUE(clear > 1000 && solid > 1000 && soft > 100);
}

UTEST(libstb_sample_data_tests, test_blend_over_leaves_transparent_pixels_alone_and_shows_opaque_ones) {
    const stb::image badge = stb::image::open(image_path("badge.png"));
    const stb::image background = stb::image::open(image_path("chelsea.png"));
    stb::image placed = background.copy();
    placed.blend(badge, 40, 30);
    for (int y = 0; y < background.height(); ++y)
        for (int x = 0; x < background.width(); ++x) {
            const int bx = x - 40, by = y - 30;
            const bool inside = bx >= 0 && by >= 0 && bx < badge.width() && by < badge.height();
            const int alpha = inside ? badge(bx, by, 3) : 0;
            for (int c = 0; c < 3; ++c) {
                if (alpha == 0) ASSERT_EQ(int(background(x, y, c)), int(placed(x, y, c)));        // nothing changed
                if (alpha == 255) ASSERT_EQ(int(badge(bx, by, c)), int(placed(x, y, c)));         // the badge shows
            }
        }
}

// ---- SIMD backends and threads must not change a single byte -----------------------------------------------------

UTEST(libstb_sample_data_tests, test_every_simd_backend_gives_the_scalar_result_on_real_pictures) {
    stb::load_options rgba;
    rgba.format = stb::pixel_format::rgba;
    const stb::image base = stb::image::open(image_path("astronaut.png"), rgba);
    const stb::image overlay = stb::image::open(image_path("badge.png"));
    auto run = [&](const std::string& backend) {
        stb::simd_scope scope(backend);
        stb::image blended = base.copy();
        blended.blend(overlay, 20, 20, stb::blend_mode::normal, 0.7f);
        std::vector<stb::image> out;   // an initializer_list would copy, and an image is move-only
        out.push_back(std::move(blended));
        out.push_back(base.convert(stb::pixel_format::gray));
        out.push_back(base.resize(333, 211));
        return out;
    };
    const std::vector<stb::image> want = run("scalar");
    for (const std::string& backend : stb::simd_backends()) {
        const std::vector<stb::image> got = run(backend);
        for (std::size_t i = 0; i < want.size(); ++i) ASSERT_TRUE(same_pixels(got[i], want[i]));
    }
}

UTEST(libstb_sample_data_tests, test_the_thread_count_does_not_change_the_pixels) {
    const stb::image img = stb::image::open(image_path("astronaut.png"));
    const stb::image want = img.resize(1500, 1000, "cubic");
    for (int threads : {1, 2, 4}) {
        stb::thread_scope scope(threads);
        ASSERT_TRUE(same_pixels(img.resize(1500, 1000, "cubic"), want));
    }
}

// ---- the font ---------------------------------------------------------------------------------------------------

UTEST(libstb_sample_data_tests, test_the_font_is_what_data_readme_says) {
    const stb::font faces[] = {regular(), bold()};
    for (const stb::font& f : faces) {
        const stb::font_design_metrics d = f.design_metrics();
        ASSERT_TRUE(d.units_per_em == 2048 && d.ascent == 2100 && d.descent == -1100);
        ASSERT_EQ(std::uint32_t(1333), f.glyph_count());
    }
    const stb::font f = regular();
    for (char32_t ch : U"AZaz09,.!\u067e\u0686\u0698\u06af\u06f0\u06f9\u0627\u0628\u064e") ASSERT_TRUE(f.has_glyph(ch));
    ASSERT_FALSE(f.has_glyph(U'\U0001F600'));
    ASSERT_TRUE(bold().glyph_info(f.glyph_index(U'A')).advance > f.glyph_info(f.glyph_index(U'A')).advance);
}

UTEST(libstb_sample_data_tests, test_persian_joins_ligates_and_orders_like_the_synthetic_font_says) {
    const stb::font f = regular();
    ASSERT_TRUE(clusters(f.shape(SALAM)) == (U{6, 2, 0}));                         // م, the ligature لا, س
    ASSERT_EQ(std::size_t(1), f.shape("\u0644\u0627").size());
    const std::string mixed = std::string(SALAM) + " hello " + DONYA;              // SALAM 0-7, hello 9-13, DONYA 15-22
    ASSERT_TRUE(clusters(f.shape(mixed)) == (U{21, 19, 17, 15, 14, 9, 10, 11, 12, 13, 8, 6, 2, 0}));
    stb::shape_options ltr;
    ltr.direction = stb::text_direction::ltr;
    ASSERT_TRUE(clusters(f.shape(mixed, ltr)) == (U{6, 2, 0, 8, 9, 10, 11, 12, 13, 14, 21, 19, 17, 15}));

    const std::vector<stb::shaped_glyph> beh = f.shape("\u0628\u0628\u0628");     // initial, medial, final: all different
    ASSERT_EQ(std::size_t(3), beh.size());
    ASSERT_TRUE(beh[0].glyph != beh[1].glyph && beh[1].glyph != beh[2].glyph && beh[0].glyph != beh[2].glyph);
    ASSERT_TRUE(f.shape("\u0628")[0].glyph != beh[0].glyph);
}

UTEST(libstb_sample_data_tests, test_the_kerning_of_a_gpos_only_font) {
    const stb::font f = regular();
    const std::uint32_t a = f.glyph_index(U'A'), v = f.glyph_index(U'V');
    ASSERT_TRUE(f.glyph_kerning(a, v, stb::kerning_mode::automatic) < 0.0f);
    ASSERT_TRUE(f.glyph_kerning(a, v, stb::kerning_mode::kern_table) == 0.0f);     // no `kern` table: GPOS has the pair
    ASSERT_TRUE(f.glyph_kerning(a, v, stb::kerning_mode::none) == 0.0f);
    ASSERT_TRUE(f.shape("AV")[0].x_advance == f.glyph_info(a).advance + f.glyph_kerning(a, v, stb::kerning_mode::automatic));
    // font::kerning is the same number in pixels: pixel_height / (ascent - descent) pixels per unit
    ASSERT_NEAR(f.glyph_kerning(a, v, stb::kerning_mode::automatic) * 40.0f / 3200.0f, f.kerning(U'A', U'V', 40.0f), 1e-3f);
}

UTEST(libstb_sample_data_tests, test_a_vector_text_from_shape_and_outline_is_the_text_draw_text_draws) {
    const stb::font f = regular();
    const stb::font_design_metrics d = f.design_metrics();
    const float size = 40.f, scale = size / float(d.ascent - d.descent);          // draw_text's size: descent to ascent
    stb::image white(400, 90, stb::pixel_format::rgb);
    white.fill(stb::color(255, 255, 255));
    const stb::image ref = f.draw_text(white, "Hello, AV kerning", size, 10, 10, stb::color(0, 0, 0));

    stb::image img = white.copy();
    float pen = 0.f;
    for (const stb::shaped_glyph& g : f.shape("Hello, AV kerning")) {
        stb::draw_options options;
        options.transform = stb::matrix().translate(10.f + pen * scale, 10.f + float(d.ascent) * scale).scale(scale, -scale);
        stb::fill(img, stb::outline(f.outline(g.glyph)), stb::color(0, 0, 0), stb::fill_rule::non_zero, options);
        pen += g.x_advance;
    }
    double total = 0;
    for (std::size_t i = 0; i < img.size_bytes(); ++i) total += std::abs(int(img.data()[i]) - int(ref.data()[i]));
    ASSERT_TRUE(total / double(img.size_bytes()) < 3.0);                          // measured 1.5: whole-pixel glyph positions
}

UTEST(libstb_sample_data_tests, test_draw_text_puts_ink_inside_the_measured_box) {
    const stb::font f = regular();
    for (const std::string& text : {std::string("Hello"), std::string(SALAM) + " " + DONYA, std::string("\u067e\u0686\u0698\u06af")}) {
        const stb::text_size box = f.measure(text, 36.f);
        stb::image white(420, 110, stb::pixel_format::rgb);
        white.fill(stb::color(255, 255, 255));
        const stb::image out = f.draw_text(white, text, 36.f, 10, 10, stb::color(0, 0, 0));
        int min_x = out.width(), max_x = -1, min_y = out.height(), max_y = -1, ink = 0;
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x)
                if (out(x, y, 0) < 255) {
                    ++ink;
                    min_x = STB_MIN(min_x, x), max_x = STB_MAX(max_x, x);
                    min_y = STB_MIN(min_y, y), max_y = STB_MAX(max_y, y);
                }
        ASSERT_TRUE(ink > 100);
        ASSERT_TRUE(min_x >= 8 && max_x <= 10 + box.width + 2);
        ASSERT_TRUE(min_y >= 8 && max_y <= 10 + box.height + 2);
    }
}
