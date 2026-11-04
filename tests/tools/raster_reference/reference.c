// Draws the scenes of scenes.h with plutovg and prints "<name> <hash>" for each, where the hash is the
// 64-bit FNV-1a of the coverage image (w * h bytes, row by row). tests/cpp/test_raster_reference.cpp holds
// the printed values and checks that libstb gives the same pixels. With a directory as the argument the
// coverage images are also written there (<name>.bin), to find the first differing pixel.
//
// Build (plutovg 1.x, from a checkout of https://github.com/sammycage/plutovg):
//   cmake -S plutovg -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF && cmake --build build
//   cc -O2 -I plutovg/source -I plutovg/include reference.c build/libplutovg.a -lm -o reference
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plutovg-private.h"
#include "scenes.h"

static uint64_t fnv1a(const unsigned char* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

int main(int argc, char** argv) {
    for (int i = 0; i < ref_scene_count; ++i) {
        const ref_scene* s = &ref_scenes[i];
        plutovg_path_t* path = plutovg_path_create();
        for (int k = 0; k < s->nops; ++k) {
            const ref_op* o = &s->ops[k];
            switch (o->op) {
                case 'M': plutovg_path_move_to(path, o->v[0], o->v[1]); break;
                case 'L': plutovg_path_line_to(path, o->v[0], o->v[1]); break;
                case 'Q': plutovg_path_quad_to(path, o->v[0], o->v[1], o->v[2], o->v[3]); break;
                case 'C': plutovg_path_cubic_to(path, o->v[0], o->v[1], o->v[2], o->v[3], o->v[4], o->v[5]); break;
                case 'Z': plutovg_path_close(path); break;
            }
        }
        plutovg_matrix_t m;
        plutovg_matrix_init(&m, s->m[0], s->m[1], s->m[2], s->m[3], s->m[4], s->m[5]);
        plutovg_rect_t clip = {0, 0, (float)s->w, (float)s->h};

        plutovg_span_buffer_t spans;
        plutovg_span_buffer_init(&spans);
        if (s->mode == 2) {
            plutovg_stroke_data_t sd;
            memset(&sd, 0, sizeof sd);
            sd.style.width = s->width;
            sd.style.cap = s->cap == 1 ? PLUTOVG_LINE_CAP_ROUND : s->cap == 2 ? PLUTOVG_LINE_CAP_SQUARE : PLUTOVG_LINE_CAP_BUTT;
            sd.style.join = s->join == 1 ? PLUTOVG_LINE_JOIN_ROUND : s->join == 2 ? PLUTOVG_LINE_JOIN_BEVEL : PLUTOVG_LINE_JOIN_MITER;
            sd.style.miter_limit = s->miter;
            sd.dash.offset = s->doff;
            sd.dash.array.data = (float*)s->dashes;
            sd.dash.array.size = s->ndashes;
            plutovg_rasterize(&spans, path, &m, &clip, &sd, PLUTOVG_FILL_RULE_NON_ZERO);
        } else {
            plutovg_rasterize(&spans, path, &m, &clip, NULL,
                              s->mode == 1 ? PLUTOVG_FILL_RULE_EVEN_ODD : PLUTOVG_FILL_RULE_NON_ZERO);
        }

        unsigned char* cov = calloc((size_t)s->w * s->h, 1);
        for (int k = 0; k < spans.spans.size; ++k) {
            const plutovg_span_t* sp = &spans.spans.data[k];
            if (sp->y < 0 || sp->y >= s->h) continue;
            for (int x = sp->x; x < sp->x + sp->len; ++x)
                if (x >= 0 && x < s->w) cov[(size_t)sp->y * s->w + x] = (unsigned char)sp->coverage;
        }
        printf("%s %016llx\n", s->name, (unsigned long long)fnv1a(cov, (size_t)s->w * s->h));
        if (argc > 1) {
            char file[512];
            snprintf(file, sizeof file, "%s/%s.bin", argv[1], s->name);
            FILE* f = fopen(file, "wb");
            if (f) { fwrite(cov, 1, (size_t)s->w * s->h, f); fclose(f); }
        }
        free(cov);
        plutovg_span_buffer_destroy(&spans);
        plutovg_path_destroy(path);
    }
    return 0;
}
