"""Regions follow the story (docs/ap/field.md, "Story order", "Virtual Expel"):

    Menu -> Expel -> Energy Nede -> Fienal -> Cave of Trials (option)

- Expel: disc 1. The disc switch (map 625, chapter 35) leaves it behind, but after the Final
  Save Point (map 671) Fun City's arena reopens it as Virtual Expel, with the same chests and
  opened flags; so its locations are never lost (the time-limited ones are marked missable).
- Energy Nede: disc 2 (with the hub maps 625, 654-658, 687), entered at the disc switch.
- Fienal: maps 647-685, the final dungeon: the Final Save Point (map 671), then Indalecio
  (map 685, encounters 539 / 540), the goal.
- Cave of Trials: maps 698-825, in Virtual Expel, so after the Final Save Point.

Story mode has no progression items of its own: the story's key items are script flags that
stay in place. The entrances hang on three events, so the spheres follow the story.
"""
from BaseClasses import ItemClassification, Region

from .data import DISC_SWITCH_STAGE, STAGES
from .items import EVENT_ENERGY_NEDE, EVENT_FINAL_SAVE_POINT, EVENT_TELEPORTER, EVENT_VICTORY, KEY_NAME, SO2Item, \
    stage_event
from .locations import SO2Location

REGIONS = ("Menu", "Expel", "Energy Nede", "Fienal", "Cave of Trials")
EVENTS = (  # (region, location name, event item)
    ("Expel", "Disc Switch (Eluria Tower)", EVENT_ENERGY_NEDE),
    ("Fienal", "Final Save Point (Fienal 9F)", EVENT_FINAL_SAVE_POINT),
    ("Fienal", "Indalecio", EVENT_VICTORY),
)


def create_regions(world):
    mw, player = world.multiworld, world.player
    regions = {name: Region(name, player, mw) for name in REGIONS}
    mw.regions += regions.values()
    regions["Menu"].connect(regions["Expel"], "New Game")
    regions["Expel"].connect(regions["Energy Nede"], "Disc Switch",
                             lambda state: state.has(EVENT_ENERGY_NEDE, player))
    regions["Energy Nede"].connect(regions["Fienal"], "To Fienal")
    regions["Fienal"].connect(regions["Cave of Trials"], "Virtual Expel",
                              lambda state: state.has(EVENT_FINAL_SAVE_POINT, player))
    events = list(EVENTS)
    if world.open_world:
        # Open world: the story's stages as events, each needing the previous one and the keys of
        # the areas the story walks into in it (data.STAGES; docs/ap/apworld.md, "Open world").
        for k, (name, _, _) in enumerate(STAGES, start=1):
            events.append(("Expel" if k <= DISC_SWITCH_STAGE else "Energy Nede", f"Story: {name}", stage_event(k)))
        if world.options.expel_teleporter:
            events.append(("Energy Nede", "Expel Teleporter (Fun City arena)", EVENT_TELEPORTER))
    for region, name, item in events:
        loc = SO2Location(player, name, None, regions[region])
        regions[region].locations.append(loc)
        loc.place_locked_item(SO2Item(item, ItemClassification.progression, None, player))
    return regions


def stage_rules(world):
    """Open world: the events' rules (the stages, the disc switch, the teleporter, the Final Save
    Point)."""
    player = world.player
    for k, (name, _, keys) in enumerate(STAGES, start=1):
        need = [KEY_NAME[g] for g in keys]
        prev = stage_event(k - 1) if k > 1 else None
        world.get_location(f"Story: {name}").access_rule = \
            lambda state, prev=prev, need=need: (
                (prev is None or state.has(prev, player)) and state.has_all(need, player))
    world.get_location("Disc Switch (Eluria Tower)").access_rule = \
        lambda state: state.has(stage_event(DISC_SWITCH_STAGE), player)
    world.get_location("Final Save Point (Fienal 9F)").access_rule = \
        lambda state: state.has(stage_event(len(STAGES)), player)
    if world.options.expel_teleporter:
        # Chapter 38 (after stage 13, the scenes after the switch): the arena's old man.
        world.get_location("Expel Teleporter (Fun City arena)").access_rule = \
            lambda state: state.has_all((stage_event(DISC_SWITCH_STAGE + 1), KEY_NAME["Fun City"]), player)
