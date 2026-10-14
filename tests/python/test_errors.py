import pytest

import libstb as stb


def test_python_hierarchy_mirrors_cpp():
    assert issubclass(stb.Error, RuntimeError)
    for sub in (stb.DecodeError, stb.EncodeError, stb.LimitError):
        assert issubclass(sub, stb.Error)
    # siblings, not parent/child of each other
    assert not issubclass(stb.DecodeError, stb.LimitError)


def test_one_except_clause_catches_every_libstb_failure():
    for bad in (b"junk", b""):
        with pytest.raises(stb.Error):
            stb.imread(bad)


def test_bad_arguments_are_value_error_not_libstb_error():
    from test_image import RGB

    with pytest.raises(ValueError) as e:
        stb.imread(RGB, channels=9)
    assert not isinstance(e.value, stb.Error)
