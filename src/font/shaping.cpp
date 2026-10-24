#include "stb/shaping.hpp"

#include "font/shaper/arabic.hpp"
#include "font/shaper/bidi.hpp"
#include "font/shaper/text_shaper.hpp"

namespace stb {
namespace {

// A "font" whose glyph for a code point is the code point itself (0 if has_glyph says no). The
// shaper then returns code points: presentation forms and the visual order, as a string.
class identity_source final : public glyph_source {
public:
    explicit identity_source(const std::function<bool(char32_t)>& has_glyph) : has_(has_glyph) {}
    std::uint32_t glyph(char32_t cp) const override { return (!has_ || has_(cp)) ? std::uint32_t(cp) : 0; }

private:
    const std::function<bool(char32_t)>& has_;
};

}  // namespace

bool has_rtl(std::u32string_view text) { return bidi::has_rtl(text); }

bool is_arabic_mark(char32_t cp) { return arabic::is_mark(cp); }

bool is_arabic_mark_below(char32_t cp) { return arabic::is_mark_below(cp); }

std::u32string shape_for_display(std::u32string_view logical, const std::function<bool(char32_t)>& has_glyph) {
    if (!bidi::has_rtl(logical)) return std::u32string(logical);  // nothing to do, and the common case
    const identity_source source(has_glyph);
    std::u32string out;
    out.reserve(logical.size());
    for (const shaped_glyph& g : text_shaper(source).shape(logical)) out.push_back(g.gid ? char32_t(g.gid) : g.cp);
    return out;
}

}  // namespace stb
