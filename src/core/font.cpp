#include "stb/font.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define STB_FONT_HAVE_MMAP 1
#endif

#include "file.hpp"
#include "stb/utf8.hpp"

// STATIC: internal linkage for every stbtt_* / stbrp_* function (see image.cpp).
// stb_rect_pack must be compiled first: stb_truetype detects it and uses it
// for the packing API instead of its built-in placeholder types.
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace stb {

// ------------------------------------------------------------------ pimpl

// The font file's bytes: either owned memory or a read-only mapping of the file.
// stbtt_fontinfo points into this, so it is never moved after init.
struct font_bytes {
    std::vector<std::uint8_t> owned;
    const std::uint8_t* ptr = nullptr;
    std::size_t len = 0;
#ifdef STB_FONT_HAVE_MMAP
    void* map = nullptr;
#endif

    font_bytes() = default;
    font_bytes(const font_bytes&) = delete;
    font_bytes& operator=(const font_bytes&) = delete;
    ~font_bytes() {
#ifdef STB_FONT_HAVE_MMAP
        if (map) ::munmap(map, len);
#endif
    }

    void own(std::vector<std::uint8_t> v) {
        owned = std::move(v);
        ptr = owned.data();
        len = owned.size();
    }

#ifdef STB_FONT_HAVE_MMAP
    // Maps the file instead of reading it: opening a 750 KB font costs a few
    // microseconds, and only the pages stb_truetype actually touches are paged in.
    // Returns false (caller falls back to reading) on any problem, including the
    // empty / over-2-GiB files read_file reports a proper error for.
    bool map_file(const std::filesystem::path& path) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        const bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= INT_MAX;
        void* m = ok ? ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
        ::close(fd);
        if (m == MAP_FAILED) return false;
        map = m;
        ptr = static_cast<const std::uint8_t*>(m);
        len = static_cast<std::size_t>(st.st_size);
        return true;
    }
#endif
};

struct font::impl {
    font_bytes data;
    stbtt_fontinfo info{};
    int index = 0;

    float scale(float pixel_height) const { return stbtt_ScaleForPixelHeight(&info, pixel_height); }
};

namespace {

void check_px(float px) {
    if (!(px > 0.0f) || px > font::max_pixel_height)  // also rejects NaN
        throw std::invalid_argument("pixel_height must be in (0, " +
                                    std::to_string(int(font::max_pixel_height)) + "]");
}

int check_cp(char32_t cp) {
    if (cp > 0x10FFFF) throw std::invalid_argument("code point out of range");
    return static_cast<int>(cp);
}

struct bitmap_free {
    void operator()(unsigned char* p) const { stbtt_FreeBitmap(p, nullptr); }
};

// One pass over the text: where every glyph's pen and baseline are.
struct layout {
    struct item {
        char32_t cp;
        float x;         // pen position
        float baseline;
    };
    std::vector<item> items;
    float width = 0;
    float height = 0;
    int lines = 1;
};

layout do_layout(const font::impl& f, const std::u32string& text, float px) {
    const float s = f.scale(px);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    const float ascent = asc * s;
    const float line_h = (asc - desc + gap) * s;

    layout L;
    float x = 0;
    int line = 0;
    char32_t prev = 0;
    bool has_prev = false;
    for (char32_t cp : text) {
        if (cp == U'\n') {
            L.width = std::max(L.width, x);
            x = 0;
            ++line;
            has_prev = false;
            continue;
        }
        if (cp == U'\r') continue;
        if (has_prev) x += stbtt_GetCodepointKernAdvance(&f.info, int(prev), int(cp)) * s;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, int(cp), &adv, &lsb);
        L.items.push_back({cp, x, ascent + line * line_h});
        x += adv * s;
        prev = cp;
        has_prev = true;
    }
    L.width = std::max(L.width, x);
    L.lines = line + 1;
    L.height = L.lines * line_h;
    return L;
}

}  // namespace

// ------------------------------------------------------------------ atlas

atlas::atlas(image bitmap, std::vector<atlas_glyph> glyphs)
    : bitmap_(std::move(bitmap)), glyphs_(std::move(glyphs)) {
    for (std::size_t i = 0; i < glyphs_.size(); ++i) index_.emplace(glyphs_[i].codepoint, i);
}

const atlas_glyph* atlas::find(char32_t codepoint) const noexcept {
    const auto it = index_.find(codepoint);
    return it == index_.end() ? nullptr : &glyphs_[it->second];
}

// ------------------------------------------------------------------- font

namespace {

void init_font(font::impl* p, int index) {
    const int offset = stbtt_GetFontOffsetForIndex(p->data.ptr, index);
    if (offset < 0) throw decode_error("no font at this index (not a TrueType/OpenType file?)");
    if (!stbtt_InitFont(&p->info, p->data.ptr, offset))
        throw decode_error("not a valid TrueType/OpenType font");
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&p->info, &asc, &desc, &gap);
    if (asc - desc <= 0) throw decode_error("font has no usable vertical metrics");
}

}  // namespace

font font::from_memory(std::vector<std::uint8_t> data, int index) {
    if (index < 0) throw std::invalid_argument("font index must be >= 0");
    if (data.empty()) throw decode_error("empty font data");

    auto p = std::make_shared<impl>();
    p->data.own(std::move(data));
    p->index = index;
    init_font(p.get(), index);
    return font(std::move(p));
}

font font::open(const std::filesystem::path& path, int index) {
    if (index < 0) throw std::invalid_argument("font index must be >= 0");
    auto p = std::make_shared<impl>();
    p->index = index;
#ifdef STB_FONT_HAVE_MMAP
    if (!p->data.map_file(path))
#endif
    {
        p->data.own(detail::read_file(path));  // throws io_error / limit_error with the right errno
        if (p->data.len == 0) throw decode_error("empty font data");
    }
    init_font(p.get(), index);
    return font(std::move(p));
}

font_metrics font::metrics(float px) const {
    check_px(px);
    const float s = impl_->scale(px);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &asc, &desc, &gap);
    return {asc * s, desc * s, gap * s};
}

bool font::has_glyph(char32_t cp) const { return stbtt_FindGlyphIndex(&impl_->info, check_cp(cp)) != 0; }

float font::advance(char32_t cp, float px) const {
    check_px(px);
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, check_cp(cp), &adv, &lsb);
    return adv * impl_->scale(px);
}

float font::kerning(char32_t left, char32_t right, float px) const {
    check_px(px);
    return stbtt_GetCodepointKernAdvance(&impl_->info, check_cp(left), check_cp(right)) * impl_->scale(px);
}

glyph font::render_glyph(char32_t cp, float px) const {
    check_px(px);
    const int c = check_cp(cp);
    const float s = impl_->scale(px);

    int w = 0, h = 0, xo = 0, yo = 0;
    std::unique_ptr<unsigned char, bitmap_free> bmp(
        stbtt_GetCodepointBitmap(&impl_->info, s, s, c, &w, &h, &xo, &yo));

    glyph g;
    g.x_offset = xo;
    g.y_offset = yo;
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, c, &adv, &lsb);
    g.advance = adv * s;
    if (bmp && w > 0 && h > 0)
        g.bitmap = image(w, h, 1, std::vector<std::uint8_t>(bmp.get(), bmp.get() + std::size_t(w) * std::size_t(h)));
    return g;
}

text_size font::measure(std::string_view utf8, float px) const {
    check_px(px);
    const layout L = do_layout(*impl_, utf8_decode(utf8), px);
    return {L.width, L.height, L.lines};
}

text_bitmap font::render(std::string_view utf8, float px, std::size_t max_bytes) const {
    check_px(px);
    const layout L = do_layout(*impl_, utf8_decode(utf8), px);

    // Rasterise each distinct character once.
    std::unordered_map<char32_t, glyph> cache;
    for (const auto& it : L.items)
        if (!cache.count(it.cp)) cache.emplace(it.cp, render_glyph(it.cp, px));

    // Canvas = layout box plus any overhanging ink.
    long long minx = 0, miny = 0;
    long long maxx = std::llround(std::ceil(L.width));
    long long maxy = std::llround(std::ceil(L.height));
    struct placed {
        const glyph* g;
        long long x, y;
    };
    std::vector<placed> placements;
    for (const auto& it : L.items) {
        const glyph& g = cache.at(it.cp);
        if (g.bitmap.empty()) continue;
        const long long gx = std::llround(it.x) + g.x_offset;
        const long long gy = std::llround(it.baseline) + g.y_offset;
        minx = std::min(minx, gx);
        miny = std::min(miny, gy);
        maxx = std::max(maxx, gx + g.bitmap.width());
        maxy = std::max(maxy, gy + g.bitmap.height());
        placements.push_back({&g, gx, gy});
    }
    const long long w = maxx - minx, h = maxy - miny;
    if (w < 1 || h < 1) throw std::invalid_argument("text produces an empty image");
    if (w > INT_MAX || h > INT_MAX ||
        std::uint64_t(w) * std::uint64_t(h) > std::min<std::uint64_t>(max_bytes, INT_MAX))
        throw limit_error("rendered text too large: " + std::to_string(w) + "x" + std::to_string(h));

    image canvas(int(w), int(h), 1);
    for (const auto& p : placements) {
        const image& b = p.g->bitmap;
        for (int row = 0; row < b.height(); ++row) {
            std::uint8_t* dst = canvas.data() + std::size_t(p.y - miny + row) * canvas.stride() + std::size_t(p.x - minx);
            const std::uint8_t* src = b.data() + std::size_t(row) * b.stride();
            // Saturating add: overlapping glyphs (kerning, italics) accumulate coverage.
            for (int col = 0; col < b.width(); ++col)
                dst[col] = static_cast<std::uint8_t>(std::min(255, int(dst[col]) + int(src[col])));
        }
    }
    return {std::move(canvas), int(-minx), int(-miny)};
}

atlas font::make_atlas(std::u32string_view codepoints, float px, int width, int height, int padding) const {
    check_px(px);
    if (width < 1 || height < 1) throw std::invalid_argument("atlas width and height must be >= 1");
    if (padding < 0) throw std::invalid_argument("padding must be >= 0");
    if (std::uint64_t(width) * std::uint64_t(height) > std::uint64_t(INT_MAX))
        throw limit_error("atlas larger than 2 GiB");

    std::vector<int> ids;
    std::unordered_set<char32_t> seen;
    for (char32_t cp : codepoints)
        if (seen.insert(cp).second) ids.push_back(check_cp(cp));
    if (ids.empty()) throw std::invalid_argument("no code points given");

    std::vector<std::uint8_t> pixels(std::size_t(width) * std::size_t(height), 0);
    std::vector<stbtt_packedchar> packed(ids.size());

    stbtt_pack_context spc;
    if (!stbtt_PackBegin(&spc, pixels.data(), width, height, 0, padding, nullptr))
        throw error("cannot initialise the atlas packer (out of memory?)");
    stbtt_pack_range range{};
    range.font_size = px;  // positive = pixel height
    range.array_of_unicode_codepoints = ids.data();
    range.num_chars = int(ids.size());
    range.chardata_for_range = packed.data();
    const int ok = stbtt_PackFontRanges(&spc, impl_->data.ptr, impl_->index, &range, 1);
    stbtt_PackEnd(&spc);
    if (!ok) throw limit_error("atlas too small for the requested glyphs");

    std::vector<atlas_glyph> glyphs;
    glyphs.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const stbtt_packedchar& c = packed[i];
        glyphs.push_back({char32_t(ids[i]), c.x0, c.y0, c.x1, c.y1, c.xoff, c.yoff, c.xoff2, c.yoff2, c.xadvance});
    }
    return atlas(image(width, height, 1, std::move(pixels)), std::move(glyphs));
}

}  // namespace stb
