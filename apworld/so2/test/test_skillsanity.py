"""Skillsanity (docs/apworld/apworld.md "Skillsanity"; docs/handoff/skillsanity.md) [skillsanity2]: the
guild checks, the learn points, the skill and move items, their logic and the shop prices."""
from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from .. import hell, skillsanity
from ..items import EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN, EVENT_FINAL_SAVE_POINT, SO2Item
from ..locations import ROW, fighter_status
from ..skills import LEARN, SKILL_SETS
from .bases import SO2TestBase, check_seed, fill, own_locations, own_pool, progression_item, rows_of


def skill_ways(test, loc):
    """Skillsanity: the ways the logic reaches a location, as the skill / move items each way
    needs (any one will do; events and character items aside): a craft one of its specialties'
    (skillsanity.specialty_needs: a sure member's starting skills, or the items), a learn point
    taught by an item (Machinery) Machinery's, the Iseria Queen Musical Talent's (the song), a
    killer-move level its move (special shuffled, not the member's first); anything else none."""
    world = test.world
    skills_on, _ = skillsanity.modes(world.options)
    if loc.address is None:
        if loc.name == "Iseria Queen (Cave of Trials)" and skills_on:
            return skillsanity.specialty_needs(world, "Musical Talent") or [()]
        return [()]
    row = ROW[loc.name]
    if row["type"] == "craft" and skills_on:
        ways = []
        for spec in row["specialties"]:
            found = skillsanity.specialty_needs(world, spec)
            if not found:
                return [()]
            ways += found
        return ways
    if row["type"] == "learn" and row.get("how") == "item" and skills_on:
        return skillsanity.specialty_needs(world, "Machinery") or [()]
    if row["type"] == "killer_move":
        return [tuple(skillsanity.move_need(world, row))]
    return [()]


def may_hold(test, loc):
    """What the design lets a skillsanity location hold (the user, 2026-10-27; docs/apworld/apworld.md
    "Skillsanity"): a guild check progression with battles (every set has a guild on Energy Nede
    that the late proof, or the hub proof, reached); a learn-table level of a member the logic
    counts on (sure, or joining by their item) with battles, unless EXCLUDED; the machines, the
    cave's learn chests and Float never. "With battles": random battles, or on a hell seed the
    free Fun City arena's (hell.trained) [hellseed]."""
    row = ROW[loc.name]
    battles = hell.trained(test.world.options)
    if row["type"] == "guild":
        return battles
    return (battles and row["how"] == "level" and loc.progress_type != LocationProgressType.EXCLUDED
            and fighter_status(row["character"], test.world.party_info) in ("sure", "item"))


def learn_need(test, row):
    """A learn-table level's needs by the design (skillsanity.learn_rule): the disc switch up to
    EARLY_LEVEL, else the Final Save Point (hub mode: nothing / Fienal open); open world under the
    story's party rules, Noel (not the start) early also Stage 15 (his house); a member who joins
    by their item, the item when the level may hold progression. A hell seed: every level from the
    Fun City arena (story mode the disc switch, open world Stage 16 done and the Fun City Pass,
    hub mode nothing) [hellseed]."""
    world = test.world
    early = row["level"] <= skillsanity.EARLY_LEVEL
    if hell.is_hell_seed(world.options):
        need = {"story": [EVENT_ENERGY_NEDE], "open": ["Stage 16 done", "Fun City Pass"],
                "hub": []}["hub" if world.hub else "open" if world.open_world else "story"]
    elif world.hub:
        need = [] if early else [EVENT_FIENAL_OPEN]
    else:
        need = [EVENT_ENERGY_NEDE if early else EVENT_FINAL_SAVE_POINT]
        party = world.party_info
        if world.open_world and early and row["character"] == "Noel" and party.rules == "story" \
                and party.start != "Noel":
            need.append("Stage 15 done")
    if fighter_status(row["character"], world.party_info) == "item" and may_hold(test, world.get_location(row["name"])):
        need.append(row["character"])
    return need


def state_with(test, names):
    """A state holding exactly these items or events (by name) of this world's, nothing swept."""
    state = CollectionState(test.multiworld)
    for name in names:
        item = test.world.create_item(name) if name in test.world.item_name_to_id \
            else SO2Item(name, ItemClassification.progression, None, test.player)
        state.collect(item, True)
    return state


def swept_without(test, names):
    """Every item of this world's but `names`, and the events that reaches (a sweep)."""
    state = CollectionState(test.multiworld)
    for item in test.multiworld.get_items():
        if item.code is not None and item.name not in names:
            state.collect(item, True)
    state.sweep_for_advancements()
    return state


def guild_checks(test, sets):
    """The guild checks of the named sets ("Knowledge 1", ...)."""
    return [loc.name for loc in own_locations(test) if ROW[loc.name]["type"] == "guild"
            and SKILL_SETS[ROW[loc.name]["set"]][0] in sets]


def learn_levels(test, who=None, early=None):
    """The learn-table levels (of `who`; up to EARLY_LEVEL or past it) of this world."""
    return [loc.name for loc in own_locations(test) if ROW[loc.name]["type"] == "learn"
            and ROW[loc.name]["how"] == "level" and (who is None or ROW[loc.name]["character"] == who)
            and (early is None or (ROW[loc.name]["level"] <= skillsanity.EARLY_LEVEL) == early)]


CHEAP = [s[0] for s in SKILL_SETS if s[1] < skillsanity.DEAR_PRICE]
DEAR = [s[0] for s in SKILL_SETS if s[1] >= skillsanity.DEAR_PRICE]


class SkillsanityChecks:
    """Every mix: the pool has one item per location; what a guild check or learn point may hold
    (may_hold) and a learn level's needs (learn_need); shop slots still hold no progression; the
    seed's checks and item kinds are the port's (seed_format.md "Skillsanity"); the generation
    fills."""

    def test_what_skillsanity_locations_hold(self):
        other = progression_item(self)
        mine = [self.world.create_item(n) for n in sorted(self.world.skill_needs)[:3]]
        st = self.multiworld.state
        held = 0
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["type"] in ("guild", "learn"):
                want = may_hold(self, loc)
                self.assertEqual(loc.can_fill(st, other, False), want, loc.name)
                held += want
            elif row["type"] == "shop":
                # The user (2026-10-25, 2026-10-27): item shops stay progression-free, this
                # world's own progression skill and move items too.
                self.assertFalse(loc.can_fill(st, other, False), loc.name)
                for item in mine:
                    self.assertFalse(loc.can_fill(st, item, False), (loc.name, item.name))
        o = self.world.options
        self.assertEqual(held > 0, any(skillsanity.modes(o)) and hell.trained(o))

    def test_learn_level_needs(self):
        """Each learn-table level is reached with exactly learn_need's items and events (each one
        taken away keeps it out), and its member's item is progression when it is needed."""
        for name in learn_levels(self):
            loc = self.world.get_location(name)
            need = learn_need(self, ROW[name])
            with self.subTest(location=name):
                self.assertTrue(loc.access_rule(state_with(self, need)), need)
                for k in range(len(need)):
                    self.assertFalse(loc.access_rule(state_with(self, need[:k] + need[k + 1:])), need[k])
                for who in need[-1:]:
                    if who in self.world.item_name_to_id:
                        self.assertTrue(self.world.create_item(who).advancement, who)

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

    def test_skill_items(self):
        """What each skill or move item the logic needs opens (assertAccessDependency, every
        location checked): the locations whose every way needs it (skill_ways); nothing else.
        With skills or specials shuffled and something needing them, at least one opens some."""
        ways = {loc.name: skill_ways(self, loc) for loc in self.multiworld.get_locations(self.player)}
        pool = {i.name for i in own_pool(self)}
        opened = 0
        for name in sorted(self.world.skill_needs):
            self.assertIn(name, pool)
            needs = [n for n, w in ways.items() if all(name in k for k in w)]
            with self.subTest(item=name):
                self.assertAccessDependency(needs, [[name]])
            opened += bool(needs)
        self.assertEqual(opened > 0, bool(self.world.skill_needs))

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
        # The guide's way: each skill alone keeps the Queen out, and without them the goal too.
        for n in ("Skill: Music Instrument", "Skill: Musical Notation"):
            with self.subTest(item=n):
                self.assertAccessDependency([loc.name], [[n]], only_check_listed=True)
        self.assertSweptAccessDependency([loc.name, "Indalecio"], [["Skill: Music Instrument", "Skill: Musical Notation"]])


# ---- Progression at guild checks and learn points (the user, 2026-10-27; docs/handoff/skilllogic.md)
class TestGuildsOpenWorld(SO2TestBase, SkillsanityChecks):
    """Open world with the teleporter and expel_progression on: a guild check counts through each
    of its set's guilds that may hold progression (a first drop's places), never one that closes
    at the El voyage (Port of Clik's); a dear set (DEAR_PRICE) through its Expel guilds only from
    the end of stage FOL_STAGE (the Fol)."""
    options = {"world_mode": "open_world", "skillsanity": "skills_only", "story_route": "claude"}

    def test_guild_places(self):
        # Knowledge 1: Cross, Port of Clik, Linga, Central City. Clik's Pass alone doesn't reach it
        # (collected by assertAccessDependency's first half); any one of the others does.
        names = guild_checks(self, ["Knowledge 1"])
        self.assertEqual(len(names), 3)
        self.assertAccessDependency(names, [["Cross Pass"], ["Linga Pass"], ["Central City Pass"]],
                                    only_check_listed=True)

    def test_fol_tiers(self):
        self.assertEqual(skillsanity.FOL_STAGE, 9)
        dear = guild_checks(self, DEAR)
        cheap = guild_checks(self, CHEAP)
        self.assertEqual((len(dear), len(cheap)), (15, 30))
        # The dear sets' first guilds (Lacour, stage 6; Linga, 8) count from stage 9 (Hoffmann
        # Ruins); the cheap sets from their guilds' towns (Cross, Herlie: stages 3 and 5).
        self.assertSweptAccessDependency(dear, [["Hoffmann Pass"]])
        state = swept_without(self, ["Hoffmann Pass"])
        for name in cheap:
            self.assertTrue(self.can_reach_in(state, name), name)


class TestGuildsOpenWorldNoExpel(SO2TestBase, SkillsanityChecks):
    """Open world with expel_progression off: Expel holds only our Expel-story keys, so a guild
    check counts only through its set's Energy Nede guild (Central City, Armlock or North City;
    the North City Pass is given at the start)."""
    options = {"world_mode": "open_world", "skillsanity": "insanity", "expel_progression": False,
               "story_route": "rena"}

    def test_nede_guilds(self):
        self.assertAccessDependency(guild_checks(self, ["Knowledge 1", "Sensibility 1", "Technique 1", "Knowledge 2"]),
                                    [["Central City Pass"]], only_check_listed=True)
        self.assertAccessDependency(guild_checks(self, ["Combat 1", "Combat 2", "Sensibility 3", "Combat 3"]),
                                    [["Armlock Pass"]], only_check_listed=True)


class StoryTierChecks:
    """Story mode (no stage events): a cheap set counts from the start through an Expel guild the
    late proof reached (expel_progression on), every other way from the disc switch."""

    def test_story_tiers(self):
        expel = bool(self.world.options.expel_progression)
        empty = state_with(self, [])
        switch = state_with(self, [EVENT_ENERGY_NEDE])
        for name in guild_checks(self, CHEAP + DEAR):
            cheap = SKILL_SETS[ROW[name]["set"]][0] in CHEAP
            with self.subTest(location=name):
                self.assertEqual(self.can_reach_in(empty, name), cheap and expel)
                self.assertTrue(self.can_reach_in(switch, name))


class TestGuildsStory(SO2TestBase, SkillsanityChecks, StoryTierChecks):
    options = {"skillsanity": "skills_only", "craftsanity": True}


class TestGuildsStoryNoExpel(SO2TestBase, SkillsanityChecks, StoryTierChecks):
    options = {"skillsanity": "insanity", "expel_progression": False, "story_route": "claude"}

    def test_noel_from_the_switch(self):
        # Story mode: Noel joins at his house with no item; the disc switch stands for it.
        names = learn_levels(self, "Noel", early=True)
        self.assertEqual(len(names), 8)
        for name in names:
            self.assertFalse(self.can_reach_in(state_with(self, []), name), name)
            self.assertTrue(self.can_reach_in(state_with(self, [EVENT_ENERGY_NEDE]), name), name)


class TestLearnOpenWorld(SO2TestBase, SkillsanityChecks):
    """Open world under the story's party rules: a learn level counts as killer moves do, early ones
    from the disc switch, the rest from the Final Save Point; Noel's early ones also need his house
    (stage 15). No mix makes that a key of its own today: the Noel's House Pass is at the start
    unless the teleporter and expel_progression are on, and then the El voyage needs it (the
    teleporter's gate keys); so the event states show it."""
    options = {"world_mode": "open_world", "skillsanity": "special_only", "story_route": "claude"}

    def test_noel_needs_his_house(self):
        noel = learn_levels(self, "Noel", early=True)
        self.assertEqual(len(noel), 8)
        switch = state_with(self, [EVENT_ENERGY_NEDE])
        house = state_with(self, [EVENT_ENERGY_NEDE, "Stage 15 done"])
        for name in noel:
            self.assertFalse(self.can_reach_in(switch, name), name)
            self.assertTrue(self.can_reach_in(house, name), name)
        for name in learn_levels(self, "Claude", early=True) + learn_levels(self, "Rena", early=True):
            self.assertTrue(self.can_reach_in(switch, name), name)

    def test_learn_tiers(self):
        early = learn_levels(self, "Claude", early=True) + learn_levels(self, "Rena", early=True)
        late = [n for n in learn_levels(self, early=False) if may_hold(self, self.world.get_location(n))]
        self.assertTrue(early and late)
        # The disc switch needs Eluria Tower (stage 12); the Final Save Point Mihne Cavern (21).
        self.assertSweptAccessDependency(early, [["Eluria Tower Pass"]])
        self.assertSweptAccessDependency(late, [["Mihne Pass"]])
        state = swept_without(self, ["Mihne Pass"])
        for name in early:
            self.assertTrue(self.can_reach_in(state, name), name)


class TestLearnCharacterItems(SO2TestBase, SkillsanityChecks):
    """Open world with character_items: the seven members of the pool join by their items, so
    their learn levels need them and the items are progression (the user, 2026-10-27)."""
    options = {"world_mode": "open_world", "character_items": True, "skillsanity": "special_only",
               "story_route": "rena", "exclude_characters": ["Bowman", "Dias", "Leon", "Ernest"]}

    def test_character_items(self):
        self.assertEqual(set(self.world.party),
                         {"Claude", "Celine", "Precis", "Ashton", "Opera", "Noel", "Chisato"})
        self.assertEqual(skillsanity.learn_characters(self.world), set(self.world.party))
        self.assertLessEqual(set(self.world.party), self.world.progression_characters)
        for who in sorted(self.world.party):
            with self.subTest(character=who):
                self.assertTrue(self.world.create_item(who).advancement)
                self.assertAccessDependency(learn_levels(self, who), [[who]])


class TestLearnHub(SO2TestBase, SkillsanityChecks):
    """Hub mode: the cheap sets and the early learn levels from the start, the dear sets and the
    later levels once Fienal opens."""
    options = {"world_mode": "hub", "skillsanity": "insanity", "story_route": "claude"}

    def test_hub_tiers(self):
        empty = state_with(self, [])
        fienal = state_with(self, [EVENT_FIENAL_OPEN])
        for name in guild_checks(self, CHEAP):
            self.assertTrue(self.can_reach_in(empty, name), name)
        for name in guild_checks(self, DEAR):
            self.assertFalse(self.can_reach_in(empty, name), name)
            self.assertTrue(self.can_reach_in(fienal, name), name)
        for name in learn_levels(self, "Claude", early=True):
            self.assertTrue(self.can_reach_in(empty, name), name)
        for name in learn_levels(self, "Claude", early=False):
            self.assertFalse(self.can_reach_in(empty, name), name)
            self.assertTrue(self.can_reach_in(fienal, name), name)


class TestSkillsanityNoEncounters(SO2TestBase, SkillsanityChecks):
    """encounter_rate 0 with skillsanity's guild checks only (skills_only) and no killer-move checks:
    not a hell seed (nothing needs levels or uses), no battles, so no Fol for a guild: no guild check
    holds progression (may_hold), the arena's fee stays (check_seed)."""
    options = {"skillsanity": "skills_only", "encounter_rate": 0, "cave_of_trials": True}

    def test_not_hell(self):
        self.assertFalse(hell.is_hell_seed(self.world.options))
        self.assertFalse(hell.trained(self.world.options))


class TestSkillsanityHellInsanity(SO2TestBase, SkillsanityChecks):
    """encounter_rate 0 with skillsanity insanity (learn points): a hell seed (hell.py). Guild checks
    and learn points count from the free Fun City arena (story mode: the disc switch) and may hold
    progression (may_hold, learn_need)."""
    options = {"skillsanity": "insanity", "encounter_rate": 0, "cave_of_trials": True}

    def test_hell(self):
        self.assertTrue(hell.is_hell_seed(self.world.options))
        empty = state_with(self, [])
        switch = state_with(self, [EVENT_ENERGY_NEDE])
        for name in guild_checks(self, CHEAP + DEAR):
            with self.subTest(location=name):
                self.assertFalse(self.can_reach_in(empty, name))
                self.assertTrue(self.can_reach_in(switch, name))


class TestSkillsanitySeed75(SO2TestBase, SkillsanityChecks):
    """The release workflow's solo_noexpel seed 75 (docs/handoff/ncpfix.md, skilllogic.md): open
    world in Rena's story with expel_progression, boss_checks and the teleporter off, skillsanity
    special_only and killer moves for the hero. 0.2.1 before this change: 60 move items
    progression and only 63 locations that might hold one, every fill a FillError ("Not enough
    locations for progression items"); learn levels now hold progression too. WorldTestBase's
    test_fill and test_seed are the reproducer."""
    options = {"goal": "indalecio_limiter_off", "world_mode": "open_world", "expel_teleporter": False,
               "shuffle_shops": "full", "story_route": "rena", "starting_character": "noel",
               "exclude_characters": ["Bowman", "Ernest"], "missable_locations": "exclude",
               "expel_progression": False, "boss_checks": False, "killer_move_checks": True,
               "killer_move_progression": "hero", "skillsanity": "special_only", "skillsanity_shop_share": 50,
               "traps": 25, "fol_multiplier": 5, "drop_progression": "force", "no_fail_crafting": True}

    def test_capacity(self):
        moves = [i for i in own_pool(self) if i.advancement and i.name in skillsanity.MOVE_ITEMS]
        self.assertEqual(len(moves), 60)
        sample = moves[0]
        st = self.multiworld.state
        room = [loc for loc in own_locations(self) if loc.item is None and loc.can_fill(st, sample, False)]
        self.assertGreater(len(room), 100)
