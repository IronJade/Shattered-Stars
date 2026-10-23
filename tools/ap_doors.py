#!/usr/bin/env python3
"""The door table for Archipelago's open world and entrance shuffle (docs/ap/seed_format.md,
"Phase 4"): every map transfer in the map scripts with a constant target.

    tools/ap_doors.py DISC1.cue [--field docs/ap/field.json] [-o docs/ap/doors.json]

A door is one map-transfer instruction (script ext 0x7d map_transfer, 0xa4 map_transfer_b,
0xda map_transfer_c), inline or with its arguments pushed as constants. Its id is
`map * 65536 + offset / 4`: the script's map (the lowest map of a shared script: 909 stands
for Expel's world-map chunks 909-980, 1048 for Energy Nede's 1048-1110) and the instruction's
word index (byte offset / 4; the largest on the discs is 16967). Ids never change while the
discs and this rule stay the same. Doors are not Archipelago locations.

Each record:
  id, map, maps (all maps running that script), offset, routine (the byte offset of the
  routine's frame: what a trigger region or talk handler starts), op, form (inline / stack),
  to: {map, transition, x, y, z, dir, extra} (extra: map_transfer_b's game_state +0x1e value,
  map_transfer_c's arrival parameter; null where not constant),
  requires / via (conditions, as tools/ap_extract.py writes them for transfers),
  world (1 / 2 / 3 for a world-map entrance from a chunk script), world_script (the world
  script's warp list: not play), from_area / to_area (field.json map_areas),
  key_group (world-map entrances: the place the door enters, named by the target's banner, else
  its area; a suggestion for area keys).

Also `arrivals`: every distinct target (map, x, y, z, dir) with the doors that use it, so an
entrance shuffle only sends doors to places the game itself arrives at.

The tables are the same on both discs (all 820 scripts are byte-identical), so disc 1 is read.
Python 3, no third-party modules. Written by the world agent.
"""

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ap_extract  # noqa: E402

OPS = {"map_transfer": 6, "map_transfer_b": 7, "map_transfer_c": 7}


def door_id(map_id, offset):
    return map_id * 65536 + offset // 4


def transfer_args(mp, n):
    """(op, form, [args]) of instruction n when it is a map transfer, else None. Stack forms
    give None for arguments that are not pushed constants."""
    i = mp.ins[n]
    text = i[2]
    m = ap_extract.TRANSFER.match(text)
    if m and len(i[1]) >= 4:
        op = m.group(1)
        w = i[1]
        args = [w[0] & 0xFFFF] + [half for pair in w[1:] for half in (pair & 0xFFFF, pair >> 16)]
        args = [a - 0x10000 if a & 0x8000 else a for a in args[:OPS[op]]]
        return op, "inline", args
    m = ap_extract.TRANSFER_STACK.match(text)
    if m:
        op = m.group(1)
        count = int(m.group(2))
        return op, "stack", ap_extract.args_before(mp.ins, n, count)
    return None


def routine_of(mp, n):
    k = mp.func_of.get(n)
    return mp.ins[mp.funcs[k][0]][0] if k is not None else None


def build(disc_path, field):
    _, maps = ap_extract.load_maps(disc_path)
    areas = field.get("map_areas", {}) if field else {}
    banners = field.get("map_names", {}) if field else {}
    doors = []
    for mp in sorted(maps.values(), key=lambda m: m.id):
        for n, i in enumerate(mp.ins):
            got = transfer_args(mp, n)
            if got is None:
                continue
            op, form, args = got
            if args[0] is None or args[0] < 0:
                continue  # a computed target: not a door
            args = args + [None] * (7 - len(args))
            to = {"map": args[0], "transition": args[1], "x": args[2], "y": args[3], "z": args[4],
                  "dir": args[5], "extra": args[6] if op != "map_transfer" else None}
            rec = {"id": door_id(mp.id, i[0]), "map": mp.id}
            if len(mp.maps) > 1:
                rec["maps"] = mp.maps
            rec.update({"offset": i[0], "routine": routine_of(mp, n), "op": op, "form": form, "to": to})
            k = mp.func_of.get(n)
            if k is not None:
                rec.update(ap_extract.context(mp, k, n, depth=1))
            if mp.id in ap_extract.CHUNK_GROUPS:
                rec["world"] = ap_extract.CHUNK_GROUPS[mp.id]
            if mp.id == ap_extract.WORLD_SCRIPT:
                rec["world_script"] = True
            rec["from_area"] = areas.get(str(mp.id))
            rec["to_area"] = areas.get(str(to["map"]))
            if "world" in rec:
                banner = (banners.get(str(to["map"])) or [None])[0]
                rec["key_group"] = banner or rec["to_area"] or f"map {to['map']}"
            doors.append(rec)
    arrivals = collections.OrderedDict()
    for d in doors:
        t = d["to"]
        if None in (t["x"], t["y"], t["z"]):
            continue
        key = (t["map"], t["x"], t["y"], t["z"], t["dir"])
        arrivals.setdefault(key, []).append(d["id"])
    return {
        "source": "tools/ap_doors.py",
        "id_formula": "map * 65536 + offset / 4",
        "doors": doors,
        "arrivals": [{"map": k[0], "x": k[1], "y": k[2], "z": k[3], "dir": k[4], "doors": v}
                     for k, v in sorted(arrivals.items(), key=lambda kv: kv[0][:4])],
    }


def write(data, path):
    """One door per line (the field.json style), so diffs stay readable."""
    with open(path, "w", newline="\n") as f:
        f.write("{\n")
        keys = list(data)
        for ki, k in enumerate(keys):
            v = data[k]
            end = "," if ki + 1 < len(keys) else ""
            if isinstance(v, list):
                f.write(f' "{k}": [\n')
                for j, rec in enumerate(v):
                    f.write("  " + json.dumps(rec, separators=(",", ":")) + ("," if j + 1 < len(v) else "") + "\n")
                f.write(f" ]{end}\n")
            else:
                f.write(f" {json.dumps(k)}: {json.dumps(v)}{end}\n")
        f.write("}\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("disc")
    p.add_argument("--field", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "ap", "field.json"))
    p.add_argument("-o", "--output")
    a = p.parse_args()
    field = json.load(open(a.field)) if a.field and os.path.exists(a.field) else None
    data = build(a.disc, field)
    if a.output:
        write(data, a.output)
    ents = [d for d in data["doors"] if "world" in d]
    print(f"doors: {len(data['doors'])} ({sum(d['form'] == 'stack' for d in data['doors'])} stack form), "
          f"world-map entrances: {len(ents)} in {len({d['key_group'] for d in ents})} groups, "
          f"arrival points: {len(data['arrivals'])}", file=sys.stderr)


if __name__ == "__main__":
    main()
