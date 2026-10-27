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

from .data import DISC_SWITCH_STAGE, STAGES, STORY_GATES, TELEPORTER_STAGE
from .items import EVENT_ENERGY_NEDE, EVENT_FINAL_SAVE_POINT, EVENT_ISERIA_QUEEN, EVENT_LIMITER_OFF, EVENT_TELEPORTER, \
    EVENT_VICTORY, KEY_NAME, SO2Item, stage_event
from .locations import SO2Location
from .logic import Has, need

REGIONS = ("Menu", "Expel", "Energy Nede", "Fienal", "Cave of Trials")
EVENTS = (  # (region, location name, event item)
    ("Expel", "Disc Switch (Eluria Tower)", EVENT_ENERGY_NEDE),
    ("Fienal", "Final Save Point (Fienal 9F)", EVENT_FINAL_SAVE_POINT),
    ("Fienal", "Indalecio", EVENT_VICTORY),
)
# The goals' events (goal 1 and 2, goal 2): Filia's scene in Central City City Hall after the Final
# Save Point (flag 556, Indalecio's limiter off), and the Iseria Queen at the bottom of the Cave
# of Trials (summoned by the song composed from G-Celesta's Silver Trumpet).
LIMITER_OFF_EVENT = ("Energy Nede", "Limiter Off (Filia, Central City)", EVENT_LIMITER_OFF)
ISERIA_QUEEN_EVENT = ("Cave of Trials", "Iseria Queen (Cave of Trials)", EVENT_ISERIA_QUEEN)


def create_regions(world):
    mw, player = world.multiworld, world.player
    regions = {name: Region(name, player, mw) for name in REGIONS}
    mw.regions += regions.values()
    regions["Menu"].connect(regions["Expel"], "New Game")
    regions["Expel"].connect(regions["Energy Nede"], "Disc Switch", Has(EVENT_ENERGY_NEDE))
    regions["Energy Nede"].connect(regions["Fienal"], "To Fienal")
    regions["Fienal"].connect(regions["Cave of Trials"], "Virtual Expel", Has(EVENT_FINAL_SAVE_POINT))
    events = list(EVENTS)
    goal = world.options.goal.value
    events += ([LIMITER_OFF_EVENT] if goal >= 1 else []) + ([ISERIA_QUEEN_EVENT] if goal == 2 else [])
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


def gate_groups(world, gate):
    """The entrance groups whose keys a one-way story point needs in this world: its own, and
    with the Expel teleporter and expel_progression on (Nede keys and other worlds' items may
    then sit on the Cross and Lacour continents, which the teleporter reopens only from the
    Synard) the keys of the story's stages until then (`teleporter_groups`)."""
    later = world.options.expel_teleporter and world.options.expel_progression
    return list(gate["groups"]) + (list(gate["teleporter_groups"]) if later else [])


def gate_keys(world, gate):
    """gate_groups' key numbers (the seed door's `keys`)."""
    numbers = dict(zip(gate["groups"] + gate["teleporter_groups"], gate["keys"] + gate["teleporter_keys"]))
    return [numbers[g] for g in gate_groups(world, gate)]


def stage_rules(world):
    """Open world: the events' rules (the stages, the disc switch, the teleporter, the Final Save
    Point). A stage that ends with a one-way story point (data.STORY_GATES: the El voyage at the
    end of the Lacour Front Line stage) also needs the gate's keys: the port does not let the
    story past it without them."""
    gated = {}
    for gate in STORY_GATES:
        gated.setdefault(gate["stage"], []).extend(KEY_NAME[g] for g in gate_groups(world, gate))
    for k, (name, _, keys) in enumerate(STAGES, start=1):
        prev = [stage_event(k - 1)] if k > 1 else []
        world.set_rule(world.get_location(f"Story: {name}"),
                       need(prev + [KEY_NAME[g] for g in keys] + gated.get(k, [])))
    world.set_rule(world.get_location("Disc Switch (Eluria Tower)"), Has(stage_event(DISC_SWITCH_STAGE)))
    world.set_rule(world.get_location("Final Save Point (Fienal 9F)"), Has(stage_event(len(STAGES))))
    if world.options.expel_teleporter:
        # The arena's old man, from chapter 36; but Fun City lies on another island of Energy
        # Nede's world map than Central City, reached by the Synard (stage 16, the Cave of Red
        # Crystal; data.TELEPORTER_STAGE).
        world.set_rule(world.get_location("Expel Teleporter (Fun City arena)"),
                       need((stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"])))
