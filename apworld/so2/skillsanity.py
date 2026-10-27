"""Skillsanity (the option `skillsanity`; docs/ap/apworld.md "Skillsanity", docs/ap/seed_format.md
"Skillsanity"; the user's design and decisions: docs/handoff/skillsanity.md).

- skills_only / insanity: the skill guilds' sets are no longer bought as skills: a set's purchase
  (flag 700 + set, any of its guilds) is one check per skill of the set ("Skill Guild: Sensibility 2
  (1 of 4)"), and each skill is an item ("Skill: Perseverance") that lets the party learn it with SP
  as buying its set did. With the Cave of Trials, its Level 11 prize (Float) is a check and Float an
  item. A guild check may hold progression where its guilds are in reach and the set affordable
  (guild_rule; the user, 2026-10-27, who had ruled it out on 2026-10-25).
- special_only / insanity: every member keeps the first move of their learn table (known from the
  start or the join); every other killer move and spell is an item ("Claude: Shooting Stars"), and
  each learn point is a check instead: a learn-table level ("Claude Lv 7: Shooting Stars"; passed
  ones fire at the join), the items 464-469 (Precis's and Opera's machines), the arena's Private
  Action (Holo-holograph) and, with the Cave of Trials, its three learn chests. A learn-table level
  of a member the logic counts on may hold progression (learn_rule; the user, 2026-10-27); the
  machines, the cave's learn chests and Float hold none.
- Logic: Item Creation needs its specialty's skills (a member who starts with them, or the skill
  items: craftsanity's locations then need them), the Iseria Queen goal Musical Talent's, Precis's
  and Opera's machines Machinery's; a killer-move level needs its move. Those items are progression;
  every other skill and move item is useful.
- Shops (skillsanity_shop_share, with shuffle_shops): that share of this world's non-progression
  skill and move items is placed in this world's shop slots before the fill, where the port sells
  each as itself at the user's price (a move: its learn level x 100; an item-taught move 5000, a
  cave prize 8000, Float 900; a skill: its set's price / the set's size). A slot the fill gives one
  of them gets its price too.
"""
from BaseClasses import ItemClassification, LocationProgressType

from . import hell, logic
from .data import GUILD_PLACES, HUB_MAPS, ITEM_BASE, LOCATIONS
from .skills import EXTRA_MOVES, FLOAT, FLOAT_PRICE, GUILD_TOWNS, LEARN, SKILL_SETS, SKILLS, SPECIALTIES, \
    STARTING_SKILLS, SUPER_SPECIALTIES

SKILL_BASE = ITEM_BASE + 0x60000    # a skill item: SKILL_BASE + skill id (1-46)
MOVE_BASE = ITEM_BASE + 0x70000     # a move item: MOVE_BASE + (party id << 8) + record index
GUILD_TYPE = 13                     # a guild check: ITEM_BASE + (13 << 21) + (set << 8) + k
LEARN_TYPE = 14                     # a learn point: ITEM_BASE + (14 << 21) + (party id << 8) + index;
                                    # the Float prize: party id 0, index 46
MEMBERS = ("Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Noel",
           "Chisato")
EARLY_LEVEL = 30                    # as killer moves: a learn level up to it counts from the disc switch
CAVE_MAPS = {"Leon": 740, "Ashton": 798, "Celine": 799}
FLOAT_MAP = 807
MACHINES = ("Precis", "Opera")      # their extra moves come from Machinery's items (464-469)


def skill_item(skill):
    return f"Skill: {SKILLS[skill]}"


def move_name(who, index):
    for w, _, i, name, *_ in LEARN + EXTRA_MOVES:
        if w == who and i == index:
            return name
    return f"move {index}"


def move_item(who, index):
    return f"{who}: {move_name(who, index)}"


FIRST = {w: i for w, _, i, _, _, first in LEARN if first}
SKILL_ITEMS = {skill_item(s): SKILL_BASE + s for s in SKILLS}
_MOVES = [(w, pid, i) for w, pid, i, _, _, first in LEARN if not first] + [(w, pid, i) for w, pid, i, *_ in EXTRA_MOVES]
MOVE_ITEMS = {move_item(w, i): MOVE_BASE + (pid << 8) + i for w, pid, i in _MOVES}
SKILL_OF = {SKILL_BASE + s: s for s in SKILLS}
MOVE_OF = {MOVE_BASE + (pid << 8) + i: (w, i) for w, pid, i in _MOVES}
ITEM_NAME_TO_ID = {**SKILL_ITEMS, **MOVE_ITEMS}
ITEM_GROUPS = {"Skills": set(SKILL_ITEMS), "Moves": set(MOVE_ITEMS),
               **{f"Moves: {w}": {n for n in MOVE_ITEMS if n.startswith(f"{w}: ")} for w in MEMBERS}}


def set_price(k):
    name, price, skills, _ = SKILL_SETS[k]
    return price // len(skills)


def skill_price(skill):
    if skill == FLOAT:
        return FLOAT_PRICE
    for k, (_, _, skills, _) in enumerate(SKILL_SETS):
        if skill in skills:
            return set_price(k)
    return FLOAT_PRICE


def move_price(who, index):
    for w, _, i, _, how, price in EXTRA_MOVES:
        if w == who and i == index:
            return price
    for w, _, i, _, level, _ in LEARN:
        if w == who and i == index:
            return level * 100
    return 100


def price_of(code):
    """The price a shop slot sells this world's skill / move item at (the user's prices)."""
    if code in SKILL_OF:
        return skill_price(SKILL_OF[code])
    if code in MOVE_OF:
        return move_price(*MOVE_OF[code])
    return None


def seed_item(name, code):
    if code in SKILL_OF:
        return {"name": name, "kind": "skill", "skill": SKILL_OF[code]}
    if code in MOVE_OF:
        who, index = MOVE_OF[code]
        return {"name": name, "kind": "move", "character": MEMBERS.index(who) + 1, "index": index}
    return None


# ---- Locations -----------------------------------------------------------------------------------
_CAVE_REF = next(r for r in LOCATIONS if r["region"] == "Cave of Trials" and r["type"] == "chest" and r.get("late"))
_CAVE_FIELDS = {k: _CAVE_REF.get(k) for k in ("group", "group_near", "late", "late_keys", "tele", "tele_keys")}


def _row(**kw):
    base = {"item": None, "fol": None, "sites": [], "missable": [], "story_route": None}
    base.update(kw)
    return base


GUILD_ROWS = []
for _k, (_name, _price, _skills, _maps) in enumerate(SKILL_SETS):
    _expel = any(GUILD_PLACES[m]["region"] == "Expel" for m in _maps)
    for _j, _s in enumerate(_skills):
        GUILD_ROWS.append(_row(id=ITEM_BASE + (GUILD_TYPE << 21) + (_k << 8) + _j, type="guild", set=_k, index=_j,
                               name=f"Skill Guild: {_name} ({_j + 1} of {len(_skills)})", skill=_s,
                               area="Skill Guilds", region="Expel" if _expel else "Energy Nede", guild_maps=_maps))
LEARN_ROWS = []
for _w, _pid, _i, _name, _level, _first in LEARN:
    if _first:
        continue
    LEARN_ROWS.append(_row(id=ITEM_BASE + (LEARN_TYPE << 21) + (_pid << 8) + _i, type="learn", how="level",
                           name=f"{_w} Lv {_level}: {_name}", character=_w, party_id=_pid, move_index=_i,
                           level=_level, area="Learn Points", region="Expel"))
for _w, _pid, _i, _name, _how, _price in EXTRA_MOVES:
    if _w in CAVE_MAPS:
        _floor = "Cave of Trials Level 4" if _w == "Leon" else "Cave of Trials Level 10"
        LEARN_ROWS.append(_row(id=ITEM_BASE + (LEARN_TYPE << 21) + (_pid << 8) + _i, type="learn", how="cave",
                               name=f"{_floor}: {_w}'s {_name}", character=_w, party_id=_pid, move_index=_i,
                               level=None, area=_floor, region="Cave of Trials", map=CAVE_MAPS[_w], **_CAVE_FIELDS))
    else:
        _thing = _how.split(" or ")[0].split(" ", 2)[2]
        LEARN_ROWS.append(_row(id=ITEM_BASE + (LEARN_TYPE << 21) + (_pid << 8) + _i, type="learn", how="item",
                               name=f"{_w}: {_name} ({_thing})", character=_w, party_id=_pid, move_index=_i,
                               level=None, area="Learn Points", region="Expel"))
FLOAT_ROW = _row(id=ITEM_BASE + (LEARN_TYPE << 21) + FLOAT, type="learn", how="float",
                 name="Cave of Trials Level 11: Float", character=None, party_id=0, move_index=None, skill=FLOAT,
                 level=None, area="Cave of Trials Level 11", region="Cave of Trials", map=FLOAT_MAP, **_CAVE_FIELDS)
ROWS = GUILD_ROWS + LEARN_ROWS + [FLOAT_ROW]
LOCATION_GROUPS = {"Skill Guilds": {r["name"] for r in GUILD_ROWS},
                   "Learn Points": {r["name"] for r in LEARN_ROWS + [FLOAT_ROW]},
                   **{f"Learn Points: {w}": {r["name"] for r in LEARN_ROWS if r["character"] == w} for w in MEMBERS}}


def modes(options):
    """(skills shuffled, specials shuffled)."""
    v = options.skillsanity.value
    return v in (1, 3), v in (2, 3)


def choose(options, party, hub_mode=False):
    """The skillsanity locations of this world: the guild checks (skills), the Float prize (skills,
    with the Cave of Trials), the learn points of the members who can be in the party (special;
    filler only for a member the logic can't count on; the cave's with the cave)."""
    from .locations import fighter_status
    skills_on, specials_on = modes(options)
    out = []
    if skills_on:
        out += [(r, LocationProgressType.DEFAULT) for r in GUILD_ROWS]
        if options.cave_of_trials:
            out.append((FLOAT_ROW, LocationProgressType.DEFAULT))
    if specials_on:
        for r in LEARN_ROWS:
            if r["how"] == "cave" and not options.cave_of_trials:
                continue
            status = fighter_status(r["character"], party)
            if status is None:
                continue
            out.append((r, LocationProgressType.EXCLUDED if status == "maybe" else LocationProgressType.DEFAULT))
    return out


def pool_name(row):
    """The item a skillsanity location adds to the pool: its skill or its move."""
    if row["type"] == "guild" or row.get("how") == "float":
        return skill_item(row["skill"])
    return move_item(row["character"], row["move_index"])


def is_skillsanity_item(code):
    return code in SKILL_OF or code in MOVE_OF


# ---- Logic -----------------------------------------------------------------------------------------
def specialty_skills(spec):
    """The skills a specialty (or a super specialty: both of its specialties) needs."""
    if spec in SUPER_SPECIALTIES:
        return tuple(dict.fromkeys(s for part in SUPER_SPECIALTIES[spec] for s in SPECIALTIES[part]))
    return SPECIALTIES[spec]


def specialty_needs(world, spec):
    """The ways a specialty is had: per member the logic is sure of (and 'anyone', no starting
    skills), the skill items still needed; [] when a sure member starts with all of them."""
    skills = specialty_skills(spec)
    ways = []
    for who in sorted(world.party_info.guaranteed) + [None]:
        start = set(STARTING_SKILLS.get(who, ())) if who else set()
        need = tuple(skill_item(s) for s in skills if s not in start)
        if not need:
            return []
        if need not in ways:
            ways.append(need)
    return ways


def needs_rule(world, ways):
    """specialty_needs' ways as a rule: any of them (None: none needed)."""
    if not ways:
        return None
    return logic.any_of(ways)


def plan(world):
    """generate_early: the skill and move items the logic needs (progression), from this world's
    locations and goal."""
    o = world.options
    skills_on, specials_on = modes(o)
    world.skill_needs = set()
    if skills_on:
        specs = set()
        for row, _ in world.chosen:
            if row["type"] == "craft":
                specs |= set(row["specialties"])
            if row["type"] == "learn" and row.get("how") == "item":
                specs.add("Machinery")
        if o.goal.value == 2:
            specs.add("Musical Talent")     # the Iseria Queen's song: Musical 10
        for spec in specs:
            for way in specialty_needs(world, spec):
                world.skill_needs |= set(way)
    if specials_on:
        for row, _ in world.chosen:
            if row["type"] == "killer_move" and FIRST.get(row["character"]) != row["move"] - 1:
                world.skill_needs.add(move_item(row["character"], row["move"] - 1))


def classify(world, name):
    """A skillsanity item's classification, or None for any other item."""
    code = ITEM_NAME_TO_ID.get(name)
    if code is None:
        return None
    if name in getattr(world, "skill_needs", ()):
        return ItemClassification.progression | ItemClassification.useful
    return ItemClassification.useful


def craft_rule(world, row):
    """skills shuffled: a craft needs one of its specialties (a sure member's starting skills or
    the skill items)."""
    ways = []
    for spec in row["specialties"]:
        found = specialty_needs(world, spec)
        if not found:
            return None
        ways += [w for w in found if w not in ways]
    return needs_rule(world, ways)


def move_need(world, row):
    """special shuffled: a killer-move level needs its move (not the member's first)."""
    if not modes(world.options)[1] or FIRST.get(row["character"]) == row["move"] - 1:
        return []
    return [move_item(row["character"], row["move"] - 1)]


def iseria_queen_need(world):
    """skills shuffled: the Iseria Queen's song needs Musical Talent (at 10): its skills' items."""
    if not modes(world.options)[0]:
        return None
    return needs_rule(world, specialty_needs(world, "Musical Talent"))


# ---- Progression at guild checks and learn points (the user, 2026-10-27; docs/ap/apworld.md
# "Skillsanity", "Logic"). Both need battles: with encounter_rate 0 neither holds progression,
# except on a hell seed (hell.py), whose free Fun City arena pays the EXP and the Fol: there both
# count from the arena (hell.arena_need, hell.arena_way).
# The Fol a guild set costs: random battles pay it (Earth, fol_multiplier 1, the floor; the story's
# chests and Fol gifts are shuffled, so they may hold anything). Mean Fol a battle (docs/ap/
# battles.json's enemy Fol over field.json's encounter tables): Expel's world map ~460 (120-780),
# Mars ~400, the Sanctuary of Linga ~920, Hoffmann Ruins ~2100, Eluria Tower ~3500; Energy Nede's
# world map ~520, its dungeons 2400-3600. A set up to 1600 Fol (the 1 and 2 sets) counts from a
# guild that sells it: its first guilds open in stages 3 (Cross, 300-400 Fol) and 5 (Herlie,
# 1500-1600), when a set costs about four world-map battles. A set from DEAR_PRICE (the 3 sets,
# 2700-4500 Fol; 13500 all four) counts from the end of story stage FOL_STAGE (Hilton and Hoffmann
# Ruins), whose battles pay the dearest in about two and all four in about seven; their first
# guilds (Lacour, stage 6; Linga, 8) would count them when it takes ten world-map battles (five
# at Linga's Sanctuary) for one. Story mode has no stage events: the disc switch stands for it.
# Hub mode: the cheap sets from the start (the world maps), the dear ones once Fienal opens.
DEAR_PRICE = 2700
FOL_STAGE = 9
# Learn levels: as killer moves (rules.py killer_rule), a level up to EARLY_LEVEL from the disc
# switch, any other from the Final Save Point (hub: from the start / once Fienal opens). A member
# who joins only through the story after the disc switch (open world under the story's party
# rules, not the start: Noel, at Noel's house, stage 15) also needs that stage; story mode reaches
# it with no item, so the disc switch stands for it there. A member who joins by a character item
# needs the item (world.progression_characters). Only learn-table levels: the machines (items
# 465-469 are Precis's / Opera's rare Machinery results, 464 a shuffled gift or a Private Action)
# and the Cave of Trials' learn chests and Float (the late proof did not reach them: behind
# puzzles, or a prize no proof sweeps) hold none.
STORY_JOINS = {"Noel": 15}


def fol_mode(mode, dear):
    """A guild place's access mode with the set's Fol: a dear set's early mode no sooner than the
    end of stage FOL_STAGE (the teleporter's and the late proof's come later anyway)."""
    from .items import stage_event
    ev, keys = mode
    if not dear:
        return mode
    number = {stage_event(k): k for k in range(1, FOL_STAGE + 1)}
    if ev is None or ev in number:
        return stage_event(FOL_STAGE), keys
    return mode


def guild_rule(world, row):
    """A guild check's (what it may hold: "any", "expel keys" or "none", its access ways [(event or
    None, keys)], any of which reaches it). It is bought at any of its set's guilds (each guild a
    place, as a first drop's: rules.drop_rule), and holds the best class of its places, reached
    only through the places of that class (never through one that can't hold it), each with the
    set's Fol (fol_mode). Story mode: a place the late proof reached (outside Expel with
    expel_progression off); hub mode: one the hub proof reached in every game the player may play,
    open from the start (towns). None with encounter_rate 0: the Fol comes from battles; but on a
    hell seed the free Fun City arena pays it (its fights pay their enemies' Fol: hell.py), so a
    set counts from the arena, whatever its price: story mode from the disc switch, open world
    through each of its places' ways combined with the arena's (hell.arena_way; an Expel place's
    own early way closes before the arena opens), hub mode from the start."""
    from .items import EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN
    from .rules import PROGRESSION_RANK, in_expel, open_world_access, open_world_progression, place_row, \
        proven_late
    o = world.options
    side = {0: None, 1: "claude", 2: "rena"}[o.story_route.value]
    games = ("claude", "rena") if side is None else (side,)
    dear = SKILL_SETS[row["set"]][1] >= DEAR_PRICE
    expel = bool(o.expel_progression)
    battles = o.encounter_rate.value > 0
    arena = hell.is_hell_seed(o)        # [hellseed] the Fol from the free arena
    places = [GUILD_PLACES[m] for m in row["guild_maps"]]
    if getattr(world, "hub", False):
        ok = any(all(m in HUB_MAPS[g] for g in games for m in p["maps"]) and (expel or p["region"] != "Expel")
                 for p in places)
        if arena:
            return ("any" if ok else "none"), [(None, ())]
        return ("any" if ok and battles else "none"), [(EVENT_FIENAL_OPEN if dear else None, ())]
    by_class = {}
    for p in places:
        pr = place_row(p)
        if world.open_world:
            teleporter = bool(o.expel_teleporter)
            cls = open_world_progression(pr, side, teleporter, expel)
            if arena:
                ways = [hell.arena_way(world, m) for m in open_world_access(pr, side, teleporter)
                        if not (in_expel(pr) and hell.early_way(m))]
            else:
                ways = [fol_mode(m, dear) for m in open_world_access(pr, side, teleporter)]
        else:
            cls = "any" if proven_late(pr, side) and (expel or p["region"] != "Expel") else "none"
            ways = [(EVENT_ENERGY_NEDE if arena or dear or p["region"] != "Expel" else None, ())]
        found = by_class.setdefault(cls, [])
        found += [w for w in ways if w not in found]
    # (A hell seed's open world drops an Expel place's early way; every set has a guild on Energy
    # Nede, so some way is always left.)
    by_class = {cls: found for cls, found in by_class.items() if found}
    best = max(by_class, key=PROGRESSION_RANK.index) if battles or arena else "none"
    if best == "none":
        ways = []
        for found in by_class.values():
            ways += [w for w in found if w not in ways]
        return "none", ways
    return best, by_class[best]


def learn_rule(world, row, kind):
    """A learn point's (whether it may hold progression, the items / events it needs, an extra
    rule or None). See STORY_JOINS above for the design."""
    from .items import EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN, EVENT_FINAL_SAVE_POINT, stage_event
    from .locations import fighter_status
    o = world.options
    hub_mode = getattr(world, "hub", False)
    how = row["how"]
    if how == "item":
        extra = needs_rule(world, specialty_needs(world, "Machinery")) if modes(o)[0] else None
        return False, ([] if hub_mode else [EVENT_FINAL_SAVE_POINT]), extra
    if how != "level":
        return False, [], None
    who, early = row["character"], row["level"] <= EARLY_LEVEL
    party = world.party_info
    if hell.is_hell_seed(o):
        # [hellseed] every level from the free Fun City arena (its EXP); in open world it opens
        # after Noel's house (STORY_JOINS: stage 15 < TELEPORTER_STAGE 16), so that need is in it.
        need = hell.arena_need(world)
    elif hub_mode:
        need = [] if early else [EVENT_FIENAL_OPEN]
    else:
        need = [EVENT_ENERGY_NEDE if early else EVENT_FINAL_SAVE_POINT]
        if world.open_world and early and who in STORY_JOINS and party.rules == "story" and who != party.start:
            need.append(stage_event(STORY_JOINS[who]))
    status = fighter_status(who, party)
    prog = hell.trained(o) and status in ("sure", "item") and kind != LocationProgressType.EXCLUDED
    if prog and status == "item":
        need.append(who)        # the character item, then progression (learn_characters)
    return prog, need, None


def learn_characters(world):
    """generate_early: the members who join by their item and have a learn point that may hold
    progression: their items are progression (the learn point needs it)."""
    if not modes(world.options)[1] or not hell.trained(world.options):
        return set()
    from .locations import fighter_status
    return {row["character"] for row, kind in world.chosen
            if row["type"] == "learn" and row.get("how") == "level" and kind == LocationProgressType.DEFAULT
            and fighter_status(row["character"], world.party_info) == "item"}


def set_location_rule(world, loc, row):
    """A guild check's or learn point's access and what it may hold (guild_rule, learn_rule)."""
    from worlds.generic.Rules import add_item_rule
    from .items import EXPEL_STORY_KEYS
    from .rules import open_world_access
    player = world.player
    side = {0: None, 1: "claude", 2: "rena"}[world.options.story_route.value]
    if row["type"] == "guild":
        allowed, ways = guild_rule(world, row)
        world.set_rule(loc, logic.ways(ways))
        if allowed == "none":
            add_item_rule(loc, lambda item: not item.advancement)
        elif allowed == "expel keys":
            add_item_rule(loc, lambda item: not item.advancement
                          or (item.player == player and item.name in EXPEL_STORY_KEYS))
        return
    if row["how"] in ("cave", "float"):
        add_item_rule(loc, lambda item: not item.advancement)
        if world.open_world:
            world.set_rule(loc, logic.ways(open_world_access(row, side, bool(world.options.expel_teleporter))))
        return
    prog, need, extra = learn_rule(world, row, loc.progress_type)
    if not prog:
        add_item_rule(loc, lambda item: not item.advancement)
    world.set_rule(loc, logic.both(logic.need(need), extra))


def hub_guild_region(row):
    """Hub mode: a guild check's town region: Energy Nede (every set has a guild there the hub proof
    reached in both games), else Expel."""
    for m in row["guild_maps"]:
        if GUILD_PLACES[m]["region"] == "Energy Nede" and all(m in HUB_MAPS[g] for g in HUB_MAPS):
            return "Energy Nede"
    return "Expel"


def place_in_shops(world, pool):
    """skillsanity_shop_share (with shuffle_shops): that share of this world's non-progression skill
    and move items goes to this world's shop slots (DEFAULT ones: they take useful items) before the
    fill, locked there; the port sells each as itself at its price."""
    o = world.options
    share = o.skillsanity_shop_share.value
    if not share or not o.shuffle_shops.value or not any(modes(o)):
        return pool
    candidates = [k for k, it in enumerate(pool) if is_skillsanity_item(it.code) and not it.advancement]
    slots = [world.get_location(row["name"]) for row, kind in world.chosen
             if row["type"] == "shop" and kind == LocationProgressType.DEFAULT]
    count = min(len(candidates) * share // 100, len(slots))
    if count <= 0:
        return pool
    picked = sorted(world.random.sample(candidates, count))
    for k, loc in zip(picked, world.random.sample(slots, count)):
        loc.place_locked_item(pool[k])
    taken = set(picked)
    return [it for k, it in enumerate(pool) if k not in taken]


def seed_check(row):
    if row["type"] == "guild":
        return {"type": "guild", "set": row["set"], "index": row["index"]}
    if row.get("how") == "float":
        return {"type": "learn", "skill": FLOAT}
    out = {"type": "learn", "character": row["party_id"], "index": row["move_index"]}
    if row.get("level"):
        out["level"] = row["level"]
    return out


__all__ = ["ROWS", "GUILD_ROWS", "LEARN_ROWS", "FLOAT_ROW", "ITEM_NAME_TO_ID", "ITEM_GROUPS", "LOCATION_GROUPS",
           "choose", "pool_name", "plan", "classify", "craft_rule", "move_need", "iseria_queen_need",
           "set_location_rule", "guild_rule", "learn_rule", "learn_characters", "hub_guild_region", "place_in_shops",
           "price_of", "seed_item", "seed_check", "is_skillsanity_item", "GUILD_TOWNS", "DEAR_PRICE", "FOL_STAGE",
           "STORY_JOINS"]
