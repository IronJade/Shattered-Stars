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

from .items import EVENT_ENERGY_NEDE, EVENT_FINAL_SAVE_POINT, EVENT_VICTORY, SO2Item
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
    for region, name, item in EVENTS:
        loc = SO2Location(player, name, None, regions[region])
        regions[region].locations.append(loc)
        loc.place_locked_item(SO2Item(item, ItemClassification.progression, None, player))
    return regions
