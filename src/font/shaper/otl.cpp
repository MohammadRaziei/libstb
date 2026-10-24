#include "otl.hpp"

namespace stb::otl {

byte_view find_table(byte_view file, std::uint32_t font_offset, std::uint32_t tag) {
    const std::size_t base = font_offset;
    const unsigned count = file.u16(base + 4);
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t rec = base + 12 + 16 * std::size_t(i);
        if (file.u32(rec) == tag) return file.sub(file.u32(rec + 8), file.u32(rec + 12));
    }
    return {};
}

// ---------------------------------------------------------------- Coverage

int coverage::index(std::uint32_t gid) const noexcept {
    if (v_.empty() || gid > 0xFFFF) return -1;
    const unsigned format = v_.u16(0), count = v_.u16(2);
    std::size_t lo = 0, hi = count;
    if (format == 1) {  // sorted glyph array
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            const unsigned g = v_.u16(4 + 2 * mid);
            if (g == gid) return static_cast<int>(mid);
            if (g < gid) lo = mid + 1;
            else hi = mid;
        }
    } else if (format == 2) {  // sorted ranges: start, end, index of the first glyph
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            const std::size_t rec = 4 + 6 * mid;
            const unsigned start = v_.u16(rec), end = v_.u16(rec + 2);
            if (gid < start) hi = mid;
            else if (gid > end) lo = mid + 1;
            else return static_cast<int>(v_.u16(rec + 4) + (gid - start));
        }
    }
    return -1;
}

bool coverage::add_to(glyph_set& out) const {
    if (v_.empty()) return true;
    const unsigned format = v_.u16(0), count = v_.u16(2);
    if (count > 4096) return false;  // a hostile or absurd table: do not filter on it
    if (format == 1) {
        for (unsigned i = 0; i < count; ++i) out.add(v_.u16(4 + 2 * std::size_t(i)));
        return true;
    }
    if (format == 2) {
        for (unsigned i = 0; i < count; ++i) out.add_range(v_.u16(4 + 6 * std::size_t(i)), v_.u16(6 + 6 * std::size_t(i)));
        return true;
    }
    return true;
}

// ---------------------------------------------------------------- ClassDef

std::uint16_t class_def::class_of(std::uint32_t gid) const noexcept {
    if (v_.empty() || gid > 0xFFFF) return 0;
    const unsigned format = v_.u16(0);
    if (format == 1) {  // start glyph + one class per glyph
        const unsigned start = v_.u16(2), count = v_.u16(4);
        return (gid >= start && gid - start < count) ? v_.u16(6 + 2 * (gid - start)) : 0;
    }
    if (format == 2) {  // sorted ranges: start, end, class
        const unsigned count = v_.u16(2);
        std::size_t lo = 0, hi = count;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            const std::size_t rec = 4 + 6 * mid;
            if (gid < v_.u16(rec)) hi = mid;
            else if (gid > v_.u16(rec + 2)) lo = mid + 1;
            else return v_.u16(rec + 4);
        }
    }
    return 0;
}

// -------------------------------------------------------------------- GDEF

gdef::gdef(byte_view t) {
    if (t.empty()) return;
    const unsigned classes = t.u16(4), attach = t.u16(10);
    if (classes) {
        classes_ = class_def(t.from(classes));
        has_classes_ = true;
    }
    if (attach) attach_ = class_def(t.from(attach));
    if (t.u32(0) >= 0x00010002) {  // version 1.2 adds mark glyph sets
        const unsigned sets = t.u16(12);
        if (sets) mark_sets_ = t.from(sets);
    }
}

std::uint8_t gdef::glyph_class(std::uint32_t gid) const noexcept {
    const std::uint16_t c = classes_.class_of(gid);
    return c <= gc_component ? static_cast<std::uint8_t>(c) : std::uint8_t(gc_unclassified);
}

std::uint16_t gdef::mark_attach_class(std::uint32_t gid) const noexcept { return attach_.class_of(gid); }

bool gdef::in_mark_set(std::uint16_t set, std::uint32_t gid) const noexcept {
    if (mark_sets_.empty() || mark_sets_.u16(0) != 1 || set >= mark_sets_.u16(2)) return false;
    const std::size_t off = mark_sets_.u32(4 + 4 * std::size_t(set));
    return coverage(mark_sets_.from(off)).index(gid) >= 0;
}

}  // namespace stb::otl
