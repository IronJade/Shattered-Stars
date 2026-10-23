#!/usr/bin/env python3
"""Which of Expel's world-map entrances a party on foot can reach (docs/ap/apworld.md, "Open
world": the teleporter proof).

The late proof's sweep fires every entrance trigger of a world map wherever it lies. After the
Final Save Point that is right (Virtual Expel's Synard flies anywhere, Prima p. 130), but the
teleporter state (chapter 38, the Fun City arena's old man to Arlia) has no flight on Expel:
Expel's Synard spot (game_state +0x26c) is set only by Virtual Arlia's arrival (map 18 0x5a4,
chapter >= 60 and flag 730), and the Synard flag (11) only on Energy Nede (maps 417, 588). So
this tool reads the terrain the way the world map walks it and tells which entrances lie on
the land the teleporter's party can walk or sail to.

- The terrain: Expel's world map is 9 x 7 blocks of 0x3000 units on a torus (0x1b000 x
  0x15000), archive entries 0xfd4 + i (block 48 is entry 0x1013 at chapters 15-60,
  worldmap_bg.cpp map1_alternate). Each block holds 16 sub-cells of polygons; a point is
  walkable when the polygon under it has attr bit 0 (block_query, 0x8008ca04; polygon kinds E,
  F and H never are). The polygons are rasterised onto a grid (--step units) and the walkable
  cells flood-filled, 4-connected, across the torus seams.
- An entrance group (docs/ap/doors.json `key_group`, world 1) lies in the component of the
  points where its maps' exits put the party on map 1.
- The teleporter's party lands in Arlia (map 18) and leaves it onto the Cross continent; the
  Herlie ferry (map 144's sailor, routine 0x7298: chapter >= 16 and the fare) sails to Port
  Hilton on the Lacour continent and back (map 169, from chapter 23). The other ships of the
  story (Port Hilton to map 231 for Hoffmann Ruins, chapters 23-27; to El, chapter 28) no
  longer sail at chapter 38, so every other component is an island for it.

Usage: tools/ap_walkmap.py [--disc1 CUE] [--chapter 38] [--step 64] [--png OUT.png] [--check]
--check exits 1 unless tools/ap_late_proof.py TELEPORTER_ISLANDS is exactly the set of maps
the islands' entrances lead to.
"""
import argparse
import collections
import json
import os
import struct
import sys
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import so2_data  # noqa: E402

WIDTH, DEPTH, BLOCK = 0x1B000, 0x15000, 0x3000
FIRST_BLOCK, ALT_BLOCK = 0xFD4, 0x1013
LANDING_MAP = 17       # Arlia's world-map exit (the teleporter lands on map 18, inside Arlia)
FERRY_MAPS = (144, 169)  # Herlie harbour <-> Port Hilton, sailing at chapter 38


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _s16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def polygons(blk):
    """(attr, [(x, z), ...]) of a block's polygons in its centred coordinates, as block_query
    reads them (kinds A-K; E, F and H are skipped, never walkable)."""
    hdr_base = _u32(blk, 0) * 8 + 0x68
    out = []
    sizes = (0x18, 0x14, 0x10, 0x10, 0x20, 0x18, 0x14, 0x14, 0x10, 0x18, 0x20)
    for i in range(16):
        hdr = hdr_base + i * 0x20
        polys = _u32(blk, hdr_base + 0x200 + i * 4) + 0x280
        verts = _u32(blk, 0x28 + i * 4) + 0x68
        attrs = hdr_base + polys
        rec = hdr_base + polys + (_u16(blk, hdr) & 0xFFFC)

        def v(off):
            return (_s16(blk, verts + off + 6), _s16(blk, verts + off + 4))   # (x, z)

        index = 0
        for kind, size in enumerate(sizes):
            n = _u16(blk, hdr + 2 + 2 * kind)
            for k in range(n):
                r, pts = rec, None
                if kind in (0, 9):        # A, J: a, b at +0x14, c at +6
                    ab = _u32(blk, r + 0x14)
                    pts = [v((ab & 0xFFFF) * 2), v((ab >> 16) * 2), v(_u16(blk, r + 6) * 2)]
                elif kind == 1:           # B: a, c at +0x10, b at +0xa
                    ac = _u32(blk, r + 0x10)
                    pts = [v((ac & 0xFFFF) * 2), v(_u16(blk, r + 0xA) * 2), v((ac >> 16) * 2)]
                elif kind == 2:           # C: packed bytes
                    t1, t0 = _u32(blk, r + 4), _u32(blk, r + 8)
                    pts = [v(((t0 >> 14) & 0x3FC) * 2), v(((t1 >> 22) & 0x3FC) * 2), v(((t0 >> 22) & 0x3FC) * 2)]
                elif kind == 3:           # D: a, c at +0xc, b at +0xa
                    ac = _u32(blk, r + 0xC)
                    pts = [v((ac & 0xFFFF) * 2), v(_u16(blk, r + 0xA) * 2), v((ac >> 16) * 2)]
                elif kind in (6, 8):      # G, I: quads a, b, d, c
                    q = _u32(blk, r + (0x10 if kind == 6 else 0xC))
                    pts = [v((q & 0xFF) * 8), v(((q >> 6) & 0x3FC) * 2), v(((q >> 22) & 0x3FC) * 2),
                           v(((q >> 14) & 0x3FC) * 2)]
                elif kind == 10:          # K: quads a, b, d, c
                    ab, cd = _u32(blk, r + 0x18), _u32(blk, r + 0x1C)
                    pts = [v((ab & 0xFFFF) * 2), v((ab >> 16) * 2), v((cd >> 16) * 2), v((cd & 0xFFFF) * 2)]
                if pts is not None:
                    out.append((blk[attrs + index + k], pts))
                rec += size
            index += n
    return out


def _fill(grid, pts, value, bx, bz, step):
    xs = [bx * BLOCK + 0x1800 + p[0] for p in pts]
    zs = [bz * BLOCK + 0x1800 + p[1] for p in pts]
    gx, gz = np.meshgrid(np.arange(min(xs) // step, max(xs) // step + 2),
                         np.arange(min(zs) // step, max(zs) // step + 2))
    px, pz = gx * step + step / 2, gz * step + step / 2
    pos = np.ones(px.shape, bool)
    neg = np.ones(px.shape, bool)
    for i in range(len(xs)):
        ax, az, cx, cz = xs[i], zs[i], xs[(i + 1) % len(xs)], zs[(i + 1) % len(xs)]
        cross = (cx - ax) * (pz - az) - (cz - az) * (px - ax)
        pos &= cross >= 0
        neg &= cross <= 0
    inside = pos | neg
    grid[gz[inside] % grid.shape[0], gx[inside] % grid.shape[1]] = value


def walk_components(disc, chapter, step):
    """(component grid, sizes): -1 where not walkable."""
    nx, nz = WIDTH // step, DEPTH // step
    grid = np.full((nz, nx), -1, np.int8)
    for i in range(63):
        entry = ALT_BLOCK if i == 48 and 0xF <= chapter < 0xF + 0x2E else FIRST_BLOCK + i
        polys = polygons(disc.entry(entry))
        for attr, pts in sorted(polys, key=lambda p: p[0] & 1):   # walkable drawn last
            _fill(grid, pts, attr & 1, i % 9, i // 9, step)
    walk = grid == 1
    comp = np.full(walk.shape, -1, np.int32)
    sizes = []
    for z0, x0 in zip(*np.nonzero(walk)):
        if comp[z0, x0] >= 0:
            continue
        c = len(sizes)
        comp[z0, x0] = c
        queue, n = collections.deque([(z0, x0)]), 0
        while queue:
            z, x = queue.popleft()
            n += 1
            for zz, xx in (((z + 1) % nz, x), ((z - 1) % nz, x), (z, (x + 1) % nx), (z, (x - 1) % nx)):
                if walk[zz, xx] and comp[zz, xx] < 0:
                    comp[zz, xx] = c
                    queue.append((zz, xx))
        sizes.append(n)
    return comp, sizes


def component_at(comp, x, z, step, reach=6):
    """The walkable component nearest to (x, z) within 'reach' cells, or None."""
    nz, nx = comp.shape
    gx, gz = int(x // step), int(z // step)
    for d in range(reach + 1):
        for dz in range(-d, d + 1):
            for dx in range(-d, d + 1):
                c = comp[(gz + dz) % nz, (gx + dx) % nx]
                if c >= 0:
                    return int(c)
    return None


def entrance_components(comp, step, doors):
    """{key group: (component, its maps, the maps its world-map entrances lead to)}."""
    groups = collections.defaultdict(lambda: [set(), set()])
    for d in doors:
        if d.get("key_group") and d.get("world") == 1:
            groups[d["key_group"]][1].add(d["to"]["map"])
    maps_of = {g: v[1] for g, v in groups.items()}
    exits = collections.defaultdict(collections.Counter)
    for d in doors:
        if d["to"]["map"] != 1 or d["map"] >= 900 or d["map"] in (0, 1, 2, 3, 826):
            continue
        c = component_at(comp, d["to"]["x"], d["to"]["z"], step)
        for g, maps in maps_of.items():
            if d["map"] in maps and c is not None:
                exits[g][c] += 1
    return {g: (exits[g].most_common(1)[0][0] if exits[g] else None, sorted(maps_of[g])) for g in maps_of}


def islands(disc_path, chapter=38, step=64, png=None):
    """(the maps behind entrances the teleporter's party can't reach, report lines)."""
    disc = so2_data.Disc(disc_path)
    comp, sizes = walk_components(disc, chapter, step)
    doors = json.load(open(os.path.join(ROOT, "docs", "ap", "doors.json")))["doors"]
    groups = entrance_components(comp, step, doors)
    home = set()
    for d in doors:   # the land of Arlia's exit and of the ferry's two harbours
        if d["map"] in (LANDING_MAP,) + FERRY_MAPS and d["to"]["map"] == 1:
            c = component_at(comp, d["to"]["x"], d["to"]["z"], step)
            if c is not None:
                home.add(c)
    lines = [f"grid {comp.shape[1]} x {comp.shape[0]} cells of {step} units, {len(sizes)} walkable components "
             f"(largest {sorted(sizes, reverse=True)[:6]}); the teleporter's land: components {sorted(home)}"]
    out = set()
    for g, (c, maps) in sorted(groups.items(), key=lambda kv: (kv[1][0] is None, kv[1][0] or 0, kv[0])):
        reach = c in home
        if not reach:
            out.update(maps)
        lines.append(f"  {'walk/ferry' if reach else 'ISLAND    '} component {c}: {g} -> maps {maps}")
    if png:
        pal = np.array([[60, 60, 60]] + [[(37 * k) % 200 + 55, (91 * k) % 200 + 55, (53 * k) % 200 + 55]
                                         for k in range(len(sizes))], np.uint8)
        img = pal[comp + 1]
        raw = b"".join(b"\x00" + img[z].tobytes() for z in range(img.shape[0]))

        def chunk(t, data):
            return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
        with open(png, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", img.shape[1], img.shape[0], 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    return out, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    ap.add_argument("--chapter", type=int, default=38)
    ap.add_argument("--step", type=int, default=64)
    ap.add_argument("--png")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if not args.disc1:
        sys.exit("set SS_DISC1 (or --disc1)")
    out, lines = islands(args.disc1, args.chapter, args.step, args.png)
    print("\n".join(lines))
    print(f"maps behind the islands' entrances: {sorted(out)}")
    if args.check:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ap_late_proof", os.path.join(ROOT, "tools", "ap_late_proof.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        if set(mod.TELEPORTER_ISLANDS) != out:
            print(f"MISMATCH: ap_late_proof.py TELEPORTER_ISLANDS {sorted(mod.TELEPORTER_ISLANDS)}")
            return 1
        print("ap_late_proof.py TELEPORTER_ISLANDS agrees")
    return 0


if __name__ == "__main__":
    sys.exit(main())
