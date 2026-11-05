"""libstb's log records go to Python's `logging` (the bridge installed by `import libstb`)."""

import logging
import subprocess
import sys

import pytest

import libstb as stb
from libstb import libstb_py


@pytest.fixture(autouse=True)
def bridge_on():
    stb.logging_bridge(True)
    yield
    stb.logging_bridge(True)


def test_a_record_reaches_python_logging_with_its_name_level_and_message(caplog):
    with caplog.at_level(logging.DEBUG, logger="libstb"):
        libstb_py._log("libstb.simd", 30, "backend x is not available")
    [rec] = [r for r in caplog.records if r.name.startswith("libstb")]
    assert (rec.name, rec.levelno, rec.getMessage()) == ("libstb.simd", logging.WARNING, "backend x is not available")


@pytest.mark.parametrize("number", [10, 20, 30, 40, 50, 25])
def test_levels_keep_their_numbers(caplog, number):
    with caplog.at_level(1, logger="libstb"):
        libstb_py._log("libstb", number, "m")
    assert [r.levelno for r in caplog.records if r.name == "libstb"] == [number]


def test_python_decides_what_is_shown(caplog):
    with caplog.at_level(logging.WARNING, logger="libstb"):
        libstb_py._log("libstb", 10, "debug line")
        libstb_py._log("libstb", 30, "warning line")
    assert [r.getMessage() for r in caplog.records if r.name == "libstb"] == ["warning line"]


def test_the_dotted_names_form_the_python_hierarchy(caplog):
    with caplog.at_level(logging.INFO, logger="libstb"):
        libstb_py._log("libstb.threads", 20, "from the child")
    assert logging.getLogger("libstb.threads").parent is logging.getLogger("libstb")
    assert any(r.name == "libstb.threads" for r in caplog.records)


def test_a_percent_sign_in_a_message_is_not_a_format_string(caplog):
    with caplog.at_level(logging.INFO, logger="libstb"):
        libstb_py._log("libstb", 20, "100% done %s %(x)d")
    assert [r.getMessage() for r in caplog.records if r.name == "libstb"] == ["100% done %s %(x)d"]


def test_the_bridge_can_be_switched_off_and_on(caplog):
    with caplog.at_level(logging.DEBUG, logger="libstb"):
        stb.logging_bridge(False)
        libstb_py._log("libstb", 40, "off")
        stb.logging_bridge(False)  # twice is harmless
        stb.logging_bridge(True)
        stb.logging_bridge(True)
        libstb_py._log("libstb", 40, "on")
    assert [r.getMessage() for r in caplog.records if r.name == "libstb"] == ["on"]  # once, not twice


def test_a_raising_python_handler_does_not_break_the_call():
    class Boom(logging.Handler):
        def emit(self, record):
            raise RuntimeError("boom")

    h = Boom()
    lg = logging.getLogger("libstb")
    lg.addHandler(h)
    old = logging.raiseExceptions
    logging.raiseExceptions = False
    try:
        libstb_py._log("libstb", 40, "x")  # must not raise
    finally:
        logging.raiseExceptions = old
        lg.removeHandler(h)


def run(code, **env):
    import os

    return subprocess.run([sys.executable, "-c", code], capture_output=True, text=True,
                          env={**os.environ, **env})


def test_the_env_warning_goes_through_python_logging():
    out = run(
        "import logging, libstb\n"
        "logging.basicConfig(format='%(name)s|%(levelname)s|%(message)s')\n"
        "libstb.simd_name()",
        LIBSTB_SIMD="no-such-backend")
    assert out.returncode == 0
    assert "libstb.simd|WARNING|LIBSTB_SIMD=no-such-backend is not an available SIMD backend" in out.stderr


def test_without_a_python_handler_a_warning_still_shows_up():
    out = run("import libstb\nlibstb.thread_count()", LIBSTB_THREADS="bogus")
    assert "LIBSTB_THREADS=bogus" in out.stderr  # Python's last resort handler


def test_libstb_log_is_kept_when_set():
    out = run(
        "import logging, libstb\n"
        "from libstb import libstb_py\n"
        "logging.basicConfig(level=logging.DEBUG, format='%(message)s')\n"
        "libstb_py._log('libstb', 20, 'info line')\n"
        "libstb_py._log('libstb', 40, 'error line')",
        LIBSTB_LOG="error")
    assert "error line" in out.stderr and "info line" not in out.stderr
