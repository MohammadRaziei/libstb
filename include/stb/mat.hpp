#pragma once

// mat<T>: a dense width x height array of T with a fixed number of channels per cell, and nothing
// else. It is the one array type of libstb. A filter kernel is a mat32f, a structuring element a
// mat8u, a label map a mat32i, and an image is a mat with pixel meaning on top: image8u (see
// image.hpp) is a mat8u that can also decode, encode, blend and so on, and image32f is another name
// for mat32f. So every function that takes a mat takes an image too.
//
// Layout: row-major, interleaved channels, no padding, so size() == width * height * channels.
// A default-constructed mat is empty.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <algorithm>

#include "stb/export.hpp"

namespace stb {

// What one pixel holds. The value of each enumerator is its number of 8-bit channels, so
// channel_count() is a cast, but code never needs to spell that number: say pixel_format::rgb.
enum class pixel_format : std::uint8_t {
    gray = 1,        // one channel: luminance
    gray_alpha = 2,  // luminance, alpha
    rgb = 3,         // red, green, blue
    rgba = 4,        // red, green, blue, alpha (straight, not premultiplied)
};

constexpr int channel_count(pixel_format f) noexcept { return static_cast<int>(f); }
constexpr bool has_alpha(pixel_format f) noexcept { return f == pixel_format::gray_alpha || f == pixel_format::rgba; }
// The format with this many channels. Throws invalid_argument unless channels is in 1..4.
constexpr pixel_format pixel_format_from_channels(int channels) {
    if (channels < 1 || channels > 4) throw std::invalid_argument("channels must be in 1..4");
    return static_cast<pixel_format>(channels);
}
// "gray", "gray_alpha", "rgb" or "rgba".
constexpr std::string_view pixel_format_name(pixel_format f) noexcept {
    switch (f) {
        case pixel_format::gray: return "gray";
        case pixel_format::gray_alpha: return "gray_alpha";
        case pixel_format::rgb: return "rgb";
        case pixel_format::rgba: return "rgba";
    }
    return {};
}
// The inverse of pixel_format_name (lower case only). Throws invalid_argument for any other text.
STB_API pixel_format pixel_format_from_name(std::string_view name);

// What a filter reads where its window leaves the array (row 0 and width - 1 are the edges; the
// samples around "abcd" are shown for each mode).
enum class edge_mode : std::uint8_t {
    clamp,    // aaa|abcd|ddd  the nearest edge value repeats (the default everywhere)
    reflect,  // cba|abcd|dcb  mirrored, the edge value is repeated once (numpy "symmetric")
    mirror,   // dcb|abcd|cba  mirrored around the edge value, which is not repeated (OpenCV's default)
    wrap,     // bcd|abcd|abc  the array repeats
    zero,     // 000|abcd|000  everything outside is 0
};

template <class T>
class mat;
using mat8u = mat<std::uint8_t>;
using mat32f = mat<float>;
using mat32i = mat<std::int32_t>;

namespace detail {

// What every array type has in common: the memory (owned, or viewed with a keep-alive, see mat::wrap)
// and the size, and nothing about what a cell means. It is the base of mat and of premultiplied_image,
// which are not related to each other: a premultiplied_image has no blend(), no encoder and no resize,
// so it cannot be mistaken for straight-alpha pixels. Not for direct use: the constructors and the
// destructor are protected, and it is move-only like all its children (a moved-from object is empty).
template <class T>
class image_base_t {
public:
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return !data_; }

    T* data() noexcept { return data_.get(); }
    const T* data() const noexcept { return data_.get(); }

    // The deleter of type D that was given to the shared_ptr passed as `keep_alive` to wrap(), or
    // nullptr if the memory is held by something else. It lets a binding reach what the object keeps
    // alive (for a garbage collector to see it) without knowing about the host language.
    template <class D>
    D* owner_deleter() const noexcept {
        return std::get_deleter<D>(data_);
    }

protected:
    image_base_t() = default;
    image_base_t(int width, int height) noexcept : width_(width), height_(height) {}
    image_base_t(image_base_t&& o) noexcept
        : width_(std::exchange(o.width_, 0)), height_(std::exchange(o.height_, 0)), data_(std::move(o.data_)) {}
    image_base_t& operator=(image_base_t&& o) noexcept {
        if (this != &o) {
            width_ = std::exchange(o.width_, 0);
            height_ = std::exchange(o.height_, 0);
            data_ = std::move(o.data_);
        }
        return *this;
    }
    image_base_t(const image_base_t&) = delete;
    image_base_t& operator=(const image_base_t&) = delete;
    ~image_base_t() = default;

    int width_ = 0;
    int height_ = 0;
    // Owner + pointer in one: the deleter (or aliased owner) is what frees, or keeps alive, the
    // memory. Null when empty.
    std::shared_ptr<T> data_;
};

// The 8-bit instance: the base of premultiplied_image.
using image_base = image_base_t<std::uint8_t>;

// How many values of type T a width x height array with this many channels holds. Throws
// invalid_argument for a size or channel count below 1 or one that does not fit in memory addressing.
template <class T>
inline std::size_t checked_value_count(int width, int height, int channels) {
    if (width < 1 || height < 1) throw std::invalid_argument("width and height must be >= 1");
    if (channels < 1) throw std::invalid_argument("channels must be >= 1");
    const std::uint64_t pixels = std::uint64_t(width) * std::uint64_t(height);  // <= 2^62
    if (pixels > std::uint64_t(std::numeric_limits<std::size_t>::max()) / (std::uint64_t(channels) * sizeof(T)))
        throw std::invalid_argument("image too large");
    return static_cast<std::size_t>(pixels * std::uint64_t(channels));
}

}  // namespace detail

// A width x height array of T (std::uint8_t, float, std::int32_t, ...) with `channels` values per cell.
//
// Move-only: a mat never copies its values behind your back. Say so with copy(). The values may be
// memory the mat owns (constructors) or memory someone else owns (wrap), which is how a numpy array
// becomes a mat without a copy.
//
// Indexing is (x, y, c): the column first, like pixel coordinates. m(2, 0) is column 2 of row 0.
// rows() and cols() are height() and width() under the names a matrix has.
template <class T>
class mat : public detail::image_base_t<T> {
    using base = detail::image_base_t<T>;

public:
    using value_type = T;

    mat() = default;

    // Zero-filled; one channel unless said otherwise. Throws invalid_argument unless width, height
    // and channels are >= 1.
    mat(int width, int height, int channels = 1) : base(width, height), channels_(channels) {
        const std::size_t n = detail::checked_value_count<T>(width, height, channels);
        this->data_ = std::shared_ptr<T>(new T[n](), std::default_delete<T[]>());
    }
    mat(int width, int height, pixel_format format) : mat(width, height, channel_count(format)) {}

    // Takes ownership of `values`; throws invalid_argument on a size mismatch.
    mat(int width, int height, int channels, std::vector<T> values) : base(width, height), channels_(channels) {
        if (values.size() != detail::checked_value_count<T>(width, height, channels))
            throw std::invalid_argument("buffer size does not match width * height * channels");
        auto holder = std::make_shared<std::vector<T>>(std::move(values));
        this->data_ = std::shared_ptr<T>(holder, holder->data());  // one block: vector + pointer
    }
    mat(int width, int height, pixel_format format, std::vector<T> values)
        : mat(width, height, channel_count(format), std::move(values)) {}

    // One channel, from rows: mat32f k = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    // Throws invalid_argument for no rows, no columns, or rows of different lengths.
    mat(std::initializer_list<std::initializer_list<T>> rows) {
        if (rows.size() == 0 || rows.begin()->size() == 0)
            throw std::invalid_argument("a mat needs at least one row and one column");
        const std::size_t w = rows.begin()->size();
        std::vector<T> values;
        values.reserve(w * rows.size());
        for (const auto& r : rows) {
            if (r.size() != w) throw std::invalid_argument("all rows must have the same length");
            values.insert(values.end(), r.begin(), r.end());
        }
        *this = mat(static_cast<int>(w), static_cast<int>(rows.size()), 1, std::move(values));
    }

    // Views memory that someone else owns, without copying it. `data` must hold
    // width * height * channels values and stay valid while the mat (or anything moved from it) lives;
    // `keep_alive` is released when that mat dies, so put whatever owns or frees the memory in it (a
    // deleter, a Python reference, ...). Throws invalid_argument on a null `data` or bad dimensions.
    static mat wrap(int width, int height, int channels, T* data, std::shared_ptr<void> keep_alive = {}) {
        detail::checked_value_count<T>(width, height, channels);  // validates the dimensions
        if (!data) throw std::invalid_argument("wrap needs a non-null pointer");
        mat m;
        m.width_ = width;
        m.height_ = height;
        m.channels_ = channels;
        m.data_ = std::shared_ptr<T>(keep_alive, data);  // aliasing: keeps `keep_alive` (C++17 has no moving form)
        return m;
    }
    static mat wrap(int width, int height, pixel_format format, T* data, std::shared_ptr<void> keep_alive = {}) {
        return wrap(width, height, channel_count(format), data, std::move(keep_alive));
    }

    // A moved-from mat is empty (not just "valid but unspecified").
    mat(mat&& o) noexcept : base(std::move(o)), channels_(std::exchange(o.channels_, 0)) {}
    mat& operator=(mat&& o) noexcept {
        if (this != &o) {
            base::operator=(std::move(o));
            channels_ = std::exchange(o.channels_, 0);
        }
        return *this;
    }
    mat(const mat&) = delete;             // copies are explicit:
    mat& operator=(const mat&) = delete;  // see copy()
    ~mat() = default;

    // A deep copy that owns its own values (also when this mat is a wrap).
    mat copy() const {
        if (this->empty()) return mat();
        return mat(this->width_, this->height_, channels_, std::vector<T>(begin(), end()));
    }

    // --- accessors (width(), height(), empty(), data() and owner_deleter() come from the base) ---
    int cols() const noexcept { return this->width_; }
    int rows() const noexcept { return this->height_; }
    int channels() const noexcept { return channels_; }  // 0 when empty
    // The pixel format of this many channels (gray for an empty mat). Throws invalid_argument when
    // the channel count is above 4: such a mat is an array of numbers, not an image.
    pixel_format format() const { return pixel_format_from_channels(channels_ ? channels_ : 1); }
    // Values per row and in all, and the same in bytes.
    std::size_t stride() const noexcept { return std::size_t(this->width_) * std::size_t(channels_); }
    std::size_t size() const noexcept { return stride() * std::size_t(this->height_); }
    std::size_t size_bytes() const noexcept { return size() * sizeof(T); }

    // All values, for range-for and <algorithm> (empty range for an empty mat).
    T* begin() noexcept { return this->data(); }
    T* end() noexcept { return this->data() + size(); }
    const T* begin() const noexcept { return this->data(); }
    const T* end() const noexcept { return this->data() + size(); }

    // Unchecked element access (x < width, y < height, c < channels).
    T& operator()(int x, int y, int c = 0) noexcept { return this->data_.get()[index(x, y, c)]; }
    const T& operator()(int x, int y, int c = 0) const noexcept { return this->data_.get()[index(x, y, c)]; }
    // The first value of row y (y < height): stride() values follow it.
    T* row(int y) noexcept { return this->data() + std::size_t(y) * stride(); }
    const T* row(int y) const noexcept { return this->data() + std::size_t(y) * stride(); }

    // Sets every value.
    void fill(T value) { std::fill(begin(), end(), value); }

protected:
    int channels_ = 0;

private:
    std::size_t index(int x, int y, int c) const noexcept {
        return std::size_t(y) * stride() + std::size_t(x) * std::size_t(channels_) + std::size_t(c);
    }
};

}  // namespace stb
