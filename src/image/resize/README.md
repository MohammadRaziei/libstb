# resize

The resizer behind `image::resize`, split by topic from stb_image_resize2.h v2.18. It is one translation
unit: `src/image/resize.cpp` includes the parts in this order (api, config, srgb, simd, filters,
coefficients, coders, decode, horizontal, vertical, scanline, sampler, engine), because the kernels
are specialised by macros and the compiler needs to see them together. `coders.inl`, `horizontals.inl` and
`verticals.inl` are templates that the parts above include several times with different macros set (what
upstream does by including its own header again).

License and lineage: `NOTICE.md` at the repository root. The algorithms are unchanged; `tests/tools/snapshot.py` compares the output with the original build on
every filter, edge mode, colour space and layout.

One function of upstream that the split had left out is back: `stbir_resize_extended_split` (engine.hpp), the per-piece entry point of upstream's own multi-threading API. `resize.cpp` calls it from `resizer::resize` when the work is big enough to be shared between threads (see `src/util/parallel.hpp`); the output is byte for byte the one-thread output.
