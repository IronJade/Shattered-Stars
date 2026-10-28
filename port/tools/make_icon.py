#!/usr/bin/env python3
"""make_icon.py: draws the Shattered Stars application icon and writes port/res/icon/.

The icon ("Fractured Gold") is an original design, drawn here from plain vector shapes: a
gold five-point star broken into shards that drift slightly apart, on a midnight-blue
rounded tile. It holds no game art, no logo, no trademark, no font and no text; nothing in
it comes from the discs.

Each size is drawn at its own pixel size rather than scaled from the largest, so the small
ones keep their gaps about a pixel wide: six shards from 48 px up, three at 24 and 32 px,
one clean break at 16 px.

    port/tools/make_icon.py [--out DIR]        (default: port/res/icon beside this tools/ folder)

Writes, in DIR:
    shattered-stars-<N>.png   N = 16 24 32 48 64 128 256 (the hicolor sizes the Linux
                              bundle installs; 64/128/256 are compiled into both programs
                              for SDL_SetWindowIcon by port/CMakeLists.txt)
    shattered-stars.ico       all seven sizes: the Windows programs' icon resource
                              (port/res/windows/app.rc.in); BMP entries, the 256 one PNG
    shattered-stars.svg       the 256 px drawing as SVG: hicolor/scalable

Needs pycairo and Pillow (Fedora: python3-cairo python3-pillow). The outputs are
committed, so building the port never needs this script; run it again only when the
design changes, and commit what it writes.
"""
import argparse
import io
import math
import os
import struct
import sys

import cairo
from PIL import Image

SIZES = [16, 24, 32, 48, 64, 128, 256]


def hexc(h, a=1.0):
    h = h.lstrip('#')
    return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, a)


def poly(ctx, pts):
    ctx.move_to(*pts[0])
    for p in pts[1:]:
        ctx.line_to(*p)
    ctx.close_path()


def rrect(ctx, x, y, w, h, r):
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()


def star_pts(cx, cy, R, r, n=5, rot=-90.0):
    pts = []
    for i in range(2 * n):
        a = math.radians(rot + i * 180.0 / n)
        rad = R if i % 2 == 0 else r
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    return pts


def sparkle(ctx, cx, cy, rx, ry, pinch):
    """A small four-point star with concave sides (the background specks)."""
    k = pinch
    ctx.move_to(cx, cy - ry)
    ctx.curve_to(cx + rx * k, cy - ry * k, cx + rx * k, cy - ry * k, cx + rx, cy)
    ctx.curve_to(cx + rx * k, cy + ry * k, cx + rx * k, cy + ry * k, cx, cy + ry)
    ctx.curve_to(cx - rx * k, cy + ry * k, cx - rx * k, cy + ry * k, cx - rx, cy)
    ctx.curve_to(cx - rx * k, cy - ry * k, cx - rx * k, cy - ry * k, cx, cy - ry)
    ctx.close_path()


def draw(ctx, S):
    """Paints the icon into the unit square; ctx is already scaled to S x S pixels."""
    px = 1.0 / S
    m = round(0.03 * S) / S  # a whole-pixel margin
    # the midnight tile
    rrect(ctx, m, m, 1 - 2 * m, 1 - 2 * m, 0.22)
    g = cairo.LinearGradient(0, 0, 0, 1)
    g.add_color_stop_rgba(0, *hexc('#2b2f7a'))
    g.add_color_stop_rgba(1, *hexc('#0b0d2a'))
    ctx.set_source(g)
    ctx.fill_preserve()
    if S >= 32:
        ctx.set_source_rgba(1, 1, 1, 0.14)
        ctx.set_line_width(px * (1.0 if S < 128 else 2.0))
        ctx.stroke()
    else:
        ctx.new_path()
    # faint specks in the background
    if S >= 48:
        for (x, y, s) in [(0.20, 0.22, 0.035), (0.82, 0.30, 0.025), (0.78, 0.84, 0.03)]:
            sparkle(ctx, x, y, s, s * 1.4, 0.15)
            ctx.set_source_rgba(0.75, 0.82, 1.0, 0.55)
            ctx.fill()
    cx, cy = 0.5, 0.54
    R = 0.40 if S >= 32 else 0.43
    r = R * 0.46
    star = star_pts(cx, cy, R, r)
    # a warm glow behind the star
    if S >= 48:
        rg = cairo.RadialGradient(cx, cy, 0.02, cx, cy, 0.48)
        rg.add_color_stop_rgba(0, 1.0, 0.80, 0.35, 0.35)
        rg.add_color_stop_rgba(1, 1.0, 0.80, 0.35, 0.0)
        ctx.set_source(rg)
        ctx.arc(cx, cy, 0.48, 0, 2 * math.pi)
        ctx.fill()
    # the shards: wedges around an impact point slightly off centre, each pushed outward
    P = (cx - 0.025, cy - 0.03)
    if S >= 48:
        rays = [-78, -12, 38, 104, 166, 232]
        tones = ['#ffe07a', '#ffc53d', '#ffd860', '#f5a623', '#ffcc4a', '#f8b62e']
        d = 0.026
    elif S >= 24:
        rays = [-60, 60, 160]  # three bold shards, so the break still reads at 24-32 px
        tones = ['#ffe07a', '#f5a623', '#ffcc4a']
        d = 0.9 * px if S < 32 else 1.0 * px
    else:
        rays = [-54, 126]  # 16 px: one clean break, the halves a pixel apart
        tones = ['#ffe07a', '#f7ad2a']
        d = 0.55 * px
    for i, a0 in enumerate(rays):
        a1 = rays[(i + 1) % len(rays)] + (360 if i == len(rays) - 1 else 0)
        mid = math.radians((a0 + a1) / 2)
        ctx.save()
        ctx.translate(d * math.cos(mid), d * math.sin(mid))
        wedge = [P]
        for k in range(9):
            a = math.radians(a0 + (a1 - a0) * k / 8)
            wedge.append((P[0] + 2 * math.cos(a), P[1] + 2 * math.sin(a)))
        poly(ctx, wedge)
        ctx.clip()
        poly(ctx, star)
        ctx.set_source_rgba(*hexc(tones[i % len(tones)]))
        ctx.fill_preserve()
        lg = cairo.LinearGradient(0, cy - R, 0, cy + R)
        lg.add_color_stop_rgba(0, 1, 1, 1, 0.35)
        lg.add_color_stop_rgba(0.55, 1, 1, 1, 0.0)
        lg.add_color_stop_rgba(1, 0.55, 0.25, 0.0, 0.30)
        ctx.set_source(lg)
        ctx.fill()
        ctx.restore()


def render_png(S, path):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, S, S)
    ctx = cairo.Context(surf)
    ctx.scale(S, S)
    draw(ctx, S)
    surf.flush()
    surf.write_to_png(path)


def render_svg(path):
    surf = cairo.SVGSurface(path, 256, 256)
    surf.set_document_unit(cairo.SVGUnit.PX)
    ctx = cairo.Context(surf)
    ctx.scale(256, 256)
    draw(ctx, 256)
    surf.finish()


def png_bytes(im):
    out = io.BytesIO()
    im.save(out, format='PNG', optimize=True)
    return out.getvalue()


def dib_bytes(im):
    """An ICO image entry as a 32-bit BMP (BITMAPINFOHEADER, bottom-up BGRA, AND mask)."""
    w, h = im.size
    px = im.load()
    header = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    rows = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            rows += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(mask_row)
        for x in range(w):
            if px[x, y][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return header + bytes(rows) + bytes(mask)


def write_ico(images, path):
    """images: {size: RGBA Image}. Largest first; 256 as PNG, the rest as BMP."""
    entries = []
    for S in sorted(images, reverse=True):
        im = images[S]
        entries.append((S, png_bytes(im) if S >= 256 else dib_bytes(im)))
    offset = 6 + 16 * len(entries)
    out = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    for S, data in entries:
        dim = 0 if S >= 256 else S
        out += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    with open(path, 'wb') as f:
        f.write(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(here, '..', 'res', 'icon'))
    args = ap.parse_args()
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    images = {}
    for S in SIZES:
        path = os.path.join(out, f'shattered-stars-{S}.png')
        render_png(S, path)
        with Image.open(path) as im:
            images[S] = im.convert('RGBA')
        # re-save through Pillow, optimised, without timestamps (stable bytes)
        with open(path, 'wb') as f:
            f.write(png_bytes(images[S]))
    write_ico(images, os.path.join(out, 'shattered-stars.ico'))
    render_svg(os.path.join(out, 'shattered-stars.svg'))
    for name in sorted(os.listdir(out)):
        print(f'{os.path.join(out, name)}: {os.path.getsize(os.path.join(out, name))} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
