"""Development check, NOT a test: does libstb's JPEG encoder (src/codec/jpeg.cpp) write the same
bytes as upstream stb_image_write.h v1.16?

JPEG is lossy and its bytes are not a stable contract: two correct builds may differ in rare
rounding ties (a compiler that fuses a*b+c, as Apple clang does on ARM64 or any x86 build with
-mfma, changes the output of the UNMODIFIED upstream encoder too). So this is only meaningful
when both are compiled with the SAME compiler and flags, which is why it lives here and not in
the test suite. Use it after touching the JPEG code; the tests check JPEG quality with
tolerances (tests/python/test_lossy_tolerance.py).

    python tests/tools/jpeg_vs_upstream.py /path/to/upstream/stb_image_write.h [c++] [extra flags]
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# The same 400 pseudo-random images for both encoders.
IMAGES = r"""
#include <stdio.h>
#include <stdlib.h>
#include <vector>
template <class Encode>
static void run(const char *path, unsigned s, Encode encode) {
   FILE *out = fopen(path, "wb");
   for (int t = 0; t < 400; t++) {
      static const int qs[] = {1, 10, 30, 50, 75, 90, 91, 95, 100};
      s = s*1103515245u + 12345u; int w = 1 + (s >> 8) % 97;
      s = s*1103515245u + 12345u; int h = 1 + (s >> 8) % 83;
      s = s*1103515245u + 12345u; int c = 1 + (s >> 8) % 4;
      s = s*1103515245u + 12345u; int q = qs[(s >> 8) % 9];
      s = s*1103515245u + 12345u; int kind = (int) ((s >> 8) % 3);
      std::vector<unsigned char> d((size_t) w*h*c);
      for (int i = 0; i < w*h*c; i++) {
         s = s*1103515245u + 12345u;
         d[i] = kind == 0 ? (unsigned char) (s >> 16)
              : kind == 1 ? (unsigned char) ((i/c%w)*3 + (i/c/w)*2 + (i%c)*40 + ((s >> 16) & 7))
              : ((i/c/w/8 + i/c%w/8) & 1 ? 230 : 20);
      }
      encode(out, d.data(), w, h, c, q);
   }
   fclose(out);
}
"""

UPSTREAM = r"""
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include HDR
""" + IMAGES + r"""
static void sink(void *c, void *d, int n) { fwrite(d, 1, (size_t) n, (FILE *) c); }
int main(int, char **argv) {
   run(argv[1], (unsigned) atoi(argv[2]), [](FILE *out, unsigned char *d, int w, int h, int c, int q) {
      stbi_write_jpg_to_func(sink, out, w, h, c, d, q);
   });
}
"""

OURS = r"""
#include "codec/codec.hpp"
""" + IMAGES + r"""
int main(int, char **argv) {
   run(argv[1], (unsigned) atoi(argv[2]), [](FILE *out, unsigned char *d, int w, int h, int c, int q) {
      const auto bytes = stb::codec::encode_jpeg({d, w, h, c}, q);
      fwrite(bytes.data(), 1, bytes.size(), out);
   });
}
"""


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    upstream = os.path.abspath(sys.argv[1])
    cc = sys.argv[2] if len(sys.argv) > 2 else "c++"
    flags = sys.argv[3:] or ["-O2", "-fno-trapping-math"]
    seed = random.randint(1, 10**6)
    with tempfile.TemporaryDirectory() as tmp:
        results = {}
        for name, code, extra in (("upstream", UPSTREAM, [f'-DHDR="{upstream}"']),
                                  ("libstb", OURS, [os.path.join(ROOT, "src", "codec", "jpeg.cpp"),
                                                    "-I", os.path.join(ROOT, "src")])):
            src, exe, out = (os.path.join(tmp, name + ext) for ext in (".cpp", "", ".bin"))
            open(src, "w").write(code)
            subprocess.run([cc, "-std=c++17", *flags, *extra, src, "-o", exe], check=True)
            subprocess.run([exe, out, str(seed)], check=True)
            results[name] = open(out, "rb").read()
    same = results["upstream"] == results["libstb"]
    print(f"400 random images (seed {seed}, flags {' '.join(flags)}): "
          f"{'byte-identical' if same else 'DIFFERENT bytes'} ({len(results['upstream'])} bytes compared)")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
