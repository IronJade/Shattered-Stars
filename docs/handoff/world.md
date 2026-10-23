# World map / field gaps: continuation log

Worker: world agent (phase 3: closing the field program's remaining gaps). Build dir:
`engine/build-world`. Scratch (on disk): `scratchpad/world/` (Redux driver `world.lua` +
`run_redux.sh`, comparison scripts `cmp.py` / `side.py`, Ghidra helpers `ghd.sh` /
`gha.sh` on the project `analysis/ghidra_world`). Nothing is committed by the worker; the
coordinator commits.

## Done and verified (committed)

- Phase 1-2: world map overlay 0xc1e, vehicles, bunny, courier, minimap, insert-disc
  screen (b906414, 69b33dd, 6c88bd4). Notes: `docs/notes/field_overlay_c1e.md`.
- Phase 3 step 1 (catalog triage, party_swap_characters, screen clamp flag, draw_list
  bounds): b32c1a9.
- Phase 3 step 2 (afterimages, encounter voice streaming, field_load_map /
  field_unload_map teardown, voice budget floor): 1879d21.
- Phase 3 steps 3-4 (Voice Collection, readability renames): 6ddcb4d.
- Phase 4 step 1 (saves byte-compatible: field save icon, buffer leftovers, snapshot
  refresh; SS_STATE_DUMP; test_save): 4b86443.
- Phase 4 step 2 (mods item / shop / enemy overrides, debug overlay, smoke cases): 357e64b.

## Phase 5 plan (coordinator, 2026-10-22): battle spells and items

Split off from the battle agent's queue (its step 4). New files battle/spells*,
battle/spellcast*. Build `engine/build-world`, scratch `scratchpad/world/`.

1. The spell system: spell_cast 0x80062c24 and the spell routines (0x80062000-0x80064fff),
   the #2129 SpellCast API (0x80085518-0x80088054), the spell data loader (spell_data_*
   0x80048000-0x80048fff), the unit side (0x80052000-0x80053fff: unit_can_cast,
   unit_spell_mp_cost, unit_request_spell_data, unit_begin_cast, unit_cast_effects,
   unit_update_casting), shot_hit_magic 0x800547d4 (shot.cpp, flagged), the spell-clash /
   skill-notice / scan windows. Then the callers: the party cast states (battle agent's
   party_states.cpp) and the enemy spell path (enemy_try_cast / enemy_state_chant /
   enemy_state_cast / enemy_release_spell in enemy_ai.cpp); delete the enemy agent's
   stand-ins in enemy_support.{h,cpp} as the real ones land (flagged).
2. Battle items: the command ring menu and item list (input_ring_menu_open / update
   0x8004c000-0x8004dfff, input_open_member_select), item use in battle.

Verify against Redux (RNG draw counts, unit state / HP / MP, effect frames) with
scratchpad/enemy/cmp.py's per-reference alignment (CMP_D). Casters: wizard (encounter
340), Dreamshade (412: heal / time stop), Rena's heal (party setup: ask the battle agent).

## Phase 4 plan (coordinator, 2026-10-22)

1. Save / load compatible with the original memory card data, on scratch cards only:
   (a) a port save loads in PCSX-Redux with the same state, (b) a Redux save loads in the
   port with the same state, (c) the port's save block matches the original's byte for
   byte apart from random / time fields (each difference explained). Fix mismatches
   (menu_save.cpp is the menu agent's: minimal edits, flagged).
2. World edits part 2: mods data overrides (item records, shop inventories, enemy stats
   per type via a hook in the battle encounter loader), checked against the original
   value and logged; an in-window overlay with the debug console's status / output (only
   with --debug); smoke cases; engine/README.md.

(Phase 3 plan, done: triage, field TODOs, Voice Collection, readability.)

Sub-steps (reported to the coordinator): 1a spell data cache, spell_cast + the ten
built-in spells (Shot kinds 3/4/6-0xf/0x1d), unit cast side, party / enemy cast states,
shot_hit_magic, windows; 1b SpellCast object + API + kinds 1/2 + a module registry and the
modules of the test casters (wizard: Cure All 0x32, Magnum Tornado 0xb, Foehn 0xa;
Goathead 277: Cure All, Thundercloud 0x10); 1c the other ~50 spell modules; 2 items.

Battle agent's answers (2026-10-22): no character module overrides PartyUnit slots 11 /
40 / 41 (I add them to unit.h + spell_units.cpp; two lines in party_states.cpp); slot 42
spell_hook after spell_cast; party AI stays theirs. Rena setup: port
`SS_BATTLE_POKE="3:0x0=2/2,3:0x20=200/2" SS_BATTLE_RAM=scratchpad/battle/ref/victory_2_3/
ram_field_exit_004046.bin --battle 2:3` (ids 1 Claude 2 Rena 3 Celine 4 Bowman 5 Dias 6
Precis 7 Ashton 8 Leon 9 Opera 10 Ernest 11 Noel 12 Chisato); Redux (copy of
run_victory.sh) `BW_EXTRA_POKE="4047@0x8009a9c0=0x2/2;4047@0x8009a9e0=0xc8/2"` (handoff 3
at 0x8009a9c0, handoff 4 slot-0 record 0x8009acd0); her spell list bytes are in her
record (to find); D = 2 for that dump.

## State at the weekly-limit cutoff (2026-10-22, about 11:10; committed unverified in e37c3a5)

The weekly limit stopped this worker before it reported phase 5. What is in e37c3a5, none of
it compared with PCSX-Redux yet:
- Step 1a, written: `battle/spells.{h,cpp}` (the spell tables, the spell-data cache spell_data_*
  0x800482a8-0x80048c5c, spell_cast 0x80062c24), `battle/spell_shots.cpp` (the ten built-in
  spells as Shot kinds 3 / 4 / 6-0xf / 0x1d; kind 0x12 with +0x10c 1 still logs),
  `battle/spell_units.cpp` (the unit casting side 0x8005287c-0x80053054: chant, release,
  clash; EnemyUnit::state_cast / release_spell / enemy_state_chant), `battle/spell_windows.{h,cpp}`
  (spell-clash, skill-notice and scan windows), `battle/spell_fx.{h,cpp}` (drawing helpers the
  spells use).
- Step 1b, partly: `battle/spellcast.{h,cpp}` (the SpellCast object and its API; a kind with no
  handler logs "not ported yet" and ends the cast), `battle/spell_modules.{h,cpp}` (registry by
  block hash) with one module, `battle/spell_mod_foehn.cpp` (spell 0xa).
- Callers wired (flagged edits in other agents' files): `unit.h` (Unit [3] can_cast is
  unit_can_cast; PartyUnit [3] can_cast, [11] release_spell, [40] state_cast, [41]
  state_spell_release, all in spell_units.cpp), `party_states.cpp` (states 0xe / 0xf and
  command 4 -> unit_begin_cast), `enemy_ai.cpp` (enemy_state_chant, spell_data_cancel_owner
  in state 0, unit_begin_cast; enemy_try_cast / state_cast / release_spell moved to
  spell_units.cpp), `combat.cpp` (unit_release_spell_data cancels the spell data),
  `shot.cpp` / `shot.h` (the spell kinds through spell_shot_update in Shot::update's default;
  shot_hit_magic 0x800547d4 next to shot_apply_damage), `sprite.cpp` (effect_skill_popup ->
  skill_notice_show), `battle_program.cpp` (spell_data_update in the main loop,
  spell_data_clear_all at victory / the common end, spells_reset + spell_windows_reset at
  init and battle_free_all). The enemy agent's spell stand-ins and screen_fade_create are gone
  from enemy_support.{h,cpp} (it includes spells.h / spellcast.h / spell_fx.h);
  enemy_mod_bark's kind-0x36 handler and enemy_mod_indalecio's kind-0x3b handler run on the
  real SpellCast (spellcast_update dispatches them); cyril / decus / vesper read
  spellcast_slot0() (dias / zand: the coordinator's follow-ups).
- Step 2, started: `battle/command_menu.{h,cpp}`: the command ring (input_ring_menu_open /
  update 0x8004cf5c / 0x8004d178), the member select (0x8004b1ac / 0x8004b65c), escape
  (0x8004d914) and the #1984.0 list menus (spell, item, tactics) with the description balloon.
  NOT yet in CMakeLists.txt nor called from input.cpp (written, never compiled);
  item_use_battle 0x80082dec is a logging stand-in there.
- No catalog rows for phase 5 yet (the +118 rows in e37c3a5's functions.csv are other agents').
- HEAD (e37c3a5) builds and passes every suite: ss_tests 78/0, ss_game_tests 74/0 with and without the disc, smoke 21/21; encounters 2 10 47 65 277 280 285 315 340 412 457 run 2000 frames headless with no crash and no unported-code line (coordinator, after the cutoff).

## In progress (since the restart, 2026-10-22 afternoon)

- Verified against Redux (scratchpad/world/spell, cmp.py at the reference's own D; all RNG
  samples and all unit samples equal): encounter 516 Shin (Foehn 0xa, module), 27 Magius
  (Firebolt x3, Starlight x3), 41 Beastmaster (Wind Blade, Starlight x2), 3000 / 2000 vsyncs.
  340 wizard and 277 Goathead match too (they never cast in 6000 vsyncs idle).
- Catalog: the port column of every phase-5 function set (scratchpad/world/spell/cat_port.py),
  rows for the Foehn / Starlight modules (module `battle_spell_<name>`, README updated;
  scratchpad/world/spell/catalog/*.csv + cat_mods.py).
- New since e37c3a5 (on disk): `battle/spell_mod_starlight.cpp` (spell 0x12, af4ef66c53;
  in 858a228), kind 0x31 vortex + effect_spawn_t31_at (spell_shots.cpp / spell_fx.{h,cpp}),
  spellcast.h `aux_point[4]` (+0x124), spell_modules.h helpers (spellcast_leads_camera,
  spellcast_model), `SS_SPELL_TRACE=1` (a line per spell_cast) and
  `SS_BATTLE_CAST="FRAMES:SLOT:SPELL:p0|e0;..."` (a forced party cast, spells.cpp); the Redux
  driver copy `scratchpad/world/spell/redux/explore.lua` has the same `BW_CAST`, and
  run.sh takes `BW_EXTRA_POKE`.
- 1b modules written: magnum_tornado (0xb, Redux 197 exact), cure_all (0x32), thundercloud
  (0x10) (+ effect_spawn_streak_at in spell_fx); Foehn renamed (was "Feehn"); original_bugs.md
  S1 (Foehn's effects on the wrong side) fixed; catalog rows for all five modules.
- Step 2, item use: VERIFIED (18:30). `battle/items.{h,cpp}`: item_use_battle 0x80082dec (all
  effect kinds), its helpers, Shot kinds 0x2e / 0x2f / 0x30, shot_spawn_sparkle_on_target.
  Redux item tests on encounter 340 (`scratchpad/world/spell/item_cfg.py`: 16 items poked
  into the inventory, forced uses): `ref/it340_3` (7 items: HP heal, kind 0x40, status 2:
  0x40000000, petrify all / one (the battle ends: the wizard turned to stone, as on the
  console), stun, MP drain) and `ref/iu340_3` (11: drain, scan, rolling boulder, 3 balls,
  bouncing balls, bombs x3 kinds, stun, heal+curse, kind 0x47): every RNG sample and every
  unit sample exact (one party sample at it340 b1290 differs: a hit reaction one frame early in
  the over state, the wizard's melee: battle agent's area); captures match (scan window, the
  boulder, the bomb's dark sphere near the camera). Suites green: ss_tests 78/0,
  ss_game_tests 74/0 (SS_DISC1), smoke 21/21.
  - original_bugs.md S2 (kind 0x5c's blasts use the enemy's y as z) fixed; S3 (a monster's
    counter-cast after an item hit has no target; the Shot spells aim at "address 0") fixed.
  - spellcast_retarget now reads a null target the way the console does (kernel bytes at
    0x123 / 0x125: party slot 2), so such a SpellCast spell goes to the first standing member
    from slot 3 on (it was dropped: found by iu340, the wizard's Foehn after the bombs).
  - Flagged edits: unit.h / unit.cpp unit_check_move takes `const BObj*`; title/new_game.cpp
    scale_colour keeps IR0's low 16 bits; input.cpp input_update calls
    `debug_forced_items()`; spell_fx.h declares voice_thank_healer.
  - Test drivers: `SS_BATTLE_ITEM="FRAMES:SLOT:INDEX:TARGET;..."` / Redux `BW_ITEM`;
    `BW_RNG_TRACE=LO:HI` logs every RNG call's site in Redux; run.sh `BW_CARD_TAG`.
- 1c: ALL 55 modules registered and building (18:30): groups A (13), B (14), C (16) from
  the helpers + my 5 + the enemy agent's 7. `scratchpad/world/spell/register.py NAME...`
  registers (kModules key, header, CMake); `cat_mods.py NAME...` adds the catalog rows (all
  added). Not yet verified against Redux.
  - Missing: model_shatter 0x8005f30c + the PolyShard pool (0x800600a0-0x800606f8): Southern
    Cross, Delay and Haste have empty stand-ins (RNG differs from the shatter on). A helper
    (started 18:32) ports it into new `battle/shatter.{h,cpp}`; then switch the three modules.
  - Helpers' ORIGINAL BUG? notes to decide after verification (listed in their reports;
    examples: Black Saber / Sonic Saber 4-entry hit lists overflowing, Gremlin Lair's waits
    without timer ticks, Press / Gravity Press stars at 0/120/120/240, Deep Mist without the
    pause check, Lunar Light's second spark ring, Raise Dead's funnel y, Silence / Blood
    Sucker / Curse heights, Delay / Haste dz bounds, Shadow Flare's shimmer phase).
  - Port gaps from the helpers: Earthquake / Explode set a stand-in of g_stage_height_fn
    0x8006fe04 (unit_draw_model's hook, TODO in unit.cpp / stage.cpp: units don't ride the
    waves yet); Divine Comedy reads Indalecio's +0x2f8 through `caster->flying` (needs an
    accessor); Raise Dead's music save / restore mapping to check.
  - Duplicated local helpers to move to spell_fx: item_fx_sparkle (6 copies), draw_disc
    0x8007cc5c (3), draw_wave_grid 0x8007d9e0 (4), voice_release 0x8007fd54 (2),
    model_find_vertices_near 0x80061694 + sv_distance_xz 0x800627c0 (2),
    camera_orbit_object (2; camera_orbit_unit could take a BObj*), line_trail 0x80076454..
- Rena test redone with the forced casts applied where the port applies them (both at
  input_dispatch now; the old per-vsync Redux poke landed a frame later than the port's,
  which was the whole "+4 RNG at Cure Light" of r340): `ref/rn340_3` vs `prn340`: all 240
  RNG samples and all enemy samples exact over 2400 frames (Heal, Cure Light, Antidote,
  Grave, Word of Death, the wizard's Foehn and Magnum Tornado); 3 party samples show the
  hit reaction a frame early in the port (battle agent's area, as in it340).
- Module sweeps (Redux, encounter 340, max 3 instances): the caster is the class's
  character (Rena 2 / Leon 8 / Celine 3 / Noel 11, the party loader sets the class from it)
  with every list spell learned (handoff 4 record +0x3c..), HP 30000, MP 999; the wizard made
  passive (AI odds 0, SS_BATTLE_ENEMY_ODDS / BW poke during the intro). `sweep_cfg.py RUN
  [FOR]`; runs i9a i9b i10a i10b i11a i11b i12 cast the class's spells one after another
  (SS_BATTLE_CAST_IDLE / BW_CAST_IDLE: each waits until the caster is idle, no command
  pending and no SpellCast runs); `start_sweep.sh RUN FOR` starts one (captures kept in
  `ref/sweep_raw/RUN`). Port side: `(source sweep_RUN.sh; NO_MASH=1 ./run_port.sh 340 3
  ref/i340_3 pRUN FOR)`, then cmp.py. All 7 idle runs play every cast to its end in the port.
  - `ref/s9a_3` (fixed-time casts): exact through Cure All, Fairy Light, Dispel, Silence,
    Deep Mist, Energy Net (b3310); Growth then differs: the console ran slow (frame ticks 3
    under Growth's load; the port never slows down).
  - So the Redux driver now logs the slow frames (`BW_TICK_LOG=1`: "[bw] tick F T" at
    input_dispatch) and the port can replay them (`SS_BATTLE_TICK_MAP="F:T,..."`, F the
    battle frame counter before the frame: battle_debug.cpp, flagged); and the spell casts /
    finishes (`BW_SPELL_TRACE=1`, as SS_SPELL_TRACE).
  - Running (19:00): i11a, i12, i10a (with the tick log and spell trace).
- model_shatter + PolyShard ported by the helper (`battle/shatter.{h,cpp}`, in CMake;
  shatter_reset from spells_reset); Southern Cross / Delay / Haste use it. The task list is
  now 320 entries (battle.h, flagged: g_tasks is 320 on the console; a shatter adds up to
  192 tasks).
- Shared helpers moved: item_fx_sparkle (items.h, 6 local copies gone), draw_disc /
  draw_wave_grid (spell_fx, 5 + the enemy agent's star_flare copy gone: flagged),
  g_stage_height_fn 0x8006fe04 (spell_fx; unit_draw_model now lifts grounded units by it:
  unit.cpp, flagged), enemy_indalecio_transformed (enemy_mod_indalecio.cpp, flagged) for
  Divine Comedy.
- Port debug: `SS_RNG_SITES=LO:HI` prints the call site of RNG draws LO..HI (rng.cpp;
  run under `setarch x86_64 -R`, PORT_WRAP in run_port.sh, then addr2line); Redux
  `BW_RNG_TRACE=LO:HI` the same on the console.
- Findings:
  - Spell data load timing: the port's data is ready one frame after the request; the
    console's CD read can take longer, so a monster whose chant is short releases its spell a
    frame or two later on the console (m111 Goathead's Thundercloud: ref b602, port 600; then
    everything shifts). Not a port bug (no slowdown modelling); verify modules with casts whose
    data loads in time (the party sweeps).
  - Rena test r340: resolved (test-driver timing, see above).
  - Never `pgrep -f` / kill by a pattern that the current shell's own command line contains
    (it matched the shell itself once); use the PIDs a listing printed.
  - Redux: `pgrep -f` waits on a pattern also match their own command line (the r340d queue
    never started); use a pid file or a pattern the waiter does not contain.

## Next steps

1. Compare the idle sweeps as they finish (i11a, i12, i10a running; then i9a, i9b, i10b,
   i11b): extract the tick map from the ref log ("[bw] tick F T" -> F - T:T), run the port
   with SS_BATTLE_TICK_MAP, cmp.py, and the spell timelines ([bw] spell vs [spell]); fix the
   modules; decide the helpers' ORIGINAL BUG? notes (fix after the faithful version matched;
   S-entries in original_bugs.md).
2. Remaining duplicated helpers (earthquake / explode: model_find_vertices_near,
   sv_distance_xz, obj_set_matrix_parent / child; gremlin_lair / press camera_orbit_object;
   reflection's line_trail; acid_rain's voice_release copy (enemy_support.h has one)).
3. W1 (the post-battle report's MAG row under the window clip) when convenient.

## Findings not yet in the catalog

- Spell data entries 0x8e1-0x981: command lists (5 sound, 6 code, 3 texture, 4 model, 7/9
  part, 8 raw); 55 distinct code modules (block k linked for slot k: 0x8008cc00 /
  0x8008e600); `scratchpad/world/spell/modules/index.txt` maps module hash -> entries ->
  spells. Spell names: #1980 string 0x3e7 + spell, each byte + 1 (0x0a Foehn, 0x0b Magnum
  Tornado, 0x10 Thundercloud, 0x32 Cure All ...).
- Analysis program: `battle_s2.elf` in analysis/ghidra_world (the #2129 image with #1984.0
  / .1 patched in, the SpellCast code kept; PSX language: the default MIPS64 R6 pick breaks
  `jr ra`), global labels applied (`scratchpad/world/ApplyLabels.java`).
