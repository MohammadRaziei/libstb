#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "test_common.hpp"
#include "util/file.hpp"
#include "font/woff.hpp"
#include "stb/zlib.hpp"

// WOFF 1.0 files (font::from_memory / open unpack them to the sfnt they hold). The fonts are the
// synthetic ones of tests/data, each next to a .woff that fontTools made of it
// (make_woff_test_fonts.py: some tables compressed, some stored). Damaged files are made here by
// changing single fields of one of them, and a few WOFF files are written by the tests themselves
// (a second, independent writer: other table order, stored tables, an empty table).

namespace {

using bytes = std::vector<std::uint8_t>;

std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }
bytes read(const char* name) { return stb::detail::read_file(data(name)); }

std::uint32_t get16(const bytes& b, std::size_t at) { return (std::uint32_t(b[at]) << 8) | b[at + 1]; }
std::uint32_t get32(const bytes& b, std::size_t at) { return (get16(b, at) << 16) | get16(b, at + 2); }
void put16(bytes& b, std::size_t at, std::uint32_t x) {
    b[at] = std::uint8_t(x >> 8);
    b[at + 1] = std::uint8_t(x);
}
void put32(bytes& b, std::size_t at, std::uint32_t x) {
    put16(b, at, x >> 16);
    put16(b, at + 2, x & 0xFFFFu);
}

const std::pair<const char*, const char*> fonts[] = {
    {"libstb-test.woff", "libstb-test.ttf"},
    {"libstb-curves-test.woff", "libstb-curves-test.ttf"},
    {"libstb-cff-test.woff", "libstb-cff-test.otf"},
};

// The fields of directory entry `i` of a WOFF file, at their offsets.
constexpr std::size_t tag_at(std::size_t i) { return 44 + 20 * i; }
constexpr std::size_t offset_at(std::size_t i) { return 44 + 20 * i + 4; }
constexpr std::size_t comp_at(std::size_t i) { return 44 + 20 * i + 8; }
constexpr std::size_t orig_at(std::size_t i) { return 44 + 20 * i + 12; }

// The first entry whose table is compressed / stored (SIZE_MAX if none).
std::size_t find_entry(const bytes& woff, bool compressed) {
    const std::size_t n = get16(woff, 12);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = get32(woff, comp_at(i)), o = get32(woff, orig_at(i));
        if (compressed ? (c < o) : (c == o && c > 0)) return i;
    }
    return std::size_t(-1);
}

// The sfnt directory of a font file: tag -> (checksum, bytes of the table).
struct sfnt_table {
    std::uint32_t tag, checksum;
    bytes body;
};
std::vector<sfnt_table> sfnt_tables(const bytes& f) {
    std::vector<sfnt_table> out;
    const std::size_t n = get16(f, 4);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t e = 12 + 16 * i;
        const std::size_t off = get32(f, e + 8), len = get32(f, e + 12);
        out.push_back({get32(f, e), get32(f, e + 4), bytes(f.begin() + std::ptrdiff_t(off), f.begin() + std::ptrdiff_t(off + len))});
    }
    return out;
}

// A table to put into a WOFF file written by the tests: its tag, the bytes as they go in the file,
// and the original length and checksum to declare.
struct table_spec {
    std::uint32_t tag;
    bytes stored;
    std::uint32_t orig;
    std::uint32_t checksum;
};

// Writes a WOFF file: header, directory (as given, in that order), then the data blocks, each padded to 4 bytes,
// in the order of `layout` (indices into `tables`). The length field is the real size.
bytes write_woff(std::uint32_t flavor, const std::vector<table_spec>& tables, std::vector<std::size_t> layout = {}) {
    if (layout.empty())
        for (std::size_t i = 0; i < tables.size(); ++i) layout.push_back(i);
    bytes out(44 + 20 * tables.size(), 0);
    put32(out, 0, 0x774F4646u);
    put32(out, 4, flavor);
    put16(out, 12, std::uint32_t(tables.size()));
    std::vector<std::uint32_t> where(tables.size(), 0);
    for (std::size_t i : layout) {
        where[i] = std::uint32_t(out.size());
        out.insert(out.end(), tables[i].stored.begin(), tables[i].stored.end());
        out.resize((out.size() + 3) & ~std::size_t(3), 0);
    }
    for (std::size_t i = 0; i < tables.size(); ++i) {
        put32(out, tag_at(i), tables[i].tag);
        put32(out, offset_at(i), where[i]);
        put32(out, comp_at(i), std::uint32_t(tables[i].stored.size()));
        put32(out, orig_at(i), tables[i].orig);
        put32(out, orig_at(i) + 4, tables[i].checksum);
    }
    put32(out, 8, std::uint32_t(out.size()));
    return out;
}

enum class how { compress, store_all };

// A WOFF file of a font file, made with libstb's own zlib: a table is compressed when that makes it smaller
// (or never, for store_all). `reversed` puts the data blocks in the opposite order to the directory, and
// `with_empty_table` adds a table with no bytes (sorted in among the others by its tag).
bytes woff_of(const bytes& font, how h, bool reversed = false, bool with_empty_table = false) {
    std::vector<table_spec> tables;
    for (const sfnt_table& t : sfnt_tables(font)) {
        bytes z = h == how::compress ? stb::zlib_compress(t.body, 9) : bytes();
        tables.push_back({t.tag, !z.empty() && z.size() < t.body.size() ? z : t.body, std::uint32_t(t.body.size()), t.checksum});
    }
    if (with_empty_table) tables.push_back({0x5A5A5A5Au, {}, 0, 0});  // "ZZZZ"
    std::sort(tables.begin(), tables.end(), [](const table_spec& a, const table_spec& b) { return a.tag < b.tag; });
    std::vector<std::size_t> layout;
    for (std::size_t i = 0; i < tables.size(); ++i) layout.push_back(i);
    if (reversed) std::reverse(layout.begin(), layout.end());
    return write_woff(get32(font, 0), tables, layout);
}

// Same glyphs, metrics, outlines and kerning: what "the same font" means to a caller.
bool same_font(const stb::font& a, const stb::font& b) {
    if (a.glyph_count() != b.glyph_count()) return false;
    const stb::font_design_metrics da = a.design_metrics(), db = b.design_metrics();
    if (!(da.units_per_em == db.units_per_em && da.ascent == db.ascent && da.descent == db.descent &&
          da.line_gap == db.line_gap && da.x_min == db.x_min && da.y_min == db.y_min && da.x_max == db.x_max &&
          da.y_max == db.y_max))
        return false;
    for (std::uint32_t g = 0; g < a.glyph_count(); ++g) {
        const stb::glyph_metrics ma = a.glyph_info(g), mb = b.glyph_info(g);
        if (!(ma.advance == mb.advance && ma.lsb == mb.lsb && ma.x_min == mb.x_min && ma.y_min == mb.y_min &&
              ma.x_max == mb.x_max && ma.y_max == mb.y_max && ma.has_outline == mb.has_outline))
            return false;
        if (!(a.outline(g) == b.outline(g))) return false;
        for (std::uint32_t h = 0; h < a.glyph_count(); ++h)
            if (a.glyph_kerning(g, h) != b.glyph_kerning(g, h)) return false;
    }
    for (char32_t cp : {U' ', U'A', U'B', U'C', U'O', U'Q', U'V', U'Z'})
        if (a.glyph_index(cp) != b.glyph_index(cp)) return false;
    return true;
}

}  // namespace

UTEST(libstb_woff_tests, test_the_unpacked_font_has_the_tables_of_the_original_byte_for_byte) {
    for (const auto& [woff_name, font_name] : fonts) {
        const bytes woff = read(woff_name), original = read(font_name);
        const bytes sfnt = stb::detail::woff1_to_sfnt(woff.data(), woff.size());
        const bool padded = sfnt.size() % 4 == 0;  // not inside ASSERT_*: utest reads '%' there as a format
        ASSERT_TRUE(padded);
        // The 12-byte header: flavor, numTables, searchRange, entrySelector, rangeShift.
        ASSERT_TRUE(std::equal(original.begin(), original.begin() + 12, sfnt.begin()));
        const std::vector<sfnt_table> got = sfnt_tables(sfnt), want = sfnt_tables(original);
        ASSERT_EQ(want.size(), got.size());
        for (std::size_t i = 0; i < want.size(); ++i) {
            ASSERT_EQ(want[i].tag, got[i].tag);
            ASSERT_EQ(want[i].checksum, got[i].checksum);
            ASSERT_TRUE(want[i].body == got[i].body);
        }
    }
}

UTEST(libstb_woff_tests, test_the_test_fonts_hold_both_stored_and_compressed_tables) {
    // The other tests are only as good as this: they must reach both ways of storing a table.
    bool stored = false, compressed = false;
    for (const auto& [woff_name, font_name] : fonts) {
        (void)font_name;
        const bytes woff = read(woff_name);
        stored = stored || find_entry(woff, false) != std::size_t(-1);
        compressed = compressed || find_entry(woff, true) != std::size_t(-1);
    }
    ASSERT_TRUE(stored);
    ASSERT_TRUE(compressed);
}

UTEST(libstb_woff_tests, test_a_woff_font_is_the_same_font_as_the_file_it_was_made_from) {
    for (const auto& [woff_name, font_name] : fonts) {
        const stb::font want = stb::font::open(data(font_name));
        ASSERT_TRUE(same_font(want, stb::font::open(data(woff_name))));  // read from a path (a mapped file)
        ASSERT_TRUE(same_font(want, stb::font::from_memory(read(woff_name))));
    }
}

UTEST(libstb_woff_tests, test_woff_files_written_by_the_tests_load_too) {
    for (const auto& [woff_name, font_name] : fonts) {
        (void)woff_name;
        const bytes original = read(font_name);
        const stb::font want = stb::font::from_memory(original);
        ASSERT_TRUE(same_font(want, stb::font::from_memory(woff_of(original, how::compress))));
        ASSERT_TRUE(same_font(want, stb::font::from_memory(woff_of(original, how::store_all))));
        ASSERT_TRUE(same_font(want, stb::font::from_memory(woff_of(original, how::compress, /*reversed=*/true))));
        ASSERT_TRUE(same_font(want, stb::font::from_memory(woff_of(original, how::compress, false, /*with_empty_table=*/true))));
    }
}

UTEST(libstb_woff_tests, test_a_woff_has_one_face) {
    const bytes woff = read("libstb-test.woff");
    ASSERT_EQ(1, stb::font::face_count(woff));
    ASSERT_EQ(1, stb::font::face_count(std::filesystem::path(data("libstb-curves-test.woff"))));
    ASSERT_THROWS(stb::font::from_memory(woff, 1), stb::decode_error);
    ASSERT_THROWS(stb::font::open(data("libstb-test.woff"), 1), stb::decode_error);
    ASSERT_THROWS(stb::font::from_memory(woff, -1), std::invalid_argument);
}

UTEST(libstb_woff_tests, test_woff2_is_refused_and_says_so) {
    bytes woff2 = read("libstb-test.woff");
    put32(woff2, 0, 0x774F4632u);  // "wOF2": the signature is all that is looked at
    const auto message = [](auto&& call) {
        try {
            call();
        } catch (const stb::decode_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    ASSERT_TRUE(message([&] { (void)stb::font::from_memory(woff2); }).find("WOFF2") != std::string::npos);
    ASSERT_TRUE(message([&] { (void)stb::font::face_count(woff2); }).find("WOFF2") != std::string::npos);
}

UTEST(libstb_woff_tests, test_a_file_that_is_not_a_font_is_still_refused_as_before) {
    ASSERT_THROWS(stb::font::from_memory(bytes{'w', 'O', 'F'}), stb::decode_error);  // shorter than a signature
    ASSERT_THROWS(stb::font::from_memory(bytes{'w', 'O', 'F', 'F'}), stb::decode_error);  // a signature and nothing else
    ASSERT_THROWS(stb::font::face_count(bytes{'x', 'x', 'x', 'x'}), stb::decode_error);
}

// Each of these changes one field of a good file and asks the unpacker itself (so the error is the
// WOFF reader's, not the TrueType reader's that would see the unpacked bytes).
namespace {

bytes good() { return read("libstb-curves-test.woff"); }

template <class Change>
bool is_refused(Change change) {
    bytes w = good();
    change(w);
    try {
        (void)stb::detail::woff1_to_sfnt(w.data(), w.size());
    } catch (const stb::decode_error&) {
        return true;
    } catch (...) {
    }
    return false;
}

}  // namespace

UTEST(libstb_woff_tests, test_the_good_file_is_accepted_by_the_checks_below) {
    const bytes w = good();
    ASSERT_TRUE(stb::detail::woff1_to_sfnt(w.data(), w.size()).size() > 0);
}

UTEST(libstb_woff_tests, test_a_damaged_header_is_refused) {
    ASSERT_TRUE(is_refused([](bytes& w) { w.resize(w.size() - 1); }));  // cut short
    ASSERT_TRUE(is_refused([](bytes& w) { w.push_back(0); }));  // something after the end
    ASSERT_TRUE(is_refused([](bytes& w) { w.resize(43); }));  // shorter than the header
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, 8, std::uint32_t(w.size()) + 1); }));
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, 0, 0x774F4647u); }));  // not "wOFF"
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, 4, 0x74746366u); }));  // "ttcf": a collection
    ASSERT_TRUE(is_refused([](bytes& w) { put16(w, 12, 0); }));  // no tables
    ASSERT_TRUE(is_refused([](bytes& w) { put16(w, 12, 0xFFFF); }));  // a directory longer than the file
}

UTEST(libstb_woff_tests, test_a_damaged_directory_is_refused) {
    ASSERT_TRUE(is_refused([](bytes& w) { std::swap_ranges(w.begin() + 44, w.begin() + 48, w.begin() + 64); }));  // unsorted
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, tag_at(1), get32(w, tag_at(0))); }));  // two tables with one tag
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, offset_at(0), 0); }));  // data where the directory is
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, offset_at(0), std::uint32_t(w.size())); }));  // data past the end
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, offset_at(0), 0xFFFFFFFFu); }));  // offset + length would wrap
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, comp_at(0), get32(w, orig_at(0)) + 1); }));  // larger packed than not
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, comp_at(0), 0xFFFFFFFFu); }));
    ASSERT_TRUE(is_refused([](bytes& w) { put32(w, offset_at(1), get32(w, offset_at(0))); }));  // two tables on the same bytes
}

UTEST(libstb_woff_tests, test_tables_that_share_bytes_are_refused_even_when_each_would_unpack) {
    // Two stored tables: whichever bytes they point at, both unpack, so only the overlap itself is wrong.
    const bytes x(64, 0x5A);
    const bytes fine = write_woff(0x00010000u, {{0x61616161u, x, 64, 0}, {0x62626262u, x, 64, 0}});
    ASSERT_TRUE(stb::detail::woff1_to_sfnt(fine.data(), fine.size()).size() > 0);  // two blocks of their own

    bytes same = fine;
    put32(same, offset_at(1), get32(same, offset_at(0)));  // the second on the very bytes of the first
    ASSERT_THROWS(stb::detail::woff1_to_sfnt(same.data(), same.size()), stb::decode_error);

    bytes inside = fine;
    put32(inside, offset_at(1), get32(inside, offset_at(0)) + 4);  // the second starting inside the first
    put32(inside, comp_at(1), 60);
    put32(inside, orig_at(1), 60);
    ASSERT_THROWS(stb::detail::woff1_to_sfnt(inside.data(), inside.size()), stb::decode_error);
}

UTEST(libstb_woff_tests, test_a_damaged_compressed_table_is_refused) {
    const std::size_t c = find_entry(good(), true);
    ASSERT_TRUE(c != std::size_t(-1));
    ASSERT_TRUE(is_refused([c](bytes& w) { w[get32(w, offset_at(c))] ^= 0xFF; }));  // the zlib header
    ASSERT_TRUE(is_refused([c](bytes& w) { w[get32(w, offset_at(c)) + get32(w, comp_at(c)) - 1] ^= 0xFF; }));  // its checksum
    ASSERT_TRUE(is_refused([c](bytes& w) { put32(w, comp_at(c), get32(w, comp_at(c)) - 1); }));  // a stream cut short
    ASSERT_TRUE(is_refused([c](bytes& w) { put32(w, orig_at(c), get32(w, orig_at(c)) + 4); }));  // origLength is a lie (more)
    ASSERT_TRUE(is_refused([c](bytes& w) { put32(w, orig_at(c), get32(w, comp_at(c)) + 1); }));  // a lie (less, still > packed)
}

UTEST(libstb_woff_tests, test_a_table_that_unpacks_to_more_than_it_says_is_refused_without_unpacking_it_all) {
    // A table of 4 MiB of zeros packs to a few KiB. Declared as 1000 bytes it must be refused (the
    // limit passed to zlib is the declared size), and declared as 4 MiB + 1 too.
    const bytes zeros(4u << 20, 0);
    const bytes z = stb::zlib_compress(zeros, 9);
    ASSERT_TRUE(z.size() < 10000);
    for (const std::uint32_t declared : {1000u, (4u << 20) + 1u}) {
        const bytes w = write_woff(0x00010000u, {{0x74657374u, z, declared, 0}});
        bool refused = false;
        try {
            (void)stb::detail::woff1_to_sfnt(w.data(), w.size());
        } catch (const stb::decode_error&) {
            refused = true;
        }
        ASSERT_TRUE(refused);
    }
    const bytes ok = write_woff(0x00010000u, {{0x74657374u, z, 4u << 20, 0}});  // the honest one unpacks
    ASSERT_EQ(std::size_t(12 + 16 + (4u << 20)), stb::detail::woff1_to_sfnt(ok.data(), ok.size()).size());
}

UTEST(libstb_woff_tests, test_a_font_that_would_unpack_to_gigabytes_is_a_limit_error_and_allocates_nothing) {
    bytes w = good();
    put32(w, orig_at(0), 0x30000000u);  // 768 MiB, over the 512 MiB limit: refused before any table is unpacked
    ASSERT_THROWS(stb::detail::woff1_to_sfnt(w.data(), w.size()), stb::limit_error);
    ASSERT_THROWS(stb::font::from_memory(w), stb::limit_error);
}

UTEST(libstb_woff_tests, test_it_logs_one_debug_record_and_no_warning) {
    const bytes woff = read("libstb-test.woff");
    testutil::log_capture cap(stb::logging::level::debug);
    (void)stb::font::from_memory(woff);
    int woff_records = 0;
    for (const auto& r : cap.records) {
        ASSERT_TRUE(r.level < stb::logging::level::warning);
        if (r.name == "libstb.font" && r.message.find("WOFF 1.0") != std::string::npos) ++woff_records;
    }
    ASSERT_EQ(1, woff_records);

    cap.records.clear();
    (void)stb::font::from_memory(read("libstb-test.ttf"));  // a plain font says nothing about WOFF
    for (const auto& r : cap.records) ASSERT_TRUE(r.message.find("WOFF") == std::string::npos);
}
