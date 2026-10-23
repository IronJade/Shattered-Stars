"""Rules. Story mode's only requirements are the story's own order (the region entrances in
regions.py); here: the goal, and where progression may not go. Open world adds the area keys
(docs/ap/apworld.md, "Open world").

Archipelago assumes a location stays reachable once it is in logic. Star Ocean 2's maps close
as the story goes on (Expel at the disc switch, towns that change), so a location may hold
progression only when the late proof (tools/ap_late_proof.py, docs/ap/late_proof.json; the
rows' `late`) checked it in a sweep of the game after the Final Save Point, the point the
story always reaches, with every location left undone. Story bosses are exempt: the story
fights them. First drops are dice rolls, join sites need their member's story and shop slots a
price: never progression. expel_progression off keeps progression out of Expel as well.

Open world: the story's stages need the keys of the areas they walk into (regions.py
stage_rules), and a location needs its area's key from the stage the story first enters the
area (or the teleporter's or the late proof's route keys). Our keys are needed before the
Final Save Point, so a skipped Expel location must be reachable again after the disc switch:
only the teleporter proof (chapter 38, through Fun City's arena; the rows' `tele`) says where,
so Expel holds progression only there, except the Expel story's own keys, which the player
must pick up before the switch anyway.
"""
from worlds.generic.Rules import add_item_rule

from .data import DISC_SWITCH_STAGE, KEY_GROUPS, STAGES
from .items import EVENT_FINAL_SAVE_POINT, EVENT_TELEPORTER, EVENT_VICTORY, EXPEL_STORY_KEYS, KEY_NAME, stage_event
from .locations import ROW
from .regions import stage_rules

STAGE_OF_GROUP = {g["group"]: g["stage"] for g in KEY_GROUPS}
WORLD_OF_GROUP = {g["group"]: g["world"] for g in KEY_GROUPS}


def in_expel(row):
    """Expel's maps for open world: the Expel region, and an Energy Nede row whose map is entered
    through an Expel entrance (Lacour's King's Room, map 429, a chapter-60 scene)."""
    return row["region"] == "Expel" or (row["region"] == "Energy Nede" and not row.get("group_near")
                                        and WORLD_OF_GROUP.get(row.get("group")) == 1)
FIENAL_STAGE = len(STAGES)      # Fienal's floors above the first visit's open in the last chapters


def games_of(side):
    return ("claude", "rena") if side is None else (side,)


def proven_late(row, side):
    """Whether the late proof reached this location in the game(s) the player may play (side
    None: either game, so both)."""
    return all(g in row.get("late", ()) for g in games_of(side))


def proven_teleporter(row, side):
    """Whether the teleporter proof (chapter 38, Fun City's arena to Arlia) reached it."""
    return all(g in row.get("tele", ()) for g in games_of(side))


def may_hold_progression(row, side, expel_progression=True):
    if row["type"] in ("drop", "join", "shop"):
        return False
    if row["region"] == "Expel" and not expel_progression:
        return False
    if row["type"] == "boss" and row.get("boss") == "story":
        return True
    return proven_late(row, side)


def stage_of_chapter(ch):
    for k, (_, (lo, hi), _) in enumerate(STAGES, start=1):
        if lo <= ch <= hi:
            return k
    return 1 if ch < STAGES[0][1][0] else len(STAGES)


def open_world_access(row, side, teleporter):
    """Open world: (stage needed, keys needed) pairs, any of which reaches the location; the
    stage is "the stage before it is finished" (0: the start). Event names stand for stages
    where noted: EVENT_FINAL_SAVE_POINT, EVENT_TELEPORTER."""
    modes = []
    kind, region, group = row["type"], row["region"], row.get("group")
    if kind == "boss" and row.get("boss") == "story":
        return [(stage_event(stage_of_chapter(row["chapter"])), ())]
    if kind == "drop":
        groups = row.get("groups", [])
        if row.get("on_world_map") or any(STAGE_OF_GROUP.get(g) for g in groups):
            return [(EVENT_FINAL_SAVE_POINT, ())]
        return [(EVENT_FINAL_SAVE_POINT, (KEY_NAME[g],)) for g in groups] or [(EVENT_FINAL_SAVE_POINT, ())]
    stage = STAGE_OF_GROUP.get(group)
    if region == "Energy Nede" and WORLD_OF_GROUP.get(group) == 1:
        stage = None    # after the switch an Expel entrance is reached only through the teleporter
    if region == "Fienal":
        stage = FIENAL_STAGE
    if stage and region != "Cave of Trials":
        if row.get("from_chapter"):
            stage = max(stage, stage_of_chapter(row["from_chapter"]))
        modes.append((stage_event(stage - 1) if stage > 1 else None, (KEY_NAME[group],) if group else ()))
    if teleporter and in_expel(row) and proven_teleporter(row, side):
        modes.append((EVENT_TELEPORTER, tuple(KEY_NAME[g] for g in row.get("tele_keys", ()))))
    if proven_late(row, side):
        modes.append((EVENT_FINAL_SAVE_POINT, tuple(KEY_NAME[g] for g in row.get("late_keys", ()))))
    if not modes:   # unproven, outside the story's areas: after the Final Save Point with its key
        modes.append((EVENT_FINAL_SAVE_POINT, (KEY_NAME[group],) if group else ()))
    return modes


def open_world_progression(row, side, teleporter, expel_progression=True):
    """Open world: what a location may hold: "any", "expel keys" (non-progression or our
    Expel-story keys) or "none" (non-progression). expel_progression off still lets our
    Expel-story keys into Expel: the disc switch can't be reached without them."""
    if row["type"] in ("drop", "join", "shop"):
        return "none"
    if not in_expel(row):
        return "any" if may_hold_progression(row, side) else "none"
    full = "any" if expel_progression else "expel keys"
    if row["type"] == "boss" and row.get("boss") == "story":
        return full
    if teleporter and proven_teleporter(row, side):
        return full
    return "expel keys" if proven_late(row, side) else "none"


def set_rules(world):
    player = world.player
    world.get_location("Indalecio").access_rule = lambda state: state.has(EVENT_FINAL_SAVE_POINT, player)
    world.multiworld.completion_condition[player] = lambda state: state.has(EVENT_VICTORY, player)
    side = {0: None, 1: "claude", 2: "rena"}[world.options.protagonist.value]
    teleporter = world.open_world and bool(world.options.expel_teleporter)
    if world.open_world:
        stage_rules(world)
    for loc in world.multiworld.get_locations(player):
        if loc.address is None:
            continue
        row = ROW[loc.name]
        expel = bool(world.options.expel_progression)
        allowed = (open_world_progression(row, side, teleporter, expel) if world.open_world
                   else "any" if may_hold_progression(row, side, expel) else "none")
        if allowed == "none":
            add_item_rule(loc, lambda item: not item.advancement)
        elif allowed == "expel keys":
            add_item_rule(loc, lambda item: not item.advancement
                          or (item.player == player and item.name in EXPEL_STORY_KEYS))
        if world.open_world:
            modes = open_world_access(row, side, teleporter)
            loc.access_rule = lambda state, modes=modes: any(
                (ev is None or state.has(ev, player)) and state.has_all(keys, player) for ev, keys in modes)


__all__ = ["set_rules", "may_hold_progression", "proven_late", "proven_teleporter", "open_world_access",
           "DISC_SWITCH_STAGE"]
