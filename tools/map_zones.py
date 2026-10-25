#!/usr/bin/env python3
"""List a field map's floor zones: the world-space triangles the party walks on (the map scene's
cell grid, cell 0 list 14; docs/catalog/formats.md "Map data", docs/notes/field_part3.md 3.1.2,
engine/src/game/field/collision.cpp walk_snap_to_floor). Each zone: index, used, tag, its
y / x / z bounds and its three vertices (x, y, z; y grows downwards). By default only zones that
are not flat at y 0 (stairs, ramps, ledges); --all lists every one.

Usage: tools/map_zones.py MAP [--all] [--disc1 CUE]   (default disc: $SS_DISC1)
Example: tools/map_zones.py 558   (Eluria Tower's stair ramps down to the doors to map 559)
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import unpack_block  # noqa: E402
from so2_data import Disc  # noqa: E402


def zones(disc, map_id):
    data, sections = disc.map_sections(map_id)
    scene = bytes(unpack_block(data, sections[0]))
    cells = struct.unpack_from("<I", scene, 0x80)[0]
    table = cells + 4
    count = struct.unpack_from("<H", scene, table + 14 * 2)[0]
    base = cells + struct.unpack_from("<I", scene, table + 0x20 + 14 * 4)[0]
    out = []
    for i in range(count):
        z = base + i * 0x58
        ymin, ymax, xmin, zmin, xmax, zmax = struct.unpack_from("<6h", scene, z)
        verts = [struct.unpack_from("<3h", scene, z + 0x10 + k * 8) for k in range(3)]
        out.append((i, scene[z + 0xC], scene[z + 0xD], (ymin, ymax), (xmin, xmax), (zmin, zmax), verts))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("map", type=int)
    p.add_argument("--all", action="store_true")
    p.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    a = p.parse_args()
    if not a.disc1:
        raise SystemExit("give --disc1 or set SS_DISC1")
    rows = zones(Disc(a.disc1), a.map)
    print(f"map {a.map}: {len(rows)} floor zones")
    for i, used, tag, y, x, z, verts in rows:
        if a.all or any(v[1] != 0 for v in verts):
            print(f" {i:3d} used {used} tag {tag} y {y[0]}..{y[1]} x {x[0]}..{x[1]} z {z[0]}..{z[1]} v {verts}")


if __name__ == "__main__":
    main()
