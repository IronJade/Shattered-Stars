"""The difficulty option (docs/apworld/seed_format.md, "v2 fields"): New Game's Battle Rank. The seed
carries it; the logic does not change (only the monsters' stats do, not EXP, Fol, drops or
encounters)."""
from .. import hub, locations
from .bases import SO2TestBase, check_seed, fill


class DifficultyChecks:
    key = "earth"
    version = 1

    def test_difficulty(self):
        data = self.world.fill_slot_data()
        self.assertEqual(data["difficulty"], self.key)
        self.assertEqual(self.world.seed_version(), self.version)
        # No logic change: on Earth the world would choose the same locations, of the same kinds.
        o = self.world.options
        value = o.difficulty.value
        o.difficulty.value = o.difficulty.option_earth
        try:
            earth = (hub.choose if self.world.hub else locations.choose)(o, self.world.party_info)
        finally:
            o.difficulty.value = value
        self.assertEqual([(row["name"], kind) for row, kind in earth],
                         [(row["name"], kind) for row, kind in self.world.chosen])
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["options"]["difficulty"], self.key)


class TestDifficultyDefault(SO2TestBase, DifficultyChecks):
    options = {}


class TestDifficultyGalaxy(SO2TestBase, DifficultyChecks):
    options = {"difficulty": "galaxy"}
    key, version = "galaxy", 2


class TestDifficultyUniverse(SO2TestBase, DifficultyChecks):
    options = {"difficulty": "universe", "world_mode": "open_world"}
    key, version = "universe", 2


class TestDifficultyAny(SO2TestBase, DifficultyChecks):
    options = {"difficulty": "any"}
    key, version = "any", 2


class TestDifficultyHub(SO2TestBase, DifficultyChecks):
    options = {"difficulty": "universe", "world_mode": "hub"}
    key, version = "universe", 4        # a hub seed with the bench (on by default)
