"""Turn pytest-memray on for every run when it is installed.

`--memray` is not in addopts on purpose: pytest refuses an unknown option, and the plugin is
not installable everywhere (Windows, some wheel test environments). Here it is switched on only
if the plugin can be imported, so `pytest tests/python` checks the limit_leaks and limit_memory
markers of test_memory.py wherever it can, and behaves as before where it cannot.
Set LIBSTB_NO_MEMRAY=1 to run without it (for example under an emulator).
"""

import importlib.util
import os

import pytest


@pytest.hookimpl(tryfirst=True)
def pytest_configure(config):
    if os.environ.get("LIBSTB_NO_MEMRAY"):
        return
    if importlib.util.find_spec("pytest_memray") is not None and hasattr(config.option, "memray"):
        config.option.memray = True
