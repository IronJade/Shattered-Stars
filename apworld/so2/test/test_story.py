"""Story awareness (story.py; docs/apworld/apworld.md "Story awareness"; docs/plans/trackers.md B6, D8).

UT's side is mirrored as test_ut.py mirrors it (TMain, updateTracker), plus its deferred-entrance
client steps (worlds/tracker/TrackerClient.py: after Connected it reads the world's
`found_entrances_datastorage_key`, formats {player} / {team}, Gets and SetNotifies them, and on each
Retrieved / SetReply calls `reconnect_found_entrances(key, value)` and updates the tracker). The
PopTracker side is the export's `story` key evaluated by the reference (trackers/tools/ap_tracker_logic.py).
For every story mix, sphere by sphere and at every story point, UT's in-logic set equals the
reference's; without a story position (or with enforce_deferred_entrances "off") it is the room's
own sphere, as before; open world and hub have no story awareness at all."""
import json
import os
import unittest

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds import AutoWorld
from worlds.generic.Rules import exclusion_rules

from .. import story
from ..data import STAGES
from ..items import GAME
from ..locations import ROW
from ..world import SO2World
from .test_tracker import REFERENCE, needs_reference
from .test_ut import MIXES, SLOW, UT_STEPS, real_generation, room_slot_data

TrackerLogic = REFERENCE.TrackerLogic if REFERENCE is not None else None   # the repo's tools/ (packaged: skip)

STORY_MIXES = ("story default", "story party", "story party rena")
# Positions: before the first chapter stored (counts as the first point), each point, a step before the
# Final Save Point, the end.
POSITIONS = [{"chapter": c, "step": s} for c, s in sorted(
    [(2, 0), (60, 3000), (60, 5000)] + [(c, s) for _, c, s in story.POINTS])]


def ut_rebuild(slot_data, seed, enforce="default"):
    """test_ut.ut_rebuild with UT's enforce_deferred_connections set first (TMain sets it from host.yaml's
    enforce_deferred_entrances: "default" unless the player changed it)."""
    mw = setup_multiworld(SO2World, (), seed)
    mw.generation_is_fake = True
    mw.re_gen_passthrough = {GAME: slot_data}
    if enforce is not None:
        mw.enforce_deferred_connections = enforce
    for step in UT_STEPS:
        AutoWorld.call_all(mw, step)
        if step == "set_rules":
            exclusion_rules(mw, 1, mw.worlds[1].options.exclude_locations.value)
    mw.precollected_items = {p: [i for i in items if i.code is None] for p, items in mw.precollected_items.items()}
    return mw


def ut_keys(world, team=0, slot=1):
    """The keys UT's client watches for this world (TrackerClient: the attribute, a str or a list,
    formatted with player / team)."""
    keys = getattr(world, "found_entrances_datastorage_key", None)
    if not keys:
        return []
    if isinstance(keys, str):
        keys = [keys]
    return [k.format(player=slot, team=team) for k in keys]


def ut_set_reply(world, key, value):
    """UT's client on a Retrieved / SetReply of a watched key."""
    world.reconnect_found_entrances(key, value)


def ut_in_logic(mw, received):
    """UT's updateTracker: a fresh state, the received items, the events swept; every reachable location."""
    world = mw.worlds[1]
    state = CollectionState(mw, True)
    for name, flags in received:
        item = world.create_item(name)
        item.classification |= flags
        state.collect(item, True)
    state.sweep_for_advancements(locations=[loc for loc in mw.get_locations(1) if not loc.address])
    return {loc.address for loc in mw.get_reachable_locations(state, 1) if loc.address is not None}


def counts_of(world, received):
    out = {}
    for name, _ in received:
        code = world.item_name_to_id[name]
        out[code] = out.get(code, 0) + 1
    return out


def sphere_items(real):
    """The received items before each sphere of the room's own playthrough (the first: the start)."""
    received = [(i.name, i.classification) for i in real.precollected_items[1] if i.code is not None]
    out = [list(received)]
    for sphere in real.get_sendable_spheres():
        got = [(loc.item.name, loc.item.classification) for loc in sphere
               if loc.address is not None and loc.item.player == 1 and loc.item.code is not None]
        if not any(loc.address is not None for loc in sphere):
            break
        received += got
        out.append(list(received))
    return out


class TestStoryData(unittest.TestCase):
    def test_points(self):
        """The 23 stages' first chapters (data.STAGES), in order, then the Final Save Point."""
        self.assertEqual(len(story.POINTS), len(STAGES) + 1)
        self.assertEqual([c for _, c, _ in story.POINTS[:-1]], [lo for _, (lo, _), _ in STAGES])
        self.assertEqual(story.POINTS[-1][1:], (60, 4000))
        pairs = [(c, s) for _, c, s in story.POINTS]
        self.assertEqual(pairs, sorted(set(pairs)))
        # each stage starts right after the previous one ends
        for (_, (lo, hi), _), (_, (nlo, _), _) in zip(STAGES, STAGES[1:]):
            self.assertEqual(nlo, hi + 1)

    def test_reached(self):
        self.assertIsNone(story.reached(None))
        for bad in ("12", [12], {"step": 1}, {"chapter": "12"}, {"chapter": True}, 7, {"chapter": 0},
                    {"chapter": -1, "step": 5}):
            self.assertIsNone(story.reached(bad), bad)
        self.assertEqual(story.reached({"chapter": 1, "step": 0}), 1)     # before the first: the start
        self.assertEqual(story.reached({"chapter": 3, "step": 0}), 1)
        self.assertEqual(story.reached({"chapter": 4, "step": 2000}), 1)     # Rena's game starts at 4
        self.assertEqual(story.reached({"chapter": 5}), 2)                    # no step: 0
        self.assertEqual(story.reached({"chapter": 35, "step": 100}), 12)     # the disc switch's chapter
        self.assertEqual(story.reached({"chapter": 36, "step": 0}), 13)       # on Energy Nede
        self.assertEqual(story.reached({"chapter": 60, "step": 3999}), 23)
        self.assertEqual(story.reached({"chapter": 60, "step": 4000}), 24)
        self.assertEqual(story.reached({"chapter": 60.0, "step": 4000, "extra": 1}), 24)

    def test_location_stages(self):
        """Every story-mode location has a point; Energy Nede and Fienal come after the disc switch,
        Expel before it or after the Final Save Point (Virtual Expel), the Cave of Trials after it."""
        mw = setup_multiworld(SO2World, ("generate_early", "create_regions", "create_items", "set_rules"), 3,
                              dict(MIXES["story party"], shuffle_shops="full", enemy_dropsanity="first_drop",
                                   cave_of_trials=True, goal=2, missable_locations="include"))
        events, stages = story.overlay(mw.worlds[1])
        self.assertEqual(events, {"Arrived on Energy Nede": 13, "Saved at the Final Save Point": 24})
        for loc in mw.get_locations(1):
            if loc.address is None:
                continue
            k, row = stages[loc.address], ROW[loc.name]
            with self.subTest(loc.name):
                self.assertIn(k, range(1, 25))
                if row["region"] == "Cave of Trials":
                    self.assertEqual(k, 24)
                elif row["region"] in ("Energy Nede", "Fienal"):
                    self.assertGreaterEqual(k, 13)
                elif row["region"] == "Expel" and row["type"] not in ("drop", "guild", "learn", "killer_move"):
                    self.assertTrue(k <= 12 or k == 24, k)
                if row["type"] == "boss" and row.get("boss") == "story" and row["region"] != "Cave of Trials":
                    lo, hi = STAGES[k - 1][1]
                    self.assertTrue(lo <= row["chapter"] <= hi)
        # a gift of a later chapter in its area's stage, the King's Room (behind an Expel entrance)
        by_name = {loc.name: stages[loc.address] for loc in mw.get_locations(1) if loc.address is not None}
        self.assertEqual(by_name["Town of Lacour: Sinclair Sabre gift"], 24)      # chapter 60, on Expel
        self.assertEqual(by_name["King's Room: 10000 Fol chest"], 24)
        self.assertEqual(by_name["Town of Salva: Shrimp Shu-mai gift"], 2)       # Salva's stage

    def test_no_story_outside_story_mode(self):
        for label in ("open full", "hub full"):
            mw = setup_multiworld(SO2World, ("generate_early", "create_regions", "create_items", "set_rules"), 3,
                                  MIXES[label])
            with self.subTest(label):
                self.assertIsNone(story.overlay(mw.worlds[1]))
                self.assertIsNone(story.export(mw.worlds[1]))
                self.assertNotIn("story", mw.worlds[1].fill_slot_data()["tracker"])


class TestStoryExport(unittest.TestCase):
    def test_shape(self):
        mw = setup_multiworld(SO2World, ("generate_early", "create_regions", "create_items", "set_rules"), 3,
                              MIXES["story party"])
        tracker = json.loads(json.dumps(mw.worlds[1].fill_slot_data()))["tracker"]
        st = tracker["story"]
        self.assertEqual(set(st), {"key", "stages", "events", "locations"})
        self.assertEqual(st["key"], "so2_story_{team}_{slot}")
        self.assertEqual([tuple(p) for p in st["stages"]], [tuple(p) for p in story.POINTS])
        self.assertEqual(st["events"], [["Arrived on Energy Nede", 13], ["Saved at the Final Save Point", 24]])
        exported = {i for g in tracker["locations"] for i in g[2:]}
        listed = [i for g in st["locations"] for i in g[1:]]
        self.assertEqual(len(listed), len(set(listed)))
        self.assertLessEqual(set(listed), exported)
        self.assertTrue(all(2 <= g[0] <= 24 for g in st["locations"]))
        self.assertEqual([g[0] for g in st["locations"]], sorted({g[0] for g in st["locations"]}))
        events = {e[0] for e in tracker["events"]}
        self.assertLessEqual({e[0] for e in st["events"]}, events)

    def test_real_generation_has_no_story_events(self):
        mw = setup_multiworld(SO2World, ("generate_early", "create_regions", "create_items", "set_rules"), 3,
                              MIXES["story default"])
        world = mw.worlds[1]
        self.assertFalse(any(loc.name.startswith("Story reached") for loc in mw.get_locations(1)))
        self.assertIsNone(getattr(world, "found_entrances_datastorage_key", None))
        mw.enforce_deferred_connections = "on"      # without re_gen_passthrough it is no UT regeneration
        story.prepare(world)
        self.assertFalse(any(loc.name.startswith("Story reached") for loc in mw.get_locations(1)))


class StoryChecks:
    """Mixed into one class per story mix: the room generated and filled, rebuilt in UT's way."""
    mix = None
    room_seed = 11
    ut_seed = 1011

    @classmethod
    def setUpClass(cls):
        cls.real = real_generation(MIXES[cls.mix], cls.room_seed)
        cls.room = room_slot_data(cls.real.worlds[1])
        cls.ut = ut_rebuild(cls.room, cls.ut_seed)
        cls.stages = sphere_items(cls.real)

    def test_ut_watches_the_ports_key(self):
        world = self.ut.worlds[1]
        self.assertEqual(ut_keys(world, team=0, slot=1), ["so2_story_0_1"])
        self.assertEqual(ut_keys(world, team=2, slot=7), ["so2_story_2_7"])
        self.assertEqual(sum(loc.name.startswith("Story reached: ") for loc in self.ut.get_locations(1)), 23)

    def test_no_position_is_the_room(self):
        """Before the port reports (or with a value that says nothing) UT lists what the room's own
        logic does, sphere by sphere."""
        world = self.ut.worlds[1]
        key = ut_keys(world)[0]
        plain = [ut_in_logic(self.ut, received) for received in self.stages]
        for value in (None, {"step": 3}, "chapter 12"):
            ut_set_reply(world, key, value)
            for k, received in enumerate(self.stages):
                self.assertEqual(ut_in_logic(self.ut, received), plain[k], f"{value!r}, sphere {k}")
        off = ut_rebuild(self.room, self.ut_seed, enforce="off")
        self.assertEqual(ut_keys(off.worlds[1]), [])
        self.assertFalse(any(loc.name.startswith("Story reached") for loc in off.get_locations(1)))
        for k, received in enumerate(self.stages):
            self.assertEqual(ut_in_logic(off, received), plain[k], f"off, sphere {k}")
        # and that is the room's own logic: the world's reachable set for those items
        real_state = CollectionState(self.real)
        for name, flags in self.stages[0]:
            item = self.real.worlds[1].create_item(name)
            item.classification |= flags
            real_state.collect(item, True)
        real_state.sweep_for_advancements(locations=[l for l in self.real.get_locations(1) if not l.address])
        self.assertEqual(plain[0], {l.address for l in self.real.get_reachable_locations(real_state, 1)
                                    if l.address is not None})

    @needs_reference
    def test_story_follows_the_chapter(self):
        """At every story point, sphere by sphere (the first, a middle one, the last): UT's in-logic set
        equals the reference's from the export's `story` key; it only grows with the story; it is the
        plain set less what is later in the story; going back shuts the later points again."""
        world = self.ut.worlds[1]
        key = ut_keys(world)[0]
        ref = TrackerLogic(self.room["tracker"])
        picks = sorted({0, len(self.stages) // 2, len(self.stages) - 1})
        for k in picks:
            received = self.stages[k]
            counts = counts_of(self.real.worlds[1], received)
            ut_set_reply(world, key, None)
            plain = ut_in_logic(self.ut, received)
            before = set()
            for value in POSITIONS:
                ut_set_reply(world, key, value)
                got = ut_in_logic(self.ut, received)
                with self.subTest(sphere=k, story=value):
                    self.assertLessEqual(before, got)
                    self.assertLessEqual(got, plain)
                    view = ref.evaluate(counts, value)
                    self.assertEqual(got, view.locations)
                    self.assertEqual(view.later, plain - got)
                    self.assertEqual(view.stage, story.reached(value))
                before = got
            self.assertEqual(before, plain, "at the end of the story everything the items allow")
            ut_set_reply(world, key, {"chapter": 3, "step": 0})
            start = ut_in_logic(self.ut, received)
            self.assertLess(len(start), len(plain))
            ut_set_reply(world, key, {"chapter": 40, "step": 0})
            ut_set_reply(world, key, {"chapter": 3, "step": 0})
            self.assertEqual(ut_in_logic(self.ut, received), start, "back to chapter 3")

    def test_stage_by_stage(self):
        """With every item: at each point only locations of points reached are listed; Expel's alone
        before the disc switch; Energy Nede's come at chapter 36 (not at 35, the disc switch's own)."""
        world = self.ut.worlds[1]
        key = ut_keys(world)[0]
        received = self.stages[-1]
        _, stages = story.overlay(self.real.worlds[1])
        region = {loc.address: loc.parent_region.name for loc in self.ut.get_locations(1) if loc.address}
        sizes = []
        for value in POSITIONS[1:]:
            ut_set_reply(world, key, value)
            got = ut_in_logic(self.ut, received)
            reached = story.reached(value)
            sizes.append(len(got))
            with self.subTest(story=value):
                self.assertTrue(all(stages[a] <= reached for a in got))
                if reached < 13:
                    self.assertEqual({region[a] for a in got}, {"Expel"})
                if reached < 24:
                    self.assertNotIn("Cave of Trials", {region[a] for a in got})
        self.assertGreater(sizes[0], 0)
        ut_set_reply(world, key, {"chapter": 36, "step": 0})
        nede = ut_in_logic(self.ut, received)
        ut_set_reply(world, key, {"chapter": 35, "step": 9999})
        expel = ut_in_logic(self.ut, received)
        self.assertIn("Energy Nede", {region[a] for a in nede - expel})


class TestStoryDefault(StoryChecks, unittest.TestCase):
    mix = "story default"


class TestStoryParty(StoryChecks, unittest.TestCase):
    mix = "story party"


class TestStoryPartyRena(StoryChecks, unittest.TestCase):
    mix = "story party rena"


@needs_reference
@unittest.skipUnless(SLOW, "SO2_SLOW_TESTS=1 (the release workflow)")
class TestStoryManySeeds(unittest.TestCase):
    """20 rooms per story mix (seeds 1-20): UT's in-logic set equals the reference's at every story
    position, with the start's items and with every item."""
    def test_many_seeds(self):
        for mix in STORY_MIXES:
            for seed in range(1, 21):
                with self.subTest(mix=mix, seed=seed):
                    real = real_generation(MIXES[mix], seed)
                    room = room_slot_data(real.worlds[1])
                    ut = ut_rebuild(room, seed + 1000)
                    world = ut.worlds[1]
                    key = ut_keys(world)[0]
                    ref = TrackerLogic(room["tracker"])
                    stages = sphere_items(real)
                    for received in (stages[0], stages[-1]):
                        counts = counts_of(real.worlds[1], received)
                        for value in POSITIONS:
                            ut_set_reply(world, key, value)
                            self.assertEqual(ut_in_logic(ut, received), ref.evaluate(counts, value).locations,
                                             f"{value}")


class TestStoryNotElsewhere(unittest.TestCase):
    """Open world and hub mode: UT watches no story key and builds no story events, whatever
    enforce_deferred_entrances says; the export has no `story` key."""
    def test_open_and_hub(self):
        for label in ("open full", "hub full"):
            real = real_generation(MIXES[label], 11)
            room = room_slot_data(real.worlds[1])
            self.assertNotIn("story", room["tracker"])
            for enforce in ("default", "on"):
                mw = ut_rebuild(room, 1011, enforce)
                with self.subTest(label, enforce=enforce):
                    self.assertEqual(ut_keys(mw.worlds[1]), [])
                    self.assertFalse(any(loc.name.startswith("Story reached") for loc in mw.get_locations(1)))


if __name__ == "__main__":
    unittest.main()
