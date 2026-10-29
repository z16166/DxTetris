#!/usr/bin/env python3
"""gen_assets.py — regenerate every asset section in the order CMake expects.

The compiled shader section (section_cso.inc) is produced by CMake; these four
generators build the font/atlas/mesh/sfx sections from assets/src, and each one
re-assembles assets/generated/assets.h deterministically.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATORS = ("gen_font.py", "gen_atlas.py", "gen_mesh.py", "gen_sfx.py")


def main():
    for name in GENERATORS:
        path = os.path.join(ROOT, "tools", name)
        print(f"== {name} ==", flush=True)
        subprocess.run([sys.executable, path], check=True)
    print("asset sections generated")


if __name__ == "__main__":
    main()
