"""Open world (`world_mode: open_world`; docs/apworld/apworld.md "Open world", "The El voyage"): the
area keys and the starting ones, the story's stages, the ship landings, where progression may go,
the El voyage gate, the teleporter, the seed's doors; what each key opens (assertAccessDependency).
WorldTestBase's default tests run for every class too."""
from BaseClasses import CollectionState, ItemClassification, LocationProgressType

from ..locations import ROW
from .bases import SO2TestBase, assert_no_gift_progression, check_seed, doors_json, fill, own_locations, own_pool


def key_ways(test, loc):
    """Open world: the ways the logic reaches a location, as the area keys each way needs (any one
    way will do; the events each also needs aside, which assertAccessDependency collects). The
    design (docs/apworld/apworld.md "Open world"): a story stage needs the keys of the areas it walks
    into (data.STAGES) and, ending with the El voyage, the voyage's passes (data.STORY_GATES); the
    teleporter's old man the Fun City Pass; Filia's scene the Central City Pass, the Iseria Queen
    the Cave of Trials and Fun City passes; a chest, gift, boss or shop slot its area's key from
    its early stage (and its landing's), the teleporter proof's or the late proof's route keys
    (rules.open_world_access); a first drop its places' (rules.drop_rule); a guild check its
    guilds' places'; a cave learn point the cave's; a killer-move level, a craft or a level /
    item learn point no key."""
    from .. import skillsanity
    from ..data import STAGES, STORY_GATES
    from ..items import KEY_NAME
    from ..regions import gate_groups
    from ..rules import drop_rule, open_world_access, place_row
    world, side = test.world, test.side
    tele = bool(world.options.expel_teleporter)
    if loc.address is None:
        stages = {f"Story: {name}": [KEY_NAME[g] for g in keys] for name, _, keys in STAGES}
        for gate in STORY_GATES:
            stages[f"Story: {STAGES[gate['stage'] - 1][0]}"] += [KEY_NAME[g] for g in gate_groups(world, gate)]
        other = {"Expel Teleporter (Fun City arena)": [KEY_NAME["Fun City"]],
                 "Limiter Off (Filia, Central City)": ["Central City Pass"],
                 "Iseria Queen (Cave of Trials)": ["Cave of Trials Pass", "Fun City Pass"]}
        return [tuple(stages.get(loc.name, other.get(loc.name, ())))]
    row = ROW[loc.name]
    kind = row["type"]
    if kind in ("killer_move", "craft") or (kind == "learn" and row["how"] not in ("cave", "float")):
        return [()]
    if kind == "guild":
        modes = [m for g in row["guild_maps"]
                 for m in open_world_access(place_row(skillsanity.GUILD_PLACES[g]), side, tele)]
    elif kind == "drop":
        o = world.options
        modes = drop_rule(row, side, True, tele, bool(o.expel_progression), o.drop_progression.current_key,
                          o.encounter_rate.value)[1] or open_world_access(row, side, tele)
    else:
        modes = open_world_access(row, side, tele)
    return [tuple(keys) for _, keys in modes]


class OpenWorldChecks:
    """open_world: the keys, the stages, the doors in the seed, where progression may go, what each
    key opens (assertAccessDependency). `teleporter`: the class's expel_teleporter."""
    teleporter = True

    def test_keys_and_start(self):
        """The Arlia and Fun City passes at the start, and the North City and Noel's House
        passes too unless the teleporter and expel_progression are both on (0.2.1: with either
        off they have no spot in a solo world, TestOpenWorldNoExpelProgression, or only the
        Synard boss); every other key in the pool."""
        from ..data import KEY_GROUPS
        from ..items import KEY_OF, START_KEYS
        later = self.teleporter and bool(self.world.options.expel_progression)
        start = set(START_KEYS) | (set() if later else {"North City Pass", "Noel's House Pass"})
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
        expel = bool(self.world.options.expel_progression)
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
            # expel_progression off: none of it in Expel but our own Expel-story keys, which go
            # where either proof reached (the phase-4 batch's fill failures; "expel keys").
            self.assertEqual(loc.can_fill(st, nede, False), tele and expel, loc.name)
            self.assertEqual(loc.can_fill(st, other, False), tele and expel, loc.name)
            self.assertEqual(loc.can_fill(st, mine, False), tele or late, loc.name)
            allowed["any" if tele and expel else "expel keys"] += tele or late
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
        self.assertEqual(stage_event(gate["stage"]), voyage.item.name)

    def test_voyage_gate_placements(self):
        """After a fill (test_voyage_gate's rule in practice): every El pass of this world sits
        where the voyage proof reached it at the gate, or on a story boss fought before it."""
        from ..data import STORY_GATES
        from ..items import KEY_NAME
        from ..regions import gate_groups
        from ..rules import early_stage, proven_at_gate
        (gate,) = STORY_GATES
        passes = [KEY_NAME[g] for g in gate_groups(self.world, gate)]
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
        Synard (apworld/tools/ap_walkmap.py, apworld/tools/ap_late_proof.py TELEPORTER_ISLANDS). So no
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


    def test_area_keys(self):
        """What each area key in the pool opens (assertAccessDependency, every location of this
        world checked): the locations and story events whose every way there needs it
        (key_ways), and nothing else; with it they are reached. Open world gives every area key
        whatever the options (the seed's 66 doors carry them: test_seed_doors), so a key whose area
        holds nothing in this mix opens no location ("idle"): only an area the story never enters
        can be one (docs/apworld/apworld.md "Open world": Lasguss Mountain, the Mountain Palace, the Cave
        of Trials, Giveaway, map 463's Gallery), and the Cave of Trials Pass never with the cave's
        locations or the Iseria Queen goal. Every other key opens something outright."""
        from ..data import KEY_GROUPS
        ways = {loc.name: key_ways(self, loc) for loc in self.multiworld.get_locations(self.player)}
        start = {i.name for i in self.multiworld.precollected_items[self.player]}
        keys = [g["name"] for g in KEY_GROUPS if g["name"] not in start]
        self.assertEqual(sorted(keys), sorted(i.name for i in own_pool(self) if i.name in keys))
        idle = {key for key in keys if not any(key in w for ws in ways.values() for w in ws)}
        self.assertLessEqual(idle, {"Lasguss Pass", "Mountain Palace Pass", "Cave of Trials Pass", "Giveaway Pass",
                                    "Gallery Pass"})
        o = self.world.options
        if o.cave_of_trials or o.goal.value == 2:
            self.assertNotIn("Cave of Trials Pass", idle)
        for key in keys:
            if key in idle:
                continue
            needs = [name for name, w in ways.items() if all(key in k for k in w)]
            with self.subTest(key=key):
                self.assertTrue(needs)
                self.assertAccessDependency(needs, [[key]])

    def test_voyage_needs_each_pass(self):
        """The El voyage gate: the stage that ends with it needs each of its passes (three, or six
        with the teleporter and expel_progression on)."""
        from ..data import STAGES, STORY_GATES
        from ..items import KEY_NAME
        from ..regions import gate_groups
        (gate,) = STORY_GATES
        voyage = f"Story: {STAGES[gate['stage'] - 1][0]}"
        passes = [KEY_NAME[g] for g in gate_groups(self.world, gate)]
        later = bool(self.world.options.expel_teleporter and self.world.options.expel_progression)
        self.assertEqual(len(passes), 6 if later else 3)
        for name in passes:
            with self.subTest(key=name):
                self.assertAccessDependency([voyage], [[name]], only_check_listed=True)
        self.assertSweptAccessDependency([voyage, "Final Save Point (Fienal 9F)", "Indalecio"], [passes])

    def test_ship_routes(self):
        """The ships: the Herlie stage (the Herlie ship to the Lacour continent lands in Port
        Hilton) needs the Hilton and the Lacour passes; a location of an area reached by a ship's
        landing (data.KEY_GROUPS `landing`: Lacour through Port Hilton, Hoffmann Ruins through map
        231) whose only way is the early one needs the landing's key outright. Most such locations
        also have the late proof's way (the Final Save Point and its `late_keys`, without the
        landing), so only their early way needs the landing (test_ship_landings, with the stages'
        states): every Town of Lacour chest has that late way, 4 of Hoffmann Ruins' don't."""
        from ..data import KEY_GROUPS
        from ..items import KEY_NAME
        for name in ("Hilton Pass", "Lacour Pass"):
            with self.subTest(key=name):
                self.assertAccessDependency(["Story: Herlie"], [[name]], only_check_listed=True)
        landed = 0
        for g in KEY_GROUPS:
            for landing in g.get("landing", ()):
                only_early = [loc.name for loc in own_locations(self) if ROW[loc.name].get("group") == g["group"]
                              and len(key_ways(self, loc)) == 1 and KEY_NAME[landing] in key_ways(self, loc)[0]]
                if only_early:
                    with self.subTest(group=g["group"], landing=landing):
                        self.assertAccessDependency(only_early, [[KEY_NAME[landing]]], only_check_listed=True)
                landed += len(only_early)
        self.assertGreater(landed, 0)

class TestOpenWorld(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world"}


class TestOpenWorldRenaShops(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world", "story_route": "rena", "shuffle_shops": "full", "cave_of_trials": True,
               "character_items": True}


class TestOpenWorldNoTeleporter(SO2TestBase, OpenWorldChecks):
    options = {"world_mode": "open_world", "expel_teleporter": False, "story_route": "claude",
               "missable_locations": "include"}
    teleporter = False




class TestOpenWorldNoExpelProgression(SO2TestBase, OpenWorldChecks):
    """Open world with expel_progression off, a solo world (the reproducer of two fill failures:
    three seeds of the first 64-seed batch, 2026-10-23, the Expel-story keys having nowhere to go,
    fixed by letting our own Expel-story keys stay where a proof reached, test_progression_rules;
    and every solo seed with the teleporter on, found by this class 2026-10-26, the North City
    Pass having no legal spot: a Nede key the first Nede stage needs, kept out of Expel, with
    nothing on Energy Nede reached before it. 0.2.1 gives it at the start, and the Noel's House
    Pass too: test_keys_and_start. docs/handoff/aptests.md "World bug"). WorldTestBase's
    test_fill and this mix's fills (test_voyage_gate_placements, test_seed_doors) are the
    reproducer."""
    options = {"world_mode": "open_world", "expel_progression": False, "story_route": "rena"}


class TestOpenWorldNoExpelProgressionNoBosses(SO2TestBase, OpenWorldChecks):
    """Open world with expel_progression and boss_checks off, a solo world: before 0.2.1 the
    Noel's House Pass's only legal spot was the Synard boss, so with boss_checks off every solo
    fill raised FillError (the release workflow's solo_noexpel set: 11 of 96 seeds,
    docs/handoff/ncpfix.md). 0.2.1 gives it at the start (test_keys_and_start); the fills are the
    reproducer."""
    options = {"world_mode": "open_world", "expel_progression": False, "boss_checks": False,
               "story_route": "claude"}
