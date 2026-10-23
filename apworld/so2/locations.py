"""Locations (data.py, from docs/ap/field.json): the chests (one per opened flag) and the gift
sites; with the options, the boss fights, the join sites of the shuffled characters and the
first drop of each enemy type. The ids are tools/ap_extract.py's (docs/ap/field.md, "Location
ids")."""
from BaseClasses import Location, LocationProgressType

from .data import LOCATIONS
from .items import GAME

LOCATION_NAME_TO_ID = {row["name"]: row["id"] for row in LOCATIONS}
ROW = {row["name"]: row for row in LOCATIONS}

GROUP_OF_TYPE = {"chest": "Chests", "gift": "Gifts", "boss": "Bosses", "join": "Joins", "drop": "First Drops"}
LOCATION_NAME_GROUPS = {}
for _row in LOCATIONS:
    LOCATION_NAME_GROUPS.setdefault(_row["region"], set()).add(_row["name"])
    LOCATION_NAME_GROUPS.setdefault(_row["area"], set()).add(_row["name"])
    LOCATION_NAME_GROUPS.setdefault(GROUP_OF_TYPE[_row["type"]], set()).add(_row["name"])

# Missable reasons a diligent player avoids by being in time (tools/ap_world_tables.py
# SOFT_REASONS): missable_locations "include" makes them ordinary. The others (a choice, a
# member, a Private Action, a side quest, a losable fight, a join site, the other protagonist)
# can rule each other out, so they never hold progression or useful items.
SOFT_REASONS = {"window", "time-limited"}


class SO2Location(Location):
    game = GAME


def missable_reasons(row, side):
    reasons = set(row["missable"])
    if side is None and row["protagonist"] is not None:
        reasons.add("protagonist")
    return reasons


def choose(options):
    """(row, progress type) of every location this player's world has. A location is left out
    when its kind or region is off, it needs the other protagonist, or it can never run; a
    missable one follows missable_locations (join sites always stay while character_items is
    on: they are how the members join)."""
    side = {0: None, 1: "claude", 2: "rena"}[options.protagonist.value]
    first_drop = options.drops_as_checks == options.drops_as_checks.option_first_drop
    out = []
    for row in LOCATIONS:
        kind_of = row["type"]
        if (kind_of == "boss" and not options.boss_checks) or (kind_of == "join" and not options.character_items) \
                or (kind_of == "drop" and not first_drop):
            continue
        if row["region"] == "Cave of Trials" and not options.cave_of_trials:
            continue
        if "never" in row["missable"]:
            continue
        if side is not None and row["protagonist"] not in (None, side):
            continue
        reasons = missable_reasons(row, side)
        kind = LocationProgressType.DEFAULT
        if reasons:
            if options.missable_locations == options.missable_locations.option_remove and kind_of != "join":
                continue
            if options.missable_locations == options.missable_locations.option_exclude or reasons - SOFT_REASONS:
                kind = LocationProgressType.EXCLUDED
        out.append((row, kind))
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
    return {"type": "gift", "map": row["map"], "offset": row["offset"]}


def seed_sites(row):
    """Its patch sites, as apworld/so2/patches.py takes them (chests and gifts only)."""
    return [{"map": m, "word": w, "expected": e, "form": f, "role": r} for m, w, e, f, r in row["sites"]]
