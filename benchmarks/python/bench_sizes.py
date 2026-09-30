"""
bench_sizes.py: real install footprint (download size, including
dependencies) for libstb vs. the competitors this suite benchmarks,
using pip-size's own Python API directly
(github.com/MohammadRaziei/pip-size). pip-size has no install step
either: it resolves the real wheel for the current platform/Python
straight from PyPI's JSON API and sums sizes without downloading or
installing anything.

numpy is listed on its own, because it is a dependency of most of the
others (libstb included: it is what Image.array returns), so the
"total" column is not double-counted magic: it is what `pip install`
of that one package pulls in.
"""
import argparse
import asyncio
import json
import sys

# name -> (pip spec to resolve, one-line capability summary shown next to it in the report)
PACKAGES = {
    "libstb":      ("libstb",                 "image decode, png/jpg/bmp/tga encode, resize, TrueType text (stb, no system dependencies)"),
    "pillow":      ("Pillow",                 "many-format image io, resize, filters, drawing, FreeType text"),
    "opencv":      ("opencv-python-headless", "image io, resize, and a large computer-vision toolkit"),
    "imageio":     ("imageio",                "many-format image io (delegates to Pillow and friends)"),
    "skimage":     ("scikit-image",           "image processing algorithms (io + resize + far more)"),
    "numpy":       ("numpy",                  "arrays only (a dependency of everything above except Pillow)"),
}


async def _resolve(spec):
    from packaging.requirements import Requirement
    from pip_size import DependencyResolver, Printer
    from pip_size.core import PyPIClient

    req = Requirement(spec)
    async with PyPIClient() as client:
        resolver = DependencyResolver(client=client, quiet=True)
        pkg = await resolver.resolve(req)

    if pkg is None:
        return {"available": False, "error": "pip-size could not resolve this package on PyPI"}

    def _flatten(p):
        out = [{"name": p.name, "version": p.version, "size": Printer.format_size(p.size)}]
        for d in p.dependencies:
            out.extend(_flatten(d))
        return out

    return {
        "available": True,
        "name": pkg.name,
        "version": pkg.version,
        "size": Printer.format_size(pkg.size),
        "total_size": Printer.format_size(pkg.total_size()),
        "filename": pkg.filename,
        "dependencies": _flatten(pkg)[1:],  # everything but the package itself
    }


def _pip_size(spec):
    try:
        return asyncio.run(_resolve(spec))
    except ImportError:
        return {"available": False, "error": "pip-size is not installed"}
    except Exception as e:  # noqa: BLE001
        return {"available": False, "error": f"{type(e).__name__}: {e}"}


def run():
    result = {}
    for key, (spec, capability) in PACKAGES.items():
        data = _pip_size(spec)
        data["capability"] = capability
        result[key] = data
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", help="path to write results JSON")
    args = p.parse_args()

    result = run()
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    for pkg, data in result.items():
        if data.get("available"):
            ndeps = len(data.get("dependencies", []))
            print(f"{pkg:12s} {data.get('total_size', '?'):>10s}  ({ndeps} dependenc{'y' if ndeps == 1 else 'ies'})", file=sys.stderr)
        else:
            print(f"{pkg:12s} unavailable: {data.get('error')}", file=sys.stderr)
