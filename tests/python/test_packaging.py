"""What the installed package carries: one shared libstb library the extension module and C++ users share."""

import ctypes
import pathlib
import shutil
import subprocess
import sys

import pytest

import libstb as stb

PACKAGE = pathlib.Path(stb.__file__).parent
SHARED_NAME = {"win32": "stb.dll", "darwin": "libstb.dylib"}.get(sys.platform, "libstb.so")


def found(name):
    return [p for p in PACKAGE.rglob(name) if p.is_file()]


def test_the_package_carries_the_shared_core_library():
    assert len(found(SHARED_NAME)) == 1, f"expected exactly one {SHARED_NAME} under {PACKAGE}"


def test_the_package_carries_no_static_core_library():
    # A static archive of the core would hold a second copy of the engine (and, built with LTO,
    # a compiler-specific one): the shared library is the only core the package ships.
    assert not found("libstb_core.a") and not found("stb_core.lib")


def test_the_headers_and_cmake_files_for_c_plus_plus_users_are_there():
    assert (pathlib.Path(stb.get_include_dir()) / "stb.h").is_file()
    assert (pathlib.Path(stb.get_include_dir()) / "stb" / "export.hpp").is_file()
    assert (pathlib.Path(stb.get_cmake_dir()) / "libstbConfig.cmake").is_file()


@pytest.mark.skipif(sys.platform == "win32", reason="C++ symbol names are mangled differently by MSVC")
def test_the_shared_library_loads_on_its_own_and_exports_the_public_api():
    lib = ctypes.CDLL(str(found(SHARED_NAME)[0]))
    assert hasattr(lib, "_ZN3stb9simd_nameEv")  # stb::simd_name()
    # What is not part of the headers stays hidden (-fvisibility=hidden): no decoder entry point.
    assert not hasattr(lib, "_ZN3stb5codec6decodeEPKhmib")


@pytest.mark.skipif(not sys.platform.startswith("linux") or not shutil.which("readelf"), reason="needs Linux and readelf")
def test_the_extension_uses_the_dynamic_cpp_runtime_the_shared_core_uses():
    # An extension with its own static libstdc++ next to a libstb.so that uses the dynamic one has two
    # C++ runtimes in the process: an exception thrown inside libstb.so then crashes the caller
    # (this is what nanobind does by default on musllinux: -static-libstdc++).
    def needed(path):
        out = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True).stdout
        return [line for line in out.splitlines() if "NEEDED" in line]

    core = needed(found(SHARED_NAME)[0])
    assert any("libstdc++" in line for line in core)
    extension = next(PACKAGE.glob("libstb_py*.so"))
    assert any("libstdc++" in line for line in needed(extension)), "the extension has a static libstdc++ of its own"
