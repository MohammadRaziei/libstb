"""Build and run every example in examples/<topic>/ and compare what it does with what its docs page claims (docs/adr/0004).

    pytest tests/python/test_examples.py

A topic is examples/<topic>/<lang>/<file>.<ext>: `main` is the example of its page. Each example is run in an empty temporary folder that holds
only `astronaut.png`, as its README says, so what a reader copies is what is tested. The Python and the C++ example must write the same files, and
those files must be the pictures that the page shows (docs/site/static/img/<topic>/). When the library changes a picture, run the example again
and copy `out/*.png` there.

The C++ side is built with the CMake recipe of docs/site/docs/install.mdx (the first cmake block, read from the page itself, so a broken recipe fails
here too). It needs CMake and the installed package or build tree (`python -m libstb --cmake-dir`), and is skipped when CMake is not there.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import libstb as stb

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "examples"
SITE = REPO / "docs" / "site"
DATA = REPO / "data" / "images" / "astronaut.png"

# topic -> the files its example writes, with the size each must have (width, height)
RESIZE = {"tiny.png": (64, 64), **{f"{n}.png": (256, 256) for n in ("nearest", "linear", "cubic", "bspline", "mitchell")}}
CHECKS = {"resize": RESIZE}


def topics():
    return sorted(p.name for p in EXAMPLES.iterdir() if (p / "python" / "main.py").is_file() and (p / "cpp" / "main.cpp").is_file())


def run_python(topic, work):
    shutil.copy(DATA, work / "astronaut.png")
    subprocess.run([sys.executable, str(EXAMPLES / topic / "python" / "main.py")], cwd=work, check=True, timeout=120)
    return work / "out"


def recipe():
    """The first cmake block of the install page, plus the copy of the DLLs that Windows needs (the page says it in text)."""
    text = (SITE / "docs" / "install.mdx").read_text(encoding="utf-8")
    block = re.search(r"```cmake\n(.*?)```", text, re.S).group(1)
    assert "find_package(libstb CONFIG REQUIRED)" in block
    return block + (
        "\nif(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)\n"
        "    add_custom_command(TARGET app POST_BUILD\n"
        "        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:app> $<TARGET_FILE_DIR:app>\n"
        "        COMMAND_EXPAND_LISTS)\n"
        "endif()\n"
    )


def run_cpp(topic, work):
    src = work / "src"
    src.mkdir()
    (src / "CMakeLists.txt").write_text(recipe(), encoding="utf-8")
    shutil.copy(EXAMPLES / topic / "cpp" / "main.cpp", src / "main.cpp")
    build = work / "build"
    subprocess.run(["cmake", "-S", str(src), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"], check=True, timeout=300)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release"], check=True, timeout=900)
    exe = next(p for p in [build / "app", build / "app.exe", build / "Release" / "app.exe"] if p.is_file())
    run = work / "run"
    run.mkdir()
    shutil.copy(DATA, run / "astronaut.png")
    subprocess.run([str(exe)], cwd=run, check=True, timeout=120)
    return run / "out"


def test_every_example_has_a_check():
    assert topics(), "no example found"
    assert set(topics()) == set(CHECKS), "an example topic and its check in CHECKS must come together"


@pytest.mark.parametrize("topic", sorted(CHECKS))
def test_python_example(topic, tmp_path):
    out = run_python(topic, tmp_path)
    assert sorted(p.name for p in out.iterdir()) == sorted(CHECKS[topic])
    for name, size in CHECKS[topic].items():
        img = stb.Image.open(out / name)
        assert (img.width, img.height) == size, name


@pytest.mark.parametrize("topic", sorted(CHECKS))
def test_the_page_shows_what_the_example_writes(topic, tmp_path):
    out = run_python(topic, tmp_path)
    shown = SITE / "static" / "img" / topic
    for name in CHECKS[topic]:
        assert (shown / name).read_bytes() == (out / name).read_bytes(), (
            f"{name}: the picture on the page is not what the example writes now; run the example and copy out/*.png to {shown}"
        )


@pytest.mark.skipif(shutil.which("cmake") is None, reason="CMake is not installed")
@pytest.mark.parametrize("topic", sorted(CHECKS))
def test_cpp_example_writes_what_python_writes(topic, tmp_path):
    (tmp_path / "py").mkdir()
    (tmp_path / "cpp").mkdir()
    py = run_python(topic, tmp_path / "py")
    cpp = run_cpp(topic, tmp_path / "cpp")
    assert sorted(p.name for p in cpp.iterdir()) == sorted(CHECKS[topic])
    for name in CHECKS[topic]:
        assert (cpp / name).read_bytes() == (py / name).read_bytes(), f"{name}: C++ and Python write different bytes"
