#include "text_shaper.hpp"

#include <algorithm>

#include "arabic.hpp"
#include "bidi.hpp"

namespace stb {

// ------------------------------------------------------------ arabic_layout

arabic_layout arabic_layout::build(otl::byte_view file, std::uint32_t font_offset) {
    arabic_layout r;
    using otl::make_tag;
    const std::initializer_list<std::uint32_t> scripts = {make_tag('a', 'r', 'a', 'b'), make_tag('D', 'F', 'L', 'T')};
    r.defs = otl::gdef(otl::find_table(file, font_offset, make_tag('G', 'D', 'E', 'F')));
    const otl::byte_view gpos_table = otl::find_table(file, font_offset, make_tag('G', 'P', 'O', 'S'));
    r.gpos = otl::positioner(gpos_table, scripts);

    // Left-to-right scripts: the script's own tag, then the default one, then Latin (what other shapers
    // fall back to for a font that has no table for the script).
    const std::uint32_t ltr_tags[script_count] = {make_tag('l', 'a', 't', 'n'), make_tag('g', 'r', 'e', 'k'),
                                                  make_tag('c', 'y', 'r', 'l')};
    const std::uint32_t dflt = make_tag('D', 'F', 'L', 'T'), latn = ltr_tags[script_latn];
    for (std::size_t k = 0; k < script_count; ++k) r.ltr[k].gpos = otl::positioner(gpos_table, {ltr_tags[k], dflt, latn});

    const otl::byte_view table = otl::find_table(file, font_offset, make_tag('G', 'S', 'U', 'B'));
    const auto finish_ltr = [&r] {
        r.shapes_ltr = false;
        for (const script_layout& l : r.ltr) r.shapes_ltr = r.shapes_ltr || l.usable();
    };
    if (table.empty()) {
        finish_ltr();
        return r;
    }
    r.gsub = otl::gsub_table(table);

    const std::uint32_t isol = make_tag('i', 's', 'o', 'l'), fina = make_tag('f', 'i', 'n', 'a'),
                        medi = make_tag('m', 'e', 'd', 'i'), init = make_tag('i', 'n', 'i', 't'),
                        rlig = make_tag('r', 'l', 'i', 'g');
    // The order of the Arabic shaping model: decomposition first, then the four shapes, then the
    // required ligatures, contextual alternates and the optional ligatures.
    const auto stages = [&](bool ligatures) {
        std::vector<otl::stage_request> s = {
            {{make_tag('c', 'c', 'm', 'p'), make_tag('l', 'o', 'c', 'l')}, mask_all},
            {{isol}, mask_isol},
            {{fina}, mask_fina},
            {{medi, make_tag('m', 'e', 'd', '2')}, mask_medi},
            {{init}, mask_init},
            {{rlig}, mask_all},
            {{make_tag('c', 'a', 'l', 't')}, mask_all},
        };
        // The last stage: the optional ligatures and the required contextual alternates. Without
        // ligatures only the required ones are left.
        if (ligatures)
            s.push_back({{make_tag('l', 'i', 'g', 'a'), make_tag('c', 'l', 'i', 'g'), make_tag('r', 'c', 'l', 't')}, mask_all});
        else
            s.push_back({{make_tag('r', 'c', 'l', 't')}, mask_all});
        return s;
    };
    r.plan = r.gsub.make_plan(scripts, 0, stages(true));
    r.plan_plain = r.gsub.make_plan(scripts, 0, stages(false));
    r.usable = !r.plan.stages().empty();
    r.has_forms = r.plan.has_feature(isol) || r.plan.has_feature(fina) || r.plan.has_feature(medi) ||
                  r.plan.has_feature(init);
    r.has_rlig = r.plan.has_feature(rlig);

    // The same stages without the shapes of Arabic: decomposition, required ligatures, contextual
    // alternates, then the optional ligatures (none of them when ligatures are off).
    const auto ltr_stages = [&](bool ligatures) {
        std::vector<otl::stage_request> s = {
            {{make_tag('c', 'c', 'm', 'p'), make_tag('l', 'o', 'c', 'l')}, mask_all},
            {{rlig}, mask_all},
            {{make_tag('c', 'a', 'l', 't')}, mask_all},
        };
        if (ligatures)
            s.push_back({{make_tag('l', 'i', 'g', 'a'), make_tag('c', 'l', 'i', 'g'), make_tag('r', 'c', 'l', 't')}, mask_all});
        else
            s.push_back({{make_tag('r', 'c', 'l', 't')}, mask_all});
        return s;
    };
    for (std::size_t k = 0; k < script_count; ++k) {
        r.ltr[k].plan = r.gsub.make_plan({ltr_tags[k], dflt, latn}, 0, ltr_stages(true));
        r.ltr[k].plan_plain = r.gsub.make_plan({ltr_tags[k], dflt, latn}, 0, ltr_stages(false));
        r.ltr[k].has_gsub = !r.ltr[k].plan.stages().empty();
    }
    finish_ltr();
    return r;
}

// ------------------------------------------------------------- text_shaper

std::vector<visual_glyph> text_shaper::shape(std::u32string_view text, const shaper_options& options) const {
    std::vector<visual_glyph> out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos <= text.size();) {
        std::size_t end = text.find(U'\n', pos);
        if (end == std::u32string_view::npos) end = text.size();
        shape_line(text.substr(pos, end - pos), pos, options, out);
        if (end < text.size()) out.push_back({0, U'\n', static_cast<std::uint32_t>(end)});
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
void apply_gsub(const arabic_layout& al, const otl::shaping_plan& plan, std::vector<otl::glyph_record>& g) {
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
            al.gsub.apply(plan, run, &al.defs);
            g.erase(g.begin() + std::ptrdiff_t(a), g.begin() + std::ptrdiff_t(b));
            g.insert(g.begin() + std::ptrdiff_t(a), run.begin(), run.end());
            b = a + run.size();
        }
        a = b;
    }
}

}  // namespace

namespace {

// The order of the marks of one letter, as in Unicode's canonical ordering (their combining
// class) with the shadda first: so that a vowel sign stacks above it and the font's mark to mark
// anchors apply in the order they were designed for, whatever order the text was typed in.
int mark_order(char32_t cp) {
    if (cp == 0x0651) return 0;
    if (cp >= 0x064B && cp <= 0x0652) return int(cp - 0x064B) + 27;  // fathatan ... sukun (the shadda is above)
    if (cp >= 0x0618 && cp <= 0x061A) return int(cp - 0x0618) + 30;
    if (cp == 0x0670) return 35;
    if (cp == 0x0655 || cp == 0x0656 || cp == 0x065C || cp == 0x065F || cp == 0x06E3 || cp == 0x06EA ||
        cp == 0x06ED)
        return 220;  // below
    return 230;      // above: the other marks
}

template <class Glyph>
void order_marks(std::vector<Glyph>& out, std::size_t first) {
    for (std::size_t i = first; i < out.size();) {
        if (!arabic::is_mark(out[i].cp)) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < out.size() && arabic::is_mark(out[j].cp)) ++j;
        std::stable_sort(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(j),
                         [](const Glyph& a, const Glyph& b) { return mark_order(a.cp) < mark_order(b.cp); });
        i = j;
    }
}

}  // namespace

std::vector<otl::glyph_record> text_shaper::to_glyphs(std::u32string_view line, std::size_t first,
                                                      const arabic_layout* al) const {
    const bool gsub = al && al->usable;
    const bool presentation_ligatures = !(gsub && al->has_rlig);
    const std::vector<arabic::form> forms = arabic::joining_forms(line);

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
                    g.push_back({gid, c, mask_all, glyph_class(gid, c), static_cast<std::uint32_t>(first + i)});
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
        g.push_back({gid, c, mask, glyph_class(gid, c), static_cast<std::uint32_t>(first + i)});
    }
    return g;
}

void text_shaper::append_in_visual_order(const std::vector<otl::glyph_record>& g, bidi::base_dir direction,
                                          std::vector<visual_glyph>& out) const {
    // A glyph is classified by the character it stands for.
    std::vector<bidi::cls> classes(g.size());
    for (std::size_t k = 0; k < g.size(); ++k) classes[k] = bidi::classify(g[k].cp);
    const bidi::result order = bidi::resolve(classes, direction);

    for (std::uint32_t k : order.order) {
        visual_glyph s{g[k].gid,    g[k].cp,     g[k].cluster, g[k].attached, g[k].mark_x,
                       g[k].mark_y, g[k].kern,   g[k].shift_x, g[k].shift_y};
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
}

namespace {

// The shaped script of a character: an index into arabic_layout::ltr, or -1 for the other scripts and
// -2 for a character that takes the script of its neighbours (spaces, digits, punctuation, combining marks).
int ltr_script_of(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ? int(script_latn) : -2;
    if (c >= 0x0300 && c <= 0x036F) return -2;  // combining diacritical marks
    if (c == 0x00AA || c == 0x00BA || (c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7)) return int(script_latn);
    if (c >= 0x0250 && c <= 0x02AF) return int(script_latn);  // IPA extensions
    if (c >= 0x1E00 && c <= 0x1EFF) return int(script_latn);  // Latin extended additional
    if (c >= 0x2C60 && c <= 0x2C7F) return int(script_latn);
    if (c >= 0xA720 && c <= 0xA7FF) return int(script_latn);
    if (c >= 0xFB00 && c <= 0xFB06) return int(script_latn);  // the Latin ligature characters
    if ((c >= 0x0370 && c <= 0x03FF) || (c >= 0x1F00 && c <= 0x1FFF)) return c == 0x037E || c == 0x0387 ? -2 : int(script_grek);
    if ((c >= 0x0400 && c <= 0x052F) || (c >= 0x1C80 && c <= 0x1C8F) || (c >= 0x2DE0 && c <= 0x2DFF) ||
        (c >= 0xA640 && c <= 0xA69F))
        return int(script_cyrl);
    // Common characters: space and the punctuation, symbols and digits of the Latin-1 and General
    // Punctuation blocks, and the other combining marks.
    if (c < 0x00C0 || (c >= 0x2000 && c <= 0x206F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
        (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F))
        return -2;
    return -1;
}

bool is_combining_mark(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

}  // namespace

void text_shaper::shape_ltr(std::u32string_view line, std::size_t first, const shaper_options& options,
                            const arabic_layout& al, std::vector<visual_glyph>& out) const {
    const std::size_t n = line.size();
    std::vector<otl::glyph_record> g;
    g.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t gid = src_.glyph(line[i]);
        // GDEF says what a glyph is; a font without glyph classes is guessed at from the character.
        std::uint8_t cls = is_combining_mark(line[i]) ? std::uint8_t(otl::gc_mark) : std::uint8_t(otl::gc_base);
        if (al.defs.has_glyph_classes()) cls = al.defs.glyph_class(gid);
        g.push_back({gid, line[i], mask_all, cls, static_cast<std::uint32_t>(first + i)});
    }

    // The script of each character; a common one takes that of the character before it (the one
    // after it at the start of the line, Latin when the whole line is common).
    std::vector<int> script(n);
    for (std::size_t i = 0; i < n; ++i) script[i] = ltr_script_of(line[i]);
    int carry = -2;
    for (std::size_t i = 0; i < n; ++i) {
        if (script[i] == -2) script[i] = carry;
        else carry = script[i];
    }
    carry = -2;
    for (std::size_t i = n; i-- > 0;) {
        if (script[i] == -2) script[i] = carry;
        else carry = script[i];
    }
    for (int& v : script)
        if (v == -2) v = int(script_latn);

    for (std::size_t a = 0; a < n;) {
        std::size_t b = a;
        while (b < n && script[b] == script[a]) ++b;
        const bool shaped = script[a] >= 0 && al.ltr[std::size_t(script[a])].usable();
        if (!shaped) {
            for (std::size_t k = a; k < b; ++k) out.push_back({g[k].gid, g[k].cp, g[k].cluster});
        } else {
            const script_layout& sl = al.ltr[std::size_t(script[a])];
            std::vector<otl::glyph_record> run(g.begin() + std::ptrdiff_t(a), g.begin() + std::ptrdiff_t(b));
            if (sl.has_gsub) al.gsub.apply(options.ligatures ? sl.plan : sl.plan_plain, run, &al.defs);
            if (sl.gpos.usable()) sl.gpos.position(run, &al.defs);
            const bool kerned = sl.gpos.kerns();
            for (const otl::glyph_record& r : run)
                out.push_back({r.gid, r.cp, r.cluster, r.attached, r.mark_x, r.mark_y, r.kern, r.shift_x, r.shift_y, kerned});
        }
        a = b;
    }
}

void text_shaper::shape_line(std::u32string_view line, std::size_t first, const shaper_options& options,
                             std::vector<visual_glyph>& out) const {
    // Left-to-right text under a left-to-right (or automatic) paragraph: per script run, with the
    // font's features when it has any for the script, else one glyph per character.
    if (options.direction != bidi::base_dir::rtl && !bidi::has_rtl(line)) {
        const arabic_layout* ltr = src_.arabic();
        if (ltr && ltr->shapes_ltr) {
            shape_ltr(line, first, options, *ltr, out);
            return;
        }
        for (std::size_t i = 0; i < line.size(); ++i)
            out.push_back({src_.glyph(line[i]), line[i], static_cast<std::uint32_t>(first + i)});
        return;
    }

    const arabic_layout* al = src_.arabic();
    std::vector<otl::glyph_record> g = to_glyphs(line, first, al);  // 1-2. glyphs in logical order
    if (al && al->usable) apply_gsub(*al, options.ligatures ? al->plan : al->plan_plain, g);  // 3. GSUB features

    if (al && al->gpos.usable()) {                                    // 4. GPOS: kerning, marks
        order_marks(g, 0);
        al->gpos.position(g, &al->defs);
    }

    const std::size_t begin = out.size();
    append_in_visual_order(g, options.direction, out);               // 5. visual order
    order_marks(out, begin);
    if (al && al->gpos.kerns())
        for (std::size_t k = begin; k < out.size(); ++k) out[k].kerned = arabic::is_arabic_script(out[k].cp);
}

}  // namespace stb
