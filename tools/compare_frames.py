#!/usr/bin/env python3
"""Compare port captures (PNG) with PCSX-Redux reference captures (raw 16-bit).

compare_frames.py REF_DIR PORT_DIR OFFSET [--sheet OUT.png]
A reference file callNNNNN_* or vsyncNNNNN_* pairs with the port's frame(NNNNN + OFFSET).png.
Pixels are compared in the console's 15-bit colour space.
"""
import glob, os, re, struct, sys, zlib

def read_png(path):
    data = open(path, "rb").read(); i = 8; idat = b""; w = h = 0
    while i < len(data):
        n = struct.unpack(">I", data[i:i+4])[0]; t = data[i+4:i+8]; body = data[i+8:i+8+n]
        if t == b"IHDR": w, h = struct.unpack(">II", body[:8])
        if t == b"IDAT": idat += body
        i += 12 + n
    raw = zlib.decompress(idat)
    px = []
    for y in range(h):
        row = raw[y*(w*4+1)+1:(y+1)*(w*4+1)]
        px += [(row[x*4] >> 3, row[x*4+1] >> 3, row[x*4+2] >> 3) for x in range(w)]
    return w, h, px

def read_raw16(path, w, h):
    data = open(path, "rb").read()
    out = []
    for i in range(w*h):
        v = struct.unpack_from("<H", data, i*2)[0]
        out.append((v & 31, v >> 5 & 31, v >> 10 & 31))
    return out

def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = rgb[y*w+x]; raw += bytes((r << 3, g << 3, b << 3, 255))
    def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t+b) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))

ref_dir, port_dir, offset = sys.argv[1], sys.argv[2], int(sys.argv[3])
sheet = sys.argv[sys.argv.index("--sheet")+1] if "--sheet" in sys.argv else None
rows = []
for ref in sorted(glob.glob(os.path.join(ref_dir, "*_*x*_16.raw"))):
    m = re.search(r"(?:call|vsync)(\d+)_(\d+)x(\d+)_16", ref)
    if not m: continue
    n, w, h = int(m.group(1)), int(m.group(2)), int(m.group(3))
    port = os.path.join(port_dir, f"frame{n + offset:05d}.png")
    if not os.path.exists(port):
        print(f"call {n}: no port frame {port}"); continue
    pw, ph, pp = read_png(port)
    rp = read_raw16(ref, w, h)
    if (pw, ph) != (w, h):
        print(f"call {n}: size differs ref {w}x{h} port {pw}x{ph}"); continue
    diff = sum(1 for a, b in zip(rp, pp) if a != b)
    print(f"call {n:5d}: {diff:6d} of {w*h} pixels differ ({100.0*diff/(w*h):.2f}%)")
    rows.append((w, h, rp, pp))
if sheet and rows:
    w, h = rows[0][0], rows[0][1]
    W, H = w*2, h*len(rows)
    canvas = [(0, 0, 0)] * (W*H)
    for k, (_, _, rp, pp) in enumerate(rows):
        for y in range(h):
            for x in range(w):
                canvas[(k*h+y)*W + x] = rp[y*w+x]
                canvas[(k*h+y)*W + w + x] = pp[y*w+x]
    write_png(sheet, W, H, canvas)
    print("sheet (left: original, right: port):", sheet)
