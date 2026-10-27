#!/usr/bin/env python3
"""ap_batch_check.py OUTDIR: the release workflow's checker (tools/ap_batch.sh; docs/ap/apworld.md
"Testing"). Checks every .apso2 under OUTDIR/<seed>/ (shape, ids, patches, where progression
went, the party, drops, crafts, killer moves, skillsanity, the goals and the version rule; hub
seeds through check_hub_file: the hub block, the chambers, the dungeon doors, the Fienal gate,
what the hub reaches) and prints a summary: "N seeds files, M with errors; ...", then the option
kinds seen. Run with the AP venv from the Archipelago checkout that has worlds/so2 (it imports
worlds.so2), or set SO2_AP_DIR to it:
    cd scratchpad/ap/Archipelago && ../ap-venv/bin/python ../../../tools/ap_batch_check.py OUTDIR
History: scratchpad/apworld2/batch/check.py (agents apworld2, hubworld, charselect, difficulty,
skillsanity2, 2026-10-23..25), moved here by aptests (2026-10-26) with its paths made relative;
the checks are unchanged."""
import collections
import glob
import importlib.util
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))     # this repo
spec = importlib.util.spec_from_file_location("data", os.path.join(ROOT, "apworld/so2/data.py"))
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)
sys.path.insert(0, os.environ.get("SO2_AP_DIR", os.getcwd()))     # the Archipelago checkout
os.environ.setdefault("SKIP_REQUIREMENTS_UPDATE", "1")
from worlds.so2.locations import ALL_ROWS  # noqa: E402
from worlds.so2.rules import MIN_DROP_CHANCE, drop_rule  # noqa: E402
ROW = {r["id"]: r for r in ALL_ROWS}
from worlds.so2 import skillsanity as SK  # noqa: E402  [skillsanity2]


def sk_may_hold(row, s, it):
    """[skilllogic2] Whether a guild check or learn point may hold this progression item (the user,
    2026-10-27; docs/ap/apworld.md "Skillsanity"), worked out here from the options and data.py, not
    from the world's code: nothing with encounter_rate 0 (no Fol, no levels); a learn point only at a
    learn-table level of a member the logic counts on (sure: the heroes, the starting character, and
    Noel under the story's party rules; hub mode only a one-hero route's hero; or a member of the
    character-item pool); a guild check where one of its set's guilds could hold it as a chest on the
    guild's map would: story mode as `allowed`, open world as `allowed_open` and never a guild the
    story reaches before the El voyage that the voyage proof didn't, hub mode a guild whose map the
    hub proof reached in every game (outside Expel with expel_progression off)."""
    o = s["options"]
    if o["encounter_rate"] == 0:
        return False
    side = None if o["story_route"] == "any" else o["story_route"]
    games = ("claude", "rena") if side is None else (side,)
    heroes = {"Claude", "Rena"} if side is None else {side.capitalize()}
    start = o["starting_character"]
    if isinstance(start, list):     # "any": the player's pick; the logic counts on nobody extra (take_choices)
        start = None
    if row["type"] == "learn":
        if row.get("how") != "level":
            return False
        if o["world_mode"] == "hub":
            sure = (heroes if side else set()) | {start}
        elif o["party_rules"] == "open":
            sure = heroes | {start}
        else:
            sure = {"Claude", "Rena", "Noel", start}
        return row["character"] in sure or row["character"] in o["characters"]
    expel = o["expel_progression"]
    places = [data.GUILD_PLACES[m] for m in row["guild_maps"]]
    if o["world_mode"] == "hub":
        return any(all(m in data.HUB_MAPS[g] for g in games for m in p["maps"]) and (expel or p["region"] != "Expel")
                   for p in places)
    rows = [(p, {"type": "chest", "id": None, "region": p["region"], "group": p.get("group"),
                 "group_near": p.get("group_near"), "late": p.get("late", []), "tele": p.get("tele", [])})
            for p in places]
    if o["world_mode"] == "open_world":
        return any(allowed_open(pr, side, expel, o["expel_teleporter"], it, s["slot"]["id"])
                   and not (before_gate(pr) and not all(g in p.get("voyage", ()) for g in games)) for p, pr in rows)
    return any(allowed(pr, side, expel) for _, pr in rows)


def check_skillsanity(s, errs):
    """[skillsanity2] Skillsanity: the guild checks and learn points by the option (ids, checks,
    progression only where sk_may_hold allows: skilllogic2, 2026-10-27), the item kinds, no first
    move as an item, the shop prices."""
    o = s["options"]
    mode = o.get("skillsanity", "none")
    skills_on, specials_on = mode in ("skills_only", "insanity"), mode in ("special_only", "insanity")
    kinds = {v["kind"] for v in s["items"].values()}
    if ("skill" in kinds) != skills_on and skills_on:
        errs.append("skillsanity: no skill item")
    if not skills_on and "skill" in kinds:
        errs.append("skillsanity: skill items without skills")
    if not specials_on and "move" in kinds:
        errs.append("skillsanity: move items without special")
    for v in s["items"].values():
        if v["kind"] == "move" and SK.FIRST.get(NAMES[v["character"] - 1]) == v["index"]:
            errs.append(f"skillsanity: a first move as an item ({v['name']})")
    guild = 0
    o["_sk_prog"] = collections.Counter()
    for l in s["locations"]:
        row = ROW.get(l["id"])
        if row is None:
            continue
        it = l["item"]
        if row["type"] in ("guild", "learn"):
            if l["check"] != SK.seed_check(row):
                errs.append(f"{row['name']}: skillsanity check")
            if it.get("classification") == "progression":
                o["_sk_prog"][row["type"]] += 1
                if not sk_may_hold(row, s, it):
                    errs.append(f"{row['name']}: progression at a skillsanity location")
            if row["type"] == "guild":
                guild += 1
                if not skills_on or l["id"] != 0x50320000 + (13 << 21) + (row["set"] << 8) + row["index"]:
                    errs.append(f"{row['name']}: guild check")
            elif row.get("how") == "float":
                if not (skills_on and o["cave_of_trials"]):
                    errs.append(f"{row['name']}: Float")
            elif not specials_on or l["id"] != 0x50320000 + (14 << 21) + (row["party_id"] << 8) + row["move_index"]:
                errs.append(f"{row['name']}: learn point")
        own = it.get("player") == s["slot"]["id"]
        code = SK.ITEM_NAME_TO_ID.get(it.get("name")) if own else it.get("ap_id")
        sk_item = own and it.get("kind") in ("skill", "move")
        if "price" in l:
            if row["type"] != "shop" or not sk_item or l["price"] != SK.price_of(code):
                errs.append(f"{row['name']}: price {l['price']} for {it.get('name')}")
        elif row["type"] == "shop" and sk_item:
            errs.append(f"{row['name']}: {it.get('name')} sold without a price")
    if skills_on and guild != 45:
        errs.append(f"skillsanity: {guild} guild checks")
NAMES = ("Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Noel", "Chisato")


def chance0(row):
    miss = 1.0
    for odds in row["odds"]:
        if odds - 7 > 0:
            miss *= 1 - (odds - 7) / 256
    return 1 - miss
PATCH = re.compile(r"^script (\d+) 0x([0-9a-f]+) 0x([0-9a-f]{8}) 0x([0-9a-f]{8})$")
PUZZLE = {289, 317}
sys.path.insert(0, os.path.join(ROOT, "apworld", "so2"))
import doors as DOORS  # noqa: E402  (the world agent's validator, standard library only)
DOOR_TABLE = json.load(open(os.path.join(ROOT, "docs", "ap", "doors.json")))
WORLD_DOORS = {r["id"]: r for r in DOOR_TABLE["doors"] if r.get("world") in (1, 2)}
KEYS = {g["name"]: g for g in data.KEY_GROUPS}
# The El voyage (data.STORY_GATES): its door, its keys per seed, where they may sit.
(GATE,) = data.STORY_GATES
GROUP_KEY = {g["group"]: g["name"] for g in data.KEY_GROUPS}
STAGE_OF_GROUP = {g["group"]: g["stage"] for g in data.KEY_GROUPS}


def gate_names(o):
    later = o["expel_teleporter"] and o["expel_progression"]
    return [GROUP_KEY[g] for g in GATE["groups"] + (GATE["teleporter_groups"] if later else [])]


def gate_proven(row, side):
    games = ("claude", "rena") if side is None else (side,)
    return all(row["id"] in GATE["proven"].get(g, ()) for g in games)


def before_gate(row):
    """A location the story reaches before the voyage (its early mode: rules.early_stage)."""
    if row["type"] == "drop" or (row["type"] == "boss" and row.get("boss") == "story"):
        return False
    st = STAGE_OF_GROUP.get(row.get("group"))
    if row["region"] != "Expel" or st is None:
        return False
    if row.get("from_chapter"):
        st = max(st, next((k for k, (_, (lo, hi), _) in enumerate(data.STAGES, 1) if lo <= row["from_chapter"] <= hi), 99))
    return st <= GATE["stage"]
EXPEL_KEYS = {g["name"] for g in data.KEY_GROUPS if g["stage"] and g["stage"] <= data.DISC_SWITCH_STAGE}


def proven(row, side, key):
    games = ("claude", "rena") if side is None else (side,)
    return all(g in row.get(key, ()) for g in games)


def allowed_open(row, side, expel, tele, item, me):
    """Open world (docs/ap/apworld.md): Expel holds progression where the teleporter proof
    reached it; where only the late proof did (or with expel_progression off), only our own
    Expel-story keys. Gift sites never hold progression (2026-10-25), not even our own keys."""
    if row["type"] in ("drop", "join", "shop", "gift"):
        return False
    wog = {g["group"]: g["world"] for g in data.KEY_GROUPS}
    expel_like = row["region"] == "Expel" or (row["region"] == "Energy Nede" and not row.get("group_near")
                                              and wog.get(row.get("group")) == 1)
    if not expel_like:
        return (row["type"] == "boss" and row.get("boss") == "story") or proven(row, side, "late")
    ours = item.get("player") == me and item.get("name") in EXPEL_KEYS
    if (row["type"] == "boss" and row.get("boss") == "story") or (tele and proven(row, side, "tele")):
        return expel or ours
    return proven(row, side, "late") and ours


def allowed(row, side, expel):
    if row["type"] in ("drop", "join", "shop", "gift"):
        return False
    if row["region"] == "Expel" and not expel:
        return False
    if row["type"] == "boss" and row.get("boss") == "story":
        return True
    games = ("claude", "rena") if side is None else (side,)
    return all(g in row.get("late", ()) for g in games)


def drop_gate_ok(row, side):
    """A gate key in a drop: one of its places (an Expel map with no encounter condition) is one
    the story reaches before the voyage and the voyage proof reached."""
    games = ("claude", "rena") if side is None else (side,)
    return any(not p.get("world_map") and p["region"] == "Expel" and STAGE_OF_GROUP.get(p.get("group"))
               and STAGE_OF_GROUP[p["group"]] <= GATE["stage"] and all(g in p.get("voyage", ()) for g in games)
               for p in row["places"])


HUB_KEY_NAMES = {g["name"] for g in data.HUB_DUNGEONS if g["dungeon"] != "Fienal"}
CHAMBERS = {b["encounter"]: b for b in data.HUB_BOSSES}
DUNGEON_OF_MAP = {m: g["dungeon"] for g in data.HUB_DUNGEONS for m in g["maps"]}
HUB_GROUPS = {g["key"]: g for g in data.HUB_DUNGEONS}
HUB_DUNGEON_NAMES = {g["dungeon"] for g in data.HUB_DUNGEONS}
FIELDS = [524, 525, 527, 528]


def hub_dungeon(row):
    if row["type"] == "boss" and row["encounter"] in CHAMBERS:
        d = CHAMBERS[row["encounter"]]["dungeon"]
        return d if d in HUB_DUNGEON_NAMES else None
    if row["type"] in ("drop", "craft", "killer_move") or row.get("map") is None:
        return None
    return DUNGEON_OF_MAP.get(row["map"])


def check_hub(s, errs, locs):
    """A hub seed (docs/ap/seed_format.md "Hub"; apworld/so2/hub.py)."""
    from worlds.so2.hub import ASK, BANNERS, CONFIRMED_OPTIONAL, OUT_OF_HUB
    o = s["options"]
    side = None if o["story_route"] == "any" else o["story_route"]
    games = ("claude", "rena") if side is None else (side,)
    if s["version"] != 3 or o["seed_format"] != 3:
        errs.append(f"hub version {s['version']}")
    if (o["party_rules"], o["character_items"], o["expel_teleporter"]) != ("open", True, False):
        errs.append("hub options party_rules / character_items / expel_teleporter")
    # The hub block.
    h = s.get("hub", {})
    if {k: h.get(k) for k in data.HUB_STATE} != data.HUB_STATE:
        errs.append("hub block differs from data.HUB_STATE")
    if o["hub_start_town"] not in data.HUB_STARTS or h.get("start") != data.HUB_STARTS[o["hub_start_town"]]:
        errs.append(f"hub start {o.get('hub_start_town')}")
    if o["hub_start"] != "random_town" and o["hub_start"] != o["hub_start_town"]:
        errs.append(f"hub_start {o['hub_start']} but {o['hub_start_town']}")
    # The Fienal requirement.
    want_won = {"fields_and_berle": FIELDS + [535], "fields": FIELDS}.get(o["fienal_requirement"])
    if want_won is None:
        want_won = [b["encounter"] for b in data.HUB_BOSSES if b["dungeon"] != "Fienal" and b.get("game") in (None, side)]
    if o["fienal_won"] != want_won:
        errs.append(f"fienal_won {o['fienal_won']}")
    # Keys: the items table, the doors, something behind each.
    keys = {v["name"]: v["key"] for v in s["items"].values() if v["kind"] == "key"}
    if set(keys) != set(o["dungeon_keys"]) or not set(keys) <= HUB_KEY_NAMES:
        errs.append(f"keys in the items table {sorted(keys)}")
    if any(HUB_GROUPS[n]["name"] != name for name, n in keys.items()):
        errs.append("a key's number is not its dungeon's")
    if "Fienal Key" in keys or "Portal of Sealing Key" in keys:
        errs.append("a Fienal / Portal of Sealing key")
    if any(i["kind"] == "key" for i in s["start"]["items"]):
        errs.append("a starting key")
    keyed = {HUB_GROUPS[n]["dungeon"] for n in keys.values()}
    behind = {hub_dungeon(ROW[l["id"]]) for l in locs if ROW[l["id"]]["type"] not in ("drop", "craft", "killer_move")}
    behind |= {CHAMBERS[e]["dungeon"] for e in o["fienal_won"]}
    if o["goal"].endswith("iseria_queen"):
        behind.add("Cave of Trials")
    behind = {d for d in behind if d in HUB_DUNGEON_NAMES and d != "Fienal" and d not in OUT_OF_HUB}
    if keyed != behind:
        errs.append(f"keyed {sorted(keyed ^ behind)}")
    doors = s["doors"]
    keyed_doors = sorted((d["id"], d["key"], d.get("routine")) for d in doors if "key" in d)
    want_doors = sorted((d["id"], n, d["routine"]) for n in keys.values() for d in HUB_GROUPS[n]["doors"])
    if keyed_doors != want_doors:
        errs.append("keyed doors")
    fienal = [d for d in doors if d.get("won") and d["id"] != 43978493]
    if len(fienal) != 1 or fienal[0]["id"] != 68685691 or fienal[0]["won"] != o["fienal_won"] \
            or fienal[0]["banners"] != [BANNERS[e] for e in o["fienal_won"]] or "key" in fienal[0]:
        errs.append(f"Fienal gate {fienal}")
    goal_doors = [d for d in doors if d["id"] == 43978493]
    # P5 (hubcore2): the arena antechamber's door to Marsilio's arena 305 while he is unbeaten;
    # (hubask) Fienal's 679 -> 674's east side while Cyril is unbeaten.
    p5_doors = [d for d in doors if "to" in d]
    if p5_doors != [{"id": 19929509, "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271, "while_unbeaten": 532},
                     "name": "Fun City Fighting Arena (map 304 -> 305 while Marsilio is unbeaten)"},
                    {"id": 44502760, "to": {"map": 674, "x": 882, "y": 0, "z": 30, "dir": 180, "while_unbeaten": 538},
                     "name": "Fienal (679 -> 674: Cyril's east side while unbeaten)"}]:
        errs.append(f"P5 doors {p5_doors}")
    if len(doors) != len(keyed_doors) + 1 + len(goal_doors) + len(p5_doors) or len(goal_doors) != (o["goal"] != "indalecio"):
        errs.append("other doors in a hub seed")
    probs = DOORS.validate_doors(doors, DOOR_TABLE)
    if probs:
        errs.append(f"validate_doors: {probs[:2]}")
    # The chambers.
    numbers = {HUB_GROUPS[n]["dungeon"]: n for n in keys.values()}
    bosses = s.get("bosses", [])
    if [b["encounter"] for b in bosses] != [b["encounter"] for b in data.HUB_BOSSES]:
        errs.append("bosses rows")
    for b, ref in zip(bosses, data.HUB_BOSSES):
        if any(b.get(k) != ref.get(k) for k in ("name", "map", "maps", "view", "game", "location", "dungeon")) \
                or b.get("keys") != ([numbers[ref["dungeon"]]] if ref["dungeon"] in numbers else []):
            errs.append(f"boss row {b['encounter']}")
        if b.get("ask") != ASK.get(b["encounter"]):  # (hubask) Shigeo 281 and Marsilio 305 ask first
            errs.append(f"boss row {b['encounter']} ask")
    # The locations: what the hub reaches; where progression went.
    for l in locs:
        row = ROW[l["id"]]
        t = row["type"]
        reach = None
        if t == "join" or (t == "boss" and row.get("boss") == "losable"):
            errs.append(f"{row['name']}: not a hub location")
            continue
        if t == "boss" and row["encounter"] in CHAMBERS:
            g = CHAMBERS[row["encounter"]].get("game")
            reach = ("claude", "rena") if g is None else (g,)
        elif t in ("chest", "gift", "boss"):
            reach = tuple(data.HUB_REACHED.get(row["id"], ()))   # the hub proof (hubproof, 2026-10-25)
        elif t == "shop":
            reach = tuple(g for g in ("claude", "rena") if row["shop"] in data.HUB_SHOPS[g])
        elif t == "drop":
            reach = tuple(g for g in ("claude", "rena") if data.HUB_DROPS[row["id"]][g])
        if reach is not None and not any(g in reach for g in games):
            errs.append(f"{row['name']}: the hub doesn't reach it")
        if hub_dungeon(row) in OUT_OF_HUB:
            errs.append(f"{row['name']}: Portal of Sealing")
        it = l["item"]
        if t == "drop" and not o["area_keys_in_drops"] and it.get("name") in (set(KEYS) | HUB_KEY_NAMES):
            errs.append(f"key {it['name']} in {row['name']} with area_keys_in_drops off")
        if it.get("classification") != "progression":
            continue
        every = all(g in (reach or ()) for g in games)
        expel_ok = o["expel_progression"] or row["region"] != "Expel"
        if t in ("gift", "shop", "craft"):
            ok = False
        elif t == "chest":
            ok = every and expel_ok
        elif t == "boss":
            ok = every and expel_ok and (row.get("boss") != "optional" or row["encounter"] in CONFIRMED_OPTIONAL)
        elif t == "drop":
            ok = (o["drop_progression"] != "never" and o["encounter_rate"] > 0 and chance0(row) >= MIN_DROP_CHANCE
                  and every and any(any(all(m in data.HUB_MAPS[g] for g in games) for m in p["maps"])
                                    and (o["expel_progression"] or p["region"] != "Expel") for p in row["places"]))
        elif t in ("guild", "learn"):
            ok = True   # check_skillsanity (sk_may_hold) [skilllogic2]
        elif t == "killer_move":
            mode = o["killer_move_progression"]
            heroes = ("Claude", "Rena") if side is None else (side.capitalize(),)
            sure = ({side.capitalize()} if side else set()) | {o["starting_character"]}
            ok = o["encounter_rate"] > 0 and ((mode == "hero" and row["character"] in heroes)
                                              or (mode == "any" and (row["character"] in sure
                                                                     or row["character"] in o["characters"])))
        else:
            ok = False
        if not ok:
            errs.append(f"hub: progression at {row['name']}")
    # The party (D7): items always; under story_route any both heroes (but the start).
    party = o["characters"]
    start = o["starting_character"]
    heroes = ("Claude", "Rena") if side is None else (side.capitalize(),)
    if side is None:
        if {"Claude", "Rena"} - {start} != {"Claude", "Rena"} & set(party):
            errs.append(f"hub party {party}: the heroes under story_route any")
    elif heroes[0] in party or start == heroes[0]:
        errs.append(f"hub party {party}: the hero")
    members = set(heroes) | set(party) | ({start} if start else set())
    if len(members) > 8:
        errs.append(f"hub party {party}: more than 8")
    excl = set(o["exclude_characters"]) - set(heroes) - {start}
    if set(party) & excl:
        errs.append(f"hub party {party}: excluded members")
    allowed_n = [n for n in NAMES if n not in heroes and n != start and n not in excl]
    if len(members) != min(8, len(set(heroes) | ({start} if start else set())) + len(allowed_n)):
        errs.append(f"hub party {party}: room left")
    if not members & {"Rena", "Noel"} and not {"Rena", "Noel"} <= excl:
        errs.append(f"hub party {party}: no healer")
    for c in party:
        if str(0x50340000 + NAMES.index(c) + 1) not in s["items"]:
            errs.append(f"{c} missing from the items table")
    if s["goal"] != {"indalecio": {"encounters": [539, 540]},
                     "indalecio_limiter_off": {"encounters": [540], "flags": [445, 446]},
                     "indalecio_limiter_off_and_iseria_queen": {"all": [561, 540], "encounters": [540],
                                                                "flags": [445, 446]}}[o["goal"]]:
        errs.append(f"goal {s['goal']}")



def take_choices(o, errs, hub):
    """[charselect agent] starting_character "any" (so2.apworld 0.2.0 since 2026-10-25): the seed
    lists whom New Game's character select offers; the logic counts on nobody extra. Checks the
    list (the heroes and the pool, at most 8, nobody excluded; under the story's party rules every
    member not excluded) and leaves starting_character None for the other checks."""
    sc = o["starting_character"]
    if not isinstance(sc, list):
        return
    o["_choices"] = sc
    o["starting_character"] = None
    side = None if o["story_route"] == "any" else o["story_route"]
    heroes = ("Claude", "Rena") if side is None else (side.capitalize(),)
    excl = set(o["exclude_characters"]) - set(heroes)
    if sc != [n for n in NAMES if n in sc] or len(set(sc)) != len(sc):
        errs.append(f"starting_character list {sc}: not in party order")
    if not hub and o["world_mode"] != "open_world":
        errs.append("starting character list in story mode")
    if hub or o["party_rules"] == "open":
        if set(sc) != set(heroes) | set(o["characters"]) or len(sc) > 8 or set(sc) & excl:
            errs.append(f"starting_character list {sc}: not the heroes and the pool {o['characters']}")
    elif sc != [n for n in NAMES if n in heroes or n not in excl]:
        errs.append(f"starting_character list {sc}: not every member but the excluded")


def check_hub_file(s, errs):
    """check()'s shape checks (ids, placeholders, patches, the gems, the option-bound kinds), then
    check_hub()."""
    o = s["options"]
    take_choices(o, errs, True)
    locs = s["locations"]
    side = None if o["story_route"] == "any" else o["story_route"]
    ids = [l["id"] for l in locs]
    if len(set(ids)) != len(ids):
        errs.append("repeated ids")
    ph = [l["placeholder"] for l in locs if "placeholder" in l]
    if len(set(ph)) != len(ph):
        errs.append("repeated placeholders")
    words = set()
    for line in s["patches"]:
        m = PATCH.match(line)
        if not m:
            errs.append(f"bad patch {line}")
            continue
        if (m.group(1), m.group(2)) in words:
            errs.append(f"two patches on {(m.group(1), m.group(2))}")
        words.add((m.group(1), m.group(2)))
    prog = 0
    for l in locs:
        row = ROW.get(l["id"])
        if row is None or l["check"]["type"] != row["type"]:
            errs.append(f"unknown id or type {l['id']}")
            return errs, len(locs), 0, (), o
        if side is not None and row["story_route"] not in (None, side):
            errs.append(f"{row['name']}: other protagonist's location")
        if row["region"] == "Cave of Trials" and not o["cave_of_trials"]:
            errs.append(f"{row['name']}: cave off")
        if row["type"] in ("chest", "gift") and row["item"] in PUZZLE and (
                l["item"].get("game_id") != row["item"] or l["item"].get("player") != s["slot"]["id"]):
            errs.append(f"{row['name']}: the gem moved")
        if row["type"] == "shop" and o["shuffle_shops"] == "specials" and not row["special"]:
            errs.append(f"{row['name']}: not a special")
        for kind, opt in (("craft", "craftsanity"), ("killer_move", "killer_move_checks"), ("boss", "boss_checks")):
            if row["type"] == kind and not o[opt]:
                errs.append(f"{row['name']}: {kind} without {opt}")
        if row["type"] == "drop" and o["enemy_dropsanity"] != "first_drop":
            errs.append(f"{row['name']}: drop without dropsanity")
        prog += l["item"].get("classification") == "progression"
    if o["shuffle_shops"] == "off" and any(l["check"]["type"] == "shop" for l in locs):
        errs.append("shop slots with shuffle_shops off")
    check_hub(s, errs, locs)
    combo = ("hub", o["hub_start"], o["fienal_requirement"], o["story_route"], o["missable_locations"],
             o["expel_progression"], o["cave_of_trials"], o["boss_checks"], o["enemy_dropsanity"],
             o["drop_progression"], o["area_keys_in_drops"], o["shuffle_shops"], o["goal"], o["craftsanity"],
             o["starting_character"] is not None, o["killer_move_checks"], o["killer_move_progression"])
    o["_drops_prog"] = sum(1 for l in locs if l["check"]["type"] == "drop"
                           and l["item"].get("classification") == "progression")
    return errs, len(locs), prog, combo, o


def check(path):
    errs = []
    drops_prog = [0]
    s = json.load(open(path))
    o = s["options"]
    check_skillsanity(s, errs)  # [skillsanity2]
    if o["world_mode"] == "hub":
        return check_hub_file(s, errs)
    take_choices(o, errs, False)
    side = None if o["story_route"] == "any" else o["story_route"]
    party = o["characters"]
    if o["party_rules"] == "open":
        members_sure = set(("Claude", "Rena") if side is None else (side.capitalize(),)) | {o["starting_character"]}
    else:
        members_sure = {"Claude", "Rena", "Noel", o["starting_character"]}
    v2 = (o["goal"] != "indalecio" or o["party_rules"] == "open" or o["starting_character"] is not None
          or "_choices" in o
          or o["craftsanity"] or o["no_fail_crafting"] or o["killer_move_checks"]
          or o.get("difficulty", "earth") != "earth"
          or o.get("skillsanity", "none") != "none")  # [skillsanity2]  # [difficulty agent] an older port would play it on Earth
    if (s["format"], s["version"]) != ("apso2", 2 if v2 else 1) or o["seed_format"] != s["version"]:
        errs.append(f"format / version {s['version']}")
    locs = s["locations"]
    ids = [l["id"] for l in locs]
    if len(set(ids)) != len(ids):
        errs.append("repeated ids")
    ph = [l["placeholder"] for l in locs if "placeholder" in l]
    if len(set(ph)) != len(ph):
        errs.append("repeated placeholders")
    words = set()
    for line in s["patches"]:
        m = PATCH.match(line)
        if not m:
            errs.append(f"bad patch {line}")
            continue
        k = (m.group(1), m.group(2))
        if k in words:
            errs.append(f"two patches on {k}")
        words.add(k)
    prog = 0
    for l in locs:
        row = ROW.get(l["id"])
        if row is None:
            errs.append(f"unknown id {l['id']}")
            continue
        if l["check"]["type"] != row["type"]:
            errs.append(f"{l['id']} type")
        if side is not None and row["story_route"] not in (None, side):
            errs.append(f"{row['name']}: other protagonist's location")
        if row["region"] == "Cave of Trials" and not o["cave_of_trials"]:
            errs.append(f"{row['name']}: cave off")
        it = l["item"]
        if row["type"] == "shop":
            c = l["check"]
            if (c["shop"], c["index"], c["item"]) != (row["shop"], row["index"], row["item"]) or \
                    l["id"] != 0x50320000 + (10 << 21) + (row["shop"] << 8) + row["index"]:
                errs.append(f"{row['name']}: shop check")
            if o["shuffle_shops"] == "specials" and not row["special"]:
                errs.append(f"{row['name']}: not a special")
        if row["type"] == "craft":
            if l["check"] != {"type": "craft", "item": row["item"]} or l["id"] != 0x50320000 + (11 << 21) + row["item"]:
                errs.append(f"{row['name']}: craft check")
            if not o["craftsanity"]:
                errs.append(f"{row['name']}: craft without craftsanity")
        if row["type"] == "killer_move":
            c = l["check"]
            if c != {"type": "killer_move", "character": NAMES.index(row["character"]) + 1, "move": row["move"],
                     "level": row["level"], "uses": row["uses"]} or l["id"] != 0x50320000 + (12 << 21) + (
                    c["character"] << 14) + (c["move"] << 10) + c["level"] or not o["killer_move_checks"]:
                errs.append(f"{row['name']}: killer check")
        if row["type"] == "drop" and it.get("name") in KEYS and not o["area_keys_in_drops"]:
            errs.append(f"key {it['name']} in {row['name']} with area_keys_in_drops off")
        if it.get("classification") == "progression":
            prog += 1
            if row["type"] == "drop":
                cls = drop_rule(row, side, o["world_mode"] == "open_world", o["expel_teleporter"],
                                o["expel_progression"], o["drop_progression"], o["encounter_rate"])[0]
                ok = cls == "any" or (cls == "expel keys" and it.get("player") == s["slot"]["id"]
                                      and it.get("name") in EXPEL_KEYS)
                if chance0(row) < MIN_DROP_CHANCE or o["encounter_rate"] == 0 or o["drop_progression"] == "never":
                    ok = False
                drops_prog[0] += 1
            elif row["type"] == "craft":
                ok = False
            elif row["type"] in ("guild", "learn"):
                ok = True   # check_skillsanity (sk_may_hold) [skilllogic2]
            elif row["type"] == "killer_move":
                mode = o["killer_move_progression"]
                ok = o["encounter_rate"] > 0 and (
                    (mode == "hero" and row["character"] in (("Claude", "Rena") if side is None else (side.capitalize(),)))
                    or (mode == "any" and (row["character"] in members_sure or row["character"] in party)))
            else:
                ok = (allowed_open(row, side, o["expel_progression"], o["expel_teleporter"], it, s["slot"]["id"])
                      if o["world_mode"] == "open_world" else allowed(row, side, o["expel_progression"]))
            if not ok:
                errs.append(f"progression at {row['name']} (late {row.get('late')}, tele {row.get('tele')})")
            if o["world_mode"] == "open_world" and before_gate(row) and not gate_proven(row, side) \
                    and row["type"] != "drop":
                errs.append(f"progression at {row['name']}, which the voyage proof did not reach")
            if o["world_mode"] == "open_world" and it.get("player") == s["slot"]["id"] and it.get("name") in gate_names(o):
                if row["type"] == "boss" and row.get("boss") == "story":
                    if row["chapter"] > GATE["chapter"] or (row["chapter"] == GATE["chapter"]):
                        errs.append(f"gate key {it['name']} at the story boss {row['name']} (chapter {row['chapter']})")
                elif row["type"] == "drop":
                    if not drop_gate_ok(row, side):
                        errs.append(f"gate key {it['name']} at {row['name']}, no place the voyage proof reached")
                elif row["type"] == "killer_move":
                    errs.append(f"gate key {it['name']} at the killer move {row['name']}")
                elif row["type"] == "guild":    # [skilllogic2] a guild the story reaches before the voyage
                    if not drop_gate_ok({"places": [data.GUILD_PLACES[m] for m in row["guild_maps"]]}, side):
                        errs.append(f"gate key {it['name']} at {row['name']}, no guild the voyage proof reached")
                elif row["type"] == "learn":    # (from the disc switch at the earliest)
                    errs.append(f"gate key {it['name']} at the learn point {row['name']}")
                elif not (before_gate(row) and gate_proven(row, side)):
                    errs.append(f"gate key {it['name']} at {row['name']}, not proven reachable at the voyage")
        if row["type"] in ("chest", "gift") and row["item"] in PUZZLE:
            if it.get("game_id") != row["item"] or it.get("player") != s["slot"]["id"]:
                errs.append(f"{row['name']}: the gem moved")
    if o["shuffle_shops"] == "off" and any(l["check"]["type"] == "shop" for l in locs):
        errs.append("shop slots with shuffle_shops off")
    # Phase 4: the doors and the keys.
    goal_doors = [d for d in s["doors"] if d.get("id") == 43978493]
    want_goal = {"indalecio": {"encounters": [539, 540]},
                 "indalecio_limiter_off": {"encounters": [540], "flags": [445, 446]},
                 "indalecio_limiter_off_and_iseria_queen": {"all": [561, 540], "encounters": [540],
                                                            "flags": [445, 446]}}[o["goal"]]
    if s["goal"] != want_goal:
        errs.append(f"goal {s['goal']}")
    if o["goal"] == "indalecio":
        if goal_doors:
            errs.append("goal gate with goal indalecio")
    else:
        gd = goal_doors[0] if len(goal_doors) == 1 else {}
        won = [561] if o["goal"].endswith("iseria_queen") else None
        if gd.get("flags") != [556] or gd.get("won") != won or gd.get("routine") != "0x3bec" \
                or len(gd.get("banners", ())) != 1 + bool(won):
            errs.append(f"goal gate {goal_doors}")
    s["doors"] = [d for d in s["doors"] if d.get("id") != 43978493]
    if o["world_mode"] == "open_world":
        ent = [d for d in s["doors"] if "keys" not in d]
        gates = [d for d in s["doors"] if "keys" in d and "until" not in d]
        exits = [d for d in s["doors"] if "until" in d]
        if exits != [{"id": e["door"], "routine": f"{e['routine']:#x}", "keys": e["keys"], "until": e["until"],
                      "name": e["name"], "purpose": e["purpose"]} for e in data.EXIT_GATES]:
            errs.append(f"exit gates {exits}")
        if {d["id"] for d in ent} != set(WORLD_DOORS) or len(ent) != 66:
            errs.append("doors: not the 66 world-map entrances")
        for d in ent:
            g = data.KEY_GROUPS[d["key"] - 1]
            if WORLD_DOORS[d["id"]]["key_group"] != g["group"]:
                errs.append(f"door {d['id']}: key {d['key']} is not its group's")
        want_keys = [KEYS[n]["key"] for n in gate_names(o)]
        if gates != [{"id": GATE["door"], "routine": f"{GATE['routine']:#x}", "keys": want_keys, "name": GATE["name"],
                      "purpose": GATE["purpose"]}]:
            errs.append(f"gate door {gates}")
        probs = DOORS.validate_doors(s["doors"], DOOR_TABLE)
        if probs:
            errs.append(f"validate_doors: {probs[:2]}")
        keys = {v["name"] for v in s["items"].values() if v["kind"] == "key"}
        if keys != set(KEYS):
            errs.append(f"keys in the items table: {len(keys)}")
        start = {i["name"] for i in s["start"]["items"] if i["kind"] == "key"}
        # The North City and Noel's House passes too unless the teleporter and expel_progression
        # are both on (0.2.1).
        later = o["expel_teleporter"] and o["expel_progression"]
        want = {"Arlia Pass", "Fun City Pass"} | (set() if later else {"North City Pass", "Noel's House Pass"})
        if start != want:
            errs.append(f"starting keys {sorted(start)}")
        if not o["expel_teleporter"] and any(
                l["item"].get("classification") == "progression" and ROW[l["id"]]["region"] == "Expel"
                and ROW[l["id"]]["type"] not in ("boss", "drop", "craft", "killer_move", "guild", "learn")
                and l["item"].get("name") not in EXPEL_KEYS for l in locs):
            # (guild checks and learn points: their row's region is nominal; sk_may_hold judges
            # them by their guilds and members [skilllogic2])
            errs.append("teleporter off: progression other than Expel keys in Expel")
    elif s["doors"] or o["expel_teleporter"]:
        errs.append("story mode with doors or the teleporter")
    party = o["characters"]
    for c in party:
        if str(0x50340000 + NAMES.index(c) + 1) not in s["items"]:
            errs.append(f"{c} missing from the items table")
    heroes = ("Claude", "Rena") if side is None else (side.capitalize(),)
    start = o["starting_character"]
    if start is not None and o["world_mode"] != "open_world":
        errs.append("starting character in story mode")
    if start in heroes and side is not None:
        errs.append(f"starting character {start} is the hero")
    if o["party_rules"] != ("open" if o["world_mode"] == "open_world" and o["character_items"] else "story"):
        errs.append(f"party_rules {o['party_rules']}")
    if o["party_rules"] == "open":
        members = set(heroes) | ({start} if start else set()) | set(party)
        excl = set(o["exclude_characters"]) - set(heroes)
        if len(members) > 8 or set(party) & set(heroes) or start in party or set(party) & excl:
            errs.append(f"open party {party} start {start}")
        allowed_n = [n for n in NAMES if n not in heroes and n != start and n not in excl]
        if len(members) < min(8, len(set(heroes) | ({start} if start else set())) + len(allowed_n)):
            errs.append(f"open party {party}: room left")
        if not members & {"Rena", "Noel"} and not {"Rena", "Noel"} <= excl:
            errs.append(f"open party {party}: no healer")
        for l in locs:
            r = ROW[l["id"]]
            if r["type"] == "join" and r["character"] in heroes:
                errs.append(f"join site of a story member {r['name']}")
    elif o["character_items"]:
        if sum(x in party for x in ("Bowman", "Precis")) != 1:
            errs.append(f"party {party}: Bowman / Precis")
        if ("Ashton" in party) == ("Opera" in party):
            errs.append(f"party {party}: Ashton / Opera")
        if "Ernest" in party and "Opera" not in party:
            errs.append(f"party {party}: Ernest without Opera")
        if ("Leon" in party and side != "claude") or ("Dias" in party and side != "rena"):
            errs.append(f"party {party}: wrong game")
        if len(party) > 5:
            errs.append(f"party {party}: too many")
        if any(ROW[l["id"]].get("open_only") for l in locs):
            errs.append("open-only join site under the story's party rules")
    elif party:
        errs.append("characters without character_items")
    combo = (o["world_mode"], o["expel_teleporter"], o["shuffle_shops"], o["story_route"], o["missable_locations"],
             o["expel_progression"], o["cave_of_trials"], o["boss_checks"], o["character_items"],
             o["enemy_dropsanity"], o["traps"] > 0, o["goal"], o["drop_progression"], o["area_keys_in_drops"],
             o["craftsanity"], o["starting_character"] is not None, o["killer_move_checks"],
             o["killer_move_progression"])
    o["_drops_prog"] = drops_prog[0]
    return errs, len(locs), prog, combo, o


def main():
    files = sorted(glob.glob(os.path.join(sys.argv[1], "*", "*.apso2")))
    bad, sizes, progs, combos = 0, [], [], set()
    sk_prog = collections.Counter()     # [skilllogic2] progression at guild checks / learn points
    kinds = collections.Counter()
    for f in files:
        errs, n, prog, combo, o = check(f)
        sizes.append(n)
        progs.append(prog)
        sk_prog.update(o.get("_sk_prog", {}))
        combos.add(combo)
        for k in ("boss_checks", "character_items", "cave_of_trials", "expel_progression"):
            kinds[(k, bool(o[k]))] += 1
        kinds[("drops", o["enemy_dropsanity"], o["drop_progression"])] += 1
        kinds[("goal", o["goal"])] += 1
        kinds[("craftsanity", o["craftsanity"])] += 1
        kinds[("killer", o["killer_move_checks"], o["killer_move_progression"])] += 1
        kinds[("party_rules", o["party_rules"], "list" if "_choices" in o else o["starting_character"] is not None)] += 1
        kinds[("version", o["seed_format"])] += 1
        kinds[("progression in drops>0", o["_drops_prog"] > 0)] += 1
        kinds[("world", o["world_mode"], o["expel_teleporter"])] += 1
        if o["world_mode"] == "hub":
            kinds[("hub_start", o["hub_start"])] += 1
            kinds[("fienal_requirement", o["fienal_requirement"])] += 1
            kinds[("hub keys", len(o["dungeon_keys"]))] += 1
        kinds[("shops", o["shuffle_shops"])] += 1
        kinds[("story_route", o["story_route"])] += 1
        kinds[("difficulty", o.get("difficulty"))] += 1  # [difficulty agent]
        if o.get("difficulty", "earth") not in ("earth", "galaxy", "universe", "any"):
            errs.append(f"difficulty {o.get('difficulty')!r}")
        kinds[("missable", o["missable_locations"])] += 1
        kinds[("traps>0", o["traps"] > 0)] += 1
        kinds[("multiplier>1", o["exp_multiplier"] > 1 or o["fol_multiplier"] > 1)] += 1
        if errs:
            bad += 1
            print(f, errs[:5])
    print(f"{len(files)} seeds files, {bad} with errors; locations {min(sizes)}-{max(sizes)}; progression placed "
          f"in SO2 locations {min(progs)}-{max(progs)} (total {sum(progs)}); {len(combos)} option combinations")
    print(dict(sorted(kinds.items(), key=str)))
    print(f"progression at skillsanity locations: guild checks {sk_prog['guild']}, learn points {sk_prog['learn']}")


if __name__ == "__main__":
    main()
