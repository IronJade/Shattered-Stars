"""Locations (data.py, from docs/ap/field.json): the chests (one per opened flag) and the gift
sites; with the options, the boss fights, the join sites of the shuffled characters, the first
drop of each enemy type and the shop slots. The ids are tools/ap_extract.py's (docs/ap/field.md,
"Location ids"). Craftsanity's locations come from crafts.py: one per item Item Creation can
make, id ITEM_BASE + (11 << 21) + item (docs/ap/seed_format.md, "v2 fields")."""
from BaseClasses import Location, LocationProgressType

from .crafts import CRAFTS
from .data import HUB_BOSSES, HUB_LOCATIONS, ITEM_BASE, ITEMS, LOCATIONS
from .items import GAME
from .moves import MOVES, move_levels
from . import hell, skillsanity

CRAFT_TYPE = 11
CRAFT_ROWS = [{"id": ITEM_BASE + (CRAFT_TYPE << 21) + c["item"], "name": f"{c['specialty']}: {ITEMS[c['item']]}",
               "type": "craft", "item": c["item"], "fol": None, "specialty": c["specialty"],
               "specialties": c["specialties"], "makers": c["makers"], "area": c["specialty"], "region": "Expel",
               "sites": [], "missable": [], "story_route": None} for c in CRAFTS]
# Killer-move levels (killer_move_checks; moves.py): id ITEM_BASE + (12 << 21) + (party id << 14) +
# (move << 10) + level.
KILLER_TYPE = 12
KILLER_ROWS = [{"id": ITEM_BASE + (KILLER_TYPE << 21) + (pid << 14) + (move << 10) + level,
                "name": f"{who}: {name} level {level}", "type": "killer_move", "character": who, "party_id": pid,
                "move": move, "level": level, "uses": uses, "learn_level": learn, "item": None, "fol": None,
                "area": who, "region": "Expel", "sites": [], "missable": [], "story_route": None}
               for who, pid, move, name, learn, div, top in MOVES
               for level, uses in move_levels((who, pid, move, name, learn, div, top))]
# Hub mode's own rows (data.HUB_LOCATIONS: the chambers that are no story-mode location, the Cross
# Cave guardians); story mode and open world never choose them (`hub_only`).
# Skillsanity's guild checks and learn points (skillsanity.py) [skillsanity2].
ALL_ROWS = LOCATIONS + CRAFT_ROWS + KILLER_ROWS + HUB_LOCATIONS + skillsanity.ROWS
LOCATION_NAME_TO_ID = {row["name"]: row["id"] for row in ALL_ROWS}
ROW = {row["name"]: row for row in ALL_ROWS}

GROUP_OF_TYPE = {"chest": "Chests", "gift": "Gifts", "boss": "Bosses", "join": "Joins", "drop": "First Drops",
                 "shop": "Shop Slots", "craft": "Crafts", "killer_move": "Killer Moves"}
LOCATION_NAME_GROUPS = {}
for _row in LOCATIONS + HUB_LOCATIONS:
    LOCATION_NAME_GROUPS.setdefault(_row["region"], set()).add(_row["name"])
    LOCATION_NAME_GROUPS.setdefault(_row["area"], set()).add(_row["name"])
    LOCATION_NAME_GROUPS.setdefault(GROUP_OF_TYPE[_row["type"]], set()).add(_row["name"])
for _row in CRAFT_ROWS:     # by specialty: every specialty that makes the item
    LOCATION_NAME_GROUPS.setdefault("Crafts", set()).add(_row["name"])
    for _spec in _row["specialties"]:
        LOCATION_NAME_GROUPS.setdefault(_spec, set()).add(_row["name"])
_CHAMBERS = {b["location"] for b in HUB_BOSSES}
LOCATION_NAME_GROUPS["Boss Chambers"] = {r["name"] for r in LOCATIONS + HUB_LOCATIONS if r["id"] in _CHAMBERS}
LOCATION_NAME_GROUPS.update(skillsanity.LOCATION_GROUPS)
for _row in KILLER_ROWS:    # by member
    LOCATION_NAME_GROUPS.setdefault("Killer Moves", set()).add(_row["name"])
    LOCATION_NAME_GROUPS.setdefault(f"Killer Moves: {_row['character']}", set()).add(_row["name"])

# Missable reasons a diligent player avoids by being in time (tools/ap_world_tables.py
# SOFT_REASONS): missable_locations "include" makes them ordinary. The others (a choice, a
# member, a Private Action, a side quest, a losable fight, a join site, the other story_route)
# can rule each other out, so they never hold progression or useful items.
SOFT_REASONS = {"window", "time-limited"}


class SO2Location(Location):
    game = GAME


def missable_reasons(row, side):
    reasons = set(row["missable"])
    if side is None and row["story_route"] is not None:
        reasons.add("story_route")
    return reasons


def choose(options, party):
    """(row, progress type) of every location this player's world has. A location is left out
    when its kind or region is off, it needs the other story_route, or it can never run; a
    missable one follows missable_locations (join sites always stay while character_items is
    on: they are how the members join). Open world's party rules (party.rules "open") add the
    join sites of the other hero and Noel when they are not story members; craftsanity adds the
    crafts (choose_crafts)."""
    side = {0: None, 1: "claude", 2: "rena"}[options.story_route.value]
    first_drop = options.enemy_dropsanity == options.enemy_dropsanity.option_first_drop
    shops = options.shuffle_shops.value    # 0 off, 1 specials, 2 full
    out = []
    for row in LOCATIONS:
        kind_of = row["type"]
        if kind_of == "join" and (row["character"] in party.story_members if party.rules == "open"
                                  else row.get("open_only")):
            continue
        if (kind_of == "boss" and not options.boss_checks) or (kind_of == "join" and not options.character_items) \
                or (kind_of == "drop" and not first_drop) \
                or (kind_of == "shop" and (shops == 0 or (shops == 1 and not row["special"]))):
            continue
        if row["region"] == "Cave of Trials" and not options.cave_of_trials:
            continue
        if "never" in row["missable"]:
            continue
        if side is not None and row["story_route"] not in (None, side):
            continue
        reasons = missable_reasons(row, side)
        kind = LocationProgressType.DEFAULT
        if reasons:
            if options.missable_locations == options.missable_locations.option_remove and kind_of != "join":
                continue
            if options.missable_locations == options.missable_locations.option_exclude or reasons - SOFT_REASONS:
                kind = LocationProgressType.EXCLUDED
        out.append((row, kind))
    if options.craftsanity:
        out += choose_crafts(party)
    if options.killer_move_checks:
        out += choose_killer_moves(party, hell.trained(options))
    out += skillsanity.choose(options, party)
    return out


def fighter_status(who, party):
    """"sure" (a story member under the story's party rules or a hero, or the starting character),
    "item" (joins by their character item), "maybe" (joins only by the player's choice in the
    story) or None (never in the party)."""
    if who in party.guaranteed:
        return "sure"
    if who in party.pool:
        return "item"
    return "maybe" if who in party.possible else None


def choose_killer_moves(party, trained):
    """Killer-move levels of the members who can be in the party: filler only (EXCLUDED) for a
    member the logic can't count on (fighter_status "maybe") and without battles to train in
    (`trained`, hell.trained: encounter_rate above 0, or a hell seed's free Fun City arena)."""
    out = []
    for row in KILLER_ROWS:
        status = fighter_status(row["character"], party)
        if status is None:
            continue
        kind = LocationProgressType.EXCLUDED if status == "maybe" or not trained \
            else LocationProgressType.DEFAULT
        out.append((row, kind))
    return out


def choose_crafts(party):
    """Craftsanity's locations: every craft whose makers can be in the party (crafts.py
    `makers`; None: anyone). One that needs members of whom none is sure to be in the party
    (party.guaranteed) holds only filler (EXCLUDED); one none of whose makers can ever join
    (party.possible) is left out."""
    out = []
    for row in CRAFT_ROWS:
        makers = row["makers"]
        if makers is None:
            out.append((row, LocationProgressType.DEFAULT))
        elif set(makers) & party.guaranteed:
            out.append((row, LocationProgressType.DEFAULT))
        elif set(makers) & party.possible:
            out.append((row, LocationProgressType.EXCLUDED))
    return out


def seed_check(row):
    """A location's check fields in the seed (docs/ap/seed_format.md, "A location", "Phase 3")."""
    t = row["type"]
    if t == "chest":
        return {"type": "chest", "map": row["map"], "flag": row["flag"]}
    if t == "boss":
        return {"type": "boss", "encounter": row["encounter"]}
    if t == "join":
        return {"type": "join", "map": row["map"], "offset": row["offset"], "character": row["character"]}
    if t == "drop":
        return {"type": "drop", "enemy": row["enemy"]}
    if t == "shop":
        return {"type": "shop", "shop": row["shop"], "index": row["index"], "item": row["item"]}
    if t == "craft":
        return {"type": "craft", "item": row["item"]}
    if t == "killer_move":
        return {"type": "killer_move", "character": row["party_id"], "move": row["move"], "level": row["level"],
                "uses": row["uses"]}
    if t in ("guild", "learn"):
        return skillsanity.seed_check(row)
    return {"type": "gift", "map": row["map"], "offset": row["offset"]}


def seed_sites(row):
    """Its patch sites, as apworld/so2/patches.py takes them (chests and gifts only)."""
    return [{"map": m, "word": w, "expected": e, "form": f, "role": r} for m, w, e, f, r in row["sites"]]
