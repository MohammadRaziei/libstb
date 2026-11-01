"""The command line: python -m libstb / libstb."""

import json
import subprocess
import sys

import pytest

import libstb as stb
from libstb.__main__ import main


@pytest.fixture()
def ppm(tmp_path):
    """A 4x2 RGB image (binary PPM), so no numpy is needed."""
    path = tmp_path / "in.ppm"
    path.write_bytes(b"P6\n4 2\n255\n" + bytes(range(24)))
    return path


def test_version_flag(capsys):
    with pytest.raises(SystemExit) as e:
        main(["--version"])
    assert e.value.code == 0
    assert capsys.readouterr().out.strip() == f"libstb {stb.__version__}"


def test_paths(capsys):
    assert main(["--cmake-dir", "--include-dir"]) == 0
    assert capsys.readouterr().out.split() == [stb.get_cmake_dir(), stb.get_include_dir()]


def test_no_command_is_a_usage_error(capsys):
    assert main([]) == 2


def test_info(ppm, capsys):
    assert main(["info", "--json", str(ppm)]) == 0
    got = json.loads(capsys.readouterr().out)
    assert (got["width"], got["height"], got["channels"], got["pixel_format"]) == (4, 2, 3, "rgb")


def test_info_keeps_going_after_a_bad_file(ppm, tmp_path, capsys):
    assert main(["info", str(tmp_path / "missing.png"), str(ppm)]) == 1
    cap = capsys.readouterr()
    assert "missing.png" in cap.err and "4x2" in cap.out


def test_convert_resize_keeps_aspect_ratio(ppm, tmp_path):
    out = tmp_path / "out.png"
    assert main(["convert", str(ppm), str(out), "--resize", "8x", "--pixel-format", "gray"]) == 0
    info = stb.iminfo(out)
    assert (info.width, info.height, info.channels) == (8, 4, 1)


def test_convert_rejects_an_option_the_format_lacks(ppm, tmp_path, capsys):
    assert main(["convert", str(ppm), str(tmp_path / "out.png"), "--quality", "50"]) == 1
    assert "quality" in capsys.readouterr().err


def test_zlib_round_trip(tmp_path):
    src, packed, back = tmp_path / "a", tmp_path / "a.z", tmp_path / "b"
    src.write_bytes(b"hello hello hello " * 100)
    assert main(["zlib", "compress", str(src), str(packed), "-l", "9"]) == 0
    assert main(["zlib", "decompress", str(packed), str(back)]) == 0
    assert back.read_bytes() == src.read_bytes()


def test_python_dash_m():
    out = subprocess.run([sys.executable, "-m", "libstb", "--version"], capture_output=True, text=True)
    assert out.returncode == 0 and out.stdout.strip() == f"libstb {stb.__version__}"
