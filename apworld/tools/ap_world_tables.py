#!/usr/bin/env python3
"""The SO2 APWorld's tables (apworld/so2/data.py), generated from docs/apworld/field.json and
docs/apworld/battles.json (docs/apworld/field.md, docs/apworld/apworld.md). Run it after regenerating
field.json:

    apworld/tools/ap_world_tables.py [--field docs/apworld/field.json] [--battles docs/apworld/battles.json]
                             [--late docs/apworld/late_proof.json] [-o apworld/so2/data.py]

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
    member", "never"), and the story_route it needs ("claude" / "rena"), from its
    conditions evaluated with apworld/tools/ap_extract.py's three-valued evaluator;
  - the games in which apworld/tools/ap_late_proof.py proved it reachable after the Final Save Point
    (`late`: chests, gifts, bosses), which decides whether it may hold progression.
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

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # <section>/tools/ -> the repo
ITEM_BASE = LOCATION_BASE              # AP item id = ITEM_BASE + game item (1-1023)
FOL_BASE = LOCATION_BASE + 0x10000     # a Fol bundle: FOL_BASE + amount
# Characters who join only by the player's choice (docs/apworld/field.md, recruitments). Claude,
# Rena and Noel (map 277, the story) are always there.
OPTIONAL = {"Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Chisato"}
# Equipment (accessories 75-162, weapons and armour 485-754) is "useful"; the rest "filler".
USEFUL_RANGES = ((75, 162), (485, 754))
FIENAL = range(647, 686)
CAVE_OF_TRIALS = range(698, 826)
# [cavefix 2026-10-25] The Cave of Trials' learn chests are no item chests: their routine (map
# 740 0x4938) teaches a member a move (A 0x21 known?, ext 0x73 learn) and opens only with that
# member in the party; field.json read its push of the move index (0xc / 9 / 0xd) as items 12 /
# 9 / 13 (Portrait F / G / J). so2.apworld 0.1.x made them chests, whose placeholder patch put
# the index past the record's 0x20 moves (the port 0.02.00 then wrote outside the records).
# Left out from 0.2.0; skillsanity brings them back as learn points, under the floors' banners
# (map: flag -> member, move, area):
LEARN_CHESTS = {(740, 2675): ("Leon", "Extinction", "Cave of Trials Level 4"),
                (798, 2657): ("Ashton", "tri-Ace", "Cave of Trials Level 10"),
                (799, 2721): ("Celine", "Meteor Swarm", "Cave of Trials Level 10")}
# Skillsanity [skillsanity2]: the skill guilds' maps (apworld/so2/skills.py SKILL_SETS; docs/port/notes/menu.md
# "Skill guilds"): Cross, Port of Clik, Herlie, Hilton "Skill Power", Linga, Lacour, Armlock, Central
# City, North City "Boondocks Office". data.GUILD_PLACES: each as a drop's place (its area, group and
# proofs), for the guild checks' access.
GUILD_MAPS = (80, 123, 147, 173, 190, 219, 316, 258, 264)
CHARACTERS =["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest",
              "Noel", "Chisato"]   # party_add's argument n (party id n + 1)
# character_items shuffles the members who join by the player's choice or for part of the story
# only, and lists the sites where each joins for good (docs/apworld/apworld.md, "Characters"; agreed
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
# Open world's party rules (docs/apworld/seed_format.md, "Party v2") make the other hero and Noel
# items too: their first join for good is a join location, listed only when they are not a
# story member of the seed (`open_only`): Rena in Claude's game (map 9, chapter 4 step 2000),
# Claude in Rena's (map 12, chapter 4 step 150), Noel (map 277, chapter 39 step 1000). The
# story route each needs is fixed here (the records' own conditions don't carry flag 1).
OPEN_JOIN_SITES = {"Rena": ((9, 0x6B94), "claude"), "Claude": ((12, 0x6E08), "rena"), "Noel": ((277, 0x6FE0), None)}
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
# Boss encounters (docs/apworld/battles.json, field.json "bosses"), by what the story does with them:
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
# member, a Private Action, a losable fight, a join site, the other story_route) stays
# excluded from progression whatever the option, because two such locations can rule each
# other out and the generator can't tell which one the player will take.
SOFT_REASONS = ("window", "time-limited")


# Location names (with their name groups and the rows' `area`) are the released datapackage's
# (so2.apworld 0.1.0), which named each map's locations after field.json's nearest-name area.
# apworld/tools/ap_extract.py AREA_FIXES later corrected these maps' areas; their locations keep the
# released labels, because renaming a location changes what YAMLs and trackers name and, since
# Archipelago's fill sorts locations by name, every fixed seed's placements.
RELEASED_AREAS = {**dict.fromkeys((27, 29, 32), "Rena's House"), 38: "Rena's Room", 81: "Bar Mikado",
                  83: "Inn Kingdom Hotel", 143: "Town of Herlie",
                  **dict.fromkeys((398, 399, 401), "Town of Armlock"),
                  **dict.fromkeys((426, 427, 434, *range(467, 478)), "Port Hilton"),
                  **dict.fromkeys((593, 594, 595), "North City"), **dict.fromkeys((617, 618, 619), "L'Aqua")}
# The Cave of Trials by its real floors (so2.apworld 0.2.0; the user, 2026-10-25: 0.1.x named every
# cave map "Cave of Trials Level 1", the first floor's banner). Archipelago's trackers read the
# names from each multiworld's own data package, so rooms generated before keep theirs. The floors
# are docs/modding/reference/map_names.csv's (the game's "Cave of Trials Level N" banners and the
# guide's walkthrough, pp. 130-133); two maps belong to no single floor: 710, the treasure vault
# whose doors reach rooms of floors 2, 6 and 7, and 711, the Weird Beast's arena (Puffy's first,
# unwinnable fight; its doors reach floors 5, 6 and 8).
CAVE_FLOORS = {1: ((698, 709), (712, 715)), 2: ((716, 725),), 3: ((726, 736),), 4: ((737, 743),),
               5: ((744, 755),), 6: ((756, 762),), 7: ((763, 774),), 8: ((775, 785),), 9: ((786, 793),),
               10: ((794, 806),), 11: ((807, 808),), 12: ((809, 814), (816, 818)), 13: ((815, 815), (819, 825))}
RELEASED_AREAS.update({m: f"Cave of Trials Level {floor}" for floor, spans in CAVE_FLOORS.items()
                       for lo, hi in spans for m in range(lo, hi + 1)})
RELEASED_AREAS.update({710: "Cave of Trials Treasure Vault", 711: "Cave of Trials Weird Beast Arena"})


def area_label(rec):
    """A field.json record's area as the location names have it (RELEASED_AREAS)."""
    return RELEASED_AREAS.get(rec.get("map"), rec.get("area"))


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
    """(missable reasons, story_route) of a location reached through any of these contexts."""
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


def late_games(late):
    """Location id -> the games (claude / rena) whose late proof checked it."""
    out = collections.defaultdict(list)
    for game, result in sorted(late.get("games", {}).items()):
        for lid in result.get("proven", []):
            out[lid].append(game)
    return out


def build(field, battles, late=None, doors=None):
    proven = late_games(late or {})
    names = {int(k): v for k, v in field["item_names"].items()}
    count = collections.Counter(names.values())
    items = {n: (v if count[v] == 1 else f"{v} ({n})") for n, v in sorted(names.items())}
    worlds = {int(k): v for k, v in field["map_worlds"].items()}
    locs = []

    def contents_name(item, fol):
        return f"{fol} Fol" if fol is not None else items.get(item, f"Item {item}")

    learn = set()
    for c in field["chests"]:
        if c.get("dynamic") or "key_flag" in c:
            continue    # a key item (script flag) stays in place in story mode
        if (c["map"], c["flag"]) in LEARN_CHESTS:
            learn.add((c["map"], c["flag"]))
            continue    # a learn chest (LEARN_CHESTS): no item
        contexts = [{"requires": s.get("requires", []), "via": s.get("via")} for s in c["sites"]]
        reasons, side = judge(contexts, c)
        locs.append({"id": c["location_id"], "type": "chest", "map": c["map"], "flag": c["flag"],
                     "area": area_label(c) or f"Map {c['map']}", "item": c.get("item"), "fol": c.get("fol"),
                     "region": region_of(c["map"], c.get("world", worlds.get(c["map"]))),
                     "sites": [site_row(s["map"], s) for s in c["sites"]],
                     "missable": reasons, "story_route": side})
    if learn != set(LEARN_CHESTS):
        raise SystemExit(f"learn chests not in field.json: {sorted(set(LEARN_CHESTS) - learn)}")
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
                             "area": area_label(g) or f"Map {g['map']}",
                             "item": s["value"] if s["what"] == "item" else None,
                             "fol": s["value"] if s["what"] == "fol" else None,
                             "region": region_of(g["map"], g.get("world", worlds.get(g["map"]))),
                             "sites": [site_row(g["map"], s)], "missable": reasons, "story_route": side,
                             "from_chapter": (g.get("window") or [None])[0]})
    # Boss fights: one location per encounter; its sites' contexts decide the story_route.
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
        # [cavenames 2026-10-25] A cave boss's place is its real floor (RELEASED_AREAS /
        # CAVE_FLOORS, the same table skillsanity2 renamed the cave chests by), not the nearest
        # map's banner text below it: that walk named Phoenix (809) "Level 11" (it's on 12),
        # G-Celesta (815) "Level 12" (on 13), and Weirdbeast (711, its own arena) "Level 1".
        place = area_label(s0) or f"Map {s0['map']}"
        label, place, _ = STORY_BOSSES.get(e) or OPTIONAL_BOSSES.get(e) or (
            " and ".join(dict.fromkeys(b["names"])), place, "")
        region = region_of(s0["map"], s0.get("world", worlds.get(s0["map"])))
        hi = (s0.get("window") or [None, None])[1]
        if region == "Energy Nede" and hi is not None and hi < 36:
            region = "Expel"    # map 463 (Azamgil, chapter 15) is Mars's dungeon (field.md, anomalies)
        locs.append({"id": b["location_id"], "type": "boss", "map": s0["map"], "encounter": e,
                     "area": place, "item": FILLER_ITEM, "fol": None, "region": region, "sites": [],
                     "missable": reasons, "story_route": side, "boss": cls, "label": label})
    # Join sites of the shuffled characters (character_items).
    wanted = {(m, off): who for who, sites in JOIN_SITES.items() for m, off in sites}
    wanted.update({site: who for who, (site, _) in OPEN_JOIN_SITES.items()})
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
            open_only = who in OPEN_JOIN_SITES
            if open_only:
                side = OPEN_JOIN_SITES[who][1]
                reasons = [x for x in reasons if x != "never"]
            locs.append({"id": lid, "type": "join", "map": r["map"], "offset": site["at"], "character": who,
                         "area": area_label(r) or f"Map {r['map']}", "item": FILLER_ITEM, "fol": None,
                         "region": region_of(r["map"], r.get("world", worlds.get(r["map"]))), "sites": [],
                         "missable": reasons, "story_route": side, "open_only": True if open_only else None})
    if found != set(wanted):
        raise SystemExit(f"join sites not in field.json: {sorted(set(wanted) - found)}")
    # First drops (enemy_dropsanity): the enemy types random encounters meet. The location's
    # region is the earliest one where the type is met; it never holds progression (a drop is
    # a dice roll, and some odds are low).
    order = ("Expel", "Energy Nede", "Fienal", "Cave of Trials")
    esites = {(s["map"], s["offset"]): s for s in field.get("encounter_sites", [])}
    for d in field.get("enemy_drops", []):
        places = []
        for m, off in d["sites"]:
            s = esites[(m, off)]
            places.append((region_of(m, s.get("world", worlds.get(m))), s.get("late", True), area_label(s)))
        for w in d["world_maps"]:
            places.append(({1: "Expel", 2: "Energy Nede"}[w], True, WORLD_MAP_NAMES[w]))
        region, _, area = min(places, key=lambda p: order.index(p[0]))
        reasons = [] if any(late for _, late, _ in places) else ["time-limited"]
        drop_maps = sorted({m for m, _ in d["sites"]})
        # The maps where the type is met with no condition on the encounter (no chapter, flag or
        # floor test): the ones the drop's progression logic may count (rules.py drop_places).
        stable = sorted({(m2, esites[(m, off)].get("world", worlds.get(m))) for m, off in d["sites"]
                         if unconditional(esites[(m, off)]) for m2 in esites[(m, off)].get("maps", [m])})
        locs.append({"id": d["location_id"], "type": "drop", "map": None, "enemy": d["enemy"], "site_maps": drop_maps,
                     "on_world_map": bool(d["world_maps"]),
                     "area": area or region, "item": d["drops"][0]["item"], "fol": None, "region": region,
                     "sites": [], "missable": reasons, "story_route": None, "label": d["name"],
                     "regions": sorted({p[0] for p in places}, key=order.index),
                     "odds": [x["odds"] for x in d["drops"]], "stable_maps": stable,
                     "world_maps": list(d["world_maps"])})
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
    for l in locs:
        if l["type"] in ("chest", "gift", "boss"):
            l["late"] = proven.get(l["id"], [])
        if l["type"] == "boss" and l.get("boss") == "story":
            l["chapter"] = STORY_BOSS_CHAPTER[l["encounter"]]
    # Phase 4: the entrance groups, each location's group and proof routes, the shop slots.
    groups = key_groups(doors) if doors else []
    if groups:
        gmap = map_groups(doors, groups)
        late_keys, _ = route_keys(late or {}, groups)
        tele_keys, tele_games = route_keys((late or {}).get("teleporter", {}), groups)
        for l in locs:
            if l["map"] is not None and l["map"] in gmap:
                l["group"], near = gmap[l["map"]]
                if near:
                    l["group_near"] = True
            if l["type"] == "drop":
                l["groups"] = sorted({gmap[m][0] for m in l.pop("site_maps") if m in gmap})
                l["places"] = drop_places(l.pop("stable_maps"), l.pop("world_maps"), gmap, late or {}, groups)
            if l["type"] in ("chest", "gift", "boss"):
                l["late_keys"] = sorted(late_keys.get(l["id"], ()))
                l["tele"] = tele_games.get(l["id"], [])
                l["tele_keys"] = sorted(tele_keys.get(l["id"], ()))
        shops = shop_slots(field, items, worlds, gmap)
        for l in shops:
            l["group"], near = gmap.get(l["map"], (None, False))
            if near:
                l["group_near"] = True
        guilds = {m: dict(drop_places([(m, worlds.get(m))], [], gmap, late or {}, groups)[0],
                          area=field["map_areas"].get(str(m))) for m in GUILD_MAPS}
        names = {l["name"] for l in locs}
        if any(l["name"] in names for l in shops):
            raise SystemExit("a shop slot's name repeats a location's")
        locs += shops
    for l in locs:
        for k in ("site_maps", "stable_maps", "world_maps"):
            l.pop(k, None)
    fol = sorted({l["fol"] for l in locs if l["fol"] is not None})
    goal = battles.get("final_boss_encounters", [539, 540])
    numbers = {g["group"]: g["key"] for g in groups}
    exits = [dict(e, keys=[numbers[x] for x in e["groups"]]) for e in EXIT_GATES] if groups else []
    return {"items": items, "fol": fol, "locations": sorted(locs, key=lambda l: l["id"]), "goal": goal,
            "groups": groups, "gates": story_gates(late or {}, groups, locs) if groups else [], "exits": exits,
            "guilds": guilds if groups else {}}


# ---- Phase 4: open world (area keys, story stages) and shop slots ----------------------------
# One key per world-map entrance group (docs/apworld/doors.json `key_group`, the world agent's 35
# groups of the 66 entrances); key N = the group's place in doors.json order.
KEY_NAMES = {
    "Arlia Village": "Arlia Pass", "Town of Salva": "Salva Pass", "Mars Village": "Mars Pass",
    "Town of Herlie": "Herlie Pass", "Lasguss Mountain": "Lasguss Pass", "Mountain Palace": "Mountain Palace Pass",
    "map 231": "Map 231 Pass", "Hoffmann Ruins": "Hoffmann Pass", "Town of Cross": "Cross Pass",
    "Port of Clik": "Clik Pass", "Port Hilton": "Hilton Pass", "Town of Lacour": "Lacour Pass",
    "Town of Linga": "Linga Pass", "Sanctuary of Linga": "Sanctuary Pass", "Cross Cave": "Cross Cave Pass",
    "Lacour Front Line": "Front Line Pass", "Eluria Tower": "Eluria Tower Pass", "El Beach": "El Beach Pass",
    "Eluria Colony": "Eluria Pass", "Cave of Trials Level 1": "Cave of Trials Pass", "Giveaway": "Giveaway Pass",
    "Town of Armlock": "Armlock Pass", "Field of Intelligence": "Intelligence Pass", "Mihne Cavern": "Mihne Pass",
    "L'Aqua": "L'Aqua Pass", "map 687": "Fienal Pass", "Secret Charity/Fake Gallery": "Gallery Pass",
    "Central City": "Central City Pass", "North City": "North City Pass", "Noel's House": "Noel's House Pass",
    "Cave of Red Crystal": "Red Crystal Pass", "Field of Courage": "Courage Pass", "Field of Power": "Power Pass",
    "Fun City": "Fun City Pass", "Field of Love": "Love Pass",
}
# The story's stages for open world (docs/apworld/apworld.md, "Open world"): the chapters each one
# spans and the entrance groups the story walks into while in it, from every store to the
# chapter / step globals and the area of its map (apworld/tools/ap_extract.py story_points with their
# conditions). A stage is finished when the next one's first chapter is stored, so it lists
# every key its story needs up to that store, from the stage that first walks into the area: a
# key listed a stage late leaves a stage passable without it, and one listed early moves the
# area's early mode (rules.py) before the story can reach it.
STAGES = [
    ("Shingo Forest and Arlia", (3, 4), ["Arlia Village"]),
    ("Salva and Salva Drift", (5, 8), ["Town of Salva", "Arlia Village"]),
    ("Cross", (9, 11), ["Town of Cross"]),
    ("Cross Cave", (12, 13), ["Cross Cave"]),
    ("Clik and Mars", (14, 15), ["Port of Clik", "Mars Village", "Town of Herlie"]),
    # Chapter 17 is stored boarding the ship at Herlie (map 144, flag 18), which lands at Hilton
    # through world door 59578036 (Hilton's group); chapter 18 is stored entering Lacour from the
    # world map (map 909, the entrance routine at 0x47e8). So this stage needs both.
    ("Herlie", (16, 17), ["Town of Herlie", "Port Hilton", "Town of Lacour"]),
    ("Lacour's tournament", (18, 19), ["Town of Lacour"]),   # Lacour Castle's coliseum, maps 426-477
    ("Linga and its sanctuary", (20, 22), ["Town of Linga", "Sanctuary of Linga"]),
    ("Hilton and Hoffmann Ruins", (23, 25), ["Port Hilton", "Town of Lacour", "map 231", "Hoffmann Ruins"]),
    ("Lacour Front Line", (26, 28), ["Lacour Front Line"]),
    ("El", (29, 31), ["El Beach", "Eluria Colony"]),
    ("Eluria Tower", (32, 35), ["Eluria Tower"]),
    ("Energy Nede", (36, 37), []),   # scenes: maps 618, 619, then Central City 248
    ("Synard", (38, 38), ["North City"]),
    ("Noel", (39, 39), ["Noel's House"]),
    ("Cave of Red Crystal", (40, 40), ["Cave of Red Crystal"]),
    ("Central City", (41, 41), ["Central City"]),
    ("The four fields", (42, 45), ["Field of Courage", "Field of Power", "Field of Intelligence", "Field of Love",
                                   "Town of Armlock"]),   # the Field of Love ends at 596, by North City / Armlock
    ("L'Aqua", (46, 47), ["Central City", "L'Aqua"]),
    ("Fienal's first visit", (48, 51), ["map 687", "L'Aqua", "Central City"]),
    ("Armlock and Mihne", (52, 56), ["Town of Armlock", "Mihne Cavern"]),
    ("Fun City", (57, 57), ["Fun City"]),
    ("The last chapters", (58, 60), ["Town of Armlock", "L'Aqua", "map 687"]),
]
DISC_SWITCH_STAGE = 12      # STAGES[:12] are Expel's; the disc switch ends stage 12
# The Expel teleporter (Fun City's arena, open world) is reached from the stage that ends taming
# the Synard: Energy Nede's world map (archive 0x1060 + i) puts Central City and North City on
# one island and Fun City (279, entered only from that map) on another; before the Synard (flag
# 11, set at map 417, the Cave of Red Crystal, where chapter 41 is stored) the party moves only
# by North City's transporter (the director's office, 272) (267 -> Noel's House 278, chapters 38-40). So stage 16.
TELEPORTER_STAGE = 16
# Areas the story reaches only by a ship that lands through another entrance group's door: a
# location's early mode (rules.py open_world_access) needs that group's key as well. The Lacour
# continent: the Herlie ship (flag 18, set on map 144 from chapter 16) lands through world door
# 59578036 in Port Hilton (map 169), the continent's only harbour; Hoffmann Ruins: the Hilton
# ship (flag 22, set on map 169 in chapters 23-27) lands through world door 59578306 on map 231,
# the ruins' beach (flag 21 sails back). Later stages need these keys anyway.
SHIP_LANDINGS = {"Town of Lacour": ["Port Hilton"], "Hoffmann Ruins": ["map 231"]}
# One-way story points an open-world seed gates on keys (docs/apworld/apworld.md, "Open world": the
# El voyage). The El voyage: at chapter 27 step 7000 the Lacour Front Line's meeting room (map
# 444) holds the trigger (thread_start at 0x86d4, routine 0x7c28) whose scene stores chapter 28
# and sends the party to Lacour Castle (422), whose scene sends it to Port Hilton (169), whose
# scene sails to El (548) at chapter 28 step 0: no free play between. El is left only through
# the disc switch, and the Cross and Lacour continents and Hoffmann Ruins' island reopen only
# through the Expel teleporter (chapter 38) or after the Final Save Point. So the stage that
# ends with the voyage needs the El passes (the logic), the port keeps the trigger shut until
# the player holds them (the seed door: the transfer at 0x851c, door 29106503, in that
# routine), and an Expel location the story reaches before the voyage holds progression only
# where the voyage proof (apworld/tools/ap_late_proof.py --state voyage: chapter 27 step 7000, map 444)
# reached it.
STORY_GATES = [
    {"name": "El voyage", "stage": 10, "groups": ["El Beach", "Eluria Colony", "Eluria Tower"],
     "map": 444, "door": 29106503, "routine": 0x7C28, "chapter": 27, "step": 7000, "purpose": "to sail",
     "proof": "voyage", "reopens": TELEPORTER_STAGE},
]
# Exits an open-world seed keeps shut for a while (no logic: the story does not need them then).
# Before the Synard, Energy Nede's islands confine the party (TELEPORTER_STAGE above), and the
# story brings it into two areas without their world-map entrance: Central City (the scenes 619
# -> 248, chapters 36-37; chapter 38 is stored inside it, at 246) and Noel's House (Central
# City's transporter 267 -> 278, chapter 38; chapter 40 is stored inside it, at 277). A player
# who walked out before then would need the area's key to get back in (Central City's is first
# needed at stage 17), and on that island nothing may hold it. So until that chapter the exit
# to the world map (235 -> 2, routine 0x670c; 277 -> 2, routine 0x7164) needs the area's key.
EXIT_GATES = [
    {"name": "Central City exit", "groups": ["Central City"], "map": 235, "door": 15407589, "routine": 0x670C,
     "until": 38, "purpose": "to leave"},
    {"name": "Noel's House exit", "groups": ["Noel's House"], "map": 277, "door": 18160748, "routine": 0x7164,
     "until": 40, "purpose": "to leave"},
]
# A gate's `reopens`: with the Expel teleporter and expel_progression on, the Cross and Lacour
# continents hold any progression (the teleporter brings the player back), but only from the
# Synard (TELEPORTER_STAGE): the gate then also needs the keys of the stages after the disc
# switch up to it (`teleporter_groups`: North City, Noel's House, Cave of Red Crystal), so
# nothing the story needs in between is left behind.
# Story bosses: the chapter of the fight (its stage must be finished to count it).
STORY_BOSS_CHAPTER = {191: 6, 194: 15, 195: 15, 209: 21, 210: 24, 516: 27, 519: 33, 520: 33, 521: 38, 522: 40,
                      524: 42, 525: 42, 527: 42, 528: 42, 531: 55, 532: 57, 533: 57, 535: 58, 536: 60, 537: 60,
                      538: 60}
# Shops whose slots are never locations: 0x58 sells back the books sold to it (its list grows
# with flags 0x194 + item); 0x5a-0x5e are the Familiar's order shops (a specialty).
SHOPS_LEFT_OUT = {0x58, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E}


def stage_of_chapter(ch):
    for k, (_, (lo, hi), _) in enumerate(STAGES):
        if lo <= ch <= hi:
            return k + 1
    return 1 if ch < 3 else len(STAGES)


def key_groups(doors):
    """The 35 entrance groups in doors.json order: key number, name, world, door ids, target maps."""
    out = {}
    for r in doors["doors"]:
        if r.get("world") in (1, 2) and r.get("key_group"):
            g = out.setdefault(r["key_group"], {"group": r["key_group"], "world": r["world"], "doors": [], "maps": []})
            g["doors"].append(r["id"])
            if r["to"]["map"] not in g["maps"]:
                g["maps"].append(r["to"]["map"])
    for n, g in enumerate(out.values(), start=1):
        g["key"], g["name"] = n, KEY_NAMES[g["group"]]
        first = [k + 1 for k, (_, _, keys) in enumerate(STAGES) if g["group"] in keys]
        g["stage"] = first[0] if first else None    # the first stage that needs it (None: not the story's)
        if g["group"] in SHIP_LANDINGS:
            g["landing"] = SHIP_LANDINGS[g["group"]]    # the groups whose keys its ship landing needs
    unknown = ({k for _, _, keys in STAGES for k in keys} | set(SHIP_LANDINGS)
               | {v for vs in SHIP_LANDINGS.values() for v in vs}) - set(out)
    if unknown:
        raise SystemExit(f"stage keys without an entrance group: {unknown}")
    return list(out.values())


def map_groups(doors, groups):
    """Map -> (the entrance group a player walks in by, near): the group whose entrances reach
    the map over unconditional field doors, else (near) the nearest group over any field door,
    scenes included (scene-only maps get one too; rules.py never takes a near group's world
    for the map's)."""
    recs = [r for r in doors["doors"] if not r.get("world_script") and not r.get("world")]
    plain, anyd = collections.defaultdict(set), collections.defaultdict(set)
    for r in recs:
        to = r["to"]["map"]
        if to in (1, 2, 3):
            continue
        uncond = not r["requires"] and (not r["via"] or any(not v["requires"] for v in r["via"]))
        for m in r.get("maps") or [r["map"]]:
            anyd[m].add(to)
            if uncond:
                plain[m].add(to)
    best = {}
    for adj, rank in ((plain, 0), (anyd, 1)):
        for g in groups:
            dist = {m: 0 for m in g["maps"]}
            todo = list(g["maps"])
            while todo:
                nxt = []
                for m in todo:
                    for n in adj[m]:
                        if n not in dist:
                            dist[n] = dist[m] + 1
                            nxt.append(n)
                todo = nxt
            for m, d in dist.items():
                cand = (rank, d, g["key"])
                if m not in best or cand < best[m][0]:
                    best[m] = (cand, g["group"])
    return {m: (g, rank == 1) for m, ((rank, _, _), g) in best.items()}


def story_gates(late, groups, locs):
    """STORY_GATES with each gate's keys (numbers) and, per game, the chest, gift and boss
    locations its proof (late_proof.json's key `proof`) reached at the gate."""
    numbers = {g["group"]: g["key"] for g in groups}
    ids = {l["id"] for l in locs if l["type"] in ("chest", "gift", "boss")}
    out = []
    for gate in STORY_GATES:
        unknown = set(gate["groups"]) - set(numbers)
        if unknown:
            raise SystemExit(f"gate {gate['name']}: groups without a key {unknown}")
        proof = late.get(gate["proof"], {}).get("games", {})
        if not proof:
            print(f"no {gate['proof']} proof in the late proof file: gate {gate['name']!r} lets no location "
                  f"the story reaches before it hold progression")
        g = {k: v for k, v in gate.items() if k not in ("proof", "reopens")}
        g["keys"] = [numbers[x] for x in gate["groups"]]
        later = [x for _, _, keys in STAGES[DISC_SWITCH_STAGE:gate["reopens"]] for x in keys]
        g["teleporter_groups"] = [x for x in dict.fromkeys(later) if x not in gate["groups"]]
        g["teleporter_keys"] = [numbers[x] for x in g["teleporter_groups"]]
        g["proven"] = {game: sorted(set(res.get("proven", [])) & ids) for game, res in sorted(proof.items())}
        out.append(g)
    return out


def unconditional(site):
    """An encounter site with no condition on it or on the calls reaching it."""
    return not site.get("requires") and all(not v.get("requires") for v in site.get("via") or [])


def map_proofs(proof, groups):
    """Map -> (the games whose search reached it, the entrance groups on its routes there: the
    union over the games and the map's nodes), for one proof of late_proof.json."""
    target = {m: g["group"] for g in groups for m in g["maps"]}
    games, keys = collections.defaultdict(list), collections.defaultdict(set)
    for game, res in sorted(proof.get("games", {}).items()):
        for m in res.get("maps", []):
            games[m].append(game)
        for node, route in res.get("routes", {}).items():
            m = int(node.split("@")[0])
            steps = [x.split(":") for x in route.split("|", 1)[1].split(",") if x] if "|" in route else []
            maps_ = [int(a) for a, _ in steps] + [m]
            for k, (a, _) in enumerate(steps):
                if int(a) in (1, 2) and maps_[k + 1] in target:
                    keys[m].add(target[maps_[k + 1]])
    return games, keys


def drop_places(stable, world_maps, gmap, late, groups):
    """A first drop's places for its progression logic (rules.py drop_places): the maps where
    random encounters meet the type with no condition (`stable`: (map, world) pairs), merged
    where they share their area and proofs: region, entrance group (`group_near` as for
    locations), the games whose late / teleporter / voyage proof reached the map and the late and
    teleporter route keys; and each world map the type is met on (`world_map`: 1 Expel, 2 Nede),
    which the logic counts only after the Final Save Point (its encounter cells are not mapped
    to islands)."""
    proofs = {k: map_proofs(p, groups) for k, p in
              (("late", late), ("tele", late.get("teleporter", {})), ("voyage", late.get("voyage", {})))}
    merged = {}
    for m, world in stable:
        if m in (1, 2, 3):
            continue
        group, near = gmap.get(m, (None, False))
        sig = (region_of(m, world), group, near,
               tuple(proofs["late"][0].get(m, ())), tuple(sorted(proofs["late"][1].get(m, ()))),
               tuple(proofs["tele"][0].get(m, ())), tuple(sorted(proofs["tele"][1].get(m, ()))),
               tuple(proofs["voyage"][0].get(m, ())))
        merged.setdefault(sig, []).append(m)
    out = []
    for (region, group, near, lg, lk, tg, tk, vg), maps_ in merged.items():
        place = {"maps": sorted(maps_), "region": region, "group": group}
        if near:
            place["group_near"] = True
        place.update({"late": list(lg), "late_keys": list(lk), "tele": list(tg), "tele_keys": list(tk),
                      "voyage": list(vg)})
        out.append(place)
    out.sort(key=lambda p: p["maps"][0])
    for w in sorted(world_maps):
        out.append({"maps": [w], "world_map": w, "region": {1: "Expel", 2: "Energy Nede"}[w],
                    "late": list(proofs["late"][0].get(w, ()))})
    return out


def route_keys(proof, groups):
    """Location id -> the entrance groups on the proof routes to the map that checked it (union
    over the games and over that map's nodes), and the games that proved it."""
    target = {m: g["group"] for g in groups for m in g["maps"]}
    keys, games = collections.defaultdict(set), collections.defaultdict(list)
    for game, res in sorted(proof.get("games", {}).items()):
        by_map = collections.defaultdict(set)
        for node, route in res.get("routes", {}).items():
            m = int(node.split("@")[0])
            steps = [x.split(":") for x in route.split("|", 1)[1].split(",") if x] if "|" in route else []
            maps_ = [int(a) for a, _ in steps] + [m]
            for k, (a, _) in enumerate(steps):
                if int(a) in (1, 2) and maps_[k + 1] in target:
                    by_map[m].add(target[maps_[k + 1]])
            by_map.setdefault(m, set())
        for m, ids in res.get("checks", {}).items():
            for lid in ids:
                keys[lid] |= by_map.get(int(m), set())
        for lid in res.get("proven", []):
            games[lid].append(game)
    return keys, games


def shop_slots(field, items, worlds, groups_of_map):
    """Every entry of the shops a script opens (minus SHOPS_LEFT_OUT), one location each
    (type 10); `special` when no other of these shops sells the item."""
    sites = collections.defaultdict(list)
    for x in field["shop_sites"]:
        sites[x["shop"]].append(x)
    shops = {int(k): v for k, v in field["shops"].items() if int(k) in sites and int(k) not in SHOPS_LEFT_OUT}
    sellers = collections.Counter(i for v in shops.values() for i in set(v["stock"]))
    out = []
    for shop, v in sorted(shops.items()):
        site = sites[shop][0]
        reasons, side = judge([{"requires": x.get("requires", []), "via": x.get("via")} for x in sites[shop]],
                              {"missable": all(x.get("missable") for x in sites[shop]),
                               "late": any(x.get("late", True) for x in sites[shop])})
        area = area_label(site) or f"Map {site['map']}"
        for index, item in enumerate(v["stock"]):
            out.append({"id": LOCATION_BASE + (10 << 21) + (shop << 8) + index, "type": "shop", "shop": shop,
                        "index": index, "map": site["map"], "area": area, "item": item, "fol": None,
                        "region": region_of(site["map"], site.get("world", worlds.get(site["map"]))),
                        "sites": [], "missable": reasons, "story_route": side, "special": sellers[item] == 1,
                        "name": f"{area}: shop {shop} {items.get(item, item)}"})
    taken = collections.Counter(l["name"] for l in out)
    for l in out:
        if taken[l["name"]] > 1:
            l["name"] = f"{l['name']} #{l['index']}"
    return out


FILLER_ITEM = 757          # Blueberry: what a boss or join location adds to the pool
WORLD_MAP_NAMES = {1: "Expel world map", 2: "Energy Nede world map"}


# ---- Hub mode (world_mode: hub; docs/apworld/seed_format.md "Hub", docs/handoff/hub.md) ---------
# The hub's New Game state (docs/apworld/hub_state.json, apworld/tools/ap_hub_state.py) and its boss chambers
# and dungeon keys (docs/apworld/hub_bosses.json, apworld/tools/ap_hub_bosses.py) are copied into data.py's
# HUB_* tables, which the APWorld writes into a hub seed (`hub`, `bosses`, `doors`). What the hub
# reaches is the hub proof's (docs/apworld/hub_proof.json: apworld/tools/ap_late_proof.py --state hub, the late
# proof run from the hub's New Game with the chapter lock on): HUB_REACHED, the games in which it
# proved each chest, gift and boss; HUB_MAPS, the maps it reached per game; the shops open at the
# hub state and the dungeons where each enemy type is met there, on those maps. Without a hub
# proof, the late proof's reach stands in (its maps; the rows' own `late` games).
HUB_STEP = 4000
# Chamber rows that are no location of story mode (data.LOCATIONS has the other 21 story bosses):
# encounter -> (label, chapter). The Cross Cave guardians are a set battle (523, after the chest
# with flag 2569 on map 547), a chamber in hub mode (D5).
HUB_EXTRA_BOSSES = {523: ("Guardians", 13)}
HUB_STATE_KEYS = ("chapter", "step", "disc", "flags", "globals", "games", "lock")
HUB_BOSS_KEYS = ("encounter", "name", "map", "maps", "view", "game",          # the port's
                 "location", "dungeon", "town", "trigger", "late", "p5")    # reference


def hub_atom(flags, game):
    """An atom's value at the hub state (chapter 60, step HUB_STEP, every flag known: the hub's
    flags set, all others clear; flag 1 per game; not in a Private Action). Globals, members,
    locals: unknown."""
    flags = (set(flags) - {1, 2, 4}) | ({1} if game == "claude" else set())
    values = {"chapter": 60, "step": HUB_STEP}

    def atom(a):
        m = re.fullmatch(r"(chapter|step)\s*(==|!=|>=|<=|<|>)\s*(-?(?:0x[0-9a-f]+|\d+))", a)
        if m:
            v, n = values[m.group(1)], int(m.group(3), 0)
            return {"==": v == n, "!=": v != n, ">=": v >= n, "<=": v <= n, "<": v < n, ">": v > n}[m.group(2)]
        m = re.fullmatch(r"flag\[(\d+)\](?:\s*(==|!=)\s*(\d+))?", a)
        if m:
            v = int(m.group(1)) in flags
            return compare(v, m.group(2), int(m.group(3) or 0))
        return None
    return atom


def door_routine(door, own, doors):
    """A dungeon door's routine as the seed writes it, or None: the port refuses a door with a
    routine at its trigger, which stops every transfer the routine makes, so only when each of
    them leads into the dungeon (docs/apworld/doors.json, same map and routine). Else (Zand's Mansion's
    9314034: its routine 0x77cc also leads to Herlie, 157) the port refuses the transfer alone."""
    routine = int(door["routine"], 16)
    siblings = [r for r in (doors or {}).get("doors", []) if r.get("routine") == routine and r["map"] == door["map"]
                and not r.get("world_script")]
    return door["routine"] if all(r["to"]["map"] in own for r in siblings) else None


def hub_tables(field, late, hub_state, hub_bosses, locs, doors=None, hub_proof=None):
    """data.py's HUB_* tables (write()): the state, the starts, the chamber rows, the dungeon key
    groups, the hub-only location rows (HUB_EXTRA_BOSSES), the shops open at the hub state and,
    per enemy type, the dungeons ("" = a town or a world map) where it is met there; with the hub
    proof (docs/apworld/hub_proof.json) what it reached: the locations per game and the maps."""
    games = ("claude", "rena")
    reach = hub_proof if hub_proof else late
    late_maps = {g: set(reach.get("games", {}).get(g, {}).get("maps", [])) for g in games}
    dungeon = {m: d["dungeon"] for d in hub_bosses["dungeons"] for m in d["maps"]}
    atoms = {g: hub_atom(hub_state["hub"]["flags"], g) for g in games}
    state = {k: hub_state["hub"][k] for k in HUB_STATE_KEYS}
    bosses = [{k: b[k] for k in HUB_BOSS_KEYS if k in b} for b in hub_bosses["bosses"]]
    dungeons = [{"key": d["key"], "name": d["name"], "dungeon": d["dungeon"], "world": d["world"], "maps": d["maps"],
                 "doors": [{"id": x["id"], "map": x["map"], "routine": door_routine(x, set(d["maps"]), doors),
                            "to": x["to"]} for x in d["doors"]],
                 "chambers": d["chambers"]} for d in hub_bosses["dungeons"]]
    ids = {l["id"] for l in locs}
    names = {l["name"] for l in locs}
    rows = []
    for b in hub_bosses["bosses"]:
        if b["location"] in ids:
            continue
        label, chapter = HUB_EXTRA_BOSSES[b["encounter"]]
        area = b.get("area") or f"Map {b['map']}"
        row = {"id": b["location"], "name": f"{area}: {label} boss", "type": "boss", "map": b["map"],
               "encounter": b["encounter"], "boss": "story", "chapter": chapter, "area": area,
               "region": region_of(b["map"], field["map_worlds"].get(str(b["map"]))), "item": FILLER_ITEM, "fol": None,
               "sites": [], "missable": [], "story_route": None,
               "late": [g for g in games if (b.get("late") or {}).get(g)], "hub_only": True}
        if row["name"] in names:
            raise SystemExit(f"hub location name {row['name']!r} repeats a location's")
        rows.append(row)
    # Shops open at the hub state: a site on a map the proof reached whose conditions can hold.
    shops = {}
    for g in games:
        shops[g] = sorted({x["shop"] for x in field["shop_sites"] if x["shop"] not in SHOPS_LEFT_OUT
                           and x["map"] in late_maps[g] and possible(x, atoms[g])})
    # Enemy types: where random encounters meet them at the hub state.
    esites = {(s["map"], s["offset"]): s for s in field.get("encounter_sites", [])}
    drops = {}
    for d in field.get("enemy_drops", []):
        per = {}
        for g in games:
            where = set()
            for m, off in d["sites"]:
                s = esites[(m, off)]
                if possible(s, atoms[g]):
                    where |= {dungeon.get(m2, "") for m2 in s.get("maps", [m]) if m2 in late_maps[g] or m2 in (1, 2, 3)}
            if d["world_maps"]:
                where.add("")
            per[g] = sorted(where)
        drops[d["location_id"]] = per
    starts = hub_state["starts"]    # a list of rows with their `key` (or, before 2026-10-25, a dict by key)
    starts = {r["key"]: r for r in starts} if isinstance(starts, list) else starts
    out = {"state": state, "starts": starts, "bosses": bosses, "dungeons": dungeons, "rows": rows,
           "shops": shops, "drops": drops, "town_chambers": {int(k): v for k, v in hub_bosses["town_chambers"].items()}}
    if hub_proof:
        kinds = {l["id"] for l in locs + rows if l["type"] in ("chest", "gift", "boss")}
        proven = {g: set(hub_proof["games"].get(g, {}).get("proven", [])) for g in games}
        out["reached"] = {i: [g for g in games if i in proven[g]] for i in sorted(kinds) if any(i in proven[g] for g in games)}
        out["maps"] = {g: sorted(late_maps[g]) for g in games}
    return out


def write_hub(hub):
    """data.py's hub section (appended after the open-world tables)."""
    proof = "docs/apworld/hub_proof.json" if hub.get("reached") is not None else "docs/apworld/late_proof.json"
    out = ["", "# ---- Hub mode (world_mode: hub): apworld/tools/ap_world_tables.py from docs/apworld/hub_state.json,",
           f"# docs/apworld/hub_bosses.json, {proof} and docs/apworld/field.json (docs/apworld/seed_format.md",
           "# \"Hub\"). HUB_STATE: the seed's `hub` block but `start` (chapter, step, disc, the flags set at New",
           "# Game, the globals written, the per-game flags / globals, the chapter lock).",
           f"HUB_STATE = {hub['state']!r}",
           "# The start points (hub_start; every town of both worlds, hub_state.json `starts`): key -> the row",
           "# the seed's `hub.start` takes (with its `globals`: the start's Synard words, which the port",
           "# writes after HUB_STATE's).",
           "HUB_STARTS = {"]
    out += [f"    {k!r}: {v!r}," for k, v in hub["starts"].items()]
    out += ["}",
            "# The chambers (the seed's `bosses` rows: the port's keys and the reference keys).",
            "HUB_BOSSES = ["]
    out += [f"    {b!r}," for b in hub["bosses"]]
    out += ["]",
            "# Chambers in towns (no key): map -> town.",
            f"HUB_TOWN_CHAMBERS = {hub['town_chambers']!r}",
            "# The dungeon key groups (D4): key number, item name, dungeon, world, maps, the doors into it",
            "# (id = map * 65536 + offset / 4, the routine of its trigger or None when that routine also",
            "# leads elsewhere, the map it leads to), chambers.",
            "HUB_DUNGEONS = ["]
    out += [f"    {d!r}," for d in hub["dungeons"]]
    out += ["]",
            "# Locations only hub mode has (the chambers that are no story-mode location).",
            "HUB_LOCATIONS = ["]
    out += [f"    {r!r}," for r in hub["rows"]]
    out += ["]",
            "# Per game, the shops open at the hub state (a site on a map the hub proof reached whose",
            "# conditions can hold at chapter 60, step 4000, with the hub's flags).",
            f"HUB_SHOPS = {hub['shops']!r}",
            "# Per first-drop location, per game: the dungeons where random encounters meet the type at the",
            "# hub state (\"\": a town or a world map, no key).",
            "HUB_DROPS = {"]
    out += [f"    {k}: {v!r}," for k, v in sorted(hub["drops"].items())]
    out += ["}"]
    if hub.get("reached") is not None:
        out += ["# The hub proof (docs/apworld/hub_proof.json): per chest, gift and boss it proved, the games in which",
                "# it reached it (a chamber's own game decides for the chambers: hub_bosses.json `game`).",
                "HUB_REACHED = {"]
        out += [f"    {k}: {v!r}," for k, v in sorted(hub["reached"].items())]
        out += ["}",
                "# The maps the hub proof reached, per game (a first drop's places count only there).",
                f"HUB_MAPS = {hub['maps']!r}"]
    out += [""]
    return out


def write(data, path, sources):
    out = ['"""The SO2 APWorld\'s tables. Generated by apworld/tools/ap_world_tables.py from',
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
            "# reasons, the story_route it needs (None: both), late: the games in which it was proven",
            "# reachable after the Final Save Point (docs/apworld/late_proof.json; chests, gifts, bosses).",
            "LOCATIONS = ["]
    for l in data["locations"]:
        row = {k: l.get(k) for k in ("id", "name", "type", "map", "flag", "offset", "encounter", "character", "enemy",
                                      "shop", "index", "special", "boss", "chapter", "from_chapter", "area", "region",
                                      "regions", "group", "group_near", "groups", "on_world_map", "item", "fol", "sites", "missable",
                                      "story_route", "late", "late_keys", "tele", "tele_keys", "odds", "places",
                                      "open_only")}
        row = {k: v for k, v in row.items() if v is not None or k in ("item", "fol", "story_route")}
        out.append(f"    {row!r},")
    out += ["]", "",
            "# Phase 4 (open world): the world-map entrance groups (docs/apworld/doors.json key_group), one key",
            "# each: key number, name, group, world map, door ids (map * 65536 + offset / 4), target maps, the",
            "# first story stage that needs it (None: not the story's), and `landing`: the groups whose keys",
            "# the ship there needs (SHIP_LANDINGS).",
            "KEY_GROUPS = ["]
    out += [f"    {g!r}," for g in data.get("groups", [])]
    out += ["]", "",
            "# The story's stages: name, chapters, the entrance groups the story walks into in it.",
            "STAGES = ["]
    out += [f"    {st!r}," for st in STAGES]
    out += ["]", f"DISC_SWITCH_STAGE = {DISC_SWITCH_STAGE}", ""]
    if data.get("gates"):
        out += ["# Open world: the Expel teleporter is reached once this stage is finished (the Synard).",
                f"TELEPORTER_STAGE = {TELEPORTER_STAGE}", "",
                "# Open world: one-way story points gated on keys (apworld/tools/ap_world_tables.py STORY_GATES): the stage",
                "# that ends with it needs the keys of `groups` (key numbers `keys`), and with the Expel teleporter",
                "# and expel_progression on those of `teleporter_groups` too; the seed door (`door`, in `routine`",
                "# of `map`) stays shut until the player holds them, the banner says `purpose`; `proven`: per",
                "# game, the chests, gifts and bosses its proof reached at the gate (chapter / step).",
                "STORY_GATES = ["]
        for g in data["gates"]:
            head = {k: v for k, v in g.items() if k != "proven"}
            out.append(f"    dict({head!r},")
            out.append("         proven={")
            for game, ids in g["proven"].items():
                out.append(f"             {game!r}: {ids!r},")
            out.append("         }),")
        out += ["]", "",
                "# Open world: exits kept shut until a chapter (apworld/tools/ap_world_tables.py EXIT_GATES; no logic): the",
                "# seed door needs the keys of `groups` while the story's chapter is below `until`.",
                "EXIT_GATES = ["]
        out += [f"    {e!r}," for e in data.get("exits", [])]
        out += ["]", ""]
    if data.get("hub"):
        out = out[:-1] + write_hub(data["hub"])
    if data.get("guilds"):
        out = out[:-1] if out[-1] == "" else out
        out += ["", "# Skillsanity: the skill guilds' maps as places (a drop's place: area, region, group, proofs),",
                "# for the guild checks' access (any guild of the set).",
                "GUILD_PLACES = {"]
        out += [f"    {m}: {p!r}," for m, p in data["guilds"].items()]
        out += ["}", ""]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--field", default=os.path.join(ROOT, "docs/apworld/field.json"))
    ap.add_argument("--battles", default=os.path.join(ROOT, "docs/apworld/battles.json"))
    ap.add_argument("--late", default=os.path.join(ROOT, "docs/apworld/late_proof.json"))
    ap.add_argument("--doors", default=os.path.join(ROOT, "docs/apworld/doors.json"))
    ap.add_argument("--hub-state", default=os.path.join(ROOT, "docs/apworld/hub_state.json"))
    ap.add_argument("--hub-bosses", default=os.path.join(ROOT, "docs/apworld/hub_bosses.json"))
    ap.add_argument("--hub-proof", default=os.path.join(ROOT, "docs/apworld/hub_proof.json"))
    ap.add_argument("-o", "--output", default=os.path.join(ROOT, "apworld/so2/data.py"))
    a = ap.parse_args()
    field = json.load(open(a.field))
    battles = json.load(open(a.battles)) if os.path.exists(a.battles) else {}
    late = json.load(open(a.late)) if os.path.exists(a.late) else {}
    if not late:
        print(f"no late proof ({a.late}): no chest, gift or Cave of Trials boss may hold progression")
    doors = json.load(open(a.doors)) if os.path.exists(a.doors) else None
    data = build(field, battles, late, doors)
    if os.path.exists(a.hub_state) and os.path.exists(a.hub_bosses):
        hub_proof = json.load(open(a.hub_proof)) if os.path.exists(a.hub_proof) else None
        if not hub_proof:
            print(f"no hub proof ({a.hub_proof}): the hub's reach is the late proof's")
        data["hub"] = hub_tables(field, late, json.load(open(a.hub_state)), json.load(open(a.hub_bosses)),
                                 data["locations"], doors, hub_proof)
    else:
        print(f"no hub tables ({a.hub_state}, {a.hub_bosses}): data.py without hub mode")
    write(data, a.output, ["docs/apworld/field.json", "docs/apworld/battles.json", "docs/apworld/late_proof.json",
                           "docs/apworld/doors.json"])
    locs = data["locations"]
    why = collections.Counter(r for l in locs for r in l["missable"])
    kinds = collections.Counter(l["type"] for l in locs)
    print(f"{len(locs)} locations ({', '.join(f'{n} {k}' for k, n in sorted(kinds.items()))}); "
          f"boss classes {dict(collections.Counter(l['boss'] for l in locs if l['type'] == 'boss'))}; "
          f"joins {dict(collections.Counter(l['character'] for l in locs if l['type'] == 'join'))}")
    print(f"missable {sum(1 for l in locs if l['missable'])} "
          f"{dict(why)}; claude-only {sum(1 for l in locs if l['story_route'] == 'claude')}, rena-only "
          f"{sum(1 for l in locs if l['story_route'] == 'rena')}; regions "
          f"{dict(collections.Counter(l['region'] for l in locs))}; Fol bundles {len(data['fol'])}")
    late_rows = [l for l in locs if "late" in l]
    print(f"proven late: both games {sum(1 for l in late_rows if len(l['late']) == 2)}, Claude's only "
          f"{sum(1 for l in late_rows if l['late'] == ['claude'])}, Rena's only "
          f"{sum(1 for l in late_rows if l['late'] == ['rena'])}, neither {sum(1 for l in late_rows if not l['late'])}"
          f" (of {len(late_rows)} chests, gifts and bosses)")
    if data.get("hub"):
        h = data["hub"]
        met = {g: sum(1 for v in h["drops"].values() if v[g]) for g in ("claude", "rena")}
        print(f"hub: {len(h['state']['flags'])} flags, starts {sorted(h['starts'])}, {len(h['bosses'])} chambers, "
              f"{len(h['dungeons'])} key groups, extra rows {[r['name'] for r in h['rows']]}; shops open "
              f"{ {g: len(v) for g, v in h['shops'].items()} }; enemy types met {met}")


if __name__ == "__main__":
    main()
