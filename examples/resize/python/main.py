from pathlib import Path

import libstb as stb

out = Path("out")
out.mkdir(exist_ok=True)

img = stb.Image.open("astronaut.png")

# Shrink with the default filter, then enlarge the tiny result with each filter by name.
tiny = img.resize(64, 64)
tiny.write(out / "tiny.png")

for name in ("nearest", "linear", "cubic", "bspline", "mitchell"):
    tiny.resize(256, 256, name).write(out / f"{name}.png")
