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

// --------------------------------------------------------------- estimate
// Font-free size estimate: advance widths come from fixed tables (see
// tools/gen_font_metrics.py), so the result is the same on every machine.

namespace {

// BEGIN GENERATED (tools/gen_font_metrics.py), do not edit by hand
// All values are in 1/2048 em. Latin table: U+0020..U+00FF; extras: U+2013, U+2014, U+2018, U+2019, U+201C, U+201D, U+2022, U+2026, U+20AC, U+2122.
constexpr int kEstimateLow = 0x20, kEstimateHigh = 0xFF;
constexpr char32_t kEstimateExtraCp[10] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2122};
struct estimate_family {
    std::uint16_t latin[kEstimateHigh - kEstimateLow + 1];
    std::uint16_t extra[10];
    std::uint16_t fallback;  // any other narrow character
    int ascent, descent, line_gap;
};
const estimate_family kEstimate[3] = {
    {  // sans (LiberationSans)
    {
    569, 569, 727, 1139, 1139, 1821, 1366, 391, 682, 682, 797, 1196, 569, 682, 569, 569,
    1139, 1139, 1139, 1139, 1139, 1139, 1139, 1139, 1139, 1139, 569, 569, 1196, 1196, 1196, 1139,
    2079, 1366, 1366, 1479, 1479, 1366, 1251, 1593, 1479, 569, 1024, 1366, 1139, 1706, 1479, 1593,
    1366, 1593, 1479, 1366, 1251, 1479, 1366, 1933, 1366, 1366, 1251, 569, 569, 569, 961, 1139,
    682, 1139, 1139, 1024, 1139, 1139, 569, 1139, 1139, 455, 455, 1024, 455, 1706, 1139, 1139,
    1139, 1139, 682, 1024, 569, 1139, 1024, 1479, 1024, 1024, 1024, 684, 532, 684, 1196, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    569, 682, 1139, 1139, 1139, 1139, 532, 1139, 682, 1509, 758, 1139, 1196, 682, 1509, 1131,
    819, 1124, 682, 682, 682, 1180, 1100, 682, 682, 682, 748, 1139, 1708, 1708, 1708, 1251,
    1366, 1366, 1366, 1366, 1366, 1366, 2048, 1479, 1366, 1366, 1366, 1366, 569, 569, 569, 569,
    1479, 1479, 1593, 1593, 1593, 1593, 1593, 1196, 1593, 1479, 1479, 1479, 1479, 1366, 1366, 1251,
    1139, 1139, 1139, 1139, 1139, 1139, 1821, 1024, 1139, 1139, 1139, 1139, 569, 569, 569, 569,
    1139, 1139, 1139, 1139, 1139, 1139, 1139, 1124, 1251, 1139, 1139, 1139, 1139, 1024, 1139, 1024,
    },
    {1139, 2048, 455, 455, 682, 682, 717, 2048, 1139, 2048},
    1195,
    1854, -434, 67,
    },
    {  // serif (LiberationSerif)
    {
    512, 682, 836, 1024, 1024, 1706, 1593, 369, 682, 682, 1024, 1155, 512, 682, 512, 569,
    1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 569, 569, 1155, 1155, 1155, 909,
    1886, 1479, 1366, 1366, 1479, 1251, 1139, 1479, 1479, 682, 797, 1479, 1251, 1821, 1479, 1479,
    1139, 1479, 1366, 1139, 1251, 1479, 1479, 1933, 1479, 1479, 1251, 682, 569, 682, 961, 1024,
    682, 909, 1024, 909, 1024, 909, 682, 1024, 1024, 569, 569, 1024, 569, 1593, 1024, 1024,
    1024, 1024, 682, 797, 569, 1024, 1024, 1479, 1024, 1024, 909, 983, 410, 983, 1108, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    512, 682, 1024, 1024, 1024, 1024, 410, 1024, 682, 1556, 565, 1024, 1155, 682, 1556, 1024,
    819, 1124, 614, 614, 682, 1180, 928, 682, 682, 614, 635, 1024, 1536, 1536, 1536, 909,
    1479, 1479, 1479, 1479, 1479, 1479, 1821, 1366, 1251, 1251, 1251, 1251, 682, 682, 682, 682,
    1479, 1479, 1479, 1479, 1479, 1479, 1479, 1155, 1479, 1479, 1479, 1479, 1479, 1479, 1139, 1024,
    909, 909, 909, 909, 909, 909, 1366, 909, 909, 909, 909, 909, 569, 569, 569, 569,
    1024, 1024, 1024, 1024, 1024, 1024, 1024, 1124, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
    },
    {1024, 2048, 682, 682, 909, 909, 717, 2048, 1024, 2007},
    1153,
    1825, -443, 87,
    },
    {  // mono (LiberationMono)
    {
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
    },
    {1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229},
    1229,
    1705, -615, 0,
    },
};
// END GENERATED

constexpr float kEm = 2048.0f;

const estimate_family& family_data(font_family f) {
    switch (f) {
        case font_family::sans: return kEstimate[0];
        case font_family::serif: return kEstimate[1];
        case font_family::mono: return kEstimate[2];
    }
    throw std::invalid_argument("unknown font family");
}

// Characters that take no room (combining marks, joiners, variation selectors) ...
bool is_zero_width(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x20D0 && c <= 0x20FF) ||
           (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF || c == 0x2060;
}

// ... and the ones that take a full em (CJK, Hangul, fullwidth forms, emoji).
bool is_wide(char32_t c) {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) ||
           (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x3FFFD);
}

float estimate_units(const estimate_family& fam, char32_t c) {
    if (c >= char32_t(kEstimateLow) && c <= char32_t(kEstimateHigh)) return fam.latin[c - kEstimateLow];
    for (int i = 0; i < int(sizeof kEstimateExtraCp / sizeof kEstimateExtraCp[0]); ++i)
        if (kEstimateExtraCp[i] == c) return fam.extra[i];
    if (c < 0x20 || is_zero_width(c)) return 0.0f;
    if (is_wide(c)) return kEm;
    return fam.fallback;
}

}  // namespace

font_metrics estimate_metrics(float px, font_family family) {
    check_px(px);
    const estimate_family& f = family_data(family);
    const float s = px / (f.ascent - f.descent);  // same scale stbtt_ScaleForPixelHeight uses
    return {f.ascent * s, f.descent * s, f.line_gap * s};
}

text_size estimate_text(std::string_view utf8, float px, font_family family) {
    check_px(px);
    const estimate_family& f = family_data(family);
    const float s = px / (f.ascent - f.descent);
    const float line_h = float(f.ascent - f.descent + f.line_gap) * s;

    float x = 0, widest = 0;
    int lines = 1;
    for (char32_t c : utf8_decode(utf8)) {
        if (c == U'\n') {
            widest = std::max(widest, x);
            x = 0;
            ++lines;
        } else if (c != U'\r') {
            x += estimate_units(f, c) * s;
        }
    }
    widest = std::max(widest, x);
    return {widest, lines * line_h, lines};
}

}  // namespace stb
