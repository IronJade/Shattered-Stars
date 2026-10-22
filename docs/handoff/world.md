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
modules of the test casters (wizard: Cure All 0x32, Magnum Tornado 0xb, Feehn 0xa;
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
  block hash) with one module, `battle/spell_mod_feehn.cpp` (spell 0xa).
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

## Next steps

1. Verify 1a against PCSX-Redux: Rena's Heal / Cure Light (setup above), the Beastmaster's
   heal; RNG counts and unit HP / MP at the reference's own offset (CMP_D).
2. Finish 1b: the modules the test casters need (wizard 340: Cure All 0x32, Magnum Tornado
   0xb, Feehn 0xa; Goathead 277: Cure All, Thundercloud 0x10); verify against 340 and 277.
3. 1c: the other ~54 spell modules (helpers allowed, up to six, write-only; see the rules from
   the coordinator).
4. Step 2: item_use_battle and the rest of item use; verify the command menus against Redux.

## Findings not yet in the catalog

- Spell data entries 0x8e1-0x981: command lists (5 sound, 6 code, 3 texture, 4 model, 7/9
  part, 8 raw); 55 distinct code modules (block k linked for slot k: 0x8008cc00 /
  0x8008e600); `scratchpad/world/spell/modules/index.txt` maps module hash -> entries ->
  spells. Spell names: #1980 string 0x3e7 + spell, each byte + 1 (0x0a Feehn, 0x0b Magnum
  Tornado, 0x10 Thundercloud, 0x32 Cure All ...).
- Analysis program: `battle_s2.elf` in analysis/ghidra_world (the #2129 image with #1984.0
  / .1 patched in, the SpellCast code kept; PSX language: the default MIPS64 R6 pick breaks
  `jr ra`), global labels applied (`scratchpad/world/ApplyLabels.java`).
