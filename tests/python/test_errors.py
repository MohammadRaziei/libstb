import pytest

import libstb


def test_python_hierarchy_mirrors_cpp():
    assert issubclass(libstb.Error, RuntimeError)
    for sub in (libstb.DecodeError, libstb.EncodeError, libstb.LimitError):
        assert issubclass(sub, libstb.Error)
    # siblings, not parent/child of each other
    assert not issubclass(libstb.DecodeError, libstb.LimitError)


def test_one_except_clause_catches_every_libstb_failure():
    for bad in (b"junk", b""):
        with pytest.raises(libstb.Error):
            libstb.load(bad)


def test_bad_arguments_are_value_error_not_libstb_error():
    from test_image import RGB

    with pytest.raises(ValueError) as e:
        libstb.load(RGB, channels=9)
    assert not isinstance(e.value, libstb.Error)
