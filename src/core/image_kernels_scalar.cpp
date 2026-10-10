#include "image_kernels.hpp"

// The pixel kernels compiled with the compiler's auto-vectorisation switched off (CMake
// gives this one file -fno-tree-vectorize / -fno-vectorize): the plain one-pixel-at-a-time
// reference. It is the "scalar" backend, and what the tests compare the vector ones with.
// Where the compiler has no such switch (MSVC) it may still vectorise: the results are the
// same either way.

namespace stb {
namespace detail {
namespace {

template <int SC, int DC>
void convert_scalar(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { convert_row<SC, DC>(s, d, n); }
template <int SC, int DC>
void composite_scalar(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { composite_row<SC, DC>(s, d, n); }

#define STB_PICK_SCALAR(FN, SC, DC) (&FN##_scalar<SC, DC>)

}  // namespace

row_fn pick_convert_scalar(int sc, int dc) {
    switch (sc * 10 + dc) {
        STB_CONVERT_CASES(STB_PICK_SCALAR)
        default: return nullptr;  // same channel count
    }
}

row_fn pick_composite_scalar(int sc, int dc) {
    switch (sc * 10 + dc) {
        STB_COMPOSITE_CASES(STB_PICK_SCALAR)
        default: return &composite_scalar<4, 4>;
    }
}

}  // namespace detail
}  // namespace stb
