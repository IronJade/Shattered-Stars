"""Rules. Story mode's only requirements are the story's own order (the region entrances in
regions.py); here: the goal, and where progression may not go (first drops; Expel when the
player asks)."""
from worlds.generic.Rules import add_item_rule

from .items import EVENT_FINAL_SAVE_POINT, EVENT_VICTORY
from .locations import ROW


def set_rules(world):
    player = world.player
    world.get_location("Indalecio").access_rule = lambda state: state.has(EVENT_FINAL_SAVE_POINT, player)
    world.multiworld.completion_condition[player] = lambda state: state.has(EVENT_VICTORY, player)
    for loc in world.multiworld.get_locations(player):
        if loc.address is None:
            continue
        row = ROW[loc.name]
        # A first drop is a dice roll (some odds are low): never progression there.
        no_progression = row["type"] == "drop"
        if row["region"] == "Expel" and not world.options.expel_progression:
            no_progression = True
        if no_progression:
            add_item_rule(loc, lambda item: not item.advancement)
