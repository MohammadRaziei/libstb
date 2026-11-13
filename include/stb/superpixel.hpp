#pragma once

// Superpixels: a partition of an image into compact regions of similar colour (about `count` of them),
// the usual first step of segmentation, colour quantisation, a mosaic that follows the picture, or a
// feature grid for a model.
//
//   image8u img = image8u::decode(bytes, size);
//   superpixel_options opt; opt.count = 300;
//   superpixel_map sp = stb::superpixels(img, opt);               // SLICO by default
//   image8u mosaic = sp.mean_color(img);                          // every region in its mean colour
//   image8u lines = sp.overlay(img, color(255, 255, 0));          // region borders on the picture
//   int region_of_pixel = sp.label(x, y);                         // 0 .. sp.count() - 1
//
// The algorithms are SLIC and SLICO (Achanta et al., "SLIC Superpixels Compared to State-of-the-Art
// Superpixel Methods", TPAMI 2012) and SNIC (Achanta and Susstrunk, "Superpixels and Polygons using
// Simple Non-Iterative Clustering", CVPR 2017), written for libstb from those papers. They cluster in the
// CIELAB colour space plus position, so a region is as far as its colour stays alike and as wide as its
// compactness allows.

#include <cstdint>

#include "stb/color.hpp"
#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

enum class superpixel_method : std::uint8_t {
    slic,   // k-means in colour + position, one compactness for every region
    slico,  // SLIC where every region adapts its own compactness: no parameter to tune (the default)
    snic,   // one pass over the pixels in order of distance: no iterations, regions are connected by construction
};

struct superpixel_options {
    // About how many regions: the picture is cut into a grid of count cells and the regions start there.
    // The result has about as many (see superpixel_map::count()). Must be >= 1.
    int count = 400;
    // How much position counts against colour (CIELAB units, 10 is usual, 1 to 40 is the useful range):
    // higher gives squarer, more regular regions, lower follows the edges more closely. For slico this
    // is only where the first iteration starts. Must be a positive finite number.
    float compactness = 10.0f;
    // How many times slic and slico refine the regions (they stop earlier if nothing moves any more).
    // Unused by snic. Must be >= 1.
    int iterations = 10;
    // slic and slico can leave a few small detached pieces: merge them into a neighbour so that every
    // region is one connected piece. (snic always is.)
    bool enforce_connectivity = true;
    superpixel_method method = superpixel_method::slico;
};

// The regions of an image: a label for every pixel, 0 .. count() - 1, each label one connected area (4-neighbours,
// unless enforce_connectivity was off). Move-only like the arrays it holds; copy() says so.
class STB_API superpixel_map {
public:
    superpixel_map() = default;
    // From a label map that you made yourself. Throws invalid_argument unless labels has one channel, count
    // >= 1 and every label is in [0, count).
    superpixel_map(mat32i labels, int count);

    superpixel_map(superpixel_map&&) noexcept = default;
    superpixel_map& operator=(superpixel_map&&) noexcept = default;
    superpixel_map(const superpixel_map&) = delete;             // copies are explicit:
    superpixel_map& operator=(const superpixel_map&) = delete;  // see copy()
    ~superpixel_map() = default;

    superpixel_map copy() const;

    bool empty() const noexcept { return labels_.empty(); }
    int width() const noexcept { return labels_.width(); }
    int height() const noexcept { return labels_.height(); }
    // How many regions there are.
    int count() const noexcept { return count_; }
    // The label of every pixel: a one-channel mat32i of the image's size.
    const mat32i& labels() const noexcept { return labels_; }
    // The region of pixel (x, y), unchecked like mat::operator().
    std::int32_t label(int x, int y) const noexcept { return labels_(x, y); }

    // The pixels where a region ends: those whose right or lower neighbour has another label, in `c` on a
    // transparent RGBA image of the map's size.
    image8u boundaries(const color& c = color(255, 0, 0)) const;
    // `src` with its region borders drawn in `c` (the colour channels of the border pixels are replaced, an alpha
    // channel is kept). A gray image becomes RGB. Throws invalid_argument if src is not the map's size.
    image8u overlay(const mat8u& src, const color& c = color(255, 0, 0)) const;
    // `src` with every pixel replaced by the mean of its region, per channel (rounded). Same format as src.
    // Throws invalid_argument if src is not the map's size or has more than 4 channels.
    image8u mean_color(const mat8u& src) const;

private:
    mat32i labels_;
    int count_ = 0;
};

// Cuts `src` (1 to 4 channels; an alpha channel is ignored; gray is read as gray) into superpixels.
// Deterministic: the same image and options give the same map on every number of threads and every SIMD
// backend. SLIC and SLICO use all the cores; SNIC is one sequential pass. Throws invalid_argument for an
// empty image, one with more than 4 channels, or options outside the ranges documented above.
STB_API superpixel_map superpixels(const mat8u& src, const superpixel_options& options = {});

}  // namespace stb
