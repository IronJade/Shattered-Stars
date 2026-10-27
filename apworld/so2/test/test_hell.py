"""The hell seed (hell.py; the user, 2026-10-27; docs/ap/apworld.md "The hell seed"; docs/handoff/
hellseed.md): encounter_rate 0 with killer-move checks or skillsanity's learn points. The Fun City
arena is then the only place to train: the seed waives its fee (the patch line) and says so
(`hell_seed`), and killer-move levels, learn points and guild checks count from the arena (story
mode the disc switch, open world Stage 16 done and the Fun City Pass, hub mode the start), not from
the Final Save Point or Fienal. Every other seed keeps the fee and its rules."""
import unittest
from types import SimpleNamespace

from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from .. import hell, skillsanity
from ..data import TELEPORTER_STAGE
from ..items import EVENT_ENERGY_NEDE, EVENT_FIENAL_OPEN, EVENT_FINAL_SAVE_POINT, KEY_NAME, SO2Item, stage_event
from ..locations import ROW, fighter_status
from ..options import EncounterRate, KillerMoveChecks, Skillsanity
from .bases import PATCH, SO2TestBase, check_seed, fill, own_locations, own_pool, progression_item
from .test_skillsanity import CHEAP, DEAR, SkillsanityChecks, guild_checks, learn_levels, may_hold, swept_without
from .test_v2 import KillerChecks

# The release workflow's solo_noexpel seed 75 (test_skillsanity.TestSkillsanitySeed75).
SEED75 = {"goal": "indalecio_limiter_off", "world_mode": "open_world", "expel_teleporter": False,
          "shuffle_shops": "full", "story_route": "rena", "starting_character": "noel",
          "exclude_characters": ["Bowman", "Ernest"], "missable_locations": "exclude",
          "expel_progression": False, "boss_checks": False, "killer_move_checks": True,
          "killer_move_progression": "hero", "skillsanity": "special_only", "skillsanity_shop_share": 50,
          "traps": 25, "fol_multiplier": 5, "drop_progression": "force", "no_fail_crafting": True}


class TestHellRule(unittest.TestCase):
    """is_hell_seed / trained over the options, and the fee's patch line."""

    def test_rule(self):
        for enc in (0, 1, 100):
            for killer in (False, True):
                for sk in range(4):
                    o = SimpleNamespace(encounter_rate=EncounterRate(enc), killer_move_checks=KillerMoveChecks(killer),
                                        skillsanity=Skillsanity(sk))
                    want = enc == 0 and (killer or sk in (2, 3))   # special_only, insanity: learn points
                    with self.subTest(encounter_rate=enc, killer_move_checks=killer, skillsanity=sk):
                        self.assertEqual(hell.is_hell_seed(o), want)
                        self.assertEqual(hell.trained(o), enc > 0 or want)

    def test_fee_patch(self):
        # Map 297's word 0x177f (offset 0x5dfc): `push 0x7d0` (2000 Fol) -> `push 0`.
        m = PATCH.match(hell.FEE_PATCH)
        self.assertIsNotNone(m)
        self.assertEqual((int(m[1]), int(m[2], 16), int(m[3], 16), int(m[4], 16)),
                         (297, 0x177f, 0x120007d0, 0x12000000))
        self.assertEqual(0x177f * 4, 0x5dfc)


def all_but_events(test, names=()):
    """Every item of this world's but `names`, no events (not swept)."""
    state = CollectionState(test.multiworld)
    for item in test.multiworld.get_items():
        if item.player == test.player and item.code is not None and item.name not in names:
            state.collect(item, True)
    return state


def with_events(test, state, events):
    state = state.copy()
    for name in events:
        state.collect(SO2Item(name, ItemClassification.progression, None, test.player), True)
    return state


def trained_locations(test):
    """The locations the arena trains for: killer-move levels, learn-table levels, guild checks."""
    return [loc.name for loc in own_locations(test)
            if ROW[loc.name]["type"] in ("killer_move", "guild")
            or (ROW[loc.name]["type"] == "learn" and ROW[loc.name].get("how") == "level")]


class HellChecks:
    """Every hell seed: the rule holds; the arena's needs; what the trained locations may hold; the
    seed's patch line and `hell_seed` (check_seed)."""
    arena = None    # the mode's arena_need

    def test_hell_seed(self):
        self.assertTrue(hell.is_hell_seed(self.world.options))
        self.assertTrue(hell.trained(self.world.options))
        self.assertEqual(hell.arena_need(self.world), self.arena)

    def test_what_trained_locations_hold(self):
        """Killer-move levels as killer_move_progression allows (as with battles); learn levels and
        guild checks as may_hold (with battles)."""
        info = self.world.party_info
        mode = self.world.options.killer_move_progression.current_key
        other = progression_item(self)
        st = self.multiworld.state
        held = 0
        for loc in own_locations(self):
            row = ROW[loc.name]
            if row["type"] == "killer_move":
                status = fighter_status(row["character"], info)
                want = ((mode == "any" and status in ("sure", "item"))
                        or (mode == "hero" and row["character"] in info.story_members)) and status != "maybe"
                self.assertEqual(loc.progress_type == LocationProgressType.EXCLUDED, status == "maybe", loc.name)
            elif row["type"] in ("guild", "learn"):
                want = may_hold(self, loc)
            else:
                continue
            self.assertEqual(loc.can_fill(st, other, False), want, loc.name)
            held += want
        self.assertGreater(held, 0)

    def test_hell_seed_data(self):
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)     # the patch line and `hell_seed` iff a hell seed
        self.assertIn(hell.FEE_PATCH, seed["patches"])
        self.assertIs(seed["options"]["hell_seed"], True)
        self.assertIs(self.world.fill_slot_data()["hell_seed"], True)
        self.assertGreaterEqual(seed["version"], 2)


class TestHellSeed75(SO2TestBase, HellChecks):
    """Seed 75's mix with encounter_rate 0: 0.2.1 before the hell seed failed every fill (killer
    levels EXCLUDED, their moves still progression). WorldTestBase's test_fill is the reproducer."""
    options = dict(SEED75, encounter_rate=0)
    arena = [stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"]]

    def test_arena_open_world(self):
        """Every killer-move and learn level needs the Cave of Red Crystal's stage (16, the Synard;
        the Fun City Pass is a start key), not the Final Save Point (Mihne Cavern, stage 21)."""
        self.assertIn("Fun City Pass", [i.name for i in self.multiworld.precollected_items[self.player]])
        names = trained_locations(self)
        self.assertTrue(names)
        self.assertSweptAccessDependency(names, [["Red Crystal Pass"]])
        state = swept_without(self, ["Mihne Pass"])
        self.assertFalse(state.has(EVENT_FINAL_SAVE_POINT, self.player))
        for name in names:
            self.assertTrue(self.can_reach_in(state, name), name)

    def test_capacity(self):
        moves = [i for i in own_pool(self) if i.advancement and i.name in skillsanity.MOVE_ITEMS]
        self.assertEqual(len(moves), 60)
        st = self.multiworld.state
        room = [loc for loc in own_locations(self) if loc.item is None and loc.can_fill(st, moves[0], False)]
        self.assertGreater(len(room), 100)
        # (Rena's story, killer moves for the hero: Rena has no killer moves; the room is her
        # learn levels and the other members', counted from the arena.)
        self.assertGreater(len([n for n in trained_locations(self) if ROW[n]["type"] == "learn"
                                and self.world.get_location(n).can_fill(st, moves[0], False)]), 30)


class TestHellSeed75Claude(SO2TestBase, HellChecks):
    """Seed 75's neighbour: Claude's story."""
    options = dict(SEED75, encounter_rate=0, story_route="claude")
    arena = [stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"]]


class TestHellOpenGuilds(SO2TestBase, HellChecks, SkillsanityChecks):
    """Open world with the teleporter, skills_only and killer moves: guild checks count from the
    arena too (its fights pay Fol), through each of their places' ways no sooner than stage 16, an
    Expel place's own early way left out (Expel closes at the El voyage)."""
    options = {"world_mode": "open_world", "skillsanity": "skills_only", "killer_move_checks": True,
               "killer_move_progression": "any", "story_route": "claude", "encounter_rate": 0}
    arena = [stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"]]

    def test_arena_guilds(self):
        names = trained_locations(self)
        guilds = guild_checks(self, CHEAP + DEAR)
        self.assertEqual(len(guilds), 45)
        self.assertSweptAccessDependency(names, [["Red Crystal Pass"]])
        state = swept_without(self, ["Mihne Pass"])
        for name in names:
            self.assertTrue(self.can_reach_in(state, name), name)
        # Knowledge 1 (Cross, Port of Clik, Linga, Central City): with battles the Cross Pass alone
        # reaches it (test_skillsanity.TestGuildsOpenWorld); here not before stage 16.
        state = swept_without(self, ["Red Crystal Pass"])
        for name in guild_checks(self, ["Knowledge 1"]):
            self.assertFalse(self.can_reach_in(state, name), name)
        # With the teleporter, Cross's guild (Expel) counts through it from stage 16: the Central
        # City Pass isn't needed.
        state = swept_without(self, ["Central City Pass"])
        for name in guild_checks(self, ["Knowledge 1"]):
            self.assertTrue(self.can_reach_in(state, name), name)


class TestHellOpenGuildsNoTeleporter(SO2TestBase, HellChecks, SkillsanityChecks):
    """Open world without the teleporter: an Expel guild's own early way is left out (Expel closes at
    the El voyage, before the arena opens), so Knowledge 1 (Cross, Port of Clik, Linga, Central City)
    counts only through Central City (the Central City Pass) or Expel after the Final Save Point."""
    options = {"world_mode": "open_world", "expel_teleporter": False, "skillsanity": "skills_only",
               "killer_move_checks": True, "story_route": "claude", "encounter_rate": 0}
    arena = [stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"]]

    def test_arena_guilds(self):
        """Every guild check and killer-move level needs stage 16 (the Red Crystal Pass), North
        City's sets (stage 13 with battles: Sensibility 2, Technique 2 and 3, Knowledge 3) too.
        (With the teleporter and expel_progression on the El voyage needs the keys up to the
        Synard anyway, so TestHellOpenGuilds can't tell.)"""
        names = trained_locations(self)
        self.assertTrue(guild_checks(self, ["Sensibility 2", "Technique 2", "Knowledge 3", "Technique 3"]))
        self.assertSweptAccessDependency(names, [["Red Crystal Pass"]])

    def test_expel_guilds_after_the_voyage(self):
        names = guild_checks(self, ["Knowledge 1"])
        self.assertEqual(len(names), 3)
        # (Without the Central City Pass the story stops at stage 17: no Final Save Point either.)
        self.assertSweptAccessDependency(names, [["Central City Pass"]])


class TestHellStory(SO2TestBase, HellChecks, SkillsanityChecks):
    """Story mode, killer moves for anyone and skillsanity insanity: every level and guild check from
    the disc switch (it stands for the Synard), none from the Final Save Point."""
    options = {"killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "insanity",
               "story_route": "claude", "encounter_rate": 0}
    arena = [EVENT_ENERGY_NEDE]

    def test_arena_story(self):
        names = trained_locations(self)
        self.assertTrue(guild_checks(self, DEAR) and learn_levels(self, early=False))
        items = all_but_events(self)
        switch = with_events(self, items, [EVENT_ENERGY_NEDE])
        for name in names:
            with self.subTest(location=name):
                self.assertFalse(self.can_reach_in(items, name))
                self.assertTrue(self.can_reach_in(switch, name))


class TestHellHub(SO2TestBase, HellChecks, SkillsanityChecks):
    """Hub mode (the arena is open from the start): every level and guild check from the start,
    the dear sets and the late levels too (with battles: once Fienal opens)."""
    options = {"world_mode": "hub", "killer_move_checks": True, "killer_move_progression": "any",
               "skillsanity": "insanity", "story_route": "claude", "encounter_rate": 0}
    arena = []

    def test_arena_hub(self):
        names = trained_locations(self)
        self.assertTrue(guild_checks(self, DEAR) and learn_levels(self, early=False))
        items = all_but_events(self)
        self.assertFalse(items.has(EVENT_FIENAL_OPEN, self.player))
        for name in names:
            with self.subTest(location=name):
                self.assertTrue(self.can_reach_in(items, name))


class TestNotHellLowEncounters(SO2TestBase, KillerChecks):
    """encounter_rate 1 (random battles, rare): an ordinary seed. The killer-move levels keep their
    stages (KillerChecks: the disc switch / the Final Save Point), no patch line, no `hell_seed`."""
    options = {"killer_move_checks": True, "killer_move_progression": "any", "encounter_rate": 1}

    def test_not_hell(self):
        self.assertFalse(hell.is_hell_seed(self.world.options))
        self.assertNotIn("hell_seed", self.world.fill_slot_data())


class TestNotHellLowEncountersSkills(SO2TestBase, SkillsanityChecks):
    """encounter_rate 1 with skillsanity insanity: an ordinary seed; learn levels and guild checks
    keep their tiers (learn_need: the disc switch / the Final Save Point; check_seed: no patch line,
    no `hell_seed`)."""
    options = {"skillsanity": "insanity", "encounter_rate": 1}

    def test_not_hell(self):
        self.assertFalse(hell.is_hell_seed(self.world.options))
        self.assertTrue(learn_levels(self, early=False))
        for name in learn_levels(self, early=False):
            self.assertFalse(self.can_reach_in(with_events(self, all_but_events(self), [EVENT_ENERGY_NEDE]), name),
                             name)


class TestHellMarks(SO2TestBase):
    """The patch line and `hell_seed` are in a seed if and only if it is a hell seed, over mixes of
    encounter_rate, killer_move_checks, skillsanity and world_mode."""
    options = {}
    run_default_tests = False

    def test_marks(self):
        mixes = [
            ({"encounter_rate": 0, "killer_move_checks": True}, True),
            ({"encounter_rate": 0, "skillsanity": "special_only"}, True),
            ({"encounter_rate": 0, "skillsanity": "insanity", "world_mode": "hub"}, True),
            ({"encounter_rate": 0, "skillsanity": "skills_only", "killer_move_checks": True,
              "world_mode": "open_world"}, True),
            ({"encounter_rate": 0}, False),
            ({"encounter_rate": 0, "skillsanity": "skills_only", "world_mode": "open_world"}, False),
            ({"encounter_rate": 1, "killer_move_checks": True, "skillsanity": "insanity"}, False),
            ({"killer_move_checks": True, "skillsanity": "special_only", "world_mode": "hub"}, False),
        ]
        for options, want in mixes:
            with self.subTest(**options):
                self.options = options
                self.world_setup()
                self.assertEqual(hell.is_hell_seed(self.world.options), want)
                fill(self)
                seed = self.world.seed_data()
                check_seed(self, seed)
                self.assertEqual(hell.FEE_PATCH in seed["patches"], want)
                self.assertEqual(seed["options"].get("hell_seed"), True if want else None)
                self.assertEqual("hell_seed" in self.world.fill_slot_data(), want)
