"""The tracker export (tracker_export.py; docs/ap/seed_format.md "The tracker export"; docs/plans/trackers.md
B3) and its reference evaluator (tools/ap_tracker_logic.py, the algorithm of the PopTracker pack's Lua).

The slot data's `tracker` key is this world's resolved logic as data. For every option mix of test_ut.MIXES
(one class each: the room, generated with a fixed seed and filled), the reference evaluator, reading the
export as a tracker receives it (through JSON), shows what the world's own logic shows: sphere by sphere of
the real generation (the items found so far, the precollected ones too, as the server sends them), and for
random item states, the same reached regions, events, locations in logic and goal as a CollectionState with
those items and the events swept. Many seeds per mix: SO2_SLOW_TESTS=1 (TestTrackerManySeeds). Also: the
export's shape and round trip (each spot's body, read back as Rule Builder rules, resolves to the spot's own
rule), the evaluator's rule kinds on a hand-made export (the "region" kind Phase C will need too), and the
size at the richest mixes (at most 64 KB of JSON).

The evaluator is a repo tool: from a packaged .apworld (no tools/ beside it) these tests skip."""
import collections
import importlib.util
import json
import os
import random
import unittest

from BaseClasses import CollectionState
from rule_builder.rules import And, False_, Has, HasAll, HasAny, Or, True_
from test.general import setup_multiworld

from .. import logic, tracker_export
from ..world import SO2World
from .bases import ROOT, SO2TestBase, fill
from .test_ut import MIXES, SLOW, SLOW_SEEDS, UT_STEPS, real_generation, room_slot_data

TOOL = os.path.join(ROOT, "tools", "ap_tracker_logic.py")
SIZE_LIMIT = 64 * 1024      # bytes of JSON (compact, as the server sends slot data)
RICH = {"character_items": True, "craftsanity": True, "killer_move_checks": True, "killer_move_progression": "any",
        "skillsanity": "insanity", "shuffle_shops": "full", "enemy_dropsanity": "first_drop", "goal": 2,
        "cave_of_trials": True, "missable_locations": "include"}
RICHEST = {"story": dict(RICH), "open world": dict(RICH, world_mode="open_world"),
           "hub": dict(RICH, world_mode="hub", fienal_requirement="all_bosses")}
RANDOM_STATES = 8


def reference():
    """tools/ap_tracker_logic.py as a module, or None outside this repo."""
    if not os.path.exists(TOOL):
        return None
    spec = importlib.util.spec_from_file_location("ap_tracker_logic", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


REFERENCE = reference()
needs_reference = unittest.skipIf(REFERENCE is None, "tools/ap_tracker_logic.py is not beside this world")


def compact_size(value):
    return len(json.dumps(value, separators=(",", ":")))


def world_view(mw, received):
    """The world's own logic for these items (name, flags), as UT's updateTracker sees it: the items
    (create_item's, the flags or'd in) collected, the events swept. (regions, events, locations, goal)."""
    world = mw.worlds[1]
    saved = mw.precollected_items     # a CollectionState collects them: here they are in `received`
    mw.precollected_items = {p: [i for i in items if i.code is None] for p, items in saved.items()}
    try:
        state = CollectionState(mw)
    finally:
        mw.precollected_items = saved
    for name, flags in received:
        item = world.create_item(name)
        item.classification |= flags
        state.collect(item, True)
    events = [loc for loc in mw.get_locations(1) if loc.address is None]
    state.sweep_for_advancements(locations=events)
    state.update_reachable_regions(1)
    got = collections.Counter(loc.item.name for loc in events if loc.can_reach(state))
    locations = {loc.address for loc in mw.get_locations(1) if loc.address is not None and loc.can_reach(state)}
    return ({r.name for r in state.reachable_regions[1]}, dict(got), locations,
            bool(mw.completion_condition[1](state)))


def counts_of(world, received):
    return dict(collections.Counter(world.item_name_to_id[name] for name, _ in received))


def check_spheres(test, mw):
    """For every sphere of the filled generation and after the last: the reference evaluator over the
    room's export shows the world's own logic; its unchecked locations in logic are the sphere's."""
    world = mw.worlds[1]
    export = room_slot_data(world)["tracker"]
    evaluator = REFERENCE.TrackerLogic(export)
    test.assertEqual(set(evaluator.location_of), {loc.address for loc in mw.get_locations(1) if loc.address})
    received = [(i.name, i.classification) for i in mw.precollected_items[1] if i.code is not None]
    missing = set(evaluator.location_of)
    spheres = 0

    def compare(label, current):
        view = evaluator.evaluate(counts_of(world, received))
        test.assertEqual((view.regions, view.events, view.locations, view.goal), world_view(mw, received), label)
        test.assertEqual(view.locations & missing, current, label)

    for sphere in mw.get_sendable_spheres():
        current = {loc.address for loc in sphere if loc.address is not None}
        if not current:
            break
        spheres += 1
        compare(f"sphere {spheres}", current)
        missing -= current
        received += [(loc.item.name, loc.item.classification) for loc in sphere
                     if loc.address is not None and loc.item.player == 1 and loc.item.code is not None]
    compare("after the last sphere", set())
    test.assertGreater(spheres, 0)
    return evaluator


def check_random_states(test, mw, evaluator, seed):
    """Random item states (subsets of every item of the world's, the precollected ones too), the empty
    state and all of them: the same view."""
    world = mw.worlds[1]
    everything = [(i.name, i.classification) for i in mw.get_items() if i.player == 1 and i.code is not None]
    everything += [(i.name, i.classification) for i in mw.precollected_items[1] if i.code is not None]
    rng = random.Random(seed)
    states = [[], everything] + [[it for it in everything if rng.random() < p]
                                 for p in [rng.uniform(0.05, 0.9) for _ in range(RANDOM_STATES)]]
    for k, received in enumerate(states):
        view = evaluator.evaluate(counts_of(world, received))
        test.assertEqual((view.regions, view.events, view.locations, view.goal), world_view(mw, received),
                         f"state {k} ({len(received)} items)")


class TrackerChecks:
    """Mixed into one SO2TestBase class per mix of test_ut.MIXES: the room (this class's world, a
    fixed seed, filled)."""
    room_seed = 11
    run_default_tests = False       # test_ut's class for the same mix runs them

    def world_setup(self, seed=None):
        super().world_setup(self.room_seed if seed is None else seed)

    @needs_reference
    def test_reference_is_the_world(self):
        fill(self)
        evaluator = check_spheres(self, self.multiworld)
        check_random_states(self, self.multiworld, evaluator, self.room_seed)


def _mix_class(label):
    name = "TestTracker" + "".join(part.capitalize() for part in label.split())
    return type(name, (TrackerChecks, SO2TestBase), {"options": MIXES[label], "__doc__": f"The mix {label!r}."})


for _label in MIXES:
    _cls = _mix_class(_label)
    globals()[_cls.__name__] = _cls
del _label, _cls


@unittest.skipUnless(SLOW, "many seeds per mix: SO2_SLOW_TESTS=1 (the release workflow)")
@needs_reference
class TestTrackerManySeeds(unittest.TestCase):
    def test_many_seeds(self):
        for label, options in MIXES.items():
            for seed in SLOW_SEEDS:
                with self.subTest(mix=label, seed=seed):
                    mw = real_generation(options, seed)
                    check_random_states(self, mw, check_spheres(self, mw), seed)


def rule_from_body(export, k, names):
    """Export body k as the Rule Builder rule it was written from (items by name again)."""
    body = export["rules"][k]
    if body is True or body is False:
        return True_() if body else False_()
    kind, args = body[0], body[1:]
    name = (lambda ref: ref if isinstance(ref, str) else names[ref])
    if kind == "has":
        return Has(name(args[0]), args[1])
    if kind in ("all", "any"):
        return (HasAll if kind == "all" else HasAny)(*map(name, args))
    return (And if kind == "and" else Or)(*(rule_from_body(export, c, names) for c in args))


class TestTrackerExport(unittest.TestCase):
    """The export's shape, for a world per mode and the richest mixes."""
    WORLDS = {"story": MIXES["story party"], "open world": MIXES["open full"], "hub": MIXES["hub rich"],
              "open hell": MIXES["open hell"], "open no teleporter": MIXES["open no teleporter"]}

    @classmethod
    def setUpClass(cls):
        cls.worlds = {label: setup_multiworld(SO2World, UT_STEPS, 5, options).worlds[1]
                      for label, options in cls.WORLDS.items()}

    def test_shape(self):
        for label, world in self.worlds.items():
            with self.subTest(label):
                export = room_slot_data(world)["tracker"]
                self.assertEqual(export["format"], tracker_export.FORMAT)
                self.assertEqual(set(export), {"format", "regions", "rules", "entrances", "events", "locations", "goal"})
                regions = list(world.multiworld.get_regions(1))
                self.assertEqual(export["regions"][0], world.origin_region_name)
                self.assertEqual(sorted(export["regions"]), sorted(r.name for r in regions))
                n_rules, n_regions = len(export["rules"]), len(export["regions"])
                bodies = [json.dumps(b) for b in export["rules"]]
                self.assertEqual(len(set(bodies)), len(bodies), "equal bodies once (interned)")
                events = {loc.item.name for loc in world.multiworld.get_locations(1) if loc.address is None}
                ids = set(world.item_name_to_id.values())
                for k, body in enumerate(export["rules"]):
                    if isinstance(body, bool):
                        continue
                    kind, args = body[0], body[1:]
                    self.assertIn(kind, ("has", "all", "any", "and", "or"))
                    if kind in ("and", "or"):
                        self.assertTrue(all(isinstance(c, int) and 0 <= c < k for c in args), "children first")
                    else:
                        refs = args[:1] if kind == "has" else args
                        for ref in refs:
                            self.assertTrue(ref in events if isinstance(ref, str) else ref in ids, ref)
                        if kind == "has":
                            self.assertIsInstance(args[1], int)
                for name, origin, target, rule in export["entrances"]:
                    self.assertIsInstance(name, str)
                    self.assertTrue(0 <= origin < n_regions and 0 <= target < n_regions and 0 <= rule < n_rules)
                self.assertEqual(len(export["entrances"]), sum(len(r.exits) for r in regions))
                self.assertEqual(sorted(e[0] for e in export["events"]), sorted(
                    loc.item.name for loc in world.multiworld.get_locations(1) if loc.address is None))
                seen = [a for group in export["locations"] for a in group[2:]]
                self.assertEqual(sorted(seen), sorted(loc.address for loc in world.multiworld.get_locations(1)
                                                      if loc.address is not None))
                self.assertEqual(len(set(seen)), len(seen))
                self.assertTrue(0 <= export["goal"] < n_rules)
                self.assertFalse(events & set(world.item_name_to_id), "event names are no item names")

    def test_round_trip(self):
        """Each spot's body, read back as Rule Builder rules (items by name), resolves to the spot's own
        rule; so does the goal's."""
        for label, world in self.worlds.items():
            with self.subTest(label):
                export = room_slot_data(world)["tracker"]
                names = {code: name for name, code in world.item_name_to_id.items()}
                region = {r.name: r for r in world.multiworld.get_regions(1)}
                exits = [e for name in export["regions"] for e in region[name].exits]
                self.assertEqual([(name, region[export["regions"][o]], region[export["regions"][t]])
                                  for name, o, t, _ in export["entrances"]],
                                 [(e.name, e.parent_region, e.connected_region) for e in exits])
                spots = [(e, rule) for (_, _, _, rule), e in zip(export["entrances"], exits)]
                by_address = {a: g[1] for g in export["locations"] for a in g[2:]}
                spots += [(loc, by_address[loc.address]) for loc in world.multiworld.get_locations(1) if loc.address]
                event_rules = collections.defaultdict(list)
                for name, _, rule in export["events"]:
                    event_rules[name].append(rule)
                for loc in world.multiworld.get_locations(1):
                    if loc.address is None:
                        spots.append((loc, event_rules[loc.item.name].pop(0)))
                for spot, k in spots:
                    self.assertEqual(rule_from_body(export, k, names).resolve(world), spot.access_rule, spot.name)
                self.assertEqual(rule_from_body(export, export["goal"], names).resolve(world),
                                 world.multiworld.completion_condition[1])

    def test_kinds(self):
        """The export has a form for each of logic.RESOLVED_KINDS and refuses any other rule (a new
        kind, such as CanReachRegion in Phase C, needs its form first: the evaluators read "region")."""
        world = self.worlds["open world"]
        made = tracker_export._Export(world)
        made.events = {"Victory"}
        resolved = [True_().resolve(world), False_().resolve(world), Has("Victory").resolve(world),
                    HasAll("Victory", "Salva Pass").resolve(world), HasAny("Victory", "Salva Pass").resolve(world)]
        resolved += [And(HasAny("Victory", "Salva Pass"), Has("Arlia Pass")).resolve(world),
                     Or(HasAll("Victory", "Salva Pass"), Has("Arlia Pass")).resolve(world)]
        self.assertEqual({type(r) for r in resolved}, set(logic.RESOLVED_KINDS))
        salva, arlia = world.item_name_to_id["Salva Pass"], world.item_name_to_id["Arlia Pass"]
        bodies = [made.rules[made.rule(r)] for r in resolved]
        self.assertEqual(bodies[:5], [True, False, ["has", "Victory", 1], ["all", salva, "Victory"],
                                      ["any", salva, "Victory"]])     # Rule Builder sorts the names
        self.assertEqual([b[0] for b in bodies[5:]], ["and", "or"])
        with self.assertRaises(TypeError):
            made.rule(object())
        with self.assertRaises(ValueError):
            made.rule(Has("Nothing of the kind").resolve(world))

    def test_not_in_the_seed(self):
        """The .apso2 doesn't carry it (the port never reads it): seed_data's options are slot_options."""
        world = self.worlds["open world"]
        self.assertNotIn("tracker", world.slot_options())


@needs_reference
class TestReferenceEvaluator(unittest.TestCase):
    """The evaluator's kinds and sweep on a hand-made export: counts, events (by name, counted per event
    location), the passes to a fixpoint, the "region" kind (an indirect condition: an entrance whose rule
    reads another region, reached later in the same sweep), the goal; unknown formats and kinds."""
    EXPORT = {
        "format": 1,
        "regions": ["Menu", "A", "B", "C"],
        "rules": [True, False, ["has", 10, 2], ["all", 10, 11], ["any", 11, 12], ["and", 0, 3], ["or", 1, 2],
                  ["has", "E", 1], ["has", "E", 2], ["region", 3], ["all", "E", 12]],
        # Menu -> A (open), A -> B (reached C: indirect), Menu -> C (event E), Menu -> B (never), A -> ?
        # (not connected: an entrance randomizer's deferred entrance, open once found)
        "entrances": [["New Game", 0, 1, 0], ["A to B", 1, 2, 9], ["To C", 0, 3, 7], ["Never", 0, 2, 1],
                      ["Door 7", 1, -1, 0]],
        "events": [["E", 1, 4], ["E", 2, 0], ["G", 2, 8]],
        "locations": [[0, 0, 100, 101], [1, 2, 102], [2, 0, 103], [3, 6, 104], [1, 10, 105]],
        "goal": 10,
    }

    def view(self, counts):
        v = REFERENCE.TrackerLogic(self.EXPORT).evaluate(counts)
        return sorted(v.regions), v.events, sorted(v.locations), v.goal

    def test_sweep(self):
        self.assertEqual(self.view({}), (["A", "Menu"], {}, [100, 101], False))
        self.assertEqual(self.view({10: 1}), (["A", "Menu"], {}, [100, 101], False))
        # 11: the event E in A -> C -> B (its rule reads C) -> E again, then G (two E)
        self.assertEqual(self.view({11: 1}), (["A", "B", "C", "Menu"], {"E": 2, "G": 1}, [100, 101, 103], False))
        self.assertEqual(self.view({10: 2, 12: 1}),
                         (["A", "B", "C", "Menu"], {"E": 2, "G": 1}, [100, 101, 102, 103, 104, 105], True))

    def test_deferred_entrance(self):
        """An entrance with no target reaches nothing until `connect` names its region."""
        logic = REFERENCE.TrackerLogic(self.EXPORT)
        self.assertEqual(sorted(logic.evaluate({}).regions), ["A", "Menu"])
        logic.connect("Door 7", "C")
        view = logic.evaluate({})
        self.assertEqual((sorted(view.regions), view.events, sorted(view.locations)),
                         (["A", "B", "C", "Menu"], {"E": 1}, [100, 101, 103]))   # E in B, not G (two E)
        self.assertEqual(REFERENCE.TrackerLogic(self.EXPORT).evaluate({}).regions, {"A", "Menu"},
                         "the export itself is unchanged")
        with self.assertRaises(KeyError):
            logic.connect("Door 8", "C")

    def test_refusals(self):
        for bad in (dict(self.EXPORT, format=2), {"regions": []}, []):
            with self.assertRaises(ValueError):
                REFERENCE.TrackerLogic(bad)
        with self.assertRaises(ValueError):
            REFERENCE.TrackerLogic({"format": 1, "regions": ["Menu"], "rules": [["xor", 1]], "entrances": [],
                                    "events": [], "locations": [[0, 0, 100]], "goal": 0}).evaluate({})


class TestTrackerExportSize(unittest.TestCase):
    """At most SIZE_LIMIT bytes of JSON at the richest mix of each mode (every check option on)."""

    def test_size(self):
        for label, options in RICHEST.items():
            with self.subTest(label):
                world = setup_multiworld(SO2World, UT_STEPS, 3, options).worlds[1]
                export = room_slot_data(world)["tracker"]
                size = compact_size(export)
                self.assertLessEqual(size, SIZE_LIMIT, f"{label}: {size} bytes")
