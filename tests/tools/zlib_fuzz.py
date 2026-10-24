"""Fuzz libstb's deflate (src/codec/deflate.cpp) directly.

The PNG tests reach zlib_deflate through the PNG filters, which shape the data it
sees. This script calls it on raw bytes instead (every quality 0..10, sizes around the
64K / 32K block and window edges, noise, runs, periodic data) and checks the result with
Python's own zlib. Needs a C++ compiler on PATH; not part of the ctest run.

    python tests/tools/zlib_fuzz.py [c++] [seed]
"""
import os
import random
import subprocess
import sys
import tempfile
import zlib

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "codec/codec.hpp"
int main(int, char **argv) {
   FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
   std::vector<unsigned char> d((size_t) n);
   if (n && fread(d.data(), 1, (size_t) n, f) != (size_t) n) return 2;
   fclose(f);
   const auto o = stb::codec::zlib_deflate(d.data(), d.size(), atoi(argv[3]));
   f = fopen(argv[2], "wb"); fwrite(o.data(), 1, o.size(), f); fclose(f);
   return 0;
}
"""


def main():
    cc = sys.argv[1] if len(sys.argv) > 1 else "c++"
    random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    src_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "src"))
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, "h.cpp"), os.path.join(tmp, "h")
        open(src, "w").write(HARNESS)
        subprocess.run([cc, "-O2", "-std=c++17", "-I", src_dir, src, os.path.join(src_dir, "codec", "deflate.cpp"),
                        "-o", exe], check=True)

        def check(data, q):
            inp, out = os.path.join(tmp, "in"), os.path.join(tmp, "out")
            open(inp, "wb").write(data)
            subprocess.run([exe, inp, out, str(q)], check=True)
            assert zlib.decompress(open(out, "rb").read()) == data, ("MISMATCH", len(data), q)

        sizes = [0, 1, 2, 3, 4, 5, 8, 9, 100, 257, 258, 259, 1000, 32767, 32768, 32769, 65535, 65536, 65537, 140000]
        for _ in range(500):
            n = random.choice(sizes) + random.choice([0, 0, random.randint(0, 50000)])
            kind = random.randint(0, 4)
            if kind == 0:
                data = os.urandom(n)
            elif kind == 1:
                data = bytes(random.choice(b"abcd") for _ in range(n))
            elif kind == 2:
                data = (os.urandom(random.randint(1, 300)) * (n // 10 + 1))[:n]
            elif kind == 3:
                step = random.randint(1, 50)
                data = bytes((i // step) & 255 for i in range(n))
            else:  # a repeat exactly one window (32768 bytes) apart: the longest legal distance
                block = os.urandom(32768)
                data = (block * (n // 32768 + 2))[:max(n, 32769)]
            check(data, random.choice([0, 1, 2, 4, 6, 8, 10]))
        print("zlib_fuzz: 500 cases ok")


if __name__ == "__main__":
    main()
