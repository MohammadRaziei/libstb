#include "stb/font.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
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

#include "util/file.hpp"
#include "stb/shaping.hpp"
#include "font/shaper/text_shaper.hpp"
#include "stb/util.hpp"

#include "font/truetype/truetype.hpp"

namespace stb {

// ------------------------------------------------------------------ pimpl

// The font file's bytes: either owned memory or a read-only mapping of the file.
// The face points into this, so it is never moved after init.
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
    font_bytes(font_bytes&&) = delete;  // `ptr` points into `owned` or the mapping
    font_bytes& operator=(font_bytes&&) = delete;
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
    truetype::face face;
    int index = 0;

    float scale(float pixel_height) const { return face.scale_for_pixel_height(pixel_height); }

    // The font's GSUB data for Arabic script, parsed the first time right-to-left text is shaped.
    const arabic_layout& arabic() const {
        std::call_once(arabic_once_, [this] {
            arabic_ = arabic_layout::build(otl::byte_view(data.ptr, data.len), std::uint32_t(face.font_start()));
        });
        return arabic_;
    }

private:
    mutable std::once_flag arabic_once_;
    mutable arabic_layout arabic_;
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

// One pass over the text: where every glyph's pen and baseline are.
struct layout {
    struct item {
        int gid;         // glyph index in the font
        float x;         // pen position
        float baseline;
    };
    std::vector<item> items;
    float width = 0;
    float height = 0;
    int lines = 1;
};

// The shaper's view of a font.
class font_glyphs final : public glyph_source {
public:
    explicit font_glyphs(const font::impl& f) : f_(f) {}
    std::uint32_t glyph(char32_t cp) const override {
        return cp > 0x10FFFF ? 0 : std::uint32_t(f_.face.glyph_index(int(cp)));
    }
    const arabic_layout* arabic() const override { return &f_.arabic(); }

private:
    const font::impl& f_;
};

layout do_layout(const font::impl& f, const std::u32string& logical, float px) {
    // Glyphs in visual order: right-to-left lines (Persian, Arabic, ...) are joined, substituted
    // with the font's GSUB features and reordered; other text is one glyph per character.
    const std::vector<shaped_glyph> glyphs = text_shaper(font_glyphs(f)).shape(logical);
    const float s = f.scale(px);
    int asc = 0, desc = 0, gap = 0;
    f.face.v_metrics(asc, desc, gap);
    const float ascent = asc * s;
    const float line_h = (asc - desc + gap) * s;

    // Marks (harakat) are placed without a GPOS table: centred on the ink of the letter before them
    // and stacked above or below its ink box. `base` is that letter.
    struct ink_box { bool valid = false; float cx = 0, top = 0, bottom = 0; } base;
    const float mark_gap = 0.05f * px;

    layout lay;
    float x = 0;
    int line = 0;
    int prev = 0;
    bool has_prev = false;
    for (const shaped_glyph& sg : glyphs) {
        if (sg.cp == U'\n' && sg.gid == 0) {
            lay.width = std::max(lay.width, x);
            x = 0;
            ++line;
            has_prev = false;
            base.valid = false;
            continue;
        }
        if (sg.cp == U'\r') continue;
        const int gid = int(sg.gid);
        const float baseline = ascent + line * line_h;
        const truetype::box ink = f.face.bitmap_box(gid, s, s);
        const int bx0 = ink.x0, by0 = ink.y0, bx1 = ink.x1, by1 = ink.y1;
        const bool has_ink = bx1 > bx0 && by1 > by0;
        if (base.valid && has_ink && is_arabic_mark(sg.cp)) {
            float dy;
            if (is_arabic_mark_below(sg.cp)) {
                dy = (base.bottom + mark_gap) - float(by0);
                base.bottom = float(by1) + dy;
            } else {
                dy = (base.top - mark_gap) - float(by1);
                base.top = float(by0) + dy;
            }
            lay.items.push_back({gid, base.cx - 0.5f * float(bx0 + bx1), baseline + dy});
            continue;  // a mark takes no room and does not break kerning
        }
        if (has_prev) x += f.face.kern_advance(prev, gid) * s;
        int adv = 0, lsb = 0;
        f.face.h_metrics(gid, adv, lsb);
        lay.items.push_back({gid, x, baseline});
        base = has_ink ? ink_box{true, x + 0.5f * float(bx0 + bx1), float(by0), float(by1)} : ink_box{};
        x += adv * s;
        prev = gid;
        has_prev = true;
    }
    lay.width = std::max(lay.width, x);
    lay.lines = line + 1;
    lay.height = lay.lines * line_h;
    return lay;
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
    const otl::byte_view file(p->data.ptr, p->data.len);
    const int offset = truetype::face::offset_for_index(file, index);
    if (offset < 0) throw decode_error("no font at this index (not a TrueType/OpenType file?)");
    if (!p->face.init(file, offset))
        throw decode_error("not a valid TrueType/OpenType font");
    int asc = 0, desc = 0, gap = 0;
    p->face.v_metrics(asc, desc, gap);
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
    impl_->face.v_metrics(asc, desc, gap);
    return {asc * s, desc * s, gap * s};
}

bool font::has_glyph(char32_t cp) const { return impl_->face.glyph_index(check_cp(cp)) != 0; }

float font::advance(char32_t cp, float px) const {
    check_px(px);
    int adv = 0, lsb = 0;
    impl_->face.h_metrics(impl_->face.glyph_index(check_cp(cp)), adv, lsb);
    return adv * impl_->scale(px);
}

float font::kerning(char32_t left, char32_t right, float px) const {
    check_px(px);
    const truetype::face& face = impl_->face;
    return face.kern_advance(face.glyph_index(check_cp(left)), face.glyph_index(check_cp(right))) * impl_->scale(px);
}

namespace {

// Rasterises one glyph of the font by glyph index.
glyph render_glyph_index(const font::impl& f, int gid, float px) {
    const float s = f.scale(px);
    const truetype::bitmap bmp = f.face.render(gid, s, s);

    glyph g;
    g.x_offset = bmp.x_offset;
    g.y_offset = bmp.y_offset;
    int adv = 0, lsb = 0;
    f.face.h_metrics(gid, adv, lsb);
    g.advance = adv * s;
    if (bmp.width > 0 && bmp.height > 0) g.bitmap = image(bmp.width, bmp.height, pixel_format::gray, bmp.pixels);
    return g;
}

}  // namespace

glyph font::render_glyph(char32_t cp, float px) const {
    check_px(px);
    return render_glyph_index(*impl_, impl_->face.glyph_index(check_cp(cp)), px);
}

std::vector<std::uint32_t> font::shape_glyphs(std::string_view utf8) const {
    std::vector<std::uint32_t> out;
    for (const shaped_glyph& g : text_shaper(font_glyphs(*impl_)).shape(utf8_decode(utf8)))
        if (!(g.cp == U'\r' && g.gid == 0)) out.push_back(g.gid);
    return out;
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
    std::unordered_map<int, glyph> cache;
    for (const auto& it : L.items)
        if (!cache.count(it.gid)) cache.emplace(it.gid, render_glyph_index(*impl_, it.gid, px));

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
        const glyph& g = cache.at(it.gid);
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

    image canvas(int(w), int(h), pixel_format::gray);
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

image font::draw_text(const image& dst, std::string_view utf8, float px, int x, int y,
                      const color& text_color, text_align align) const {
    image out = dst.copy();
    draw_text_in_place(out, utf8, px, x, y, text_color, align);
    return out;
}

void font::draw_text_in_place(image& dst, std::string_view utf8, float px, int x, int y,
                              const color& text_color, text_align align) const {
    if (align != text_align::left && utf8.find_first_not_of("\n\r") != std::string_view::npos) {
        // Each line goes where its own width puts it; lines keep the pitch the layout uses.
        const text_size all = measure(utf8, px);
        const float pitch = all.height / float(all.lines);
        const float k = align == text_align::center ? 0.5f : 1.0f;
        int line = 0;
        for (std::size_t pos = 0; pos <= utf8.size(); ++line) {
            std::size_t end = utf8.find('\n', pos);
            if (end == std::string_view::npos) end = utf8.size();
            const std::string_view text = utf8.substr(pos, end - pos);
            pos = end + 1;
            if (text.find_first_not_of('\r') == std::string_view::npos) continue;   // blank line
            const float w = measure(text, px).width;
            draw_text_in_place(dst, text, px, x - int(std::lround(w * k)), y + int(std::lround(float(line) * pitch)),
                               text_color, text_align::left);
        }
        return;
    }
    // The coverage mask becomes the alpha of a solid-colour RGBA layer that image::composite
    // blends: no second blending loop to keep in step with it (and it gets the SIMD kernels).
    const text_bitmap t = render(utf8, px);
    const image& cov = t.bitmap;
    image layer(cov.width(), cov.height(), pixel_format::rgba);
    for (int row = 0; row < cov.height(); ++row) {
        const std::uint8_t* s = cov.data() + std::size_t(row) * cov.stride();
        std::uint8_t* d = layer.data() + std::size_t(row) * layer.stride();
        for (int col = 0; col < cov.width(); ++col, d += 4) {
            d[0] = text_color.r();
            d[1] = text_color.g();
            d[2] = text_color.b();
            d[3] = static_cast<std::uint8_t>((int(s[col]) * text_color.a() + 127) / 255);
        }
    }
    dst.composite_in_place(layer, int(std::int64_t(x) - t.origin_x), int(std::int64_t(y) - t.origin_y));
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
    if (width - padding < 1 || height - padding < 1) throw limit_error("atlas too small for the requested glyphs");

    std::vector<truetype::packed_glyph> packed;
    if (!truetype::pack_glyphs(impl_->face, pixels.data(), width, height, padding, px, ids, packed))
        throw limit_error("atlas too small for the requested glyphs");

    std::vector<atlas_glyph> glyphs;
    glyphs.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const truetype::packed_glyph& c = packed[i];
        glyphs.push_back({char32_t(ids[i]), c.x0, c.y0, c.x1, c.y1, c.x_offset, c.y_offset, c.x_offset2, c.y_offset2, c.x_advance});
    }
    return atlas(image(width, height, pixel_format::gray, std::move(pixels)), std::move(glyphs));
}

// --------------------------------------------------------------- estimate
// Font-free size estimate: advance widths come from fixed tables (see
// tools/gen_font_metrics.py), so the result is the same on every machine.

namespace {

// BEGIN GENERATED (tools/gen_font_metrics.py), do not edit by hand
// All values are in 1/2048 em. Latin table: U+0020..U+00FF; extras: U+2013, U+2014, U+2018, U+2019, U+201C, U+201D, U+2022, U+2026, U+20AC, U+2122.
constexpr int estimate_low = 0x20, estimate_high = 0xFF;
constexpr char32_t estimate_extra_cps[10] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2122};
struct estimate_family {
    std::uint16_t latin[estimate_high - estimate_low + 1];
    std::uint16_t extra[10];
    std::uint16_t fallback;  // any other narrow character
    int ascent, descent, line_gap;
};
const estimate_family estimate_families[3] = {
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

constexpr float em_units = 2048.0f;

const estimate_family& family_data(font_family f) {
    switch (f) {
        case font_family::sans: return estimate_families[0];
        case font_family::serif: return estimate_families[1];
        case font_family::mono: return estimate_families[2];
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
    if (c >= char32_t(estimate_low) && c <= char32_t(estimate_high)) return fam.latin[c - estimate_low];
    for (int i = 0; i < int(sizeof estimate_extra_cps / sizeof estimate_extra_cps[0]); ++i)
        if (estimate_extra_cps[i] == c) return fam.extra[i];
    if (c < 0x20 || is_zero_width(c)) return 0.0f;
    if (is_wide(c)) return em_units;
    return fam.fallback;
}

// The same scale face::scale_for_pixel_height uses: pixel_height / (ascent - descent).
float estimate_scale(const estimate_family& f, float px) {
    check_px(px);
    return px / (f.ascent - f.descent);
}

}  // namespace

font_metrics estimate_metrics(float px, font_family family) {
    const estimate_family& f = family_data(family);
    const float s = estimate_scale(f, px);
    return {f.ascent * s, f.descent * s, f.line_gap * s};
}

text_size estimate_text(std::string_view utf8, float px, font_family family) {
    const estimate_family& f = family_data(family);
    const float s = estimate_scale(f, px);
    const float line_h = estimate_metrics(px, family).line_height();

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
