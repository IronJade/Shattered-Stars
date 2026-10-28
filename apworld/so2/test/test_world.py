"""The SO2 world's own tests (docs/apworld/apworld.md): story mode's locations, pool, goal and seed;
the missables, the cave, Expel without progression, the late proof's rule, the drops and traps,
the shop slots. WorldTestBase's default tests run for each option set too: every location
reachable with all items, something reachable from the start, and a fill that meets the
accessibility setting. Open world: test_open_world.py; the party: test_party.py."""
from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from ..data import LOCATIONS
from ..items import EVENT_FINAL_SAVE_POINT, TRAPS
from ..locations import ROW, SOFT_REASONS, missable_reasons
from .bases import SO2TestBase, assert_no_gift_progression, check_seed, fill, own_locations, own_pool


class TestDefault(SO2TestBase):
    def test_pool_is_the_vanilla_contents(self):
        locs = own_locations(self)
        pool = own_pool(self)
        self.assertEqual(len(pool), len(locs))
        self.assertFalse(any(i.advancement for i in pool), "story mode has no progression items of its own")

    def test_default_location_set(self):
        rows = [ROW[loc.name] for loc in own_locations(self)]
        self.assertFalse(any(r["region"] == "Cave of Trials" for r in rows))
        self.assertFalse(any("never" in r["missable"] for r in rows))
        self.assertEqual({r["type"] for r in rows}, {"chest", "gift", "boss"})
        for loc in own_locations(self):
            missable = bool(missable_reasons(ROW[loc.name], None))
            self.assertEqual(loc.progress_type == LocationProgressType.EXCLUDED, missable, loc.name)

    def test_bosses(self):
        bosses = [ROW[loc.name] for loc in own_locations(self) if ROW[loc.name]["type"] == "boss"]
        # 47 boss encounters: 2 are the goal, 4 have no constant battle site, 13 are in the cave.
        self.assertEqual(len(bosses), 47 - 2 - 4 - 13)
        story = [b for b in bosses if b["boss"] == "story"]
        self.assertEqual(len(story), 21)
        self.assertFalse(any(b["missable"] for b in story))
        self.assertTrue(all(b["missable"] for b in bosses if b["boss"] in ("losable", "optional")))
        self.assertEqual({b["encounter"] for b in bosses if b["boss"] == "losable"}, {204, 517, 530})

    def test_goal_needs_the_final_save_point(self):
        # Story mode's spheres hang on events: a state without them can't reach the end.
        bare = CollectionState(self.multiworld)
        self.assertFalse(self.world.get_location("Indalecio").access_rule(bare))
        self.assertFalse(bare.can_reach_region("Energy Nede", self.player))
        self.assertTrue(bare.can_reach_region("Expel", self.player))
        self.assertBeatable(True)   # with the events swept up, the story runs to the end

    def test_seed_output(self):
        fill(self)
        seed = self.world.seed_data()
        self.assertIs(seed["options"]["deathlink"], False)
        self.assertIs(seed["options"]["character_items"], False)
        self.assertEqual(seed["options"]["enemy_dropsanity"], "off")
        self.assertEqual((seed["options"]["exp_multiplier"], seed["options"]["fol_multiplier"],
                          seed["options"]["encounter_rate"]), (1, 1, 100))
        check_seed(self, seed)


class TestStoryRouteClaude(SO2TestBase):
    options = {"story_route": "claude"}

    def test_no_rena_only_locations(self):
        rows = [ROW[loc.name] for loc in own_locations(self)]
        self.assertFalse(any(r["story_route"] == "rena" for r in rows))
        self.assertTrue(any(r["story_route"] == "claude" for r in rows))
        self.assertEqual(self.world.fill_slot_data()["story_route"], "claude")


class TestMissableRemoved(SO2TestBase):
    options = {"missable_locations": "remove"}

    def test_no_missable_locations(self):
        rows = [ROW[loc.name] for loc in own_locations(self)]
        self.assertFalse(any(r["missable"] or r["story_route"] for r in rows))
        self.assertEqual(len(own_pool(self)), len(rows))


class TestMissableIncluded(SO2TestBase):
    options = {"missable_locations": "include", "story_route": "rena", "character_items": True}

    def test_only_soft_reasons_are_included(self):
        # A chapter window or a town that changes: be in time. A choice, a member, a Private
        # Action, a side quest, a losable fight or a join site: still excluded.
        seen = set()
        for loc in own_locations(self):
            reasons = missable_reasons(ROW[loc.name], "rena")
            excluded = loc.progress_type == LocationProgressType.EXCLUDED
            self.assertEqual(excluded, bool(reasons - SOFT_REASONS), loc.name)
            seen.add(excluded)
        self.assertEqual(seen, {True, False})


class TestCaveOfTrials(SO2TestBase):
    options = {"cave_of_trials": True}

    def test_cave_needs_the_final_save_point(self):
        cave = [loc for loc in own_locations(self) if ROW[loc.name]["region"] == "Cave of Trials"]
        self.assertEqual(len(cave), sum(1 for r in LOCATIONS if r["region"] == "Cave of Trials"
                                        and r["type"] in ("chest", "gift", "boss") and "never" not in r["missable"]))
        self.assertEqual(sum(1 for loc in cave if ROW[loc.name]["type"] == "boss"), 13)
        bare = CollectionState(self.multiworld)
        self.assertFalse(bare.can_reach_region("Cave of Trials", self.player))
        bare.collect(self.world.get_location("Final Save Point (Fienal 9F)").item, True)
        bare.collect(self.world.get_location("Disc Switch (Eluria Tower)").item, True)
        self.assertTrue(bare.has(EVENT_FINAL_SAVE_POINT, self.player))
        self.assertTrue(cave[0].can_reach(bare))

    def test_cave_gems_stay(self):
        # The cave's doors on maps 751 / 764 take the gems: they stay at their own locations.
        from ..items import ITEMS, PUZZLE_ITEMS
        gems = [loc for loc in own_locations(self)
                if ROW[loc.name]["type"] in ("chest", "gift") and ROW[loc.name]["item"] in PUZZLE_ITEMS]
        self.assertEqual(len(gems), 3)
        for loc in gems:
            self.assertTrue(loc.locked, loc.name)
            self.assertEqual(loc.item.name, ITEMS[ROW[loc.name]["item"]])
        self.assertFalse(any(i.name in {ITEMS[n] for n in PUZZLE_ITEMS} for i in own_pool(self)))


class TestExpelWithoutProgression(SO2TestBase):
    options = {"expel_progression": False}

    def test_expel_refuses_progression(self):
        """Every Expel location refuses progression (the first one only, before 2026-10-26); the
        late-proven ones outside Expel still take it."""
        from ..items import SO2Item
        key = SO2Item("test key", ItemClassification.progression, None, self.player)
        expel = [loc for loc in self.world.get_region("Expel").locations if loc.address is not None]
        self.assertTrue(expel)
        for loc in expel:
            self.assertFalse(loc.can_fill(self.multiworld.state, key, False), loc.name)
        self.assertTrue(any(loc.can_fill(self.multiworld.state, key, False) for loc in own_locations(self)
                            if loc.parent_region.name != "Expel"))


class LateProofChecks:
    """Progression goes only where the late proof (docs/apworld/late_proof.json, rows' `late`)
    reached the location in the story_route's game (both under "any"); story bosses are exempt,
    drops and join sites never hold it."""

    def test_progression_only_where_proven(self):
        from ..items import SO2Item
        from ..rules import drop_rule, may_hold_progression
        key = SO2Item("test key", ItemClassification.progression, None, self.player)
        allowed = 0
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["type"] == "drop":   # drop_progression allow: test_v2 DropChecks
                proven = drop_rule(row, self.side, False, False, True, "allow", 100)[0] != "none"
            else:
                proven = may_hold_progression(row, self.side)
            want = proven and loc.progress_type != LocationProgressType.EXCLUDED
            self.assertEqual(loc.can_fill(self.multiworld.state, key, False), want, loc.name)
            allowed += want
        self.assertGreater(allowed, 100)
        # Arlia's crate is proven in both games (Virtual Arlia); old Clik's chests (map 117 / 119,
        # gone after chapter 14) are never.
        self.assertTrue(may_hold_progression(ROW["Arlia Village: 200 Fol chest"], self.side))
        for row in LOCATIONS:
            if row["type"] == "chest" and row["map"] in (117, 119):
                self.assertFalse(may_hold_progression(row, self.side), row["name"])

    def test_gifts_never_hold_progression(self):
        assert_no_gift_progression(self)


class TestLateProofAny(SO2TestBase, LateProofChecks):
    options = {"cave_of_trials": True, "missable_locations": "include", "enemy_dropsanity": "first_drop"}


class TestLateProofRena(SO2TestBase, LateProofChecks):
    options = {"story_route": "rena", "cave_of_trials": True, "missable_locations": "include"}


class TestBossesOff(SO2TestBase):
    options = {"boss_checks": False}

    def test_no_bosses(self):
        self.assertFalse(any(ROW[loc.name]["type"] == "boss" for loc in own_locations(self)))


class TestDropsAndTraps(SO2TestBase):
    options = {"enemy_dropsanity": "first_drop", "traps": 50, "exp_multiplier": 3, "fol_multiplier": 2,
               "encounter_rate": 50, "cave_of_trials": True}

    def test_drops(self):
        drops = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "drop"]
        self.assertEqual(len(drops), sum(1 for r in LOCATIONS if r["type"] == "drop"))
        from ..items import SO2Item
        from ..rules import drop_rule
        key = SO2Item("test key", ItemClassification.progression, None, self.player)
        for loc in drops:   # drop_progression allow (test_v2 has the details)
            want = drop_rule(ROW[loc.name], None, False, False, True, "allow", 50)[0] != "none"
            self.assertEqual(loc.can_fill(self.multiworld.state, key, False), want, loc.name)

    def test_traps_and_options(self):
        pool = own_pool(self)
        traps = [i for i in pool if i.name in TRAPS]
        filler = [i for i in pool if i.classification == ItemClassification.filler]
        self.assertTrue(traps)
        self.assertEqual(len(traps), (len(traps) + len(filler)) * 50 // 100)
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertTrue(any(v["kind"] == "trap" for v in seed["items"].values()))
        o = seed["options"]
        self.assertEqual((o["enemy_dropsanity"], o["exp_multiplier"], o["fol_multiplier"], o["encounter_rate"]),
                         ("first_drop", 3, 2, 50))


# ---- Shop slots ----

class ShopChecks:
    mode = "full"

    def test_shop_slots(self):
        from ..data import LOCATIONS
        slots = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "shop"]
        rows = [r for r in LOCATIONS if r["type"] == "shop" and (self.mode == "full" or r["special"])
                and "never" not in r["missable"]]
        self.assertEqual(len(slots), len(rows))
        self.assertEqual(len({(ROW[l.name]["shop"], ROW[l.name]["index"]) for l in slots}), len(slots))
        from ..items import SO2Item
        key = SO2Item("test key", ItemClassification.progression, None, self.player)
        for loc in slots:
            self.assertFalse(loc.can_fill(self.multiworld.state, key, False), loc.name)
            self.assertNotIn(ROW[loc.name]["shop"], (0x58, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E))
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["options"]["shuffle_shops"], self.mode)
        self.assertEqual(seed["doors"], [])


class TestShopsFull(SO2TestBase, ShopChecks):
    options = {"shuffle_shops": "full"}


class TestShopsSpecials(SO2TestBase, ShopChecks):
    options = {"shuffle_shops": "specials", "story_route": "claude"}
    mode = "specials"
