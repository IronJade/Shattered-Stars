"""The SO2 world's own tests (docs/ap/apworld.md). WorldTestBase's default tests run for
each option set too: every location reachable with all items, something reachable from the
start, and a fill that meets the accessibility setting."""
import re

from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from ..data import GOAL_ENCOUNTERS, ITEM_BASE, LOCATIONS
from ..items import CHARACTER_OF, EVENT_FINAL_SAVE_POINT, FIXED_MEMBERS, PARTY_SIZE, TRAPS
from ..locations import ROW, SOFT_REASONS, missable_reasons
from ..patches import role_of_placeholder
from .bases import SO2TestBase

PATCH = re.compile(r"^script (\d+) 0x([0-9a-f]+) 0x([0-9a-f]{8}) 0x([0-9a-f]{8})$")


def own_locations(test):
    return [loc for loc in test.multiworld.get_locations(test.player) if loc.address is not None]


def own_pool(test):
    return [i for i in test.multiworld.itempool if i.player == test.player]


def fill(test):
    from Fill import distribute_items_restrictive
    distribute_items_restrictive(test.multiworld)


def assert_no_gift_progression(test):
    """No gift location holds progression (the user, 2026-10-25): not this world's own keys
    (open world's area keys) and not another world's, whatever `may_hold_progression` /
    `open_world_progression` would otherwise let through."""
    from ..items import SO2Item
    gifts = [loc for loc in own_locations(test) if ROW[loc.name]["type"] == "gift"]
    test.assertTrue(gifts)
    other = SO2Item("test key", ItemClassification.progression, None, test.player + 1)
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


class Teststory_routeClaude(SO2TestBase):
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
        from ..items import SO2Item
        key = SO2Item("test key", ItemClassification.progression, None, self.player)
        for loc in self.world.get_region("Expel").locations:
            if loc.address is not None:
                self.assertFalse(loc.can_fill(self.multiworld.state, key, False), loc.name)
                break


class LateProofChecks:
    """Progression goes only where the late proof (docs/ap/late_proof.json, rows' `late`)
    reached the location in the story_route's game (both under "any"); story bosses are exempt,
    drops and join sites never hold it."""
    side = None

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
    side = "rena"


class TestBossesOff(SO2TestBase):
    options = {"boss_checks": False}

    def test_no_bosses(self):
        self.assertFalse(any(ROW[loc.name]["type"] == "boss" for loc in own_locations(self)))


def party_of(test):
    return [CHARACTER_OF[i.code] for i in own_pool(test) if i.code in CHARACTER_OF]


class CharacterChecks:
    """character_items: the pool's characters always make a party the story allows."""
    side = None

    def test_party_is_consistent(self):
        for _ in range(20):     # the choice is random: a few more draws with the world's RNG
            self.check_party(self.world.choose_party())
        self.check_party(party_of(self))
        self.assertEqual(sorted(party_of(self)), sorted(self.world.party))

    def check_party(self, party):
        self.assertEqual(len(party), len(set(party)))
        self.assertLessEqual(len(party) + len(FIXED_MEMBERS), PARTY_SIZE)
        self.assertEqual(sum(x in party for x in ("Bowman", "Precis")), 1, party)
        self.assertFalse("Ashton" in party and "Opera" in party, party)
        self.assertTrue("Ashton" in party or "Opera" in party, party)
        if "Ernest" in party:
            self.assertIn("Opera", party)
        if self.side != "claude":
            self.assertNotIn("Leon", party)
        if self.side != "rena":
            self.assertNotIn("Dias", party)

    def test_join_sites(self):
        joins = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "join"]
        self.assertTrue(joins)
        for loc in joins:
            self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)
        listed = {ROW[loc.name]["character"] for loc in joins}
        # Every shuffled member's sites are listed, in the pool or not, so nobody joins at a scene.
        self.assertTrue({"Celine", "Bowman", "Precis", "Ashton", "Opera", "Ernest", "Chisato"} <= listed)
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertIs(seed["options"]["character_items"], True)
        kinds = {v["kind"] for v in seed["items"].values()}
        self.assertIn("character", kinds)
        for v in seed["items"].values():
            if v["kind"] == "character":
                self.assertIn(v["character"], self.world.party)


class TestCharactersAny(SO2TestBase, CharacterChecks):
    options = {"character_items": True}


class TestCharactersClaude(SO2TestBase, CharacterChecks):
    options = {"character_items": True, "story_route": "claude"}
    side = "claude"


class TestCharactersRena(SO2TestBase, CharacterChecks):
    options = {"character_items": True, "story_route": "rena", "missable_locations": "remove"}
    side = "rena"


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


# ---- Phase 4: open world and shop slots ----

def doors_json():
    import json
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "ap", "doors.json")
    return json.load(open(path)) if os.path.exists(path) else None


class OpenWorldChecks:
    """open_world: the keys, the stages, the doors in the seed, where progression may go."""
    side = None
    teleporter = True

    def test_keys_and_start(self):
        from ..data import KEY_GROUPS
        from ..items import KEY_OF, START_KEYS
        start = set(START_KEYS) | (set() if self.teleporter else {"North City Pass"})
        pre = {i.name for i in self.multiworld.precollected_items[self.player]}
        self.assertEqual(pre & {g["name"] for g in KEY_GROUPS}, start)
        keys = [i for i in own_pool(self) if i.code in KEY_OF]
        self.assertEqual(sorted(i.name for i in keys), sorted(g["name"] for g in KEY_GROUPS if g["name"] not in start))
        self.assertTrue(all(i.advancement for i in keys))
        self.assertEqual(len(own_pool(self)), sum(1 for loc in own_locations(self) if not loc.locked))

    def test_stages_need_keys(self):
        from ..items import stage_event
        state = CollectionState(self.multiworld)
        for i in self.multiworld.precollected_items[self.player]:
            state.collect(i, True)
        self.assertTrue(self.world.get_location("Story: Shingo Forest and Arlia").can_reach(state))
        self.assertFalse(self.world.get_location("Story: Salva and Salva Drift").can_reach(state))
        state.collect(self.world.get_location("Story: Shingo Forest and Arlia").item, True)
        state.collect(self.world.create_item("Salva Pass"), True)
        self.assertTrue(self.world.get_location("Story: Salva and Salva Drift").can_reach(state))
        self.assertFalse(state.has(stage_event(2), self.player))
        # Salva's chests need the pass and the story's first stage.
        salva = [loc for loc in own_locations(self) if ROW[loc.name].get("group") == "Town of Salva"
                 and ROW[loc.name]["type"] == "chest"]
        self.assertTrue(salva and all(loc.can_reach(state) for loc in salva))

    def test_ship_landings(self):
        """The Lacour continent is reached by the Herlie ship, which lands in Port Hilton, and
        Hoffmann Ruins by the Hilton ship, which lands on map 231: before the story's later
        stages their locations need those keys too, and the Herlie stage (which ends entering
        Lacour) needs Hilton's and Lacour's."""
        from ..data import KEY_GROUPS, STAGES
        from ..items import KEY_NAME

        def state_at(stage, keys):
            """The stages up to `stage` done, with the keys they need and `keys`; no teleporter, no
            Final Save Point (so only a location's early mode counts)."""
            state = CollectionState(self.multiworld)
            for i in self.multiworld.precollected_items[self.player]:
                state.collect(i, True)
            for name, _, need in STAGES[:stage]:
                state.collect(self.world.get_location(f"Story: {name}").item, True)
                for g in need:
                    state.collect(self.world.create_item(KEY_NAME[g]), True)
            for name in keys:
                state.collect(self.world.create_item(name), True)
            return state

        herlie = self.world.get_location("Story: Herlie")
        self.assertFalse(herlie.can_reach(state_at(5, ["Lacour Pass"])))
        self.assertFalse(herlie.can_reach(state_at(5, ["Hilton Pass"])))
        self.assertTrue(herlie.can_reach(state_at(5, ["Hilton Pass", "Lacour Pass"])))
        tested = 0
        for g in KEY_GROUPS:
            if not g.get("landing"):
                continue
            locs = [loc for loc in own_locations(self) if ROW[loc.name].get("group") == g["group"]
                    and ROW[loc.name]["region"] == "Expel" and ROW[loc.name]["type"] == "chest"
                    and not ROW[loc.name].get("from_chapter")]
            before = state_at(g["stage"] - 1, [g["name"]])
            after = state_at(g["stage"] - 1, [g["name"]] + [KEY_NAME[x] for x in g["landing"]])
            for loc in locs:
                self.assertFalse(loc.can_reach(before), loc.name)
                self.assertTrue(loc.can_reach(after), loc.name)
            tested += len(locs)
        self.assertGreater(tested, 20)    # Rosalia's Room and Hoffmann Ruins' chests

    def test_progression_rules(self):
        from ..items import SO2Item
        from ..rules import closes_at_gate, proven_late, proven_teleporter
        mine = self.world.create_item("Salva Pass")            # an Expel-story key of ours
        nede = self.world.create_item("North City Pass")       # needed after the disc switch
        other = SO2Item("test key", ItemClassification.progression, None, self.player + 1)
        allowed = {"any": 0, "expel keys": 0}
        for loc in own_locations(self):
            row = ROW[loc.name]
            if loc.progress_type == LocationProgressType.EXCLUDED or row["type"] not in ("chest", "gift"):
                continue
            if row["region"] != "Expel":
                continue
            st = self.multiworld.state
            if row["type"] == "gift":   # never progression, not even our own Expel-story keys
                for item in (nede, other, mine):
                    self.assertFalse(loc.can_fill(st, item, False), loc.name)
                continue
            # (A location the story reaches before the El voyage that the voyage proof did not
            # reach there holds none: test_voyage_gate.)
            stays = not closes_at_gate(row, self.side)
            tele = self.teleporter and proven_teleporter(row, self.side) and stays
            late = proven_late(row, self.side) and stays
            self.assertEqual(loc.can_fill(st, nede, False), tele, loc.name)
            self.assertEqual(loc.can_fill(st, other, False), tele, loc.name)
            self.assertEqual(loc.can_fill(st, mine, False), tele or late, loc.name)
            allowed["any" if tele else "expel keys"] += tele or late
        self.assertGreater(sum(allowed.values()), 50)

    def test_voyage_gate(self):
        """The El voyage (data.STORY_GATES): from the Lacour Front Line's meeting room at chapter
        27 step 7000 the story sails to El and the Cross and Lacour continents and Hoffmann
        Ruins' island close until the teleporter or the Final Save Point. The stage that ends
        with it needs the three El passes; the seed's gate door keeps the port's trigger shut
        until they arrive; a location the story reaches before it holds progression only where
        the voyage proof reached it at the gate, so the passes (and whatever they hang on) are
        in reach there."""
        from ..data import STAGES, STORY_GATES
        from ..items import KEY_NAME, stage_event
        from ..regions import gate_groups
        from ..rules import closes_at_gate, early_stage, proven_at_gate
        (gate,) = STORY_GATES
        self.assertEqual((gate["name"], gate["stage"], gate["door"], gate["routine"]), ("El voyage", 10, 29106503, 0x7C28))
        self.assertEqual(sorted(KEY_NAME[g] for g in gate["groups"]), ["El Beach Pass", "Eluria Pass", "Eluria Tower Pass"])
        # With the teleporter and expel_progression on, also the Nede stages' keys until the
        # Synard reaches Fun City's teleporter (test_teleporter_needs_the_synard).
        later = bool(self.world.options.expel_teleporter and self.world.options.expel_progression)
        self.assertEqual(gate["teleporter_groups"], ["North City", "Noel's House", "Cave of Red Crystal"])
        passes = [KEY_NAME[g] for g in gate_groups(self.world, gate)]
        self.assertEqual(len(passes), 6 if later else 3)
        self.assertTrue(all(gate["proven"].get(g) for g in ("claude", "rena")))

        def state_at(stage, extra):
            state = CollectionState(self.multiworld)
            for i in self.multiworld.precollected_items[self.player]:
                state.collect(i, True)
            for name, _, need in STAGES[:stage]:
                state.collect(self.world.get_location(f"Story: {name}").item, True)
                for g in need:
                    state.collect(self.world.create_item(KEY_NAME[g]), True)
            for g in STAGES[stage][2]:
                state.collect(self.world.create_item(KEY_NAME[g]), True)
            for name in extra:
                state.collect(self.world.create_item(name), True)
            return state

        voyage = self.world.get_location(f"Story: {STAGES[gate['stage'] - 1][0]}")
        for k in range(len(passes)):
            self.assertFalse(voyage.can_reach(state_at(gate["stage"] - 1, passes[:k] + passes[k + 1:])), passes[k])
        self.assertTrue(voyage.can_reach(state_at(gate["stage"] - 1, passes)))
        # Where a pass may go in this world: a location the voyage proof reached at the gate, or
        # a story boss fought before it; never one the story reaches only after the voyage.
        el_pass = self.world.create_item(passes[0])
        st = self.multiworld.state
        counts = {"proven": 0, "closes": 0}
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["type"] not in ("chest", "gift", "boss"):
                continue
            stage = early_stage(row)
            if stage is not None and stage <= gate["stage"] and row.get("boss") != "story":
                if not proven_at_gate(row, self.side, 0):
                    self.assertTrue(closes_at_gate(row, self.side), loc.name)
                    self.assertFalse(loc.can_fill(st, el_pass, False), loc.name)
                    counts["closes"] += 1
                elif loc.can_fill(st, el_pass, False):
                    counts["proven"] += 1
        self.assertGreater(counts["proven"], 50)
        fill(self)
        stage_of = {loc.name: early_stage(ROW[loc.name]) for loc in own_locations(self) if loc.address}
        for loc in own_locations(self):
            if loc.item and loc.item.player == self.player and loc.item.name in passes:
                row = ROW[loc.name]
                if row.get("boss") == "story":
                    self.assertLess(row["chapter"], gate["chapter"] + 1, loc.name)
                else:
                    self.assertTrue(proven_at_gate(row, self.side, 0), loc.name)
                    self.assertLessEqual(stage_of[loc.name], gate["stage"], loc.name)
        self.assertEqual(stage_event(gate["stage"]), voyage.item.name)

    def test_teleporter_needs_the_synard(self):
        """Fun City, the teleporter's end on Energy Nede, lies on another island of the Nede
        world map than Central City: the party reaches it once the Synard flies (stage 16, the
        Cave of Red Crystal), not at chapter 38."""
        from ..data import STAGES, TELEPORTER_STAGE
        from ..items import stage_event
        self.assertEqual(STAGES[TELEPORTER_STAGE - 1][0], "Cave of Red Crystal")
        if not self.teleporter:
            return
        loc = self.world.get_location("Expel Teleporter (Fun City arena)")
        state = CollectionState(self.multiworld)
        for i in self.multiworld.precollected_items[self.player]:
            state.collect(i, True)
        for name, _, _ in STAGES[:TELEPORTER_STAGE - 1]:
            state.collect(self.world.get_location(f"Story: {name}").item, True)
        state.collect(self.world.get_location("Disc Switch (Eluria Tower)").item, True)
        self.assertTrue(state.has(stage_event(TELEPORTER_STAGE - 1), self.player))
        self.assertFalse(loc.can_reach(state))
        state.collect(self.world.get_location(f"Story: {STAGES[TELEPORTER_STAGE - 1][0]}").item, True)
        self.assertTrue(loc.can_reach(state))

    def test_teleporter_islands(self):
        """The teleporter's party (chapter 38, landing in Arlia) walks the Cross continent and
        sails the Herlie ferry to the Lacour continent; the El continent and Hoffmann Ruins'
        island are reached only by the story's ships (chapters 23-28) or by Virtual Expel's
        Synard (tools/ap_walkmap.py, tools/ap_late_proof.py TELEPORTER_ISLANDS). So no
        location there counts as reached through the teleporter, and none may hold a key the
        story needs after the disc switch."""
        from ..items import EVENT_TELEPORTER
        from ..rules import open_world_access, proven_teleporter
        nede = self.world.create_item("Red Crystal Pass")   # chapter 40, where the Synard is won
        islands = {"El Beach", "Eluria Colony", "Eluria Tower", "Hoffmann Ruins", "map 231"}
        tested = 0
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["region"] != "Expel" or row.get("group") not in islands or row.get("boss") == "story":
                continue    # (a story boss is fought by the story itself)
            self.assertFalse(proven_teleporter(row, self.side), loc.name)
            modes = open_world_access(row, self.side, self.teleporter)
            self.assertNotIn(EVENT_TELEPORTER, [ev for ev, _ in modes], loc.name)
            self.assertFalse(loc.can_fill(self.multiworld.state, nede, False), loc.name)
            tested += 1
        self.assertGreater(tested, 30)

    def test_gifts_never_hold_progression(self):
        assert_no_gift_progression(self)

    def test_seed_doors(self):
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["options"]["world_mode"], "open_world")
        self.assertIs(seed["options"]["expel_teleporter"], self.teleporter)
        from ..data import EXIT_GATES, STORY_GATES
        entrances = [d for d in seed["doors"] if "key" in d]
        gates = [d for d in seed["doors"] if "keys" in d and "until" not in d]
        exits = [d for d in seed["doors"] if "until" in d]
        # Central City's and Noel's House's exits, shut until chapters 38 / 40 without their keys.
        self.assertEqual([(d["id"], d["until"], d["keys"], d["purpose"]) for d in exits],
                         [(15407589, 38, [28], "to leave"), (18160748, 40, [30], "to leave")])
        self.assertEqual(len(exits), len(EXIT_GATES))
        self.assertEqual(len(entrances), 66)
        self.assertEqual(len({d["key"] for d in entrances}), 35)
        from ..regions import gate_keys
        self.assertEqual(gates, [{"id": g["door"], "routine": f"{g['routine']:#x}", "keys": gate_keys(self.world, g),
                                  "name": g["name"], "purpose": g["purpose"]} for g in STORY_GATES])
        later = self.teleporter and bool(self.world.options.expel_progression)
        self.assertEqual([len(d["keys"]) for d in gates], [6 if later else 3])
        self.assertEqual(len(seed["doors"]), 66 + len(STORY_GATES) + len(EXIT_GATES))
        kinds = {i["kind"] for i in seed["start"]["items"]}
        self.assertEqual(kinds, {"key"})
        table = doors_json()
        if table is not None:
            from ..doors import validate_doors
            self.assertEqual(validate_doors(seed["doors"], table), [])
            world = {r["id"]: r for r in table["doors"] if r.get("world") in (1, 2)}
            self.assertEqual({d["id"] for d in entrances}, set(world))
            for d in entrances:
                self.assertIn(world[d["id"]]["key_group"], d["name"])
            by_id = {r["id"]: r for r in table["doors"]}
            for d in gates:   # the El voyage's: the transfer to Lacour Castle in the trigger's routine
                self.assertEqual((by_id[d["id"]]["map"], by_id[d["id"]]["routine"], by_id[d["id"]]["to"]["map"]),
                                 (444, 0x7C28, 422))
            for d in exits:   # an exit to Energy Nede's world map in the trigger's routine
                self.assertEqual((by_id[d["id"]]["routine"], by_id[d["id"]]["to"]["map"]), (int(d["routine"], 16), 2))


class TestOpenWorld(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world"}


class TestOpenWorldRenaShops(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world", "story_route": "rena", "shuffle_shops": "full", "cave_of_trials": True,
               "character_items": True}
    side = "rena"


class TestOpenWorldNoTeleporter(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world", "expel_teleporter": False, "story_route": "claude",
               "missable_locations": "include"}
    side = "claude"
    teleporter = False


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
