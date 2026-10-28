"""The door table's validator (docs/apworld/seed_format.md, "Phase 4"): a seed's `doors` against
the game's doors (docs/apworld/doors.json, written by apworld/tools/ap_doors.py from the discs).

Standard library only, no disc access, so the .apworld ships it. The entrance shuffle itself is
the APWorld's; this only checks a table:

    problems = validate_doors(seed_doors, doors_json)    # [] when the table is good

- every door id is one of the game's doors (id = map * 65536 + offset / 4);
- a key is 1-255, and so is each of a door's `keys` (a door that needs them all: a one-way
  story point, the El voyage's);
- a `routine` is the door's own (the routine its trigger starts); `until` is a chapter 1-255;
- a target ("to") is an arrival point of the game: the map and position some door of the
  game itself sends the party to (so nobody lands inside a wall);
- every target map can get back to a world map (1 Expel, 2 Energy Nede, 3) over the doors,
  with the seed's targets applied (conditions are ignored: this is the map graph).

CLI (tests): python3 apworld/so2/doors.py SEED.apso2 [--doors docs/apworld/doors.json]
Written by the world agent.
"""
import collections
import json

WORLD_MAPS = (1, 2, 3)


def door_id(map_id, offset):
    return map_id * 65536 + offset // 4


def _int(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def _seed_door_id(d):
    if "id" in d:
        return _int(d["id"])
    return door_id(_int(d["map"]), _int(d["offset"]))


def _sources(rec):
    """The maps a door leaves from: its world map for a chunk-script entrance, else the maps
    running its script."""
    if rec.get("world"):
        return [rec["world"]]
    return rec.get("maps") or [rec["map"]]


def validate_doors(seed_doors, doors_json):
    """Problems with a seed's door table (a list of strings; empty: good)."""
    records = doors_json["doors"] if isinstance(doors_json, dict) else doors_json
    by_id = {r["id"]: r for r in records}
    arrivals = {(r["to"]["map"], r["to"]["x"], r["to"]["y"], r["to"]["z"]) for r in records
                if None not in (r["to"].get("x"), r["to"].get("y"), r["to"].get("z"))}
    problems = []
    targets = {}
    seen = set()
    for n, d in enumerate(seed_doors):
        where = f"doors[{n}]"
        try:
            did = _seed_door_id(d)
        except (KeyError, ValueError, TypeError):
            problems.append(f"{where}: needs id, or map and offset")
            continue
        if "id" in d and "map" in d and "offset" in d and door_id(_int(d["map"]), _int(d["offset"])) != did:
            problems.append(f"{where}: id {did} is not map * 65536 + offset / 4")
        if did in seen:
            problems.append(f"{where}: door {did} repeated")
        seen.add(did)
        rec = by_id.get(did)
        if rec is None:
            problems.append(f"{where}: door {did} is not a door of the game")
            continue
        if rec.get("world_script"):
            problems.append(f"{where}: door {did} is the world script's warp, not play")
        key = d.get("key", 0)
        if key and not 1 <= _int(key) <= 255:
            problems.append(f"{where}: key {key} is not 1-255")
        if "keys" in d:
            keys = d["keys"]
            if not isinstance(keys, list) or not keys:
                problems.append(f"{where}: keys must be a list of keys")
            else:
                for k in keys:
                    try:
                        ok = 1 <= _int(k) <= 255
                    except (ValueError, TypeError):
                        ok = False
                    if not ok:
                        problems.append(f"{where}: key {k} is not 1-255")
        if "until" in d:
            try:
                ok = 1 <= _int(d["until"]) <= 255
            except (ValueError, TypeError):
                ok = False
            if not ok:
                problems.append(f"{where}: until {d['until']} is not a chapter 1-255")
        if "routine" in d:
            try:
                routine = _int(d["routine"])
            except (ValueError, TypeError):
                routine = None
            if routine is None or routine != rec.get("routine"):
                problems.append(f"{where}: routine {d['routine']} is not door {did}'s ({rec.get('routine')})")
        if "to" in d:
            t = d["to"]
            try:
                spot = (_int(t["map"]), _int(t["x"]), _int(t["y"]), _int(t["z"]))
            except (KeyError, ValueError, TypeError):
                problems.append(f"{where}: to needs map, x, y and z")
                continue
            if spot not in arrivals:
                problems.append(
                    f"{where}: map {spot[0]} at ({spot[1]}, {spot[2]}, {spot[3]}) is not an arrival point of the game")
                continue
            targets[did] = spot[0]
    # The map graph with the seed's targets: which maps get back to a world map.
    back = collections.defaultdict(set)   # target map -> maps leading to it
    for r in records:
        if r.get("world_script"):
            continue
        to = targets.get(r["id"], r["to"]["map"])
        for m in _sources(r):
            back[to].add(m)
    home = set(WORLD_MAPS)
    frontier = list(WORLD_MAPS)
    while frontier:
        m = frontier.pop()
        for prev in back.get(m, ()):
            if prev not in home:
                home.add(prev)
                frontier.append(prev)
    for did, to in sorted(targets.items()):
        if to not in home:
            problems.append(f"door {did}: map {to} never gets back to a world map")
    return problems


def main():
    import argparse
    import os
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description="Check a seed's door table against docs/apworld/doors.json.")
    p.add_argument("seed")
    p.add_argument("--doors", default=os.path.join(here, "..", "..", "docs", "apworld", "doors.json"))
    a = p.parse_args()
    seed = json.load(open(a.seed))
    problems = validate_doors(seed.get("doors", []), json.load(open(a.doors)))
    for line in problems:
        print(line)
    print(f"{len(seed.get('doors', []))} doors, {len(problems)} problem(s)", file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
