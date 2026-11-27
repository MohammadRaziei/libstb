#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "conftest.hpp"

// font::embedding() (the license bits) and subsetter (a smaller font file with only some glyphs).
//
// The subset is checked against the font it came from, through libstb's own reader: glyph n of the subset must give
// the outline, advance and left side bearing of glyph kept[n] of the original, composites included (their parts are
// renumbered). Vazirmatn is the real font here: 785 of its 1333 glyphs are composites (accents, the dots of Persian
// letters). tests/python/test_font_subset.py checks the files again with fontTools.

namespace {

std::string fonts(const char* name) { return std::string(LIBSTB_DATA_DIR) + "/fonts/" + name; }
std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::uint8_t buffer[65536];
        for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
        std::fclose(f);
    }
    return bytes;
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// Where the table `tag` starts in a one-face font file, 0 if there is none.
std::size_t table_at(const std::vector<std::uint8_t>& b, const char* tag) {
    const std::size_t count = (std::size_t(b[4]) << 8) | b[5];
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 12 + 16 * i;
        if (std::string(b.begin() + long(record), b.begin() + long(record) + 4) == tag) return be32(b, record + 8);
    }
    return 0;
}

// A copy of Vazirmatn with OS/2 fsType set to `fs_type`.
stb::font with_fs_type(std::uint16_t fs_type) {
    std::vector<std::uint8_t> bytes = read_all(fonts("Vazirmatn-Regular.ttf"));
    const std::size_t at = table_at(bytes, "OS/2") + 8;
    bytes[at] = std::uint8_t(fs_type >> 8);
    bytes[at + 1] = std::uint8_t(fs_type);
    return stb::font::from_memory(std::move(bytes));
}

stb::font vazirmatn() { return stb::font::open(fonts("Vazirmatn-Regular.ttf")); }

// "" when `sub` holds, for every glyph it keeps, what the original has for the same glyph; else what differs first.
std::string compare(const stb::font& original, const stb::font_subset& sub, const stb::font& reloaded) {
    if (reloaded.glyph_count() != sub.glyph_count()) return "glyph count";
    std::uint32_t seen = 0;
    for (std::uint32_t g = 0; g < original.glyph_count(); ++g) {
        const std::uint32_t n = sub.new_index(g);
        if (n == stb::font_subset::absent) continue;
        ++seen;
        if (n >= reloaded.glyph_count()) return "index past the end";
        const std::vector<stb::outline_command> a = original.outline(g), b = reloaded.outline(n);
        if (a.size() != b.size()) return "outline size of glyph " + std::to_string(g);
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].op != b[i].op) return "outline op of glyph " + std::to_string(g);
            for (int k = 0; k < 3; ++k)
                if (a[i].p[k].x != b[i].p[k].x || a[i].p[k].y != b[i].p[k].y) return "outline point of glyph " + std::to_string(g);
        }
        const stb::glyph_metrics m = original.glyph_info(g), r = reloaded.glyph_info(n);
        if (m.advance != r.advance || m.lsb != r.lsb) return "metrics of glyph " + std::to_string(g);
    }
    return seen == sub.glyph_count() ? "" : "the map and the count disagree";
}

}  // namespace

UTEST(libstb_font_subset_tests, test_embedding_reports_the_license_bits_of_os2) {
    EXPECT_TRUE(vazirmatn().embedding().permission() == stb::font_embedding::level::installable);  // fsType 0
    EXPECT_TRUE(vazirmatn().embedding().subsetting_allowed());
    EXPECT_FALSE(vazirmatn().embedding().bitmap_only());

    const stb::font latin = stb::font::open(data("libstb-latin-test.ttf"));  // fsType 4
    EXPECT_TRUE(latin.embedding().permission() == stb::font_embedding::level::preview_print);
    EXPECT_EQ(latin.embedding().fs_type(), 4);

    EXPECT_TRUE(with_fs_type(0x0002).embedding().permission() == stb::font_embedding::level::restricted);
    EXPECT_TRUE(with_fs_type(0x0008).embedding().permission() == stb::font_embedding::level::editable);
    const stb::font_embedding no_subset = with_fs_type(0x0108).embedding();
    EXPECT_TRUE(no_subset.permission() == stb::font_embedding::level::editable);
    EXPECT_FALSE(no_subset.subsetting_allowed());
    EXPECT_TRUE(with_fs_type(0x0200).embedding().bitmap_only());
}

UTEST(libstb_font_subset_tests, test_embedding_takes_the_most_restrictive_level_when_several_bits_are_set) {
    EXPECT_TRUE(stb::font_embedding(0x000E).permission() == stb::font_embedding::level::restricted);
    EXPECT_TRUE(stb::font_embedding(0x000C).permission() == stb::font_embedding::level::preview_print);
    EXPECT_TRUE(stb::font_embedding().permission() == stb::font_embedding::level::installable);
}

UTEST(libstb_font_subset_tests, test_supported_means_truetype_outlines_and_no_variation_applied) {
    EXPECT_TRUE(stb::subsetter(vazirmatn()).supported());
    EXPECT_FALSE(stb::subsetter(stb::font::open(data("libstb-cff-test.otf"))).supported());

    const stb::font variable = stb::font::open(data("libstb-variable-test.ttf"));
    EXPECT_TRUE(stb::subsetter(variable).supported());  // the default instance
    EXPECT_FALSE(stb::subsetter(variable.with_weight(800)).supported());
}

UTEST(libstb_font_subset_tests, test_a_subset_gives_the_same_outlines_and_metrics_under_new_indices) {
    const stb::font font = vazirmatn();
    std::vector<std::uint32_t> wanted;
    for (const char32_t cp : std::u32string(U"AaZ\u00C1\u00E9\u0628\u0633\u0645 .")) wanted.push_back(font.glyph_index(cp));

    stb::subsetter builder(font);
    builder.glyphs(wanted);
    const stb::font_subset sub = builder.run();
    const stb::font reloaded = stb::font::from_memory(sub.data());

    EXPECT_STREQ(compare(font, sub, reloaded).c_str(), "");
    for (const std::uint32_t g : wanted) EXPECT_NE(sub.new_index(g), stb::font_subset::absent);
    EXPECT_EQ(sub.new_index(0), 0u);  // .notdef is always glyph 0
    // Aacute (U+00C1) is a composite of A and an accent, beh (U+0628) of a dotless beh and a dot: the parts came along.
    EXPECT_GT(sub.glyph_count(), wanted.size() - 1);  // more glyphs than were asked for (one was asked for twice)
    EXPECT_LT(sub.data().size(), read_all(fonts("Vazirmatn-Regular.ttf")).size() / 20);
}

UTEST(libstb_font_subset_tests, test_the_glyphs_keep_their_order_and_the_ones_not_asked_for_are_absent) {
    const stb::font font = vazirmatn();
    const std::uint32_t z = font.glyph_index(U'Z');
    const stb::font_subset sub = stb::subsetter(font).glyph(font.glyph_index(U'b')).glyph(font.glyph_index(U'a')).run();

    EXPECT_EQ(sub.glyph_count(), 3u);  // .notdef, a, b
    EXPECT_EQ(sub.new_index(font.glyph_index(U'a')), 1u);
    EXPECT_EQ(sub.new_index(font.glyph_index(U'b')), 2u);
    EXPECT_EQ(sub.new_index(z), stb::font_subset::absent);
    EXPECT_EQ(sub.new_index(font.glyph_count() + 5), stb::font_subset::absent);  // not even a glyph of the font
}

UTEST(libstb_font_subset_tests, test_nothing_asked_for_leaves_only_notdef_and_a_duplicate_does_nothing) {
    const stb::font font = vazirmatn();
    const stb::font_subset empty = stb::subsetter(font).run();
    EXPECT_EQ(empty.glyph_count(), 1u);
    EXPECT_EQ(stb::font::from_memory(empty.data()).glyph_count(), 1u);

    const std::uint32_t a = font.glyph_index(U'a');
    EXPECT_EQ(stb::subsetter(font).glyph(a).glyph(a).glyph(a).run().glyph_count(), 2u);
}

UTEST(libstb_font_subset_tests, test_the_file_is_valid_sfnt_with_the_checksums_of_the_standard) {
    const stb::font font = vazirmatn();
    const stb::font_subset sub = stb::subsetter(font).glyph(font.glyph_index(U'\u00C1')).run();
    const std::vector<std::uint8_t>& file = sub.data();

    const bool whole_words = file.size() % 4 == 0;  // (utest prints the text of an expression: no % inside one)
    ASSERT_TRUE(whole_words);
    EXPECT_EQ(be32(file, 0), 0x00010000u);
    std::uint32_t whole = 0;
    for (std::size_t i = 0; i < file.size(); i += 4) whole += be32(file, i);
    EXPECT_EQ(whole, 0xB1B0AFBAu);  // head.checkSumAdjustment makes the whole file add up to this

    const std::size_t tables = (std::size_t(file[4]) << 8) | file[5];
    std::uint32_t previous_tag = 0;
    for (std::size_t i = 0; i < tables; ++i) {
        const std::size_t record = 12 + 16 * i;
        EXPECT_GT(be32(file, record), previous_tag);  // sorted by tag
        previous_tag = be32(file, record);
        const std::size_t offset = be32(file, record + 8), length = be32(file, record + 12);
        EXPECT_TRUE(offset % 4 == 0);
        ASSERT_LE(offset + length, file.size());
        std::uint32_t sum = 0;
        for (std::size_t k = 0; k < length; k += 4) {
            std::uint32_t word = 0;
            for (std::size_t b = 0; b < 4; ++b) word = (word << 8) | (k + b < length ? file[offset + k + b] : 0u);
            sum += word;
        }
        if (be32(file, record) == 0x68656164u) sum -= be32(file, offset + 8);  // 'head': the adjustment does not count
        EXPECT_EQ(sum, be32(file, record + 4));
    }
}

UTEST(libstb_font_subset_tests, test_every_glyph_is_empty_or_a_whole_glyph_and_loca_only_grows) {
    const stb::font font = vazirmatn();
    // Vazirmatn's .notdef has no outline: its slot in the subset must stay empty (a reader such as fontTools refuses a
    // glyph shorter than its 10-byte header).
    const stb::font_subset sub = stb::subsetter(font).glyph(font.glyph_index(U'\u00C1')).run();
    const std::vector<std::uint8_t>& file = sub.data();
    const std::size_t loca = table_at(file, "loca");
    const bool long_format = file[table_at(file, "head") + 51] == 1;
    std::uint32_t previous = 0;
    for (std::uint32_t g = 0; g <= sub.glyph_count(); ++g) {
        const std::size_t at = loca + std::size_t(g) * (long_format ? 4 : 2);
        const std::uint32_t offset = long_format ? be32(file, at) : ((std::uint32_t(file[at]) << 8) | file[at + 1]) * 2u;
        EXPECT_GE(offset, previous);
        if (g > 0) EXPECT_TRUE(offset == previous || offset - previous >= 10);
        previous = offset;
    }
}

UTEST(libstb_font_subset_tests, test_the_same_request_gives_the_same_bytes) {
    const stb::font font = vazirmatn();
    stb::subsetter builder(font);
    builder.glyph(font.glyph_index(U'\u0628')).glyph(font.glyph_index(U'x'));
    EXPECT_TRUE(builder.run().data() == builder.run().data());
}

UTEST(libstb_font_subset_tests, test_a_variable_font_at_its_default_gives_its_default_instance) {
    const stb::font variable = stb::font::open(data("libstb-variable-test.ttf"));
    const stb::font_subset sub = stb::subsetter(variable).glyph(variable.glyph_index(U'\u00E9')).glyph(variable.glyph_index(U'a')).run();
    EXPECT_STREQ(compare(variable, sub, stb::font::from_memory(sub.data())).c_str(), "");
}

UTEST(libstb_font_subset_tests, test_the_bad_cases_throw) {
    const stb::font font = vazirmatn();
    ASSERT_THROWS(stb::subsetter(font).glyph(font.glyph_count()), std::invalid_argument);
    ASSERT_THROWS(stb::subsetter(font).glyphs({1, 2, 0xFFFFFFFFu}), std::invalid_argument);
    ASSERT_THROWS(stb::subsetter(stb::font::open(data("libstb-cff-test.otf"))).run(), stb::error);

    // AEacute is glyph 7 and is made of glyphs 5 and 313: a font that says it has 8 glyphs has a composite whose part is missing.
    std::vector<std::uint8_t> bytes = read_all(fonts("Vazirmatn-Regular.ttf"));
    const std::size_t maxp = table_at(bytes, "maxp");
    bytes[maxp + 4] = 0;
    bytes[maxp + 5] = 8;
    const stb::font damaged = stb::font::from_memory(std::move(bytes));
    ASSERT_THROWS(stb::subsetter(damaged).glyph(7).run(), stb::decode_error);
}
