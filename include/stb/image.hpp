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

#include "stb/export.hpp"
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
STB_API pixel_format pixel_format_from_name(std::string_view name);

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
    exclusion,   // b + s - 2 b s: like difference with less contrast; mid gray stays mid gray
};
// "normal", "multiply", "screen", "overlay", "darken", "lighten", "add", "difference" or "exclusion".
STB_API std::string_view blend_mode_name(blend_mode m) noexcept;
// The inverse of blend_mode_name (lower case only). Throws invalid_argument for any other text.
STB_API blend_mode blend_mode_from_name(std::string_view name);

// How the overlay and the pixels below it share the alpha (the Porter-Duff operators of the W3C
// "Compositing and Blending" specification), on top of how the colours mix (blend_mode). With S the
// overlay (its alpha times the opacity) and D what is below:
//   clear             nothing (transparent)          source_over       S over D (the usual one, the default)
//   source            S only                         destination_over  D over S
//   destination       D only (nothing changes)       source_in         S where D is, nothing else
//   destination_in    D where S is                   source_out        S where D is not
//   destination_out   D where S is not               source_atop       S over D, only where D is
//   destination_atop  D over S, only where S is      exclusive_or      S and D where the other is not
//   plus_lighter      S + D, at most 1 (the CSS "plus-lighter")
// The operator works inside the part of the image the overlay lands on (clipped to the image); pixels
// outside it are not touched. On an image without alpha the result is taken over black, so
// source_over looks as it always did and clear gives black.
enum class composite_op : std::uint8_t {
    clear,
    source,
    destination,
    source_over,
    destination_over,
    source_in,
    destination_in,
    source_out,
    destination_out,
    source_atop,
    destination_atop,
    exclusive_or,
    plus_lighter,
};
// The lower-case name of the enumerator ("source_over", "exclusive_or", ...).
STB_API std::string_view composite_op_name(composite_op op) noexcept;
// The inverse of composite_op_name (lower case only). Throws invalid_argument for any other text.
STB_API composite_op composite_op_from_name(std::string_view name);

// Where the shorter images of hstack / vstack sit across the stack: at the start (top or left),
// in the middle, or at the end (bottom or right).
enum class align : std::uint8_t { start, center, end };

// Header-only facts about an encoded image (no pixel decoding).
struct STB_API image_info {
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
STB_API int exif_orientation(const void* data, std::size_t size) noexcept;

// SIMD backends. libstb's own conversion and compositing loops (convert, composite, flatten, and
// the premultiply conversions further down)
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
STB_API std::vector<std::string> simd_backends();
STB_API const char* simd_name() noexcept;
STB_API bool set_simd(std::string_view name) noexcept;

class STB_API simd_scope {
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

// ---- straight RGBA <-> premultiplied ARGB32
// Rasterizers (novasvg, plutovg, Cairo, Skia, Qt) draw into premultiplied ARGB32, while
// stb::image holds straight RGBA. These two functions are the conversion at that boundary, on
// raw buffers (an image's data() / stride(), or any memory), 8-bit, 4 channels, run by the
// SIMD backend in use (see simd_backends()). Strides are in bytes. Both are bit-exact with
//   straight -> premultiplied:  c' = c * a / 255, packed as the 32-bit word 0xAARRGGBB
//   premultiplied -> straight:  c' = c * 255 / a, stored as bytes R, G, B, A
// (integer divisions truncate; a == 0 gives all zeros). The word is native-endian: on a
// little-endian machine its bytes are B, G, R, A. A premultiplied colour is never larger than
// its alpha; for input where it is (c > a) the quotient can exceed 255 and wraps modulo 256, like
// a plain C store into a byte.
//
// dst and src are the same buffer with the same stride (in place), or do not overlap at all;
// other overlaps are not supported. A width or height of 0 or less does nothing. Neither
// function allocates or throws.
STB_API void rgba_to_premultiplied_argb(std::uint8_t* dst, std::size_t dst_stride, const std::uint8_t* src,
                                        std::size_t src_stride, int width, int height) noexcept;
STB_API void premultiplied_argb_to_rgba(std::uint8_t* dst, std::size_t dst_stride, const std::uint8_t* src,
                                        std::size_t src_stride, int width, int height) noexcept;

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
STB_API resize_filter resize_filter_from_name(std::string_view name);

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

namespace detail {

// What an image and a premultiplied_image have in common: the pixel memory (owned, or viewed with a
// keep-alive, see image::wrap) and the size, and nothing about what a pixel means. That is why it is a
// base class of both and the two are not related to each other: a premultiplied_image has no blend(), no
// encoder and no resize, so it cannot be mistaken for straight-alpha pixels. Not for direct use: the
// constructors and the destructor are protected, and it is move-only like both of them (a moved-from
// object is empty).
class STB_API image_base {
public:
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return !data_; }

    std::uint8_t* data() noexcept { return data_.get(); }
    const std::uint8_t* data() const noexcept { return data_.get(); }

    // The deleter of type D that was given to the shared_ptr passed as `keep_alive` to wrap(), or
    // nullptr if the pixels are held by something else. It lets a binding reach what the object keeps
    // alive (for a garbage collector to see it) without knowing about the host language.
    template <class D>
    D* owner_deleter() const noexcept {
        return std::get_deleter<D>(data_);
    }

protected:
    image_base() = default;
    image_base(int width, int height) noexcept : width_(width), height_(height) {}
    image_base(image_base&& o) noexcept
        : width_(std::exchange(o.width_, 0)), height_(std::exchange(o.height_, 0)), data_(std::move(o.data_)) {}
    image_base& operator=(image_base&& o) noexcept {
        if (this != &o) {
            width_ = std::exchange(o.width_, 0);
            height_ = std::exchange(o.height_, 0);
            data_ = std::move(o.data_);
        }
        return *this;
    }
    image_base(const image_base&) = delete;
    image_base& operator=(const image_base&) = delete;
    ~image_base() = default;

    int width_ = 0;
    int height_ = 0;
    // Owner + pointer in one: the deleter (or aliased owner) is what frees, or keeps alive, the
    // memory. Null when empty.
    std::shared_ptr<std::uint8_t> data_;
};

}  // namespace detail

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
class STB_API image : public detail::image_base {
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
    // A copy whose colour channels are multiplied by alpha (c' = c * a / 255, truncated), for the formats with
    // alpha (rgba, gray_alpha); the bytes stay in the same order (R, G, B, A), only their meaning changes. The
    // other formats have nothing to multiply, so they come back as a plain copy. The result is NOT tracked: the
    // image does not remember that it is premultiplied, and blend(), the encoders and resize all expect straight
    // alpha, so convert it back with unpremultiplied() (or hand the bytes to something that wants premultiplied
    // data) before using it as an ordinary image. It is the same arithmetic as rgba_to_premultiplied_argb, which
    // also moves the bytes into the packed ARGB32 word that rasterizers draw into. The result owns its pixels.
    image premultiplied() const;
    // The inverse: c' = c * 255 / a (truncated), and a pixel with alpha 0 becomes all zeros. For input that is
    // not valid premultiplied data (c > a) the quotient wraps modulo 256, as in rgba_to_premultiplied_argb's pair.
    // premultiplied() followed by unpremultiplied() is not exact: colours lose precision where alpha is small.
    image unpremultiplied() const;

    // --- drawing into this image (these change THIS image: use copy() to keep the original) ---
    // `overlay` blended on top at (x, y). The overlay may hang over the edges (it is clipped) and
    // may have any channel count: without alpha it is opaque, gray is replicated into colour. The
    // image keeps its own channel count; if that has no alpha, the overlay's alpha just weights
    // the blend. `mode` says how the colours mix (blend_mode::normal: the "over" operator with
    // straight alpha), `opacity` (0..1) scales the overlay's alpha. If this image wraps memory it
    // does not own (see wrap()), that memory changes. Returns *this, so calls chain, and on a
    // temporary the temporary itself: `stb::image b = a.copy().blend(logo, 16, 16);`.
    // `op` is how the alpha is shared (composite_op::source_over, the usual "over", by default; the
    // others work inside the overlay's rectangle only, see composite_op).
    // Throws invalid_argument on an empty image or an opacity outside 0..1.
    image& blend(const image& overlay, int x = 0, int y = 0, blend_mode mode = blend_mode::normal,
                 float opacity = 1.0f, composite_op op = composite_op::source_over) &;
    image&& blend(const image& overlay, int x = 0, int y = 0, blend_mode mode = blend_mode::normal,
                  float opacity = 1.0f, composite_op op = composite_op::source_over) &&;
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

    // --- accessors (width(), height(), empty(), data() and owner_deleter() come from the base) ---
    // An empty image has no format: it reports gray and 0 channels.
    pixel_format format() const noexcept { return channels_ ? static_cast<pixel_format>(channels_) : pixel_format::gray; }
    int channels() const noexcept { return channels_; }  // channel_count(format()), 0 when empty
    std::size_t stride() const noexcept { return std::size_t(width_) * std::size_t(channels_); }
    std::size_t size_bytes() const noexcept { return stride() * std::size_t(height_); }

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

    int channels_ = 0;
};

// Pixels that are already multiplied by alpha, in the layout rasterizers draw into: one 32-bit word per
// pixel, 0xAARRGGBB (native-endian: on a little-endian machine the bytes are B, G, R, A), exactly what
// rgba_to_premultiplied_argb produces. It is a separate type from image on purpose: image always holds
// straight alpha, so blend(), the encoders and resize cannot be called on these pixels by mistake. Go
// between the two with from_image() and to_image() (the SIMD conversions above), or hand data() and
// stride() to a rasterizer that draws into the buffer.
//
// Rows are stride() bytes apart (at least 4 * width, a multiple of 4, so every pixel can be read as a
// std::uint32_t). Like image it is move-only (copies are explicit: copy()) and a moved-from object is
// empty. Same thread-safety as std::vector.
class STB_API premultiplied_image : public detail::image_base {
public:
    premultiplied_image() = default;

    // All pixels 0 (transparent), rows packed (stride 4 * width). Throws invalid_argument unless
    // width, height >= 1, or if the size does not fit.
    premultiplied_image(int width, int height);

    // Views memory that someone else owns, without copying it (as image::wrap does): `data` must be
    // aligned to 4 bytes and hold (height - 1) * stride + 4 * width bytes, and stay valid while the
    // object (or anything moved from it) lives; `keep_alive` is released when it dies. Throws
    // invalid_argument on a null or misaligned `data`, bad dimensions, or a stride that is smaller than
    // 4 * width or not a multiple of 4.
    static premultiplied_image wrap(int width, int height, std::uint8_t* data, std::size_t stride,
                                    std::shared_ptr<void> keep_alive = {});

    // The straight-alpha pixels of `straight`, premultiplied. Any pixel format: gray, gray_alpha and rgb
    // are taken as RGBA first (no alpha means opaque). An empty image gives an empty result.
    static premultiplied_image from_image(const image& straight);

    premultiplied_image(premultiplied_image&& other) noexcept;
    premultiplied_image& operator=(premultiplied_image&& other) noexcept;
    ~premultiplied_image() = default;

    // A deep copy that owns its own pixels, packed (also when this one is a wrap or has padding).
    premultiplied_image copy() const;

    // Back to straight RGBA, as a new image (premultiplied_argb_to_rgba, so colour precision is lost
    // where alpha is small). An empty object gives an empty image.
    image to_image() const;

    // Bytes between the starts of two rows; 0 when empty.
    std::size_t stride() const noexcept { return stride_; }
    // The bytes the pixels span, padding between rows included.
    std::size_t size_bytes() const noexcept { return stride_ * std::size_t(height_); }

private:
    std::size_t stride_ = 0;
};

// Composites one row of premultiplied pixels (0xAARRGGBB, the layout of premultiplied_image, every colour
// channel <= alpha) into another, in place: dest[i] becomes `op` of src[i] over dest[i], with `mode` mixing
// the colours as in image::blend (see blend_mode and composite_op for the exact meaning). It is the inner
// loop of a rasterizer, so it takes raw rows: the caller owns the geometry, clipping, and the source (a
// gradient or texture row it has just sampled). `const_alpha` (0..255) is the coverage: the part of the
// change the operator would make that is applied, so 255 is the operator at full strength and 0 leaves the
// destination alone (for source_over it is the same as scaling the source by it). Results are rounded to
// the nearest 8-bit value (never more than one level from the exact result of one operator; several
// operators in a row do not accumulate beyond what premultiplied 8-bit storage forces).
//
// `src` and `dest` may be the same row (pixel by pixel) but must not overlap otherwise. Throws
// invalid_argument for a null pointer when count > 0, or an `op` or `mode` that is not an enumerator.
// composite_solid_row is the same with one colour (premultiplied too) as the source of every pixel (for `source`
// with a partial coverage it rounds in two steps, so it can differ from a row of that colour by one level).
STB_API void composite_row(std::uint32_t* dest, const std::uint32_t* src, std::size_t count, composite_op op,
                           blend_mode mode, std::uint8_t const_alpha);
STB_API void composite_solid_row(std::uint32_t* dest, std::uint32_t color, std::size_t count, composite_op op,
                                 blend_mode mode, std::uint8_t const_alpha);

// Resizes 8-bit images of 1..4 channels (2 = gray+alpha, 4 = RGBA; alpha is
// weighted correctly, so transparent pixels do not bleed colour).
// Immutable after construction, hence shareable between threads.
class STB_API resizer {
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
