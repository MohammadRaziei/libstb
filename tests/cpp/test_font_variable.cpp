#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_common.hpp"

// Variable fonts: font::axes / with_variation / with_weight and font_face_info::weight_min / weight_max.
//
// The reference is fontTools, not this code. tests/data/libstb-variable-test.ttf is a subset of Open Sans
// (SIL OFL 1.1, tests/data/OFL-OpenSans.txt) that is still variable (fvar, avar, gvar, HVAR); the files
// libstb-variable-ref-*.ttf are static instances of it that fontTools' varLib.instancer made at known
// coordinates. The glyph order is the same in all of them, so glyph n of an instance of the variable font must
// give the outline and the advance of glyph n of the static file, up to the rounding to whole font units.

namespace {

std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }
stb::font variable() { return stb::font::open(data("libstb-variable-test.ttf")); }

// Largest coordinate difference between two outlines, or -1 when their commands differ in kind or number.
float outline_distance(const std::vector<stb::outline_command>& a, const std::vector<stb::outline_command>& b) {
    if (a.size() != b.size()) return -1.0f;
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].op != b[i].op) return -1.0f;
        for (int k = 0; k < 3; ++k) {
            worst = std::max(worst, std::fabs(a[i].p[k].x - b[i].p[k].x));
            worst = std::max(worst, std::fabs(a[i].p[k].y - b[i].p[k].y));
        }
    }
    return worst;
}

// Every glyph of `instance` against the same glyph of the static reference: "" when they agree, else what differs
// first (the UTEST macros cannot run outside a test body).
std::string compare_to_reference(const stb::font& instance, const stb::font& reference) {
    if (reference.glyph_count() != instance.glyph_count()) return "glyph count";
    for (std::uint32_t g = 0; g < reference.glyph_count(); ++g) {
        const std::string at = "glyph " + std::to_string(g) + ": ";
        const float distance = outline_distance(instance.outline(g), reference.outline(g));
        if (distance < 0.0f) return at + "different commands";
        if (distance > 1.01f) return at + "outline off by " + std::to_string(distance);
        const stb::glyph_metrics a = instance.glyph_info(g), b = reference.glyph_info(g);
        if (std::fabs(a.advance - b.advance) > 1.01f) return at + "advance " + std::to_string(a.advance) + " vs " + std::to_string(b.advance);
        if (a.has_outline != b.has_outline) return at + "has_outline";
        if (b.has_outline && (std::fabs(a.x_min - b.x_min) > 2.0f || std::fabs(a.x_max - b.x_max) > 2.0f ||
                              std::fabs(a.y_min - b.y_min) > 2.0f || std::fabs(a.y_max - b.y_max) > 2.0f))
            return at + "ink box";
    }
    return "";
}

stb::font bold_from_a_font_that_is_gone() { return variable().with_weight(700); }

}  // namespace

UTEST(libstb_font_variable_tests, test_axes_of_a_variable_and_of_a_static_font) {
    const std::vector<stb::font_axis> axes = variable().axes();
    ASSERT_EQ(2u, axes.size());
    ASSERT_TRUE(axes[0].tag == "wght" && axes[0].min == 300.0f && axes[0].default_value == 400.0f && axes[0].max == 800.0f);
    ASSERT_TRUE(axes[1].tag == "wdth" && axes[1].min == 75.0f && axes[1].default_value == 100.0f && axes[1].max == 100.0f);

    ASSERT_TRUE(stb::font::open(data("libstb-curves-test.ttf")).axes().empty());
}

UTEST(libstb_font_variable_tests, test_the_default_instance_is_the_file_as_it_is) {
    const stb::font f = variable();
    const stb::font same = f.with_variation("wght", 400.0f);  // the default value: nothing moves
    for (std::uint32_t g = 0; g < f.glyph_count(); ++g) {
        ASSERT_TRUE(outline_distance(f.outline(g), same.outline(g)) == 0.0f);
        ASSERT_TRUE(f.glyph_info(g).advance == same.glyph_info(g).advance);
    }
}

UTEST(libstb_font_variable_tests, test_every_glyph_matches_the_static_instances_of_fonttools) {
    const stb::font f = variable();
    ASSERT_STREQ("", compare_to_reference(f.with_weight(300), stb::font::open(data("libstb-variable-ref-w300.ttf"))).c_str());
    ASSERT_STREQ("", compare_to_reference(f.with_weight(700), stb::font::open(data("libstb-variable-ref-w700.ttf"))).c_str());
    // the far corner of both axes, and a point inside the range of both (avar maps it, tuples are intermediate)
    ASSERT_STREQ("", compare_to_reference(f.with_variation("wght", 800.0f).with_variation("wdth", 75.0f),
                             stb::font::open(data("libstb-variable-ref-w800-wd75.ttf"))).c_str());
    ASSERT_STREQ("", compare_to_reference(f.with_variation("wdth", 90.0f).with_variation("wght", 350.0f),
                             stb::font::open(data("libstb-variable-ref-w350-wd90.ttf"))).c_str());
}

UTEST(libstb_font_variable_tests, test_composite_glyphs_follow_the_axes) {
    // e acute and A ring are composites (a base and an accent): the loop above covers them, here the accent is
    // checked to move at all
    const stb::font f = variable();
    const std::uint32_t g = f.glyph_index(U'\u00e9');
    ASSERT_TRUE(g != 0);
    ASSERT_TRUE(outline_distance(f.outline(g), f.with_weight(800).outline(g)) > 5.0f);
}

UTEST(libstb_font_variable_tests, test_bold_is_wider_and_the_box_grows_with_the_outline) {
    const stb::font f = variable();
    const stb::font bold = f.with_weight(800);
    const std::uint32_t g = f.glyph_index(U'H');
    ASSERT_TRUE(bold.glyph_info(g).advance > f.glyph_info(g).advance);
    // the pixels: a heavier weight covers more of the bitmap (the box used to size it follows the outline)
    const stb::text_size a = f.measure("Hamburgefonstiv", 32.0f), b = bold.measure("Hamburgefonstiv", 32.0f);
    ASSERT_TRUE(b.width > a.width);
}

UTEST(libstb_font_variable_tests, test_with_variation_leaves_the_original_alone_and_chains) {
    const stb::font f = variable();
    const std::uint32_t g = f.glyph_index(U'a');
    const float before = f.glyph_info(g).advance;
    const stb::font light = f.with_weight(300);
    const stb::font light_narrow = light.with_variation("wdth", 75.0f);  // keeps the weight set so far
    ASSERT_TRUE(f.glyph_info(g).advance == before);
    ASSERT_EQ(300, light.weight());
    ASSERT_EQ(300, light_narrow.weight());
    ASSERT_TRUE(light_narrow.glyph_info(g).advance < light.glyph_info(g).advance);
}

UTEST(libstb_font_variable_tests, test_weight_and_the_range_of_a_face) {
    const stb::font f = variable();
    ASSERT_EQ(400, f.weight());
    ASSERT_EQ(700, f.with_weight(700).weight());
    ASSERT_EQ(800, f.with_weight(5000).weight());  // clamped to the axis
    ASSERT_EQ(300, f.with_weight(1).weight());

    const std::vector<stb::font_face_info> info = stb::font::scan(data("libstb-variable-test.ttf"));
    ASSERT_EQ(1u, info.size());
    ASSERT_TRUE(info[0].weight_min == 300 && info[0].weight_max == 800 && info[0].weight == 400);

    const std::vector<stb::font_face_info> fixed = stb::font::scan(data("libstb-curves-test.ttf"));
    ASSERT_TRUE(fixed[0].weight_min == fixed[0].weight && fixed[0].weight_max == fixed[0].weight);
}

UTEST(libstb_font_variable_tests, test_a_static_font_and_bad_arguments) {
    const stb::font fixed = stb::font::open(data("libstb-curves-test.ttf"));
    ASSERT_EQ(fixed.weight(), fixed.with_weight(700).weight());  // no weight axis: the same font
    ASSERT_THROWS(fixed.with_variation("wght", 700.0f), std::invalid_argument);
    ASSERT_THROWS(variable().with_variation("opsz", 12.0f), std::invalid_argument);  // an axis this font lacks
    ASSERT_THROWS(variable().with_variation("wg", 700.0f), std::invalid_argument);   // not four characters
    ASSERT_THROWS(variable().with_variation("wght", std::nanf("")), std::invalid_argument);
}

UTEST(libstb_font_variable_tests, test_an_instance_outlives_the_font_it_came_from) {
    const stb::font instance = bold_from_a_font_that_is_gone();  // the font it came from no longer exists
    ASSERT_TRUE(instance.glyph_info(instance.glyph_index(U'H')).advance > 0.0f);
    ASSERT_EQ(700, instance.weight());
}

// A font is untrusted input. Damage the tables the variation code reads (fvar, avar, gvar, HVAR) at random and use
// the result at several weights: every outcome is fine (an exception, odd glyphs) except a crash or, under the
// sanitizers, an out-of-bounds read.
UTEST(libstb_font_variable_tests, test_damaged_variation_tables_never_read_out_of_bounds) {
    std::vector<std::uint8_t> original;
    {
        std::FILE* fp = std::fopen(data("libstb-variable-test.ttf").c_str(), "rb");
        ASSERT_TRUE(fp != nullptr);
        std::uint8_t buffer[4096];
        for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, fp)) > 0;) original.insert(original.end(), buffer, buffer + n);
        std::fclose(fp);
    }
    const auto be32 = [&](std::size_t at) {
        return (std::uint32_t(original[at]) << 24) | (std::uint32_t(original[at + 1]) << 16) | (std::uint32_t(original[at + 2]) << 8) |
               std::uint32_t(original[at + 3]);
    };
    struct range {
        std::size_t offset, length;
    };
    std::vector<range> tables;  // where fvar, avar, gvar and HVAR are
    const std::size_t count = (std::size_t(original[4]) << 8) | original[5];
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 12 + 16 * i;
        const std::string tag(reinterpret_cast<const char*>(&original[record]), 4);
        if (tag == "fvar" || tag == "avar" || tag == "gvar" || tag == "HVAR") tables.push_back({be32(record + 8), be32(record + 12)});
    }
    ASSERT_EQ(4u, tables.size());

    std::uint32_t state = 12345u;
    const auto next = [&] { return state = state * 1664525u + 1013904223u; };
    for (int round = 0; round < 400; ++round) {
        std::vector<std::uint8_t> damaged = original;
        for (int k = 0, n = 1 + int(next() % 4); k < n; ++k) {
            const range& t = tables[(next() >> 8) % tables.size()];
            damaged[t.offset + (next() >> 8) % t.length] = std::uint8_t(next() >> 16);
        }
        try {
            const stb::font f = stb::font::from_memory(damaged);
            for (const float weight : {300.0f, 560.0f, 800.0f}) {
                const stb::font v = f.with_variation("wght", weight).with_variation("wdth", 80.0f);
                for (std::uint32_t g = 0; g < v.glyph_count(); ++g) {
                    (void)v.outline(g);
                    (void)v.glyph_info(g);
                }
                (void)v.measure("Hamburgefonstiv", 24.0f);
            }
        } catch (const stb::error&) {
        } catch (const std::invalid_argument&) {
        }
    }
}
