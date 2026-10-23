#!/usr/bin/env python3
"""The SO2 APWorld's tables (apworld/so2/data.py), generated from docs/ap/field.json and
docs/ap/battles.json (docs/ap/field.md, docs/ap/apworld.md). Run it after regenerating
field.json:

    tools/ap_world_tables.py [--field docs/ap/field.json] [--battles docs/ap/battles.json]
                             [-o apworld/so2/data.py]

Story mode's locations are the chests (one per opened flag) and the gift sites that give an
item or Fol (key-item flags stay in place: the story needs them). Optional ones (by the
player's options): the boss fights (location type 9, by encounter), the join sites of the
characters that character_items shuffles (type 4, per site) and the first drop of each enemy
type that random encounters meet (type 8, by enemy type). Each location carries:
  - its check (chest: map + opened flag; gift: map + call site offset), as the seed needs it;
  - its patch sites [map, word, expected, form, role] for apworld/so2/patches.py;
  - its vanilla contents (a game item or a Fol amount), which make the item pool;
  - its region (Expel, Energy Nede, Fienal, Cave of Trials);
  - why it can be missed, if it can ("window", "time-limited", "private action", "optional
    member", "never"), and the protagonist it needs ("claude" / "rena"), from its
    conditions evaluated with tools/ap_extract.py's three-valued evaluator.
Nothing but numbers and names (items, places) is written.
"""

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ap_extract import LOCATION_BASE, possible  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEM_BASE = LOCATION_BASE              # AP item id = ITEM_BASE + game item (1-1023)
FOL_BASE = LOCATION_BASE + 0x10000     # a Fol bundle: FOL_BASE + amount
# Characters who join only by the player's choice (docs/ap/field.md, recruitments). Claude,
# Rena and Noel (map 277, the story) are always there.
OPTIONAL = {"Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Chisato"}
# Equipment (accessories 75-162, weapons and armour 485-754) is "useful"; the rest "filler".
USEFUL_RANGES = ((75, 162), (485, 754))
FIENAL = range(647, 686)
CAVE_OF_TRIALS = range(698, 826)
CHARACTERS = ["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest",
              "Noel", "Chisato"]   # party_add's argument n (party id n + 1)
# character_items shuffles the members who join by the player's choice or for part of the story
# only, and lists the sites where each joins for good (docs/ap/apworld.md, "Characters"; agreed
# with the port side, seed_format.md "Phase 3"). A listed site is a location: the member joins
# there only once their item has arrived. Sites not listed play as the original: a scene taking
# a member along for a while (Celine on map 74 for Cross Cave, which she leaves on map 524
# unless the player keeps her, and again at Mars on 126 / 462; Leon on 437 until 422; Dias on
# 434 and at the tournament, 468 / 477) and the restores after a split (flags 758-769). Noel
# joins in the story and is not shuffled.
#   Celine: Mars, 124 (Claude's game, if she is not in the party) and 125 (flag 112);
#   Dias: Mars's choice (130), the Lacour front line (439), El beach (229, flag 762);
#   Leon: El beach (230, Claude's game; map 234 then asks whether he stays) and the Rena-game
#     exception (229, flag 765);
#   Bowman 208, Precis 209 (a Private Action), Ashton 71 (three states of his quest), Opera
#   374 and 479 (three), Ernest 479 (with Opera, choice 0), Chisato 242.
JOIN_SITES = {
    "Celine": ((124, 0x7058), (125, 0x9AEC)),
    "Bowman": ((208, 0x6BC4),),
    "Dias": ((130, 0x5150), (439, 0x7594), (229, 0x7C9C)),
    "Precis": ((209, 0x8C90),),
    "Ashton": ((71, 0x7348), (71, 0x78B4), (71, 0x7998)),
    "Leon": ((230, 0x6C58), (229, 0x73DC)),
    "Opera": ((374, 0x70F0), (479, 0x6B8C), (479, 0x7C28), (479, 0x8244)),
    "Ernest": ((479, 0x7C30),),
    "Chisato": ((242, 0x6CA4),),
}
# Boss encounters (docs/ap/battles.json, field.json "bosses"), by what the story does with them:
#   story: the main story fights it (a loss is a game over), so its check can't be missed;
#   optional: a side quest's fight (Opera's / Ashton's quest, Lasguss Mountain), which other
#     choices rule out (Ashton excludes Opera);
#   losable: the script lets the party lose and goes on (battle param 0): the tournament's
#     Dias (204), Shin on the ship to El (517), Marsilio's first Fienal fight (530).
# The Cave of Trials' bosses (maps 698-825) are its own floors' fights. Left out: 539 / 540
# (Indalecio, the goal), and 0 / 263 / 534 / 561, which no script starts with a constant
# encounter (unverified how the game reaches them). Each entry: (name, place, why).
STORY_BOSSES = {
    191: ("Alen-Tax", "Salva Drift", "chapter 6"),
    194: ("Varmillion", "Mars", "chapter 15, Claude's game"),
    195: ("Azamgil", "Mars", "chapter 15, Rena's game"),
    209: ("Visseyer", "Sanctuary of Linga", "chapter 21; Prima p. 66"),
    210: ("Harfainx", "Hoffmann Ruins", "chapter 24"),
    516: ("Shin", "Lacour meeting room", "chapter 27"),
    519: ("Shin", "Eluria Tower", ""),
    520: ("Berle", "Eluria Tower", "chapter 33"),
    521: ("Synard", "Synard Plant", "chapter 38"),
    522: ("Arcmene", "Cavern of the Red Crystal", "chapter 40"),
    524: ("Magichand", "Field of Intelligence", "chapter 42"),
    525: ("Guardian", "Field of Power", "chapter 42"),
    527: ("Guardian", "Field of Courage", "chapter 42"),
    528: ("Lover", "Field of Love", "chapter 42"),
    531: ("Bark", "Mihne Cavern", "Prima p. 106"),
    532: ("Marsilio", "Fun City arena", "chapter 57"),
    533: ("Shigeo", "Fun City", "chapter 57"),
    535: ("Berle", "Armlock", "chapter 58"),
    536: ("Ruprecht and Nicolus", "Fienal", "chapter 60"),
    537: ("Vesper and Decus", "Fienal", "chapter 60"),
    538: ("Cyril", "Fienal", "chapter 60"),
}
OPTIONAL_BOSSES = {
    196: ("Flarelizard", "Mountain Palace", "Opera's quest: flag 141, G[0x1be] 2100"),
    197: ("Nightmare", "Mountain Palace", "Ashton's quest: flag 113, G[0x1bc] 2000-2999"),
    199: ("XINE", "Lasguss Mountain", "flag 754"),
    211: ("Ghost", "Hoffmann Ruins", "Opera's quest: G[0x1be] 3000"),
}
# Reasons that a diligent player can avoid (get it before the story closes it): missable
# "include" makes these ordinary locations. Every other reason (a choice, an exclusive party
# member, a Private Action, a losable fight, a join site, the other protagonist) stays
# excluded from progression whatever the option, because two such locations can rule each
# other out and the generator can't tell which one the player will take.
SOFT_REASONS = ("window", "time-limited")


def compare(v, op, n):
    if op is None:
        return v
    return (v == bool(n)) if op == "==" else (v != bool(n))


def scenario(flag1=None, solo=False):
    """An atom function for possible(): flag 1 fixed (True: Claude's game, False: Rena's), and
    with solo the optional members absent and Private Action mode off. Anything else unknown."""
    def atom(a):
        m = re.fullmatch(r"flag\[(\d+)\](?:\s*(==|!=)\s*(\d+))?", a)
        if m:
            f = int(m.group(1))
            if f == 1 and flag1 is not None:
                return compare(flag1, m.group(2), int(m.group(3) or 0))
            if f in (2, 4) and solo:
                return compare(False, m.group(2), int(m.group(3) or 0))
            return None
        m = re.fullmatch(r"(saved_party_has|has)\((\w+)\)(?:\s*(==|!=)\s*(\d+))?", a)
        if m and solo and (m.group(1) == "saved_party_has" or m.group(2) in OPTIONAL):
            return compare(False, m.group(3), int(m.group(4) or 0))
        return None
    return atom


CLAUDE, RENA, SOLO = scenario(flag1=True), scenario(flag1=False), scenario(solo=True)


def judge(contexts, rec):
    """(missable reasons, protagonist) of a location reached through any of these contexts."""
    reasons = []
    if rec.get("missable"):
        reasons.append("window")
    if not rec.get("late", True):
        reasons.append("time-limited")
    if rec.get("private_action"):
        reasons.append("private action")
    c = any(possible(x, CLAUDE) for x in contexts)
    r = any(possible(x, RENA) for x in contexts)
    if not any(possible(x, SOLO) for x in contexts) and "private action" not in reasons:
        reasons.append("optional member")
    side = None
    if c and not r:
        side = "claude"
    elif r and not c:
        side = "rena"
    elif not c and not r:
        reasons.append("never")
    return reasons, side


def region_of(m, world):
    if m in CAVE_OF_TRIALS:
        return "Cave of Trials"
    if m in FIENAL:
        return "Fienal"
    return "Expel" if world == 1 else "Energy Nede"


def site_row(m, s):
    return [m, s["word"], s["expected"], s["form"], s["role"]]


def build(field, battles):
    names = {int(k): v for k, v in field["item_names"].items()}
    count = collections.Counter(names.values())
    items = {n: (v if count[v] == 1 else f"{v} ({n})") for n, v in sorted(names.items())}
    worlds = {int(k): v for k, v in field["map_worlds"].items()}
    locs = []

    def contents_name(item, fol):
        return f"{fol} Fol" if fol is not None else items.get(item, f"Item {item}")

    for c in field["chests"]:
        if c.get("dynamic") or "key_flag" in c:
            continue    # a key item (script flag) stays in place in story mode
        contexts = [{"requires": s.get("requires", []), "via": s.get("via")} for s in c["sites"]]
        reasons, side = judge(contexts, c)
        locs.append({"id": c["location_id"], "type": "chest", "map": c["map"], "flag": c["flag"],
                     "area": c.get("area") or f"Map {c['map']}", "item": c.get("item"), "fol": c.get("fol"),
                     "region": region_of(c["map"], c.get("world", worlds.get(c["map"]))),
                     "sites": [site_row(s["map"], s) for s in c["sites"]],
                     "missable": reasons, "protagonist": side})
    for key in ("gifts", "fol_gifts"):
        for g in field[key]:
            if g.get("world_script") or g.get("payment") or "location_id" not in g:
                continue
            reasons, side = judge([{"requires": g.get("requires", []), "via": g.get("via")}], g)
            for k, s in enumerate(g["sites"]):
                if s["what"] not in ("item", "fol"):
                    continue    # key-item flags stay in place in story mode
                lid = s.get("location_id", g["location_id"] if k == 0 else None)
                if lid is None:
                    raise SystemExit(f"map {g['map']} site {s['at']:#x}: no location id")
                locs.append({"id": lid, "type": "gift", "map": g["map"], "offset": s["at"],
                             "area": g.get("area") or f"Map {g['map']}",
                             "item": s["value"] if s["what"] == "item" else None,
                             "fol": s["value"] if s["what"] == "fol" else None,
                             "region": region_of(g["map"], g.get("world", worlds.get(g["map"]))),
                             "sites": [site_row(g["map"], s)], "missable": reasons, "protagonist": side})
    # Boss fights: one location per encounter; its sites' contexts decide the protagonist.
    final = set(battles.get("final_boss_encounters", [539, 540]))
    for b in field.get("bosses", []):
        e = b["encounter"]
        if e in final or not b["sites"]:
            continue
        maps_ = sorted({s["map"] for s in b["sites"]})
        if maps_[0] in CAVE_OF_TRIALS:
            cls = "cave"
        elif all(s.get("param") == 0 for s in b["sites"]):
            cls = "losable"
        elif e in STORY_BOSSES:
            cls = "story"
        elif e in OPTIONAL_BOSSES:
            cls = "optional"
        else:
            raise SystemExit(f"boss encounter {e} ({b['names']}) is not classified")
        if cls != "losable" and any(s.get("param") == 0 for s in b["sites"]):
            raise SystemExit(f"boss encounter {e}: a losable site in a {cls} fight")
        contexts = [{"requires": s.get("requires", []), "via": s.get("via")} for s in b["sites"]]
        reasons, side = judge(contexts, {"missable": all(s.get("missable") for s in b["sites"]),
                                         "late": any(s.get("late", True) for s in b["sites"])})
        if cls in ("story", "cave"):
            reasons = []        # the story (or the cave's way down) fights it
        elif cls == "losable":
            reasons = ["losable"]
        else:
            reasons = ["optional quest"] + [r for r in reasons if r != "optional member"]
        s0 = b["sites"][0]
        place = s0.get("area") or f"Map {s0['map']}"
        if cls == "cave":   # the floor's banner (the nearest map at or below with one)
            banners = field.get("map_names", {})
            m = next((m for m in range(s0["map"], CAVE_OF_TRIALS.start - 1, -1) if banners.get(str(m))), None)
            place = banners[str(m)][0] if m is not None else place
        label, place, _ = STORY_BOSSES.get(e) or OPTIONAL_BOSSES.get(e) or (
            " and ".join(dict.fromkeys(b["names"])), place, "")
        region = region_of(s0["map"], s0.get("world", worlds.get(s0["map"])))
        hi = (s0.get("window") or [None, None])[1]
        if region == "Energy Nede" and hi is not None and hi < 36:
            region = "Expel"    # map 463 (Azamgil, chapter 15) is Mars's dungeon (field.md, anomalies)
        locs.append({"id": b["location_id"], "type": "boss", "map": s0["map"], "encounter": e,
                     "area": place, "item": FILLER_ITEM, "fol": None, "region": region, "sites": [],
                     "missable": reasons, "protagonist": side, "boss": cls, "label": label})
    # Join sites of the shuffled characters (character_items).
    wanted = {(m, off): who for who, sites in JOIN_SITES.items() for m, off in sites}
    found = set()
    for r in field["recruitments"]:
        if r.get("world_script") or "location_id" not in r:
            continue
        for k, site in enumerate(r["sites"]):
            who = wanted.get((r["map"], site["at"]))
            if who is None:
                continue
            if site.get("what") != "join" or site.get("value") != CHARACTERS.index(who):
                raise SystemExit(f"map {r['map']} {site['at']:#x}: not {who}'s join ({site})")
            found.add((r["map"], site["at"]))
            lid = site.get("location_id", r["location_id"] if k == 0 else None)
            if lid is None:
                raise SystemExit(f"map {r['map']} join site {site['at']:#x}: no location id")
            reasons, side = judge([{"requires": r.get("requires", []), "via": r.get("via")}], r)
            reasons = ["join"] + [x for x in reasons if x != "optional member"]
            locs.append({"id": lid, "type": "join", "map": r["map"], "offset": site["at"], "character": who,
                         "area": r.get("area") or f"Map {r['map']}", "item": FILLER_ITEM, "fol": None,
                         "region": region_of(r["map"], r.get("world", worlds.get(r["map"]))), "sites": [],
                         "missable": reasons, "protagonist": side})
    if found != set(wanted):
        raise SystemExit(f"join sites not in field.json: {sorted(set(wanted) - found)}")
    # First drops (drops_as_checks): the enemy types random encounters meet. The location's
    # region is the earliest one where the type is met; it never holds progression (a drop is
    # a dice roll, and some odds are low).
    order = ("Expel", "Energy Nede", "Fienal", "Cave of Trials")
    esites = {(s["map"], s["offset"]): s for s in field.get("encounter_sites", [])}
    for d in field.get("enemy_drops", []):
        places = []
        for m, off in d["sites"]:
            s = esites[(m, off)]
            places.append((region_of(m, s.get("world", worlds.get(m))), s.get("late", True), s.get("area")))
        for w in d["world_maps"]:
            places.append(({1: "Expel", 2: "Energy Nede"}[w], True, WORLD_MAP_NAMES[w]))
        region, _, area = min(places, key=lambda p: order.index(p[0]))
        reasons = [] if any(late for _, late, _ in places) else ["time-limited"]
        locs.append({"id": d["location_id"], "type": "drop", "map": None, "enemy": d["enemy"],
                     "area": area or region, "item": d["drops"][0]["item"], "fol": None, "region": region,
                     "sites": [], "missable": reasons, "protagonist": None, "label": d["name"],
                     "regions": sorted({p[0] for p in places}, key=order.index)})
    # Names: "AREA: CONTENTS chest|gift"; "AREA: NAME boss"; "AREA: CHARACTER joins";
    # "NAME first drop"; made unique with the map, then a number.
    base = {}
    for l in locs:
        if l["type"] == "boss":
            base[l["id"]] = f"{l['area']}: {l['label']} boss"
        elif l["type"] == "join":
            base[l["id"]] = f"{l['area']}: {l['character']} joins"
        elif l["type"] == "drop":
            base[l["id"]] = f"{l['label']} first drop"
        else:
            base[l["id"]] = f"{l['area']}: {contents_name(l['item'], l['fol'])} {l['type']}"
    seen = collections.Counter(base.values())
    taken = collections.Counter()
    for l in sorted(locs, key=lambda l: l["id"]):
        name = base[l["id"]]
        if seen[name] > 1:
            name = f"{name} (map {l['map']})" if l["map"] is not None else f"{name} (type {l['enemy']})"
        taken[name] += 1
        if taken[name] > 1:
            name = f"{name} #{taken[name]}"
        l["name"] = name
    if len({l["name"] for l in locs}) != len(locs) or len({l["id"] for l in locs}) != len(locs):
        raise SystemExit("location names or ids repeat")
    fol = sorted({l["fol"] for l in locs if l["fol"] is not None})
    goal = battles.get("final_boss_encounters", [539, 540])
    return {"items": items, "fol": fol, "locations": sorted(locs, key=lambda l: l["id"]), "goal": goal}


FILLER_ITEM = 757          # Blueberry: what a boss or join location adds to the pool
WORLD_MAP_NAMES = {1: "Expel world map", 2: "Energy Nede world map"}


def write(data, path, sources):
    out = ['"""The SO2 APWorld\'s tables. Generated by tools/ap_world_tables.py from',
           f'{", ".join(sources)}; do not edit (regenerate)."""', "",
           f"ITEM_BASE = {ITEM_BASE:#x}  # AP item id = ITEM_BASE + game item",
           f"FOL_BASE = {FOL_BASE:#x}   # a Fol bundle: FOL_BASE + amount",
           f"USEFUL_RANGES = {USEFUL_RANGES!r}",
           f"GOAL_ENCOUNTERS = {data['goal']!r}",
           f"FOL_AMOUNTS = {data['fol']!r}", "",
           "# Game items by number (names made unique with the number where the game repeats one).",
           "ITEMS = {"]
    out += [f"    {n}: {name!r}," for n, name in data["items"].items()]
    out += ["}", "",
            "# One location per line: id, name, type (chest / gift / boss / join / drop), map, flag (chest) /",
            "# offset (gift, join) / encounter (boss) / enemy (drop), character (join), boss class, area,",
            "# region, vanilla item or Fol, patch sites [map, word, expected, form, role], missable",
            "# reasons, the protagonist it needs (None: both).",
            "LOCATIONS = ["]
    for l in data["locations"]:
        row = {k: l.get(k) for k in ("id", "name", "type", "map", "flag", "offset", "encounter", "character", "enemy",
                                      "boss", "area", "region", "regions", "item", "fol", "sites", "missable",
                                      "protagonist")}
        row = {k: v for k, v in row.items() if v is not None or k in ("item", "fol", "protagonist")}
        out.append(f"    {row!r},")
    out += ["]", ""]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--field", default=os.path.join(ROOT, "docs/ap/field.json"))
    ap.add_argument("--battles", default=os.path.join(ROOT, "docs/ap/battles.json"))
    ap.add_argument("-o", "--output", default=os.path.join(ROOT, "apworld/so2/data.py"))
    a = ap.parse_args()
    field = json.load(open(a.field))
    battles = json.load(open(a.battles)) if os.path.exists(a.battles) else {}
    data = build(field, battles)
    write(data, a.output, ["docs/ap/field.json", "docs/ap/battles.json"])
    locs = data["locations"]
    why = collections.Counter(r for l in locs for r in l["missable"])
    kinds = collections.Counter(l["type"] for l in locs)
    print(f"{len(locs)} locations ({', '.join(f'{n} {k}' for k, n in sorted(kinds.items()))}); "
          f"boss classes {dict(collections.Counter(l['boss'] for l in locs if l['type'] == 'boss'))}; "
          f"joins {dict(collections.Counter(l['character'] for l in locs if l['type'] == 'join'))}")
    print(f"missable {sum(1 for l in locs if l['missable'])} "
          f"{dict(why)}; claude-only {sum(1 for l in locs if l['protagonist'] == 'claude')}, rena-only "
          f"{sum(1 for l in locs if l['protagonist'] == 'rena')}; regions "
          f"{dict(collections.Counter(l['region'] for l in locs))}; Fol bundles {len(data['fol'])}")


if __name__ == "__main__":
    main()
