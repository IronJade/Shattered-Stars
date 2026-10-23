"""Star Ocean: The Second Story (PlayStation, NA), played on Shattered Stars (a native port).

Story mode: the chests and the gifts that give items or Fol are shuffled; the story's key
items (script flags) stay in place, so the story always goes on. The goal is the final boss.
The output is one .apso2 seed per player (docs/ap/seed_format.md), which the port loads.
"""
import json
import os

from BaseClasses import ItemClassification, Tutorial
from worlds.AutoWorld import WebWorld, World

from . import items, locations, regions, rules
from .data import GOAL_ENCOUNTERS
from .options import SO2Options, option_groups
from .patches import placement_patches

SEED_FORMAT_VERSION = 1


class SO2Web(WebWorld):
    theme = "partyTime"
    option_groups = option_groups
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "Playing Star Ocean: The Second Story in a multiworld on Shattered Stars.",
        "English", "setup_en.md", "setup/en", ["Shattered Stars"])]


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
        self.chosen = locations.choose(self.options)
        self.party = self.choose_party()

    def choose_party(self):
        """The characters this world's pool holds (character_items): a party the story allows.
        One of Bowman / Precis (Precis rules Bowman out), one of Ashton / Opera with Ernest
        (Ashton rules Opera out, and Ernest comes only with Opera), Celine, Chisato, and Leon in
        Claude's game or Dias in Rena's (neither when the protagonist is "any": the port turns
        away the wrong one). With Claude, Rena and Noel, at most 8: when there is one too many,
        Ernest stays out (Opera can join alone) or one of the others does."""
        if not self.options.character_items:
            return []
        rng = self.random
        side = self.options.protagonist.current_key
        singles = ["Celine", "Chisato"] + {"claude": ["Leon"], "rena": ["Dias"]}.get(side, [])
        party = [rng.choice(["Bowman", "Precis"])] + rng.choice([["Ashton"], ["Opera", "Ernest"]]) + singles
        while len(party) > items.PARTY_SIZE - len(items.FIXED_MEMBERS):
            party.remove(rng.choice([x for x in party if x in singles or x == "Ernest"]))
        return sorted(party, key=items.CHARACTER_ITEMS.index)

    def create_regions(self):
        made = regions.create_regions(self)
        for row, kind in self.chosen:
            loc = locations.SO2Location(self.player, row["name"], row["id"], made[row["region"]])
            loc.progress_type = kind
            made[row["region"]].locations.append(loc)

    def create_item(self, name):
        return items.SO2Item(name, items.classification(name), self.item_name_to_id[name], self.player)

    def create_items(self):
        # The pool is the vanilla contents of this world's locations (a boss or join location
        # adds a Blueberry), with the party's characters in place of the join sites' filler,
        # then the traps option's share of the filler turned into traps.
        names = [items.item_name(row["item"], row["fol"]) for row, _ in self.chosen]
        joins = [k for k, (row, _) in enumerate(self.chosen) if row["type"] == "join"]
        if len(joins) < len(self.party):
            raise Exception(f"{self.player_name}: {len(self.party)} characters but {len(joins)} join locations")
        for k, who in zip(joins, self.party):
            names[k] = who
        pool = [self.create_item(n) for n in names]
        filler = [k for k, it in enumerate(pool) if it.classification == ItemClassification.filler]
        count = len(filler) * self.options.traps.value // 100
        trap_names = sorted(items.TRAPS)
        for k in self.random.sample(filler, count):
            pool[k] = self.create_item(self.random.choice(trap_names))
        self.multiworld.itempool += pool

    def get_filler_item_name(self):
        return items.FILLER_NAME

    def set_rules(self):
        rules.set_rules(self)

    def fill_slot_data(self):
        o = self.options
        return {
            "seed_format": SEED_FORMAT_VERSION,
            "goal_encounters": list(GOAL_ENCOUNTERS),
            "protagonist": o.protagonist.current_key,
            "missable_locations": o.missable_locations.current_key,
            "expel_progression": bool(o.expel_progression),
            "cave_of_trials": bool(o.cave_of_trials),
            "boss_checks": bool(o.boss_checks),
            "character_items": bool(o.character_items),
            "characters": list(self.party),
            "drops_as_checks": o.drops_as_checks.current_key,
            "traps": o.traps.value,
            "exp_multiplier": o.exp_multiplier.value,
            "fol_multiplier": o.fol_multiplier.value,
            "encounter_rate": o.encounter_rate.value,
            "death_link": bool(o.death_link),
        }

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
            "version": SEED_FORMAT_VERSION,
            "seed": mw.seed_name,
            "game": self.game,
            "slot": {"name": mw.get_player_name(player), "id": player},
            # The port reads options.deathlink (seed_format.md); the rest is kept for reference.
            "options": dict(self.fill_slot_data(), deathlink=bool(self.options.death_link)),
            "items": own_items,
            "locations": locs,
            "patches": patches,
            "start": {"items": start_items, "fol": start_fol, "flags": []},
            "goal": {"encounters": list(GOAL_ENCOUNTERS)},
        }

    def generate_output(self, output_directory):
        path = os.path.join(output_directory, f"{self.multiworld.get_out_file_name_base(self.player)}.apso2")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(self.seed_data(), f, indent=1)
            f.write("\n")
