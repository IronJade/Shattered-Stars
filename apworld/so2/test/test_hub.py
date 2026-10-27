"""Hub mode (`world_mode: hub`; apworld/so2/hub.py, docs/ap/apworld.md "Hub mode",
docs/ap/seed_format.md "Hub"): the locations the hub reaches, the dungeon keys, the chambers, the
Fienal requirement, where progression may go, the party, the version-3 seed. WorldTestBase's own
tests run for every class too (every location reachable with all items, the fill)."""
import unittest

from BaseClasses import CollectionState, LocationProgressType

from .. import hub
from ..data import HUB_BOSSES, HUB_DROPS, HUB_SHOPS, HUB_STARTS, HUB_STATE, LOCATIONS
from ..items import (ALL_KEY_IDS, BENCH_PARTY_SIZE, CHARACTER_NAMES, CHARACTER_OF, HUB_KEY_NAME, HUB_KEY_OF, KEY_OF,
                     PARTY_SIZE)
from ..locations import ROW
from .bases import SO2TestBase, check_seed, doors_json, fill, own_locations, own_pool, progression_item


class HubChecks:
    """Every hub option mix: the checks below, plus WorldTestBase's."""

    def games(self):
        return hub.games_of(self.side)

    def rows(self):
        return [ROW[loc.name] for loc in own_locations(self)]

    def hub_version(self):
        """3, or 4 with the bench (on by default: options.Bench)."""
        return 4 if self.world.bench else 3

    def test_mode(self):
        self.assertTrue(self.world.hub)
        self.assertFalse(self.world.open_world)
        self.assertEqual(self.world.bench, bool(self.world.options.bench))
        self.assertEqual(self.world.seed_version(), self.hub_version())

    def test_only_what_the_hub_reaches(self):
        """No join site, no losable fight, nothing the hub can't reach in the player's game(s);
        chests, gifts and the cave's / optional bosses proven by the hub proof (data.HUB_REACHED),
        shops open and enemy types met at the hub state, the chambers of this game."""
        side = self.side
        for row in self.rows():
            kind = row["type"]
            self.assertNotEqual(kind, "join", row["name"])
            if kind == "boss":
                self.assertNotEqual(row.get("boss"), "losable", row["name"])
                self.assertNotIn(row["encounter"], (204, 265, 517, 530, 539, 540), row["name"])
                if row["encounter"] in hub.CHAMBERS:
                    game = hub.CHAMBERS[row["encounter"]].get("game")
                    self.assertIn(game, (None, side) if side else (None, "claude", "rena"), row["name"])
                else:
                    self.assertTrue(any(g in hub.reached_games(row) for g in self.games()), row["name"])
            elif kind in ("chest", "gift"):
                self.assertTrue(any(g in hub.reached_games(row) for g in self.games()), row["name"])
            elif kind == "shop":
                self.assertTrue(any(row["shop"] in HUB_SHOPS[g] for g in self.games()), row["name"])
            elif kind == "drop":
                self.assertTrue(any(HUB_DROPS[row["id"]][g] for g in self.games()), row["name"])
            if kind not in ("drop", "craft", "killer_move"):
                self.assertNotIn(hub.dungeon_of(row), hub.OUT_OF_HUB, row["name"])

    def test_chambers(self):
        """The 22 chambers (boss_checks): this game's, as locations; their region is their
        dungeon's (with its key) or a town's; under story_route any the hero's chamber (Varmillion,
        Azamgil) is filler only."""
        side = self.side
        locs = {loc.name: loc for loc in own_locations(self)}
        chambers = [r for r in LOCATIONS + hub.HUB_LOCATIONS if r["id"] in hub.CHAMBER_LOCATIONS]
        self.assertEqual(len(chambers), 22)
        for row in chambers:
            game = hub.CHAMBERS[row["encounter"]].get("game")
            remove = self.world.options.missable_locations.current_key == "remove"
            want = bool(self.world.options.boss_checks) and (game is None or game == side
                                                              or (side is None and not remove))
            self.assertEqual(row["name"] in locs, want, row["name"])
            if not want:
                continue
            loc = locs[row["name"]]
            dungeon = hub.CHAMBERS[row["encounter"]]["dungeon"]
            if dungeon in hub.DUNGEONS:
                self.assertEqual(loc.parent_region.name, dungeon, row["name"])
            else:
                self.assertIn(loc.parent_region.name, hub.TOWNS, row["name"])
            excluded = loc.progress_type == LocationProgressType.EXCLUDED
            self.assertEqual(excluded, side is None and game is not None, row["name"])

    def test_keys(self):
        """One key per dungeon that has something behind it (a location, a chamber of the Fienal
        requirement, the Iseria Queen's cave); never Fienal's or the Portal of Sealing's (out of
        hub mode); the Lasguss key only with XINE; each in the pool once, progression; no starting
        key."""
        keys = [i for i in own_pool(self) if i.code in HUB_KEY_OF]
        names = sorted(i.name for i in keys)
        self.assertEqual(names, sorted(HUB_KEY_NAME[g["dungeon"]] for g in self.world.hub_keys))
        self.assertEqual(len(set(names)), len(names))
        self.assertTrue(all(i.advancement for i in keys))
        self.assertFalse(any(i.code in KEY_OF for i in own_pool(self)))     # no area key (open world's)
        self.assertNotIn("Fienal Key", self.world.item_name_to_id)
        self.assertNotIn("Portal of Sealing Key", self.world.item_name_to_id)
        self.assertFalse([i for i in self.multiworld.precollected_items[self.player] if i.code in ALL_KEY_IDS])
        behind = {hub.dungeon_of(r) for r in self.rows() if r["type"] not in ("drop", "craft", "killer_move")}
        behind |= {hub.CHAMBERS[e]["dungeon"] for e in self.world.fienal_won}
        if self.world.options.goal.value == 2:
            behind.add(hub.CAVE)
        for g in self.world.hub_keys:
            self.assertIn(g["dungeon"], behind, g["name"])
        xine = "Lasguss Mountain: XINE boss" in {loc.name for loc in own_locations(self)}
        self.assertEqual("Lasguss Mountain Key" in names, xine)
        self.assertEqual(len(own_pool(self)), sum(1 for loc in own_locations(self) if not loc.locked))

    def test_every_boss_needs_only_its_key(self):
        """D3: a chamber is open once its dungeon's key has arrived, nothing else; the chambers in
        towns from the start; Fienal's (and Fienal's chests) once the requirement is met."""
        locs = {loc.name: loc for loc in own_locations(self)}
        for b in HUB_BOSSES:
            name = next((r["name"] for r in LOCATIONS + hub.HUB_LOCATIONS if r["id"] == b["location"]), None)
            if name not in locs:
                continue
            state = CollectionState(self.multiworld)
            if b["dungeon"] == hub.FIENAL:
                self.assertFalse(locs[name].can_reach(state), name)
                for e in self.world.fienal_won:
                    d = hub.CHAMBERS[e]["dungeon"]
                    if d in hub.keyed(self.world):
                        state.collect(self.world.create_item(HUB_KEY_NAME[d]), True)
                state.sweep_for_advancements()
                self.assertTrue(locs[name].can_reach(state), name)
            elif b["dungeon"] in hub.keyed(self.world):
                self.assertFalse(locs[name].can_reach(state), name)
                state.collect(self.world.create_item(HUB_KEY_NAME[b["dungeon"]]), True)
                self.assertTrue(locs[name].can_reach(state), name)
            else:
                self.assertTrue(locs[name].can_reach(state), name)

    def test_fienal_requirement(self):
        """D6: Fienal opens (and the goal can be reached) only with every key of the requirement's
        chambers; without any one of them, not."""
        value = self.world.options.fienal_requirement.value
        won = self.world.fienal_won
        if value == 0:
            self.assertEqual(won, [524, 525, 527, 528, 535])
        elif value == 1:
            self.assertEqual(won, [524, 525, 527, 528])
        else:
            side = self.side
            want = [b["encounter"] for b in HUB_BOSSES if b["dungeon"] != hub.FIENAL and b.get("game") in (None, side)]
            self.assertEqual(won, want)
        keys = list(dict.fromkeys(HUB_KEY_NAME[hub.CHAMBERS[e]["dungeon"]] for e in won
                                  if hub.CHAMBERS[e]["dungeon"] in hub.keyed(self.world)))
        self.assertTrue(keys)
        gate = self.world.get_location(hub.EVENT_FIENAL_GATE)
        for missing in keys:
            state = CollectionState(self.multiworld)
            for k in keys:
                if k != missing:
                    state.collect(self.world.create_item(k), True)
            self.assertFalse(gate.can_reach(state), missing)
        state = CollectionState(self.multiworld)
        for k in keys:
            state.collect(self.world.create_item(k), True)
        self.assertTrue(gate.can_reach(state))
        # The goal: everything collected reaches Victory.
        self.assertTrue(self.multiworld.can_beat_game(self.multiworld.get_all_state(False)))

    def test_dungeon_keys(self):
        """What each dungeon key opens (assertAccessDependency, every location checked): what lies
        in its dungeon (the region its locked doors lead to: chests, gifts, its chamber, a cave
        learn point, the Iseria Queen in the cave), a first drop met only there, and the Fienal
        gate when one of the requirement's chambers is in the dungeon; nothing else."""
        gate = {HUB_KEY_NAME[hub.CHAMBERS[e]["dungeon"]] for e in self.world.fienal_won
                if hub.CHAMBERS[e]["dungeon"] in hub.keyed(self.world)}
        for g in self.world.hub_keys:
            key = HUB_KEY_NAME[g["dungeon"]]
            needs = [loc.name for loc in self.multiworld.get_locations(self.player)
                     if loc.parent_region.name == g["dungeon"]]
            needs += [loc.name for loc in own_locations(self) if ROW[loc.name]["type"] == "drop"
                      and all(key in m for m in hub.drop_rule(self.world, ROW[loc.name], self.side)[1])]
            if key in gate:
                needs.append(hub.EVENT_FIENAL_GATE)
            with self.subTest(key=key):
                self.assertAccessDependency(needs, [[key]])

    def test_fienal_gate_needs_each_key(self):
        """D6 the guide's way: the Fienal gate can't be reached without any one of the
        requirement's keys; without them all the goal can't either (assertSweptAccessDependency:
        through the events), with them it can."""
        keys = list(dict.fromkeys(HUB_KEY_NAME[hub.CHAMBERS[e]["dungeon"]] for e in self.world.fienal_won
                                  if hub.CHAMBERS[e]["dungeon"] in hub.keyed(self.world)))
        self.assertTrue(keys)
        for key in keys:
            with self.subTest(key=key):
                self.assertAccessDependency([hub.EVENT_FIENAL_GATE], [[key]], only_check_listed=True)
        self.assertSweptAccessDependency([hub.EVENT_FIENAL_GATE, hub.EVENT_INDALECIO], [keys])

    def test_progression_rules(self):
        """Progression only in a chamber, a chest or cave boss the proof reached in every game
        the player may play (an optional boss only once hubchambers confirmed it), drops per
        drop_progression; never in a gift, a shop, a craft, an EXCLUDED location, or Expel with
        expel_progression off."""
        other = progression_item(self)
        st = self.multiworld.state
        expel = bool(self.world.options.expel_progression)
        held = 0
        for loc in own_locations(self):
            row = ROW[loc.name]
            ok = loc.can_fill(st, other, False)
            excluded = loc.progress_type == LocationProgressType.EXCLUDED
            kind = row["type"]
            if kind in ("gift", "shop", "craft", "join") or excluded:
                self.assertFalse(ok, loc.name)
            elif kind == "chest" or (kind == "boss" and row["encounter"] not in hub.CHAMBERS):
                want = all(g in hub.reached_games(row) for g in self.games()) and (expel or row["region"] != "Expel")
                if kind == "boss" and row.get("boss") == "optional":
                    want = want and row["encounter"] in hub.CONFIRMED_OPTIONAL
                self.assertEqual(ok, want, loc.name)
            elif kind == "boss":
                self.assertEqual(ok, expel or row["region"] != "Expel", loc.name)
            elif kind == "drop":
                cls, _ = hub.drop_rule(self.world, row, self.side)
                self.assertEqual(ok, cls == "any", loc.name)
            held += ok
        self.assertGreater(held, len(self.world.hub_keys))

    def test_party(self):
        """D7: characters are always items (no join location); the party is at most 8, or with the
        bench (12 places) every member not excluded; under story_route any both heroes are items
        (the one picked at New Game does nothing)."""
        o = self.world.options
        p = self.world.party_info
        self.assertEqual(p.rules, "open")
        heroes = p.story_members
        pool = [CHARACTER_OF[i.code] for i in own_pool(self) if i.code in CHARACTER_OF]
        self.assertEqual(sorted(pool), sorted(p.pool))
        if len(heroes) == 1:
            self.assertNotIn(heroes[0], pool)
        else:
            self.assertEqual({"Claude", "Rena"} - {p.start}, {"Claude", "Rena"} & set(pool))
        members = set(heroes) | set(pool) | ({p.start} if p.start else set())
        self.assertLessEqual(len(members), BENCH_PARTY_SIZE if self.world.bench else PARTY_SIZE)
        for name in o.exclude_characters.value:
            if name not in heroes and name != p.start:
                self.assertNotIn(name, pool)
        if self.world.bench:     # every member not excluded, no random draw
            self.assertEqual(members, {n for n in CHARACTER_NAMES if n in heroes or n == p.start
                                       or n not in o.exclude_characters.value})
        self.assertFalse([r for r in self.rows() if r["type"] == "join"])

    def test_seed(self):
        """The version-3 seed: the `hub` block, the chambers, the doors, the options."""
        fill(self)
        seed = self.world.seed_data()
        check_seed(self, seed)
        self.assertEqual(seed["version"], self.hub_version())
        o = seed["options"]
        self.assertEqual((o["world_mode"], o["seed_format"], o["party_rules"], o["character_items"]),
                         ("hub", self.hub_version(), "open", True))
        self.assertEqual(o.get("bench"), True if self.world.bench else None)
        self.assertIs(o["expel_teleporter"], False)
        self.assertEqual(o["hub_start"], self.world.options.hub_start.current_key)
        self.assertEqual(o["fienal_requirement"], self.world.options.fienal_requirement.current_key)
        self.assertIn(o["hub_start_town"], HUB_STARTS)
        if self.world.options.hub_start.current_key != "random_town":
            self.assertEqual(o["hub_start_town"], self.world.options.hub_start.current_key)
        h = seed["hub"]
        for k, v in HUB_STATE.items():
            self.assertEqual(h[k], v, k)
        self.assertEqual((h["chapter"], h["step"], h["disc"]), (60, 4000, 2))
        self.assertEqual(h["start"], HUB_STARTS[o["hub_start_town"]])
        self.assertEqual(seed["start"]["flags"], [])
        # bosses: every chamber, the port's keys as hub_bosses.json has them, `keys` this seed's.
        self.assertEqual([b["encounter"] for b in seed["bosses"]], [b["encounter"] for b in HUB_BOSSES])
        numbers = {g["dungeon"]: g["key"] for g in self.world.hub_keys}
        for b, ref in zip(seed["bosses"], HUB_BOSSES):
            for k in ("encounter", "name", "map", "maps", "view", "game", "location", "dungeon", "trigger", "late"):
                self.assertEqual(b.get(k), ref.get(k), (b["encounter"], k))
            self.assertEqual(b["keys"], [numbers[ref["dungeon"]]] if ref["dungeon"] in numbers else [])
            self.assertEqual(set(b) - set(hub.BOSS_KEYS), set())
            # The chambers that ask first (Shigeo 281, Marsilio 305), as the port reads `ask`.
            self.assertEqual(b.get("ask"), hub.ASK.get(b["encounter"]), b["encounter"])
        self.assertEqual(sorted(b["encounter"] for b in seed["bosses"] if "ask" in b), [532, 533])
        # doors: every door of each dungeon with a key, with it; Fienal's with the requirement.
        doors = seed["doors"]
        self.assertEqual(len({d["id"] for d in doors}), len(doors))
        keyed = [d for d in doors if "key" in d]
        want = sorted((d["id"], g["key"], d["routine"]) for g in self.world.hub_keys for d in g["doors"])
        self.assertEqual(sorted((d["id"], d["key"], d.get("routine")) for d in keyed), want)
        # A routine only where every transfer it makes leads into the dungeon (Zand's Mansion's
        # 9314034 also leads to Herlie: written without it).
        zand = [d for d in keyed if d["id"] == 9314034]
        self.assertTrue(all("routine" not in d for d in zand))
        (fienal,) = [d for d in doors if d.get("won") and d["id"] != 43978493]
        self.assertEqual(fienal["id"], hub.DUNGEONS[hub.FIENAL]["doors"][0]["id"])
        self.assertEqual(fienal["won"], self.world.fienal_won)
        self.assertEqual(len(fienal["banners"]), len(fienal["won"]))
        self.assertTrue(all(len(b) <= 33 for b in fienal["banners"]))
        self.assertNotIn("key", fienal)
        goal_gates = [d for d in doors if d["id"] == 43978493]
        self.assertEqual(goal_gates, self.world.goal_doors())
        # P5: the arena antechamber's door to Marsilio's arena while he is unbeaten (to.while_unbeaten).
        p5 = [d for d in doors if "to" in d]
        self.assertEqual(p5, [dict(d) for d in hub.P5_DOORS])
        self.assertEqual([(d["id"], d["to"]["map"], d["to"]["while_unbeaten"]) for d in p5],
                         [(19929509, 305, 532), (44502760, 674, 538)])
        self.assertTrue(all("key" not in d and "won" not in d for d in p5))
        self.assertEqual(len(doors), len(keyed) + 1 + len(goal_gates) + len(p5))
        table = doors_json()
        if table is not None:
            from ..doors import validate_doors
            self.assertEqual(validate_doors(doors, table), [])
        # The items table describes the dungeon keys as keys with their numbers.
        for g in self.world.hub_keys:
            code = self.world.item_name_to_id[HUB_KEY_NAME[g["dungeon"]]]
            self.assertEqual(seed["items"][str(code)], {"name": g["name"], "kind": "key", "key": g["key"]})


class TestHubDefault(SO2TestBase, HubChecks):
    options = {"world_mode": "hub"}

    def test_default_counts(self):
        """Defaults (story_route any, boss_checks on, cave off): 217 chests and 7 gifts the hub
        proof reached (the cave's left out; with Eluria Tower's 23 and Fienal's maze 9), 20 chambers (+ 2 hero chambers, filler only), XINE
        (it plays at the hub state: it may hold progression), 17 keys (no cave, no Fienal, no
        Portal of Sealing)."""
        kinds = {}
        for row in self.rows():
            kinds[row["type"]] = kinds.get(row["type"], 0) + 1
        self.assertEqual(kinds, {"chest": 217, "gift": 7, "boss": 23})
        self.assertEqual(len(self.world.hub_keys), 17)
        self.assertEqual(self.world.hub_start, "fun_city")


class TestHubClaudeArlia(SO2TestBase, HubChecks):
    options = {"world_mode": "hub", "story_route": "claude", "hub_start": "arlia", "fienal_requirement": "fields",
               "cave_of_trials": True, "starting_character": "ashton", "killer_move_checks": True,
               "killer_move_progression": "any"}

    def test_cave_and_start(self):
        self.assertEqual(self.world.hub_start, "arlia")
        self.assertIn("Cave of Trials Key", [g["name"] for g in self.world.hub_keys])
        self.assertEqual(len(self.world.hub_keys), 18)
        rows = self.rows()
        self.assertEqual(sum(1 for r in rows if r["type"] == "chest"), 290)   # the hub proof's (the late proof's 258)
        self.assertNotIn("Mars: Azamgil boss", {r["name"] for r in rows})


class TestHubRenaAllBosses(SO2TestBase, HubChecks):
    options = {"world_mode": "hub", "story_route": "rena", "fienal_requirement": "all_bosses",
               "goal": "indalecio_limiter_off_and_iseria_queen", "enemy_dropsanity": "first_drop",
               "drop_progression": "force", "area_keys_in_drops": False, "shuffle_shops": "specials"}

    def test_cave_key_for_the_queen(self):
        """The Iseria Queen goal needs the Cave of Trials (its key) even with cave_of_trials off."""
        self.assertIn("Cave of Trials Key", [g["name"] for g in self.world.hub_keys])
        state = CollectionState(self.multiworld)
        queen = self.world.get_location("Iseria Queen (Cave of Trials)")
        self.assertFalse(queen.can_reach(state))
        state.collect(self.world.create_item("Cave of Trials Key"), True)
        self.assertTrue(queen.can_reach(state))
        self.assertIn(195, self.world.fienal_won)
        self.assertNotIn(194, self.world.fienal_won)

    def test_no_keys_in_drops(self):
        drops = [loc for loc in own_locations(self) if ROW[loc.name]["type"] == "drop"]
        self.assertTrue(drops)
        key = self.world.create_item("Salva Drift Key")
        for loc in drops:
            self.assertFalse(loc.can_fill(self.multiworld.state, key, False), loc.name)
        fill(self)
        for loc in drops:
            self.assertFalse(loc.item.code in ALL_KEY_IDS and loc.item.game == self.world.game, loc.name)


class TestHubAnyLean(SO2TestBase, HubChecks):
    options = {"world_mode": "hub", "hub_start": "random_town", "boss_checks": False, "expel_progression": False,
               "missable_locations": "remove", "shuffle_shops": "full", "craftsanity": True,
               "killer_move_checks": True, "killer_move_progression": "hero", "starting_character": "any",
               "enemy_dropsanity": "first_drop", "goal": "indalecio_limiter_off", "traps": 30}

    def test_lean(self):
        """No boss checks: the keys only where a chest (or the requirement) lies behind them; no
        Lasguss key (XINE is no location); under story_route any Claude's killer moves (hero) need
        his item, which is then progression."""
        names = [g["name"] for g in self.world.hub_keys]
        self.assertNotIn("Lasguss Mountain Key", names)
        # The Front Line has no chest and Shin is no location: its key only for its shops (full).
        shops = [r for r in self.rows() if r["type"] == "shop" and hub.dungeon_of(r) == "Lacour Front Line"]
        self.assertTrue(shops)
        self.assertIn("Lacour Front Line Key", names)
        for k in ("Field of Intelligence Key", "Field of Power Key", "Field of Courage Key", "Field of Love Key"):
            self.assertIn(k, names)
        self.assertFalse([r for r in self.rows() if r["type"] == "boss"])
        if self.world.party_info.start != "Claude":
            self.assertIn("Claude", self.world.progression_characters)

    def test_pick_at_new_game(self):
        """[charselect agent] starting_character "any": the player picks at New Game among the
        party this world allows (both heroes, items here, and the pool); nobody is counted on."""
        p = self.world.party_info
        self.assertIsNone(p.start)
        self.assertEqual(set(p.choices), set(p.pool) | {"Claude", "Rena"})
        self.assertEqual(self.world.fill_slot_data()["starting_character"], list(p.choices))
        self.assertTrue(self.world.bench)       # the default: every member (none excluded here)
        self.assertEqual(p.choices, CHARACTER_NAMES)


class TestHubFieldsOnlyNoChecks(SO2TestBase, HubChecks):
    options = {"world_mode": "hub", "story_route": "claude", "fienal_requirement": "fields", "boss_checks": False,
               "expel_progression": False, "missable_locations": "remove"}


class TestHubNoBench(SO2TestBase, HubChecks):
    """bench off: the hub's party keeps the game's 8 places (both heroes and a random 6 of the
    other 10), the select offers those 8, and the seed is version 3 without `bench`."""
    options = {"world_mode": "hub", "bench": False, "starting_character": "any"}

    def test_eight(self):
        w = self.world
        p = w.party_info
        self.assertEqual((w.bench, w.party_size(), w.seed_version()), (False, 8, 3))
        self.assertEqual(len(set(p.pool) | {"Claude", "Rena"}), 8)
        self.assertEqual(set(p.choices), set(p.pool) | {"Claude", "Rena"})
        self.assertEqual(len(p.choices), 8)
        self.assertNotIn("bench", w.fill_slot_data())


class TestHubAsk(unittest.TestCase):
    """The rows that ask first (hub.ASK) as the port's `ask` reader takes them: 1-3 lines of 1-34
    characters, the extra options' code offsets and targets, the doors among the seed's P5 doors."""

    def test_ask_rows(self):
        self.assertEqual(sorted(hub.ASK), [532, 533])
        for enc, a in hub.ASK.items():
            self.assertIn(enc, hub.CHAMBERS)
            lines = a["text"].split("\n")
            self.assertTrue(1 <= len(lines) <= 3 and all(1 <= len(l) <= 34 for l in lines), enc)
            self.assertEqual(a.get("again"), "visit", enc)  # (the user: once per visit to Fun City)
            for d in a.get("doors", []):
                self.assertIn(d, [p["id"] for p in hub.P5_DOORS if p["to"]["map"] == hub.CHAMBERS[enc]["map"]])
            for o in a.get("options", []):
                self.assertTrue(1 <= len(o["text"]) <= 34)
                w, c = int(o["window"], 16), int(o["choice"], 16)
                self.assertTrue(w % 4 == 0 and c % 4 == 0 and w < c, o)
                self.assertEqual(set(o["to"]), {"map", "x", "y", "z", "dir"})
        # Marsilio: in from the desk (297), out by the Clerk (305: his chamber) to the antechamber.
        into, out = hub.ASK[532]["options"]
        self.assertEqual((into["map"], into["to"]["map"], out["map"], out["to"]["map"]), (297, 305, 305, 304))


class TestStoryUnchanged(SO2TestBase):
    """A story-mode or open-world seed carries none of hub mode's fields."""
    options = {"world_mode": "open_world", "character_items": True}

    def test_no_hub_fields(self):
        fill(self)
        seed = self.world.seed_data()
        self.assertNotIn("hub", seed)
        self.assertNotIn("bosses", seed)
        self.assertNotEqual(seed["version"], 3)     # 1 or 2, or 4 with the bench (on by default here)
        self.assertEqual(seed["version"], 4 if self.world.bench else 2)
        for k in ("hub_start", "hub_start_town", "fienal_requirement", "fienal_won", "dungeon_keys"):
            self.assertNotIn(k, seed["options"])
        self.assertFalse([i for i in own_pool(self) if i.code in HUB_KEY_OF])
        self.assertFalse([loc for loc in own_locations(self) if ROW[loc.name].get("hub_only")])
