# resize

Shrinks `astronaut.png` to 64×64 with the default filter, then enlarges that small picture to 256×256 once per filter
(`nearest`, `linear`, `cubic`, `bspline`, `mitchell`). The pictures go to `out/`.

Page: https://mohammadraziei.github.io/libstb/docs/image/resize

Run it in a folder that holds `astronaut.png` (there is one in `data/images/`):

```bash
# Python
pip install libstb
python python/main.py

# C++: build it with the CMake recipe of the install page, then run the program in the same folder
```
