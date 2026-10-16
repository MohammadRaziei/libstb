cmake_minimum_required(VERSION 3.18)
project(LibstbBenchmarks LANGUAGES NONE)

# This whole directory is self-contained and independent of the parent
# libstb source tree: it installs libstb itself from GitHub
# (pip install git+https://...), not from ../src, so it can be copied
# out and run on its own. Same convention as
# https://github.com/MohammadRaziei/pygixml/tree/main/benchmarks and
# https://github.com/MohammadRaziei/novasvg/tree/master/benchmarks.
#
#   cmake -S . -B build
#   cmake --build build --target stb_benchmark
#
# Two folders, nothing else is ever written:
#
#   build/      disposable: the venv, the corpus, scratch files, CMake's own state. Never committed.
#   outputs/    what is meant to be committed and published:
#                 results/<step>.json   what each step measured, written by python/bench_*.py
#                 index.html            report/generate_report.py: the standalone report, built from results/
#                 benchmark.json        report/gather.py: a small digest of results/ for the docs landing page
#
# See README.md in this directory for the full rundown, and every
# individual target below for what it does on its own
# (`cmake --build build --target help` lists them all).
#
# To benchmark a local, not-yet-published patch instead of whatever is
# on GitHub, override this at configure time rather than editing it
# here, e.g.:
#   cmake -S . -B build -DLIBSTB_BENCH_GITHUB_URL=/path/to/local/checkout
set(LIBSTB_BENCH_GITHUB_URL
    "git+https://github.com/MohammadRaziei/libstb.git"
    CACHE STRING "Where to install libstb itself from (a fresh git checkout, not the local source tree)")

set(LIBSTB_BENCH_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/outputs"
    CACHE PATH "Where results/, benchmark.json and index.html land: what is meant to be committed")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchRealCorpus.cmake")

add_subdirectory(python)
add_subdirectory(report)

add_custom_target(stb_benchmark
  COMMENT "stb_benchmark: running the full benchmark suite and building the report"
)
add_dependencies(stb_benchmark stb_bench_report)
