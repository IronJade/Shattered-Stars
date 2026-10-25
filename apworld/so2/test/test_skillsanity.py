"""Skillsanity (docs/ap/apworld.md "Skillsanity"; docs/handoff/skillsanity.md) [skillsanity2]: the
guild checks, the learn points, the skill and move items, their logic and the shop prices."""
from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from .. import skillsanity
from ..locations import ROW, fighter_status
from ..skills import LEARN, SKILL_SETS
from .bases import SO2TestBase
from .test_world import check_seed, fill, own_locations, own_pool


def rows_of(test, kind):
    return [ROW[loc.name] for loc in own_locations(test) if ROW[loc.name]["type"] == kind]


def progression_item(test):
    from ..items import SO2Item
    return SO2Item("test key", ItemClassification.progression, None, test.player + 1)


class SkillsanityChecks:
    """Every mix: the pool has one item per location; a guild check or learn point never holds
    progression; the seed's checks and item kinds are the port's (seed_format.md "Skillsanity");
    the generation fills."""

    def test_no_progression_in_skillsanity_locations(self):
        other = progression_item(self)
        st = self.multiworld.state
        for loc in own_locations(self):
            if ROW[loc.name]["type"] in ("guild", "learn"):
                self.assertFalse(loc.can_fill(st, other, False), loc.name)

    def test_pool_matches(self):
        skills_on, specials_on = skillsanity.modes(self.world.options)
        names = [i.name for i in own_pool(self)] + [loc.item.name for loc in own_locations(self)
                                                     if loc.item is not None and loc.item.player == self.player]
        guild = rows_of(self, "guild")
        learn = rows_of(self, "learn")
        self.assertEqual(len(guild), 45 if skills_on else 0)
        if not specials_on:
            self.assertFalse([r for r in learn if r.get("how") != "float"])
        for row in guild + learn:
            self.assertIn(skillsanity.pool_name(row), names, row["name"])
        # No first move is an item or a location.
        for who, index in skillsanity.FIRST.items():
            self.assertNotIn(skillsanity.move_item(who, index), skillsanity.MOVE_ITEMS)

    def test_seed(self):
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        kinds = {v["kind"] for v in seed["items"].values()}
        skills_on, specials_on = skillsanity.modes(self.world.options)
        self.assertEqual("skill" in kinds, skills_on)
        self.assertEqual("move" in kinds, specials_on)
        for l in seed["locations"]:
            c = l["check"]
            if c["type"] == "guild":
                self.assertTrue(0 <= c["set"] < 12 and 0 <= c["index"] < len(SKILL_SETS[c["set"]][2]))
            if c["type"] == "learn" and "skill" not in c:
                self.assertTrue(1 <= c["character"] <= 12 and 0 <= c["index"] < 32)
            if "price" in l:
                self.assertEqual(l["check"]["type"], "shop")
                self.assertIn(l["item"]["kind"], ("skill", "move"))
                self.assertEqual(l["price"], skillsanity.price_of(l["item"]["ap_id"] if "ap_id" in l["item"]
                                                                   else self.world.item_name_to_id[l["item"]["name"]]))
        if any(skillsanity.modes(self.world.options)):
            self.assertGreaterEqual(seed["version"], 2)
        self.assertEqual(seed["options"]["skillsanity"], self.world.options.skillsanity.current_key)


class TestSkillsanityNone(SO2TestBase, SkillsanityChecks):
    options = {"craftsanity": True}

    def test_nothing_new(self):
        self.assertFalse(rows_of(self, "guild") or rows_of(self, "learn"))
        self.assertFalse([i for i in own_pool(self) if skillsanity.is_skillsanity_item(i.code)])
        # Crafts need nothing (the guilds sell the sets as ever).
        for loc in own_locations(self):
            if ROW[loc.name]["type"] == "craft":
                self.assertTrue(loc.can_reach(CollectionState(self.multiworld)), loc.name)


class TestSkillsOnlyCrafts(SO2TestBase, SkillsanityChecks):
    options = {"skillsanity": "skills_only", "craftsanity": True}

    def test_specialties(self):
        state = CollectionState(self.multiworld)
        need = self.world.skill_needs
        # Cooking: Rena joins with its skills (a sure member here): a Cooking craft needs no item.
        cooking = next(loc for loc in own_locations(self) if ROW[loc.name]["type"] == "craft"
                       and ROW[loc.name]["specialties"] == ["Cooking"])
        self.assertTrue(cooking.can_reach(state))
        # Customize (Opera's own; she is no sure member): the three skill items.
        for n in ("Skill: Craft", "Skill: Metal Casting", "Skill: Functionality"):
            self.assertIn(n, need)
            self.assertTrue(self.world.create_item(n).advancement)
        craft = next(loc for loc in own_locations(self) if loc.name.startswith("Customize: ")
                     and ROW[loc.name]["specialties"] == ["Customize"])
        self.assertFalse(craft.can_reach(state))
        for n in ("Skill: Craft", "Skill: Metal Casting", "Skill: Functionality"):
            state.collect(self.world.create_item(n), True)
        self.assertTrue(craft.can_reach(state))
        self.assertFalse(self.world.create_item("Skill: Motormouth").advancement)


class TestSpecialOnlyOpenWorld(SO2TestBase, SkillsanityChecks):
    options = {"skillsanity": "special_only", "killer_move_checks": True, "world_mode": "open_world",
               "character_items": True, "story_route": "claude", "starting_character": "ashton"}

    def test_learn_points(self):
        party = self.world.party_info
        learn = rows_of(self, "learn")
        want = [r for r in skillsanity.LEARN_ROWS if r["how"] != "cave"
                and fighter_status(r["character"], party) is not None]
        self.assertEqual(sorted(r["name"] for r in learn), sorted(r["name"] for r in want))
        firsts = {w: i for w, _, i, _, _, first in LEARN if first}
        for row in learn:
            self.assertNotEqual(firsts[row["character"]], row["move_index"], row["name"])

    def test_killer_moves_need_the_move(self):
        state = CollectionState(self.multiworld)
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["type"] != "killer_move" or row["character"] != "Claude":
                continue
            item = skillsanity.move_item("Claude", row["move"] - 1)
            if skillsanity.FIRST["Claude"] == row["move"] - 1:
                self.assertNotIn(item, self.world.skill_needs)
                continue
            self.assertIn(item, self.world.skill_needs)
            self.assertTrue(self.world.create_item(item).advancement)


class TestInsanityHubShops(SO2TestBase, SkillsanityChecks):
    options = {"skillsanity": "insanity", "skillsanity_shop_share": 50, "world_mode": "hub",
               "shuffle_shops": "full", "cave_of_trials": True, "story_route": "rena"}

    def test_shops_and_float(self):
        names = {loc.name for loc in own_locations(self)}
        self.assertIn("Cave of Trials Level 11: Float", names)
        locked = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "shop" and loc.item is not None
                  and skillsanity.is_skillsanity_item(loc.item.code)]
        self.assertTrue(locked)
        for loc in locked:
            self.assertFalse(loc.item.advancement, loc.name)
            self.assertEqual(loc.progress_type, LocationProgressType.DEFAULT)


class TestInsanityIseriaQueen(SO2TestBase, SkillsanityChecks):
    options = {"skillsanity": "insanity", "goal": "indalecio_limiter_off_and_iseria_queen",
               "cave_of_trials": True, "killer_move_checks": True, "craftsanity": True}

    def test_the_song_needs_musical_talent(self):
        for n in ("Skill: Music Instrument", "Skill: Musical Notation"):
            self.assertIn(n, self.world.skill_needs)
        loc = self.world.get_location("Iseria Queen (Cave of Trials)")
        state = CollectionState(self.multiworld)
        for item in self.multiworld.itempool:
            if item.player == self.player and item.name not in ("Skill: Music Instrument", "Skill: Musical Notation"):
                state.collect(item, True)
        state.sweep_for_advancements()
        self.assertFalse(loc.can_reach(state))
        state.collect(self.world.create_item("Skill: Music Instrument"), True)
        state.collect(self.world.create_item("Skill: Musical Notation"), True)
        state.sweep_for_advancements()
        self.assertTrue(loc.can_reach(state))
