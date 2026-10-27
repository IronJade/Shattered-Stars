"""The party (docs/ap/seed_format.md "Party v2"; docs/ap/apworld.md "Characters"): the story's party
rules with character_items (CharacterChecks), open world's party v2 (PartyChecks:
choose_open_party, the starting member, exclude_characters, the healer guarantee), and what a
character item opens. Hub mode's party: test_hub.py HubChecks.test_party.

The bench (options.Bench, on by default; docs/handoff/bench.md "The user's answers": 12 places in
open world with character_items and in hub mode, no random sample, no healer guarantee, seed
version 4): PartyChecks reads the party size and the healer rule from party_size() /
healer_guaranteed(); BenchChecks overrides them and adds the bench's own checks. The 8-place mixes
(TestPartyOpen..., TestPartyHealer..., TestPartyStartRena) pin `bench: False`; the bench mixes
(TestBench...) leave it at its default or set it."""
from BaseClasses import LocationProgressType

from ..items import CHARACTER_NAMES, CHARACTER_OF, FIXED_MEMBERS, PARTY_SIZE
from ..locations import ROW, fighter_status
from .bases import SO2TestBase, check_seed, fill, own_locations, own_pool


# ---- The story's party rules (character_items in story mode) ----

def party_of(test):
    return [CHARACTER_OF[i.code] for i in own_pool(test) if i.code in CHARACTER_OF]


class CharacterChecks:
    """character_items: the pool's characters always make a party the story allows."""

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


class TestCharactersRena(SO2TestBase, CharacterChecks):
    options = {"character_items": True, "story_route": "rena", "missable_locations": "remove"}


# ---- Party v2: open world with character_items; starting_character ----

class PartyChecks:
    """Open world's party: the heroes, the starting member and the pool make at most 8 (12 with
    the bench); nobody excluded is in the pool; Rena or Noel is in the party unless both are
    excluded; the join sites are those of every non-story member; the seed says so."""

    def party_size(self):
        """The places the logic fills (items.PARTY_SIZE; a bench option would raise it)."""
        from ..items import PARTY_SIZE
        return PARTY_SIZE

    def healer_guaranteed(self):
        """Whether choose_open_party keeps Rena or Noel in the party (a bench would drop it)."""
        return True

    def check_party(self, info):
        from ..items import CHARACTER_NAMES, HEALERS
        PARTY_SIZE = self.party_size()
        o = self.world.options
        heroes = self.world.story_members()
        members = set(heroes) | ({info.start} if info.start else set()) | set(info.pool)
        self.assertEqual(len(info.pool), len(set(info.pool)))
        self.assertLessEqual(len(members), PARTY_SIZE)
        self.assertFalse(set(info.pool) & set(heroes))
        self.assertNotIn(info.start, info.pool)
        excluded = set(o.exclude_characters.value)
        self.assertFalse(set(info.pool) & excluded)
        allowed = [n for n in CHARACTER_NAMES if n not in heroes and n != info.start and n not in excluded]
        if len(allowed) + len(heroes) + (info.start not in (None, *heroes)) <= PARTY_SIZE:
            self.assertEqual(sorted(info.pool), sorted(allowed))
        else:
            self.assertEqual(len(members), PARTY_SIZE)
        if self.healer_guaranteed() and (not set(HEALERS) <= excluded or set(HEALERS) & (set(heroes) | {info.start})):
            self.assertTrue(members & set(HEALERS), members)

    def test_party(self):
        self.check_party_seed()

    def check_party_seed(self):
        info = self.world.party_info
        self.assertEqual(info.rules, "open")
        for _ in range(30):
            heroes = self.world.story_members()
            self.check_party(info._replace(pool=self.world.choose_open_party(heroes, info.start)))
        self.check_party(info)
        from ..items import CHARACTER_OF
        pool = sorted(CHARACTER_OF[i.code] for i in self.multiworld.itempool
                      if i.player == self.player and i.code in CHARACTER_OF)
        self.assertEqual(pool, sorted(info.pool))
        joins = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "join"]
        for loc in joins:
            self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)
            self.assertNotIn(ROW[loc.name]["character"], info.story_members, loc.name)
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        o = seed["options"]
        self.assertEqual((seed["version"], o["party_rules"], o["characters"], o["starting_character"]),
                         (4 if self.world.bench else 2, "open", list(info.pool),
                          list(info.choices) if info.choices else info.start))
        self.assertEqual(o.get("bench"), True if self.world.bench else None)
        for v in seed["items"].values():
            if v["kind"] == "character":
                self.assertIn(v["character"], info.pool)
        return info, joins


class TestPartyOpenClaude(SO2TestBase, PartyChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude", "bench": False}

    def test_open_members(self):
        info, joins = self.check_party_seed()
        self.assertIsNone(info.start)
        self.assertEqual(len(info.pool), 7)
        listed = {ROW[loc.name]["character"] for loc in joins}
        self.assertIn("Rena", listed)       # Rena's first join (map 9) in Claude's game
        self.assertIn("Noel", listed)
        self.assertNotIn("Claude", listed)
        self.assertIn("Leon", listed)


class TestPartyOpenAnyStart(SO2TestBase, PartyChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "any",
               "starting_character": "any", "exclude_characters": ["Noel", "Claude"], "bench": False}

    def test_any(self):
        """[charselect agent] "any": the player picks at New Game among the heroes and the pool
        (the seed's list); the logic counts on nobody extra, so the pool keeps that place."""
        info, joins = self.check_party_seed()
        self.assertEqual(info.story_members, ("Claude", "Rena"))
        self.assertIsNone(info.start)
        self.assertEqual(len(info.pool), 6)
        self.assertEqual(info.choices, tuple(n for n in CHARACTER_NAMES if n in ("Claude", "Rena") or n in info.pool))
        self.assertNotIn("Noel", info.choices)
        self.assertEqual(fighter_status(info.choices[2], info), "item")
        self.assertFalse({"Claude", "Rena"} & {ROW[loc.name]["character"] for loc in joins})


class TestPartyAnyStartWithoutItems(SO2TestBase):
    """[charselect agent] "any" under the story's party rules (no character items): every member
    not excluded may be picked; nobody is counted on."""
    options = {"world_mode": "open_world", "story_route": "rena", "starting_character": "any",
               "exclude_characters": ["Dias"]}

    def test_any(self):
        info = self.world.party_info
        self.assertEqual((info.rules, info.start, info.pool), ("story", None, []))
        self.assertEqual(info.choices, tuple(n for n in CHARACTER_NAMES if n != "Dias"))
        self.assertNotIn("Dias", info.guaranteed)
        o = self.world.fill_slot_data()
        self.assertEqual((o["starting_character"], o["seed_format"]), (list(info.choices), 2))


class TestPartyHealersExcluded(SO2TestBase, PartyChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude",
               "exclude_characters": ["Rena", "Noel", "Celine", "Bowman"], "bench": False}

    def test_no_healer(self):
        info, _ = self.check_party_seed()
        self.assertFalse({"Rena", "Noel"} & set(info.pool))
        self.assertEqual(sorted(info.pool), ["Ashton", "Chisato", "Dias", "Ernest", "Leon", "Opera", "Precis"])


class TestPartyHealerGuarantee(SO2TestBase, PartyChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude",
               "starting_character": "leon", "bench": False}

    def test_healer(self):
        from ..items import HEALERS
        for _ in range(60):     # 6 of 10: the draw leaves out both healers about a third of the time
            pool = self.world.choose_open_party(("Claude",), "Leon")
            self.assertTrue(set(pool) & set(HEALERS), pool)
            self.assertEqual(len(pool), 6)


class TestPartyStartRena(SO2TestBase, PartyChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "rena",
               "starting_character": "leon", "exclude_characters": ["Leon", "Bowman"], "bench": False}

    def test_named_start_wins(self):
        info, joins = self.check_party_seed()
        self.assertEqual(info.start, "Leon")
        self.assertIn("Claude", {ROW[loc.name]["character"] for loc in joins})


class TestStartWithoutItems(SO2TestBase):
    options = {"world_mode": "open_world", "starting_character": "chisato", "story_route": "rena"}

    def test_start_only(self):
        info = self.world.party_info
        self.assertEqual((info.rules, info.start, info.pool), ("story", "Chisato", []))
        self.assertFalse(any(ROW[loc.name]["type"] == "join" for loc in own_locations(self)))
        o = self.world.fill_slot_data()
        self.assertEqual((o["starting_character"], o["party_rules"], o["seed_format"]), ("Chisato", "story", 2))


class TestStartIgnoredInStory(SO2TestBase):
    options = {"starting_character": "ashton", "character_items": True}

    def test_story(self):
        info = self.world.party_info
        self.assertEqual((info.rules, info.start), ("story", None))
        self.assertEqual(self.world.seed_version(), 1)
        self.assertFalse(any(ROW[loc.name].get("open_only") for loc in own_locations(self)))


class TestHeroStart(SO2TestBase):
    options = {"world_mode": "open_world", "starting_character": "claude", "story_route": "claude"}

    def test_hero_is_nobody_extra(self):
        self.assertIsNone(self.world.party_info.start)
        self.assertEqual(self.world.seed_version(), 1)


# ---- The bench (options.Bench, on by default): 12 places in open world and hub mode ----

class BenchChecks(PartyChecks):
    """With the bench the party has 12 places (the game's 8 and the port's bench of 4): the pool is
    every allowed member, drawn without the RNG; no healer guarantee (both healers are in unless
    excluded); every pool member joins by their item ("item" to fighter_status), the heroes and
    the starting member are sure; the select offers every member not excluded; the seed is
    version 4 with options.bench."""

    def party_size(self):
        from ..items import BENCH_PARTY_SIZE
        return BENCH_PARTY_SIZE

    def healer_guaranteed(self):
        return False

    def test_bench(self):
        from ..items import BENCH_PARTY_SIZE
        w = self.world
        info = w.party_info
        self.assertTrue(w.bench)
        self.assertEqual((w.party_size(), BENCH_PARTY_SIZE), (12, 12))
        heroes = w.story_members()
        excluded = set(w.options.exclude_characters.value)
        allowed = [n for n in CHARACTER_NAMES if n not in heroes and n != info.start and n not in excluded]
        self.assertEqual(info.pool, allowed)
        # No random draw: the pool comes out the same, and the world's RNG is left as it was.
        state = w.random.getstate()
        self.assertEqual(w.choose_open_party(heroes, info.start), allowed)
        self.assertEqual(w.random.getstate(), state)
        for who in CHARACTER_NAMES:
            want = ("sure" if who in heroes or who == info.start else "item" if who in allowed else None)
            self.assertEqual(fighter_status(who, info), want, who)
        self.assertEqual(info.possible, frozenset(set(heroes) | set(allowed) | ({info.start} if info.start else set())))
        if info.choices:
            self.assertEqual(info.choices, tuple(n for n in CHARACTER_NAMES if n in heroes or n not in excluded))
        data = w.fill_slot_data()
        self.assertEqual((data["seed_format"], data["bench"], data["characters"]), (4, True, allowed))
        self.assertEqual(w.seed_version(), 4)


class TestBenchDefaultOpenClaude(SO2TestBase, BenchChecks):
    """The default: open world with character_items and nothing else set has the bench, so all 11
    members but Claude are character items (the 8-place game drew 7 of them)."""
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude"}

    def test_default_on(self):
        self.assertEqual(self.world.options.bench.value, 1)
        info, joins = self.check_party_seed()
        self.assertEqual(len(info.pool), 11)
        self.assertEqual(set(info.pool) | {"Claude"}, set(CHARACTER_NAMES))
        # create_items puts the members in the join sites' place in the pool: there are enough.
        self.assertGreaterEqual(len(joins), len(info.pool))


class TestBenchAnyStart(SO2TestBase, BenchChecks):
    """starting_character any with the bench: the select offers every member not excluded (Claude
    can't be: a hero under story_route any)."""
    options = {"world_mode": "open_world", "character_items": True, "story_route": "any",
               "starting_character": "any", "exclude_characters": ["Noel", "Claude"], "bench": True}

    def test_select(self):
        info, _ = self.check_party_seed()
        self.assertIsNone(info.start)
        self.assertEqual(info.choices, tuple(n for n in CHARACTER_NAMES if n != "Noel"))
        self.assertEqual(len(info.pool), 9)
        self.assertEqual(self.world.fill_slot_data()["starting_character"], list(info.choices))


class TestBenchHealersExcluded(SO2TestBase, BenchChecks):
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude",
               "exclude_characters": ["Rena", "Noel", "Celine", "Bowman"]}

    def test_no_healer(self):
        info, _ = self.check_party_seed()
        self.assertEqual(info.pool, ["Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Chisato"])


class TestBenchNamedStart(SO2TestBase, BenchChecks):
    """A named starting member (who wins over exclude_characters) is sure; the pool is everyone else
    allowed."""
    options = {"world_mode": "open_world", "character_items": True, "story_route": "rena",
               "starting_character": "leon", "exclude_characters": ["Leon", "Bowman"]}

    def test_named_start(self):
        info, joins = self.check_party_seed()
        self.assertEqual(info.start, "Leon")
        self.assertEqual(info.guaranteed, frozenset({"Rena", "Leon"}))
        self.assertEqual(info.pool, [n for n in CHARACTER_NAMES if n not in ("Rena", "Leon", "Bowman")])
        self.assertIn("Claude", {ROW[loc.name]["character"] for loc in joins})


class TestBenchReceivedMembers(SO2TestBase, BenchChecks):
    """What a received member's item opens with the bench: every pool member (the four the 8 places
    left out too) has killer-move levels (with killer_move_progression any), learn levels
    (skillsanity special_only) and crafts in the world; their levels need their item and nothing
    else does (assertAccessDependency), and a craft only one of them can make holds filler only."""
    options = {"world_mode": "open_world", "character_items": True, "story_route": "claude",
               "killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "special_only",
               "craftsanity": True}

    def test_received_members(self):
        from ..locations import CRAFT_ROWS, KILLER_ROWS
        w = self.world
        info = w.party_info
        self.assertEqual(len(info.pool), 11)
        mine = {loc.name: loc for loc in own_locations(self)}
        # Killer-move levels: every member's who has killer moves (no one is left out now).
        self.assertEqual(sorted(n for n in mine if ROW[n]["type"] == "killer_move"),
                         sorted(r["name"] for r in KILLER_ROWS))
        # Crafts: every craft (all 12 can be in the party); a member-locked one is filler only unless
        # Claude (the only sure member) can make it.
        crafts = [n for n in mine if ROW[n]["type"] == "craft"]
        self.assertEqual(sorted(crafts), sorted(r["name"] for r in CRAFT_ROWS))
        for n in crafts:
            makers = ROW[n]["makers"]
            locked = makers is not None and "Claude" not in makers
            self.assertEqual(mine[n].progress_type == LocationProgressType.EXCLUDED, locked, n)
        self.assertEqual(w.progression_characters, set(info.pool))
        for who in info.pool:
            levels = [n for n in mine if ROW[n]["type"] == "killer_move" and ROW[n]["character"] == who] + \
                     [n for n in mine if ROW[n]["type"] == "learn" and ROW[n].get("how") == "level"
                      and ROW[n]["character"] == who]
            with self.subTest(character=who):
                self.assertTrue(levels)
                self.assertTrue(w.create_item(who).advancement)
                self.assertAccessDependency(levels, [[who]])


class TestBenchIgnoredInStory(SO2TestBase):
    """Story mode keeps the story's party rules and the game's 8 places: the bench option is
    ignored (no `bench` in the seed, its version as without it)."""
    options = {"character_items": True, "bench": True}

    def test_story(self):
        w = self.world
        self.assertEqual((w.bench, w.party_size(), w.party_info.rules), (False, 8, "story"))
        self.assertEqual(w.seed_version(), 1)
        self.assertNotIn("bench", w.fill_slot_data())
        self.assertLessEqual(len(w.party) + len(FIXED_MEMBERS), PARTY_SIZE)


class TestBenchIgnoredWithoutItems(SO2TestBase):
    """Open world without character_items keeps the story's party rules: no bench (a warning)."""
    options = {"world_mode": "open_world", "starting_character": "any", "bench": True}

    def test_open_without_items(self):
        w = self.world
        self.assertEqual((w.bench, w.party_size(), w.party_info.rules, w.party_info.pool), (False, 8, "story", []))
        self.assertEqual(w.seed_version(), 2)       # the select (a list), not the bench
        self.assertNotIn("bench", w.fill_slot_data())
        fill(self)
        seed = w.seed_data()
        check_seed(self, seed)
        self.assertNotIn("bench", seed["options"])
