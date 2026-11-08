# Sample data

A few pictures and fonts, small enough to live in the repository, that the examples (`examples/`) and the
tests (`tests/`) work with. Every file here can be redistributed: the licenses are listed below, and the
fonts bring their own license text. `tests/data/` is a different folder: synthetic fonts whose every glyph
is known to the unit, for tests that need exact numbers.

## images/

| file | what it is | license | source |
|---|---|---|---|
| `camera.png` | 512 x 512, gray: a photographer with a camera (the stand-in for the classic "cameraman") | CC0, by the photographer Lav Varshney | scikit-image, `skimage/data/camera.png` |
| `astronaut.png` | 512 x 512, RGB: the astronaut Eileen Collins | public domain (NASA) | scikit-image, `skimage/data/astronaut.png` |
| `chelsea.png` | 451 x 300, RGB: Chelsea the cat | CC0, by the photographer Stefan van der Walt | scikit-image, `skimage/data/chelsea.png` |
| `rocket.jpg` | 640 x 427, RGB, a real JPEG: a Falcon 9 launching DSCOVR | public domain, released by SpaceX | scikit-image, `skimage/data/rocket.jpg` |
| `badge.png` | 160 x 160, RGBA: a disc with a feathered, transparent rim | MIT, like libstb | made by `make_badge.py` with libstb |

`badge.png` is the one picture with an alpha channel, for blending, masks and RGBA conversions.
`python data/make_badge.py` draws it again.

The famous "Lena" test image is not here on purpose: it is a copyrighted photograph (a Playboy picture),
so it cannot be redistributed in an MIT-licensed repository. The original "cameraman" has the same kind of
doubt, which is why scikit-image replaced it with the CC0 photograph above, and so did this folder.

## fonts/

| file | what it is | license |
|---|---|---|
| `Vazirmatn-Regular.ttf`, `Vazirmatn-Bold.ttf` | Vazirmatn 33.003, TrueType, 1333 glyphs: Persian, Arabic and Latin, with GSUB (joining forms, ligatures) and GPOS (kerning) | SIL Open Font License 1.1, Copyright 2015 The Vazirmatn Project Authors (https://github.com/rastikerdar/vazirmatn); the license text is `fonts/OFL.txt` |

The fonts are unmodified copies of the upstream release files. Rendering a text with a font and putting the
picture in your own work does not put that work under the OFL; copying or changing the font file does carry
the license along.
