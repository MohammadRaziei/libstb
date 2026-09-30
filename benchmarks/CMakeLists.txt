cmake_minimum_required(VERSION 3.18)
project(LibstbBenchmarks LANGUAGES NONE)

# This whole directory is self-contained and independent of the parent
# libstb source tree: it installs libstb itself from GitHub
# (pip install git+https://...), not from ../src, so it can be copied
# out and run on its own. Same convention as
# https://github.com/MohammadRaziei/pygixml/tree/main/benchmarks and
# https://github.com/MohammadRaziei/ctoon/tree/main/benchmarks.
#
#   cmake -S . -B build
#   cmake --build build --target libstb_benchmark
#
# See README.md in this directory for the full rundown, and every
# individual target below for what it does on its own
# (`cmake --build build --target help` lists them all). Nothing here
# writes outside of results/ (and the gitignored corpus/ cache).
#
# To benchmark a local, not-yet-published patch instead of whatever is
# on GitHub, override this at configure time rather than editing it
# here, e.g.:
#   cmake -S . -B build -DLIBSTB_BENCH_GITHUB_URL=/path/to/local/checkout
set(LIBSTB_BENCH_GITHUB_URL
    "git+https://github.com/MohammadRaziei/libstb.git"
    CACHE STRING "Where to install libstb itself from (a fresh git checkout, not the local source tree)")

set(LIBSTB_BENCH_RESULTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/results"
    CACHE PATH "Where the final standalone report.html is written: the one artifact meant to be committed")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchRealCorpus.cmake")

add_subdirectory(python)
add_subdirectory(report)

add_custom_target(libstb_benchmark
  COMMENT "libstb_benchmark: running the full benchmark suite and building the report"
)
add_dependencies(libstb_benchmark libstb_bench_report)
