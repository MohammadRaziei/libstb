#pragma once

// Superpixels: a partition of an image into compact regions of similar colour (about `count` of them),
// the usual first step of segmentation, colour quantisation, a mosaic that follows the picture, or a
// feature grid for a model.
//
//   image8u img = image8u::decode(bytes, size);
//   superpixel_map sp = stb::superpixel(img, 300).run();          // SLICO by default
//   superpixel_map sn = stb::superpixel(img, 300).method(superpixel_method::snic).compactness(20).run();
//   image8u mosaic = sp.mean_color(img);                          // every region in its mean colour
//   image8u lines = sp.overlay(img, color(255, 255, 0));          // region borders on the picture
//   int region_of_pixel = sp.label(x, y);                         // 0 .. sp.count() - 1
//
// The algorithms are SLIC and SLICO (Achanta et al., "SLIC Superpixels Compared to State-of-the-Art
// Superpixel Methods", TPAMI 2012) and SNIC (Achanta and Susstrunk, "Superpixels and Polygons using
// Simple Non-Iterative Clustering", CVPR 2017), written for libstb from those papers. They cluster in the
// CIELAB colour space plus position, so a region is as far as its colour stays alike and as wide as its
// compactness allows. SICLE (Belem, Perret, Cousty, Guimaraes and Falcao, "Efficient Multiscale Object-based
// Superpixel Framework", arXiv 2204.03533, 2022) is a path-based method: it starts from many more seeds than regions,
// grows a region from each by the Image Foresting Transform and throws the least relevant seeds away until the wanted
// number is left. With the right schedule it is DISF (Belem, Guimaraes and Falcao, IEEE Signal Processing Letters 2020).
// It is written from the papers too; the code of their implementations was not looked at.

#include <cstdint>
#include <utility>

#include "stb/color.hpp"
#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

enum class superpixel_method : std::uint8_t {
    slic,   // k-means in colour + position, one compactness for every region
    slico,  // SLIC where every region adapts its own compactness: no parameter to tune (the default)
    snic,   // one pass over the pixels in order of distance: no iterations, regions are connected by construction
    sicle,  // seed oversampling, then runs of the Image Foresting Transform that drop the least relevant seeds each time;
            // follows edges closely, regions are connected by construction, compactness is not used. Wants a `count` several
            // times the number of distinct flat areas in the picture: with fewer regions than areas, an area can lose its seed
};

namespace detail {
struct superpixel_params {  // the settings of a superpixel builder; the defaults are the documented ones
    int count = 400;
    float compactness = 10.0f;
    int iterations = 0;  // 0: the method's own default (10 for slic and slico, 5 for sicle)
    bool enforce_connectivity = true;
    int oversample = 0;           // sicle: 0 is automatic
    float removal_rate = 0.0f;    // sicle: 0 is the schedule of `iterations`
    superpixel_method method = superpixel_method::slico;
};
}  // namespace detail

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

// Cuts an image into superpixels. The main parameters go in the constructor and every other setting is a call that
// returns the builder, so they chain in any order and run() does the work:
//
//     superpixel_map sp = stb::superpixel(img, 400).method(superpixel_method::slic).compactness(15).iterations(5).run();
//
// `src` (1 to 4 channels; an alpha channel is ignored; gray is read as gray) must outlive the builder: it is not copied.
// A temporary image is refused at compile time for that reason. Deterministic: the same image and settings give the same
// map on every number of threads and every SIMD backend. SLIC and SLICO use all the cores; SNIC and SICLE are sequential passes.
// A bad value throws invalid_argument at the call that gives it (the constructor for the image and the count).
class STB_API superpixel {
public:
    // `count`: about how many regions. The picture is cut into a grid of count cells and the regions start there; the
    // result has about as many (see superpixel_map::count()). Must be >= 1. Throws for an empty image or one with more
    // than 4 channels.
    superpixel(const mat8u& src, int count);
    superpixel(mat8u&&, int) = delete;

    // Which algorithm (default slico).
    superpixel& method(superpixel_method m) &;
    // How much position counts against colour (CIELAB units, 10 is usual, 1 to 40 is the useful range): higher gives
    // squarer, more regular regions, lower follows the edges more closely. For slico this is only where the first
    // iteration starts. Must be a positive finite number (default 10).
    superpixel& compactness(float c) &;
    // slic and slico: how many times the regions are refined (they stop earlier if nothing moves any more; default 10).
    // sicle: the most runs of the Image Foresting Transform, the last one with the wanted number of seeds (default 5; 1
    // is a single run from `count` seeds, no oversampling). Unused by snic. Must be >= 1.
    superpixel& iterations(int n) &;
    // slic and slico can leave a few small detached pieces: merge them into a neighbour so that every region is one
    // connected piece (default true; snic always is connected).
    superpixel& enforce_connectivity(bool on) &;
    // sicle only: how many seeds it starts from, on a grid that follows the shape of the picture. More seeds is slower and
    // follows thin structures better; the paper finds that a few thousand are enough for a photo. 0 is automatic: the
    // larger of 3000 and 5 times `count`, never more than the pixels. A number below `count` is raised to it.
    // Must be >= 0.
    superpixel& oversample(int seeds) &;
    // sicle only: if above 0, every run drops this share of the seeds that are left (a number in (0, 1); 1 - 1/e = 0.63
    // is DISF) and runs repeat until `count` are left, instead of following the schedule of `iterations`. 0 is off.
    // Must be in [0, 1).
    superpixel& removal_rate(float rate) &;

    // The same on a temporary, so that a chain starting from the constructor works.
    superpixel&& method(superpixel_method m) && { method(m); return std::move(*this); }
    superpixel&& compactness(float c) && { compactness(c); return std::move(*this); }
    superpixel&& iterations(int n) && { iterations(n); return std::move(*this); }
    superpixel&& enforce_connectivity(bool on) && { enforce_connectivity(on); return std::move(*this); }
    superpixel&& oversample(int seeds) && { oversample(seeds); return std::move(*this); }
    superpixel&& removal_rate(float rate) && { removal_rate(rate); return std::move(*this); }

    // Does the work. Can be called again, also after changing a setting.
    superpixel_map run() const;

private:
    const mat8u* src_;
    detail::superpixel_params p_;
};

}  // namespace stb
