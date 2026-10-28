# Plan: after the 0.02.50 release (Archipelago tests, trackers, world 0.3.0 entrance rando)

Written 2026-10-25 by the main session at the user's request ("draw up a plan of attack, and write
it down in a file for future work"). Three phases, in this order; A and B are done (trimmed to a
line each on 2026-10-28), **Phase C is the next work**. Each phase starts from this file; its agents
keep their own logs in `docs/handoff/<name>.md` as usual.

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

So Phase B became tracker support (Universal Tracker and PopTracker), and the Japan work below is
**deferred** (not in 0.03.00, no date; it stays here as the reference for when the user brings it
back).

## Done (trimmed 2026-10-28)

- **Phase A, the world's tests to Archipelago's testing guide:** done 2026-10-26 (world 0.2.1); the
  tests and the release batches are described in docs/apworld/apworld.md "Testing (scratch install)".
- **Phase B, tracker support:** built for port 0.03.00 + world 0.2.2 + PopTracker pack 1.0.0
  (docs/trackers/poptracker.md, tracker_maps.md, data_storage.md; build log docs/handoff/phaseb.md); the
  release candidate and the user's Windows check are what remain (docs/handoff/release.md).
- **Party work (hero to slot 1, the 12-member bench):** released in port 0.02.51 + world 0.2.1
  (2026-10-27).

Both Archipelago guides (tests.md, entrance randomization.md) were read in full on 2026-10-25 (the
local copies in scratchpad/ap/Archipelago/docs/, commit 54803be of 2026-10-03, matched the live
pages). Re-read the live pages when a phase starts.

---

## Deferred (2026-10-26, was Phase B): the Japanese discs

Taken out of port 0.03.00 by the user; kept as reference.

**Goal (the user):** "the ability to use Japan discs". Today the port recognises Japan and refuses it
with a clear message (`game_refuses_the_japanese_release`).

Known (docs/port/notes/regions.md, regions_text.md; the user's discs: memory "region-discs",
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

## macOS build (started 2026-10-28, in parallel)

**Decided for a macOS release (the user, 2026-10-28, after the Mac's candidacy report on daa3a00):**
`.app` bundles ("Shattered Stars.app", "Shattered Stars Setup.app") in a zip; Apple Silicon only
(arm64, macOS 11+); ad-hoc signed with instructions (Open Anyway / xattr), Developer ID maybe later;
wss:// required before release (OpenSSL 3.5 LTS built from source, static, by build_macos.sh; waits
on the user's approval at the Mac, as does the Archipelago setup for the e2e); the game follows the
OS's full-screen state (SDL enter/leave events; shared, also fixes a Linux WM's fullscreen key).
Merged so far: M1 (34fc6e9), M2/M3 (daa3a00). Log: docs/handoff/macos.md.

The user (2026-10-28): their Linux, Windows and Mac machines are all development devices for build,
run and test; "Also start a macOS build now" (alongside 0.03.00, not holding it up). The work runs
on the Mac itself (Remote Control session "Shattered-Stars Mac"), from a git bundle of main, on a
`macos` branch whose commits come back here as patches. Starting points: OpenGL 3.3 core (macOS
needs SDL's forward-compatible flag), platform code in about ten files (paths, utf8, launcher),
FetchContent deps (SDL3, apclientpp, asio, websocketpp, imgui), OpenSSL from Homebrew. First
milestone: configure, build, ss_tests, the game to the title with the user's disc, setup's window.
Packaging (.app, signing) after that. For 0.03.00 the Mac only checks the tracker side
(so2.apworld 0.2.2, Universal Tracker, the PopTracker pack), since no Mac build ships yet.

## Phase C: so2.apworld 0.3.0, entrance randomization (docs/entrance randomization.md)

**The full plan is docs/plans/er.md** (2026-10-28, agent erplan: options, the entrance model with
counts, logic, port, trackers, proof, work breakdown, release, risks, the user's open questions).
This section keeps the history and the requirements it was written from.

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
- The region graph: the late proof and the hub proof (docs/apworld/late_proof.json, hub_proof.json) list
  maps, edges and the doors between them; docs/apworld/doors.json (door ids = map * 65536 + offset / 4,
  routines for trigger regions); field.json's map names and arrivals.
- The port redirects transfers already: the seed's `doors` rows with `to` {map, x, y, z, dir}
  (`to.while_unbeaten`, the views' `cut_to`), checked ahead of the original door table.
- Known hazards from the hub work: scenes that need a particular arrival (Arcmene's lower door 418,
  Cyril's east side 674), story-gated doors (story mode), multi-map scenes, one-way scene transfers,
  the two worlds (Expel / Energy Nede), the world map's vehicles (Synard, ships, the teleporter).

**Decided (the user, 2026-10-28):**
- **Scope: all three levels are YAML option values** (the user: "These three options ... should all
  be options for the yaml in the apworld options"): `entrance_randomizer`: `off` (default) /
  `world_map` (the 66 areas' world-map entrances among themselves; inside an area nothing moves) /
  `dungeons` (also the doors inside dungeons; towns stay intact) / `all_doorways` (every doorway,
  ~1,800, town interiors included). Built and proven in that order, all in so2.apworld 0.3.0.
- **Modes:** open world and hub only; story mode refuses ER (an option error at generation).
- **Coupled by default** (leaving a place returns you where you came in); `uncoupled` as an option.
- **The two worlds separate by default** (Expel's entrances among Expel's, Energy Nede's among
  Energy Nede's); a mixing option may come later.
- Also for 0.3.0 (the coordinator's recommendation, to confirm): the indirect-conditions audit
  first (Generic ER needs it); the ~230 items still named by the game's unappraised names
  ("?GUARD (894)") get their appraised names, since location names change anyway in a minor version.

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
   (the tracker plan's section 6, summarised under "What Phase B left ready" below).

What Phase B left ready for ER (read these before er-model / er-world start):
- **Door ids.** docs/trackers/tracker_maps.md "Door ids": a door is a docs/apworld/doors.json record (id =
  map * 65536 + offset / 4), a doorway the doors one trigger starts (id = its lowest door id). The
  recommendation, to confirm at this phase's design: an entrance is a doorway, named from its id
  ("Door 1180128"), and ER sends every door of a doorway to the same new destination; the vanilla
  pairing is the layout's `to[].back`.
- **Named entrances in the logic export.** docs/apworld/seed_format.md "The tracker export" and "Room
  for the entrance randomizer": entrances are `[name, from, to, rule]`, `to` -1 is an entrance not
  connected yet; `TrackerLogic.connect` (trackers/tools/ap_tracker_logic.py) and the pack's
  `so2_logic_connect` connect one when it is found, both tested; optional keys (a region -> map
  ids hint, the pairings) need no new format. The export is written after `connect_entrances`, so
  it is the randomized graph.
- **The live door panel seam.** docs/trackers/tracker_maps.md "For the pack (part 2)": no destination is
  painted on any image; the pack shows each lettered doorway's destination from data (the vanilla
  door table, or ER's pairings plus the found-entrances key, "A -> ?" until found). What maptab
  built of it: docs/trackers/tracker_maps.md and docs/trackers/poptracker.md.
- **The found-entrances key.** `so2_entrances_{team}_{slot}` is reserved (docs/apworld/data_storage.md
  "Reserved"); the port's spot for it is marked at the end of `publish_tracker_keys`
  (port/src/game/ap/ap.cpp). Universal Tracker's deferred entrances already work the same way
  for the story (apworld/so2/story.py: `found_entrances_datastorage_key`,
  `reconnect_found_entrances`, honouring `enforce_deferred_entrances`), the model to follow.
- **Rules that read regions.** A `CanReachRegion` rule is a new kind: add it to
  `logic.RESOLVED_KINDS` / `logic.as_rule` and to the export's `region` body (both evaluators read
  it already); `set_rule` on an entrance registers its indirect condition
  (docs/apworld/apworld.md "Rules as data").
- **`ut.LOGIC_VERSION`** must be bumped when the same slot data would rebuild other logic
  (docs/apworld/apworld.md "Trackers", "When to bump"), and in Universal Tracker's regeneration
  (`re_gen_passthrough`) `connect_entrances` must connect from the slot data's pairings instead of
  randomizing (Tunic's pattern).
- **From the tracker plan (section 6), for ER:** the pairings from `randomize_entrances(...).pairings`
  go into slot data; in Universal Tracker's regeneration `connect_entrances` connects from them and
  `logic_version` is bumped. Non-spoiling play uses deferred entrances: with
  `enforce_deferred_connections` not "off", the randomized entrances stay hanging,
  `found_entrances_datastorage_key` = `so2_entrances_{team}_{player}` (or a key per entrance), and
  `reconnect_found_entrances` connects what the port reports; the port writes a transition to that
  key the first time a redirected door is used (a small addition on top of the data-storage
  `Link::set`). The logic export carries the post-ER region graph, so the pack's evaluator needs no
  change; it shows a pairing only once it is in the entrances key, and
  `poptracker_entrance_mapping` / entrance sections serve UT's map tab. Door labels are live (the
  user, 2026-10-27): no destination is baked into a map image.

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
The project's usual ones (docs/CONVENTIONS.md, the user's memories): headless only,
own build dirs, memcap, at most 3 agents / 2 game processes each, scratch XDG_CONFIG_HOME, logs in
docs/handoff/, roll over at ~400k, commit verified pieces (or leave them for the user when the user
says so), enhance never degrade, Archipelago off = the original game, no debugging in AP runs, no game
data in git.

## Small follow-ups from the 0.02.50 Windows check (any time)
- From the 0.03.00 Windows recheck: with Archipelago off, setup's notice pushes the "Render tracker
  maps for PopTracker" button below the fold at 125 % display scaling.
- From the 0.02.51 Windows check: the bench join banner shows "joined the party  bench" (no
  parentheses in the game's font; ap.cpp ~1766 "(bench)"); Party Order's refusal to bench the
  hero has no on-screen text; docs/images/setup-archipelago.png still shows v0.02.50.
- Setup's font has no CJK glyphs: a Japanese folder name shows as boxes (Memory cards page). Phase B
  (Japan) will need CJK text in setup anyway.
- A double-clicked Windows run's closing "stopped after N vblanks" (main.cpp, printf to stdout) isn't
  in shattered-stars.log; route it through util/log like the other lines.
- The --debug console window on Windows is titled with the exe's full path; set its title.
- Not covered on real Windows yet: a gamepad with the Esc prompt, display scaling above 100 %.

