"""Hub mode (`world_mode: hub`; docs/ap/apworld.md "Hub mode"; the user's decisions D1-D10 in
docs/handoff/hub.md; the seed: docs/ap/seed_format.md "Hub").

New Game starts after the Final Save Point (chapter 60, flag 730, disc 2) with every location
undone and the chapter held at 60 (D1): both worlds and every town are open from the start and
nothing closes, so the accessibility rule holds by construction (every key stays reachable from
any play state). Each dungeon has one key (D4; data.HUB_DUNGEONS). Each story boss waits in its
chamber and plays its original scene through a story view (D2); beating it is its location, open
as soon as its dungeon's key has arrived (D3: every boss independent; the chambers in towns need
none). Fienal has no key: it opens when the bosses of `fienal_requirement` are beaten (D6), and
the goal lies behind it. Characters are always items (D7).

The locations are what the hub reaches: the hub proof's (docs/ap/hub_proof.json: the late proof's
search run from the hub's New Game with the chapter lock on, tools/ap_late_proof.py --state hub):
its proven chests, gifts and Cave of Trials / optional bosses (data.HUB_REACHED), the shops open at
the hub state and the enemy types met there on the maps it reached (data.HUB_SHOPS, HUB_DROPS,
HUB_MAPS; tools/ap_world_tables.py `hub_tables`), plus the 22 chambers (docs/ap/hub_bosses.json,
proven headless by the agent "hubchambers" and from the hub's New Game by the hub proof's
`--bosses`). Progression goes only to proven chests, the chambers and proven cave bosses; drops per
drop_progression / area_keys_in_drops; never to shops, gifts or crafts.
"""
import copy

from BaseClasses import ItemClassification, LocationProgressType, Region
from worlds.generic.Rules import add_item_rule

from .data import HUB_BOSSES, HUB_DROPS, HUB_DUNGEONS, HUB_LOCATIONS, HUB_MAPS, HUB_REACHED, HUB_SHOPS, HUB_STARTS, \
    HUB_STATE, LOCATIONS
from .items import ALL_KEY_IDS, EVENT_FIENAL_OPEN, EVENT_ISERIA_QUEEN, EVENT_LIMITER_OFF, EVENT_VICTORY, GAME, \
    HUB_KEY_NAME, HUB_KEYLESS, SO2Item
from . import skillsanity
from .locations import ROW, SOFT_REASONS, SO2Location, choose_crafts, choose_killer_moves
from .rules import FORCE_DROP_CHANCE, MIN_DROP_CHANCE, drop_chance, killer_rule

# Dungeons out of hub mode (hubcore, 2026-10-25): the Portal of Sealing (the late state never
# reaches it: three story-gated doors, 306 -> 308 -> 313 -> 400 at chapter 52; its one chest, the
# Mind Ring, map 387, flag 2474; 306 is Berle 535's chamber). No key, nothing there a location.
PORTAL = "Portal of Sealing"
OUT_OF_HUB = (PORTAL,)
# ---- Waiting on the other hub agents (docs/handoff/hubworld.md, "Waiting") ---------------------
# Optional bosses (side quests) the hub reaches: a location when the hub proof proved it (XINE in
# Lasguss Mountain), filler only (their "optional quest") unless hubchambers confirmed that it
# plays at the hub state; then it may hold progression.
CONFIRMED_OPTIONAL = frozenset({199})  # XINE plays at the hub state in both games (hubchambers, 2026-10-25)

FIENAL = "Fienal"
CAVE = "Cave of Trials"
TOWNS = ("Expel", "Energy Nede")
WORLD_REGION = {1: "Expel", 2: "Energy Nede"}
GAMES = ("claude", "rena")
CHAMBERS = {b["encounter"]: b for b in HUB_BOSSES}
CHAMBER_LOCATIONS = {b["location"] for b in HUB_BOSSES}
DUNGEONS = {g["dungeon"]: g for g in HUB_DUNGEONS}
DUNGEON_OF_MAP = {m: g["dungeon"] for g in HUB_DUNGEONS for m in g["maps"]}
HUB_MAPS_OF = {g: frozenset(HUB_MAPS[g]) for g in GAMES}   # the maps the hub proof reached, per game
ROWS = LOCATIONS + HUB_LOCATIONS
# The Fienal requirement (D6): the four Fields' bosses (Magichand 524, the Guardians of Power 525
# and Courage 527, Lover 528), and Berle in Armlock (535, a town chamber: no key).
FIELDS = (524, 525, 527, 528)
BERLE_ARMLOCK = 535
FIENAL_GATE_NAMES = {0: "Fienal (the four Fields and Berle)", 1: "Fienal (the four Fields)",
                     2: "Fienal (every boss)"}
# The gate's banners, one per boss (the port shows the first missing one's; at most 236 px each,
# about 36 characters: these are 33 at most; hubcore measures them in the port).
BANNERS = {
    191: "Defeat Alen-Tax first", 523: "Clear Cross Cave first", 194: "Defeat Varmillion first",
    195: "Defeat Azamgil first", 209: "Defeat Visseyer first", 210: "Defeat Harfainx first",
    516: "Defeat Shin (Front Line) first", 519: "Defeat Shin (Eluria Tower) first",
    520: "Defeat Berle (Eluria Tower) first", 521: "Defeat the Synard first", 522: "Defeat Arcmene first",
    524: "Defeat Magichand first", 525: "Defeat Power's Guardian first", 527: "Defeat Courage's Guardian first",
    528: "Defeat Lover first", 531: "Defeat Bark first", 532: "Defeat Marsilio first", 533: "Defeat Shigeo first",
    535: "Defeat Berle (Armlock) first",
}
# P5 (hubcore2, 2026-10-25): no door enters Marsilio's arena 305 at chapter 60 (279 / 301 -> 305
# only at chapter 57 step 0, 302 -> 305 with flag 511). The antechamber 304's door to the lobby 296
# (19929509; 296 stays reachable from 299 / 300) leads to 305 instead (the registration's arrival,
# as 297 -> 305 lands), only while Marsilio (532) is unbeaten: after the win it is the original
# door again (the port's `to.while_unbeaten`). Proven headless (docs/handoff/hubcore.md).
# Fienal's third pair (hubchambers, 2026-10-25): entered from the west (679's door 44502760) while
# Cyril (538) is unbeaten, 674's scene walks the hero into the enemies and never ends; from the east
# (676) it plays: the door lands on 674's east arrival until he is beaten.
P5_DOORS = (
    {"id": 19929509, "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271, "while_unbeaten": 532},
     "name": "Fun City Fighting Arena (map 304 -> 305 while Marsilio is unbeaten)"},
    {"id": 44502760, "to": {"map": 674, "x": 882, "y": 0, "z": 30, "dir": 180, "while_unbeaten": 538},
     "name": "Fienal (679 -> 674: Cyril's east side while unbeaten)"},
)
# The chambers that ask first (the user's rulings, 2026-10-25; docs/handoff/hubask.md; the port's
# `ask`, seed_format.md "Hub"). Shigeo's street (Fun City 281) lies on the only Energy Nede -> Expel
# route: every door into it asks; no lets the party pass (281 at the hub's own state). The arena:
# "players will generally use the arena in Fun City as a way to level up their characters": the
# desk's first menu (map 297) gets "Face Marsilio." beside "Register." (the normal arena), the
# antechamber's door into 305 (P5_DOORS) asks (no: the lobby 296), and inside his view the Clerk's
# menu gets "Leave the arena." (305 has no door of its own). No answer records anything.
# Once per visit (the user, 2026-10-25: "they would need to go to the world map and then back into
# Fun City to trigger the prompt again"): `again` "visit": after a no (or the Clerk's way out) the
# row's doors are quiet until the party has been on a world map; the desk's option stays.
ASK = {
    533: {"text": "Shigeo is on the rampage ahead.\nFace Shigeo now?", "again": "visit"},
    532: {"text": "Marsilio waits in the arena.\nFace Marsilio now?", "doors": [19929509], "again": "visit",
          "options": [{"map": 297, "window": "0x5c50", "choice": "0x5c6c", "text": "Face Marsilio.",
                       "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271}},
                      {"map": 305, "window": "0x6ea8", "choice": "0x6ec4", "text": "Leave the arena.",
                       "to": {"map": 304, "x": 1339, "y": 0, "z": -6, "dir": 180}}]},
}
# The seed's `bosses` rows: the port's keys, then the reference keys (seed_format.md "Hub").
BOSS_KEYS = ("encounter", "name", "map", "maps", "view", "game", "ask", "location", "dungeon", "keys", "trigger", "late")
EVENT_FIENAL_GATE = "Fienal Gate (Fienal requirement)"
EVENT_INDALECIO = "Indalecio"
LIMITER_OFF_EVENT = (TOWNS[1], "Limiter Off (Filia, Central City)", EVENT_LIMITER_OFF)
ISERIA_QUEEN_EVENT = (CAVE, "Iseria Queen (Cave of Trials)", EVENT_ISERIA_QUEEN)


def games_of(side):
    return GAMES if side is None else (side,)


def dungeon_of(row):
    """The dungeon (data.HUB_DUNGEONS) a location lies in, or None (a town, the world maps;
    drops, crafts and killer moves have none of their own)."""
    if row["type"] == "boss" and row.get("encounter") in CHAMBERS:
        d = CHAMBERS[row["encounter"]]["dungeon"]
        return d if d in DUNGEONS else None     # the chambers in towns (Armlock, Fun City, the arena)
    if row["type"] in ("drop", "craft", "killer_move") or row.get("map") is None:
        return None
    return DUNGEON_OF_MAP.get(row["map"])


def reached_games(row):
    """The games in which the hub reaches the location. The chambers: their game (hubchambers
    proves them); chests, gifts, cave and optional bosses: the hub proof's (data.HUB_REACHED);
    shops and drops: the hub-state tables (data.HUB_SHOPS, HUB_DROPS); join sites: none (no join
    scene runs at chapter 60, D7); losable fights: none (D5)."""
    kind = row["type"]
    if kind == "boss" and row["encounter"] in CHAMBERS:
        game = CHAMBERS[row["encounter"]].get("game")
        return GAMES if game is None else (game,)
    if kind == "boss" and row.get("boss") == "losable":
        return ()
    if kind in ("chest", "gift", "boss"):
        return tuple(HUB_REACHED.get(row["id"], ()))
    if kind == "shop":
        return tuple(g for g in GAMES if row["shop"] in HUB_SHOPS[g])
    if kind == "drop":
        return tuple(g for g in GAMES if HUB_DROPS[row["id"]][g])
    return ()


def hub_reasons(row, side):
    """A hub location's missable reasons: the story's, less the soft ones (the chapter never moves,
    so nothing closes: a window or a time limit doesn't apply to what the hub reaches); an optional
    boss stays an "optional quest" (filler only) unless hubchambers confirmed it; "story_route"
    when the hub reaches it in only one of the games the player may play."""
    reasons = set(row["missable"]) - SOFT_REASONS
    if row["type"] == "boss" and row.get("boss") == "optional" and row["encounter"] in CONFIRMED_OPTIONAL:
        reasons.discard("optional quest")
    reached = reached_games(row)
    if any(g not in reached for g in games_of(side)):
        reasons.add("story_route")
    return reasons


def choose(options, party):
    """(row, progress type) of every location of a hub world (locations.choose's hub version):
    what the hub reaches in the game(s) of the story_route, by the options. Left out: join sites
    (characters are items), the losable fights (D5), whatever the hub can't reach (old Clik, the
    tournament, the story scenes' gifts...), the other game's chamber (Varmillion in Claude's game,
    Azamgil in Rena's), the Portal of Sealing (out of hub mode)."""
    side = {0: None, 1: "claude", 2: "rena"}[options.story_route.value]
    first_drop = options.enemy_dropsanity == options.enemy_dropsanity.option_first_drop
    shops = options.shuffle_shops.value
    remove = options.missable_locations == options.missable_locations.option_remove
    out = []
    for row in ROWS:
        kind_of = row["type"]
        if kind_of == "join":
            continue
        if (kind_of == "boss" and not options.boss_checks) or (kind_of == "drop" and not first_drop) \
                or (kind_of == "shop" and (shops == 0 or (shops == 1 and not row["special"]))):
            continue
        if row["region"] == CAVE and not options.cave_of_trials:
            continue
        if "never" in row["missable"] or (side is not None and row["story_route"] not in (None, side)):
            continue
        reached = reached_games(row)
        if not any(g in reached for g in games_of(side)):
            continue
        if dungeon_of(row) in OUT_OF_HUB:
            continue
        reasons = hub_reasons(row, side)
        kind = LocationProgressType.DEFAULT
        if reasons:
            if remove:
                continue
            kind = LocationProgressType.EXCLUDED
        out.append((row, kind))
    if options.craftsanity:
        out += choose_crafts(party)
    if options.killer_move_checks:
        out += choose_killer_moves(party, options.encounter_rate.value)
    out += skillsanity.choose(options, party, hub_mode=True)    # [skillsanity2]
    return out


def fienal_won(world):
    """The encounters the Fienal requirement needs (the gate door's `won`), in chamber order:
    fields_and_berle, fields, or every chamber outside Fienal (all_bosses; a chamber of one game
    only, Varmillion / Azamgil, when the story_route picks that game: under "any" the seed can't
    name the player's, so neither)."""
    value = world.options.fienal_requirement.value
    side = {0: None, 1: "claude", 2: "rena"}[world.options.story_route.value]
    if value == 0:
        want = set(FIELDS) | {BERLE_ARMLOCK}
    elif value == 1:
        want = set(FIELDS)
    else:
        want = {e for e, b in CHAMBERS.items() if b["dungeon"] != FIENAL and b.get("game") in (None, side)}
    return [b["encounter"] for b in HUB_BOSSES if b["encounter"] in want]


def needed_keys(world):
    """The dungeon key groups this world has: a key exists only where something lies behind it
    (the coordinator's ruling): one of this world's locations (a drop aside: it is met in more
    places), a chamber of the Fienal requirement, or the Iseria Queen's cave. Never Fienal (the
    requirement instead) or the Portal of Sealing (out of hub mode)."""
    behind = {dungeon_of(row) for row, _ in world.chosen if row["type"] not in ("drop", "craft", "killer_move")}
    behind |= {CHAMBERS[e]["dungeon"] for e in world.fienal_won}
    if world.options.goal.value == 2:
        behind.add(CAVE)
    return [g for g in HUB_DUNGEONS if g["dungeon"] in behind and g["dungeon"] not in HUB_KEYLESS]


def keyed(world):
    return {g["dungeon"] for g in world.hub_keys}


def access_of(world, dungeon):
    """What entering a place needs: its dungeon's key, Fienal's gate, or nothing (a town, the
    world maps, a dungeon without a key in this seed: its door is not locked)."""
    if dungeon and dungeon in keyed(world):
        return (HUB_KEY_NAME[dungeon],)
    if dungeon == FIENAL:
        return (EVENT_FIENAL_OPEN,)
    return ()


def region_of(world, row):
    """A hub location's region: its dungeon's when that has a key (or is Fienal), else its
    world's towns (a drop, a craft, a killer move: their own rules decide)."""
    d = dungeon_of(row)
    if d == FIENAL or (d and d in keyed(world)):
        return d
    if row["region"] in TOWNS:
        return row["region"]
    return TOWNS[0] if row["region"] == CAVE else TOWNS[1]


def create_regions(world):
    """Menu -> Expel and Energy Nede (their towns, open from the start) -> one region per dungeon
    with a key (entered with it); Energy Nede -> Fienal (the Fienal requirement's event). Events:
    the Fienal gate (the requirement's chambers: their dungeons' keys), Indalecio (the goal, in
    Fienal), and with the limiter-off goals Filia's scene in Central City (no requirement: the hub
    has chapter 60 and flag 730) and the Iseria Queen (the Cave of Trials)."""
    mw, player = world.multiworld, world.player
    names = ["Menu", *TOWNS, *[g["dungeon"] for g in world.hub_keys], FIENAL]
    regions = {name: Region(name, player, mw) for name in names}
    mw.regions += regions.values()
    for town in TOWNS:
        regions["Menu"].connect(regions[town], f"New Game ({town})")
    for g in world.hub_keys:
        key = HUB_KEY_NAME[g["dungeon"]]
        regions[WORLD_REGION[g["world"]]].connect(regions[g["dungeon"]], f"Into {g['dungeon']}",
                                                  lambda state, key=key: state.has(key, player))
    regions[TOWNS[1]].connect(regions[FIENAL], "Into Fienal", lambda state: state.has(EVENT_FIENAL_OPEN, player))
    goal = world.options.goal.value
    events = [(TOWNS[1], EVENT_FIENAL_GATE, EVENT_FIENAL_OPEN), (FIENAL, EVENT_INDALECIO, EVENT_VICTORY)]
    events += ([LIMITER_OFF_EVENT] if goal >= 1 else []) + ([ISERIA_QUEEN_EVENT] if goal == 2 else [])
    for region, name, item in events:
        loc = SO2Location(player, name, None, regions[region])
        regions[region].locations.append(loc)
        loc.place_locked_item(SO2Item(item, ItemClassification.progression, None, player))
    return regions


def drop_rule(world, row, side):
    """A first drop's (what it may hold, its access modes) in hub mode. Met: data.HUB_DROPS, per
    game, the dungeons where random encounters meet the type at the hub state; under story_route
    "any", those of both games (a drop met in only one is EXCLUDED, hub_reasons). Progression
    as in story mode (rules.drop_rule): never with drop_progression never, encounter_rate 0 or a
    chance under 1 / 64 at Luck 0; else only through the maps of a place where the type is met
    with no condition that the hub proof reached in every game the player may play
    (data.HUB_MAPS), outside Expel with expel_progression off; it is then reached only through
    those maps. Access: any of the places' dungeons' (their key, Fienal's gate, or nothing)."""
    o = world.options
    per = [set(HUB_DROPS[row["id"]][g]) for g in games_of(side)]
    met = set.intersection(*per) or set.union(*per)
    modes = sorted({access_of(world, d or None) for d in met})
    if o.drop_progression.current_key == "never" or o.encounter_rate.value == 0 or drop_chance(row) < MIN_DROP_CHANCE:
        return "none", modes
    prog = set()
    for p in row["places"]:
        maps = [m for m in p["maps"] if all(m in HUB_MAPS_OF[g] for g in games_of(side))]
        if not maps:
            continue
        if p["region"] == "Expel" and not o.expel_progression:
            continue
        if p.get("world_map"):
            prog.add(())
        else:
            prog |= {access_of(world, DUNGEON_OF_MAP.get(m)) for m in maps}
    if not prog:
        return "none", modes
    return "any", sorted(prog)


def may_hold_progression(world, row, side):
    """Chests, gifts, bosses: progression only in a chamber, or where the hub proof reached it in
    every game the player may play; never in a gift (all modes), an optional boss hubchambers did
    not confirm, or Expel with expel_progression off."""
    if row["type"] not in ("chest", "boss"):
        return False
    if row["region"] == "Expel" and not world.options.expel_progression:
        return False
    if row["type"] == "boss" and row["encounter"] in CHAMBERS:
        return True
    if row["type"] == "boss" and row.get("boss") == "optional" and row["encounter"] not in CONFIRMED_OPTIONAL:
        return False
    return all(g in reached_games(row) for g in games_of(side))


def set_rules(world):
    """Hub mode's rules (rules.set_rules' hub version): the regions carry the keys; here the
    events, the drops' access, the killer moves' and what each location may hold."""
    player, o = world.player, world.options
    side = {0: None, 1: "claude", 2: "rena"}[o.story_route.value]
    gate = list(dict.fromkeys(k for e in world.fienal_won for k in access_of(world, CHAMBERS[e]["dungeon"])))
    world.get_location(EVENT_FIENAL_GATE).access_rule = lambda state: state.has_all(gate, player)
    goal = o.goal.value
    need = ([EVENT_LIMITER_OFF] if goal >= 1 else []) + ([EVENT_ISERIA_QUEEN] if goal == 2 else [])
    world.get_location(EVENT_INDALECIO).access_rule = lambda state: state.has_all(need, player)
    if goal == 2 and (song := skillsanity.iseria_queen_need(world)):    # skillsanity: Musical Talent
        world.get_location(ISERIA_QUEEN_EVENT[1]).access_rule = song
    world.multiworld.completion_condition[player] = lambda state: state.has(EVENT_VICTORY, player)
    keys_in_drops = bool(o.area_keys_in_drops)
    for loc in world.multiworld.get_locations(player):
        if loc.address is None:
            continue
        row = ROW[loc.name]
        kind = row["type"]
        if kind == "killer_move":
            killer_rule(world, loc, row)
            continue
        if kind in ("guild", "learn"):     # skillsanity [skillsanity2]
            skillsanity.set_location_rule(world, loc, row)
            continue
        if kind == "craft":
            add_item_rule(loc, lambda item: not item.advancement)
            if skillsanity.modes(o)[0] and (rule := skillsanity.craft_rule(world, row)):
                loc.access_rule = rule
            continue
        if kind == "drop":
            allowed, modes = drop_rule(world, row, side)
            if allowed != "none" and loc.progress_type == LocationProgressType.EXCLUDED:
                allowed = "none"
            if allowed != "none" and o.drop_progression.current_key == "force" and \
                    drop_chance(row) >= FORCE_DROP_CHANCE:
                world.forced_drops.add(loc.name)
            if not keys_in_drops:
                add_item_rule(loc, lambda item: not (item.game == GAME and item.code in ALL_KEY_IDS))
            loc.access_rule = lambda state, modes=modes: any(state.has_all(m, player) for m in modes)
        else:
            allowed = "any" if may_hold_progression(world, row, side) else "none"
        if allowed == "none":
            add_item_rule(loc, lambda item: not item.advancement)


# ---- The seed (docs/ap/seed_format.md "Hub") -------------------------------------------------
def choose_start(world):
    """hub_start: the start point's key in data.HUB_STARTS (random_town: any of them, sorted for
    a stable pick: every town of both worlds that hub_state.json `starts` lists)."""
    value = world.options.hub_start.current_key
    if value == "random_town":
        return world.random.choice(sorted(HUB_STARTS))
    return value


def seed_hub(world):
    """The seed's `hub` block: the hub state (data.HUB_STATE) and the chosen start point (its
    row as hub_state.json `starts` has it; hubcore's port reads what it needs)."""
    return dict(HUB_STATE, start=dict(HUB_STARTS[world.hub_start]))


def seed_bosses(world):
    """The seed's `bosses` rows: every chamber (the port plays them whatever the options; a
    victory goes in goal_won), with `keys` the dungeon key this seed locks it with ([] in towns,
    in Fienal and in a dungeon without a key here) and `ask` for the chambers that ask first."""
    numbers = {g["dungeon"]: g["key"] for g in world.hub_keys}
    out = []
    for b in HUB_BOSSES:
        row = dict(b, keys=[numbers[b["dungeon"]]] if b["dungeon"] in numbers else [])
        if b["encounter"] in ASK:
            row["ask"] = copy.deepcopy(ASK[b["encounter"]])
        out.append({k: row[k] for k in BOSS_KEYS if row.get(k) is not None})
    return out


def seed_doors(world):
    """Every door into a dungeon with a key, locked by it (with the routine of its trigger, so the
    port refuses it there, unless that routine also leads elsewhere: data.HUB_DUNGEONS has None
    then, and the port refuses the transfer alone); Fienal's entrance with the Fienal
    requirement's `won` and a banner per boss (no key); the P5 doors (P5_DOORS: the arena 305)."""
    doors = []
    for g in world.hub_keys:
        for d in g["doors"]:
            door = {"id": d["id"], "key": g["key"], "routine": d["routine"],
                    "name": f"{g['dungeon']} (map {d['map']} -> {d['to']})"}
            doors.append({k: v for k, v in door.items() if v is not None})
    won = list(world.fienal_won)
    for d in DUNGEONS[FIENAL]["doors"]:
        doors.append({"id": d["id"], "routine": d["routine"], "won": won, "banners": [BANNERS[e] for e in won],
                      "name": FIENAL_GATE_NAMES[world.options.fienal_requirement.value]})
    doors += [dict(d, to=dict(d["to"])) for d in P5_DOORS]
    return doors


__all__ = ["choose", "create_regions", "set_rules", "needed_keys", "fienal_won", "seed_hub", "seed_bosses",
           "seed_doors", "choose_start", "region_of", "dungeon_of", "reached_games", "OUT_OF_HUB", "P5_DOORS",
           "CONFIRMED_OPTIONAL", "ASK"]
