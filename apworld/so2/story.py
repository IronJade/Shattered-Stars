"""Story awareness for trackers in story mode (docs/plans/trackers.md B6, decision D8; docs/ap/apworld.md
"Trackers", "Story awareness"; docs/ap/data_storage.md).

Story mode's stages have no item requirements, so nearly every check is in logic from the start. The
port publishes how far the story has got (`so2_story_{team}_{slot}` = {"chapter": C, "step": S}, the
script globals G[0xa0] / G[0xa4]); with it a tracker shows only what the story has reached. The one
source of truth is here, for both trackers:

- **The points** (POINTS): stage k of data.STAGES is reached when the port reports its first chapter
  (each chapter value is stored in exactly one place, so a stage starts where the story stores its
  first chapter: docs/ap/field.md "Story order"; tools/ap_world_tables.py STAGES); a 24th point,
  the Final Save Point, at chapter 60 step 4000 (FINAL_SAVE_POINT, below). `reached(value)` is the
  number of points at or before the reported position.
- **A location's stage** (location_stage): the first stage in which the story can take the party
  to it, from the data the open world's logic already uses (rules.early_stage: the stage the story
  first walks into the area's entrance group, raised to the stage of a gift's first chapter; a
  story boss's chapter; a first drop's or a guild check's earliest place; a member's story join
  for his or her killer moves and learn levels). Expel is left behind at the disc switch, so an
  Expel place the story first reaches after stage 12 (and an Energy Nede row behind an Expel
  entrance) is reached through Virtual Expel, after the Final Save Point; the Cave of Trials too.
  What has no stage of its own (Item Creation, areas the story never walks into: Lasguss Mountain,
  the Mountain Palace, Giveaway, the Gallery) follows its region: Expel from the start, Energy Nede
  from the disc switch.
- **The events** (EVENT_STAGES): the disc switch (Arrived on Energy Nede) needs stage 13 and the
  Final Save Point the 24th point, so everything the logic hangs on them (Energy Nede, Fienal, the
  Cave of Trials, killer moves and learn levels by level, the goal's events) follows the story too.

So a location is reachable *for the story* when its own stage and every story event its rule needs
are reached. Universal Tracker gets this as deferred events (prepare, reconnect: UT's documented
mechanism, `found_entrances_datastorage_key` + `reconnect_found_entrances`, only in UT's own
regeneration and only while host.yaml's enforce_deferred_entrances is not "off"); the PopTracker pack
gets it as the tracker export's optional `story` key (export; docs/ap/seed_format.md "Story
awareness"), which tools/ap_tracker_logic.py and tools/poptracker/logic.lua apply the same way.
Open world and hub mode have no story awareness (their stages and dungeons are keyed). A real
generation is unchanged: only the export's `story` key is new.
"""
import logging

from BaseClasses import ItemClassification
from rule_builder.rules import False_, Has, True_

from .data import DISC_SWITCH_STAGE, GUILD_PLACES, LOCATIONS, STAGES
from .items import EVENT_ENERGY_NEDE, EVENT_FINAL_SAVE_POINT, SO2Item
from .locations import ROW, SO2Location, fighter_status
from . import logic, rules

# The port's data-storage key (docs/ap/data_storage.md); Universal Tracker fills in {team} / {player}.
STORY_KEY = "so2_story_{team}_{player}"
EXPORT_KEY = "so2_story_{team}_{slot}"      # the same key as the export documents it

# The Final Save Point (Fienal 9F, map 671): chapter 60's step 4000 is stored when Cyril falls (map 674,
# Floor 7, under chapter 60 step 3000), the room before the final ascent to the save point; step 5000
# comes at Indalecio (map 685). The late proof's state, a save made at the Final Save Point, is
# chapter 60 step 4000 (docs/ap/apworld.md "The late proof"). Saving there sets flag 730, which opens
# Virtual Expel; the key carries no flags, so the point is the step before the save.
FINAL_SAVE_POINT = (60, 4000)
FINAL_SAVE_POINT_NAME = "the Final Save Point"
# Stage k (1-based) is reached at POINTS[k - 1]: the 23 stages' first chapters, then the Final Save Point.
POINTS = [(name, lo, 0) for name, (lo, _), _ in STAGES] + [(FINAL_SAVE_POINT_NAME, *FINAL_SAVE_POINT)]
AFTER_FINAL_SAVE_POINT = len(POINTS)        # 24
ENERGY_NEDE_STAGE = DISC_SWITCH_STAGE + 1   # 13: chapter 36 is stored on Energy Nede (map 618)
EVENT_STAGES = {EVENT_ENERGY_NEDE: ENERGY_NEDE_STAGE, EVENT_FINAL_SAVE_POINT: AFTER_FINAL_SAVE_POINT}
REGION_STAGE = {"Menu": 1, "Expel": 1, "Energy Nede": ENERGY_NEDE_STAGE, "Fienal": ENERGY_NEDE_STAGE,
                "Cave of Trials": AFTER_FINAL_SAVE_POINT}

logger = logging.getLogger("so2")


def applies(world):
    """Story awareness exists in story mode only."""
    return not getattr(world, "open_world", False) and not getattr(world, "hub", False)


def position(value):
    """The data-storage value as (chapter, step), or None when it says nothing (never written: None;
    not the port's shape; chapter 0 or below, which no game in progress has)."""
    if not isinstance(value, dict):
        return None
    chapter, step = value.get("chapter"), value.get("step", 0)
    if isinstance(chapter, bool) or not isinstance(chapter, (int, float)) or chapter <= 0:
        return None
    if isinstance(step, bool) or not isinstance(step, (int, float)):
        step = 0
    return int(chapter), int(step)


def reached(value):
    """How many points (stages, then the Final Save Point) the reported story position has reached
    (at least the first: a chapter before 3 is the story's start), or None without a position (no
    story awareness: everything as the logic says)."""
    pos = position(value)
    if pos is None:
        return None
    return max(1, sum(1 for _, chapter, step in POINTS if (chapter, step) <= pos))


def site_stage(row):
    """The stage at which the story first takes the party to a place (a chest, gift, shop slot,
    join site or side boss row, or a drop's or guild's place as rules.place_row makes it)."""
    region = row["region"]
    if region == "Cave of Trials":
        return AFTER_FINAL_SAVE_POINT
    if region == "Energy Nede" and rules.WORLD_OF_GROUP.get(row.get("group")) == 1:
        # An Energy Nede row with an Expel entrance group: Lacour's King's Room (map 429, a chapter-60
        # scene in Lacour Castle) is reached through Virtual Expel; the Fields' guardians, whose group
        # is only the nearest name (group_near), by their own field's entrance.
        own = row.get("area")
        if rules.WORLD_OF_GROUP.get(own) == 2 and rules.STAGE_OF_GROUP.get(own):
            return rules.STAGE_OF_GROUP[own]
        return AFTER_FINAL_SAVE_POINT
    stage = rules.early_stage(row) or REGION_STAGE[region]
    if region == "Expel" and stage > DISC_SWITCH_STAGE:
        return AFTER_FINAL_SAVE_POINT        # Expel after the disc switch: Virtual Expel
    return stage


def place_stage(place):
    """A drop's or guild check's place: a world map counts from its world's first stage."""
    if place.get("world_map"):
        return REGION_STAGE[place["region"]]
    return site_stage(rules.place_row(place))


def _join_stages():
    """Per (member, story_route "claude" / "rena"): the first stage of the member's join sites in that game."""
    out = {}
    for row in LOCATIONS:
        if row["type"] == "join":
            for side in ("claude", "rena"):
                if row["story_route"] in (None, side):
                    key = (row["character"], side)
                    out[key] = min(out.get(key, 99), site_stage(row))
    return out


JOIN_STAGES = _join_stages()


def member_stage(world, who):
    """The first stage at which a member can be in the party: a member who joins by a character item
    may come at any time; one who joins through the story, at his or her first join site."""
    if fighter_status(who, world.party_info) in ("item", None):
        return 1
    side = {0: None, 1: "claude", 2: "rena"}[world.options.story_route.value]
    sides = ("claude", "rena") if side is None else (side,)
    return min((JOIN_STAGES[who, s] for s in sides if (who, s) in JOIN_STAGES), default=1)


def location_stage(world, row):
    """The stage (1-24: POINTS) from which the story can take the party to this location; its rule's
    story events (EVENT_STAGES) come on top."""
    kind = row["type"]
    if row["region"] == "Cave of Trials":
        return AFTER_FINAL_SAVE_POINT
    if kind == "boss" and row.get("boss") == "story":
        stage = rules.stage_of_chapter(row["chapter"])
        return AFTER_FINAL_SAVE_POINT if row["region"] == "Expel" and stage > DISC_SWITCH_STAGE else stage
    if kind == "drop":
        return min((place_stage(p) for p in row["places"]), default=REGION_STAGE[row["region"]])
    if kind == "guild":
        return min((place_stage(GUILD_PLACES[m]) for m in row["guild_maps"]), default=1)
    if kind in ("killer_move", "learn") and row.get("character"):
        return member_stage(world, row["character"])
    if kind in ("craft", "learn", "killer_move"):
        return REGION_STAGE[row["region"]]
    return site_stage(row)


def overlay(world):
    """(event stages, location stages) of this world: {event item name: stage} for the story events its
    events need, {AP location id: stage} for every location; None outside story mode."""
    if not applies(world):
        return None
    locations = {loc.address: location_stage(world, ROW[loc.name])
                 for loc in world.multiworld.get_locations(world.player) if loc.address is not None}
    have = {loc.item.name for loc in world.multiworld.get_locations(world.player)
            if loc.address is None and loc.item is not None}
    return {name: k for name, k in EVENT_STAGES.items() if name in have}, locations


def export(world):
    """The tracker export's optional `story` key (docs/ap/seed_format.md "Story awareness"), or None
    outside story mode: the key, the points, the events that need a stage, and the locations that need
    one past the first, grouped by stage."""
    found = overlay(world)
    if found is None:
        return None
    events, locations = found
    groups = {}
    for address, k in sorted(locations.items()):
        if k > 1:
            groups.setdefault(k, []).append(address)
    return {"key": EXPORT_KEY,
            "stages": [[name, chapter, step] for name, chapter, step in POINTS],
            "events": [[name, k] for name, k in sorted(events.items(), key=lambda kv: (kv[1], kv[0]))],
            "locations": [[k, *groups[k]] for k in sorted(groups)]}


# ---- Universal Tracker: deferred story events ----------------------------------------------------

def event_name(k):
    """The deferred event of point k (UT's regeneration only): reached when the port reports it."""
    return f"Story reached: {POINTS[k - 1][0]}"


def ut_enabled(world):
    """Story awareness in Universal Tracker: its own regeneration of a story-mode room, with
    enforce_deferred_entrances not "off" (absent: off, as in UT's fuzzer and in a real generation)."""
    mw = world.multiworld
    return (world.ut_slot_data is not None and applies(world)
            and getattr(mw, "enforce_deferred_connections", "off") not in ("off", None))


def prepare(world):
    """UT's regeneration (after set_rules): a deferred event per point past the first, in Menu, open
    until the port reports a position (none yet: everything as the logic says); every location and
    story event of the overlay needs its point's event on top of its own rule. Afterwards
    `found_entrances_datastorage_key` on this world makes UT watch the port's key."""
    if not ut_enabled(world):
        return
    events, locations = overlay(world)
    menu = world.get_region("Menu")
    world.story_events = {}
    for k in range(2, len(POINTS) + 1):
        name = event_name(k)
        loc = SO2Location(world.player, name, None, menu)
        menu.locations.append(loc)
        loc.place_locked_item(SO2Item(name, ItemClassification.progression, None, world.player))
        world.set_rule(loc, True_())
        world.story_events[k] = loc
    for loc in world.multiworld.get_locations(world.player):
        if loc.address is not None:
            k = locations[loc.address]
        elif loc.item is not None and loc.item.name in events:
            k = events[loc.item.name]
        else:
            continue
        if k > 1:
            world.set_rule(loc, logic.both(logic.as_rule(loc.access_rule), Has(event_name(k))))
    world.found_entrances_datastorage_key = (STORY_KEY,)
    world.story_reached = None


def reconnect(world, key, value):
    """UT's reconnect_found_entrances for the story key: the events of the points the reported position
    has reached are open, the later ones shut (a position that goes back, an earlier save loaded, shuts
    them again); a value that says nothing opens them all."""
    if not getattr(world, "story_events", None) or not key.startswith("so2_story_"):
        return
    k_reached = reached(value)
    if value is not None and k_reached is None:
        logger.warning("Star Ocean: The Second Story: %s holds %r, not a story position; showing what the "
                       "logic says without the story", key, value)
    world.story_reached = k_reached
    for k, loc in world.story_events.items():
        world.set_rule(loc, True_() if k_reached is None or k <= k_reached else False_())


__all__ = ["STORY_KEY", "POINTS", "FINAL_SAVE_POINT", "EVENT_STAGES", "applies", "position", "reached",
           "location_stage", "overlay", "export", "event_name", "ut_enabled", "prepare", "reconnect"]
