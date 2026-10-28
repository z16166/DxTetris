#!/usr/bin/env python3
"""tools/harness/probe.py — full final-source verification probe (fxc + layout header generator)."""
import subprocess, os, sys, struct

FXC = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
DXC = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe"
S_VS = r"H:\tetris_cpp\shaders\game.hxlsl"
S_PS = r"H:\tetris_cpp\shaders\game.pshl"
OUT  = r"H:\tetris_cpp\build\harness\scratch\out"
os.makedirs(OUT, exist_ok=True)
os.makedirs(r"H:\tetris_cpp\build", exist_ok=True)
os.makedirs(r"H:\tetris_cpp\docs", exist_ok=True)

def run(exe, args):
    print("=" * 70)
    print(" ".join([os.path.basename(exe)] + args))
    r = subprocess.run([exe] + args, capture_output=True, text=True)
    o = ((r.stdout or "") + (r.stderr or "")).strip()
    if o: print(o)
    print(f"exit={r.returncode}")
    return r.returncode

def head(path):
    if not os.path.exists(path):
        return "MISSING"
    b = open(path, "rb").read()
    return f"size={len(b)} magic={b[0:4].hex()} hdr16={b[4:16].hex()}"

# canonical name-trailer lookup table (DXBC classic name slots), from the
# known canonical-name table in d3d11shader/d3d11shadertracing; 0x06 -> NAME_NONE.
CANON = ["PC","TV","PN","PRGB","FOG","MV","MS","C0","C1","OC","RT","AS","U0","US",
         "TC","TR","PV","PR","IN","TN","TSS","CL","OV","CG","GS","SVP","SVT","SVVC",
         "SVSS","SVOT","SVSP","SVRG","SVCS","SVRV","SVP0","SVP1","SVDP","SMP0","SMP1",
         "SMP2","SMP3","SMP4","SMP5","SMP6","SMP7","SMP8","SMP9","GSP","GSP0","GSP1",
         "GSP2","GSP3","GSP4","GSP5","GSP6","GSP7","GSP8","GSP9"]

def trailer_name(t):
    name, idx = (t >> 8) & 0xFF, t & 0xFF
    return f"slot {CANON[name]}({name}) idx {idx}" if name < len(CANON) else f"slot {name} idx {idx}"

def scan_trailers(path):
    data = open(path, "rb").read()
    hits = []
    for name in [b"PVS\x00", b"TWW\x00", b"TVB\x00", b"PRB\x00"]:
        i = 0
        while True:
            j = data.find(name, i)
            if j < 0: break
            # classic name-trailer word (name<<8) + 5-bit index follows
            cands = []
            t = struct.unpack_from("<I", data, j + 4)[0]
            nm, ix = t >> 8, t & 0xFF
            if (nm == 0 and ix == 0) or (CANON[nm] if nm < len(CANON) else "") == "":
                cands.append((j, t))
            i = j + 1
        for off, t in cands:
            hits.append((name.decode(), hex(t), trailer_name(t)))
    return hits

print("=== A) fxc fa5b0 probe (documented rejection) ===")
run(FXC, ["/nologo", "/T", "fa5b0", S_PS, "/Fo", OUT + r"\a_probe.bin"])

print("=== B) fxc vs_5_0 final sources ===")
for src, out in [(S_PS, "vs_fxc50"), (S_PS, "ps_fxc50")]:
    # note: PS source is compiled with ps_5_0 below; VS compiled with vs_5_0 via entry probe first
    pass
run(FXC, ["/nologo", "/T", "vs_5_0", S_VS, "/Fo", OUT + r"\vs_fxc50.cso"])
run(FXC, ["/nologo", "/T", "ps_5_0", S_PS, "/Fo", OUT + r"\ps_fxc50.cso"])

print("=== C) dxc fa5b0 probe (documented rejection) ===")
run(DXC, ["/nologo", "/T", "fa5b0", S_PS, "/Fo", OUT + r"\c_probe.bin"])

print("=== D) headers of the outputs (bytecode version = bytes 10-11) ===")
for f in sorted(os.listdir(OUT)):
    p = os.path.join(OUT, f)
    if f.endswith((".cso", ".bin")):
        print(f, "->", head(p))

print("=== E) semantic name-trailer bytes (vs+ps fxc outputs) ===")
for f in ["vs_fxc50.cso", "ps_fxc50.cso"]:
    for nm, t, dec in scan_trailers(os.path.join(OUT, f)):
        print(f, "trailing", nm, "trailer", t, "->", dec)

print("DONE")
