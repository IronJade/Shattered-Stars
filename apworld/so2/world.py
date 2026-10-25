"""Star Ocean: The Second Story (PlayStation, NA), played on Shattered Stars (a native port).

Story mode: the chests and the gifts that give items or Fol are shuffled; the story's key
items (script flags) stay in place, so the story always goes on. The goal is the final boss.
Open world locks the world maps' entrances behind area keys; hub mode (hub.py) starts after the
Final Save Point with both worlds open, a key per dungeon and the story bosses in their chambers.
The output is one .apso2 seed per player (docs/ap/seed_format.md), which the port loads.
"""
import json
import logging
import os
from typing import NamedTuple, Optional

from BaseClasses import ItemClassification, Tutorial
from worlds.AutoWorld import WebWorld, World

from . import hub, items, locations, regions, rules
from .data import EXIT_GATES, GOAL_ENCOUNTERS, KEY_GROUPS, STORY_GATES
from .options import SO2Options, option_groups
from .patches import placement_patches

SEED_FORMAT_VERSION = 1         # a seed that uses none of the v2 fields (docs/ap/seed_format.md)
SEED_FORMAT_V2 = 2              # one that does: the 0.02.00 port refuses it rather than ignore them
SEED_FORMAT_V3 = 3              # a hub seed (seed_format.md "Hub"): a wave-2 port refuses it
APWORLD_VERSION = "0.2.0"

# The goals (docs/ap/seed_format.md, "v2 fields": Goals; scratchpad/openworld/goals.md): the
# seed's goal object and the gate on the Final Save Point floor's way to Indalecio (door
# 43978493, map 671 -> 684), which the port keeps shut until the goal's conditions hold.
LIMITER_FLAGS = [445, 446]      # the Clik meeting with Filia (Claude's game, Rena's), preset by the port
LIMITER_OFF_FLAG = 556          # set only by Filia's scene in Central City City Hall (map 239)
ISERIA_QUEEN = 561
GOAL_GATE = {"id": 43978493, "routine": "0x3bec"}
GOAL_BANNERS = {"flags": "Talk to Filia in Central City first", "won": "Defeat the Iseria Queen first"}    # <= 236 px each
GOAL_GATE_NAMES = {1: "Indalecio (Limiter Off)", 2: "Indalecio (Limiter Off, Iseria Queen)"}    # the port logs it


class Party(NamedTuple):
    """Who can be in this world's party (docs/ap/seed_format.md, "Party v2")."""
    rules: str                  # "story": phase 3's rules; "open": open world with character_items
    story_members: tuple        # join through the story's own scenes (party rules "open")
    start: Optional[str]        # starting_character (open world): joins at the start and leads
    pool: list                  # the character items
    guaranteed: frozenset       # surely in the party in the end (craft member locks)
    possible: frozenset         # can ever be in the party
    choices: tuple = ()         # starting_character "any": whom the player may pick at New Game


class SO2Web(WebWorld):
    theme = "partyTime"
    option_groups = option_groups
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "Playing Star Ocean: The Second Story in a multiworld on Shattered Stars.",
        "English", "setup_en.md", "setup/en", ["IronJade"])]


class SO2World(World):
    """Star Ocean: The Second Story: Claude and Rena's journey across Expel and Energy Nede,
    played on the Shattered Stars port. Chests and gifts are shuffled; the goal is Indalecio."""
    game = items.GAME
    web = SO2Web()
    options_dataclass = SO2Options
    options: SO2Options
    item_name_to_id = items.ITEM_NAME_TO_ID
    location_name_to_id = locations.LOCATION_NAME_TO_ID
    item_name_groups = items.ITEM_NAME_GROUPS
    location_name_groups = locations.LOCATION_NAME_GROUPS
    origin_region_name = "Menu"

    def generate_early(self):
        o = self.options
        self.open_world = o.world_mode == o.world_mode.option_open_world
        self.hub = o.world_mode == o.world_mode.option_hub     # characters are items whatever character_items says
        self.party_info = self.plan_party()
        self.party = self.party_info.pool
        self.chosen = (hub.choose if self.hub else locations.choose)(o, self.party_info)
        self.forced_drops = set()       # drop_progression force (rules.set_rules)
        # Characters the logic needs (a member's killer-move levels that may hold progression need
        # their item when the member joins by it: killer_move_progression any, or hero for a hero
        # who is an item, hub mode under story_route any) are progression; any other is useful.
        killers = {row["character"] for row, _ in self.chosen if row["type"] == "killer_move"}
        mode = o.killer_move_progression.current_key
        if mode == "hero":
            killers &= set(self.party_info.story_members)
        self.progression_characters = set(self.party) & killers if mode in ("any", "hero") else set()
        if self.hub:
            self.fienal_won = hub.fienal_won(self)      # the Fienal requirement (D6)
            self.hub_keys = hub.needed_keys(self)       # the dungeon keys this world has (D4)
            self.hub_start = hub.choose_start(self)     # the start point (D9)

    def story_members(self):
        """The heroes: the story route's, or both under "any" (the player picks at New Game)."""
        return {"claude": ("Claude",), "rena": ("Rena",)}.get(self.options.story_route.current_key, ("Claude", "Rena"))

    def plan_party(self):
        """The party this world allows (Party). Story mode, or open world without
        character_items: the story's party rules (choose_party); open world with character_items:
        open world's (choose_open_party). starting_character only in open world and hub mode.
        Hub mode: open world's rules with character items always (D7); no join scene runs at
        chapter 60, so under story_route "any" both heroes are items too (the one the player
        picks at New Game is in the party already: their item does nothing).
        starting_character "any" (open world and hub): the player picks at New Game's character
        select, so the logic counts on nobody extra (start None) and the choices are the members
        who can be in the party: the heroes and the pool (8 at most), or under the story's party
        rules everyone not excluded. The pick's own item, when it comes, finds them in the party."""
        o = self.options
        heroes = self.story_members()
        start = self.choose_start(heroes)
        pick = (self.open_world or self.hub) and o.starting_character.value == o.starting_character.option_any
        order = items.CHARACTER_NAMES.index
        if self.hub:
            others = self.choose_open_party(heroes, start)
            pool = others if len(heroes) == 1 else \
                sorted(set(others) | {h for h in heroes if h != start}, key=order)
            sure = (set(heroes) if len(heroes) == 1 else set()) | ({start} if start else set())
            choices = tuple(sorted(set(heroes) | set(pool), key=order)) if pick else ()
            return Party("open", heroes, start, pool, frozenset(sure), frozenset(sure | set(heroes) | set(pool)), choices)
        fixed = set(items.FIXED_MEMBERS) | ({start} if start else set())
        if not (self.open_world and o.character_items):
            pool = self.choose_party()
            possible = (fixed | set(pool)) if o.character_items else set(items.CHARACTER_NAMES)
            choices = tuple(n for n in items.CHARACTER_NAMES
                            if n in heroes or n not in o.exclude_characters.value) if pick else ()
            return Party("story", heroes, start, pool, frozenset(fixed), frozenset(possible), choices)
        pool = self.choose_open_party(heroes, start)
        sure = set(heroes) | ({start} if start else set())
        choices = tuple(sorted(set(heroes) | set(pool), key=order)) if pick else ()
        return Party("open", heroes, start, pool, frozenset(sure), frozenset(sure | set(pool)), choices)

    def choose_start(self, heroes):
        """starting_character, open world and hub only: None (the hero leads, and under "any",
        where the player picks at New Game: Party.choices), or a named member. A named member wins
        over exclude_characters; the hero of a one-hero route means nobody extra."""
        o = self.options
        value = o.starting_character.value
        if value == o.starting_character.option_hero:
            return None
        if not (self.open_world or self.hub):
            logging.warning(f"{self.player_name}: starting_character is for open world; ignored in story mode")
            return None
        if value == o.starting_character.option_any:
            return None
        name = items.CHARACTER_NAMES[value - 1]
        if name in o.exclude_characters.value:
            logging.warning(f"{self.player_name}: starting_character {name} is in exclude_characters; {name} starts")
        return None if heroes == (name,) else name

    def choose_open_party(self, heroes, start):
        """Open world's character items: any member but the heroes and the starting character,
        none in exclude_characters, as many as fit the party's 8 places with the heroes and the
        starting member (a random choice when more are allowed). The healer guarantee: when
        neither the heroes, the starting member nor the pool has Rena or Noel, one of them (the
        ones not excluded) takes a random pool member's place; with both excluded, none."""
        rng = self.random
        excluded = set(self.options.exclude_characters.value)
        if excluded & set(heroes):
            logging.warning(f"{self.player_name}: exclude_characters can't exclude the story route's hero "
                            f"({', '.join(sorted(excluded & set(heroes)))})")
        allowed = [n for n in items.CHARACTER_NAMES if n not in heroes and n != start and n not in excluded]
        room = items.PARTY_SIZE - len(heroes) - (1 if start and start not in heroes else 0)
        pool = list(allowed) if len(allowed) <= room else rng.sample(allowed, room)
        if not ({start, *heroes, *pool} & set(items.HEALERS)):
            healers = [n for n in items.HEALERS if n in allowed]
            if healers:     # the pool is full (an allowed healer would be in it otherwise)
                pool.remove(rng.choice(pool))
                pool.append(rng.choice(healers))
        return sorted(pool, key=items.CHARACTER_NAMES.index)

    def choose_party(self):
        """The characters this world's pool holds (character_items): a party the story allows.
        One of Bowman / Precis (Precis rules Bowman out), one of Ashton / Opera with Ernest
        (Ashton rules Opera out, and Ernest comes only with Opera), Celine, Chisato, and Leon in
        Claude's game or Dias in Rena's (neither when the story_route is "any": the port turns
        away the wrong one). With Claude, Rena and Noel, at most 8: when there is one too many,
        Ernest stays out (Opera can join alone) or one of the others does."""
        if not self.options.character_items:
            return []
        rng = self.random
        side = self.options.story_route.current_key
        singles = ["Celine", "Chisato"] + {"claude": ["Leon"], "rena": ["Dias"]}.get(side, [])
        party = [rng.choice(["Bowman", "Precis"])] + rng.choice([["Ashton"], ["Opera", "Ernest"]]) + singles
        while len(party) > items.PARTY_SIZE - len(items.FIXED_MEMBERS):
            party.remove(rng.choice([x for x in party if x in singles or x == "Ernest"]))
        return sorted(party, key=items.CHARACTER_ITEMS.index)

    def fill_hook(self, progitempool, usefulitempool, filleritempool, fill_locations):
        """drop_progression force: the fair-odds drops that may hold progression (rules.py,
        `forced_drops`) take progression items first, any player's, as far as the logic allows;
        those it can't fill are filled as usual."""
        drops = [loc for loc in fill_locations if loc.player == self.player and loc.name in self.forced_drops]
        if not drops or not progitempool:
            return
        from Fill import fill_restrictive, sweep_from_pool
        self.random.shuffle(drops)
        candidates = [item for item in progitempool if not item.deprioritized]
        single = self.multiworld.players == 1 and not self.multiworld.groups
        fill_restrictive(self.multiworld, sweep_from_pool(self.multiworld.state), drops, candidates,
                         single_player_placement=single, swap=False, allow_partial=True, one_item_per_player=False,
                         name="SO2 forced drops")
        progitempool[:] = [item for item in progitempool if item.location is None]
        fill_locations[:] = [loc for loc in fill_locations if loc.item is None]

    def create_regions(self):
        made = (hub.create_regions if self.hub else regions.create_regions)(self)
        for row, kind in self.chosen:
            region = made[hub.region_of(self, row) if self.hub else row["region"]]
            loc = locations.SO2Location(self.player, row["name"], row["id"], region)
            loc.progress_type = kind
            region.locations.append(loc)

    def create_item(self, name):
        cls = items.classification(name)
        if name in getattr(self, "progression_characters", ()):
            cls = ItemClassification.progression | ItemClassification.useful
        return items.SO2Item(name, cls, self.item_name_to_id[name], self.player)

    def create_items(self):
        # The pool is the vanilla contents of this world's locations (a boss or join location
        # adds a Blueberry), with the party's characters in place of the join sites' filler,
        # then the traps option's share of the filler turned into traps. The cave's gems are
        # locked at their own locations instead.
        names = [items.pool_filler(row["item"]) if row["type"] == "craft"
                 else items.FILLER_NAME if row["type"] == "killer_move" else items.item_name(row["item"], row["fol"])
                 for row, _ in self.chosen]
        if self.hub:
            return self.create_hub_items(names)
        joins = [k for k, (row, _) in enumerate(self.chosen) if row["type"] == "join"]
        if len(joins) < len(self.party):
            raise Exception(f"{self.player_name}: {len(self.party)} characters but {len(joins)} join locations")
        for k, who in zip(joins, self.party):
            names[k] = who
        pool = []
        for (row, _), name in zip(self.chosen, names):
            if row["type"] in ("chest", "gift") and row["item"] in items.PUZZLE_ITEMS:
                # A cave door's gem stays where the cave puts it (items.PUZZLE_ITEMS).
                self.get_location(row["name"]).place_locked_item(self.create_item(name))
            else:
                pool.append(self.create_item(name))
        if self.open_world:
            # The area keys: the starting ones are given, the rest take the place of as many filler
            # items (the pool keeps one item per location).
            start = list(items.START_KEYS) + ([] if self.options.expel_teleporter else ["North City Pass"])
            for name in start:
                self.multiworld.push_precollected(self.create_item(name))
            keys = [g["name"] for g in KEY_GROUPS if g["name"] not in start]
            filler = [k for k, it in enumerate(pool) if it.classification == ItemClassification.filler]
            if len(filler) < len(keys):
                raise Exception(f"{self.player_name}: {len(keys)} keys but only {len(filler)} filler items to replace")
            for k, name in zip(sorted(self.random.sample(filler, len(keys))), keys):
                pool[k] = self.create_item(name)
        filler = [k for k, it in enumerate(pool) if it.classification == ItemClassification.filler]
        count = len(filler) * self.options.traps.value // 100
        trap_names = sorted(items.TRAPS)
        for k in self.random.sample(filler, count):
            pool[k] = self.create_item(self.random.choice(trap_names))
        self.multiworld.itempool += pool

    def create_hub_items(self, names):
        """Hub mode's pool: the vanilla contents of this world's locations (the cave's gems
        locked at their own), then the dungeon keys and the characters (no join sites: D7) in the
        place of as many filler items, then the traps."""
        pool = []
        for (row, _), name in zip(self.chosen, names):
            if row["type"] in ("chest", "gift") and row["item"] in items.PUZZLE_ITEMS:
                self.get_location(row["name"]).place_locked_item(self.create_item(name))
            else:
                pool.append(self.create_item(name))
        extra = [items.HUB_KEY_NAME[g["dungeon"]] for g in self.hub_keys] + list(self.party)
        filler = [k for k, it in enumerate(pool) if it.classification == ItemClassification.filler]
        if len(filler) < len(extra):
            raise Exception(f"{self.player_name}: {len(extra)} keys and characters but only {len(filler)} filler items")
        for k, name in zip(sorted(self.random.sample(filler, len(extra))), extra):
            pool[k] = self.create_item(name)
        filler = [k for k, it in enumerate(pool) if it.classification == ItemClassification.filler]
        count = len(filler) * self.options.traps.value // 100
        trap_names = sorted(items.TRAPS)
        for k in self.random.sample(filler, count):
            pool[k] = self.create_item(self.random.choice(trap_names))
        self.multiworld.itempool += pool

    def get_filler_item_name(self):
        return items.FILLER_NAME

    def set_rules(self):
        (hub.set_rules if self.hub else rules.set_rules)(self)

    def goal_encounters(self):
        """The encounters the goal needs: any of them (indalecio, limiter off), or all of them
        (with the Iseria Queen)."""
        return {0: list(GOAL_ENCOUNTERS), 1: [540], 2: [ISERIA_QUEEN, 540]}[self.options.goal.value]

    def seed_goal(self):
        """The seed's goal object (docs/ap/seed_format.md, "v2 fields": Goals)."""
        goal = self.options.goal.value
        if goal == 0:
            return {"encounters": list(GOAL_ENCOUNTERS)}
        out = {"encounters": [540], "flags": list(LIMITER_FLAGS)}
        if goal == 2:
            out = {"all": [ISERIA_QUEEN, 540], **out}
        return out

    def goal_doors(self):
        """The goal's gate (both world modes): the way to Indalecio stays shut until Filia took
        his limiter off (flag 556) and, for the Iseria Queen goal, she was beaten."""
        goal = self.options.goal.value
        if goal == 0:
            return []
        door = dict(GOAL_GATE, flags=[LIMITER_OFF_FLAG])
        banners = [GOAL_BANNERS["flags"]]
        if goal == 2:
            door["won"] = [ISERIA_QUEEN]
            banners.append(GOAL_BANNERS["won"])
        door.update(banners=banners, name=GOAL_GATE_NAMES[goal])
        return [door]

    def seed_version(self):
        """1, or 2 when the seed uses a field an older port would ignore (docs/ap/seed_format.md,
        "The version rule"); 3 for a hub seed ("Hub")."""
        o = self.options
        if self.hub:
            return SEED_FORMAT_V3
        v2 = (o.goal.value != 0 or self.party_info.rules == "open" or self.party_info.start is not None
              or bool(self.party_info.choices)
              or bool(o.craftsanity) or bool(o.no_fail_crafting) or bool(o.killer_move_checks))
        return SEED_FORMAT_V2 if v2 else SEED_FORMAT_VERSION

    def fill_slot_data(self):
        o = self.options
        p = self.party_info
        data = {
            "seed_format": self.seed_version(),
            "goal_encounters": self.goal_encounters(),
            "world_mode": o.world_mode.current_key,
            "expel_teleporter": self.open_world and bool(o.expel_teleporter),
            "shuffle_shops": o.shuffle_shops.current_key,
            "story_route": o.story_route.current_key,
            "missable_locations": o.missable_locations.current_key,
            "expel_progression": bool(o.expel_progression),
            "cave_of_trials": bool(o.cave_of_trials),
            "boss_checks": bool(o.boss_checks),
            "character_items": bool(o.character_items),
            "characters": list(self.party),
            "enemy_dropsanity": o.enemy_dropsanity.current_key,
            "traps": o.traps.value,
            "exp_multiplier": o.exp_multiplier.value,
            "fol_multiplier": o.fol_multiplier.value,
            "encounter_rate": o.encounter_rate.value,
            "death_link": bool(o.death_link),
            "goal": o.goal.current_key,
            "drop_progression": o.drop_progression.current_key,
            "area_keys_in_drops": bool(o.area_keys_in_drops),
            "craftsanity": bool(o.craftsanity),
            "no_fail_crafting": bool(o.no_fail_crafting),
            "killer_move_checks": bool(o.killer_move_checks),
            "killer_move_progression": o.killer_move_progression.current_key,
            # A name, None, or (starting_character "any") the list New Game's select offers.
            "starting_character": list(p.choices) if p.choices else p.start,
            "exclude_characters": sorted(o.exclude_characters.value, key=items.CHARACTER_NAMES.index),
            "party_rules": p.rules,
            "apworld_version": APWORLD_VERSION,
        }
        if self.hub:
            # Hub mode (seed_format.md "Hub"; reference: the port reads the `hub` block and the
            # doors). Characters are always items (D7).
            data.update(character_items=True, hub_start=o.hub_start.current_key, hub_start_town=self.hub_start,
                        fienal_requirement=o.fienal_requirement.current_key, fienal_won=list(self.fienal_won),
                        dungeon_keys=[items.HUB_KEY_NAME[g["dungeon"]] for g in self.hub_keys])
        return data

    def seed_data(self):
        """The .apso2 seed (docs/ap/seed_format.md) as a dict."""
        mw, player = self.multiworld, self.player
        own_items, locs = {}, []
        for loc in sorted(mw.get_locations(player), key=lambda l: l.address or 0):
            if loc.address is None:
                continue
            row = locations.ROW[loc.name]
            item = loc.item
            entry = {"id": loc.address, "name": loc.name, "check": locations.seed_check(row),
                     "sites": locations.seed_sites(row)}
            cls = ("progression" if item.advancement else "trap" if item.trap
                   else "useful" if item.useful else "filler")
            if item.player == player:
                described = items.seed_item(item.name, item.code)
                own_items[str(item.code)] = described
                entry["item"] = dict(described, player=player, classification=cls)
            else:
                entry["item"] = {"player": item.player, "player_name": mw.get_player_name(item.player),
                                 "name": item.name, "ap_id": item.code, "classification": cls}
            locs.append(entry)
        patches = placement_patches(locs)
        for entry in locs:
            del entry["sites"]
        # The port gives a received item from this table, so it lists every item of this
        # player's, wherever in the multiworld it was placed (a character in another world too).
        for loc in mw.get_filled_locations():
            item = loc.item
            if item.player == player and item.code is not None and str(item.code) not in own_items:
                own_items[str(item.code)] = items.seed_item(item.name, item.code)
        start_items, start_fol = [], 0
        for item in mw.precollected_items[player]:
            if item.code is None:
                continue
            described = items.seed_item(item.name, item.code)
            own_items[str(item.code)] = described
            if described["kind"] == "fol":
                start_fol += described["amount"]
            else:
                start_items.append(dict(described))
        return {
            "format": "apso2",
            "version": self.seed_version(),
            "seed": mw.seed_name,
            "game": self.game,
            "slot": {"name": mw.get_player_name(player), "id": player},
            # The port reads options.deathlink (seed_format.md); the rest is kept for reference.
            "options": dict(self.fill_slot_data(), deathlink=bool(self.options.death_link)),
            "items": own_items,
            "locations": locs,
            "patches": patches,
            "start": {"items": start_items, "fol": start_fol, "flags": []},
            "goal": self.seed_goal(),
            "doors": self.seed_doors() + self.goal_doors(),
            # Hub mode: the state New Game builds and the chambers (seed_format.md "Hub").
            **({"hub": hub.seed_hub(self), "bosses": hub.seed_bosses(self)} if self.hub else {}),
        }

    def seed_doors(self):
        """Open world: every world-map entrance with its area's key (docs/ap/seed_format.md,
        "Phase 4"; ids from docs/ap/doors.json), then each one-way story point's door with the
        keys it needs (data.STORY_GATES: the El voyage's trigger in the Lacour Front Line's
        meeting room) and the exits kept shut until a chapter. Story mode: none. Hub mode: the
        dungeons' doors and Fienal's gate (hub.seed_doors)."""
        if self.hub:
            return hub.seed_doors(self)
        if not self.open_world:
            return []
        world_name = {1: "Expel", 2: "Energy Nede"}
        doors = [{"id": d, "key": g["key"], "name": f"{world_name[g['world']]} -> {g['group']}"}
                 for g in KEY_GROUPS for d in g["doors"]]
        doors += [{"id": gate["door"], "routine": f"{gate['routine']:#x}", "keys": regions.gate_keys(self, gate),
                   "name": gate["name"], "purpose": gate["purpose"]} for gate in STORY_GATES]
        # Exits kept shut until a chapter (data.EXIT_GATES: Central City's and Noel's House's
        # before the Synard), so nobody walks out of an area the story brought them into.
        doors += [{"id": e["door"], "routine": f"{e['routine']:#x}", "keys": list(e["keys"]), "until": e["until"],
                   "name": e["name"], "purpose": e["purpose"]} for e in EXIT_GATES]
        return doors

    def generate_output(self, output_directory):
        path = os.path.join(output_directory, f"{self.multiworld.get_out_file_name_base(self.player)}.apso2")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(self.seed_data(), f, indent=1)
            f.write("\n")
