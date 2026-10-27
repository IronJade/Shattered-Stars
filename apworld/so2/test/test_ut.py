"""Universal Tracker (ut.py; docs/ap/apworld.md "Trackers"; docs/plans/trackers.md B1).

UT rebuilds this world from the room's slot data alone (yaml-less) with a new random seed, and lists
what our rules say is reachable and not yet checked. ut_rebuild and in_logic mirror UT's TMain and
updateTracker (worlds/tracker/TrackerCore.py) without UT installed; UTChecks is UT's fuzzer hook's
check (worlds/tracker/fuzzer_hook.py): for every sphere of the real, filled generation, UT's
in-logic locations are exactly that sphere's. One class per option mix (MIXES): the three world
modes and the mixes that shape the party (the pool's random draw without the bench, the healer
guarantee, the starting character, the story's party rules), bench on and off; two UT seeds each.
Many seeds per mix: SO2_SLOW_TESTS=1 (TestUTManySeeds; the release workflow).

The other classes: the refusals (D10: UT's TrackerException, or ut.TrackerRefusal without UT), the
coverage of the options and the slot data's keys, and the tracker tab's order (custom_ut_sort)."""
import inspect
import json
import os
import re
import sys
import types
import unittest
from unittest import mock

from BaseClasses import CollectionState, LocationProgressType
from test.general import setup_multiworld
from worlds import AutoWorld
from worlds.generic.Rules import exclusion_rules

from .. import ut
from ..items import GAME
from ..locations import ALL_ROWS
from ..options import SO2Options
from ..world import SO2World
from .bases import SO2TestBase, fill

UT_STEPS = ("generate_early", "create_regions", "create_items", "set_rules", "connect_entrances", "generate_basic")
SLOW = bool(os.environ.get("SO2_SLOW_TESTS"))
SLOW_SEEDS = range(1, 21)

MIXES = {
    "story default": {},
    "story party": {"character_items": True, "craftsanity": True, "killer_move_checks": True,
                    "killer_move_progression": "any", "skillsanity": "insanity"},
    "story party rena": {"character_items": True, "story_route": "rena", "missable_locations": "include",
                         "starting_character": "noel"},     # ignored in story mode: slot data None
    "open full": {"world_mode": "open_world", "character_items": True, "craftsanity": True,
                  "killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "insanity",
                  "shuffle_shops": "full", "enemy_dropsanity": "first_drop", "goal": 2, "cave_of_trials": True,
                  "story_route": "rena", "expel_teleporter": False},
    "open full no bench": {"world_mode": "open_world", "character_items": True, "bench": False,
                           "craftsanity": True, "killer_move_checks": True, "killer_move_progression": "any",
                           "skillsanity": "special_only", "shuffle_shops": "specials",
                           "enemy_dropsanity": "first_drop", "drop_progression": "force", "goal": 1},
    "open start any": {"world_mode": "open_world", "character_items": True, "starting_character": "any",
                       "killer_move_checks": True, "killer_move_progression": "any", "craftsanity": True},
    "open start any no bench": {"world_mode": "open_world", "character_items": True, "bench": False,
                                "starting_character": "any", "killer_move_checks": True,
                                "killer_move_progression": "any", "craftsanity": True},
    "open start noel": {"world_mode": "open_world", "character_items": True, "bench": False,
                        "starting_character": "noel", "killer_move_checks": True, "killer_move_progression": "hero",
                        "craftsanity": True, "exclude_characters": ["Leon", "Dias"]},
    "open healer": {"world_mode": "open_world", "character_items": True, "bench": False, "story_route": "claude",
                    "exclude_characters": ["Noel", "Celine", "Bowman"], "skillsanity": "skills_only"},
    "open no teleporter": {"world_mode": "open_world", "expel_teleporter": False, "expel_progression": False,
                           "boss_checks": False, "area_keys_in_drops": False, "enemy_dropsanity": "first_drop"},
    "open hell": {"world_mode": "open_world", "character_items": True, "encounter_rate": 0,
                  "killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "special_only"},
    "hub full": {"world_mode": "hub", "hub_start": "random_town", "fienal_requirement": "all_bosses",
                 "craftsanity": True, "killer_move_checks": True, "killer_move_progression": "any",
                 "skillsanity": "insanity", "enemy_dropsanity": "first_drop", "goal": 2, "cave_of_trials": True},
    "hub no bench": {"world_mode": "hub", "bench": False, "hub_start": "random_town", "story_route": "rena",
                     "killer_move_checks": True, "killer_move_progression": "any", "craftsanity": True},
    "hub fields claude": {"world_mode": "hub", "fienal_requirement": "fields", "story_route": "claude",
                          "starting_character": "chisato", "killer_move_checks": True,
                          "killer_move_progression": "any", "craftsanity": True},
    # A player's own hub YAML (2026-10-27, written for 0.2.0: no `bench`, so the default, on).
    "hub rich": {"world_mode": "hub", "hub_start": "arlia", "fienal_requirement": "all_bosses",
                 "goal": "indalecio_limiter_off_and_iseria_queen", "story_route": "claude", "shuffle_shops": "full",
                 "cave_of_trials": True, "enemy_dropsanity": "first_drop", "craftsanity": True,
                 "killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "insanity",
                 "starting_character": "opera", "character_items": True, "traps": 50, "difficulty": "universe",
                 "death_link": True},
    "excluded locations": {"world_mode": "open_world", "character_items": True,
                           "exclude_locations": ["Town of Salva: Heavy Ring chest", "Hoffmann Ruins: Cure Stone chest",
                                                 "Town of Cross: Music Box gift (map 75)",
                                                 "Town of Salva: shop 2 Long Sword"]},   # (no shop slots here)
}


def room_slot_data(world):
    """The slot data as UT receives it: fill_slot_data through JSON (the server sends it so)."""
    return json.loads(json.dumps(world.fill_slot_data()))


def ut_rebuild(slot_data, seed):
    """UT's TMain, yaml-less: one player with the default options (UT writes an empty YAML),
    generation_is_fake, the slot data as re_gen_passthrough, a new seed; generate_early ..
    generate_basic with exclusion_rules after set_rules; then the precollected items that have a
    code dropped (the server sends them as received items)."""
    mw = setup_multiworld(SO2World, (), seed)
    mw.generation_is_fake = True
    mw.re_gen_passthrough = {GAME: slot_data}
    for step in UT_STEPS:
        AutoWorld.call_all(mw, step)
        if step == "set_rules":
            exclusion_rules(mw, 1, mw.worlds[1].options.exclude_locations.value)
    mw.precollected_items = {p: [i for i in items if i.code is None] for p, items in mw.precollected_items.items()}
    return mw


def in_logic(mw, received, missing):
    """UT's updateTracker: the received items (made by the world's create_item, the server's flags
    or'd in) collected, the events swept; the reachable locations still missing."""
    world = mw.worlds[1]
    state = CollectionState(mw)
    for name, flags in received:
        item = world.create_item(name)
        item.classification |= flags
        state.collect(item, True)
    state.sweep_for_advancements(locations=[loc for loc in mw.get_locations(1) if not loc.address])
    return {loc.address for loc in mw.get_reachable_locations(state, 1) if loc.address in missing}


def ids(mw, kind=None):
    return {loc.address for loc in mw.get_locations(1)
            if loc.address is not None and (kind is None or loc.progress_type == kind)}


def real_generation(options, seed):
    """Generate's way (Main): the steps, exclusion_rules after set_rules, then the fill."""
    mw = setup_multiworld(SO2World, ("generate_early", "create_regions", "create_items", "set_rules"), seed, options)
    exclusion_rules(mw, 1, mw.worlds[1].options.exclude_locations.value)
    for step in ("connect_entrances", "generate_basic", "pre_fill"):
        AutoWorld.call_all(mw, step)
    from Fill import distribute_items_restrictive
    distribute_items_restrictive(mw)
    return mw


def check_room(test, real, ut_seed):
    """The rebuilt world is the room's: the same party, locations, excluded locations and slot data,
    and sphere by sphere (UT's fuzzer hook) UT's in-logic locations are the server's sphere."""
    room = room_slot_data(real.worlds[1])
    tracker = ut_rebuild(room, ut_seed)
    w, t = real.worlds[1], tracker.worlds[1]
    test.assertIsNotNone(t.ut_slot_data)
    test.assertEqual(t.party, w.party)
    test.assertEqual(ids(tracker), ids(real))
    test.assertEqual(ids(tracker, LocationProgressType.EXCLUDED), ids(real, LocationProgressType.EXCLUDED))
    test.assertEqual(room_slot_data(t), room)
    missing = ids(real)
    received = [(i.name, i.classification) for i in real.precollected_items[1] if i.code is not None]
    spheres = 0
    for sphere in real.get_sendable_spheres():
        current = {loc.address for loc in sphere if loc.address is not None}
        if not current:
            break
        spheres += 1
        test.assertEqual(in_logic(tracker, received, missing), current, f"sphere {spheres}")
        missing -= current
        received += [(loc.item.name, loc.item.classification) for loc in sphere
                     if loc.address is not None and loc.item.player == 1 and loc.item.code is not None]
    test.assertEqual(in_logic(tracker, received, missing), set(), "after the last sphere")
    test.assertGreater(spheres, 0)


class UTChecks:
    """Mixed into one SO2TestBase class per mix: the room (this class's world, generated with a
    fixed seed and filled) rebuilt in UT's way with other seeds."""
    room_seed = 11
    ut_seeds = (1011, 1012)

    def world_setup(self, seed=None):
        super().world_setup(self.room_seed if seed is None else seed)

    def test_ut_rebuilds_the_room(self):
        exclusion_rules(self.multiworld, 1, self.world.options.exclude_locations.value)    # as Main does
        fill(self)
        for ut_seed in self.ut_seeds:
            with self.subTest(room_seed=self.room_seed, ut_seed=ut_seed):
                check_room(self, self.multiworld, ut_seed)


class TestUTStoryDefault(UTChecks, SO2TestBase):
    options = MIXES["story default"]


class TestUTStoryParty(UTChecks, SO2TestBase):
    """The story's party rules: character_items' pool is a random party the story allows."""
    options = MIXES["story party"]


class TestUTStoryPartyRena(UTChecks, SO2TestBase):
    options = MIXES["story party rena"]


class TestUTOpenFull(UTChecks, SO2TestBase):
    options = MIXES["open full"]


class TestUTOpenFullNoBench(UTChecks, SO2TestBase):
    """Without the bench the pool is a random draw of 8: only the slot data can say which."""
    options = MIXES["open full no bench"]


class TestUTOpenStartAny(UTChecks, SO2TestBase):
    options = MIXES["open start any"]


class TestUTOpenStartAnyNoBench(UTChecks, SO2TestBase):
    options = MIXES["open start any no bench"]


class TestUTOpenStartNoel(UTChecks, SO2TestBase):
    options = MIXES["open start noel"]


class TestUTOpenHealer(UTChecks, SO2TestBase):
    """The healer guarantee's random swap (no bench, Noel excluded)."""
    options = MIXES["open healer"]


class TestUTOpenNoTeleporter(UTChecks, SO2TestBase):
    options = MIXES["open no teleporter"]


class TestUTOpenHell(UTChecks, SO2TestBase):
    options = MIXES["open hell"]


class TestUTHubFull(UTChecks, SO2TestBase):
    """random_town: the start town comes back from `hub_start_town`."""
    options = MIXES["hub full"]


class TestUTHubNoBench(UTChecks, SO2TestBase):
    options = MIXES["hub no bench"]


class TestUTHubFieldsClaude(UTChecks, SO2TestBase):
    options = MIXES["hub fields claude"]


class TestUTHubRich(UTChecks, SO2TestBase):
    """A player's own rich hub YAML: every check option, a named start, the bench by default."""
    options = MIXES["hub rich"]


class TestUTExcludedLocations(UTChecks, SO2TestBase):
    """exclude_locations comes back: UT shows the same locations excluded. (Generate expands a
    location group to its names before fill_slot_data; a test's options are not verified, so names.)"""
    options = MIXES["excluded locations"]

    def test_slot_data(self):
        self.assertEqual(self.world.fill_slot_data()["exclude_locations"], sorted(self.options["exclude_locations"]))


@unittest.skipUnless(SLOW, "many seeds per mix: SO2_SLOW_TESTS=1 (the release workflow)")
class TestUTManySeeds(unittest.TestCase):
    def test_many_seeds(self):
        for label, options in MIXES.items():
            for seed in SLOW_SEEDS:
                with self.subTest(mix=label, seed=seed):
                    check_room(self, real_generation(options, seed), seed + 1000)


class TestUTRoom(SO2TestBase):
    """What a room's slot data says and how the world reads it back: the hooks UT looks for,
    0.2.2's keys (in the slot data, not in the .apso2), older rooms, and every refusal (D10)."""
    options = MIXES["open start any no bench"]
    run_default_tests = False

    @property
    def room(self):
        return room_slot_data(self.world)

    def test_hooks(self):
        self.assertIs(SO2World.ut_can_gen_without_yaml, True)
        self.assertIsInstance(inspect.getattr_static(SO2World, "interpret_slot_data"), staticmethod)
        room = self.room
        self.assertIs(SO2World.interpret_slot_data(room), room)
        self.assertIsNone(self.world.ut_slot_data)

    def test_new_keys(self):
        self.assertEqual(self.room["logic_version"], ut.LOGIC_VERSION)
        self.assertEqual(self.room["exclude_locations"], [])
        fill(self)
        options = self.world.seed_data()["options"]
        for key in ut.NEW_KEYS:
            self.assertNotIn(key, options)
        self.assertEqual(set(self.room) - set(options), set(ut.NEW_KEYS))

    def test_other_game(self):
        """A passthrough for another game is no rebuild of ours."""
        mw = setup_multiworld(SO2World, (), 5)
        mw.re_gen_passthrough = {"Some Other Game": self.room}
        AutoWorld.call_all(mw, "generate_early")
        self.assertIsNone(mw.worlds[1].ut_slot_data)

    def test_older_rooms(self):
        """0.2.0 / 0.2.1 rooms: no logic_version, no exclude_locations (version 1, nothing excluded)."""
        for version in ("0.2.0", "0.2.1"):
            with self.subTest(apworld_version=version):
                old = {k: v for k, v in self.room.items() if k not in ut.NEW_KEYS}
                old["apworld_version"] = version
                t = ut_rebuild(old, 77).worlds[1]
                self.assertEqual(t.party, self.world.party)
                self.assertEqual({k: v for k, v in room_slot_data(t).items() if k != "apworld_version"},
                                 {k: v for k, v in self.room.items() if k != "apworld_version"})

    def test_older_skillsanity_rooms(self):
        """0.2.1 changed Skillsanity's logic (its guild checks and learn levels, stricter than 0.2.0's)
        and made encounter_rate 0 with killer-move checks a hell seed: a 0.2.0 room with Skillsanity
        is tracked with a logged warning (the user, 2026-10-27: "accept with a warning"; every 0.2.0
        room measured is conservative: UT lists a subset of the room's own at every sphere), a 0.2.0
        hell seed is refused, a 0.2.1 room is 0.2.2's logic (no warning)."""
        for value in ("skills_only", "special_only", "insanity"):
            mw = setup_multiworld(SO2World, UT_STEPS, 9, {"skillsanity": value, "world_mode": "hub"})
            room = {k: v for k, v in room_slot_data(mw.worlds[1]).items() if k not in ut.NEW_KEYS}
            with self.subTest(skillsanity=value, apworld_version="0.2.1"):
                with self.assertNoLogs(level="WARNING"):
                    self.assertEqual(ut_rebuild(dict(room, apworld_version="0.2.1"), 77).worlds[1].party,
                                     mw.worlds[1].party)
            with self.subTest(skillsanity=value, apworld_version="0.2.0"):
                with self.assertLogs(level="WARNING") as logged:
                    t = ut_rebuild(dict(room, apworld_version="0.2.0"), 77).worlds[1]
                self.assertEqual(t.party, mw.worlds[1].party)
                self.assertEqual(ids(t.multiworld), ids(mw))
                text = " ".join(logged.output)
                for wanted in ("so2.apworld 0.2.0", "Skillsanity", "guild checks and learn levels", "later",
                               "Regenerate", "0.2.2"):
                    self.assertIn(wanted, text)
        none = dict(room, skillsanity="none", apworld_version="0.2.0")
        with self.subTest("0.2.0 without skillsanity"), self.assertNoLogs(level="WARNING"):
            ut_rebuild(none, 77)
        hell = dict(room, skillsanity="none", encounter_rate=0, killer_move_checks=True, apworld_version="0.2.0")
        self.refused(hell, "hell seed", "YAML")
        self.refused(dict(hell, skillsanity="insanity"), "hell seed", "YAML")
        no_version = {k: v for k, v in self.room.items() if k not in ("apworld_version", "logic_version")}
        self.assertIn("older than 0.2.0", self.refused(dict(no_version, party_rules="story")))

    def refused(self, data, *wanted):
        with self.assertRaises(ut.TrackerRefusal) as caught:
            ut_rebuild(data, 77)
        for text in ("so2.apworld", "can't track this room", *wanted):
            self.assertIn(text, caught.exception.message)
        return caught.exception.message

    def test_refusals(self):
        r = self.room
        cases = {
            "newer logic": (dict(r, logic_version=2, apworld_version="0.9.0"), "version 2", "0.9.0 or later"),
            "unknown logic": (dict(r, logic_version="x"), "'x'"),
            "missing option": ({k: v for k, v in r.items() if k != "skillsanity"}, "'skillsanity'"),
            "unknown value": (dict(r, goal="win_fast"), "'goal'"),
            "unknown member": (dict(r, starting_character="Ratix"), "'starting_character'"),
            "bad exclusion": (dict(r, exclude_characters=["Nobody"]), "'exclude_characters'"),
            "unknown location": (dict(r, exclude_locations=["Nowhere: chest 1"]), "'exclude_locations'"),
            "out of range": (dict(r, traps=101), "'traps'"),
            "no party": ({k: v for k, v in r.items() if k != "characters"}, "'characters'"),
            "hub without town": (dict(r, world_mode="hub", hub_start="fun_city", fienal_requirement="fields"),
                                 "hub start town"),
            "hub without its options": (dict(r, world_mode="hub", hub_start_town="fun_city"), "'hub_start'"),
            "derived differs": (dict(r, party_rules="story"), "party_rules"),
            "a key this world doesn't write": (dict(r, hell_seed=True), "hell_seed"),
            "seed format": (dict(r, seed_format=9), "seed_format"),
        }
        for label, (data, *wanted) in cases.items():
            with self.subTest(label):
                self.refused(data, *wanted)
        older = self.refused(dict(r, party_rules="story", apworld_version="0.2.0"), "0.2.0")
        self.assertNotIn("or later", older)

    def test_trackers_exception(self):
        """With UT installed the refusal is its TrackerException (keyword `message`)."""
        class TrackerException(Exception):
            def __init__(self, *args, message):
                self.message = message
                super().__init__(*args)
        fake = types.ModuleType("worlds.tracker")
        fake.TrackerException = TrackerException
        with mock.patch.dict(sys.modules, {"worlds.tracker": fake}):
            with self.assertRaises(TrackerException) as caught:
                ut_rebuild(dict(self.room, logic_version=2), 77)
        self.assertIn("so2.apworld", caught.exception.message)


# The slot data's keys that are no option: the random choices restored (ut.py: choose_open_party /
# choose_party, hub.choose_start) and the values check_restored compares.
RESTORED_CHOICES = {"characters", "hub_start_town"}
DERIVED = {"seed_format", "goal_encounters", "party_rules", "apworld_version", "hell_seed", "fienal_won",
           "dungeon_keys"}


class TestUTCoverage(unittest.TestCase):
    """Every option is restored from the slot data or has no effect on what UT builds; every slot
    data key is an option, a restored choice, derived (compared) or 0.2.2's."""

    def test_every_option(self):
        options = set(SO2Options.type_hints)
        restored = {name for name, _, _ in ut.SLOT_OPTIONS.values()}
        self.assertEqual(restored & set(ut.NO_EFFECT), set())
        self.assertEqual(restored | set(ut.NO_EFFECT), options)

    def test_no_effect_is_never_read(self):
        """The world's code reads none of NO_EFFECT's options (so they can't shape what UT builds)."""
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        pattern = re.compile(r"\.(" + "|".join(ut.NO_EFFECT) + r")\b")
        for name in sorted(os.listdir(here)):
            if name.endswith(".py") and name != "ut.py":
                with open(os.path.join(here, name), encoding="utf-8") as f:
                    for n, line in enumerate(f, 1):
                        self.assertIsNone(pattern.search(line), f"{name}:{n}: {line.strip()}")

    def test_every_key(self):
        known = set(ut.SLOT_OPTIONS) | RESTORED_CHOICES | DERIVED | set(ut.NEW_KEYS)
        for label in ("story default", "open hell", "hub full", "open full"):
            with self.subTest(label):
                mw = setup_multiworld(SO2World, UT_STEPS, 3, MIXES[label])
                self.assertEqual(set(mw.worlds[1].fill_slot_data()) - known, set())


class TestUTSort(unittest.TestCase):
    """custom_ut_sort: (story stage of the area, area, name)."""

    def test_every_location(self):
        for row in ALL_ROWS:
            key = ut.sort_key("Expel", row["name"])
            self.assertEqual(key[1:], (row["area"], row["name"]))
            self.assertIsInstance(key[0], (int, float))

    def test_story_order(self):
        order = ["Arlia Village: Leather Armor chest", "Town of Salva: Heavy Ring chest",
                 "Town of Cross: Music Box gift (map 75)",
                 "Cross Cave: Guardians boss", "Mars Village: Silk Robe chest", "Hoffmann Ruins: Cure Stone chest",
                 "Eluria Tower: X Box chest", "Mountain Palace: Cure Stone chest", "North City: Holy Rod chest",
                 "Central City: Israfil's Tear gift", "Fun City: Shigeo boss", "Giveaway: Fairy Glass chest",
                 "Cave of Trials Level 1: Fruit Nectar chest"]
        self.assertEqual(sorted(order, key=lambda n: ut.sort_key("", n)), order)
        board = [row["name"] for row in ALL_ROWS if row["type"] in ("craft", "killer_move")]
        self.assertTrue(all(ut.sort_key("", b) > ut.sort_key("", order[-1]) for b in board))

    def test_other_lines(self):
        lines = [("Expel", "Arlia Village: Leather Armor chest"), ("Menu", "New Game"), ("", ""),
                 ("Expel", "Town of Salva: Heavy Ring chest")]
        keys = sorted(lines, key=lambda line: SO2World.custom_ut_sort(None, *line))
        self.assertEqual(keys[-2:], [("", ""), ("Menu", "New Game")])
