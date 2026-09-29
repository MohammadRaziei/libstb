#pragma once

// Internal helpers shared by the bind_*.cpp files.

#include <nanobind/nanobind.h>

#include "stb.h"

namespace nb = nanobind;

void bind_image(nb::module_& m);
void bind_resizer(nb::module_& m);
void bind_font(nb::module_& m);
