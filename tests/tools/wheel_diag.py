"""Print what an installed libstb wheel is made of and links against (for CI logs).

Used by the musllinux test command in pyproject.toml while a segfault there is being tracked
down: it shows the files of the package, the NEEDED / RUNPATH entries of every native file and
the libraries auditwheel grafted (libstb.libs).
"""

import glob
import os
import shutil
import subprocess

import libstb

package = os.path.dirname(libstb.__file__)
print("package:", package)
for root, _dirs, files in os.walk(package):
    if "__pycache__" in root or os.sep + "include" in root:
        continue
    for name in sorted(files):
        path = os.path.join(root, name)
        print(f"  {os.path.relpath(path, package)}  ({os.path.getsize(path)} bytes)")

grafted = os.path.join(os.path.dirname(package), "libstb.libs")
print("grafted libraries:", sorted(os.listdir(grafted)) if os.path.isdir(grafted) else "none")

native = glob.glob(os.path.join(package, "libstb_py*.so")) + glob.glob(os.path.join(package, "lib", "*.so*"))
for path in native:
    print("\n==", os.path.relpath(path, package))
    if shutil.which("readelf"):
        out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
        print("\n".join(line for line in out.splitlines() if any(k in line for k in ("NEEDED", "RUNPATH", "RPATH", "SONAME"))))
    if shutil.which("ldd"):
        print(subprocess.run(["ldd", path], capture_output=True, text=True).stdout)
