// White-box tests of the GSUB engine (src/font/shaper/gsub.*) with hand-made tables, plus a robustness
// test that damages a real font's GSUB table. Includes internal headers on purpose.
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_common.hpp"
#include "font/shaper/gsub.hpp"
#include "font/shaper/text_shaper.hpp"

namespace {

using stb::otl::make_tag;
using bytes = std::vector<std::uint8_t>;

void u16(bytes& b, unsigned v) {
    b.push_back(std::uint8_t(v >> 8));
    b.push_back(std::uint8_t(v));
}
void u32(bytes& b, std::uint32_t v) {
    u16(b, v >> 16);
    u16(b, v & 0xFFFF);
}
void append(bytes& b, const bytes& more) { b.insert(b.end(), more.begin(), more.end()); }

bytes coverage(const std::vector<unsigned>& glyphs) {
    bytes b;
    u16(b, 1);
    u16(b, unsigned(glyphs.size()));
    for (unsigned g : glyphs) u16(b, g);
    return b;
}

// ---- subtable builders (offsets inside a subtable are relative to its start)

bytes single_delta(const std::vector<unsigned>& glyphs, int delta) {
    bytes b;
    u16(b, 1);
    u16(b, 6);
    u16(b, unsigned(delta) & 0xFFFF);
    append(b, coverage(glyphs));
    return b;
}

bytes single_array(const std::vector<unsigned>& glyphs, const std::vector<unsigned>& out) {
    bytes b;
    u16(b, 2);
    u16(b, unsigned(6 + 2 * out.size()));
    u16(b, unsigned(out.size()));
    for (unsigned g : out) u16(b, g);
    append(b, coverage(glyphs));
    return b;
}

bytes multiple(unsigned glyph, const std::vector<unsigned>& out) {
    bytes b;
    u16(b, 1);
    u16(b, 8);   // coverage
    u16(b, 1);   // one sequence
    u16(b, 8 + 6);  // sequence offset: after the coverage (6 bytes)
    append(b, coverage({glyph}));
    u16(b, unsigned(out.size()));
    for (unsigned g : out) u16(b, g);
    return b;
}

// One ligature set for `first`; each ligature is (components after the first, ligature glyph).
bytes ligature(unsigned first, const std::vector<std::pair<std::vector<unsigned>, unsigned>>& ligs) {
    bytes b;
    u16(b, 1);
    u16(b, 8);  // coverage
    u16(b, 1);
    u16(b, 14);  // the set, after the 6-byte coverage
    append(b, coverage({first}));
    bytes set;
    u16(set, unsigned(ligs.size()));
    std::size_t at = 2 + 2 * ligs.size();
    bytes bodies;
    for (const auto& l : ligs) {
        u16(set, unsigned(at + bodies.size()));
        u16(bodies, l.second);
        u16(bodies, unsigned(l.first.size() + 1));
        for (unsigned g : l.first) u16(bodies, g);
    }
    append(b, set);
    append(b, bodies);
    return b;
}

struct record {
    unsigned seq, lookup;
};

// Chaining context, format 3 (type 6): one coverage table per position.
bytes chain3(const std::vector<unsigned>& backtrack, const std::vector<unsigned>& input,
             const std::vector<unsigned>& lookahead, const std::vector<record>& recs) {
    bytes head;
    u16(head, 3);
    const std::size_t covs = backtrack.size() + input.size() + lookahead.size();
    const std::size_t head_size = 2 + 2 + 2 * backtrack.size() + 2 + 2 * input.size() + 2 + 2 * lookahead.size() + 2 + 4 * recs.size();
    std::size_t next = head_size;
    bytes bodies;
    auto add = [&](const std::vector<unsigned>& glyphs) {
        u16(head, unsigned(glyphs.size()));
        for (unsigned g : glyphs) {
            u16(head, unsigned(next + bodies.size()));
            append(bodies, coverage({g}));
        }
    };
    (void)covs;
    add(backtrack);
    add(input);
    add(lookahead);
    u16(head, unsigned(recs.size()));
    for (const record& r : recs) {
        u16(head, r.seq);
        u16(head, r.lookup);
    }
    append(head, bodies);
    return head;
}

// Context, format 3 (type 5): one coverage table per input position.
bytes context3(const std::vector<unsigned>& input, const std::vector<record>& recs) {
    bytes b;
    u16(b, 3);
    u16(b, unsigned(input.size()));
    u16(b, unsigned(recs.size()));
    const std::size_t head = 6 + 2 * input.size() + 4 * recs.size();
    bytes bodies;
    for (unsigned g : input) {
        u16(b, unsigned(head + bodies.size()));
        append(bodies, coverage({g}));
    }
    for (const record& r : recs) {
        u16(b, r.seq);
        u16(b, r.lookup);
    }
    append(b, bodies);
    return b;
}

// Context, format 1 (type 5): glyph `first` then `rest`, with records.
bytes context1(unsigned first, const std::vector<unsigned>& rest, const std::vector<record>& recs) {
    bytes b;
    u16(b, 1);
    u16(b, 8);  // coverage
    u16(b, 1);
    u16(b, 14);  // rule set
    append(b, coverage({first}));
    u16(b, 1);  // one rule
    u16(b, 4);  // rule offset, from the rule set
    u16(b, unsigned(rest.size() + 1));
    u16(b, unsigned(recs.size()));
    for (unsigned g : rest) u16(b, g);
    for (const record& r : recs) {
        u16(b, r.seq);
        u16(b, r.lookup);
    }
    return b;
}

// Chaining context, format 1 (type 6): one rule.
bytes chain1(unsigned first, const std::vector<unsigned>& backtrack, const std::vector<unsigned>& rest,
             const std::vector<unsigned>& lookahead, const std::vector<record>& recs) {
    bytes b;
    u16(b, 1);
    u16(b, 8);
    u16(b, 1);
    u16(b, 14);
    append(b, coverage({first}));
    u16(b, 1);
    u16(b, 4);
    u16(b, unsigned(backtrack.size()));
    for (unsigned g : backtrack) u16(b, g);
    u16(b, unsigned(rest.size() + 1));
    for (unsigned g : rest) u16(b, g);
    u16(b, unsigned(lookahead.size()));
    for (unsigned g : lookahead) u16(b, g);
    u16(b, unsigned(recs.size()));
    for (const record& r : recs) {
        u16(b, r.seq);
        u16(b, r.lookup);
    }
    return b;
}

// A class definition (format 2): ranges of glyphs (first, last, class).
struct class_range {
    unsigned first, last, klass;
};
bytes class_def2(const std::vector<class_range>& ranges) {
    bytes b;
    u16(b, 2);
    u16(b, unsigned(ranges.size()));
    for (const class_range& r : ranges) {
        u16(b, r.first);
        u16(b, r.last);
        u16(b, r.klass);
    }
    return b;
}

// Context, format 2 (type 5): the class of the first glyph picks rule set `set_index`, which holds
// one rule: the classes of the glyphs after the first, then records.
bytes context2(unsigned first, const std::vector<class_range>& classes, unsigned set_index,
               const std::vector<unsigned>& rest, const std::vector<record>& recs) {
    const bytes cov = coverage({first});
    const bytes cd = class_def2(classes);
    const std::size_t head = 8 + 2 * (set_index + 1);
    bytes b;
    u16(b, 2);
    u16(b, unsigned(head));                   // coverage
    u16(b, unsigned(head + cov.size()));      // class definition
    u16(b, set_index + 1);                    // rule set count
    for (unsigned i = 0; i <= set_index; ++i) u16(b, i == set_index ? unsigned(head + cov.size() + cd.size()) : 0);
    append(b, cov);
    append(b, cd);
    u16(b, 1);  // one rule
    u16(b, 4);  // rule offset, from the rule set
    u16(b, unsigned(rest.size() + 1));
    u16(b, unsigned(recs.size()));
    for (unsigned k : rest) u16(b, k);
    for (const record& r : recs) {
        u16(b, r.seq);
        u16(b, r.lookup);
    }
    return b;
}

// Chaining context, format 2 (type 6): the same class definition serves the backtrack, input and
// lookahead sequences.
bytes chain2(unsigned first, const std::vector<class_range>& classes, unsigned set_index,
             const std::vector<unsigned>& backtrack, const std::vector<unsigned>& rest,
             const std::vector<unsigned>& lookahead, const std::vector<record>& recs) {
    const bytes cov = coverage({first});
    const bytes cd = class_def2(classes);
    const std::size_t head = 12 + 2 * (set_index + 1);
    bytes b;
    u16(b, 2);
    u16(b, unsigned(head));  // coverage
    for (int i = 0; i < 3; ++i) u16(b, unsigned(head + cov.size()));  // backtrack, input, lookahead classes
    u16(b, set_index + 1);
    for (unsigned i = 0; i <= set_index; ++i) u16(b, i == set_index ? unsigned(head + cov.size() + cd.size()) : 0);
    append(b, cov);
    append(b, cd);
    u16(b, 1);
    u16(b, 4);
    u16(b, unsigned(backtrack.size()));
    for (unsigned k : backtrack) u16(b, k);
    u16(b, unsigned(rest.size() + 1));
    for (unsigned k : rest) u16(b, k);
    u16(b, unsigned(lookahead.size()));
    for (unsigned k : lookahead) u16(b, k);
    u16(b, unsigned(recs.size()));
    for (const record& r : recs) {
        u16(b, r.seq);
        u16(b, r.lookup);
    }
    return b;
}

bytes extension(unsigned real_type, const bytes& real) {
    bytes b;
    u16(b, 1);
    u16(b, real_type);
    u32(b, 8);
    append(b, real);
    return b;
}

struct lookup_def {
    unsigned type, flag;
    std::vector<bytes> subtables;
};
struct feature_def {
    std::uint32_t tag;
    std::vector<unsigned> lookups;
};

// A GSUB table with one script (arab, default language system) and the given features/lookups.
bytes make_gsub(const std::vector<feature_def>& features, const std::vector<lookup_def>& lookups) {
    bytes script_list;  // count, record(tag, offset), script, langsys
    u16(script_list, 1);
    u32(script_list, make_tag('a', 'r', 'a', 'b'));
    u16(script_list, 8);
    u16(script_list, 4);  // script: default langsys at +4
    u16(script_list, 0);  // no other langsys
    u16(script_list, 0);  // langsys: lookup order
    u16(script_list, 0xFFFF);
    u16(script_list, unsigned(features.size()));
    for (unsigned i = 0; i < features.size(); ++i) u16(script_list, i);

    bytes feature_list;
    u16(feature_list, unsigned(features.size()));
    std::size_t at = 2 + 6 * features.size();
    bytes feats;
    for (const feature_def& f : features) {
        u32(feature_list, f.tag);
        u16(feature_list, unsigned(at + feats.size()));
        u16(feats, 0);
        u16(feats, unsigned(f.lookups.size()));
        for (unsigned l : f.lookups) u16(feats, l);
    }
    append(feature_list, feats);

    bytes lookup_list;
    u16(lookup_list, unsigned(lookups.size()));
    at = 2 + 2 * lookups.size();
    bytes bodies;
    for (const lookup_def& l : lookups) {
        u16(lookup_list, unsigned(at + bodies.size()));
        bytes lk;
        u16(lk, l.type);
        u16(lk, l.flag);
        u16(lk, unsigned(l.subtables.size()));
        std::size_t st_at = 6 + 2 * l.subtables.size();
        bytes sts;
        for (const bytes& st : l.subtables) {
            u16(lk, unsigned(st_at + sts.size()));
            append(sts, st);
        }
        append(lk, sts);
        append(bodies, lk);
    }
    append(lookup_list, bodies);

    bytes out;
    u32(out, 0x00010000);
    u16(out, 10);
    u16(out, unsigned(10 + script_list.size()));
    u16(out, unsigned(10 + script_list.size() + feature_list.size()));
    append(out, script_list);
    append(out, feature_list);
    append(out, lookup_list);
    return out;
}

using stb::otl::glyph_record;

std::vector<glyph_record> run_of(std::initializer_list<unsigned> gids) {
    std::vector<glyph_record> r;
    for (unsigned g : gids) r.push_back({g, U'x', 1, stb::otl::gc_base});
    return r;
}

std::vector<unsigned> gids(const std::vector<glyph_record>& r) {
    std::vector<unsigned> out;
    for (const auto& g : r) out.push_back(g.gid);
    return out;
}

// Runs feature "test" of `table` to `glyphs` as one stage with `mask`.
std::vector<unsigned> run_feature(const bytes& table, std::vector<glyph_record> glyphs, std::uint32_t mask = 1,
                            const stb::otl::gdef* defs = nullptr) {
    const stb::otl::gsub_table gsub{stb::otl::byte_view(table.data(), table.size())};
    const auto plan = gsub.make_plan({make_tag('a', 'r', 'a', 'b')}, 0, {{{make_tag('t', 'e', 's', 't')}, mask}});
    gsub.apply(plan, glyphs, defs);
    return gids(glyphs);
}

const std::uint32_t kTest = make_tag('t', 'e', 's', 't');

}  // namespace

using V = std::vector<unsigned>;

UTEST(libstb_gsub_tests, test_single_substitution_by_delta_and_by_array) {
    const bytes delta = make_gsub({{kTest, {0}}}, {{1, 0, {single_delta({10, 11}, 5)}}});
    ASSERT_TRUE(run_feature(delta, run_of({10, 99, 11})) == (V{15, 99, 16}));
    const bytes array = make_gsub({{kTest, {0}}}, {{1, 0, {single_array({20, 21}, {30, 31})}}});
    ASSERT_TRUE(run_feature(array, run_of({21, 20, 5})) == (V{31, 30, 5}));
}

UTEST(libstb_gsub_tests, test_the_first_matching_subtable_of_a_lookup_wins) {
    const bytes t = make_gsub({{kTest, {0}}}, {{1, 0, {single_delta({10}, 1), single_delta({10, 12}, 100)}}});
    ASSERT_TRUE(run_feature(t, run_of({10, 12})) == (V{11, 112}));
}

UTEST(libstb_gsub_tests, test_multiple_substitution_grows_and_an_empty_sequence_deletes) {
    const bytes grow = make_gsub({{kTest, {0}}}, {{2, 0, {multiple(40, {41, 42, 43})}}});
    ASSERT_TRUE(run_feature(grow, run_of({1, 40, 2})) == (V{1, 41, 42, 43, 2}));
    const bytes del = make_gsub({{kTest, {0}}}, {{2, 0, {multiple(40, {})}}});
    ASSERT_TRUE(run_feature(del, run_of({1, 40, 40, 2})) == (V{1, 2}));
}

UTEST(libstb_gsub_tests, test_ligature_picks_the_first_listed_match_and_leaves_other_text_alone) {
    const bytes t = make_gsub({{kTest, {0}}}, {{4, 0, {ligature(50, {{{51, 52}, 61}, {{51}, 60}})}}});
    ASSERT_TRUE(run_feature(t, run_of({50, 51, 52, 7})) == (V{61, 7}));  // the longer one is listed first
    ASSERT_TRUE(run_feature(t, run_of({50, 51, 7})) == (V{60, 7}));
    ASSERT_TRUE(run_feature(t, run_of({50, 52, 51})) == (V{50, 52, 51}));
}

UTEST(libstb_gsub_tests, test_ligature_across_a_mark_when_the_lookup_ignores_marks) {
    const bytes ignore = make_gsub({{kTest, {0}}}, {{4, 0x08, {ligature(50, {{{51}, 60}})}}});
    std::vector<glyph_record> g = run_of({50, 70, 51});
    g[1].gclass = stb::otl::gc_mark;
    ASSERT_TRUE(run_feature(ignore, g) == (V{60, 70}));  // the mark stays, after the ligature
    const bytes strict = make_gsub({{kTest, {0}}}, {{4, 0, {ligature(50, {{{51}, 60}})}}});
    ASSERT_TRUE(run_feature(strict, g) == (V{50, 70, 51}));  // without the flag the mark breaks the match
}

UTEST(libstb_gsub_tests, test_chaining_context_format_3_runs_a_nested_lookup_in_context) {
    // lookup 0: 81 -> 91 (nested). lookup 1: in the context 80 [81] 82, call lookup 0 at the input.
    const bytes t = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({81}, 10)}},
                                               {6, 0, {chain3({80}, {81}, {82}, {{0, 0}})}}});
    ASSERT_TRUE(run_feature(t, run_of({80, 81, 82})) == (V{80, 91, 82}));
    ASSERT_TRUE(run_feature(t, run_of({81, 82})) == (V{81, 82}));        // no backtrack glyph
    ASSERT_TRUE(run_feature(t, run_of({80, 81, 83})) == (V{80, 81, 83}));  // wrong lookahead
}

UTEST(libstb_gsub_tests, test_context_and_chaining_context_format_1) {
    const bytes ctx = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({101}, 10)}},
                                                  {5, 0, {context1(100, {101}, {{1, 0}})}}});
    ASSERT_TRUE(run_feature(ctx, run_of({100, 101})) == (V{100, 111}));
    ASSERT_TRUE(run_feature(ctx, run_of({101, 100})) == (V{101, 100}));
    const bytes chain = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({121}, 10)}},
                                                    {6, 0, {chain1(120, {}, {121}, {122}, {{1, 0}})}}});
    ASSERT_TRUE(run_feature(chain, run_of({120, 121, 122})) == (V{120, 131, 122}));
    ASSERT_TRUE(run_feature(chain, run_of({120, 121})) == (V{120, 121}));
}

UTEST(libstb_gsub_tests, test_context_and_chaining_context_format_2_pick_rules_by_class) {
    // classes: 120 -> 1, 121 -> 2, 122 -> 3, 119 -> 4
    const std::vector<class_range> cls = {{119, 119, 4}, {120, 120, 1}, {121, 121, 2}, {122, 122, 3}};
    const bytes ctx = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({121}, 10)}},
                                                  {5, 0, {context2(120, cls, 1, {2, 3}, {{1, 0}})}}});
    ASSERT_TRUE(run_feature(ctx, run_of({120, 121, 122})) == (V{120, 131, 122}));
    ASSERT_TRUE(run_feature(ctx, run_of({120, 121})) == (V{120, 121}));            // too short
    ASSERT_TRUE(run_feature(ctx, run_of({120, 122, 121})) == (V{120, 122, 121}));  // wrong classes
    ASSERT_TRUE(run_feature(ctx, run_of({7, 120, 121, 122, 7})) == (V{7, 120, 131, 122, 7}));
    const bytes chain = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({121}, 10)}},
                                                    {6, 0, {chain2(120, cls, 1, {4}, {2}, {3}, {{1, 0}})}}});
    ASSERT_TRUE(run_feature(chain, run_of({119, 120, 121, 122})) == (V{119, 120, 131, 122}));
    ASSERT_TRUE(run_feature(chain, run_of({120, 121, 122})) == (V{120, 121, 122}));      // no backtrack glyph
    ASSERT_TRUE(run_feature(chain, run_of({119, 120, 121, 121})) == (V{119, 120, 121, 121}));  // wrong lookahead
}

UTEST(libstb_gsub_tests, test_context_format_3_and_the_first_glyph_filter) {
    // The lookup's glyph filter is built from the first input coverage of each format, so a wrong
    // offset there silently disables the lookup: every format of both types needs a test.
    const bytes ctx = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({101}, 10)}},
                                                  {5, 0, {context3({100, 101}, {{1, 0}})}}});
    ASSERT_TRUE(run_feature(ctx, run_of({100, 101})) == (V{100, 111}));
    ASSERT_TRUE(run_feature(ctx, run_of({7, 100, 101, 7})) == (V{7, 100, 111, 7}));
    ASSERT_TRUE(run_feature(ctx, run_of({101, 100})) == (V{101, 100}));
    // chaining format 3 with a backtrack glyph: the filter must use the input coverage, not the backtrack's
    const bytes chain = make_gsub({{kTest, {1}}}, {{1, 0, {single_delta({81}, 10)}},
                                                    {6, 0, {chain3({80, 79}, {81}, {}, {{0, 0}})}}});
    ASSERT_TRUE(run_feature(chain, run_of({79, 80, 81})) == (V{79, 80, 91}));
}

UTEST(libstb_gsub_tests, test_extension_lookups_are_unwrapped) {
    const bytes t = make_gsub({{kTest, {0}}}, {{7, 0, {extension(1, single_delta({10}, 3))}}});
    ASSERT_TRUE(run_feature(t, run_of({10})) == (V{13}));
}

UTEST(libstb_gsub_tests, test_a_stage_mask_limits_a_lookup_to_the_glyphs_that_carry_it) {
    const bytes t = make_gsub({{kTest, {0}}}, {{1, 0, {single_delta({10}, 1)}}});
    std::vector<glyph_record> g = run_of({10, 10});
    g[0].mask = 1 | stb::mask_init;
    ASSERT_TRUE(run_feature(t, g, stb::mask_init) == (V{11, 10}));
    ASSERT_TRUE(run_feature(t, g, 1) == (V{11, 11}));
}

UTEST(libstb_gsub_tests, test_unsupported_and_empty_tables_do_nothing) {
    ASSERT_TRUE(run_feature(make_gsub({{kTest, {0}}}, {{8, 0, {bytes(10, 0)}}}), run_of({1, 2})) == (V{1, 2}));
    ASSERT_TRUE(run_feature(bytes(), run_of({1, 2})) == (V{1, 2}));
    const stb::otl::gsub_table none;
    ASSERT_TRUE(!none.valid());
    // a feature that is not in the font gives an empty plan
    const bytes t = make_gsub({{kTest, {0}}}, {{1, 0, {single_delta({1}, 1)}}});
    const stb::otl::gsub_table gsub{stb::otl::byte_view(t.data(), t.size())};
    const auto plan = gsub.make_plan({make_tag('a', 'r', 'a', 'b')}, 0, {{{make_tag('n', 'o', 'n', 'e')}, 1}});
    ASSERT_TRUE(plan.stages().empty());
    ASSERT_TRUE(plan.has_feature(kTest));
    ASSERT_TRUE(!plan.has_feature(make_tag('n', 'o', 'n', 'e')));
}

UTEST(libstb_gsub_tests, test_a_truncated_table_never_reads_out_of_range) {
    const bytes t = make_gsub({{kTest, {0, 1}}}, {{1, 0, {single_delta({10}, 1)}},
                                                  {4, 0x08, {ligature(50, {{{51}, 60}})}}});
    for (std::size_t n = 0; n <= t.size(); ++n) {
        const bytes cut(t.begin(), t.begin() + std::ptrdiff_t(n));
        (void)run_feature(cut, run_of({10, 50, 51, 10}));
    }
}

// Damages the GSUB and GDEF tables of a real font in many ways and shapes text with it: the result
// may be wrong, but nothing may crash, loop for ever, read outside the file or grow without limit.
// (Run under AddressSanitizer and UBSan, this found a font that made 50 characters into 300000
// glyphs before the growth and work limits of apply_context existed.)
namespace {
struct file_source final : stb::glyph_source {
    stb::arabic_layout layout;
    std::uint32_t glyph(char32_t cp) const override { return cp >= 0x600 && cp < 0x700 ? std::uint32_t(cp - 0x600 + 1) : 0; }
    const stb::arabic_layout* arabic() const override { return &layout; }
};
}  // namespace

UTEST(libstb_gsub_tests, test_a_damaged_font_never_crashes_the_shaper_or_blows_up) {
    std::ifstream in(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-fa-gsub-test.ttf", std::ios::binary);
    const bytes original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(original.size() > 1000);
    // where GSUB and GDEF are, from the table directory
    auto u32at = [&](std::size_t o) {
        return (std::uint32_t(original[o]) << 24) | (std::uint32_t(original[o + 1]) << 16) |
               (std::uint32_t(original[o + 2]) << 8) | std::uint32_t(original[o + 3]);
    };
    std::vector<std::pair<std::size_t, std::size_t>> regions;
    const unsigned tables = (original[4] << 8) | original[5];
    for (unsigned i = 0; i < tables; ++i) {
        const std::size_t rec = 12 + 16 * std::size_t(i);
        const std::uint32_t tag = u32at(rec);
        if (tag == make_tag('G', 'S', 'U', 'B') || tag == make_tag('G', 'D', 'E', 'F')) regions.push_back({u32at(rec + 8), u32at(rec + 12)});
    }
    ASSERT_TRUE(regions.size() == 2);

    const std::u32string text = U"\u0633\u0644\u0627\u0645 \u0627\u0644\u0644\u0647 \u0645\u0650\u06CC\u200C\u062E\u0648\u0627\u0647\u0645 \u0628\u0651\u064E";
    std::uint64_t state = 88172645463325252ull;
    auto rnd = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int round = 0; round < 800; ++round) {
        bytes font = original;
        const int flips = 1 + int(rnd() % 12);
        for (int k = 0; k < flips; ++k) {
            const auto& r = regions[rnd() % regions.size()];
            const std::size_t at = r.first + rnd() % r.second;
            switch (rnd() % 4) {
                case 0: font[at] = 0xFF; break;
                case 1: font[at] = 0x00; break;
                case 2: font[at] = std::uint8_t(font[at] ^ (1u << (rnd() % 8))); break;
                default: font[at] = std::uint8_t(rnd());
            }
        }
        if (round % 8 == 0) font.resize(font.size() - rnd() % (font.size() / 2));  // also cut it short
        file_source src;
        src.layout = stb::arabic_layout::build(stb::otl::byte_view(font.data(), font.size()), 0);
        ASSERT_TRUE(stb::text_shaper(src).shape(text).size() <= text.size() * 20);
    }
}
