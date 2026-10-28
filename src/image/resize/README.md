# resize

The resizer behind `image::resize`, split by topic from stb_image_resize2.h v2.18. It is one translation
unit: `src/image/resize.cpp` includes the parts in this order (api, config, srgb, simd, filters,
coefficients, coders, decode, horizontal, vertical, scanline, sampler, engine), because the kernels
are specialised by macros and the compiler needs to see them together. `coders.inl`, `horizontals.inl` and
`verticals.inl` are templates that the parts above include several times with different macros set (what
upstream does by including its own header again).

License and lineage: `NOTICE.md` at the repository root. The algorithms are unchanged; `tests/tools/snapshot.py` compares the output with the original build on
every filter, edge mode, colour space and layout.
