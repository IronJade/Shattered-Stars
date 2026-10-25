"""Skillsanity (the option `skillsanity`; docs/ap/apworld.md "Skillsanity", docs/ap/seed_format.md
"Skillsanity"; the user's design and decisions: docs/handoff/skillsanity.md).

- skills_only / insanity: the skill guilds' sets are no longer bought as skills: a set's purchase
  (flag 700 + set, any of its guilds) is one check per skill of the set ("Skill Guild: Sensibility 2
  (1 of 4)"), and each skill is an item ("Skill: Perseverance") that lets the party learn it with SP
  as buying its set did. With the Cave of Trials, its Level 11 prize (Float) is a check and Float an
  item. Guild checks never hold progression (the user).
- special_only / insanity: every member keeps the first move of their learn table (known from the
  start or the join); every other killer move and spell is an item ("Claude: Shooting Stars"), and
  each learn point is a check instead: a learn-table level ("Claude Lv 7: Shooting Stars"; passed
  ones fire at the join), the items 464-469 (Precis's and Opera's machines), the arena's Private
  Action (Holo-holograph) and, with the Cave of Trials, its three learn chests. Learn points hold no
  progression (they need a member and levels; no proof places them).
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
    player = world.player
    if not ways:
        return None
    return lambda state, ways=ways: any(state.has_all(w, player) for w in ways)


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


def set_location_rule(world, loc, row):
    """A guild check's or learn point's access; neither holds progression."""
    from worlds.generic.Rules import add_item_rule
    from .items import EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN, EVENT_FINAL_SAVE_POINT
    from .rules import open_world_access, place_row
    player = world.player
    add_item_rule(loc, lambda item: not item.advancement)
    hub_mode = getattr(world, "hub", False)
    side = {0: None, 1: "claude", 2: "rena"}[world.options.story_route.value]
    events, extra = [], None
    if row["type"] == "guild":
        if world.open_world:
            teleporter = bool(world.options.expel_teleporter)
            ways = []
            for m in row["guild_maps"]:
                for mode in open_world_access(place_row(GUILD_PLACES[m]), side, teleporter):
                    if mode not in ways:
                        ways.append(mode)
            loc.access_rule = lambda state, ways=ways: any(
                (ev is None or state.has(ev, player)) and state.has_all(keys, player) for ev, keys in ways)
        return
    how = row["how"]
    if how == "level":
        if hub_mode:
            events = [] if row["level"] <= EARLY_LEVEL else [EVENT_FIENAL_OPEN]
        else:
            events = [EVENT_ENERGY_NEDE if row["level"] <= EARLY_LEVEL else EVENT_FINAL_SAVE_POINT]
    elif how == "item":
        events = [] if hub_mode else [EVENT_FINAL_SAVE_POINT]
        if modes(world.options)[0]:
            extra = needs_rule(world, specialty_needs(world, "Machinery"))
    elif how in ("cave", "float"):
        if world.open_world:
            ways = open_world_access(row, side, bool(world.options.expel_teleporter))
            loc.access_rule = lambda state, ways=ways: any(
                (ev is None or state.has(ev, player)) and state.has_all(keys, player) for ev, keys in ways)
        return
    if extra is None:
        loc.access_rule = lambda state, events=events: state.has_all(events, player)
    else:
        loc.access_rule = lambda state, events=events, extra=extra: state.has_all(events, player) and extra(state)


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
           "set_location_rule", "hub_guild_region", "place_in_shops", "price_of", "seed_item", "seed_check",
           "is_skillsanity_item", "GUILD_TOWNS"]
