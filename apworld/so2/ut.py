"""Universal Tracker support (docs/plans/trackers.md B1; docs/apworld/apworld.md "Trackers"; 0.2.2).

Universal Tracker (UT: FarisTheAncient/Archipelago's worlds/tracker, an Archipelago client) builds
this world again on the player's machine and evaluates our own rules against what the server says
was received and checked. It runs generate_early .. generate_basic only (no fill), with a new
random seed and `multiworld.generation_is_fake`. Our world is yaml-less
(SO2World.ut_can_gen_without_yaml): UT starts from the default options and hands the room's slot
data back as `multiworld.re_gen_passthrough[game]` (SO2World.interpret_slot_data returns it as it
is). So the rebuilt world must be the same world, from the slot data alone:
- every option comes back from it (SLOT_OPTIONS, restore); the options it doesn't carry have no
  effect on what UT builds (NO_EFFECT);
- so do the random choices that shape locations or rules: the party pool (`characters`:
  SO2World.choose_open_party / choose_party) and hub mode's start town (`hub_start_town`:
  hub.choose_start);
- then the rebuilt world's own slot data (SO2World.slot_options) must equal the room's
  (check_restored): the derived keys (party_rules, seed_format, fienal_won, dungeon_keys,
  hell_seed...) prove that the options and the party came back and that this world computes from
  them what the room's world did.
`logic_version` (fill_slot_data) is bumped whenever the same slot data would make other logic.
When this world can't reproduce a room (a logic version it doesn't know, slot data it can't read
back, a rebuilt world that differs), it refuses with UT's TrackerException and a message that
names the so2.apworld to install (decision D10), rather than track the wrong logic. Rooms from
0.2.0 and 0.2.1 have no logic_version: version 1 when their slot data restores cleanly, but for a
0.2.0 room with a hell seed's options (OLD_LOGIC: 0.2.1 changed those rules); a 0.2.0 room with
skillsanity is tracked with a warning (OLD_LOGIC_WARNINGS: 0.2.1's rules for it are stricter).

Nothing here runs unless re_gen_passthrough has our game: a generation is unchanged.
"""
import json
import logging

from .data import DISC_SWITCH_STAGE, HUB_STARTS, KEY_GROUPS, STAGES
from .locations import ALL_ROWS, LOCATION_NAME_TO_ID
from .options import CHARACTER_NAMES

LOGIC_VERSION = 1           # this world's logic; bump when the same slot data would make other logic
LOGIC_VERSIONS = (1,)       # the room logic versions this world reproduces (a room without the key: 1)
NEW_KEYS = ("logic_version", "exclude_locations", "tracker")  # 0.2.2's keys: not in the .apso2, absent from older rooms

# Rooms made before logic_version whose logic differs from version 1 (docs/apworld/apworld.md "Trackers",
# "Older rooms": 0.2.0 rooms rebuilt here, 17 mixes x 5 seeds, and the user's own room).
# OLD_LOGIC refuses: apworld_version -> (does the room's slot data use the changed logic, why) pairs,
# and what to install instead. 0.2.1 made encounter_rate 0 with killer-move checks a hell seed (its
# levels from the Fun City arena; check_restored would refuse it too, by its rebuilt `hell_seed`).
OLD_LOGIC = {
    "0.2.0": ([(lambda data: data.get("encounter_rate") == 0 and data.get("killer_move_checks") is True,
                "0.2.1 changed the logic of encounter_rate 0 with killer-move checks (the hell seed)")],
              "Track it with so2.apworld 0.2.0 and your YAML in Universal Tracker's Players folder (0.2.0 "
              "can't rebuild a room without it, and draws its own party pool where one is random)."),
}
# OLD_LOGIC_WARNINGS tracks with a logged warning: apworld_version -> (applies, warning) pairs. 0.2.1
# let Skillsanity's guild checks and learn levels hold progression, with rules of their own that are
# stricter than 0.2.0's: rebuilt here, every 0.2.0 Skillsanity room measured shows at every sphere a
# subset of what the room's own logic counted on (29 of 45 rooms fewer somewhere, none more; the
# user's own hub room too), so UT may list such a check late, never early (the user, 2026-10-27:
# "accept with a warning", on that condition).
OLD_LOGIC_WARNINGS = {
    "0.2.0": [(lambda data: data.get("skillsanity", "none") != "none",
               "This room was made with so2.apworld 0.2.0 with Skillsanity on: 0.2.1 changed the logic of "
               "Skillsanity's guild checks and learn levels, so Universal Tracker may show some of them "
               "later than they really became reachable (never earlier). Regenerate the room with "
               "so2.apworld 0.2.2 or later for exact tracking.")],
}

REQUIRED = "required"       # SLOT_OPTIONS `absent`: a room without the key can't be restored
HUB = "hub"                 # required in hub mode; elsewhere absent (only hub mode reads it: its default)


def _same(value):
    return value


def _starting_character(value):
    """slot data `starting_character` (world.fill_slot_data): a member's name, None (the hero
    leads: hero, or a named member who is the hero of a one-hero route, or story mode, which
    ignores the option) or the list New Game's select offers (any)."""
    if value is None:
        return "hero"
    if isinstance(value, list):
        return "any"
    if value not in CHARACTER_NAMES:
        raise ValueError(f"unknown member {value!r}")
    return value.lower()


def _members(value):
    if not isinstance(value, list) or any(n not in CHARACTER_NAMES for n in value):
        raise ValueError(f"not a list of members: {value!r}")
    return value


def _locations(value):
    if not isinstance(value, (list, tuple)) or any(n not in LOCATION_NAME_TO_ID for n in value):
        raise ValueError("not a list of this world's locations")
    return value


# The one table: slot data key -> (option, read, absent). `read` turns the slot value into what the
# option's from_any takes; `absent` is what a room without the key means (REQUIRED: it can't be
# restored; HUB: required in hub mode).
SLOT_OPTIONS = {
    "goal": ("goal", _same, REQUIRED),
    "world_mode": ("world_mode", _same, REQUIRED),
    "hub_start": ("hub_start", _same, HUB),
    "fienal_requirement": ("fienal_requirement", _same, HUB),
    # Written `open_world and expel_teleporter`: false outside open world, where nothing reads it.
    "expel_teleporter": ("expel_teleporter", _same, REQUIRED),
    "shuffle_shops": ("shuffle_shops", _same, REQUIRED),
    "story_route": ("story_route", _same, REQUIRED),
    "starting_character": ("starting_character", _starting_character, REQUIRED),
    "exclude_characters": ("exclude_characters", _members, REQUIRED),
    # Written only where the bench applies (0.2.1): absent means off or ignored, and 0.2.0 had none.
    "bench": ("bench", _same, False),
    "missable_locations": ("missable_locations", _same, REQUIRED),
    "expel_progression": ("expel_progression", _same, REQUIRED),
    "cave_of_trials": ("cave_of_trials", _same, REQUIRED),
    "boss_checks": ("boss_checks", _same, REQUIRED),
    # Forced true in hub mode, where nothing reads it (characters are always items there).
    "character_items": ("character_items", _same, REQUIRED),
    "enemy_dropsanity": ("enemy_dropsanity", _same, REQUIRED),
    "drop_progression": ("drop_progression", _same, REQUIRED),
    "area_keys_in_drops": ("area_keys_in_drops", _same, REQUIRED),
    "craftsanity": ("craftsanity", _same, REQUIRED),
    "no_fail_crafting": ("no_fail_crafting", _same, REQUIRED),
    "killer_move_checks": ("killer_move_checks", _same, REQUIRED),
    "killer_move_progression": ("killer_move_progression", _same, REQUIRED),
    "skillsanity": ("skillsanity", _same, REQUIRED),
    "skillsanity_shop_share": ("skillsanity_shop_share", _same, REQUIRED),
    "traps": ("traps", _same, REQUIRED),
    "exp_multiplier": ("exp_multiplier", _same, REQUIRED),
    "fol_multiplier": ("fol_multiplier", _same, REQUIRED),
    "encounter_rate": ("encounter_rate", _same, REQUIRED),
    "difficulty": ("difficulty", _same, REQUIRED),
    "death_link": ("death_link", _same, REQUIRED),
    # 0.2.2: UT marks them excluded (it runs exclusion_rules after set_rules); older rooms: none.
    "exclude_locations": ("exclude_locations", _locations, ()),
}

# The options the slot data doesn't carry: none changes the regions, locations, rules or events UT
# builds (test_ut.py: every option is in SLOT_OPTIONS or here, and the world's code reads none of
# these).
NO_EFFECT = {
    "progression_balancing": "fill only",
    "accessibility": "fill only",
    "local_items": "item rules (locality), fill only",
    "non_local_items": "item rules (locality), fill only",
    "start_inventory": "the server sends the starting items as received items (UT drops the rebuilt world's)",
    "start_hints": "the server's hints",
    "start_location_hints": "the server's hints",
    "priority_locations": "fill only",
    "item_links": "the server's item groups; fill only",
    "plando_items": "fill only",
}


def passthrough(world):
    """The room's slot data UT hands back (re_gen_passthrough), or None in a real generation."""
    data = (getattr(world.multiworld, "re_gen_passthrough", None) or {}).get(world.game)
    return data if data else None


class TrackerRefusal(Exception):
    """Stands in for UT's TrackerException when UT isn't installed (tests, scripts)."""
    def __init__(self, *args, message):
        self.message = message
        super().__init__(*args or (message,))


def refuse(data, why, install=None):
    """Raise UT's TrackerException (it shows "your options are incompatible" and /faris_asked
    prints the message), or TrackerRefusal without UT. The message names the so2.apworld to
    install: the room's own (its slot data's apworld_version; 0.1.x wrote none)."""
    from .world import APWORLD_VERSION
    made = data.get("apworld_version") if isinstance(data, dict) else None
    if isinstance(made, str):
        newer = _version(made) > _version(APWORLD_VERSION)
        origin = f"so2.apworld {made}"
        install = install or (f"It needs so2.apworld {made}{' or later' if newer else ''}: install that "
                              f"so2.apworld beside tracker.apworld (the one the room was generated with).")
    else:
        origin = "an so2.apworld older than 0.2.0 (its slot data names no version)"
        install = install or "Install the so2.apworld the room was generated with beside tracker.apworld."
    message = (f"so2.apworld {APWORLD_VERSION} can't track this room: {why}. "
               f"This room was made with {origin}. {install}")
    logging.error(message)
    try:
        from worlds.tracker import TrackerException
    except Exception:       # UT isn't installed (or can't be imported): a plain exception
        raise TrackerRefusal(message=message) from None
    raise TrackerException(message, message=message)


def _version(text):
    try:
        return tuple(int(p) for p in text.split("."))
    except (AttributeError, ValueError):
        return ()


def restore(world, data):
    """At the top of generate_early: the room's logic version, then every option of SLOT_OPTIONS
    from the slot data (into world.options, through the option's from_any)."""
    if not isinstance(data, dict):
        refuse({}, "its slot data is not a dictionary")
    version = data.get("logic_version", 1)
    if version not in LOGIC_VERSIONS:
        why = (f"its logic is version {version}, newer than this world's {LOGIC_VERSION}"
               if isinstance(version, int) and version > LOGIC_VERSION
               else f"its logic is version {version!r}, which this world doesn't reproduce")
        refuse(data, why)
    if "logic_version" not in data and data.get("apworld_version") in OLD_LOGIC:
        changes, install = OLD_LOGIC[data["apworld_version"]]
        for differs, why in changes:
            if differs(data):
                refuse(data, why, install)
    hub_mode = data.get("world_mode") == "hub"
    o = world.options
    for key, (name, read, absent) in SLOT_OPTIONS.items():
        option = getattr(o, name)
        if key in data:
            value = data[key]
        elif absent == REQUIRED or (absent == HUB and hub_mode):
            refuse(data, f"its slot data has no {key!r}")
        elif absent == HUB:
            value = type(option).default
        else:
            value = absent
        try:
            setattr(o, name, type(option).from_any(read(value)))
        except Exception as e:
            refuse(data, f"its {key!r} ({value!r}) is not a value this world knows ({e})")
    if not isinstance(data.get("characters"), list) or any(n not in CHARACTER_NAMES for n in data["characters"]):
        refuse(data, "its slot data has no party pool ('characters')")
    if hub_mode and data.get("hub_start_town") not in HUB_STARTS:
        refuse(data, f"its hub start town ({data.get('hub_start_town')!r}) is not one this world knows")
    if "logic_version" not in data:
        for applies, warning in OLD_LOGIC_WARNINGS.get(data.get("apworld_version"), ()):
            if applies(data):
                logging.warning(warning)


def check_restored(world, data):
    """At the end of generate_early: the rebuilt world's slot data (SO2World.slot_options, as JSON
    makes it) is the room's, but for the world version and 0.2.2's keys."""
    mine = json.loads(json.dumps(world.slot_options()))
    skip = {"apworld_version", *NEW_KEYS}
    for key in sorted((set(mine) | set(data)) - skip):
        if mine.get(key) != data.get(key):
            refuse(data, f"the world rebuilt from its slot data differs from the room's "
                         f"({key}: the room's {data.get(key)!r}, rebuilt {mine.get(key)!r})")


# ---- The tracker tab's order (custom_ut_sort) ----------------------------------------------------
# An area's story stage (data.STAGES, 1-23): its locations' most common entrance group's (open
# world's key groups, data.KEY_GROUPS `stage`); an area the story never enters goes after its
# world's stages (Expel's after the disc switch, Energy Nede's after the last), the Cave of Trials
# after both; the checks that are no place (skill guilds, learn points, Item Creation's specialties,
# killer moves by member) after every area, in that order.
_STAGE_OF_GROUP = {g["group"]: g["stage"] for g in KEY_GROUPS}
_AFTER_WORLD = {"Expel": DISC_SWITCH_STAGE + 0.5, "Energy Nede": len(STAGES) + 0.5, "Fienal": len(STAGES) + 0.5,
                "Cave of Trials": len(STAGES) + 1}
_BOARDS = {"guild": 0, "learn": 1, "craft": 2, "killer_move": 3}
_BOARD_STAGE = len(STAGES) + 2


def _area_stages():
    groups, regions = {}, {}
    for row in ALL_ROWS:
        if row["type"] in _BOARDS:
            continue
        counts = groups.setdefault(row["area"], {})
        group = row.get("group")
        if group is not None and _STAGE_OF_GROUP.get(group):
            counts[group] = counts.get(group, 0) + 1
        regions.setdefault(row["area"], row["region"])
    stages = {}
    for area, counts in groups.items():
        if counts:
            top = max(counts.values())
            stages[area] = min(_STAGE_OF_GROUP[g] for g, n in counts.items() if n == top)
        else:
            stages[area] = _AFTER_WORLD[regions[area]]
    return stages


_AREA_STAGE = _area_stages()
SORT_KEY = {}
for _row in ALL_ROWS:
    if _row["type"] in _BOARDS and _row["area"] not in _AREA_STAGE:    # a cave level's learn chest stays there
        SORT_KEY[_row["name"]] = (_BOARD_STAGE + _BOARDS[_row["type"]], _row["area"], _row["name"])
    else:
        SORT_KEY[_row["name"]] = (_AREA_STAGE[_row["area"]], _row["area"], _row["name"])
_LAST = _BOARD_STAGE + len(_BOARDS)


def sort_key(region, location):
    """custom_ut_sort's key: (story stage of the area, area, name); a line that is no location of
    ours (an entrance, a message) after them all, by region and text."""
    return SORT_KEY.get(location, (_LAST, region or "", location or ""))
