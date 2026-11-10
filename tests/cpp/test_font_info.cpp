#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "test_common.hpp"
#include "stb/draw.hpp"
#include "util/file.hpp"

// font::family_name / is_bold / is_italic / scan / system_fonts. libstb-info.ttc
// (tests/data/make_info_test_fonts.py) has four faces whose name, OS/2 and head tables differ:
//   0 "Info Sans"                regular
//   1 "Info Sans" + U+1F600      bold            (a French id 1 comes first in the table and must lose)
//   2 "Info Sans"                italic          (id 1 says "Info Sans Light": the typographic id 16 wins)
//   3 "Cafe Sans" (e acute)      bold italic     (Mac Roman names only, no OS/2: head.macStyle decides)

namespace {

std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }
std::string sample(const char* name) { return std::string(LIBSTB_DATA_DIR) + "/fonts/" + name; }

const std::string grin = "\xF0\x9F\x98\x80";   // U+1F600
const std::string e_acute = "\xC3\xA9";        // U+00E9

}  // namespace

UTEST(libstb_font_info_tests, test_every_face_of_a_collection_reports_its_own_name_and_style) {
    const std::vector<stb::font_face_info> faces = stb::font::scan(std::filesystem::path(data("libstb-info.ttc")));
    ASSERT_EQ(4u, faces.size());
    for (int i = 0; i < 4; ++i) ASSERT_EQ(i, faces[std::size_t(i)].index);
    ASSERT_STREQ("Info Sans", faces[0].family_name.c_str());
    ASSERT_FALSE(faces[0].bold);
    ASSERT_FALSE(faces[0].italic);
    ASSERT_STREQ(("Info Sans " + grin).c_str(), faces[1].family_name.c_str());
    ASSERT_TRUE(faces[1].bold);
    ASSERT_FALSE(faces[1].italic);
    ASSERT_STREQ("Info Sans", faces[2].family_name.c_str());
    ASSERT_FALSE(faces[2].bold);
    ASSERT_TRUE(faces[2].italic);
    ASSERT_STREQ(("Caf" + e_acute + " Sans").c_str(), faces[3].family_name.c_str());
    ASSERT_TRUE(faces[3].bold);
    ASSERT_TRUE(faces[3].italic);
}

UTEST(libstb_font_info_tests, test_an_opened_face_agrees_with_scan) {
    const auto faces = stb::font::scan(std::filesystem::path(data("libstb-info.ttc")));
    for (const stb::font_face_info& info : faces) {
        const stb::font f = stb::font::open(data("libstb-info.ttc"), info.index);
        ASSERT_STREQ(info.family_name.c_str(), f.family_name().c_str());
        ASSERT_EQ(info.bold, f.is_bold());
        ASSERT_EQ(info.italic, f.is_italic());
    }
}

UTEST(libstb_font_info_tests, test_scan_of_bytes_and_of_a_woff_file) {
    const auto bytes = stb::detail::read_file(data("libstb-info.ttc"));
    ASSERT_EQ(4u, stb::font::scan(bytes).size());

    const auto woff = stb::font::scan(std::filesystem::path(data("libstb-info.woff")));
    ASSERT_EQ(1u, woff.size());
    ASSERT_EQ(0, woff[0].index);
    ASSERT_STREQ(("Info Sans " + grin).c_str(), woff[0].family_name.c_str());
    ASSERT_TRUE(woff[0].bold);
    ASSERT_FALSE(woff[0].italic);
    const stb::font f = stb::font::open(data("libstb-info.woff"));
    ASSERT_STREQ(woff[0].family_name.c_str(), f.family_name().c_str());
}

UTEST(libstb_font_info_tests, test_a_plain_font_gives_one_entry_and_scan_rejects_what_is_not_a_font) {
    const auto one = stb::font::scan(std::filesystem::path(data("libstb-test.ttf")));
    ASSERT_EQ(1u, one.size());
    ASSERT_STREQ("LibstbTest", one[0].family_name.c_str());
    ASSERT_FALSE(one[0].bold);
    ASSERT_FALSE(one[0].italic);

    ASSERT_THROWS(stb::font::scan(std::vector<std::uint8_t>{}), stb::decode_error);
    ASSERT_THROWS(stb::font::scan(std::vector<std::uint8_t>(64, 0x41)), stb::decode_error);
    ASSERT_THROWS(stb::font::scan(std::filesystem::path(data("no-such-font.ttf"))), stb::io_error);
}

UTEST(libstb_font_info_tests, test_a_cut_off_file_gives_no_name_instead_of_reading_outside_it) {
    auto bytes = stb::detail::read_file(data("libstb-test.ttf"));
    // Every length: scan may give an empty name or throw, never crash (sanitizers watch the reads).
    // A cut file never has a longer name than the whole one.
    const std::size_t whole = stb::font::scan(bytes).at(0).family_name.size();
    for (std::size_t n = 0; n < bytes.size(); n += 7) {
        const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + std::ptrdiff_t(n));
        try {
            for (const stb::font_face_info& f : stb::font::scan(cut)) ASSERT_TRUE(f.family_name.size() <= whole);
        } catch (const stb::decode_error&) {
        }
    }
}

UTEST(libstb_font_info_tests, test_the_sample_fonts_report_their_weight) {
    const auto regular = stb::font::scan(std::filesystem::path(sample("Vazirmatn-Regular.ttf")));
    const auto bold = stb::font::scan(std::filesystem::path(sample("Vazirmatn-Bold.ttf")));
    ASSERT_EQ(1u, regular.size());
    ASSERT_EQ(1u, bold.size());
    ASSERT_STREQ("Vazirmatn", regular[0].family_name.c_str());
    ASSERT_STREQ("Vazirmatn", bold[0].family_name.c_str());
    ASSERT_FALSE(regular[0].bold);
    ASSERT_TRUE(bold[0].bold);
    ASSERT_FALSE(regular[0].italic);
    ASSERT_FALSE(bold[0].italic);
}

UTEST(libstb_font_info_tests, test_dejavu_when_the_machine_has_it) {
    const std::filesystem::path dir = "/usr/share/fonts/truetype/dejavu";
    if (!std::filesystem::exists(dir / "DejaVuSans-BoldOblique.ttf")) return;  // not every machine has it
    const auto plain = stb::font::open(dir / "DejaVuSans.ttf");
    const auto bold = stb::font::open(dir / "DejaVuSans-Bold.ttf");
    const auto oblique = stb::font::open(dir / "DejaVuSans-Oblique.ttf");
    ASSERT_STREQ("DejaVu Sans", plain.family_name().c_str());
    ASSERT_STREQ("DejaVu Sans", bold.family_name().c_str());
    ASSERT_STREQ("DejaVu Sans", oblique.family_name().c_str());
    ASSERT_FALSE(plain.is_bold());
    ASSERT_FALSE(plain.is_italic());
    ASSERT_TRUE(bold.is_bold());
    ASSERT_FALSE(bold.is_italic());
    ASSERT_FALSE(oblique.is_bold());
    ASSERT_TRUE(oblique.is_italic());
}

UTEST(libstb_font_info_tests, test_system_fonts_of_given_folders_lists_every_face_sorted) {
    const auto all = stb::font::system_fonts({std::filesystem::path(LIBSTB_TEST_DATA_DIR)});
    // The folder holds libstb-info.ttc (4 faces), other fonts, and files that are not fonts (skipped).
    int info_faces = 0;
    for (const stb::system_font_info& f : all) {
        if (f.path.filename() == "libstb-info.ttc") ++info_faces;
        ASSERT_FALSE(f.path.empty());
    }
    ASSERT_EQ(4, info_faces);
    ASSERT_TRUE(std::is_sorted(all.begin(), all.end(), [](const stb::system_font_info& a, const stb::system_font_info& b) {
        return a.path != b.path ? a.path < b.path : a.index < b.index;
    }));
    // Each entry can be opened as it says.
    for (const stb::system_font_info& f : all) {
        const stb::font font = stb::font::open(f.path, f.index);
        ASSERT_STREQ(f.family_name.c_str(), font.family_name().c_str());
    }
    // A folder that does not exist gives nothing, and the same folder twice gives each face once.
    ASSERT_EQ(0u, stb::font::system_fonts({std::filesystem::path(data("no-such-folder"))}).size());
    ASSERT_EQ(all.size(), stb::font::system_fonts({std::filesystem::path(LIBSTB_TEST_DATA_DIR), std::filesystem::path(LIBSTB_TEST_DATA_DIR)}).size());
}

UTEST(libstb_font_info_tests, test_system_fonts_of_this_machine_does_not_throw) {
    const auto dirs = stb::font::system_font_dirs();
    for (const auto& d : dirs) ASSERT_TRUE(std::filesystem::is_directory(d));
    const auto all = stb::font::system_fonts();
    for (const auto& f : all) ASSERT_TRUE(f.index >= 0);
}
