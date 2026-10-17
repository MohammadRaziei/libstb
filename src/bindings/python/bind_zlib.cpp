#include <vector>

#include "binding.hpp"

using namespace nb::literals;

void bind_zlib(nb::module_& m) {
    m.attr("DEFAULT_ZLIB_LEVEL") = stb::default_zlib_level;
    m.attr("DEFAULT_ZLIB_MAX_BYTES") = stb::default_zlib_max_bytes;

    m.def("zlib_compress",
          [](nb::handle data, int level) {
              py_buffer in(data.ptr());
              std::vector<std::uint8_t> out;
              {
                  nb::gil_scoped_release unlock;  // compress without holding the GIL
                  out = stb::zlib_compress(in.view.buf, static_cast<std::size_t>(in.view.len), level);
              }
              return nb::bytes(reinterpret_cast<const char*>(out.data()), out.size());
          },
          "data"_a, "level"_a = stb::default_zlib_level);

    m.def("zlib_decompress",
          [](nb::handle data, std::size_t max_bytes) {
              py_buffer in(data.ptr());
              std::vector<std::uint8_t> out;
              {
                  nb::gil_scoped_release unlock;
                  out = stb::zlib_decompress(in.view.buf, static_cast<std::size_t>(in.view.len), max_bytes);
              }
              return nb::bytes(reinterpret_cast<const char*>(out.data()), out.size());
          },
          "data"_a, "max_bytes"_a = stb::default_zlib_max_bytes);
}
