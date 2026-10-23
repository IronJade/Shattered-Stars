"""Rules. Story mode's only requirements are the story's own order (the region entrances in
regions.py); here: the goal, and where progression may not go.

Archipelago assumes a location stays reachable once it is in logic. Star Ocean 2's maps close
as the story goes on (Expel at the disc switch, towns that change), so a location may hold
progression only when the late proof (tools/ap_late_proof.py, docs/ap/late_proof.json; the
rows' `late`) checked it in a sweep of the game after the Final Save Point, the point the
story always reaches, with every location left undone. Story bosses are exempt: the story
fights them. First drops are dice rolls, and join sites need their member's story: never
progression. expel_progression off keeps progression out of Expel as well.
"""
from worlds.generic.Rules import add_item_rule

from .items import EVENT_FINAL_SAVE_POINT, EVENT_VICTORY
from .locations import ROW


def proven_late(row, side):
    """Whether the late proof reached this location in the game(s) the player may play (side
    None: either game, so both)."""
    games = ("claude", "rena") if side is None else (side,)
    return all(g in row.get("late", ()) for g in games)


def may_hold_progression(row, side, expel_progression=True):
    if row["type"] in ("drop", "join"):
        return False
    if row["region"] == "Expel" and not expel_progression:
        return False
    if row["type"] == "boss" and row.get("boss") == "story":
        return True
    return proven_late(row, side)


def set_rules(world):
    player = world.player
    world.get_location("Indalecio").access_rule = lambda state: state.has(EVENT_FINAL_SAVE_POINT, player)
    world.multiworld.completion_condition[player] = lambda state: state.has(EVENT_VICTORY, player)
    side = {0: None, 1: "claude", 2: "rena"}[world.options.protagonist.value]
    for loc in world.multiworld.get_locations(player):
        if loc.address is None:
            continue
        if not may_hold_progression(ROW[loc.name], side, bool(world.options.expel_progression)):
            add_item_rule(loc, lambda item: not item.advancement)
