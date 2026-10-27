"""so2.apworld 0.2.0's features (docs/ap/apworld.md, "Changes"; docs/ap/seed_format.md, "v2
fields"): drop progression and area keys in drops, craftsanity, the goals, killer moves. Party v2:
test_party.py."""
from BaseClasses import CollectionState, LocationProgressType

from ..items import KEY_OF
from ..locations import ROW, fighter_status
from .bases import SO2TestBase, check_seed, doors_json, fill, own_locations, progression_item


# ---- Enemy drops: drop_progression, area_keys_in_drops ----

class DropChecks:
    """Each first drop holds what drop_rule allows; never a low-odds type, never with
    encounter_rate 0; force fills the fair-odds safe drops with progression first (fill_hook);
    with area_keys_in_drops off, no area key in a drop."""

    def drop_locations(self):
        return [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "drop"]

    def rule_of(self, row):
        from ..rules import drop_rule
        o = self.world.options
        return drop_rule(row, self.side, self.world.open_world,
                         self.world.open_world and bool(o.expel_teleporter), bool(o.expel_progression),
                         o.drop_progression.current_key, o.encounter_rate.value)

    def test_drop_rules(self):
        self.check_drop_rules()

    def check_drop_rules(self):
        from ..items import EXPEL_STORY_KEYS
        from ..rules import FORCE_DROP_CHANCE, MIN_DROP_CHANCE, drop_chance
        o = self.world.options
        other = progression_item(self)
        mine = self.world.create_item(sorted(EXPEL_STORY_KEYS)[1]) if self.world.open_world else None
        st = self.multiworld.state
        drops = self.drop_locations()
        self.assertEqual(len(drops), sum(1 for loc in own_locations(self) if ROW[loc.name]["type"] == "drop"))
        held = 0
        for loc in drops:
            row = ROW[loc.name]
            cls, _ = self.rule_of(row)
            excluded = loc.progress_type == LocationProgressType.EXCLUDED
            if drop_chance(row) < MIN_DROP_CHANCE or o.encounter_rate.value == 0 \
                    or o.drop_progression.current_key == "never":
                self.assertEqual(cls, "none", loc.name)
            self.assertEqual(loc.can_fill(st, other, False), cls == "any" and not excluded, loc.name)
            if mine is not None and bool(o.area_keys_in_drops):
                self.assertEqual(loc.can_fill(st, mine, False), cls != "none" and not excluded, loc.name)
            forced = (o.drop_progression.current_key == "force" and cls != "none" and not excluded
                      and drop_chance(row) >= FORCE_DROP_CHANCE)
            self.assertEqual(loc.name in self.world.forced_drops, forced, loc.name)
            self.assertNotEqual(loc.progress_type, LocationProgressType.PRIORITY, loc.name)
            held += cls != "none"
        return held

    def test_luck_gated_never(self):
        """Magius (27) and Ericodus (56) have odds 0: their first drop needs an average Luck of
        128; Shout (110) 96, Starguarder (127) 80, the odds-5 slimes 48."""
        from ..rules import drop_chance
        for row in (r for r in ROW.values() if r["type"] == "drop"):
            if row["enemy"] in (27, 56, 110, 127, 31, 32, 33, 50, 51, 52, 163):
                self.assertEqual(drop_chance(row), 0, row["name"])
                self.assertEqual(self.rule_of(row)[0], "none", row["name"])
        self.assertAlmostEqual(drop_chance(ROW["Kitty first drop"], 0), 1 - (1 - 23 / 256) * (1 - 193 / 256))


class TestDropsAllowStory(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "cave_of_trials": True}

    def test_some_hold_progression(self):
        self.assertGreater(self.check_drop_rules(), 50)


class TestDropsForceStoryNoExpel(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "drop_progression": "force", "expel_progression": False,
               "story_route": "rena"}

    def test_story_expel_off(self):
        """Story mode with expel_progression off: only a place outside Expel counts."""
        for loc in self.drop_locations():
            row = ROW[loc.name]
            if self.rule_of(row)[0] != "none":
                self.assertTrue(any(p["region"] != "Expel" for p in row["places"]), loc.name)


class TestDropsNever(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "drop_progression": "never", "world_mode": "open_world"}

    def test_none_hold_progression(self):
        self.assertEqual(self.check_drop_rules(), 0)


class TestDropsNoEncounters(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "drop_progression": "force", "encounter_rate": 0}

    def test_none_hold_progression(self):
        self.assertEqual(self.check_drop_rules(), 0)


class TestDropsForceOpenWorld(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "drop_progression": "force", "world_mode": "open_world",
               "story_route": "claude"}

    def test_access_through_stable_places(self):
        """A drop that may hold progression is reached only through its places' modes: Gerel's
        (Town of Salva's fields) from stage 1 with the Salva Pass, not before."""
        from ..items import stage_event
        loc = self.world.get_location("Gerel first drop")
        state = CollectionState(self.multiworld)
        for i in self.multiworld.precollected_items[self.player]:
            state.collect(i, True)
        state.collect(self.world.get_location("Story: Shingo Forest and Arlia").item, True)
        self.assertTrue(state.has(stage_event(1), self.player))
        self.assertFalse(loc.can_reach(state))
        state.collect(self.world.create_item("Salva Pass"), True)
        self.assertTrue(loc.can_reach(state))
        # A world-map-only type counts from the Final Save Point.
        self.assertEqual(self.rule_of(ROW["Sandworm first drop"])[1], [("Saved at the Final Save Point", ())])

    def test_force_fills_drops_first(self):
        fill(self)
        forced = [loc for loc in self.drop_locations() if loc.name in self.world.forced_drops]
        self.assertGreater(len(forced), 10)
        self.assertGreater(sum(loc.item.advancement for loc in forced), 5)
        for loc in self.drop_locations():
            if loc.item.advancement:
                self.assertNotEqual(self.rule_of(ROW[loc.name])[0], "none", loc.name)
        check_seed(self, self.world.seed_data())


class TestDropsNoKeysInDrops(SO2TestBase, DropChecks):
    options = {"enemy_dropsanity": "first_drop", "drop_progression": "force", "world_mode": "open_world",
               "area_keys_in_drops": False}

    def test_no_keys_in_drops(self):
        keys = [self.world.create_item(name) for name in ("Salva Pass", "North City Pass", "Lasguss Pass")]
        other = progression_item(self)
        allowed = 0
        for loc in self.drop_locations():
            for key in keys:
                self.assertFalse(loc.can_fill(self.multiworld.state, key, False), loc.name)
            allowed += loc.can_fill(self.multiworld.state, other, False)
        self.assertGreater(allowed, 10)     # other progression still goes there
        fill(self)
        for loc in self.drop_locations():
            self.assertFalse(loc.item.code in KEY_OF and loc.item.player == self.player, loc.name)


# ---- Craftsanity, no-fail crafting ----

class CraftChecks:
    """craftsanity: one location per craft its makers allow, never progression; a member-locked
    craft holds only filler unless a maker is sure to be in the party, and is left out when no
    maker can ever join."""

    def craft_locations(self):
        return [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "craft"]

    def test_crafts(self):
        self.check_crafts()

    def check_crafts(self):
        from ..locations import CRAFT_ROWS
        info = self.world.party_info
        crafts = self.craft_locations()
        self.assertEqual(len({ROW[loc.name]["item"] for loc in crafts}), len(crafts))
        want = [r for r in CRAFT_ROWS if r["makers"] is None or set(r["makers"]) & info.possible]
        self.assertEqual(sorted(loc.name for loc in crafts), sorted(r["name"] for r in want))
        other = progression_item(self)
        for loc in crafts:
            row = ROW[loc.name]
            self.assertFalse(loc.can_fill(self.multiworld.state, other, False), loc.name)
            locked = row["makers"] is not None and not set(row["makers"]) & info.guaranteed
            self.assertEqual(loc.progress_type == LocationProgressType.EXCLUDED, locked, loc.name)
            self.assertTrue(loc.can_reach(CollectionState(self.multiworld)), loc.name)
        self.assertEqual(len([i for i in self.multiworld.itempool if i.player == self.player]),
                         sum(1 for loc in own_locations(self) if not loc.locked))
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["version"], 4 if self.world.bench else 2)
        self.assertIs(seed["options"]["craftsanity"], True)
        return crafts


class TestCraftsanityStory(SO2TestBase, CraftChecks):
    options = {"craftsanity": True}

    def test_counts(self):
        from ..crafts import CRAFTS
        self.assertEqual(len(CRAFTS), 553)
        crafts = self.craft_locations()
        self.assertEqual(len(crafts), 553)      # story mode: any member may join
        # Leon's rare books, the portraits of members who may be missing...: filler only.
        self.assertEqual(self.world.get_location("Authoring: Treatise").progress_type, LocationProgressType.EXCLUDED)
        self.assertEqual(self.world.get_location("Art: Portrait A").progress_type, LocationProgressType.DEFAULT)
        self.assertEqual(self.world.get_location("Art: Portrait K").progress_type, LocationProgressType.DEFAULT)
        self.assertEqual(self.world.get_location("Art: Portrait H").progress_type, LocationProgressType.EXCLUDED)
        self.assertEqual(self.world.get_location("Customize: Aura Blade").progress_type, LocationProgressType.DEFAULT)
        self.assertEqual(self.world.get_location("Customize: Holy Cross").progress_type,
                         LocationProgressType.EXCLUDED)   # Ashton's
        self.assertEqual(self.world.party_info.rules, "story")


class TestCraftsanityOpenParty(SO2TestBase, CraftChecks):
    options = {"craftsanity": True, "no_fail_crafting": True, "world_mode": "open_world", "character_items": True,
               "story_route": "claude", "starting_character": "ashton", "exclude_characters": ["Leon", "Precis"]}

    def test_party_bound_crafts(self):
        names = {loc.name for loc in self.craft_locations()}
        self.assertNotIn("Authoring: Treatise", names)              # Leon's: he never joins
        self.assertIn("Customize: Holy Cross", names)                # Ashton's: the starting member
        self.assertEqual(self.world.get_location("Customize: Holy Cross").progress_type,
                         LocationProgressType.DEFAULT)
        self.assertEqual(self.world.get_location("Customize: Aura Blade").progress_type,
                         LocationProgressType.DEFAULT)               # Claude's: the hero
        self.assertIs(self.world.fill_slot_data()["no_fail_crafting"], True)


class TestNoFailOnly(SO2TestBase):
    options = {"no_fail_crafting": True}

    def test_version(self):
        self.assertEqual(self.world.seed_version(), 2)
        self.assertFalse(any(ROW[loc.name]["type"] == "craft" for loc in own_locations(self)))


# ---- Goals ----

class GoalChecks:
    """The goal's events and rules, the seed's goal object and gate door."""
    goal = 0

    def test_goal(self):
        from ..items import EVENT_FINAL_SAVE_POINT, EVENT_ISERIA_QUEEN, EVENT_LIMITER_OFF
        self.assertEqual(self.world.options.goal.value, self.goal)
        state = CollectionState(self.multiworld)
        for i in self.multiworld.precollected_items[self.player]:
            state.collect(i, True)
        indalecio = self.world.get_location("Indalecio")
        state.collect(self.world.get_location("Final Save Point (Fienal 9F)").item, True)
        self.assertTrue(state.has(EVENT_FINAL_SAVE_POINT, self.player))
        self.assertEqual(indalecio.access_rule(state), self.goal == 0)
        if self.goal >= 1:
            limiter = self.world.get_location("Limiter Off (Filia, Central City)")
            self.assertEqual(limiter.access_rule(state), not self.world.open_world)
            if self.world.open_world:
                state.collect(self.world.create_item("Central City Pass"), True)
                self.assertTrue(limiter.access_rule(state))
            state.collect(limiter.item, True)
            self.assertTrue(state.has(EVENT_LIMITER_OFF, self.player))
            self.assertEqual(indalecio.access_rule(state), self.goal == 1)
        if self.goal == 2:
            queen = self.world.get_location("Iseria Queen (Cave of Trials)")
            self.assertEqual(queen.parent_region.name, "Cave of Trials")
            if self.world.open_world:
                self.assertFalse(queen.access_rule(state))
                state.collect(self.world.create_item("Cave of Trials Pass"), True)
            self.assertTrue(queen.access_rule(state))
            state.collect(queen.item, True)
            self.assertTrue(state.has(EVENT_ISERIA_QUEEN, self.player))
            self.assertTrue(indalecio.access_rule(state))
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        gates = [d for d in seed["doors"] if d["id"] == 43978493]
        if self.goal == 0:
            self.assertEqual((seed["goal"], gates), ({"encounters": [539, 540]}, []))
            return
        self.assertEqual(seed["version"], 2)
        self.assertEqual(seed["goal"]["encounters"], [540])
        self.assertEqual(seed["goal"]["flags"], [445, 446])
        (gate,) = gates
        self.assertEqual((gate["routine"], gate["flags"]), ("0x3bec", [556]))
        self.assertEqual(len(gate["banners"]), 1 + (self.goal == 2))
        if self.goal == 2:
            self.assertEqual(seed["goal"]["all"], [561, 540])
            self.assertEqual(gate["won"], [561])
        table = doors_json()
        if table is not None:
            from ..doors import validate_doors
            self.assertEqual(validate_doors(seed["doors"], table), [])


class TestGoalAlias(SO2TestBase, GoalChecks):
    options = {"goal": "final_boss"}


class TestGoalLimiterOff(SO2TestBase, GoalChecks):
    options = {"goal": "indalecio_limiter_off"}
    goal = 1


class TestGoalIseriaOpen(SO2TestBase, GoalChecks):
    options = {"goal": "indalecio_limiter_off_and_iseria_queen", "world_mode": "open_world"}
    goal = 2


class TestGoalIseriaStoryCave(SO2TestBase, GoalChecks):
    options = {"goal": "indalecio_limiter_off_and_iseria_queen", "cave_of_trials": True, "story_route": "rena"}
    goal = 2


# ---- Killer-move checks ----

class KillerChecks:
    """killer_move_checks: the levels of the moves of members who can be in the party; access
    needs the member (their item when they join by one) and the stage for the learn level;
    progression only as killer_move_progression allows."""

    def killer_locations(self):
        return [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "killer_move"]

    def test_killer_moves(self):
        self.check_killer_moves()

    def check_killer_moves(self):
        from ..hell import arena_need, is_hell_seed, trained
        from ..items import EVENT_ENERGY_NEDE, EVENT_FINAL_SAVE_POINT
        from ..locations import KILLER_ROWS, fighter_status
        from ..rules import KILLER_EARLY_LEVEL
        info = self.world.party_info
        o = self.world.options
        locs = self.killer_locations()
        want = [r["name"] for r in KILLER_ROWS if fighter_status(r["character"], info) is not None]
        self.assertEqual(sorted(loc.name for loc in locs), sorted(want))
        other = progression_item(self)
        held = 0
        for loc in locs:
            row = ROW[loc.name]
            status = fighter_status(row["character"], info)
            # (encounter_rate 0 with killer_move_checks is a hell seed: the free arena trains.)
            self.assertEqual(loc.progress_type == LocationProgressType.EXCLUDED,
                             status == "maybe" or not trained(o), loc.name)
            mode = o.killer_move_progression.current_key
            ok = ((mode == "any" and status in ("sure", "item")) or
                  (mode == "hero" and row["character"] in info.story_members)) \
                and trained(o) and status != "maybe"
            self.assertEqual(loc.can_fill(self.multiworld.state, other, False), ok, loc.name)
            held += ok
            state = CollectionState(self.multiworld)
            if is_hell_seed(o):     # [hellseed] every level from the arena (story mode: the disc switch)
                self.assertEqual(arena_need(self.world), [EVENT_ENERGY_NEDE])
                stage = EVENT_ENERGY_NEDE
            else:
                stage = EVENT_ENERGY_NEDE if row["learn_level"] <= KILLER_EARLY_LEVEL else EVENT_FINAL_SAVE_POINT
            self.assertFalse(loc.can_reach(state), loc.name)
            event = "Disc Switch (Eluria Tower)" if stage == EVENT_ENERGY_NEDE else "Final Save Point (Fienal 9F)"
            state.collect(self.world.get_location(event).item, True)
            needs_item = status == "item" and mode == "any"
            self.assertEqual(loc.access_rule(state), not needs_item, loc.name)
            if needs_item:
                self.assertTrue(self.world.create_item(row["character"]).advancement)
                state.collect(self.world.create_item(row["character"]), True)
                self.assertTrue(loc.access_rule(state), loc.name)
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["version"], 4 if self.world.bench else 2)
        for l in seed["locations"]:
            if l["check"]["type"] == "killer_move":
                row = ROW[l["name"]]
                self.assertEqual(l["check"], {"type": "killer_move", "character": row["party_id"], "move": row["move"],
                                              "level": row["level"], "uses": row["uses"]})
        return locs, held


    def test_killer_move_items(self):
        """What a character item opens (assertAccessDependency, every location checked): with
        killer_move_progression any (or hero, for a hero who is an item), the killer-move levels
        of a member who joins by an item need the item (world.progression_characters); nothing
        else does."""
        from ..locations import fighter_status
        info = self.world.party_info
        for who in sorted(self.world.progression_characters):
            levels = [loc.name for loc in self.killer_locations() if ROW[loc.name]["character"] == who]
            with self.subTest(character=who):
                self.assertEqual(fighter_status(who, info), "item")
                self.assertTrue(levels)
                self.assertAccessDependency(levels, [[who]])


class TestKillerStory(SO2TestBase, KillerChecks):
    options = {"killer_move_checks": True}

    def test_counts(self):
        from ..locations import KILLER_ROWS
        from ..moves import MOVES
        self.assertEqual(len(MOVES), 69)
        self.assertEqual(len(KILLER_ROWS), 133)
        locs, held = self.check_killer_moves()
        self.assertEqual(len(locs), 133)     # story mode, no character items: every member may join
        self.assertEqual(held, 0)
        self.assertNotIn("Ashton: Dead Triangle level 1", {loc.name for loc in locs})
        self.assertEqual({ROW[loc.name]["level"] for loc in locs if "Secret Medicine" in loc.name}, {100, 200, 300})
        self.assertEqual(sum(loc.progress_type == LocationProgressType.DEFAULT for loc in locs),
                         sum(1 for r in KILLER_ROWS if r["character"] == "Claude"))


class TestKillerHeroOpen(SO2TestBase, KillerChecks):
    options = {"killer_move_checks": True, "killer_move_progression": "hero", "world_mode": "open_world",
               "story_route": "claude", "character_items": True, "starting_character": "chisato"}

    def test_hero(self):
        locs, held = self.check_killer_moves()
        self.assertEqual(held, sum(1 for loc in locs if ROW[loc.name]["character"] == "Claude"))


class TestKillerAnyItems(SO2TestBase, KillerChecks):
    options = {"killer_move_checks": True, "killer_move_progression": "any", "character_items": True,
               "story_route": "rena"}

    def test_any(self):
        locs, held = self.check_killer_moves()
        self.assertGreater(held, 30)
        self.assertTrue(self.world.progression_characters)     # test_killer_move_items has some
        chars = {ROW[loc.name]["character"] for loc in locs}
        self.assertLessEqual(chars, {"Claude"} | set(self.world.party))


class TestKillerAnyOpenBench(SO2TestBase, KillerChecks):
    """Open world with character_items and the bench (the default): every member is in the pool or
    the hero, so every killer-move level is in the world, and each pool member's need their item."""
    options = {"killer_move_checks": True, "killer_move_progression": "any", "world_mode": "open_world",
               "character_items": True, "story_route": "claude"}

    def test_every_member(self):
        from ..locations import KILLER_ROWS
        self.assertTrue(self.world.bench)
        locs, held = self.check_killer_moves()
        self.assertEqual(len(locs), len(KILLER_ROWS))
        self.assertEqual(held, len(locs))
        self.assertEqual(self.world.progression_characters, {r["character"] for r in KILLER_ROWS} - {"Claude"})


class TestKillerAnyOpenNoBench(SO2TestBase, KillerChecks):
    """The same without the bench: the 8 places' random pool; a member the draw left out has no
    killer-move levels."""
    options = {"killer_move_checks": True, "killer_move_progression": "any", "world_mode": "open_world",
               "character_items": True, "story_route": "claude", "bench": False}

    def test_left_out(self):
        self.assertFalse(self.world.bench)
        locs, held = self.check_killer_moves()
        self.assertEqual(len(self.world.party), 7)
        self.assertLessEqual({ROW[loc.name]["character"] for loc in locs}, {"Claude"} | set(self.world.party))


class TestKillerNoEncounters(SO2TestBase, KillerChecks):
    """encounter_rate 0 with killer_move_checks: a hell seed (hell.py, the user 2026-10-27). The
    levels count from the free Fun City arena (story mode: the disc switch stands for it), so the
    heroes' and Noel's may hold progression; the seed waives the arena's fee and says so."""
    options = {"killer_move_checks": True, "killer_move_progression": "any", "encounter_rate": 0}

    def test_hell(self):
        from ..hell import FEE_PATCH, is_hell_seed
        self.assertTrue(is_hell_seed(self.world.options))
        locs, held = self.check_killer_moves()
        self.assertEqual(held, len([loc for loc in locs if ROW[loc.name]["character"] in ("Claude", "Noel")]))
        self.assertGreater(held, 0)
        seed = self.world.seed_data()
        self.assertIn(FEE_PATCH, seed["patches"])
        self.assertIs(seed["options"]["hell_seed"], True)
        self.assertIs(self.world.fill_slot_data()["hell_seed"], True)
