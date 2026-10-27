"""Rules. Story mode's only requirements are the story's own order (the region entrances in
regions.py); here: the goal, and where progression may not go. Open world adds the area keys
(docs/ap/apworld.md, "Open world").

Archipelago assumes a location stays reachable once it is in logic. Star Ocean 2's maps close
as the story goes on (Expel at the disc switch, towns that change), so a location may hold
progression only when the late proof (tools/ap_late_proof.py, docs/ap/late_proof.json; the
rows' `late`) checked it in a sweep of the game after the Final Save Point, the point the
story always reaches, with every location left undone. Story bosses are exempt: the story
fights them. First drops are dice rolls, join sites need their member's story, shop slots a
price and gift sites are never progression (the user, 2026-10-25: an NPC gift check should
never hold progression, our own keys or another world's alike, whatever the proof reaches).
expel_progression off keeps progression out of Expel as well.

Open world: the story's stages need the keys of the areas they walk into (regions.py
stage_rules), and a location needs its area's key from the stage the story first enters the
area, plus the key of the harbour its ship lands at (Lacour, Hoffmann Ruins), or the
teleporter's or the late proof's route keys. Our keys are needed before the
Final Save Point, so a skipped Expel location must be reachable again after the disc switch:
only the teleporter proof (chapter 38's flags, through Fun City's arena, which the party
reaches once the Synard flies, data.TELEPORTER_STAGE; the rows' `tele`) says where,
so Expel holds progression only there, except the Expel story's own keys, which the player
must pick up before the switch anyway.

Open world's one-way story points (data.STORY_GATES; docs/ap/apworld.md, "The El voyage"): the
stage that ends with one needs the gate's keys (regions.py stage_rules), the port keeps it shut
until the player holds them, and a location the story reaches before it (its early mode) holds
progression only where the gate's proof reached it at the gate: the rest can close before the
player is back, and whatever the gate's keys hang on must be in reach there.
"""
from BaseClasses import LocationProgressType
from worlds.generic.Rules import add_item_rule

from .data import DISC_SWITCH_STAGE, KEY_GROUPS, STAGES, STORY_GATES
from .items import ALL_KEY_IDS, EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN, EVENT_FINAL_SAVE_POINT, EVENT_ISERIA_QUEEN, \
    EVENT_LIMITER_OFF, EVENT_TELEPORTER, EVENT_VICTORY, EXPEL_STORY_KEYS, GAME, KEY_NAME, stage_event
from . import skillsanity
from .locations import ROW
from .regions import stage_rules

STAGE_OF_GROUP = {g["group"]: g["stage"] for g in KEY_GROUPS}
WORLD_OF_GROUP = {g["group"]: g["world"] for g in KEY_GROUPS}
# Areas the story reaches only by a ship landing through another group's entrance (data.py
# KEY_GROUPS `landing`: Lacour through Port Hilton, Hoffmann Ruins through map 231).
LANDING_OF_GROUP = {g["group"]: tuple(g.get("landing", ())) for g in KEY_GROUPS}


def in_expel(row):
    """Expel's maps for open world: the Expel region, and an Energy Nede row whose map is entered
    through an Expel entrance (Lacour's King's Room, map 429, a chapter-60 scene)."""
    return row["region"] == "Expel" or (row["region"] == "Energy Nede" and not row.get("group_near")
                                        and WORLD_OF_GROUP.get(row.get("group")) == 1)
FIENAL_STAGE = len(STAGES)      # Fienal's floors above the first visit's open in the last chapters
# Per gate, per game: the locations its proof reached at the gate (data.STORY_GATES `proven`).
GATE_PROVEN = [{game: frozenset(ids) for game, ids in gate["proven"].items()} for gate in STORY_GATES]


def games_of(side):
    return ("claude", "rena") if side is None else (side,)


def proven_late(row, side):
    """Whether the late proof reached this location in the game(s) the player may play (side
    None: either game, so both)."""
    return all(g in row.get("late", ()) for g in games_of(side))


def proven_teleporter(row, side):
    """Whether the teleporter proof (chapter 38, Fun City's arena to Arlia) reached it."""
    return all(g in row.get("tele", ()) for g in games_of(side))


def proven_at_gate(row, side, k):
    """Whether gate k's proof (data.STORY_GATES) reached the location at the gate. A drop's place
    (drop_places) carries the games whose voyage proof reached its maps: the El voyage, the only
    gate, is the voyage proof's."""
    if row.get("id") is None:
        return all(g in row.get("voyage", ()) for g in games_of(side))
    return all(row["id"] in GATE_PROVEN[k].get(g, ()) for g in games_of(side))


def may_hold_progression(row, side, expel_progression=True):
    if row["type"] in ("drop", "join", "shop", "gift"):
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


def early_stage(row):
    """Open world: the stage in which the story first reaches the location (its early mode:
    the stage before it finished, plus the area's keys), or None (drops, story bosses, Energy
    Nede rows behind an Expel entrance, the Cave of Trials, areas the story never enters)."""
    kind, region, group = row["type"], row["region"], row.get("group")
    if kind == "drop" or (kind == "boss" and row.get("boss") == "story"):
        return None
    stage = STAGE_OF_GROUP.get(group)
    if region == "Energy Nede" and WORLD_OF_GROUP.get(group) == 1:
        stage = None    # after the switch an Expel entrance is reached only through the teleporter
    if region == "Fienal":
        stage = FIENAL_STAGE
    if not stage or region == "Cave of Trials":
        return None
    if row.get("from_chapter"):
        stage = max(stage, stage_of_chapter(row["from_chapter"]))
    return stage


def closes_at_gate(row, side):
    """Open world: whether the story reaches the location before a gate (data.STORY_GATES)
    whose proof did not reach it there: it may close before the player is back, so it holds
    no progression (story bosses aside: the story fights them)."""
    stage = early_stage(row)
    return stage is not None and any(stage <= gate["stage"] and not proven_at_gate(row, side, k)
                                     for k, gate in enumerate(STORY_GATES))


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
    stage = early_stage(row)
    if stage:
        keys = (KEY_NAME[group],) + tuple(KEY_NAME[g] for g in LANDING_OF_GROUP.get(group, ())) if group else ()
        modes.append((stage_event(stage - 1) if stage > 1 else None, keys))
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
    if row["type"] in ("drop", "join", "shop", "gift") or closes_at_gate(row, side):
        return "none"
    if not in_expel(row):
        return "any" if may_hold_progression(row, side) else "none"
    full = "any" if expel_progression else "expel keys"
    if row["type"] == "boss" and row.get("boss") == "story":
        return full
    if teleporter and proven_teleporter(row, side):
        return full
    return "expel keys" if proven_late(row, side) else "none"


# ---- Enemy drops (enemy_dropsanity, drop_progression; docs/ap/apworld.md, "Drops") ----------
# roll_drops (engine/src/game/battle/result.cpp, 0x800869fc) rolls a downed enemy's two drop
# slots in turn, each at odds + (average Luck of the standing members >> 4) - 7 out of 256, and
# stops at the first success. Luck only adds, so the chance at Luck 0 is the floor. A drop holds
# progression only from MIN_DROP_CHANCE (1 in 64 kills at Luck 0): that keeps out the 11 types
# that can't drop at all without Luck (Magius 27 and Ericodus 56 need an average Luck of 128,
# Shout 96, Starguarder 80, the odds-5 slimes 48) and 9 more under 1 in 64 (Rikiha, Burst, the
# odds-10 types). drop_progression "force" fills the drops from FORCE_DROP_CHANCE (20 / 256, about
# 1 in 13 kills, 8 %: a handful of fights with that enemy) with progression first, as far as the
# logic allows (world.fill_hook; not Archipelago's priority locations, whose fill fails outright
# when one can't take any of the progression left, as a late drop can't in a solo open world).
MIN_DROP_CHANCE = 4 / 256
FORCE_DROP_CHANCE = 20 / 256
PROGRESSION_RANK = ("none", "expel keys", "any")


def drop_chance(row, luck=0):
    """The chance that a downed enemy of the drop's type drops anything (roll_drops)."""
    miss = 1.0
    for odds in row["odds"]:
        c = odds + (luck >> 4) - 7
        if c > 0:
            miss *= 1 - min(c, 256) / 256
    return 1 - miss


def place_row(place):
    """A drop's place (data.py `places`) as a location row the open-world rules take: a chest on
    its maps, reached as they are."""
    return {"type": "chest", "id": None, "region": place["region"], "group": place.get("group"),
            "group_near": place.get("group_near"), "late": place.get("late", []),
            "late_keys": place.get("late_keys", []), "tele": place.get("tele", []),
            "tele_keys": place.get("tele_keys", []), "voyage": place.get("voyage", []), "missable": []}


def drop_rule(row, side, open_world, teleporter, expel_progression, mode, encounter_rate):
    """A first drop's (what it may hold, its open-world access modes or None for the default).
    It may hold progression only when its enemy can always be met again from wherever the logic
    counts it reached, which its places say (the maps where random encounters meet the type with
    no story condition, tools/ap_world_tables.py drop_places):
    - story mode: a place the late proof reached (or a world map it walked), as for chests: the
      story always reaches the Final Save Point, so the enemy is there to fight in the end;
      with expel_progression off, a place outside Expel;
    - open world: each place counts as a chest on its maps would (open_world_progression,
      open_world_access: early in the story's stage with its area's keys, through the Expel
      teleporter, after the Final Save Point; nothing that closes before the El voyage); a world
      map only after the Final Save Point (its encounter cells are not mapped to islands). The
      drop holds the best class among its places and is reached through the modes of the places
      that allow progression, so the logic never counts it through a place that can close.
    Never with drop_progression never, encounter_rate 0 or a chance under MIN_DROP_CHANCE."""
    if mode == "never" or encounter_rate == 0 or drop_chance(row) < MIN_DROP_CHANCE:
        return "none", None
    if not open_world:
        ok = any(proven_late(p, side) and (expel_progression or p["region"] != "Expel") for p in row["places"])
        return ("any" if ok else "none"), None
    best, modes = "none", []
    for p in row["places"]:
        if p.get("world_map"):
            if not proven_late(p, side) or (p["region"] == "Expel" and not expel_progression):
                continue
            cls, pm = "any", [(EVENT_FINAL_SAVE_POINT, ())]
        else:
            pr = place_row(p)
            cls = open_world_progression(pr, side, teleporter, expel_progression)
            pm = open_world_access(pr, side, teleporter)
        if cls == "none":
            continue
        best = max(best, cls, key=PROGRESSION_RANK.index)
        modes += [m for m in pm if m not in modes]
    return best, (modes or None)


# ---- Killer moves (killer_move_checks, killer_move_progression) --------------------------------
# A level needs its member in the party and the member's level to learn the move, plus the
# move's uses (battles). Conservatively: a move learned by level KILLER_EARLY_LEVEL counts from
# the disc switch (a party there is typically well past it), any other from the Final Save Point
# (after which any level can be reached).
KILLER_EARLY_LEVEL = 30


def killer_rule(world, loc, row):
    """A killer-move level's access (the stage for the learn level; with killer_move_progression
    any, a member who joins by a character item also needs the item, which is then progression:
    world.progression_characters) and what it may hold (killer_move_progression; never with
    encounter_rate 0 or when EXCLUDED). Otherwise the item isn't required unless it is progression
    anyway (skillsanity special: the member's learn points need it, skillsanity.learn_characters):
    the location holds no progression, and under full accessibility every item, the member's too,
    is reached."""
    from .locations import fighter_status
    player = world.player
    o = world.options
    status = fighter_status(row["character"], world.party_info)
    if getattr(world, "hub", False):
        # Hub mode: everything is open from the start; a move learned past level 30 counts once
        # Fienal's requirement is met (the party has beaten the Fields' bosses, or more).
        need = [] if row["learn_level"] <= KILLER_EARLY_LEVEL else [EVENT_FIENAL_OPEN]
    else:
        need = [EVENT_ENERGY_NEDE if row["learn_level"] <= KILLER_EARLY_LEVEL else EVENT_FINAL_SAVE_POINT]
    if status == "item" and row["character"] in world.progression_characters:
        need.append(row["character"])
    need += skillsanity.move_need(world, row)  # skillsanity special: the move itself [skillsanity2]
    loc.access_rule = lambda state, need=need: state.has_all(need, player)
    mode = o.killer_move_progression.current_key
    allowed = (mode == "any" and status in ("sure", "item")) or \
              (mode == "hero" and row["character"] in world.party_info.story_members)
    if not allowed or o.encounter_rate.value == 0 or loc.progress_type == LocationProgressType.EXCLUDED:
        add_item_rule(loc, lambda item: not item.advancement)


def goal_rules(world):
    """Indalecio needs the Final Save Point; with goal indalecio_limiter_off also Filia's scene in
    Central City (the Limiter Off event: the Final Save Point, and in open world the Central City
    Pass), and with the Iseria Queen goal her defeat too (the Final Save Point, and in open world
    the Cave of Trials Pass and the Fun City Pass, the cave rows' route; the cave's region exists
    whether or not cave_of_trials adds its locations)."""
    player = world.player
    goal = world.options.goal.value
    need = [EVENT_FINAL_SAVE_POINT] + ([EVENT_LIMITER_OFF] if goal >= 1 else []) + \
        ([EVENT_ISERIA_QUEEN] if goal == 2 else [])
    world.get_location("Indalecio").access_rule = lambda state: state.has_all(need, player)
    if goal >= 1:
        keys = [EVENT_FINAL_SAVE_POINT] + (["Central City Pass"] if world.open_world else [])
        world.get_location("Limiter Off (Filia, Central City)").access_rule = \
            lambda state: state.has_all(keys, player)
    if goal == 2:
        cave = [EVENT_FINAL_SAVE_POINT] + (["Cave of Trials Pass", "Fun City Pass"] if world.open_world else [])
        song = skillsanity.iseria_queen_need(world) or (lambda state: True)    # skillsanity: Musical Talent
        world.get_location("Iseria Queen (Cave of Trials)").access_rule = \
            lambda state: state.has_all(cave, player) and song(state)


def set_rules(world):
    player = world.player
    goal_rules(world)
    world.multiworld.completion_condition[player] = lambda state: state.has(EVENT_VICTORY, player)
    side = {0: None, 1: "claude", 2: "rena"}[world.options.story_route.value]
    teleporter = world.open_world and bool(world.options.expel_teleporter)
    if world.open_world:
        stage_rules(world)
    o = world.options
    expel = bool(o.expel_progression)
    drop_mode = o.drop_progression.current_key
    keys_in_drops = bool(o.area_keys_in_drops)
    for loc in world.multiworld.get_locations(player):
        if loc.address is None:
            continue
        row = ROW[loc.name]
        modes = None
        if row["type"] == "killer_move":
            killer_rule(world, loc, row)
            continue
        if row["type"] in ("guild", "learn"):     # skillsanity [skillsanity2]
            skillsanity.set_location_rule(world, loc, row)
            continue
        if row["type"] == "craft":
            # Item Creation works anywhere once a specialty is learned; never progression (the
            # user, 2026-10-25: a craft check gives a random Archipelago item). Skillsanity: the
            # specialty's skills (a sure member's own, or the skill items).
            add_item_rule(loc, lambda item: not item.advancement)
            if skillsanity.modes(o)[0] and (rule := skillsanity.craft_rule(world, row)):
                loc.access_rule = rule
            continue
        if row["type"] == "drop":
            allowed, modes = drop_rule(row, side, world.open_world, teleporter, expel, drop_mode,
                                       o.encounter_rate.value)
            if allowed != "none" and loc.progress_type == LocationProgressType.EXCLUDED:
                allowed = "none"
            if allowed != "none" and drop_mode == "force" and drop_chance(row) >= FORCE_DROP_CHANCE:
                world.forced_drops.add(loc.name)    # filled first (world.fill_hook)
            if not keys_in_drops:
                # area_keys_in_drops off: no area key (this world's or another Star Ocean
                # player's, a hub player's dungeon keys too) behind a fight's drop, in either
                # world mode.
                add_item_rule(loc, lambda item: not (item.game == GAME and item.code in ALL_KEY_IDS))
        elif world.open_world:
            allowed = open_world_progression(row, side, teleporter, expel)
        else:
            allowed = "any" if may_hold_progression(row, side, expel) else "none"
        if allowed == "none":
            add_item_rule(loc, lambda item: not item.advancement)
        elif allowed == "expel keys":
            add_item_rule(loc, lambda item: not item.advancement
                          or (item.player == player and item.name in EXPEL_STORY_KEYS))
        if world.open_world:
            modes = modes or open_world_access(row, side, teleporter)
            loc.access_rule = lambda state, modes=modes: any(
                (ev is None or state.has(ev, player)) and state.has_all(keys, player) for ev, keys in modes)


__all__ = ["set_rules", "may_hold_progression", "proven_late", "proven_teleporter", "proven_at_gate", "early_stage",
           "closes_at_gate", "open_world_access", "drop_rule", "drop_chance", "DISC_SWITCH_STAGE"]
