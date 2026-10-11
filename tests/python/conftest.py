"""Turn pytest-memray on for every run when it is installed.

`--memray` is not in addopts on purpose: pytest refuses an unknown option, and the plugin is
not installable everywhere. Whether it is installed is decided by the environment marker on
`pytest-memray` in the `test` extra of pyproject.toml (platforms where memray has no wheel are
left out); here it is switched on only if the plugin can be imported, so `pytest tests/python` checks the limit_leaks and limit_memory
markers of test_memory.py wherever it can, and behaves as before where it cannot.
Set LIBSTB_NO_MEMRAY=1 to run without it (for example under an emulator).
"""

import os

import pytest


@pytest.hookimpl(tryfirst=True)
def pytest_configure(config):
    if os.environ.get("LIBSTB_NO_MEMRAY") or not hasattr(config.option, "memray"):
        return
    try:
        import pytest_memray  # noqa: F401
    except Exception:  # not installed, or its native part does not load here: just skip it
        return
    config.option.memray = True
