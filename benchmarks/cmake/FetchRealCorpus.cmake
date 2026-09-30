# FetchRealCorpus.cmake
#
# Downloads a small real-world corpus via plain file(DOWNLOAD), no
# FetchContent needed since we just want the raw files, not to build
# anything from them:
#
#   - a handful of real photographs (the sample images that ship with
#     OpenCV: github.com/opencv/opencv/tree/4.x/samples/data), and
#   - one real TrueType font (DejaVu Sans, from matplotlib's copy),
#     for the font benchmark.
#
# Always lands in benchmarks/corpus/ in the SOURCE tree (persistent
# across clean `build/` wipes, so nothing is re-downloaded every time),
# with a .gitignore containing `*` written into it, so the downloaded
# files are never committed, only the folder itself.
# This runs at CONFIGURE time (this file is include()'d directly by the
# top-level CMakeLists.txt, before add_subdirectory()), so
# `cmake -S . -B build` alone is enough to have the corpus ready.
#
# Every download is best-effort: a failure is a warning, never a build
# error. The suite degrades gracefully to the synthetic corpus (and,
# without the font, skips the font benchmark).
#
# Defines, for the caller:
#   LIBSTB_REAL_CORPUS_DIR  the folder holding the real images ("" if none)
#   LIBSTB_REAL_FONT        path of the downloaded .ttf ("" if unavailable)

set(LIBSTB_BENCH_REAL_IMAGES_BASE
    "https://raw.githubusercontent.com/opencv/opencv/4.x/samples/data"
    CACHE STRING "Base URL the real sample photographs are downloaded from")
set(LIBSTB_BENCH_REAL_IMAGES lena.jpg fruits.jpg baboon.jpg messi5.jpg butterfly.jpg home.jpg
    CACHE STRING "File names (under the base URL) of the real photographs to download")
set(LIBSTB_BENCH_FONT_URL
    "https://raw.githubusercontent.com/matplotlib/matplotlib/main/lib/matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf"
    CACHE STRING "A real TrueType font for the font benchmark (set to an empty string to skip it)")

set(_libstb_corpus_dir "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
file(MAKE_DIRECTORY "${_libstb_corpus_dir}")
file(WRITE "${_libstb_corpus_dir}/.gitignore" "*\n!.gitignore\n")

function(_libstb_fetch url dest)
  if(EXISTS "${dest}")
    return()
  endif()
  message(STATUS "libstb benchmarks: downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}" STATUS _st TIMEOUT 60)
  list(GET _st 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _st 1 _msg)
    message(WARNING "libstb benchmarks: could not download ${url} (${_msg}); continuing without it.")
    file(REMOVE "${dest}")
  endif()
endfunction()

set(LIBSTB_REAL_CORPUS_DIR "")
set(_libstb_real_count 0)
foreach(_name ${LIBSTB_BENCH_REAL_IMAGES})
  _libstb_fetch("${LIBSTB_BENCH_REAL_IMAGES_BASE}/${_name}" "${_libstb_corpus_dir}/${_name}")
  if(EXISTS "${_libstb_corpus_dir}/${_name}")
    math(EXPR _libstb_real_count "${_libstb_real_count} + 1")
  endif()
endforeach()
if(_libstb_real_count GREATER 0)
  set(LIBSTB_REAL_CORPUS_DIR "${_libstb_corpus_dir}")
  message(STATUS "libstb benchmarks: real corpus has ${_libstb_real_count} photographs in ${LIBSTB_REAL_CORPUS_DIR}")
else()
  message(WARNING "libstb benchmarks: no real photographs available; running on the synthetic corpus only.")
endif()

set(LIBSTB_REAL_FONT "")
if(LIBSTB_BENCH_FONT_URL)
  _libstb_fetch("${LIBSTB_BENCH_FONT_URL}" "${_libstb_corpus_dir}/DejaVuSans.ttf")
  if(EXISTS "${_libstb_corpus_dir}/DejaVuSans.ttf")
    set(LIBSTB_REAL_FONT "${_libstb_corpus_dir}/DejaVuSans.ttf")
  endif()
endif()
