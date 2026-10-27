# Plan: after the 0.02.50 release (Archipelago tests, trackers, world 0.3.0 entrance rando)

Written 2026-10-25 by the main session at the user's request ("draw up a plan of attack, and write
it down in a file for future work"). Three phases, in this order. Each phase starts from this file;
its agents keep their own logs in `docs/handoff/<name>.md` as usual.

## The user's words (2026-10-25)

> After we get everything settled, we need to follow this guide
> https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/tests.md
>
> And then getting ready for the next update: Port 0.03.00 will include the ability to use Japan
> discs, and world 0.3.0 will add Entrance Randomizer to the options using this
> https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/entrance%20randomization.md as a
> guiding principal.
>
> We will start that work after I release and launch the updated port, world, and wiki.

## The user's change (2026-10-26): trackers instead of Japan in 0.03.00

> Can we nil the idea of running the Japan discs for 0.03.00 and look at outputting correctly for
> these two popular trackers? Let me know what they need and what our best plan of action would be
> to get them functional with our game.

The two trackers: https://github.com/FarisTheAncient/Archipelago.git (Universal Tracker) and
https://github.com/black-sliver/PopTracker.git. **Phase B is now tracker support**; its research
and plan are in `docs/plans/trackers.md` (agent "trackers", log docs/handoff/trackers.md). The
Japan section below is **deferred** (not in 0.03.00, no date); it stays here as the reference for
when the user brings it back.

Order: **Phase A** once the 0.02.50 / so2.apworld 0.2.0 release prep is settled; **Phases B and C**
only after the user has released and launched port 0.02.50, world 0.2.0 and the wiki.

Both guides were read in full on 2026-10-25 (the local copies in scratchpad/ap/Archipelago/docs/,
commit 54803be of 2026-10-03, match the live pages). Re-read the live pages when a phase starts.

---

## Phase A: the APWorld's tests to Archipelago's testing guide (docs/tests.md)

**Goal:** apworld/so2/test follows the guide, so the world's tests are what upstream expects and run
fast in Archipelago's CI.

**Status (2026-10-26, agent aptests; docs/handoff/aptests.md):** done (the tests only; no world
change). The suite restructured (bases.py helpers; test_open_world.py, test_party.py split out;
assertAccessDependency for the area keys, the El voyage, the ship routes, the dungeon keys, the
Fienal requirement, skillsanity's skills, killer-move items); 430 passed + 3 xfailed, each test
under a second; the batches moved to `tools/ap_batch.sh` / `tools/ap_batch_check.py`; docs updated.
**It found a world bug, left for the user:** open world with expel_progression off and the
teleporter on can't generate solo (North City Pass has no legal spot); reproducer and candidate
fixes in the handoff, "World bug". The fix (a 0.2.x) takes the 3 expectedFailure decorators off.

What the guide asks, and where the world stands (2026-10-25):

| Guide | State now | To do |
|---|---|---|
| a `test` package with an empty `__init__.py`; nothing defined in `__init__.py` (deprecated) | `test/__init__.py` is empty | keep it so |
| a test base in `bases.py` (or any file not starting with `test`), inheriting `WorldTestBase` with `game = ...` | `bases.py` exists; WorldTestBase used in bases.py, test_world.py, test_hub.py | check every class derives from the base or `unittest.TestCase` on purpose |
| tests in `test_*.py`, classes `Test*` / `*Test`, methods `test_*` (snake_case) | 7 files | audit names |
| WorldTestBase's default tests (`test_all_state_can_reach_everything`, `test_empty_state_can_reach_something`, `test_fill`) per option mix via `options = {...}` | some mixes | one class per meaningful mix: world_mode story / open_world / hub, goals, skillsanity values, shuffle_shops, craftsanity, killer moves, cave, character select, difficulty; `run_default_tests = False` where a class only tests a helper |
| `assertAccessDependency(locations, items)` for logic | few or none | the dungeon keys -> chamber bosses (hub), the Fienal requirement (D6, each value), skillsanity specialty skills -> crafts / the Iseria Queen, killer-move items -> level checks, area keys (open world), the El voyage gate, ship routes |
| no runner-specific features (no `@pytest.mark.parametrize`); parametrize with `test.param` or subtests | none found | keep; convert any loops that build many worlds |
| each test under a second; random / many-seed generation belongs to the release workflow, behind `@unittest.skipIf(<env var>)` | world tests: 380 passed, ~29k subtests (timing not yet measured per test) | measure (`pytest --durations=30`), split or skip-gate the slow ones (an env var such as `SO2_SLOW_TESTS`); move the 96-seed batches (scratchpad/apworld2/batch, check.py) into `tools/` as the release workflow, with minimal reproducers in the normal tests for anything they ever found |
| run scoped: `AP_TEST_WORLDS=so2 pytest -n N` | we symlink worlds/so2 and run worlds/so2/test | add the `AP_TEST_WORLDS` form to docs/ap/apworld.md "Testing" and the release checklist |

Work: one agent ("aptests"), APWorld only; the AP general suite (test/general) and the world tests
must pass before and after, and the batches stay byte-identical (tests only, no world change). If an
`assertAccessDependency` exposes a logic bug, stop and report (that is a world fix, maybe a 0.2.x).
Output: the world's tests restructured, the release workflow script, docs (apworld.md Testing,
release_checklist.md 2.4 / 2.5), and a short report of what each test class covers.

---

## Phase B: tracker support (Universal Tracker, PopTracker)

**Release target (the user, 2026-10-27): port 0.03.00 + world 0.2.2.** Port 0.02.51 + world 0.2.1
before it carry the bug fixes, the hell seed and the bench.

See `docs/plans/trackers.md`. Map images (decided 2026-10-26): rendered by the port from the
player's own disc into PopTracker's user-override folder; nothing from the disc is published.

**Status (2026-10-27; the build log docs/handoff/phaseb.md):**
- **Built:** B1 Universal Tracker yaml-less (utworld), B2 rules as data (rulebuilder), B3 the logic
  export and B4b its Lua evaluator (logicexport, with 0.2.0 Skillsanity rooms tracked with a
  warning), B4a the PopTracker pack (packgen), B5 the port's data-storage keys (dskeys), the map
  render command (maprender), its Windows build, ASan and setup button (portcheck), the rooms and
  lettered doorways (mapdoors), the setup render on a thread (setupthread), story awareness in
  both trackers (storyaware). **In progress:** maptab (B6's map half: the map_names `area` audit,
  the pack's room maps, door panel and click switching, Universal Tracker's map tab;
  docs/handoff/maptab.md).
- **Docs written** (releasedocs, docs/handoff/releasedocs.md): docs/ap/release_checklist.md
  section 12, docs/handoff/windows_check.md "Recheck for 0.03.00", the three release notes
  (docs/release_notes/0.03.00.md, 0.2.2.md, poptracker-1.0.0.md), README.md, the wiki's new
  Trackers page (scratchpad/wiki, uncommitted). maptab's parts are marked TODO (maptab) there.
- **Next: the release candidate** (B7b, after maptab): the version bumps (port 0.03.00, world
  0.2.2; the pack is 1.0.0 already), the RC by section 12, the user's Windows check, then three
  releases (`apworld-v0.2.2`, `poptracker-v1.0.0`, `v0.03.00` Latest), versions.json on main and
  the wiki, only with the user's go-ahead.

---

## Party work, decided 2026-10-26 (built; ships in port 0.02.51 + world 0.2.1, the user 2026-10-27)

- **Hero to slot 1** (docs/handoff/heroslot.md, D1-D4 decided): open world and hub, a starting
  member who isn't the hero takes slot 0 and the lead; the hero goes to slot 1; the lead and slot 0
  follow that member across scenes; the save card keeps the hero's name. ~3 days.
- **12-member bench** (docs/handoff/bench.md, B1-B8 decided): places 8-11 kept by the port in the
  save sidecar, swapped on Party Order (L1/R1 flips the right column between reserve and bench), editable in the
  other camp pages, a `bench` apworld option (every member not excluded becomes an item). Port
  ~10-14 days, APWorld ~1-1.5, docs ~0.5-1.

---

## Deferred (2026-10-26, was Phase B): the Japanese discs

Taken out of port 0.03.00 by the user; kept as reference.

**Goal (the user):** "the ability to use Japan discs". Today the port recognises Japan and refuses it
with a clear message (`game_refuses_the_japanese_release`).

Known (docs/notes/regions.md, regions_text.md; the user's discs: memory "region-discs",
`Emulation/Roms/Star Ocean 2/`):
- Japan (`SLPM_861.05` / `.06`, product code `BISLPM-86105`) is **a different build**: the original
  release NA was localised from. Same archive entry numbers, but only 657 entries byte-identical; ~510
  catalog rows of code differ; real changes in 6 / 7 / 7 party / enemy / spell modules; 4 items and 1
  enemy record differ; 472 maps' scripts differ in length; NTSC 60 Hz; kana / kanji text with
  per-map fonts (the line-break classes are already described in regions_text.md).
- Europe / France / Germany were done by running NA's logic on their data (region_map by position,
  na_reads, text_rules, the 50 Hz scaling); battles 564 / 564 identical per release.

**The first decision (the user's, at the start of the phase):**
1. *NA logic on Japan's data* (as Europe): Japan's text, fonts, data and maps, NA's gameplay code.
   Faster, but Japan's own gameplay differences (the ~510 rows, the module changes) would play as NA.
2. *Japan as its own original:* port Japan's differing code paths too (a JP code profile), measured
   against PCSX-Redux on the Japanese discs, as the play-state rule asks for every region.
3. A mix: Japan's own data and the gameplay differences that change the play state; NA code where
   Japan's differs only by layout.
Recommendation to bring: (3), with the list of differing rows sorted by "changes the play state" first.

Work breakdown (agents, after the decision):
- **jp-scope:** diff Japan's code against NA function by function (REA; the catalog), sort the ~510
  rows, map the 472 changed maps' scripts (offset tables, as region_map did), measure in Redux.
- **jp-text:** the Japanese text system: the encoding, kana / kanji glyphs, per-map fonts, line
  breaking, name entry (a kana keyboard), message timing; setup's text (port strings stay English
  unless the user wants Japanese port text).
- **jp-code:** the decision's code profile; battles (the 564-encounter sweep per release), event
  sweeps, the endings, both discs.
- **jp-ap:** Archipelago on Japanese discs: seed patch lines are map-script offsets (they differ on
  472 maps): a JP offset translation or per-region patch tables; the item / location tables that
  hold release text; the hub's chamber script offsets (hubask's `ask` rows were checked only on
  EU / FR / DE). Whether one seed plays on every region, as today, is the user's call.
- Verification: regions.md's method (Redux references, event sweeps explained, battles identical to
  the JP original), suites, smoke, a Japanese e2e; README / wiki region tables; release notes 0.03.00.

---

## Phase C: so2.apworld 0.3.0, entrance randomization (docs/entrance randomization.md)

**Goal (the user):** "add Entrance Randomizer to the options using [Generic ER] as a guiding principle".

What Generic ER requires of a world (the guide):
- Split the randomized edges with `disconnect_entrance_for_randomization` (preferred: it names the ER
  targets so coupled mode works); give each `Entrance` a descriptive name ("Arlia: north exit").
- `randomization_type`: one-way transitions are only paired with one-ways, two-ways with two-ways.
- `randomization_group` (an int per entrance) + `target_group_lookup` / `preserve_group_order`
  (`bake_target_group_lookup`) for directional / area / dungeon matching.
- **Explicit indirect conditions are required** (`multiworld.register_indirect_condition`): every rule
  that reads region reachability must declare it. The world registers none today: audit rules.py /
  hub.py / skillsanity.py for `can_reach` of regions first.
- Call `randomize_entrances` exactly in `World.connect_entrances`; the returned
  `ERPlacementState.pairings` go to slot data (the seed) for the client.
- Custom constraints by subclassing `Entrance` (`is_valid_source_transition`, `can_connect_to`,
  always calling `super()`), e.g. to keep sphere 1 open or to keep a hub's dungeons apart.
- Coupled (A -> B implies B -> A for two-ways) or uncoupled.

What SO2 already has to build on:
- The region graph: the late proof and the hub proof (docs/ap/late_proof.json, hub_proof.json) list
  maps, edges and the doors between them; docs/ap/doors.json (door ids = map * 65536 + offset / 4,
  routines for trigger regions); field.json's map names and arrivals.
- The port redirects transfers already: the seed's `doors` rows with `to` {map, x, y, z, dir}
  (`to.while_unbeaten`, the views' `cut_to`), checked ahead of the original door table.
- Known hazards from the hub work: scenes that need a particular arrival (Arcmene's lower door 418,
  Cyril's east side 674), story-gated doors (story mode), multi-map scenes, one-way scene transfers,
  the two worlds (Expel / Energy Nede), the world map's vehicles (Synard, ships, the teleporter).

The decisions for the user (bring them with recommendations at the start of the phase):
1. **Scope (the option's values):** e.g. `entrance_randomizer`: off / world_map (each world-map
   entrance of a town or dungeon leads to another's arrival; the most robust) / dungeons (also the
   doors inside dungeons) / full (towns' interiors too). Recommendation: start with world_map, then
   dungeons.
2. **Which modes:** story mode keeps the story's own order of places, so ER fits the open world and
   the hub best. Recommendation: open world and hub only at first.
3. **Coupled by default** (leaving a place returns you where you came from), uncoupled as an option.
4. **Mix the worlds?** Expel and Energy Nede entrances together or kept apart (groups).
5. Tracker support: the pairings in slot data; whether a PopTracker pack is in scope. **Answered
   by Phase B**: yes; the pairings in slot data plus a found-entrances key cover both trackers
   (docs/plans/trackers.md section 6).

What Phase B left ready for ER (read these before er-model / er-world start):
- **Door ids.** docs/ap/tracker_maps.md "Door ids": a door is a docs/ap/doors.json record (id =
  map * 65536 + offset / 4), a doorway the doors one trigger starts (id = its lowest door id). The
  recommendation, to confirm at this phase's design: an entrance is a doorway, named from its id
  ("Door 1180128"), and ER sends every door of a doorway to the same new destination; the vanilla
  pairing is the layout's `to[].back`.
- **Named entrances in the logic export.** docs/ap/seed_format.md "The tracker export" and "Room
  for the entrance randomizer": entrances are `[name, from, to, rule]`, `to` -1 is an entrance not
  connected yet; `TrackerLogic.connect` (tools/ap_tracker_logic.py) and the pack's
  `so2_logic_connect` connect one when it is found, both tested; optional keys (a region -> map
  ids hint, the pairings) need no new format. The export is written after `connect_entrances`, so
  it is the randomized graph.
- **The live door panel seam.** docs/ap/tracker_maps.md "For the pack (part 2)": no destination is
  painted on any image; the pack shows each lettered doorway's destination from data (the vanilla
  door table, or ER's pairings plus the found-entrances key, "A -> ?" until found). What maptab
  built of it: docs/handoff/maptab.md.
- **The found-entrances key.** `so2_entrances_{team}_{slot}` is reserved (docs/ap/data_storage.md
  "Reserved"); the port's spot for it is marked at the end of `publish_tracker_keys`
  (engine/src/game/ap/ap.cpp). Universal Tracker's deferred entrances already work the same way
  for the story (apworld/so2/story.py: `found_entrances_datastorage_key`,
  `reconnect_found_entrances`, honouring `enforce_deferred_entrances`), the model to follow.
- **Rules that read regions.** A `CanReachRegion` rule is a new kind: add it to
  `logic.RESOLVED_KINDS` / `logic.as_rule` and to the export's `region` body (both evaluators read
  it already); `set_rule` on an entrance registers its indirect condition
  (docs/handoff/rulebuilder.md "Open"; docs/ap/apworld.md "Rules as data").
- **`ut.LOGIC_VERSION`** must be bumped when the same slot data would rebuild other logic
  (docs/ap/apworld.md "Trackers", "When to bump"), and in Universal Tracker's regeneration
  (`re_gen_passthrough`) `connect_entrances` must connect from the slot data's pairings instead of
  randomizing (docs/plans/trackers.md section 6, Tunic's pattern).

Work breakdown (agents):
- **er-model:** the entrance table: every candidate transition with its source map / door id, its
  arrival on the other side, two-way / one-way, its group (world, area, terrain / direction), and the
  hazards above; from doors.json, field.json and the proofs; verified headless per transition (the
  late / hub proof machinery with a seed door table applied).
- **er-port:** the port side: coupled redirects in both directions (the door `to` rows exist; the
  world-map side needs the return position of each entrance), arrival safety (no softlock spawns),
  the views and `ask` rows following their chambers' doors, saves / loads inside redirected places,
  the seed format (a version bump if older ports must refuse ER seeds).
- **er-world:** the APWorld: regions per entrance, indirect conditions, `connect_entrances`,
  `randomize_entrances`, groups and custom `Entrance` constraints, slot data, the option(s), tests
  in the Phase A style, the batches + check.py (every ER seed beatable, the proofs' reach kept).
- **er-proof:** a proof mode that replays a generated ER seed's door table headless (reach every
  location the logic says, both games), plus an e2e.
- Docs: seed_format.md, apworld.md, the player guide, the wiki, release notes 0.3.0.

---

## Standing rules for all three phases
The project's usual ones (docs/HANDOFF.md "Conventions to keep", the user's memories): headless only,
own build dirs, memcap, at most 3 agents / 2 game processes each, scratch XDG_CONFIG_HOME, logs in
docs/handoff/, roll over at ~400k, commit verified pieces (or leave them for the user when the user
says so), enhance never degrade, Archipelago off = the original game, no debugging in AP runs, no game
data in git.

## Small follow-ups from the 0.02.50 Windows check (any time)
- From the 0.02.51 Windows check: the bench join banner shows "joined the party  bench" (no
  parentheses in the game's font; ap.cpp ~1766 "(bench)"); Party Order's refusal to bench the
  hero has no on-screen text; docs/images/setup-archipelago.png still shows v0.02.50.
- Setup's font has no CJK glyphs: a Japanese folder name shows as boxes (Memory cards page). Phase B
  (Japan) will need CJK text in setup anyway.
- A double-clicked Windows run's closing "stopped after N vblanks" (main.cpp, printf to stdout) isn't
  in shattered-stars.log; route it through util/log like the other lines.
- The --debug console window on Windows is titled with the exe's full path; set its title.
- Not covered on real Windows yet: a gamepad with the Esc prompt, display scaling above 100 %.

