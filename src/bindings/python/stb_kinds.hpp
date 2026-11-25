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

// What polybind::array_kind needs to know about stb::mat<T>
struct mat_traits {
    template <class T>
    using matrix = stb::mat<T>;
    template <class T, class A>
    static stb::mat<T> view(const A& a) { return view_of_array<T>(a); }
    template <class T>
    static T* data(stb::mat<T>& m) { return m.data(); }
    template <class T>
    static std::size_t height(const stb::mat<T>& m) { return std::size_t(m.height()); }
    template <class T>
    static std::size_t width(const stb::mat<T>& m) { return std::size_t(m.width()); }
    template <class T>
    static std::size_t channels(const stb::mat<T>& m) { return std::size_t(m.channels()); }
    template <class T>
    static bool empty(const stb::mat<T>& m) { return m.empty(); }
};

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

template <>
struct nanobind_backend::kind<stb::mat8u> : array_kind<stb_py::mat_traits, std::uint8_t> {};
template <>
struct nanobind_backend::kind<stb::mat32f> : array_kind<stb_py::mat_traits, float> {};

// A kind that is only ever the result of a function: the pair of derivatives of sobel() and scharr(): a tuple (dx, dy) of float32
// arrays. (A number, an int level or a double, is a ready-made kind of polybind.)
template <>
struct nanobind_backend::kind<stb::gradients> {
    using new_ret = nanobind::tuple;
    static constexpr auto new_policy = nanobind::rv_policy::automatic;
    static new_ret make(stb::gradients&& g, bool three_d) {
        auto dx = kind<stb::mat32f>::make(std::move(g.dx), three_d);
        auto dy = kind<stb::mat32f>::make(std::move(g.dy), three_d);
        return nanobind::make_tuple(std::move(dx), std::move(dy));
    }
    template <class H, class = decltype(std::declval<const H&>().three_d)>
    static new_ret wrap(stb::gradients&& g, const H& like) {
        return make(std::move(g), like.three_d);
    }
    static new_ret wrap(stb::gradients&& g) { return make(std::move(g), false); }
};

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
        static constexpr bool three_d = true;  // an array made from an Image is (H, W, C), also for one channel
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
