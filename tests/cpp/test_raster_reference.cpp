#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../tools/raster_reference/reference_hashes.h"
#include "../tools/raster_reference/scenes.h"
#include "test_common.hpp"

// The rasterizer and the stroker are ports of the algorithms in FreeType (through plutovg), and what
// the library promises is that the pixels are the same: for every scene in scenes.h, the coverage image
// that stb::rasterize_fill / rasterize_stroke produce must hash to the value plutovg gave (see
// tests/tools/raster_reference/reference.c, which also says how to regenerate them).
//
// For debugging: with the environment variable STB_RASTER_DUMP set to a directory, the coverage images
// are written there as <name>.bin (w * h bytes), like reference.c does with its argument.

namespace {

std::uint64_t fnv1a(const std::vector<unsigned char>& v) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : v) h = (h ^ c) * 1099511628211ULL;
    return h;
}

stb::outline build_path(const ref_scene& s) {
    stb::outline p;
    for (int k = 0; k < s.nops; ++k) {
        const ref_op& o = s.ops[k];
        switch (o.op) {
            case 'M': p.move_to(o.v[0], o.v[1]); break;
            case 'L': p.line_to(o.v[0], o.v[1]); break;
            case 'Q': p.quad_to(o.v[0], o.v[1], o.v[2], o.v[3]); break;
            case 'C': p.cubic_to(o.v[0], o.v[1], o.v[2], o.v[3], o.v[4], o.v[5]); break;
            case 'Z': p.close(); break;
        }
    }
    return p;
}

std::vector<unsigned char> coverage_of(const ref_scene& s) {
    const stb::outline p = build_path(s);
    const stb::matrix m{s.m[0], s.m[1], s.m[2], s.m[3], s.m[4], s.m[5]};
    const stb::clip_rect clip{0, 0, s.w, s.h};
    std::vector<unsigned char> cov(std::size_t(s.w) * std::size_t(s.h), 0);
    auto sink = [&](stb::span_view v) {
        for (const stb::span& sp : v) {
            for (int x = sp.x; x < sp.x + sp.len; ++x) {
                if (sp.y >= 0 && sp.y < s.h && x >= 0 && x < s.w) cov[std::size_t(sp.y) * std::size_t(s.w) + std::size_t(x)] = sp.coverage;
            }
        }
    };
    if (s.mode == 2) {
        stb::stroke_style st;
        st.width = s.width;
        st.cap = s.cap == 1 ? stb::line_cap::round : s.cap == 2 ? stb::line_cap::square : stb::line_cap::butt;
        st.join = s.join == 1 ? stb::line_join::round : s.join == 2 ? stb::line_join::bevel : stb::line_join::miter;
        st.miter_limit = s.miter;
        st.dashes.assign(s.dashes, s.dashes + s.ndashes);
        st.dash_offset = s.doff;
        stb::rasterize_stroke(p, st, m, clip, sink);
    } else {
        stb::rasterize_fill(p, s.mode == 1 ? stb::fill_rule::even_odd : stb::fill_rule::non_zero, m, clip, sink);
    }
    return cov;
}

}  // namespace

UTEST(stb_raster_reference, every_scene_matches_plutovg_pixel_for_pixel) {
    const char* dump = std::getenv("STB_RASTER_DUMP");
    const int golden_count = int(sizeof(ref_hashes) / sizeof(ref_hashes[0]));
    ASSERT_EQ(golden_count, ref_scene_count);

    int mismatches = 0;
    for (int i = 0; i < ref_scene_count; ++i) {
        const ref_scene& s = ref_scenes[i];
        ASSERT_TRUE(std::strcmp(s.name, ref_hashes[i].name) == 0);

        const std::vector<unsigned char> cov = coverage_of(s);
        if (dump) {
            const std::string file = std::string(dump) + "/" + s.name + ".bin";
            if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
                std::fwrite(cov.data(), 1, cov.size(), f);
                std::fclose(f);
            }
        }
        if (fnv1a(cov) != ref_hashes[i].hash) {
            std::printf("scene %s differs from the plutovg reference\n", s.name);
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0);
}
