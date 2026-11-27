#pragma once

// stb headers are never included here: they are compiled into src/ only, with STB_*_STATIC, so no
// stbi_* symbol leaks out of stb_core (safe to link next to your own copy of stb).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "stb/export.hpp"
#include "stb/mat.hpp"
#include "stb/parallel.hpp"
#include "stb/pixel_range.hpp"
#include "stb/color.hpp"
#include "stb/error.hpp"

namespace stb {

// How image::blend mixes an overlay pixel (s) into the pixel below it (b), before the overlay's alpha and opacity decide
// how much of the result shows. These are the blend modes of the W3C compositing spec, i.e. the ones Photoshop and CSS
// call by the same names (CSS writes color-dodge, hard-light and so on with a hyphen; here the names use underscores).
// The first nine are separable: they mix each colour channel on its own. The last four (hue, saturation, color,
// luminosity) mix the three channels together, so for a gray image they use the gray value as all three.
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
    color_dodge, // b / (1 - s), at most 1: brightens b by s (b = 0 stays 0, s = 1 gives 1)
    color_burn,  // 1 - (1 - b) / s, at least 0: darkens b by s (b = 1 stays 1, s = 0 gives 0)
    hard_light,  // overlay with the roles swapped: multiply where s is dark, screen where s is light
    soft_light,  // a gentler hard_light (the W3C formula, with its square-root curve for the lights)
    hue,         // the hue of s with the saturation and luminosity of b
    saturation,  // the saturation of s with the hue and luminosity of b
    color,       // the hue and saturation of s with the luminosity of b
    luminosity,  // the luminosity of s with the hue and saturation of b
};
// "normal", "multiply", "screen", "overlay", "darken", "lighten", "add", "difference", "exclusion", "color_dodge",
// "color_burn", "hard_light", "soft_light", "hue", "saturation", "color" or "luminosity".
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
    int width = 0;   // as stored: with an orientation of 5..8 the upright picture has them swapped
    int height = 0;
    pixel_format format = pixel_format::gray;  // what the source file holds
    // The EXIF orientation (1..8, as the standard numbers them) stored in a JPEG or PNG, or 1 ("upright") when there is
    // none, the format has none, or the tag is not readable: a damaged EXIF block never throws. Apply it with
    // image::orient(), or let loader::exif_orient() do it while decoding.
    int orientation = 1;
    int channels() const noexcept { return channel_count(format); }

    // From encoded bytes. Throws: invalid_argument (null data), decode_error, limit_error.
    static image_info read_bytes(const void* data, std::size_t size);
    // From a file. Also throws io_error.
    static image_info read_file(const std::filesystem::path& path);
};

// The default of loader::max_bytes: refuse to decode anything whose output would exceed 512 MiB.
inline constexpr std::size_t default_max_decode_bytes = std::size_t(1) << 29;

namespace detail {
// The scanner behind image_info::orientation and loader::exif_orient(): the Orientation tag of the EXIF block of an
// encoded JPEG or PNG, or 1 when there is none or the data is not readable. Never throws and never reads out of
// bounds, so it is safe on untrusted input (the data need not even be a valid image).
STB_API int exif_orientation(const void* data, std::size_t size) noexcept;
}  // namespace detail

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

class resizer;  // defined after image, which only takes a pointer to one

// Defaults of to_png / write_png and to_jpg / write_jpg.
inline constexpr int default_png_compression = 8;  // 1..9, higher = smaller and slower
inline constexpr int default_jpg_quality = 90;     // 1..100

// The pixel types. image is image8u: the 8-bit image every codec, blend and resize works on, a mat8u
// (see mat.hpp) with the meaning of a pixel on top. image32f is another name for mat32f: one float
// per channel (0 is black and 1 is full intensity for colour data), for the maths of filtering.
class image8u;
using image = image8u;
using image32f = mat32f;

namespace detail {
// What cast<To>() gives: an image8u for 8 bits (it knows its pixel format), a mat<To> for any other type.
template <class To>
struct cast_result {
    using type = mat<To>;
};
template <>
struct cast_result<std::uint8_t> {
    using type = image8u;
};
}  // namespace detail
template <class To>
using cast_result_t = typename detail::cast_result<To>::type;

// An 8-bit image, the one everything else in libstb works on (image is another name for it): a
// mat8u plus decoding, encoding, blending, resizing and the other operations below. An empty
// (default-constructed) image has size() == 0. Being a mat8u, it goes wherever a mat8u does.
//
// Move-only: an image never copies its pixels behind your back. Say so with
// copy(). The pixels may be memory the image owns (constructors, decode) or
// memory someone else owns (wrap), which is how a numpy array becomes an
// image without a copy.
//
// Thread-safety: like std::vector - const members are safe to call
// concurrently, mutation needs external synchronisation. The static
// factories are safe to call from any number of threads.
class STB_API image8u : public mat8u {
    using base = mat8u;

public:
    image8u() = default;

    // Zero-filled. Throws invalid_argument unless width, height >= 1.
    image8u(int width, int height, pixel_format format) : base(width, height, format) {}
    // Takes ownership of `pixels`; throws invalid_argument on a size mismatch.
    image8u(int width, int height, pixel_format format, std::vector<std::uint8_t> pixels)
        : base(width, height, format, std::move(pixels)) {}
    // An image from a mat8u of 1 to 4 channels (a move: no pixel is copied).
    explicit image8u(mat8u&& m) noexcept : base(std::move(m)) {}

    // Views memory that someone else owns, without copying it. See mat::wrap.
    static image8u wrap(int width, int height, pixel_format format, std::uint8_t* data,
                        std::shared_ptr<void> keep_alive = {}) {
        return image8u(base::wrap(width, height, format, data, std::move(keep_alive)));
    }

    image8u(image8u&&) noexcept = default;
    image8u& operator=(image8u&&) noexcept = default;
    image8u(const image8u&) = delete;             // copies are explicit:
    image8u& operator=(const image8u&) = delete;  // see copy()
    ~image8u() = default;

    // A deep copy that owns its own pixels (also when this image is a wrap).
    image8u copy() const { return image8u(base::copy()); }

    // --- decoding (throws invalid_argument, decode_error, limit_error) ---
    // With the defaults (the file's own format, upright as stored, 512 MiB limit); for anything else see `loader`.
    static image decode(const void* data, std::size_t size);
    // Also throws io_error.
    static image open(const std::filesystem::path& path);

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
    // see image_info::orientation): the transform a viewer applies. 1 is a copy (a move on a temporary).
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

    // The same picture in another pixel type, keeping the range: cast<float>() is a mat32f, cast<std::uint8_t>() a copy (see cast).
    template <class To>
    cast_result_t<To> cast() const;

};

// --- the pixel type -------------------------------------------------------------------------------
// cast<To>(src): the same picture in another pixel type, keeping the range: white stays white (see pixel_range: a float is 0 to 1,
// an 8-bit value 0 to 255, a 16-bit value 0 to 65535, a signed 16-bit value -32768 to 32767 read as -1 to 1). It works for every
// pair of integer types (not bool) and float types, with the same size and channels; an empty input gives an empty result.
// A value is read as value / max of its type and written as round(clamp(value * max of the new type, lowest, max)), NaN is 0:
//   cast<float>(mat8u)           every value / 255: 0 is 0.0f and 255 is exactly 1.0f. A mat32f.
//   cast<std::uint8_t>(mat32f)   round(clamp(v, 0, 1) * 255), and NaN becomes 0: the inverse of the above for every 8-bit value.
//                                An image8u. Throws invalid_argument when src has more than 4 channels (an image has at most 4).
//   cast<std::uint16_t>(mat8u)   a value times 257 (255 is 65535); and so on for the other pairs.
//   the same type                a copy (an image8u for 8 bits).
// The pairs of 8 bits and float32 have kernels that use SIMD; the others are one loop in double precision, on all the cores when
// the image is big. An image8u is a mat8u: cast<float>(img) works, and so does img.cast<float>().
template <class To, class From>
cast_result_t<To> cast(const mat<From>& src) {
    using in = pixel_range<From>;
    using out = pixel_range<To>;
    if (src.empty()) return cast_result_t<To>();
    cast_result_t<To> dst = [&] {
        if constexpr (std::is_same_v<To, std::uint8_t>) return image8u(src.width(), src.height(), pixel_format_from_channels(src.channels()));
        else return mat<To>(src.width(), src.height(), src.channels());
    }();
    const double scale = double(out::max) / double(in::max);
    const From* from = src.data();
    To* to = dst.data();
    utils::parallel_for(src.size(), 2.0, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) to[i] = out::store(double(from[i]) * scale);
    });
    return dst;
}

template <>
STB_API mat32f cast<float, std::uint8_t>(const mat8u& src);
template <>
STB_API image8u cast<std::uint8_t, float>(const mat32f& src);
template <>
STB_API mat32f cast<float, float>(const mat32f& src);
template <>
STB_API image8u cast<std::uint8_t, std::uint8_t>(const mat8u& src);

template <class To>
cast_result_t<To> image8u::cast() const {
    return stb::cast<To>(static_cast<const mat8u&>(*this));
}

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

    // Back to straight RGBA like to_image(), but without a copy when this object owns its pixels (it was made by
    // the constructor or from_image(), not wrap()ped, and nothing else shares them): the pixels are converted in
    // place and the same memory becomes the image, which frees it. That is how a finished render leaves a
    // rasterizer (a 8192 x 8192 picture is 256 MB). A wrapped buffer is somebody else's and is left alone: the
    // result is a copy then. This object is empty afterwards in both cases.
    image into_image() &&;

    // Sets every pixel to `c` (straight alpha, as everywhere in the API; converted once to its premultiplied
    // form). Padding between rows is left alone. An empty object stays empty.
    void fill(const color& c);

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
// The operators run on the SIMD backend in use (see simd_backends()) and give the same pixels on every one.
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
    // A default resizer: filter automatic, edge clamp, sRGB blending, 512 MiB limit.
    resizer() = default;
    // The filter is the main parameter; every other setting is a call that returns the resizer, so they chain:
    //   resizer(resize_filter::mitchell).edge(resize_edge::wrap).srgb(false).max_bytes(1 << 26)
    //   resizer("cubic")
    // A name is parsed by resize_filter_from_name (invalid_argument if unknown). An enum value that is not one of
    // resize_filter / resize_edge throws invalid_argument at the call that gives it.
    explicit resizer(resize_filter filter);
    explicit resizer(std::string_view filter_name);

    resizer& filter(resize_filter f) &;
    resizer& filter(std::string_view name) &;
    // How the filter reads past the image's edge (default clamp).
    resizer& edge(resize_edge e) &;
    // Treat 8-bit colour channels as sRGB and blend in linear light (correct for photos and UI; alpha is always
    // linear). Turn off for data such as normal maps or masks, which are already linear (default true).
    resizer& srgb(bool on) &;
    // Refuse to produce an output larger than this many bytes (default 512 MiB).
    resizer& max_bytes(std::size_t bytes) &;

    // The same on a temporary, so that a chain starting from the constructor works.
    resizer&& filter(resize_filter f) && { filter(f); return std::move(*this); }
    resizer&& filter(std::string_view name) && { filter(name); return std::move(*this); }
    resizer&& edge(resize_edge e) && { edge(e); return std::move(*this); }
    resizer&& srgb(bool on) && { srgb(on); return std::move(*this); }
    resizer&& max_bytes(std::size_t bytes) && { max_bytes(bytes); return std::move(*this); }

    // What is set.
    resize_filter filter() const noexcept { return filter_; }
    resize_edge edge() const noexcept { return edge_; }
    bool srgb() const noexcept { return srgb_; }
    std::size_t max_bytes() const noexcept { return max_bytes_; }

    // Throws invalid_argument (empty image, width/height < 1),
    // limit_error (output larger than max_bytes() or 2 GiB),
    // error (allocation failure inside stb).
    image resize(const image& src, int width, int height) const;

private:
    resize_filter filter_ = resize_filter::automatic;
    resize_edge edge_ = resize_edge::clamp;
    bool srgb_ = true;
    std::size_t max_bytes_ = std::size_t(1) << 29;  // 512 MiB
};

// Decodes an image with settings. The source is the main parameter and goes in the constructor; every other setting is a call
// that returns the loader, so they chain in any order and run() does the work:
//
//     image photo = stb::loader("holiday.jpg").exif_orient().format(pixel_format::rgb).max_bytes(1 << 26).run();
//     image a = stb::loader(bytes.data(), bytes.size()).flip().run();
//
// A loader made from memory does not copy the bytes: they must outlive the loader (a path is copied). run() throws what
// image::decode and image::open throw: invalid_argument (null data), decode_error, limit_error, and io_error for a file.
class STB_API loader {
public:
    loader(const void* data, std::size_t size);
    explicit loader(std::filesystem::path path);

    // Convert to this format (default: keep the source's).
    loader& format(pixel_format f) &;
    // Flip vertically while decoding (per call, thread-safe). Default off.
    loader& flip(bool on = true) &;
    // Apply the EXIF orientation stored in the file (JPEG and PNG), so a phone photo comes out upright. The result may
    // have width and height swapped relative to image_info. Combined with flip, the orientation is applied first. Default off.
    loader& exif_orient(bool on = true) &;
    // Refuse to decode anything whose output would exceed this many bytes. Checked from the header BEFORE any pixel
    // memory is allocated. Default default_max_decode_bytes (512 MiB).
    loader& max_bytes(std::size_t bytes) &;

    // The same on a temporary, so that a chain starting from the constructor works.
    loader&& format(pixel_format f) && { format(f); return std::move(*this); }
    loader&& flip(bool on = true) && { flip(on); return std::move(*this); }
    loader&& exif_orient(bool on = true) && { exif_orient(on); return std::move(*this); }
    loader&& max_bytes(std::size_t bytes) && { max_bytes(bytes); return std::move(*this); }

    // Does the work; can be called again.
    image run() const;

private:
    const void* data_ = nullptr;
    std::size_t size_ = 0;
    std::filesystem::path path_;
    bool from_file_ = false;
    std::optional<pixel_format> format_;
    bool flip_ = false;
    bool exif_orient_ = false;
    std::size_t max_bytes_ = default_max_decode_bytes;
};

// ---------------------------------------------------------------------------------------- traits
namespace traits {
namespace detail_image {
template <class M, class = void>
struct is_image_input_impl : std::false_type {};
template <class M>
struct is_image_input_impl<M, std::enable_if_t<is_mat_v<M>>>
    : std::bool_constant<std::is_same_v<mat_value_t<M>, std::uint8_t> || std::is_same_v<mat_value_t<M>, float>> {};
}  // namespace detail_image

// True for what the image functions (tone.hpp) take: a mat8u (so an image8u) or a mat32f (image32f), or a
// class derived from one of them.
template <class M>
struct is_image_input : detail_image::is_image_input_impl<M> {};
template <class M>
inline constexpr bool is_image_input_v = is_image_input<M>::value;

// For the return type of a function template: R (M unless said otherwise) when M is an image input,
// otherwise the function drops out of overload resolution.
//   template <class M> traits::if_image_t<M> equalize(const M& src);          // returns the type it is given
//   template <class M> traits::if_image_t<M, image8u> to_gray(const M& src);  // always an image8u
template <class M, class R = M>
using if_image_t = std::enable_if_t<is_image_input_v<M>, R>;

// The same for the overload that takes an rvalue as `M&&`: it must not be chosen for an lvalue (M would be
// deduced as a reference there).
template <class M, class R = M>
using if_image_rvalue_t = std::enable_if_t<is_image_input_v<M> && !std::is_lvalue_reference_v<M>, R>;

// For the in-place functions: they take `M& img` (a non-const lvalue: M would be deduced as const for a const one)
// and give it back, so the type is M& and the call chains. The same drop-out for anything that is not an image input.
//   template <class M> traits::if_image_mutable_t<M> equalize_inplace(M& img, tone_mode mode);
template <class M>
using if_image_mutable_t = std::enable_if_t<is_image_input_v<M> && !std::is_const_v<M>, M&>;

}  // namespace traits

// ---- the pixel types that have no code of their own: they go through float
// The algorithms are compiled for 8 bits (a mat8u) and for float. Every other unsigned integer type (16 bits, 32 bits) is a "wide"
// pixel: its value is read as value / max, the algorithm runs on floats and the result is written back with the rounding and
// clamping of the type (see cast). A wide image is accepted by the same functions: equalize(mat<std::uint16_t>) is a mat<std::uint16_t>.
// Signed integers are not pictures (a level 0 to max): only cast() takes them.
namespace detail {
template <class T>
inline constexpr bool is_wide_v = std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, std::uint8_t>;
}  // namespace detail

namespace traits {
namespace detail_pixel_input {
template <class M, class = void>
struct impl : std::false_type {};
template <class M>
struct impl<M, std::enable_if_t<is_mat_v<M>>> : std::bool_constant<is_image_input_v<M> || stb::detail::is_wide_v<mat_value_t<M>>> {};
}  // namespace detail_pixel_input

// True for what the algorithms take: an image input (mat8u, mat32f) or a mat of a wide unsigned integer type.
template <class M>
inline constexpr bool is_pixel_input_v = detail_pixel_input::impl<M>::value;

// The same three drop-out types as if_image_t, if_image_rvalue_t and if_image_mutable_t, for every pixel input.
template <class M, class R = M>
using if_pixel_t = std::enable_if_t<is_pixel_input_v<M>, R>;
template <class M, class R = M>
using if_pixel_rvalue_t = std::enable_if_t<is_pixel_input_v<M> && !std::is_lvalue_reference_v<M>, R>;
template <class M>
using if_pixel_mutable_t = std::enable_if_t<is_pixel_input_v<M> && !std::is_const_v<M>, M&>;
}  // namespace traits

namespace detail {
// A function of floats applied to a wide image: to float, the function, back to the type (a new image).
template <class T, class Fn>
mat<T> via_float(const mat<T>& src, Fn&& fn) {
    return cast<T>(fn(cast<float>(src)));
}
// The same for a function that changes its argument: img gets the result (img may view memory of someone else's, so the values
// are written into it).
template <class T, class Fn>
void via_float_inplace(mat<T>& img, Fn&& fn) {
    if (img.empty()) return;
    mat32f f = cast<float>(img);
    fn(f);
    const mat<T> back = cast<T>(f);
    std::copy(back.data(), back.data() + back.size(), img.data());
}
}  // namespace detail

}  // namespace stb
