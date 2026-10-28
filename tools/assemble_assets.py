#!/usr/bin/env python3
"""assemble_assets.py — deterministic header assembly from sections (MINE, final; stable).
Run standalone (or from any generator via gencommon.write_header):
  py -3 tools/assemble_assets.py  →  assets/generated/assets.h + manifest.json
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gencommon import assemble_header, GEN_DIR

if __name__ == "__main__":
    print("wrote", assemble_header())
