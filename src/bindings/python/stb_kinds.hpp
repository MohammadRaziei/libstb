#pragma once

// The kinds of data the Python functions of libstb work on, for polybind::def_dispatch (polybind/dispatch.hpp):
//   stb::image  the class Image (uint8 pixels with a pixel format),
//   stb::mat8u  a uint8 numpy array (H, W) or (H, W, C),
//   stb::mat32f a float32 numpy array of the same shapes (pixels 0..1).
// An array is received without conversion (polybind::strict) and worked on in place through a view of its memory.
// A function that makes a new value gives back a stb::image (a new Image) or a stb::mat8u / stb::mat32f (a new array that owns
// its memory: the shape of the argument for a result of one channel ((H, W) or (H, W, 1)), (H, W, C) otherwise).

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <string_view>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <polybind/nanobind.hpp>

#include "stb.h"

namespace stb_py {

namespace nb = nanobind;

// A view of the memory of an array of shape (H, W) or (H, W, C): nothing is copied.
template <class T, class A>
stb::mat<T> view_of_array(const A& a) {
    if (a.ndim() != 2 && a.ndim() != 3) throw std::invalid_argument("expected an array of shape (H, W) or (H, W, C)");
    const std::size_t h = a.shape(0), w = a.shape(1), c = a.ndim() == 3 ? a.shape(2) : 1;
    if (h == 0 || w == 0 || c == 0) throw std::invalid_argument("the array is empty");
    return stb::mat<T>::wrap(int(w), int(h), int(c), const_cast<T*>(a.data()));
}

}  // namespace stb_py

namespace polybind {

template <>
struct enum_names<stb::tone_mode> {
    static constexpr bool enabled = true;
    static stb::tone_mode from_name(std::string_view n) { return stb::tone_mode_from_name(n); }
};
template <>
struct enum_names<stb::edge_mode> {
    static constexpr bool enabled = true;
    static stb::edge_mode from_name(std::string_view n) { return stb::edge_mode_from_name(n); }
};
template <>
struct enum_names<stb::color_space> {
    static constexpr bool enabled = true;
    static stb::color_space from_name(std::string_view n) { return stb::color_space_from_name(n); }
};

template <class T>
struct stb_array_kind {
    using array_rw = strict<nanobind::ndarray<T, nanobind::c_contig, nanobind::device::cpu>>;
    using array_ro = strict<nanobind::ndarray<const T, nanobind::c_contig, nanobind::device::cpu>>;
    using const_arg = array_ro;
    using mutable_arg = array_rw;
    using mutable_ret = array_rw;
    static constexpr auto policy = nanobind::rv_policy::automatic;
    struct holder {
        stb::mat<T> view;
        stb::mat<T>& get() { return view; }
    };
    struct const_holder {
        stb::mat<T> view;
        bool three_d;  // the argument is (H, W, C): a result of one channel is (H, W, 1), not (H, W)
        explicit const_holder(const array_ro& a) : view(stb_py::view_of_array<T>(a.value)), three_d(a.value.ndim() == 3) {}
        const stb::mat<T>& get() { return view; }
    };
    static holder mutable_holder(const array_rw& a) { return holder{stb_py::view_of_array<T>(a.value)}; }
    static array_rw give_back(const array_rw& a) { return a; }

    // a new array: the matrix moves to the heap and a capsule frees it with the array
    using new_ret = nanobind::ndarray<nanobind::numpy, T>;
    static constexpr auto new_policy = nanobind::rv_policy::automatic;
    static new_ret make(stb::mat<T>&& m, bool three_d) {
        if (m.empty()) throw std::runtime_error("the result is empty");
        auto* owned = new stb::mat<T>(std::move(m));
        nanobind::capsule owner(owned, [](void* p) noexcept { delete static_cast<stb::mat<T>*>(p); });
        const std::size_t shape[3] = {std::size_t(owned->height()), std::size_t(owned->width()), std::size_t(owned->channels())};
        return new_ret(owned->data(), three_d || owned->channels() != 1 ? 3 : 2, shape, owner);
    }
    // the result of a function has the shape of its argument (one channel: (H, W) for (H, W), (H, W, 1) for (H, W, 1))
    template <class H, class = decltype(std::declval<const H&>().three_d)>
    static new_ret wrap(stb::mat<T>&& m, const H& like) {
        return make(std::move(m), like.three_d);
    }
    static new_ret wrap(stb::mat<T>&& m) { return make(std::move(m), false); }
};

template <>
struct nanobind_backend::kind<stb::mat8u> : stb_array_kind<std::uint8_t> {};
template <>
struct nanobind_backend::kind<stb::mat32f> : stb_array_kind<float> {};

template <>
struct nanobind_backend::kind<stb::image> {
    using const_arg = const stb::image&;
    using mutable_arg = stb::image&;
    using mutable_ret = stb::image&;
    static constexpr auto policy = nanobind::rv_policy::none;
    struct holder {
        stb::image& ref;
        stb::image& get() { return ref; }
    };
    struct const_holder {
        const stb::image& ref;
        explicit const_holder(const stb::image& r) : ref(r) {}
        const stb::image& get() { return ref; }
    };
    static holder mutable_holder(stb::image& x) { return holder{x}; }
    static stb::image& give_back(stb::image& x) { return x; }

    using new_ret = stb::image;
    static constexpr auto new_policy = nanobind::rv_policy::automatic;
    static stb::image wrap(stb::image&& x) { return std::move(x); }
};


}  // namespace polybind

namespace stb_py {

// The kinds of the functions, in the order the overloads are tried (the class first: it is the cheapest to test).
inline constexpr polybind::kinds<stb::image, stb::mat8u, stb::mat32f> stb_kinds{};

}  // namespace stb_py
