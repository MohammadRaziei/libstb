#include "text_shaper.hpp"

#include <algorithm>

#include "arabic.hpp"
#include "bidi.hpp"

namespace stb {

// ------------------------------------------------------------ arabic_layout

arabic_layout arabic_layout::build(otl::byte_view file, std::uint32_t font_offset) {
    arabic_layout r;
    const otl::byte_view table = otl::find_table(file, font_offset, otl::make_tag('G', 'S', 'U', 'B'));
    if (table.empty()) return r;
    r.defs = otl::gdef(otl::find_table(file, font_offset, otl::make_tag('G', 'D', 'E', 'F')));
    r.gsub = otl::gsub_table(table);

    using otl::make_tag;
    const std::uint32_t isol = make_tag('i', 's', 'o', 'l'), fina = make_tag('f', 'i', 'n', 'a'),
                        medi = make_tag('m', 'e', 'd', 'i'), init = make_tag('i', 'n', 'i', 't'),
                        rlig = make_tag('r', 'l', 'i', 'g');
    // The order of the Arabic shaping model: decomposition first, then the four shapes, then the
    // required ligatures, contextual alternates and the optional ligatures.
    const std::vector<otl::stage_request> stages = {
        {{make_tag('c', 'c', 'm', 'p'), make_tag('l', 'o', 'c', 'l')}, mask_all},
        {{isol}, mask_isol},
        {{fina}, mask_fina},
        {{medi, make_tag('m', 'e', 'd', '2')}, mask_medi},
        {{init}, mask_init},
        {{rlig}, mask_all},
        {{make_tag('c', 'a', 'l', 't')}, mask_all},
        {{make_tag('l', 'i', 'g', 'a'), make_tag('c', 'l', 'i', 'g'), make_tag('r', 'c', 'l', 't')}, mask_all},
    };
    r.plan = r.gsub.make_plan({make_tag('a', 'r', 'a', 'b'), make_tag('D', 'F', 'L', 'T')}, 0, stages);
    r.usable = !r.plan.stages().empty();
    r.has_forms = r.plan.has_feature(isol) || r.plan.has_feature(fina) || r.plan.has_feature(medi) ||
                  r.plan.has_feature(init);
    r.has_rlig = r.plan.has_feature(rlig);
    return r;
}

// ------------------------------------------------------------- text_shaper

std::vector<shaped_glyph> text_shaper::shape(std::u32string_view text) const {
    std::vector<shaped_glyph> out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos <= text.size();) {
        std::size_t end = text.find(U'\n', pos);
        if (end == std::u32string_view::npos) end = text.size();
        shape_line(text.substr(pos, end - pos), out);
        if (end < text.size()) out.push_back({0, U'\n'});
        pos = end + 1;
    }
    return out;
}

namespace {

std::uint32_t form_bit(arabic::form f) {
    switch (f) {
        case arabic::form::isol: return mask_isol;
        case arabic::form::fina: return mask_fina;
        case arabic::form::medi: return mask_medi;
        case arabic::form::init: return mask_init;
        default: return 0;
    }
}

// Runs the font's GSUB features over every stretch of Arabic text of the line (a stretch ends at
// a left-to-right letter, so Latin words are never touched by Arabic features).
void apply_gsub(const arabic_layout& al, std::vector<otl::glyph_record>& g) {
    for (std::size_t a = 0; a < g.size();) {
        if (bidi::classify(g[a].cp) == bidi::cls::L) {
            ++a;
            continue;
        }
        std::size_t b = a;
        bool arabic_text = false;
        while (b < g.size() && bidi::classify(g[b].cp) != bidi::cls::L) {
            arabic_text = arabic_text || arabic::is_arabic_script(g[b].cp);
            ++b;
        }
        if (arabic_text) {
            std::vector<otl::glyph_record> run(g.begin() + std::ptrdiff_t(a), g.begin() + std::ptrdiff_t(b));
            al.gsub.apply(al.plan, run, &al.defs);
            g.erase(g.begin() + std::ptrdiff_t(a), g.begin() + std::ptrdiff_t(b));
            g.insert(g.begin() + std::ptrdiff_t(a), run.begin(), run.end());
            b = a + run.size();
        }
        a = b;
    }
}

}  // namespace

void text_shaper::shape_line(std::u32string_view line, std::vector<shaped_glyph>& out) const {
    if (!bidi::has_rtl(line)) {
        for (char32_t c : line) out.push_back({src_.glyph(c), c});
        return;
    }

    const arabic_layout* al = src_.arabic();
    const bool gsub = al && al->usable;
    const bool presentation_ligatures = !(gsub && al->has_rlig);
    const std::vector<arabic::form> forms = arabic::joining_forms(line);

    // 1-2. One glyph per character, in logical order.
    // The font's GDEF says what a glyph is; only a font without glyph classes is guessed at, from
    // the character (a glyph GDEF does not list is then not a mark, as in other shapers).
    auto glyph_class = [&](std::uint32_t gid, char32_t cp) -> std::uint8_t {
        if (gsub && al->defs.has_glyph_classes()) return al->defs.glyph_class(gid);
        return arabic::is_mark(cp) ? otl::gc_mark : otl::gc_base;
    };
    std::vector<otl::glyph_record> g;
    g.reserve(line.size());
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char32_t c = line[i];
        if (arabic::is_invisible(c)) continue;

        // Lam + alef as one presentation-form ligature (when the font's GSUB does not do it).
        if (presentation_ligatures && c == 0x0644 && i + 1 < line.size()) {
            const bool joined_before = forms[i] == arabic::form::medi;
            if (const char32_t lig = arabic::lam_alef_ligature(line[i + 1], joined_before)) {
                if (const std::uint32_t gid = src_.glyph(lig)) {
                    g.push_back({gid, c, mask_all, glyph_class(gid, c)});
                    ++i;
                    continue;
                }
            }
        }

        const arabic::form f = forms[i];
        std::uint32_t gid = 0, mask = mask_all;
        if (f != arabic::form::none) {
            if (gsub && al->has_forms) {
                gid = src_.glyph(c);
                mask |= form_bit(f);
            }
            if (!gid) {
                const char32_t p = arabic::presentation_form(c, f);
                if (p) gid = src_.glyph(p);
            }
        }
        if (!gid) gid = src_.glyph(c);
        g.push_back({gid, c, mask, glyph_class(gid, c)});
    }

    // 3. GSUB features of the font.
    if (gsub) apply_gsub(*al, g);

    // 4. Visual order. A glyph is classified by the character it stands for.
    std::vector<bidi::cls> classes(g.size());
    for (std::size_t k = 0; k < g.size(); ++k) classes[k] = bidi::classify(g[k].cp);
    const bidi::result order = bidi::resolve(classes);

    const std::size_t first = out.size();
    for (std::uint32_t k : order.order) {
        shaped_glyph s{g[k].gid, g[k].cp};
        if (s.cp == 0x200E || s.cp == 0x200F) continue;  // LRM, RLM: only steered the order
        if (order.level[k] & 1) {
            const char32_t m = bidi::mirrored(s.cp);
            if (m != s.cp) {
                s.cp = m;
                s.gid = src_.glyph(m);
            }
        }
        out.push_back(s);
    }

    // Shadda goes first among the marks of one letter, so a vowel sign stacks above it.
    for (std::size_t i = first; i < out.size();) {
        if (!arabic::is_mark(out[i].cp)) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < out.size() && arabic::is_mark(out[j].cp)) ++j;
        std::stable_partition(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(j),
                              [](const shaped_glyph& s) { return s.cp == 0x0651; });
        i = j;
    }
}

}  // namespace stb
