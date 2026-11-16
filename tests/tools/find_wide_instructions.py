"""Lists the functions of the shared libraries in a directory that contain instructions a baseline x86-64 CPU
(SSE2, or Nehalem: SSE4.2) does not have, outside the functions that are meant to need them (the AVX2 kernels, which
have "avx2" in their name). A wheel that was built right never has one: such an instruction in the portable code is an
"Illegal instruction" crash on an older CPU.

    python tests/tools/find_wide_instructions.py venv/lib/python3.12/site-packages/libstb

Exit status 1 if any function is found. Needs objdump (binutils)."""

import pathlib
import re
import subprocess
import sys

# VEX/EVEX instructions all start with "v" (vmovaps, vpaddd, vzeroupper, ...); the rest are the BMI, ABM and FMA ones
# that -march=haswell would bring (they are mostly encoded so that old CPUs fail on them).
WIDE = re.compile(r"^(v[a-z0-9]+|andn|bextr|blsi|blsmsk|blsr|bzhi|lzcnt|mulx|pdep|pext|rorx|sarx|shlx|shrx|movbe|adcx|adox)$")
HEADER = re.compile(r"^[0-9a-f]+ <(.*)>:$")


def scan(path):
    out = subprocess.run(["objdump", "-d", "-C", "--no-show-raw-insn", str(path)], capture_output=True, text=True,
                         check=True).stdout
    found = {}
    function = None
    for line in out.splitlines():
        header = HEADER.match(line)
        if header:
            function = header.group(1)
            continue
        if function is None or "avx2" in function.lower():
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        mnemonic = parts[1].split()[0] if parts[1].split() else ""
        if WIDE.match(mnemonic):
            found.setdefault(function, set()).add(mnemonic)
    return found


def is_stripped(path):
    """True if the library has no symbol table: objdump then names an instruction after the nearest exported symbol
    before it, which can be an unrelated function (the local functions have no name left)."""
    result = subprocess.run(["nm", str(path)], capture_output=True, text=True)
    return "no symbols" in result.stderr or not result.stdout.strip()


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    bad = 0
    for path in sorted(p for p in root.rglob("*") if p.is_file() and (".so" in p.name or p.suffix == ".pyd")):
        found = scan(path)
        print(f"{path}: {len(found)} function(s) with wide instructions outside the AVX2 kernels")
        if found and is_stripped(path):
            print("    (stripped library: a name below is only the nearest exported symbol before the instruction, the"
                  " local functions have no name left; most likely these are the AVX2 kernels, which are local)")
        for function, mnemonics in sorted(found.items()):
            print(f"    {function[:200]}  [{', '.join(sorted(mnemonics))}]")
        bad += len(found)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
