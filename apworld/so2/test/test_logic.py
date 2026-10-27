"""Rules as data (logic.py; docs/ap/apworld.md "Rules"; docs/plans/trackers.md B2).

Every location (events too), entrance and the goal of a world carries an Archipelago Rule Builder
rule (rule_builder.rules.Rule.Resolved), made only of logic.RESOLVED_KINDS (no lambdas, no region
reads), naming items and events of this world by name. Each rule the world sets serialises
(Rule.to_dict, through JSON) and comes back (SO2World.rule_from_dict) as the same resolved rule; so
does each resolved rule through logic.as_rule (the tracker export's way, trackers.md B3). And each
explains itself (UT's /explain: explain_json / explain_str). One world per mode and rich mix (the
three world modes, test_ut.MIXES, the hell seed); the source check keeps lambda rules out of the
world's code."""
import json
import os
import re
import unittest
from unittest import mock

from BaseClasses import CollectionState
from rule_builder.rules import Rule
from test.general import setup_multiworld
from worlds.AutoWorld import World

from .. import logic
from ..world import SO2World
from .test_ut import MIXES

STEPS = ("generate_early", "create_regions", "create_items", "set_rules")
# Spots (location or entrance names) allowed a rule that is not the Rule Builder's: none.
NOT_RULE_BUILDER = frozenset()
WORLDS = {
    "story": {},
    "open world": {"world_mode": "open_world"},
    "hub": {"world_mode": "hub"},
    "story rich": MIXES["story party"],
    "open rich": MIXES["open full"],
    "open drops forced": MIXES["open full no bench"],
    "open no teleporter": MIXES["open no teleporter"],
    "open hell": MIXES["open hell"],
    "hub full": MIXES["hub full"],
    "hub rich": MIXES["hub rich"],
    "story hell": {"encounter_rate": 0, "killer_move_checks": True, "skillsanity": "insanity", "goal": 2,
                   "craftsanity": True, "enemy_dropsanity": "first_drop", "cave_of_trials": True},
}


def build(options, seed=1):
    """The world through set_rules, with every rule set through set_rule / set_completion_rule
    recorded (spot -> the Rule as set; "goal" -> the completion rule)."""
    sources = {}

    def set_rule(self, spot, rule):
        sources[spot] = rule
        World.set_rule(self, spot, rule)

    def set_completion_rule(self, rule):
        sources["goal"] = rule
        World.set_completion_rule(self, rule)

    with mock.patch.object(SO2World, "set_rule", set_rule), \
            mock.patch.object(SO2World, "set_completion_rule", set_completion_rule):
        mw = setup_multiworld(SO2World, STEPS, seed, options)
    return mw, sources


def spots(mw):
    return [spot for region in mw.get_regions(1) for spot in (*region.locations, *region.exits)]


def walk(rule):
    yield rule
    for child in getattr(rule, "children", ()):
        yield from walk(child)


def item_names(rule):
    for part in walk(rule):
        if hasattr(part, "item_name"):
            yield part.item_name
        yield from getattr(part, "item_names", ())


def round_trip(data):
    return SO2World.rule_from_dict(json.loads(json.dumps(data)))


class TestRulesAsData(unittest.TestCase):
    def test_every_spot(self):
        for label, options in WORLDS.items():
            with self.subTest(label):
                mw, sources = build(options)
                world = mw.worlds[1]
                events = {loc.item.name for loc in mw.get_locations(1) if loc.address is None}
                names = set(SO2World.item_name_to_id) | events
                rules = {spot: spot.access_rule for spot in spots(mw) if spot.name not in NOT_RULE_BUILDER}
                rules["goal"] = mw.completion_condition[1]
                self.assertGreater(len(rules), 100)
                bare = CollectionState(mw)
                for spot, rule in rules.items():
                    name = getattr(spot, "name", spot)
                    self.assertIsInstance(rule, Rule.Resolved, name)
                    self.assertTrue(all(isinstance(part, logic.RESOLVED_KINDS) for part in walk(rule)),
                                    f"{name}: {rule}")
                    self.assertEqual(set(item_names(rule)) - names, set(), name)
                    self.assertEqual(rule.player, 1)
                    # set through set_rule: its source serialises and resolves to it again
                    self.assertIn(spot, sources, name)
                    self.assertEqual(round_trip(sources[spot].to_dict()).resolve(world), rule, name)
                    # the resolved rule itself, through logic.as_rule (the tracker export)
                    self.assertEqual(round_trip(logic.as_rule(rule).to_dict()).resolve(world), rule, name)
                    self.assertIsInstance(rule.explain_json(bare), list)
                    self.assertIsInstance(rule.explain_str(bare), str)

    def test_rules_are_shared(self):
        """Resolved rules are interned: the same rule twice is one object (the export can intern by
        identity)."""
        mw, _ = build(MIXES["open full"])
        rules = [loc.access_rule for loc in mw.get_locations(1)]
        self.assertEqual(len({id(r) for r in rules}), len(set(rules)))
        self.assertLess(len(set(rules)), len(rules) / 2)

    def test_no_region_reads(self):
        """No rule reads a region, a location or an entrance (none needs an indirect condition)."""
        for label in ("open rich", "hub rich"):
            with self.subTest(label):
                mw, _ = build(WORLDS[label])
                for spot in spots(mw):
                    rule = spot.access_rule
                    self.assertEqual((rule.region_dependencies(), rule.location_dependencies(),
                                      rule.entrance_dependencies()), ({}, {}, {}), spot.name)

    def test_no_lambda_rules_in_the_code(self):
        """The world's modules set no access rule but through set_rule / set_completion_rule (item
        rules, add_item_rule, stay functions)."""
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        pattern = re.compile(r"\.access_rule\s*=|completion_condition\[.*\]\s*=|lambda state")
        for name in sorted(os.listdir(here)):
            if name.endswith(".py"):
                with open(os.path.join(here, name), encoding="utf-8") as f:
                    for n, line in enumerate(f, 1):
                        self.assertIsNone(pattern.search(line), f"{name}:{n}: {line.strip()}")

    def test_explain(self):
        """UT's /explain: an open-world location's rule names its event and keys, and says what is
        missing."""
        mw, _ = build({"world_mode": "open_world"})
        loc = mw.worlds[1].get_location("Indalecio")
        bare = CollectionState(mw)
        self.assertEqual(str(loc.access_rule), "Has Saved at the Final Save Point")
        self.assertEqual(loc.access_rule.explain_str(bare), "Missing Saved at the Final Save Point")
        texts = [part.get("text") for part in loc.access_rule.explain_json(bare)]
        self.assertIn("Saved at the Final Save Point", texts)
