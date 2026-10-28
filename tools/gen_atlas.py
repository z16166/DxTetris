#!/usr/bin/env python3
"""gen_atlas.py — UI atlas generator (PIL). — MINE, final.
Regions (packed, 4px gutter, top-left origin): white (1px sRGB8, sampled raw),
panel, frame, circle, vignette, shadow, 7 per-piece glyph masks (I O T S Z J L,
rounded-square cell; shapes per SRS spawn grids).
Outputs: assets/src/atlas.png + assets_atlas.h section.
"""
import hashlib
import json
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from gencommon import write_header

# stable sha256 ids
def sha256f(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        h.update(f.read())
    return h.hexdigest()

CELL = 96   # glyph mask cell px (square, 4px margin for rounding radius)
REGIONS = {}   # name -> (w, h, painter)

def paint(name, w, h, draw_fn, out):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    draw_fn(d, w, h)
    REGIONS[name] = img
    manifest = {"atlas_w": 0, "atlas_h": 0, "regions": {}}
    # pack: simple grid rows of equal height (all regions h<=48, w<=1024; gutter 4px)
    # manual: name -> (x, y, w, h) after row-packing (heights differ per region; stable order)
    pass

def draw_white(d, w, h): d.rectangle([0, 0, w-1, h-1], fill=(255, 255, 255, 255))

def draw_panel(d, w, h):
    d.rectangle([0, 0, w-1, h-1], fill=(24, 26, 32, 200))          # dark panel
    d.rectangle([1, 1, w-2, h-2], outline=(120, 130, 145, 210))    # border

def draw_frame(d, w, h):
    d.rectangle([0, 0, w-1, h-1], outline=(200, 205, 215, 255), width=2)  # outer frame

def draw_circle(d, w, h):
    d.ellipse([0, 0, w-1, h-1], fill=(255, 255, 255, 255))

def draw_vign(d, w, h):
    d.ellipse([0, 0, w-1, h-1], fill=(0, 0, 0, 255))               # inner solid (edge via sprite alpha scale)

def draw_shadow(d, w, h):
    d.ellipse([0, 0, w-1, h-1], fill=(0, 0, 0, 160))               # soft blob (scaled on draw)

def rounded(d, box, r, fill):
    x0, y0, x1, y1 = box
    d.rectangle([x0+r, y0, x1-r, y1], fill=fill)
    d.rectangle([x0, y0+r, x1, y1-r], fill=fill)
    d.pieslice([x0, y0, x0+2*r, y0+2*r], 180, 270, fill=fill)
    d.pieslice([x1-2*r, y0, x1, y0+2*r], 270, 360, fill=fill)
    d.pieslice([x0, y1-2*r, x0+2*r, y1], 90, 180, fill=fill)
    d.pieslice([x1-2*r, y1-2*r, x1, y1], 0, 90, fill=fill)

SRS_GRIDS = {  # spawn-state grids (7 pieces), 4x4 cell coords (x,y); shape mask only
    "I": [(0,1),(1,1),(2,1),(3,1)],
    "O": [(1,0),(2,0),(1,1),(2,1)],
    "T": [(1,0),(0,1),(1,1),(2,1)],
    "S": [(1,0),(2,0),(0,1),(1,1)],
    "Z": [(0,0),(1,0),(1,1),(2,1)],
    "J": [(0,0),(0,1),(1,1),(2,1)],
    "L": [(0,1),(1,1),(2,1),(2,0)],
}

def main():
    regions = {"white": (1, 1, draw_white), "panel": (48, 48, draw_panel),
               "frame": (48, 48, draw_frame), "circle": (48, 48, draw_circle),
               "vignette": (64, 40, draw_vign), "shadow": (48, 48, draw_shadow)}
    # glyph masks
    imgs = {}
    for name in regions:
        w, h, fn = regions[name]
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        fn(ImageDraw.Draw(img), w, h)
        imgs[name] = img
    for piece, cells in SRS_GRIDS.items():
        c = CELL
        img = Image.new("RGBA", (c, c), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        cs = c // 4
        for cx, cy in cells:
            rounded(d, (cx*cs+3, cy*cs+3, cx*cs+cs-3, cy*cs+cs-3), 6, (255, 255, 255, 255))
        imgs["glyph_" + piece.lower()] = img

    # pack rows: sorted by name for stable positions
    names = sorted(imgs.keys())
    GUT = 4
    total_w = max(im.size[0] for im in imgs.values())
    # simple column-stacked packer (first-fit, deterministic)
    placed = {}
    x, y, rowh = GUT, GUT, 0
    maxy = 0
    cols = []
    for n in names:
        w, h = imgs[n].size
        cols.append((n, w, h))
    W = 512
    H = 512
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # first-fit top-left shelf pack (stable)
    shelf = []  # (x,y,height)
    for n, w, h in cols:
        y0 = 0; x0 = 0
        # shelf placement
        for (sx, sy, shw, shh) in shelf:
            if w + GUT <= shw and h <= shh:
                x0, y0 = sx, sy
                break
        else:
            yb = max((sy + sh + GUT) for (_, sy, _, sh) in shelf) if shelf else GUT
            x0, y0 = GUT, yb
        placed[n] = (x0, y0, w, h)
        canvas.alpha_composite(imgs[n], (x0, y0))
        for i, s in enumerate(shelf):
            if s[1] == y0:
                shelf[i] = (s[0], s[1], max(s[2], w), s[3]); break
        else:
            shelf.append((x0, y0, w, h))
    # crop used area
    xs = max(v[0]+v[2] for v in placed.values()) + GUT
    ys = max(v[1]+v[3] for v in placed.values()) + GUT
    canvas = canvas.crop((0, 0, xs, ys))
    canvas.save(os.path.join(ROOT, "assets", "src", "atlas.png"))

    manifest = {"atlas_w": xs, "atlas_h": ys, "regions": {}}
    for n in names:
        manifest["regions"][n] = {"x": placed[n][0], "y": placed[n][1], "w": placed[n][2], "h": placed[n][3]}
    with open(os.path.join(ROOT, "assets", "src", "atlas_layout.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)

    # header section: static regions (x,y,w,h) + id hash (fnv32 in core.h)
    lines = [
        "// ATLAS (generated by tools/gen_atlas.py) — RGBA8 regions, top-left px, sampled raw",
        "static const int kAtlasW = %d;" % xs,
        "static const int kAtlasH = %d;" % ys,
        "static const struct { const char* name; unsigned int id; int x, y, w, h; } kRegions[] = {",
    ]
    def fnv32(s):
        hsh = 2166136261
        for ch in s.encode():
            hsh = ((hsh ^ ch) * 16777619) & 0xffffffff
        return hsh
    for n in names:
        x, y, w, h = placed[n]
        lines.append(f'    {{ "{n}", 0x{fnv32(n):08X}u, {x}, {y}, {w}, {h} }},')
    lines.append("};")
    write_header("assets_atlas.h", lines, tag="atlas")
    print("wrote assets_atlas.h (section)")

if __name__ == "__main__":
    main()
