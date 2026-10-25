// OpenType Layout building blocks: bounds-checked table views, Coverage and ClassDef tables,
// GDEF, and the sfnt table directory. Nothing here owns memory: every class is a view into the
// font file, which must outlive it.
//
// A font is untrusted input. Every read is range-checked: an offset or count that points outside
// the table reads as 0 and a lookup that cannot be parsed is dropped, so a damaged font gives
// wrong glyphs at worst, never an out-of-bounds read. Loops are bounded by the 16-bit counts.
//
// Internal header: included only from src/font/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stb::otl {

constexpr std::uint32_t make_tag(char a, char b, char c, char d) {
    return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) |
           (std::uint32_t(std::uint8_t(c)) << 8) | std::uint32_t(std::uint8_t(d));
}

// A read-only range of bytes with big-endian accessors. Out-of-range reads return 0.
class byte_view {
public:
    byte_view() = default;
    byte_view(const std::uint8_t* data, std::size_t size) : p_(data), n_(size) {}

    bool empty() const noexcept { return n_ == 0; }
    std::size_t size() const noexcept { return n_; }

    bool has(std::size_t off, std::size_t len) const noexcept { return off <= n_ && len <= n_ - off; }

    const std::uint8_t* data() const noexcept { return p_; }
    std::uint8_t u8(std::size_t off) const noexcept { return has(off, 1) ? p_[off] : std::uint8_t(0); }

    std::uint16_t u16(std::size_t off) const noexcept {
        return has(off, 2) ? static_cast<std::uint16_t>((p_[off] << 8) | p_[off + 1]) : 0;
    }
    std::int16_t i16(std::size_t off) const noexcept { return static_cast<std::int16_t>(u16(off)); }
    std::uint32_t u32(std::size_t off) const noexcept {
        return has(off, 4) ? (std::uint32_t(p_[off]) << 24) | (std::uint32_t(p_[off + 1]) << 16) |
                                 (std::uint32_t(p_[off + 2]) << 8) | std::uint32_t(p_[off + 3])
                           : 0;
    }

    // The bytes from `off` on, or an empty view if `off` is out of range.
    byte_view from(std::size_t off) const noexcept {
        return off < n_ ? byte_view(p_ + off, n_ - off) : byte_view();
    }
    // Exactly `len` bytes from `off`, or an empty view if they are not all inside.
    byte_view sub(std::size_t off, std::size_t len) const noexcept {
        return has(off, len) && len > 0 ? byte_view(p_ + off, len) : byte_view();
    }

private:
    const std::uint8_t* p_ = nullptr;
    std::size_t n_ = 0;
};

// The table `tag` of the font that starts at `font_offset` (0 for a plain .ttf/.otf, the entry
// of the .ttc header for a collection). Empty if there is none or it lies outside the file.
byte_view find_table(byte_view file, std::uint32_t font_offset, std::uint32_t tag);

// A set of glyph ids as a bitmap (glyph ids are 16-bit).
class glyph_set {
public:
    void add(std::uint32_t gid) {
        if (gid > 0xFFFF) return;
        const std::size_t w = gid >> 6;
        if (w >= bits_.size()) bits_.resize(w + 1, 0);
        bits_[w] |= std::uint64_t(1) << (gid & 63);
    }
    void add_range(std::uint32_t first, std::uint32_t last) {
        for (std::uint32_t g = first; g <= last && g <= 0xFFFF; ++g) add(g);
    }
    bool has(std::uint32_t gid) const noexcept {
        const std::size_t w = gid >> 6;
        return gid <= 0xFFFF && w < bits_.size() && (bits_[w] >> (gid & 63)) & 1;
    }

private:
    std::vector<std::uint64_t> bits_;
};

// Coverage table: the set of glyphs a subtable applies to, each with an index.
class coverage {
public:
    coverage() = default;
    explicit coverage(byte_view v) : v_(v) {}
    // Position of `gid` in the table, or -1.
    int index(std::uint32_t gid) const noexcept;
    // Adds every glyph of the table to `out`. Returns false (and adds nothing reliable) if the
    // table is too large to enumerate cheaply; the caller then must not use `out` as a filter.
    bool add_to(glyph_set& out) const;

private:
    byte_view v_;
};

// ClassDef table: assigns glyphs to classes (0 for every glyph not listed).
class class_def {
public:
    class_def() = default;
    explicit class_def(byte_view v) : v_(v) {}
    std::uint16_t class_of(std::uint32_t gid) const noexcept;

private:
    byte_view v_;
};

// Glyph classes of GDEF.GlyphClassDef.
enum glyph_class : std::uint8_t { gc_unclassified = 0, gc_base = 1, gc_ligature = 2, gc_mark = 3, gc_component = 4 };

// The parts of GDEF that lookup flags need: glyph classes, mark attachment classes and mark
// filtering sets.
class gdef {
public:
    gdef() = default;
    explicit gdef(byte_view table);

    bool has_glyph_classes() const noexcept { return has_classes_; }
    std::uint8_t glyph_class(std::uint32_t gid) const noexcept;
    std::uint16_t mark_attach_class(std::uint32_t gid) const noexcept;
    bool in_mark_set(std::uint16_t set, std::uint32_t gid) const noexcept;

private:
    class_def classes_, attach_;
    byte_view mark_sets_;
    bool has_classes_ = false;
};

}  // namespace stb::otl
