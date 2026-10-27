"""The SO2 world's test base and the helpers its test modules share (Archipelago's docs/tests.md:
a base in a file that doesn't start with `test`, imported by the test modules).

Every test class derives from SO2TestBase (one option mix per class: WorldTestBase's default tests
run for each, plus the class's own) or, where no multiworld is needed, unittest.TestCase. The
checks shared by several mixes live in mixin classes (`...Checks`) next to their tests; a mixin is
never collected on its own (its name doesn't start with Test and it isn't a TestCase)."""
import json
import os
import re

from BaseClasses import CollectionState, ItemClassification
from test.bases import WorldTestBase

from ..data import GOAL_ENCOUNTERS, ITEM_BASE
from ..items import GAME, SO2Item
from ..locations import ROW
from ..patches import role_of_placeholder
from ..world import SO2World

SIDES = {0: None, 1: "claude", 2: "rena"}   # story_route's value -> the game(s) the player may play
PATCH = re.compile(r"^script (\d+) 0x([0-9a-f]+) 0x([0-9a-f]{8}) 0x([0-9a-f]{8})$")
# This repo's root, through the worlds/so2 symlink of a dev checkout (realpath: the link's target).
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))))


class SO2TestBase(WorldTestBase):
    game = GAME
    world: SO2World

    @property
    def side(self):
        """The story_route's game: "claude", "rena", or None (either; also before the world is made)."""
        world = getattr(self, "world", None)
        return SIDES[world.options.story_route.value] if world is not None else None

    def assertSweptAccessDependency(self, locations, possible_items):
        """assertAccessDependency (only_check_listed) through the events: the listed locations
        can't be reached with every item but `possible_items` and only the events that state
        reaches (a sweep), and can with any one of the combinations (and its sweep). For logic
        that runs through event chains (the story's stages, Fienal's gate), which
        assertAccessDependency collects up front."""
        names = {n for combo in possible_items for n in combo}
        state = CollectionState(self.multiworld)
        for item in self.multiworld.get_items():
            if item.code is not None and item.name not in names:   # events (code None): swept
                state.collect(item, True)
        state.sweep_for_advancements()
        for name in locations:
            self.assertFalse(self.can_reach_in(state, name), f"{name} is reachable without {sorted(names)}")
        for combo in possible_items:
            with_items = state.copy()
            for item in self.get_items_by_name(combo):
                with_items.collect(item, True)
            with_items.sweep_for_advancements()
            for name in locations:
                self.assertTrue(self.can_reach_in(with_items, name), f"{name} not reachable with {combo}")

    def can_reach_in(self, state, location):
        return state.can_reach(location, "Location", self.player)


def own_locations(test):
    """The world's own locations with an id (no events)."""
    return [loc for loc in test.multiworld.get_locations(test.player) if loc.address is not None]


def own_pool(test):
    return [i for i in test.multiworld.itempool if i.player == test.player]


def rows_of(test, kind):
    return [ROW[loc.name] for loc in own_locations(test) if ROW[loc.name]["type"] == kind]


def fill(test):
    from Fill import distribute_items_restrictive
    distribute_items_restrictive(test.multiworld)


def progression_item(test, player=None):
    """A progression item of another player's (or `player`'s): what `can_fill` must refuse where
    no progression may go."""
    return SO2Item("test key", ItemClassification.progression, None, test.player + 1 if player is None else player)


def doors_json():
    """docs/ap/doors.json, or None outside this repo (a packaged .apworld): the door checks skip."""
    path = os.path.join(ROOT, "docs", "ap", "doors.json")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def assert_no_gift_progression(test):
    """No gift location holds progression (the user, 2026-10-25): not this world's own keys
    (open world's area keys) and not another world's, whatever `may_hold_progression` /
    `open_world_progression` would otherwise let through."""
    gifts = [loc for loc in own_locations(test) if ROW[loc.name]["type"] == "gift"]
    test.assertTrue(gifts)
    other = progression_item(test)
    st = test.multiworld.state
    for loc in gifts:
        test.assertFalse(loc.can_fill(st, other, False), loc.name)
        if test.world.open_world:
            mine = test.world.create_item("Salva Pass")   # this world's own progression (a key)
            test.assertFalse(loc.can_fill(st, mine, False), loc.name)


def check_seed(test, seed):
    """The seed's shape: ids, checks, placeholders and patch lines (docs/ap/seed_format.md)."""
    test.assertEqual((seed["format"], seed["version"]), ("apso2", test.world.seed_version()))
    test.assertEqual(seed["goal"], test.world.seed_goal())
    if test.world.options.goal.value == 0:
        test.assertEqual(seed["goal"], {"encounters": list(GOAL_ENCOUNTERS)})
    locs = seed["locations"]
    test.assertEqual(len(locs), len(own_locations(test)))
    test.assertEqual(len({l["id"] for l in locs}), len(locs))
    placeholders = [l["placeholder"] for l in locs if "placeholder" in l]
    test.assertEqual(len(set(placeholders)), len(placeholders))
    sites = 0
    for l in locs:
        row = ROW[l["name"]]
        check = l["check"]
        test.assertEqual(check["type"], row["type"])
        if row["type"] in ("chest", "gift"):
            sites += len(row["sites"])
            test.assertEqual(role_of_placeholder(l["placeholder"]), row["sites"][0][4])
        else:
            test.assertNotIn("placeholder", l)
        if row["type"] == "chest":
            test.assertEqual((check["map"], check["flag"]), (row["map"], row["flag"]))
        elif row["type"] == "gift":
            test.assertEqual((check["map"], check["offset"]), (row["map"], row["offset"]))
        elif row["type"] == "boss":
            test.assertEqual(check["encounter"], row["encounter"])
            test.assertEqual(l["id"], ITEM_BASE + (9 << 21) + row["encounter"])
            test.assertNotIn(row["encounter"], GOAL_ENCOUNTERS)
        elif row["type"] == "join":
            test.assertEqual((check["map"], check["offset"], check["character"]),
                             (row["map"], row["offset"], row["character"]))
        elif row["type"] == "drop":
            test.assertEqual(check["enemy"], row["enemy"])
            test.assertEqual(l["id"], ITEM_BASE + (8 << 21) + row["enemy"])
        elif row["type"] == "craft":
            test.assertEqual(check, {"type": "craft", "item": row["item"]})
            test.assertEqual(l["id"], ITEM_BASE + (11 << 21) + row["item"])
            test.assertNotEqual(l["item"]["classification"], "progression")
        elif row["type"] == "shop":
            test.assertEqual((check["shop"], check["index"], check["item"]), (row["shop"], row["index"], row["item"]))
            test.assertEqual(l["id"], ITEM_BASE + (10 << 21) + (row["shop"] << 8) + row["index"])
            test.assertNotEqual(l["item"]["classification"], "progression")
        item = l["item"]
        if item["player"] == test.player:
            test.assertIn(item["kind"], ("item", "fol", "character", "trap", "key", "skill", "move"))  # skillsanity
            test.assertIn(str(test.world.item_name_to_id[item["name"]]), seed["items"])
        else:
            test.assertIn("ap_id", item)
    test.assertEqual(len(seed["patches"]), sites)
    # Every item of this player's, wherever it was placed, is in the table the port gives from.
    for loc in test.multiworld.get_filled_locations():
        if loc.item.player == test.player and loc.item.code is not None:
            test.assertIn(str(loc.item.code), seed["items"], loc.item.name)
    by_word = {}
    for line in seed["patches"]:
        m = PATCH.match(line)
        test.assertIsNotNone(m, line)
        key = (int(m.group(1)), int(m.group(2), 16))
        test.assertNotIn(key, by_word, f"two patches on one word: {line}")
        by_word[key] = line
