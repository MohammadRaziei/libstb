"""Development check, NOT a test: does a build decode exactly like another one?

Decodes every file of a directory (see make_corpus.py) every way the API allows (header info,
channels 0..4, flipped) and hashes the pixels or the error message. Each file also goes through
a truncated copy and a few bit-flipped copies, so the damaged-input behaviour is compared too:
for those only success / failure, the size and the error message are compared, because a partly
decodable file leaves the missing part as uninitialised memory (stb_image does this, and two runs of
the same build differ).

    PYTHONPATH=<old install> python tests/tools/decode_snapshot.py old.json /tmp/corpus
    PYTHONPATH=<new install> python tests/tools/decode_snapshot.py new.json /tmp/corpus
    python tests/tools/snapshot.py --diff old.json new.json
"""
import hashlib
import zlib
import json
import os
import random
import sys

import libstb as stb


def _open(data, channels):
    """Image.open with a channel count (0 keeps the file's), for a build with or without PixelFormat."""
    if hasattr(stb, "PixelFormat"):
        return stb.Image.open(data, format=None if channels == 0 else stb.PixelFormat(channels))
    return stb.Image.open(data, channels=channels)


def _info(data):
    i = stb.iminfo(data)  # (width, height, channels) in either generation of the API
    return (i.width, i.height, i.channels)


def digest(fn, exact=True):
    try:
        v = fn()
    except Exception as e:  # the error type and message are behaviour too
        return f"raises {type(e).__name__}: {e}"
    if hasattr(v, "tobytes") and hasattr(v, "width"):
        if not exact:  # a damaged file can decode to a picture whose lost part is uninitialised memory
            return f"ok {v.width}x{v.height}x{v.channels}"
        return hashlib.sha256(f"{v.width}x{v.height}x{v.channels}".encode() + v.tobytes()).hexdigest()
    return repr(v)


def variants(data, seed):
    rnd = random.Random(seed)
    yield "full", data
    yield "trunc50", data[: len(data) // 2]
    yield "trunc90", data[: len(data) * 9 // 10]
    for k in range(3):
        b = bytearray(data)
        for _ in range(1 + k * 2):
            if b:
                b[rnd.randrange(len(b))] ^= 1 << rnd.randrange(8)
        yield f"flip{k}", bytes(b)


def main(out, folder):
    result = {}
    for name in sorted(os.listdir(folder)):
        data = open(os.path.join(folder, name), "rb").read()
        for vname, d in variants(data, zlib.crc32(name.encode()) & 0xffff):
            key = f"{name}/{vname}"
            exact = vname == "full"
            result[key + "/info"] = digest(lambda d=d: _info(d))
            for ch in (0, 1, 2, 3, 4):
                result[f"{key}/c{ch}"] = digest(lambda d=d, ch=ch: _open(d, ch), exact)
            if vname == "full":
                result[f"{key}/flip"] = digest(lambda d=d: stb.Image.open(d, flip=True))
    json.dump(result, open(out, "w"), indent=0, sort_keys=True)
    print(len(result), "cases")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
