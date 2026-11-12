import React from 'react';

export type Snippet = { language: string; title?: string; code: string };

export type Start = {
  /** One block per way to get it. */
  install: Snippet[];
  installNote: React.ReactNode;
  example: Snippet;
};

/**
 * "Install and use", per language id of languages.json. A language that has no entry here gets no tab on the home page.
 * (The lessons take their code from docs/snippets; this is the short tour of the home page.)
 */
export const START: Record<string, Start> = {
  python: {
    install: [{ language: 'bash', title: 'bash', code: 'pip install libstb' }],
    installNote: (
      <>
        Python 3.9+ and nothing else: libstb has no runtime dependencies. It shares memory with numpy and with DLPack consumers such as torch, and
        converts to and from Pillow, so there is nothing to translate by hand.
      </>
    ),
    example: {
      language: 'python',
      title: 'python',
      code: `import libstb as stb

img = stb.Image.open("photo.jpg", orient=True)      # apply the EXIF orientation
small = img.resize(320, 240, "cubic")               # filter by name
gray = small.convert(1)                             # editing returns a new Image
gray.write("photo_gray.png")                        # format from the extension

pixels = stb.imread("photo.png")                    # ndarray (needs numpy)
stb.imwrite("out.jpg", pixels, quality=80)

stb.Font.open("font.ttf").render("Hi", 32).bitmap.write("hi.png")`,
    },
  },
  cpp: {
    install: [
      {
        language: 'cmake',
        title: 'CMake, FetchContent',
        code: `# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
  libstb
  GIT_REPOSITORY https://github.com/MohammadRaziei/libstb.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(libstb)

target_link_libraries(my_app PRIVATE libstb::libstb)`,
      },
      {
        language: 'cmake',
        title: 'CMake, pip (prebuilt)',
        code: `# CMakeLists.txt
execute_process(COMMAND libstb --cmake-dir
                OUTPUT_VARIABLE libstb_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
find_package(libstb CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE libstb::libstb)`,
      },
    ],
    installNote: (
      <>
        CMake 3.21+ and a C++17 compiler. Include <code>&lt;stb.h&gt;</code> and link <code>libstb::libstb</code> (a shared library; the stb headers
        are never exposed). Also available through <code>find_package(libstb CONFIG)</code> after <code>cmake --install</code>.
      </>
    ),
    example: {
      language: 'cpp',
      title: 'c++',
      code: `#include <stb.h>

int main() {
    stb::image img = stb::image::open("in.png");        // throws stb::error subclasses

    stb::image small = img.resize(320, 240, "cubic");   // filter by name
    stb::image part  = img.crop(10, 10, 200, 100);      // editing returns a new image
    stb::image turned = img.rotate90();                 // counter-clockwise
    stb::image gray  = img.convert(1);                  // 1..4 channels

    small.write("small.jpg");                           // format from the extension
    stb::font f = stb::font::open("font.ttf");
    stb::text_bitmap t = f.render("hello", 32);         // t.bitmap is a 1-channel image
    t.bitmap.write("hello.png");
}`,
    },
  },
};

export const FONTS_CODE: Snippet = {
  language: 'python',
  title: 'python',
  code: `font = stb.Font.open("font.ttf")
font.render("Hello", 32).bitmap.write("hello.png")

# the size of a text, with no font file
size = stb.estimate_text("Hello", 32)
print(size.width, size.height)`,
};

export const ZLIB_CODE: Snippet = {
  language: 'python',
  title: 'python',
  code: `packed = stb.zlib_compress(data, 6)

# raises LimitError past max_bytes
data = stb.zlib_decompress(
    packed, max_bytes=64 << 20)`,
};

export const COPY_CODE: Snippet = {
  language: 'python',
  title: 'python',
  code: `import numpy as np
import libstb as stb

img = stb.Image.open("photo.png")
a = np.asarray(img)          # no copy
t = torch.from_dlpack(img)   # no copy
pil = img.to_pil()           # one copy`,
};

export const TOUR = [
  { name: 'open', scale: 1, size: 160, code: 'open', alt: 'The photograph at 160 by 160 pixels' },
  { name: 'crop', scale: 0.6, size: 96, code: 'crop', alt: 'The middle of the photograph, cropped to 96 by 96 pixels' },
  { name: 'rotate', scale: 1, size: 160, code: 'rotate90', alt: 'The photograph turned a quarter turn counter-clockwise' },
  { name: 'gray', scale: 1, size: 160, code: 'convert(1)', alt: 'The photograph converted to one grey channel' },
  { name: 'flip', scale: 1, size: 160, code: 'flip_horizontal', alt: 'The photograph mirrored left to right' },
];

export const REFERENCES = [
  {
    id: 'python',
    title: 'Python API',
    badge: 'Python 3.9+',
    description: (
      <>
        nanobind extension module. Documents <code>Image</code> (open, resize, edit, write), <code>imread</code> / <code>imwrite</code> /{' '}
        <code>imresize</code>, <code>Resizer</code>, <code>Font</code> with text rendering and atlases, EXIF orientation, SIMD backend selection and
        the error classes. Ships pre-built wheels for Linux, macOS and Windows.
      </>
    ),
    meta: 'Generated with Sphinx',
    href: 'python/html/index.html',
    link: 'Open Python Reference',
  },
  {
    id: 'cpp',
    title: 'C++ API',
    badge: 'C++17',
    description: (
      <>
        The core library. Covers <code>stb::image</code> (decode, encode, edit), <code>stb::resizer</code> and its options, <code>stb::font</code> with
        text bitmaps and atlases, <code>stb::zlib_compress</code> and <code>zlib_decompress</code>, EXIF orientation, SIMD backend selection and the{' '}
        <code>stb::error</code> hierarchy.
      </>
    ),
    meta: 'Generated with Doxygen',
    href: 'cpp/html/index.html',
    link: 'Open C++ Reference',
  },
];
