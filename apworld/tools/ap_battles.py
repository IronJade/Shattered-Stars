#!/usr/bin/env python3
"""Archipelago, battle side: the scripted battles, the encounters (monsters, bosses), the
final boss and the enemy drops, read straight from the disc images.

  ap_battles.py DISC1 [DISC2] [-o FILE]     writes the JSON tables (stdout without -o)
  ap_battles.py DISC1 [DISC2] --summary     a short human-readable summary

Nothing but numbers and the monsters' own names is written (no game text).

Sources (see docs/apworld/battle.md):
  - Encounter N is archive entry 0x57d + N (load_formation 0x8007344c; the port's
    battle/encounter.cpp): after three -1-terminated lists (sprite sets, palettes, code
    blocks) come 12-byte placements {type, x, z, y, model, flags} until type 0 (flags bit 1:
    a boss; bit 2: starts airborne), then the formation flags (bit 0: no escape; bits 1 / 2:
    the intro type), then one 0x5c-byte stats record per placement (name at +0x4c; drops at
    +0x18 / +0x1a, their odds at +0x37 / +0x38; see so2_mod.ENEMY_FIELDS).
  - Map scripts: ext 0xb7 (battle) takes (encounter, stage, param) from the stack. Every
    map carries a helper routine that pops the three values and calls battle (encounter 0
    means a random encounter), so a scripted battle is a call to that helper after three
    pushes; the encounter is the first. ext 0xed (battle_c) and 0xbc (battle_b) preload a
    battle's data (encounter, stage) before such a call.
"""

import argparse
import json
import os
import re
import struct
import sys

sys.path[:0] = [os.path.dirname(os.path.abspath(__file__)),  # this folder, then the shared tools/
                os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")]
import so2_mod  # noqa: E402

FORMATION_BASE = 0x57D
ENCOUNTERS = 0x234
FIELD_MAPS = 1200


def s16(b, at):
    return struct.unpack_from("<h", b, at)[0]


def u16(b, at):
    return struct.unpack_from("<H", b, at)[0]


def s32(b, at):
    return struct.unpack_from("<i", b, at)[0]


def parse_pack(pack):
    """The placements, formation flags and stats records of an encounter pack, or None."""
    p = 0
    for _ in range(3):
        while p + 4 <= len(pack) and s32(pack, p) != -1:
            p += 8
        p += 4
    members = []
    while p + 12 <= len(pack) and s16(pack, p) != 0 and len(members) < 8:
        members.append({"type": s16(pack, p), "flags": u16(pack, p + 10)})
        p += 12
    if not members or p + 4 > len(pack):
        return None
    fflags = u16(pack, p + 2)
    r = p + 4
    for m in members:
        if r + 0x5C > len(pack):
            return None
        rec = pack[r:r + 0x5C]
        m["stats"] = rec
        r += 0x5C
    return {"members": members, "flags": fflags}


def enemy_name(rec):
    return rec[0x4C:0x5C].split(b"\0")[0].decode("latin-1")


def read_encounters(disc):
    """{encounter: {...}} and {enemy type: {...}} (the first pack holding each type)."""
    encounters, enemies = {}, {}
    for n in range(ENCOUNTERS):
        try:
            pack = disc.entry(FORMATION_BASE + n)
            parsed = parse_pack(pack)
        except (SystemExit, struct.error, IndexError):
            continue
        if parsed is None:
            continue
        members = []
        for m in parsed["members"]:
            rec = m["stats"]
            t = m["type"]
            members.append({"type": t, "name": enemy_name(rec), "boss": bool(m["flags"] & 2)})
            if t not in enemies:
                f = so2_mod.ENEMY_FIELDS
                val = {k: so2_mod.le(rec, o, size) for k, (o, size) in f.items()}
                enemies[t] = {
                    "name": enemy_name(rec), "first_encounter": n,
                    "hp": val["hp"], "level": val["level"], "exp": val["exp"], "fol": val["fol"],
                    "drops": [{"item": val["drop1"], "odds": val["drop1_odds"]},
                              {"item": val["drop2"], "odds": val["drop2_odds"]}],
                }
        encounters[n] = {
            "members": members,
            "boss": any(m["boss"] for m in members),
            "can_escape": not (parsed["flags"] & 1),
            "intro": 2 if parsed["flags"] & 4 else 1 if parsed["flags"] & 2 else 0,
        }
    return encounters, enemies


def functions(ins):
    """[(start index, end index)] of the script's routines (frame .. before the next frame)."""
    starts = [n for n, i in enumerate(ins) if i[2].startswith("frame")]
    return [(s, (starts[k + 1] if k + 1 < len(starts) else len(ins))) for k, s in enumerate(starts)]


FLAG_TEST = re.compile(r"^(push flag|push_eq flag) ([GL])#(0x[0-9a-f]+)")
FLAG_STORE = re.compile(r"^pop_store flag ([GL])#(0x[0-9a-f]+)")
FLAG_TYPED = re.compile(r"^(store|pop_store) flag ([GL])#(0x[0-9a-f]+)")


def flags_in(ins, lo, hi):
    tested, stored = set(), set()
    for i in ins[lo:hi]:
        t = i[2]
        m = FLAG_TEST.match(t)
        if m and m.group(2) == "G":
            tested.add(int(m.group(3), 16))
        m = FLAG_STORE.match(t)
        if m and m.group(1) == "G":
            stored.add(int(m.group(2), 16))
        m = FLAG_TYPED.match(t)
        if m and m.group(2) == "G":
            stored.add(int(m.group(3), 16))
    return sorted(tested), sorted(stored)


def scan_map(blob):
    """The scripted battle call sites and battle preloads of one map script."""
    ins = so2_mod.instructions(blob)
    funcs = functions(ins)
    func_of = {}
    for s, e in funcs:
        for k in range(s, e):
            func_of[k] = (s, e)
    helpers = set()
    for n, i in enumerate(ins):
        if i[2].startswith("battle <3 from stack>") and n in func_of:
            helpers.add(ins[func_of[n][0]][0])
    sites, preloads = [], []
    for n, i in enumerate(ins):
        t = i[2]
        if t.startswith("call") and i[3] in helpers and n >= 3:
            vals = [so2_mod.push_value(ins[n - 3 + k]) for k in range(3)]
            s, e = func_of.get(n, (0, len(ins)))
            tested, stored = flags_in(ins, s, e)
            site = {"offset": i[0], "routine": ins[s][0], "helper": i[3],
                    "flags_tested": tested, "flags_set": stored}
            if None in vals:
                site["dynamic"] = True
                site["args"] = [ins[n - 3 + k][2] for k in range(3)]
            else:
                if vals[0] == 0:
                    continue  # the random-encounter path
                site.update({"encounter": vals[0], "stage": vals[1], "param": vals[2]})
            sites.append(site)
        elif re.match(r"^(battle_c <2 from stack>|battle_b <3 from stack>)", t):
            k = 2 if t.startswith("battle_c") else 3
            vals = [so2_mod.push_value(ins[n - k + j]) for j in range(k)] if n >= k else [None]
            if vals and vals[0] is not None:
                preloads.append({"offset": i[0], "op": t.split(" ")[0], "encounter": vals[0], "stage": vals[1]})
    return sites, preloads


def scan_discs(paths):
    """{(disc, map): (sites, preloads)} for every field map with a scripted battle."""
    out = {}
    for d, path in enumerate(paths, start=1):
        disc = so2_mod.Disc(path)
        for m in range(FIELD_MAPS):
            try:
                blob = disc.map_script(m)
            except SystemExit:
                continue
            try:
                sites, preloads = scan_map(blob)
            except Exception:  # noqa: BLE001 (a map the disassembler can't read)
                continue
            if sites or preloads:
                out[(d, m)] = (sites, preloads)
    return out


FINAL_BOSS_NAMES = ("Indalecio",)


def build(paths):
    disc = so2_mod.Disc(paths[0])
    encounters, enemies = read_encounters(disc)
    maps = scan_discs(paths)
    battles = []
    seen = {}
    for (d, m), (sites, preloads) in sorted(maps.items()):
        for s in sites:
            key = (m, s["offset"])
            if key in seen:  # field data is the same on both discs
                seen[key]["discs"].append(d)
                continue
            b = {"map": m, "discs": [d]}
            b.update(s)
            if "encounter" in s and s["encounter"] in encounters:
                e = encounters[s["encounter"]]
                b["monsters"] = [x["name"] for x in e["members"]]
                b["boss"] = e["boss"]
            seen[key] = b
            battles.append(b)
    preload_list = []
    for (d, m), (sites, preloads) in sorted(maps.items()):
        for p in preloads:
            preload_list.append({"map": m, "disc": d, **p})
    bosses = sorted(n for n, e in encounters.items() if e["boss"])
    final = sorted(n for n, e in encounters.items()
                   if any(x["name"].startswith(FINAL_BOSS_NAMES) for x in e["members"]) and e["boss"])
    return {
        "source": "apworld/tools/ap_battles.py",
        "encounters": {str(n): e for n, e in sorted(encounters.items())},
        "enemy_types": {str(t): e for t, e in sorted(enemies.items())},
        "scripted_battles": battles,
        "battle_preloads": preload_list,
        "boss_encounters": bosses,
        "final_boss_encounters": final,
    }


def summary(data):
    enc = data["encounters"]
    print(f"encounters: {len(enc)}; with a boss: {len(data['boss_encounters'])}; "
          f"enemy types: {len(data['enemy_types'])}")
    print(f"scripted battle call sites: {len(data['scripted_battles'])} "
          f"({sum(1 for b in data['scripted_battles'] if b.get('dynamic'))} with a computed encounter); "
          f"preloads: {len(data['battle_preloads'])}")
    print(f"final boss encounter(s): {data['final_boss_encounters']}")
    for b in data["scripted_battles"]:
        if b.get("dynamic"):
            print(f"  map {b['map']:4d} {b['offset']:#07x}  (computed) {b['args']}")
            continue
        names = ", ".join(b.get("monsters", []))
        print(f"  map {b['map']:4d} {b['offset']:#07x}  encounter {b['encounter']:3d} stage {b['stage']:3d}"
              f"{' BOSS' if b.get('boss') else '     '}  {names}"
              f"  flags tested {[hex(f) for f in b['flags_tested']][:4]} set {[hex(f) for f in b['flags_set']][:4]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("discs", nargs="+", help="disc image(s): DISC1 [DISC2] (.cue / .bin / .iso)")
    ap.add_argument("-o", "--output", help="write the JSON here")
    ap.add_argument("--summary", action="store_true", help="print a summary instead of JSON")
    a = ap.parse_args()
    data = build(a.discs[:2])
    if a.summary:
        summary(data)
        return 0
    text = json.dumps(data, indent=1)
    if a.output:
        with open(a.output, "w") as f:
            f.write(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
