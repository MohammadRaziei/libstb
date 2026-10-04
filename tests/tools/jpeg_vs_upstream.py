"""Development check, NOT a test: does the optimized JPEG encoder in
src/third_party/stb/stb_image_write.h write the same bytes as an upstream copy?

JPEG is lossy and its bytes are not a stable contract: two correct builds may differ in rare
rounding ties (a compiler that fuses a*b+c, as Apple clang does on ARM64 or any x86 build with
-mfma, changes the output of the UNMODIFIED upstream encoder too). So this is only meaningful
when both headers are compiled with the SAME compiler and flags, which is why it lives here and
not in the test suite. Use it after touching the JPEG code; the tests check JPEG quality with
tolerances (tests/python/test_lossy_tolerance.py).

    python tests/tools/jpeg_vs_upstream.py /path/to/upstream/stb_image_write.h [cc] [extra flags]
"""
import os
import random
import subprocess
import sys
import tempfile

HARNESS = r"""
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include HDR
#include <stdio.h>
static FILE *out;
static void sink(void *c, void *d, int n) { (void) c; fwrite(d, 1, (size_t) n, out); }
int main(int argc, char **argv) {
   unsigned s = (unsigned) atoi(argv[2]); int t;
   out = fopen(argv[1], "wb");
   for (t = 0; t < 400; t++) {
      static const int qs[] = {1, 10, 30, 50, 75, 90, 91, 95, 100};
      int w, h, c, q, kind, i; unsigned char *d;
      s = s*1103515245u + 12345u; w = 1 + (s >> 8) % 97;
      s = s*1103515245u + 12345u; h = 1 + (s >> 8) % 83;
      s = s*1103515245u + 12345u; c = 1 + (s >> 8) % 4;
      s = s*1103515245u + 12345u; q = qs[(s >> 8) % 9];
      s = s*1103515245u + 12345u; kind = (int) ((s >> 8) % 3);
      d = (unsigned char *) malloc((size_t) w*h*c);
      for (i = 0; i < w*h*c; i++) {
         s = s*1103515245u + 12345u;
         d[i] = kind == 0 ? (unsigned char) (s >> 16)
              : kind == 1 ? (unsigned char) ((i/c%w)*3 + (i/c/w)*2 + (i%c)*40 + ((s >> 16) & 7))
              : ((i/c/w/8 + i/c%w/8) & 1 ? 230 : 20);
      }
      stbi_flip_vertically_on_write(t % 5 == 0);
      stbi_write_jpg_to_func(sink, NULL, w, h, c, d, q);
      free(d);
   }
   fclose(out);
   return 0;
}
"""


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    upstream = os.path.abspath(sys.argv[1])
    cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
    flags = sys.argv[3:] or ["-O2", "-fno-trapping-math"]
    ours = os.path.join(os.path.dirname(__file__), "..", "..", "src", "third_party", "stb", "stb_image_write.h")
    seed = random.randint(1, 10**6)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "h.c")
        open(src, "w").write(HARNESS)
        results = {}
        for name, hdr in (("upstream", upstream), ("libstb", os.path.abspath(ours))):
            exe, out = os.path.join(tmp, name), os.path.join(tmp, name + ".bin")
            subprocess.run([cc, "-std=gnu99", *flags, f'-DHDR="{hdr}"', src, "-o", exe], check=True)
            subprocess.run([exe, out, str(seed)], check=True)
            results[name] = open(out, "rb").read()
    same = results["upstream"] == results["libstb"]
    print(f"400 random images (seed {seed}, flags {' '.join(flags)}): "
          f"{'byte-identical' if same else 'DIFFERENT bytes'} ({len(results['upstream'])} bytes compared)")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
