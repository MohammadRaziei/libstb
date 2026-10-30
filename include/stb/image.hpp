#pragma once

// stb headers are never included here: they are compiled into src/ only, with STB_*_STATIC, so no
// stbi_* symbol leaks out of stb_core (safe to link next to your own copy of stb).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "stb/color.hpp"
#include "stb/error.hpp"

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
pixel_format pixel_format_from_name(std::string_view name);

// How image::blend mixes an overlay pixel (s) into the pixel below it (b), per colour channel,
// before the overlay's alpha and opacity decide how much of the result shows. These are the
// separable modes of the W3C compositing spec, i.e. the ones Photoshop and CSS call by the same names.
enum class blend_mode : std::uint8_t {
    normal,      // s: the overlay replaces what is below (the usual "over" operator)
    multiply,    // b * s: darkens; white changes nothing
    screen,      // 1 - (1 - b)(1 - s): lightens; black changes nothing
    overlay,     // multiply where b is dark, screen where b is light: contrast
    darken,      // min(b, s)
    lighten,     // max(b, s)
    add,         // min(1, b + s)
    difference,  // |b - s|
};
// "normal", "multiply", "screen", "overlay", "darken", "lighten", "add" or "difference".
std::string_view blend_mode_name(blend_mode m) noexcept;
// The inverse of blend_mode_name (lower case only). Throws invalid_argument for any other text.
blend_mode blend_mode_from_name(std::string_view name);

// Where the shorter images of hstack / vstack sit across the stack: at the start (top or left),
// in the middle, or at the end (bottom or right).
enum class align : std::uint8_t { start, center, end };

// Header-only facts about an encoded image (no pixel decoding).
struct image_info {
    int width = 0;
    int height = 0;
    pixel_format format = pixel_format::gray;  // what the source file holds
    int channels() const noexcept { return channel_count(format); }

    // Throws: invalid_argument (null data), decode_error, limit_error.
    static image_info read(const void* data, std::size_t size);
    // Also throws io_error.
    static image_info read_file(const std::filesystem::path& path);
};

struct load_options {
    std::optional<pixel_format> format;  // empty = keep the source's, else convert to this
    bool flip = false;  // flip vertically while decoding (per call, thread-safe)
    // Apply the EXIF orientation stored in the file (JPEG and PNG), so a phone
    // photo comes out upright. The result may have width and height swapped
    // relative to image_info. Combined with `flip`, the orientation is applied first.
    bool orient = false;
    // Refuse to decode anything whose output would exceed this many bytes.
    // Checked from the header BEFORE any pixel memory is allocated.
    std::size_t max_bytes = std::size_t(1) << 29;  // 512 MiB
};

// The EXIF orientation (1..8, as the standard numbers them) stored in an encoded
// JPEG or PNG, or 1 ("upright") when there is none, the format has none, or the
// data is not readable. Never throws and never reads out of bounds, so it is safe
// on untrusted input. Apply it with image::orient().
int exif_orientation(const void* data, std::size_t size) noexcept;

// SIMD backends. libstb's own conversion and compositing loops (convert, composite, flatten)
// exist in up to three builds that give bit-identical results and differ only in speed:
//   "avx2"            AVX2 kernels, picked at run time when the CPU supports them (x86-64,
//                     GCC or Clang builds)
//   "sse2" / "neon"   the same loops vectorised by the compiler for the platform's baseline
//                     SIMD (x86-64 / arm64); on x86-64 the gray conversions and RGBA over RGB
//                     are hand-written SSE2 kernels (the "avx2" backend uses those too)
//   "scalar"          the same loops with auto-vectorisation switched off: the plain reference
// simd_backends() lists those this build can run on this CPU, best first (it always ends
// with "scalar"); simd_name() is the one in use, the first by default. set_simd("avx2") (or
// any listed name; "auto" restores the default) switches, and returns false, changing nothing,
// for an unknown or unavailable name. LIBSTB_SIMD=<name> in the environment chooses the
// initial backend ("off" means "scalar"); it is meant for diagnosis (a name that is unknown or
// unavailable falls back to auto and prints one line on stderr), code should use set_simd or
// simd_scope. Flips, turns, crop and pad only move bytes and have no SIMD variants. Thread-safe.
//
// set_simd changes the backend for the whole process. simd_scope changes it for the calling
// thread only, for as long as the object lives (scopes nest, and take precedence over
// set_simd): other threads are not affected, so it is the one to use in a library or a
// thread pool. "auto" means the best backend. The constructor throws std::invalid_argument
// for an unknown or unavailable name. Create and destroy a scope on the same thread.
// simd_name() reports what the calling thread uses.
std::vector<std::string> simd_backends();
const char* simd_name() noexcept;
bool set_simd(std::string_view name) noexcept;

class simd_scope {
public:
    explicit simd_scope(std::string_view name);
    ~simd_scope();
    simd_scope(const simd_scope&) = delete;
    simd_scope& operator=(const simd_scope&) = delete;
    simd_scope(simd_scope&&) = delete;  // a scope belongs to the thread and block that made it
    simd_scope& operator=(simd_scope&&) = delete;

private:
    std::size_t previous_;
};

// ---- resizing options (used by image::resize and the resizer class below)

enum class resize_filter {
    automatic,      // Catmull-Rom when enlarging, Mitchell when shrinking
    box,
    triangle,       // == bilinear when enlarging
    cubic_bspline,  // smooth, slightly blurry
    catmull_rom,    // sharp, interpolating
    mitchell,       // good all-round compromise
    point           // nearest neighbour
};

enum class resize_edge { clamp, reflect, wrap, zero };

// Maps a scipy/PIL-style name to a filter. Case-insensitive; '-' and ' ' are
// treated like '_'. Accepted names:
//   "auto" "automatic" "default"        -> automatic
//   "nearest" "point"                   -> point
//   "linear" "bilinear" "triangle"      -> triangle
//   "cubic" "bicubic" "catmull_rom"     -> catmull_rom   (PIL/OpenCV meaning of "cubic")
//   "bspline" "cubic_bspline"           -> cubic_bspline (scipy's meaning of "cubic")
//   "mitchell"                          -> mitchell
//   "box" "area"                        -> box
// Throws invalid_argument (listing the valid names) for anything else.
resize_filter resize_filter_from_name(std::string_view name);

struct resize_options {
    resize_filter filter = resize_filter::automatic;
    resize_edge edge = resize_edge::clamp;
    // Treat 8-bit colour channels as sRGB and blend in linear light (correct
    // for photos/UI; alpha is always linear). Turn off for data such as
    // normal maps or masks, which are already linear.
    bool srgb = true;
    // Refuse to produce an output larger than this many bytes.
    std::size_t max_bytes = std::size_t(1) << 29;  // 512 MiB
};

class resizer;  // defined after image, which only takes a pointer to one

// Defaults of to_png / write_png and to_jpg / write_jpg.
inline constexpr int default_png_compression = 8;  // 1..9, higher = smaller and slower
inline constexpr int default_jpg_quality = 90;     // 1..100

// An 8-bit image: row-major, interleaved channels, no padding, so
// size_bytes() == width * height * channels. A default-constructed image is
// empty.
//
// Move-only: an image never copies its pixels behind your back. Say so with
// copy(). The pixels may be memory the image owns (constructors, decode) or
// memory someone else owns (wrap), which is how a numpy array becomes an
// image without a copy.
//
// Thread-safety: like std::vector - const members are safe to call
// concurrently, mutation needs external synchronisation. The static
// factories are safe to call from any number of threads.
class image {
public:
    image() = default;

    // Zero-filled. Throws invalid_argument unless width, height >= 1.
    image(int width, int height, pixel_format format);
    // Takes ownership of `pixels`; throws invalid_argument on a size mismatch.
    image(int width, int height, pixel_format format, std::vector<std::uint8_t> pixels);

    // Views memory that someone else owns, without copying it. `data` must
    // hold width * height * channel_count(format) bytes and stay valid while the image
    // (or anything moved from it) lives; `keep_alive` is released when that
    // image dies, so put whatever owns or frees the memory in it (a deleter,
    // a Python reference, ...). Throws invalid_argument on a null `data` or
    // bad dimensions.
    static image wrap(int width, int height, pixel_format format, std::uint8_t* data,
                      std::shared_ptr<void> keep_alive = {});

    image(image&& other) noexcept;
    image& operator=(image&& other) noexcept;
    image(const image&) = delete;             // copies are explicit:
    image& operator=(const image&) = delete;  // see copy()
    ~image() = default;

    // A deep copy that owns its own pixels (also when this image is a wrap).
    image copy() const;

    // The deleter of type D that was given to the shared_ptr passed as `keep_alive` to wrap(), or
    // nullptr if the pixels are held by something else. It lets a binding reach what the image keeps
    // alive (for a garbage collector to see it) without the image knowing about the host language.
    template <class D>
    D* owner_deleter() const noexcept {
        return std::get_deleter<D>(data_);
    }

    // --- decoding (throws invalid_argument, decode_error, limit_error) ---
    static image decode(const void* data, std::size_t size, const load_options& opt = {});
    // Also throws io_error.
    static image open(const std::filesystem::path& path, const load_options& opt = {});

    // --- encoding ---
    // to_*: the bytes of a whole file in that format. Throw invalid_argument
    // (empty image, or an argument out of range), encode_error, limit_error.
    // Each format takes its own settings, all defaulted.
    std::vector<std::uint8_t> to_png(int compression = default_png_compression) const;  // 1..9
    std::vector<std::uint8_t> to_jpg(int quality = default_jpg_quality) const;          // 1..100; alpha is dropped
    std::vector<std::uint8_t> to_bmp() const;
    std::vector<std::uint8_t> to_tga(bool rle = true) const;

    // write_*: the matching to_* plus writing the file (also throws io_error).
    // The file is written only after encoding succeeded.
    void write_png(const std::filesystem::path& path, int compression = default_png_compression) const;
    void write_jpg(const std::filesystem::path& path, int quality = default_jpg_quality) const;
    void write_bmp(const std::filesystem::path& path) const;
    void write_tga(const std::filesystem::path& path, bool rle = true) const;

    // The format comes from the extension (.png .jpg .jpeg .bmp .tga,
    // case-insensitive; invalid_argument for anything else), with default
    // settings. For other settings call write_* (or to_*) directly.
    void write(const std::filesystem::path& path) const;

    // --- resizing (src/image/resize.cpp) ---
    // A new image of the given size; this one is not modified. `r` == nullptr
    // means a default-constructed resizer. Throws invalid_argument (empty
    // image, width/height < 1), limit_error, error - see resizer::resize.
    image resize(int width, int height, const resizer* r = nullptr) const;
    // Default options with just this filter, by enum or by name
    // ("cubic", "linear", "nearest", ... see resize_filter_from_name).
    image resize(int width, int height, resize_filter filter) const;
    image resize(int width, int height, std::string_view filter_name) const;

    // --- geometry: each returns a new image, this one is not modified ---
    // All throw invalid_argument on an empty image or a bad argument, and
    // limit_error if the result would exceed 2 GiB.

    // The rectangle with top-left corner (x, y); it must lie inside the image.
    image crop(int x, int y, int width, int height) const;
    image flip_horizontal() const;  // mirror left <-> right
    image flip_vertical() const;    // mirror top <-> bottom
    // `turns` quarter turns counter-clockwise, like numpy.rot90 and Pillow (negative: clockwise; any integer).
    image rotate90(int turns = 1) const&;
    image rotate90(int turns = 1) &&;  // a whole number of full turns hands the pixels back, no copy
    // Swap rows and columns (mirror across the main diagonal); width and height swap.
    image transpose() const;
    // The upright version of an image stored with this EXIF orientation (1..8,
    // see exif_orientation): the transform a viewer applies. 1 is a copy (a move on a temporary).
    image orient(int orientation) const&;
    image orient(int orientation) &&;
    // A larger image with this one in the middle of a border of the given widths
    // (all >= 0). fill[c] is the border value of channel c (so for RGBA {r, g, b, a};
    // for gray + alpha {gray, alpha}); the default is all zero.
    image pad(int left, int top, int right, int bottom, std::array<std::uint8_t, 4> fill = {}) const&;
    image pad(int left, int top, int right, int bottom, std::array<std::uint8_t, 4> fill = {}) &&;  // no border: a move
    // Shrink to fit inside max_width x max_height, keeping the aspect ratio. An image
    // that already fits is copied, never enlarged. The filter arguments are those of
    // resize().
    image thumbnail(int max_width, int max_height, const resizer* r = nullptr) const;
    image thumbnail(int max_width, int max_height, resize_filter filter) const;
    image thumbnail(int max_width, int max_height, std::string_view filter_name) const;

    // --- channels and alpha ---
    // The same pixels in another format.
    // Gray comes from RGB as 0.299 R + 0.587 G + 0.114 B (on the stored values, no
    // gamma, as Pillow does), a missing alpha is 255, and a dropped alpha is simply
    // discarded: use flatten() to blend it onto a background instead. Same format:
    // a copy (a move on a temporary: `open(p).convert(pixel_format::rgb)` costs nothing when
    // the file is already RGB).
    image convert(pixel_format format) const&;
    image convert(pixel_format format) &&;
    // One single-channel image per channel; merge() puts them back together.
    std::vector<image> split() const;
    // 1..4 single-channel images of the same size -> one image with that many
    // channels (in that order). Throws invalid_argument otherwise.
    static image merge(const std::vector<const image*>& channels);
    static image merge(const std::vector<image>& channels);
    // The alpha channel as a gray image (mask). Throws invalid_argument unless the format has alpha.
    image alpha() const;
    // This image with `mask` (gray, the same size) as its alpha channel: gray -> gray_alpha, rgb ->
    // rgba, and an existing alpha channel is replaced. 0 hides a pixel, 255 shows it.
    image with_alpha(const image& mask) const;

    // --- drawing into this image (these change THIS image: use copy() to keep the original) ---
    // `overlay` blended on top at (x, y). The overlay may hang over the edges (it is clipped) and
    // may have any channel count: without alpha it is opaque, gray is replicated into colour. The
    // image keeps its own channel count; if that has no alpha, the overlay's alpha just weights
    // the blend. `mode` says how the colours mix (blend_mode::normal: the "over" operator with
    // straight alpha), `opacity` (0..1) scales the overlay's alpha. If this image wraps memory it
    // does not own (see wrap()), that memory changes. Returns *this, so calls chain, and on a
    // temporary the temporary itself: `stb::image b = a.copy().blend(logo, 16, 16);`.
    // Throws invalid_argument on an empty image or an opacity outside 0..1.
    image& blend(const image& overlay, int x = 0, int y = 0, blend_mode mode = blend_mode::normal,
                 float opacity = 1.0f) &;
    image&& blend(const image& overlay, int x = 0, int y = 0, blend_mode mode = blend_mode::normal,
                  float opacity = 1.0f) &&;
    // Overwrite every pixel, or the rectangle (x, y, width, height) clipped to the image, with
    // `color` (no blending: its alpha is stored as is, and dropped on a format without alpha;
    // gray formats take its luma). A rectangle that is entirely outside changes nothing. Throws
    // invalid_argument on an empty image or a negative width or height. Returns *this like blend().
    image& fill(const color& c) &;
    image&& fill(const color& c) &&;
    image& fill(const color& c, int x, int y, int width, int height) &;
    image&& fill(const color& c, int x, int y, int width, int height) &&;

    // Several images side by side (hstack) or one above the other (vstack), `gap` pixels apart.
    // The others are lined up on the cross axis by `where`; the rest of the result is `background`.
    // The result has colour if any input has, and alpha if any input has: gray and rgb make rgb,
    // rgb and gray_alpha make rgba. Throws invalid_argument (nothing to stack, null or empty
    // image, negative gap), limit_error (result over 2 GiB).
    //   stb::image row = stb::image::hstack({&a, &b, &c}, 8, "white", stb::align::center);
    static image hstack(std::initializer_list<const image*> images, int gap = 0, const color& background = {},
                        align where = align::start) {
        return hstack(std::vector<const image*>(images), gap, background, where);
    }
    static image hstack(const std::vector<const image*>& images, int gap = 0, const color& background = {},
                        align where = align::start);
    static image hstack(const std::vector<image>& images, int gap = 0, const color& background = {},
                        align where = align::start);
    static image vstack(std::initializer_list<const image*> images, int gap = 0, const color& background = {},
                        align where = align::start) {
        return vstack(std::vector<const image*>(images), gap, background, where);
    }
    static image vstack(const std::vector<const image*>& images, int gap = 0, const color& background = {},
                        align where = align::start);
    static image vstack(const std::vector<image>& images, int gap = 0, const color& background = {},
                        align where = align::start);
    // Blend an alpha channel onto a solid background colour (r, g, b) and drop it:
    // 2 -> 1 channel, 4 -> 3. Images without alpha are copied (moved, on a temporary).
    image flatten(std::array<std::uint8_t, 3> background = {255, 255, 255}) const&;
    image flatten(std::array<std::uint8_t, 3> background = {255, 255, 255}) &&;

    // --- accessors ---
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    // An empty image has no format: it reports gray and 0 channels.
    pixel_format format() const noexcept { return channels_ ? static_cast<pixel_format>(channels_) : pixel_format::gray; }
    int channels() const noexcept { return channels_; }  // channel_count(format()), 0 when empty
    bool empty() const noexcept { return !data_; }
    std::size_t stride() const noexcept { return std::size_t(width_) * std::size_t(channels_); }
    std::size_t size_bytes() const noexcept { return stride() * std::size_t(height_); }

    std::uint8_t* data() noexcept { return data_.get(); }
    const std::uint8_t* data() const noexcept { return data_.get(); }
    // All bytes, for range-for and <algorithm> (empty range for an empty image).
    std::uint8_t* begin() noexcept { return data(); }
    std::uint8_t* end() noexcept { return data() + size_bytes(); }
    const std::uint8_t* begin() const noexcept { return data(); }
    const std::uint8_t* end() const noexcept { return data() + size_bytes(); }

    // Unchecked element access (x < width, y < height, c < channels).
    std::uint8_t& operator()(int x, int y, int c = 0) noexcept { return data_.get()[index(x, y, c)]; }
    const std::uint8_t& operator()(int x, int y, int c = 0) const noexcept {
        return data_.get()[index(x, y, c)];
    }

private:
    std::size_t index(int x, int y, int c) const noexcept {
        return std::size_t(y) * stride() + std::size_t(x) * std::size_t(channels_) + std::size_t(c);
    }

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    // Owner + pointer in one: the deleter (or aliased owner) is what frees, or
    // keeps alive, the memory. Null for an empty image.
    std::shared_ptr<std::uint8_t> data_;
};

// Resizes 8-bit images of 1..4 channels (2 = gray+alpha, 4 = RGBA; alpha is
// weighted correctly, so transparent pixels do not bleed colour).
// Immutable after construction, hence shareable between threads.
class resizer {
public:
    resizer() = default;
    explicit resizer(const resize_options& options);  // throws invalid_argument on a bad enum value
    // Shortcuts for "default options, just this filter":
    //   resizer(resize_filter::mitchell)   resizer("cubic")
    // The name is parsed by resize_filter_from_name (invalid_argument if unknown).
    explicit resizer(resize_filter filter);
    explicit resizer(std::string_view filter_name);

    const resize_options& options() const noexcept { return options_; }

    // Throws invalid_argument (empty image, width/height < 1),
    // limit_error (output larger than options().max_bytes or 2 GiB),
    // error (allocation failure inside stb).
    image resize(const image& src, int width, int height) const;

private:
    resize_options options_;
};

}  // namespace stb
