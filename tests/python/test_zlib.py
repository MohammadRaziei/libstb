"""libstb.zlib_compress / zlib_decompress: interoperate with python's zlib, and stay bounded."""

import array
import random
import threading
import zlib

import pytest

import libstb as stb

MiB = 1024 * 1024


def corpora():
    rng = random.Random(1234)
    text = (b"The quick brown fox jumps over the lazy dog. " * 40 + bytes(rng.randrange(256) for _ in range(300))) * 20
    noise = bytes(rng.randrange(256) for _ in range(50_000))
    runs = b"".join(bytes([i % 7]) * rng.randrange(1, 300) for i in range(400))
    return {"empty": b"", "one": b"x", "text": text, "noise": noise, "runs": runs, "zeros": bytes(300_000),
            "all-bytes": bytes(range(256)) * 300}


DATA = corpora()


@pytest.mark.parametrize("name", DATA)
@pytest.mark.parametrize("level", [-1, 0, 1, 3, 6, 8, 9])
def test_what_libstb_compresses_zlib_can_read(name, level):
    data = DATA[name]
    assert zlib.decompress(stb.zlib_compress(data, level)) == data


@pytest.mark.parametrize("name", DATA)
@pytest.mark.parametrize("zlevel", [0, 1, 6, 9])
def test_what_zlib_compresses_libstb_can_read(name, zlevel):
    data = DATA[name]
    assert stb.zlib_decompress(zlib.compress(data, zlevel)) == data


@pytest.mark.parametrize("strategy", [zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED])
def test_every_deflate_block_type_zlib_can_make_is_read(strategy):
    """Stored, fixed-Huffman and dynamic-Huffman blocks all appear across these strategies."""
    for name in ("text", "noise", "runs", "zeros"):
        c = zlib.compressobj(6, zlib.DEFLATED, 15, 8, strategy)
        packed = c.compress(DATA[name]) + c.flush()
        assert stb.zlib_decompress(packed) == DATA[name]


def test_random_round_trips():
    rng = random.Random(99)
    for _ in range(150):
        n = rng.choice([0, 1, 2, 3, 100, 1000, 40_000])
        alphabet = rng.choice([2, 4, 16, 256])
        data = bytes(rng.randrange(alphabet) for _ in range(n))
        level = rng.randrange(-1, 10)
        packed = stb.zlib_compress(data, level)
        assert stb.zlib_decompress(packed) == data
        assert zlib.decompress(packed) == data


def test_output_is_deterministic_and_levels_trade_size():
    d = DATA["text"]
    assert stb.zlib_compress(d) == stb.zlib_compress(d)
    assert len(stb.zlib_compress(d, 9)) <= len(stb.zlib_compress(d, 1)) < len(d)
    assert stb.zlib_compress(d, -1) == stb.zlib_compress(d, 6)
    assert stb.zlib_compress(d) == stb.zlib_compress(d, stb.DEFAULT_ZLIB_LEVEL)


@pytest.mark.parametrize("n", [0, 1, 65_535, 65_536, 200_000])
def test_level_0_stores_in_minimal_framing(n):
    data = bytes(n)  # zeros: any compressor would shrink them
    stored = stb.zlib_compress(data, 0)
    blocks = max(1, -(-n // 65_535))
    assert len(stored) == n + 2 + 5 * blocks + 4
    assert len(stored) <= len(zlib.compress(data, 0))     # zlib's own level 0 splits blocks a bit smaller
    assert zlib.decompress(stored) == data and stb.zlib_decompress(stored) == data


def test_accepts_any_bytes_like_object():
    data = DATA["text"]
    want = stb.zlib_compress(data)
    for obj in (bytearray(data), memoryview(data), array.array("B", data)):
        assert stb.zlib_compress(obj) == want
    assert stb.zlib_decompress(bytearray(want)) == data
    assert stb.zlib_decompress(memoryview(want)) == data
    np = pytest.importorskip("numpy")
    assert stb.zlib_compress(np.frombuffer(data, np.uint8)) == want
    with pytest.raises(TypeError):
        stb.zlib_compress("a str is not bytes-like")
    with pytest.raises((BufferError, TypeError)):
        stb.zlib_compress(memoryview(data)[::2])  # not contiguous


def test_argument_errors():
    for bad in (-2, 10, 100):
        with pytest.raises(ValueError):
            stb.zlib_compress(b"abc", bad)
    with pytest.raises((OverflowError, TypeError)):  # a size cannot be negative
        stb.zlib_decompress(zlib.compress(b"abc"), -1)


def test_corrupt_input_is_a_decode_error():
    good = zlib.compress(DATA["text"])
    bad_inputs = {
        "empty": b"",
        "not zlib": b"definitely not a zlib stream",
        "bad header": b"\x79" + good[1:],
        "truncated": good[: len(good) // 2],
        "truncated trailer": good[:-2],
        "adler off": good[:-1] + bytes([good[-1] ^ 1]),
        "trailing byte": good + b"\0",
        "damaged middle": good[: len(good) // 2] + bytes([good[len(good) // 2] ^ 0xFF]) + good[len(good) // 2 + 1:],
        "gzip, not zlib": zlib.compress(b"abc")[:0] + __import__("gzip").compress(b"abc"),
        "raw deflate": zlib.compressobj(6, zlib.DEFLATED, -15).compress(b"abc"),
    }
    for name, blob in bad_inputs.items():
        with pytest.raises(stb.DecodeError):
            stb.zlib_decompress(blob)
        assert issubclass(stb.DecodeError, stb.Error), name


def test_max_bytes_is_exact():
    for n in (1 * MiB, 1_000_003, 65_536, 65_537):
        packed = zlib.compress(bytes(n))
        assert len(stb.zlib_decompress(packed, n)) == n
        assert len(stb.zlib_decompress(packed, n + 1)) == n
        with pytest.raises(stb.LimitError):
            stb.zlib_decompress(packed, n - 1)
    assert stb.zlib_decompress(zlib.compress(b""), 0) == b""
    with pytest.raises(stb.LimitError):
        stb.zlib_decompress(zlib.compress(b"x"), 0)


def test_a_zip_bomb_is_a_limit_error_not_a_decode_error():
    bomb = zlib.compress(bytes(64 * MiB), 9)          # 64 MiB of zeros in about 64 KB
    assert len(bomb) < 100_000
    with pytest.raises(stb.LimitError) as e:
        stb.zlib_decompress(bomb, 8 * MiB)
    assert type(e.value) is stb.LimitError


def test_the_default_limit_stops_a_bomb_past_512_mib():
    bomb = zlib.compress(bytes(600 * MiB), 9)         # ~600 KB that declares 600 MiB
    assert len(bomb) < 2 * MiB and stb.DEFAULT_ZLIB_MAX_BYTES == 512 * MiB
    with pytest.raises(stb.LimitError):
        stb.zlib_decompress(bomb)


def test_it_runs_without_the_gil():
    data = DATA["text"] * 50
    results = []

    def work():
        results.append(stb.zlib_decompress(stb.zlib_compress(data)) == data)

    threads = [threading.Thread(target=work) for _ in range(6)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert results == [True] * 6
