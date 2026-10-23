# Enemy classes: continuation log

Worker: enemy agent (the battle program's monster classes: the 16 resident families, the
#2129 image's default enemy-code modules and the encounter packs' modules). Build dir:
`engine/build-enemy`. Scratch (on disk, git-ignored): `scratchpad/enemy/`. The worker does not
commit; the coordinator does. Notes for readers: `docs/notes/battle_enemies.md`.

## Layout (new files, all mine)

| File | Contents |
|---|---|
| `battle/enemy_classes.h` | `enemy_class_create(type)` (the factory's class choice), `enemy_modules_reset/load` (the pack's code list), `enemy_adopt`, `enemy_extra_data`, `enemy_try_approach` |
| `battle/enemy_families.cpp` | the 16 resident families (0x8003e248-0x80041600) |
| `battle/enemy_modules.{h,cpp}` | module registry: the 40 pack blocks (FNV-1a 64 of the unpacked bytes + size) + 3 image defaults, slot state, the factory switch for module types, `enemy_module_u8/s16/s32` (a module's own tables by link address), the 43 `enemy_mod_<name>_create()` |
| `battle/enemy_mod_<name>.cpp` | one file per block: guardian, nicolus, jibril (image defaults) and the 40 pack blocks |
| `battle/enemy_support.{h,cpp}` | resident helpers only the monsters need so far (draw_beam, draw_textured_patch, draw_quad_gt4, gte_set_view_matrix, draw_tri_g3, vec_random_in_disc_xz, bobj_poly_tag, bobj_set_pos, collider_set_attack_box, bobj_front_x, unit_play_anim_ex, unit_throw_weapons, unit_is_disabled, shot_skill_camera_end), logging stand-ins for what the battle agent has not ported (voices, load queue, shot_draw_frame_quad; the spell stand-ins were replaced by the world agent's spells), `SS_ENEMY_TRACE` |

## Edits to other agents' files (hooks only)

- `encounter.cpp`: `enemy_create_by_type` builds `enemy_class_create(type)`; `load_formation`
  (3) calls `enemy_modules_reset()` then `enemy_modules_load(kind, block)` for each code-list
  entry; `enemy_adopt()` (a module's mid-battle enemy joins g_enemies: slime / gelatin / gerel
  splits) and `enemy_extra_data(i)` (g_enemy_extra_data accessor: Cyril, Dias, Vesper, Decus,
  Indalecio clips); include + header comment.
- `enemy_ai.cpp`: `enemy_think` starts with `enemy_debug_trace(e)` (SS_ENEMY_TRACE); include.
- `engine/CMakeLists.txt`: enemy_support.cpp, enemy_families.cpp, enemy_modules.cpp, and the 43
  enemy_mod_*.cpp after pc_claude.cpp.

## Done

- Step 1 (families): ported; Redux-exact (see Verification).
- Step 2 (modules): all 43 classes ported, registered and built: Guardian (mine); 40 pack
  blocks + Nicolus / Jibril by six helpers (reports below), each file reviewed and then
  consolidated: the split copies (slime, gelatin, gerel) go through `enemy_adopt`; local
  stand-ins for g_enemy_extra_data, g_spell_landed, g_spell_effect_started and g_spellcasts[0]
  replaced by the shared `enemy_extra_data()`, `spell_landed_flag()`,
  `spell_effect_started_flag()`, `spellcast_slot0()`; stingray's `shot_skill_camera_end`
  moved to enemy_support; the stingray / magichand block Shot handlers (kinds 0x26 / 0x29)
  spawned as callback Shots (kind 0x33).
- Step 3 (factory hook): `enemy_create_by_type` -> `enemy_class_create`.
- Sweep: every disc-1 encounter (0-563) runs 3000 frames headless (new-game party, Claude
  sturdy and idle, stage 3) without a crash (`scratchpad/enemy/sweep.sh`; one Niquia crash
  fixed: pre_think with no target reads a zeroed BObj, the console reads low RAM).

## Verification (Redux, scratchpad/enemy)

`redux/run.sh` (or `batch.sh`) records the original: forced battle on stage 3, Claude poked
sturdy and left idle, RNG draws and every enemy's state / position / HP / scratch words every
10 vsyncs. `run_port.sh` runs the port from the reference's field_exit dump; `cmp.py`
compares. The original's phase differs between runs (the first frames' tick pattern decides
when its camera intro ends), so cmp.py tries offsets 0..8 and uses the one with the fewest
RNG mismatches (`CMP_D=n` forces one; for the boss fights `CMP_DMAX`, `CMP_R` (RNG draws the
original makes in its intro voice that the port makes during setup) and `CMP_FROM`). Results
at HEAD 416d7b2 + the world agent's spells (1500 vsyncs each, or to the battle's end):

- Exact (all RNG samples, all enemy samples): families 315 Bugbear, 47 Sandglass, 412
  Dreamshade, 205 Shielder, 320 Shout, 92 Ooze, 193 Bandit, 19 Landworm, 202 Troll, 21
  Alraune, 268 Lizardaxe, 352 F-thiefL99, 280 Darkcrusader (now that Shot kind 2 is ported),
  277 Goathead (now that enemy_try_cast and its spell are ported); modules 457 Owlbear
  (guardian), 80 Cockatrice (carlaeagle), 269 Vopalbunny (bunny), 314 Slime, 43 Gerel, 99
  Salamander, 50 Scewer (arcmene), 326 Hellhound (hounddog), 34 Stingray, 285 Miel64 (miel),
  286 Kidnier (visseyer), 107 Kitty, 282 Rikiha (ricki), 191 Alen-Tax (ogre), 563
  Gelatinblock (gelatin), 240 Controller, 206 Azamgil, 284 Ericodus, 523 Gargoyle, 543
  Snowman, 153 NiquiaHG (niquia), 186 Weirdavia (xine), 211 Ghost, 204 Dias, 520 Berle, 521
  Synard, 530 Marsilio, 531 Bark, 533 Shigeo, 536 Ruprecht + Nicolus + Jibril.
- Exact until a spell code module the port does not have yet takes over ("spell code module
  ... not ported yet; the cast ends"; the world agent's): 197 Nightmare (succubus) to b700,
  spell 16; 265 Zand to b1180, spell 12; 430 Weirddevil (shin) to b560, spell 51; 515
  G-Celesta to b400, spell 22; 528 Lover to b680, spell 28; 538 Cyril to b530, spell 57
  (original 57 vsyncs later: its boss intro voice); 537 Vesper + Decus to b390, spells 55 /
  56 (Vesper's attack 3 waits for the kind-0x37 SpellCast's special-move pause); 539
  Indalecio to b430, spell 22. In each the divergence starts the frame the cast is released.
- 524 Guardbox / Magicbox / Workbox / Magichand: diverges at b310, when the boxes toss
  (Shot kind 0x27, not ported: the battle agent's).
- 420 four Robinfakes: exact to b1110. From b1078 to b1110 the original runs 3-vsync frames
  (slowdown: 4 robins' 5-arrow volleys; per-vsync reference `ref/n420_3`), the port always
  2, so the draws regroup and at b1128 a different robin starts attack 3. Not a code
  difference; the port does not model the console's CPU load.
- Fixed by the Zand reference: Zand's and Dias's leap step (0x80088760 / 0x80088744) tested
  the landing the wrong way round (`pos y < ground` instead of `ground < pos y`, i.e. they
  landed at once); in HEAD since 416d7b2.
- Not exercised by these references (Claude stands idle and does no damage): Bark's
  kind-0x36 SpellCast (every tenth of its HP lost) and Indalecio's transformation (kind 0x3b).

## Committed

- 5f04296 (commit point 1): families, enemy_support, enemy_modules registry, Guardian.
- 3f6ac33 (commit point 2): all 43 modules, catalog rows, notes.
- 416d7b2 (the user, unverified): the world agent's spell wiring in enemy_support and seven
  modules, and the Zand / Dias landing fix. Verified above.

## Helper reports (all six groups done; helpers finished, none running)

Full texts: `scratchpad/enemy/reports/group<N>.txt`. What still matters:

- Group 1 (carlaeagle gelatin arcmene hounddog salamander kitty ricki ogre snowman bunny):
  carlaeagle's feathers start at absolute y -0x78 (original quirk); arcmene's blink test
  compares an index with 0x3e8000 (always true, original bug); kitty's attack 2 is
  unreachable for its only type 0x5a; bunny ignores unit_find_approach_point's result.
- Group 2 (ericodus xine gargoyle stingray gerel controller niquia azamgil guardbox lover):
  stingray's kind-0x26 handler and magichand's kind-0x29 handler are block code that the
  resident g_shot_kind_table points at; guardbox needs Shot kind 0x27 (shot_k27_tossed_item
  0x8005b558, resident); ericodus attack 5 can freeze in state 13 (original); niquia writes
  the host's +0x24e only for an enemy host (port guard).
- Group 3 (visseyer slime miel shin magichand succubus ghost berle): ghost needs Shot kind
  0x2d (0x8005b9d0); magichand attack 5 dereferences enemy_random_party_member() unchecked
  (as the original; only matters with the whole party down).
- Group 4 (cyril dias zand bark synard celesta): Cyril's attack 2 waits for
  g_spell_effect_started (set by spells) and hangs until spells exist; Dias leaks its slash
  Shot until shot_draw_frame_quad 0x8005b23c is ported; bark's SpellCast kind-0x36 handler
  (`bark_cast_step`, block code) is ported but unused until SpellCast exists.
- Group 5 (marsilio shigeo ruprecht vesper decus): Vesper's / Decus's attack 3 end only
  through their spell's SpellCast (stuck until spells exist).
- Group 6 (nicolus jibril indalecio): Indalecio's transformation is SpellCast kind 0x3b
  (`spellcast_kind3b_transform`, block code, ported, unused); his second-form writes to
  g_spell_data_entry 0x8006d1f2 are a local log-once stand-in.

## For the battle agent (missing callees the monsters use)

- Shot kinds 0x27 (guardbox tossed item 0x8005b558: enc 524 diverges there) and 0x2d (ghost
  slash 0x8005b9d0); kinds 0x26 / 0x29 are block handlers (stingray / magichand) spawned as
  callbacks; shot.cpp could dispatch them while that block is loaded.
- shot_draw_frame_quad 0x8005b23c (stand-in returns 0: Dias's slash never dies).
- voice_play_entry 0x8007f818 (boss intro voices: the original draws its RNG for them and
  holds the fight about 57 vsyncs in enc 538), bload_request 0x800475c0.

## For the world agent (owns spells and items)

- The spell stand-ins are gone (spells.h / spellcast.h / spell_fx.h); Bark's kind 0x36 and
  Indalecio's kind 0x3b handlers are dispatched by spellcast.cpp.
- Spell code modules the monsters' references reach: 12 (Zand), 16 (Nightmare), 22
  (G-Celesta, Indalecio), 28 (Lover), 51 (Weirddevil), 55 / 56 (Vesper / Decus), 57
  (Cyril). The references above (`scratchpad/enemy/ref/m<enc>_3`) can check them: the port
  matches each fight up to the cast's release.

## Findings

- 40 distinct enemy code blocks on the two discs (disc 2's packs are byte-identical to disc
  1's). Each serves exactly one factory entry (a constructor at a slot base or a vtable inside
  the slot), one class per block. The #2129 image's slot A module is Guardian / Geo-guardian /
  Owlbear (0xbe-0xc0, encounters 457 458 501 512 525 527 558), B is Nicolus (0xc9, enc 536), C
  is Jibril (0xca, enc 536).
- The original's battle phase is not fixed relative to battle_main_loop: in encounter 10 the
  first frames tick 2,2,2.. and the camera intro's first step ends at b159; in 285 they tick
  2,1,2,.. and it ends at b155 (the port: 154 in both). Everything after (appear, state 2, the
  AI batches) follows. Compare with a per-reference offset (cmp.py does).
- Under load the original slows to 3-vsync frames (enc 420, four Robinfakes shooting: b1078
  to b1110); the port always runs 2-vsync frames, so long busy fights drift there.
- Monsters with attack odds 0 (Ooze, Alraune) never start their special attack on their own
  (Redux agrees).
- Forcing an encounter in Redux: `*0x80075710 + 0x264 = ENC` (s16) at the field; +0x99 does
  nothing.
- The action kind +0x12a is uninitialised heap on the console until first set (cmp.py
  ignores it).

## Tools (scratchpad/enemy)

- `scan_packs.py`, `type_map.py` / `type_map.txt` (blocks, types, encounters), `entries.py`,
  `ghd_modules.sh` + `ghidra_scripts/` (block decompiles in `decomp/`), `show.py`.
- `redux/run.sh ENC STAGE OUT` (+ `BW_DENSE=lo:hi` per-vsync camera / tick / appear lines,
  `BW_BATTLE_EVERY=1` per-vsync enemy lines), `batch.sh`, `run_port.sh`, `run_port2.sh`
  (`SS_ENEMY_TRACE=2`), `cmp.py` (`CMP_D=n` forces the offset; `CMP_DMAX`, `CMP_R`,
  `CMP_FROM` for the boss fights), `verify_all.sh [PATTERN]` (reruns the port when the binary
  is newer and prints one line per reference), `sweep.sh FRAMES ENC...` (copy
  engine/build-enemy/shattered-stars to `bin/` first, or set SWEEP_BIN).

## Phase 2 (coordinator, 2026-10-22 evening)

1. The seven monster spell modules (53455d80ad Magnum Tornado is the world agent's):

| Module | Spell | Caster (reference) | File | Who | State |
|---|---|---|---|---|---|
| 6673c9f485 | 5 Deep Freeze | Zand (m265) | spell_mod_deep_freeze.cpp | enemy agent | done, registered; m265 exact over 1500 vsyncs (RNG and every unit) |
| 6563c70f92 | 7 Noah | G-Celesta (m515), Indalecio (m539) | spell_mod_noah.cpp | enemy agent | done, registered; m515 / m539 every unit sample exact to the battle's end, RNG only differs while the original slows to 3-vsync frames under the wall (48 draws a frame in both) |
| cbdc2116f3 | 0x1c Star Flare | Breakwing (m528) | spell_mod_star_flare.cpp | helper C (done) | registered; m528 exact over 1500 vsyncs; draw_wave_grid 0x8007d9e0 ported in the file |
| a6a642e880 | 0x24 Daemon's Gate / 0x41 The Scream | Weirddevil (m430) | spell_mod_daemons_gate.cpp | helper C (done) | registered; m430 exact over 1500 vsyncs |
| c5209066cb | 0x43 Mind Blast | Vesper (m537) | spell_mod_mind_blast.cpp | helper B (done) | registered; m537 exact past the casts to b400, then the original slows to 3-vsync frames; unit states and the party's KO (b1000) still match |
| c325548547 | 0x44 Spicule | Decus (m537) | spell_mod_spicule.cpp | helper B (done) | registered; as Mind Blast (same fight) |
| 155b3550d0 | 0x45 Wind of Destruction | Cyril (m538) | spell_mod_wind_of_destruction.cpp | helper A (done) | registered; m538 RNG exact to b920 (the cast b530 on), unit states exact to b1500; Cyril's aura via enemy_cyril_aura() (enemy_modules.h) |

   Helpers (three, write-only: their module files + `scratchpad/enemy/spell/catalog/`; guide
   `scratchpad/enemy/spell/GUIDE.md`). Registration: one line each in the world agent's
   spell_modules.{h,cpp} (agreed with it). Inputs read-only from `scratchpad/world/spell`
   (modules/, mdec/, entries/; spell names in spell_names.txt). Their asm.txt shows switch
   cases as `.word`s: `scratchpad/enemy/spell/mkelf.py` wraps a block in a MIPS ELF REA
   disassembles fully. Catalog rows: `scratchpad/enemy/spell/catalog/<name>.csv` (merge at the
   commit point). Local port in deep_freeze: voice_release 0x8007fd54 (sound_handle_release).
2. Original bugs (docs/notes/original_bugs.md): E1 E2 E3 E4 E5 E8 E9 E13 fixed; E7 not a bug
   (stun immunity); E10 = E8's cause; E6 and E14 kept (the user: intended). The references
   still match after the fixes (none of the fixed paths runs in them).
3. Shot kinds 0x27 / 0x2d and shot_draw_frame_quad: done (enemy_shots.cpp; shot.cpp's
   default case asks enemy_shot_update). m524 (the boxes' toss) now runs on to b530, where the
   rock kills Claude in the port but not in the old reference; a fresh per-vsync reference
   (`ref/n524_3`, BW_SHOT_KINDS=29) shows the original killing him on the same frame with the
   same 73 draws, and the port matches it exactly to b570: two Redux runs of the same setup
   differ there.
   The load queue (bload.{h,cpp}: 0x800475c0-0x80047c98; a read is over by the next frame,
   or with SS_BLOAD_DRIVE_TIME=1 timed as Redux's drive: seek 3 us a sector + 31 ms, single
   speed) and the voice player (voice_player.{h,cpp}: 0x8007f76c-
   0x8007fc54) are in, with hooks in battle_program.cpp (voice_update / bload_update, resets),
   enemy_ai.cpp (the intro voice, the wait), bsfx.cpp (sound_handle_active's voice half) and
   voice.cpp (victory / result clips). SS_VOICE_TRACE=1 logs the slots. Measured on 538
   (Cyril): the clip streams 256 vsyncs in both (b61-b317 on the console, 24-280 in the
   port); the load takes 22 vsyncs vs 16-18 (the drive head differs). What still differs:
   battle state 1 starts at b43 on the console (the party loader's reads, entries 0x852 /
   0x8c1 for Claude, then state 1) but at vblank 2 in the port (party.cpp reads at once), so
   the boss's voice starts ~41 vsyncs early. The coordinator: keep it (no-slowdown rule: the
   port may load faster and start earlier; no waits that exist only for the console's CD);
   comparisons line up per reference. E15 kept for now (the user decides).
4. Bark / Indalecio with their HP poked (BW_ENEMY_HP / SS_ENEMY_HP = DELAY:INDEX:VALUE, DELAY
   vsyncs after the state first reads 2). Bark (ref/h531_3, HP to 130000 at fight + 60):
   kind 0x36 casts at b440; RNG exact to b440, then the original slows down under the
   vortex; every unit sample exact to the battle's end (Claude's KO at the same sample).
   Indalecio (ref/h539_3, HP to 300000 at fight start) still casts Noah first and an idle
   STRONG Claude dies before a transform. With a tough Claude (ref/t539_3: HP 9999, 3000
   vsyncs; TOUGH=1 for run_port.sh) he survives Noah and Indalecio transforms (kind 0x3b):
   the transformation runs 1420 vsyncs on the console, 1424 in the port, and his second
   form acts after it in both. Exactness ends at Noah (the original slows down, so the
   damage rolls differ: Claude 5614 vs 5505 after it).

## Phase 2, part 2 (coordinator)

- Party voices: PartyUnit::play_voice [43] (pc_play_voice 0x8003ac38) in party_voice.cpp:
  bsfx_play_at(((slot + 1) << 24) + id, member), an effect of the member's voice bank (the
  field's voice queue loads them; a --battle run has none, so it skips: port guard);
  play_move_voice [39] inline in unit.h (slot 43 with move * 2). No RNG, no timing: the
  references cannot see them; the sound-test record (config +0x1a0) can, in field play.
- "Battle voice table patches" were spell data patches (g_spell_data_entry: spells 0x13 /
  0x14 with config +0x4e, spell 0x19 by the rng_range(3) draw, the last fights' versions for
  encounters 0 / 0x219-0x21c): a hunk in battle_program.cpp through spell_data_entry_set.
- Divine Comedy (the world agent's) against ref/t540_3 (Indalecio's second form, tough
  Claude): RNG and units exact through his first spell, then the original slows down; the
  spell runs 1600 vsyncs on the console, 1596 in the port, Claude dies after it in both.
  Reported to the coordinator and the world agent.

## E9 revised, E5 checked (2026-10-22 22:55, done; suites 78/0, 82/0, smoke 21/21)

- E9 (Carlaeagle feathers): the feathers keep the console's start (y -0x78) and path, so
  they move, collide and hit exactly as on the console; shot_k16_projectile draws them at
  pos + Shot::draw_offset * (draw_ticks - draw_age) / draw_ticks (a port field, cleared by
  shot_reset), and moves the boxes the draw placed back onto the path. The module sets the
  offset (the bird's height) and draw_ticks (the tick nearest the aim, at most the life).
  ref/caster35: SS_BATTLE_SHOTS positions equal to BW_SHOTS each frame (D 6), feathers 0, 3, 4
  hit Claude at ref b849 for 510 as on the console, RNG equal to the battle's exit (b2046;
  the first fix split it at b850). Scripts: scratchpad/enemy/e9/run35.sh.
- E5 (Marsilio's afterimage): visual only. scratchpad/enemy/e9/e5run.sh (encounter 530,
  Claude x200 HP attacking, so attack 1 runs and hits at vblank 576): the log is byte for
  byte the same with the original's line and with the fix.

## Archipelago phase 4, APWorld side (fifth AP agent, 2026-10-23 ~05:00-06:10; read this first)

**All five tasks done; phase 4 ticked in CHECKLIST.md** (entrance shuffle split into its own
unticked line). Nothing of mine is running. Env: `source scratchpad/enemy/ap4/env.sh`. Build
engine/build-enemy is current (rebuilt 05:10; no engine code changed by me this turn, so the
port suites were not rerun).

- **Teleporter proof:** `tools/ap_late_proof.py --state teleporter` (chapter 38 step 0, flag 730
  cleared, proof seed expel_teleporter on, root map 2; result in late_proof.json `teleporter`).
  Claude 583 maps / 265 locations, Rena 470 / 187 (difference = Cave of Trials floors only);
  both prove all 129 late-proven Expel locations. Logs scratchpad/late_proof/teleporter/.
- **Tables** (tools/ap_world_tables.py, `--doors`): KEY_NAMES, STAGES (23), STORY_BOSS_CHAPTER,
  key_groups / map_groups (group + `group_near` when only reached over scene doors) /
  route_keys / shop_slots; data.py 1110 rows (550 shop slots, 98 specials), KEY_GROUPS, STAGES,
  rows' group / tele / late_keys / tele_keys / from_chapter / chapter.
- **APWorld:** options world_mode / expel_teleporter / shuffle_shops; items keys (KEY_BASE + N,
  progression), START_KEYS Arlia + Fun City (+ North City with the teleporter off),
  EXPEL_STORY_KEYS, stage_event; locations shop rows + check; regions stage events +
  stage_rules; rules.py rewritten (open_world_access: early / teleporter / late modes;
  open_world_progression: Expel any where tele-proven, else only our Expel-story keys where
  late-proven; expel_progression off or teleporter off: only those keys; `in_expel`; Nede rows
  behind an Expel entrance (map 429) have no early mode); world.py keys replace filler, seed
  `doors` (66), options. Design and limits: apworld.md "Open world", "Shop slots" (never
  progression: price / Fol could block), "Entrance shuffle (later)".
- **Tests:** world 95 passed (7986 subtests); AP general 379 passed (116983); batch 64 seeds =
  128 .apso2, 127 option combinations, 0 checker errors, 3663 progression items
  (scratchpad/enemy/ap5/batch: run_batch.sh FIRST LAST [KEEP=1 appends], check.py OUT; the first
  run's 3 fill failures = open world + expel_progression off, fixed); port spot checks
  (scratchpad/enemy/ap5/spot/run.sh SEED NAME; picks.txt): 4 open-world seeds, North City door
  68685998 locked -> `ap_give key:29` -> map 260 at vblank 422; full-shops seeds pay shop 1
  slots 0 / 2 (own given, other sent), specials seeds leave shop 1 alone; tools/ap_e2e_so2.sh
  25/25 twice (open world + full shops; Salva Pass from Bob's Tile 2; session 2 buys at Hearn's,
  slot 2 = plando'd own Spectacles because shop slots refuse progression and every ChecksFinder
  item is progression; port check now before rm -rf of its work dir, per the world agent).
- **The world agent's character note** (Celine's Cross Cave choice, 124 / 125, Leon 234, Opera
  479 partings): join sites are always EXCLUDED and never progression; documented in apworld.md.
- **Open / next:** entrance shuffle (design note); a mid-Nede proof with flags of its time (the
  teleporter proof's chapter-60 flags limit it to Expel maps); "Map 231 Pass" has no better name
  (one-map area of chapters 24-25, no locations).

### Files changed this turn (none committed)
- New: none in git (scratch: scratchpad/enemy/ap5/, scratchpad/late_proof/teleporter/).
- Changed: tools/ap_late_proof.py, tools/ap_world_tables.py, tools/ap_e2e_so2.sh,
  apworld/so2/{data.py (regenerated), items.py, locations.py, options.py, regions.py, rules.py,
  world.py, test/test_world.py}, docs/ap/{late_proof.json (teleporter key), apworld.md,
  field.md}, docs/archipelago_plan.md, docs/CHECKLIST.md, docs/handoff/enemy.md.

## Archipelago phase 3: proof, batch, docs (fourth AP agent, 2026-10-23 ~01:30-05:00; read this first)

All six tasks done; phase 3 ticked in CHECKLIST.md. Nothing of mine is running (no server,
sweeps or builds). Env for everything below: `source scratchpad/enemy/ap4/env.sh` (discs,
scratch XDG, SDL drivers, SKIP_REQUIREMENTS_UPDATE). Build: engine/build-enemy (current).

- **1. Suites** (engine/build-enemy, current tree with my hooks): ss_tests 78/0, ss_game_tests
  107/0, smoke 73/73 (scratchpad/enemy/ap4/*.log). Before the hooks: 78 / 104 / 62, all green.
- **Hooks (flagged `[enemy agent, AP late proof]`):**
  - boot/system.cpp: the SS_BATTLE_STUB stand-in calls `ap::on_battle_exit(encounter, 0)`;
  - field/event_sweep.cpp: `SS_SWEEP_SHOW="G1cf:1,G1c0:2,F157"` prints a `state` line after
    every entry and at settle (script globals hex:bytes, flags).
- **2. The late proof: done.** `tools/ap_late_proof.py` (new) -> `docs/ap/late_proof.json`
  (new, 284 KB) -> `tools/ap_world_tables.py --late` (rows get `late`: games proven) ->
  `apworld/so2/rules.py may_hold_progression` (story bosses exempt; drops / joins never;
  expel_progression off: no Expel; else proven in the protagonist's game, both under "any").
  - State: the user's save at the Final Save Point (scratchpad/latesave/card.mcd frame 13;
    chapter 60, step 4000, flag 730, floor 8, previous map 681), minus 378 "done" flags
    (chests, gift guards, cave boss guards), minus the save's message speed G[0x1b8] (with it
    the sweep answered choices wrongly); Rena's game = flag 1 cleared + G[0xc4] swapped;
    party + Rena + Noel. Roots 671 and world map 2 (the search can't walk below Fienal 6F:
    stair rooms keyed by the previous room; Prima p. 130: the party walks back out).
  - Route replay (SS_SWEEP_PLAY + SS_SWEEP_PLAY_SWEEP) from the root; maps that read
    G1cf / G1c0 / flag 157 are nodes per value read; a run stuck in a settle reload loop
    (maps 413, 250, 292, 30, 330, 36, 421, 440, 458, 705, 751, 807) is killed after 6 reloads
    and resumed after the entry; `--resume` reuses finished logs.
  - **Result:** 610 maps (732 / 731 nodes), both games the same 278 of 453: chests 258 / 311
    (Expel 122/155, Nede 46/52, Fienal 17/26, cave 73/78), gifts 9 / 101, bosses 11 / 41 (cave
    10/13). Unproven chests: Eluria Tower 23 (never reached), old Clik 4, map 387, Hoffmann
    502 x4, Mountain Palace 367 x2, North City 593 x3, Field of Intelligence 550 x2, Fienal
    647 / 673 x9, cave 710 x2, 740, 798, 799.
  - Rerun: `--jobs 3` per game (coordinator: total 6 game processes), about an hour; then
    `--reparse --game claude --game rena` and `tools/ap_world_tables.py`.
- **Fixed: the cave's gems** (items.PUZZLE_ITEMS = 289, 317): world.create_items locks them at
  their own locations (doors on 751 / 764 need them).
- **Fixed: the seed's `items` table** now lists every item of this player's wherever it is
  placed (`seed_data`): Celine plando'd into Bob's world arrived as "not in the seed's table"
  and was given as nothing. seed_format.md says so; check_seed tests it.
- **3. Tests:** world tests 66 passed (4793 subtests; new classes TestLateProofAny / Rena,
  test_cave_gems_stay); AP general 379 passed (116824 subtests); batch 60 seeds x (Claude,
  Rena + ChecksFinder) = 120 .apso2, 101 option combinations, 258-560 locations, check.py 0
  errors, 1295 progression items all at allowed locations (scratchpad/enemy/ap4/batch:
  run_batch.sh FIRST LAST, check.py OUT; out_v1 = the run before the items fix, which
  check.py flags). Port spot checks (scratchpad/enemy/ap4/spot/run.sh SEED NAME): 3 seeds:
  joins, Fol Loss 1000 of 10000, exp x2/x6/x8 and fol x6/x7/x9, Kitty (encounter 110, type
  90) first drop withheld + checked; join site 71 (Ashton) checked and held back.
- **4. tools/ap_e2e_so2.sh: 19/19** (twice): character_items / first_drop / traps 20; plando
  Celine + Fol Loss Trap on Bob's Tiles 4 / 10; `fol 10000` at 730, `battle 110` at 1600
  (two Kitties, drop id 1362231386; 547 Shadowflowers loses with Claude alone), `battle 539`
  at 4400, 8000 frames. Checks: plando items in the pool, party has Celine, join, trap 1000,
  drop withheld + checked, status "checks 2 / received 3", tracker sees crate + drop.
- **5. Docs:** apworld.md rewritten (locations, ids, the party, logic with the late proof,
  options, testing); field.md "Location ids" (types 8 / 9 / 10, join site ids, battle tables);
  seed_format.md (items table rule; Phase 4 "Confirmed by the APWorld"); archipelago_plan.md
  phase 3 + finishability point 3; CHECKLIST phase 3 ticked.
- **6. Answered:** Celine's map 74 join is temporary (Claude's game: Varmillion scene 462 T7,
  chapter 15 step 2400, either flag 112; Rena's game: 126 step 1000; party dumps,
  scratchpad/enemy/ap4/celine/run.sh), so 74 stays unlisted. Leon 229 0x73dc is listed in
  rena / any seeds.
- **Phase 4 seed fields: confirmed**, no port change (seed_format.md "Phase 4").
- **For the world agent (port), not changed by me:**
  - a received Celine who joined early is put in reserve by the Mars scene (462 / 126) like
    the original's, and returns only via 125 (flag 112) or 124; with flag 112 clear she stays
    in reserve although received;
  - the debug console's `battle N` typed on the title screen (after a game over) segfaults
    (seen in an e2e run; debug path only);
  - `--battle-end F:0` against encounter 547 with Claude alone ends in outcome 2 after the
    result screen (the party fell before frame 60): a debug-option artifact.
  - The event sweep's settle reload loop (map 413 -> 416 ...) could be bounded in
    event_sweep.cpp (menu agent's file).

### Files changed this turn (none committed)
- New: tools/ap_late_proof.py, docs/ap/late_proof.json.
- Changed: tools/ap_world_tables.py (--late), tools/ap_e2e_so2.sh (phase 3), apworld/so2/
  {data.py (regenerated, `late`), items.py (PUZZLE_ITEMS), rules.py (rewritten), world.py
  (gems, items table), test/test_world.py}, docs/ap/{apworld.md, field.md, seed_format.md},
  docs/archipelago_plan.md, docs/CHECKLIST.md, docs/handoff/enemy.md.
- Hooks in others' files: engine/src/game/boot/system.cpp, engine/src/game/field/event_sweep.cpp.
- Scratch: scratchpad/late_proof/ (bin/, claude/, rena/ logs, proof.apso2, *.run*.log),
  scratchpad/enemy/ap4/ (env.sh, suites logs, batch/, spot/, celine/, e2e logs).

## Archipelago phase 2c / 3 (third AP agent, 2026-10-23 ~02:30; read this first)

Rolled over at ~430k tokens. Nothing of mine is running (no server, no sweeps, no builds).

**Tasks, in order** (from the coordinator):
1. A real end-to-end with an SO2 slot; then tick Phase 2 in CHECKLIST.md. **Done.**
2. Boss checks: battles.json's boss encounters as locations, in the logic. **Done (APWorld).**
3. Phase 3, APWorld side (the world agent a6cb3fe962f8c58f8 does the port side):
   character_items, drops_as_checks, traps, exp / fol multipliers, encounter_rate.
   **Written, world tests 55/55; docs and batch still to do.**
4. Logic soundness: no progression where only the optimistic late evaluator says a map is
   open, unless a headless run proves it. **Method chosen and tested; the BFS not run yet.**
5. Generation tests again (50+ seeds over option combinations, AP's general tests, the
   world's tests); report the numbers. **Not started** (world tests only: 55 passed).

### Task 1: done
- `tools/ap_e2e_so2.sh` (new, mine; `tools/ap_e2e.sh` stays the world agent's, now 16/16
  with its phase-3 sessions) passes 12/12, twice:
  - Generate.py, seed 4242, makes SO2 "Claude" (protagonist claude) plus ChecksFinder "Bob",
    with `--plando "bosses, items, connections, texts"`;
  - item plando puts Bob's Map Width in Arlia's 200-Fol crate (1347569665) and Claude's
    Resurrection Bottle on Bob's Tile 1;
  - MultiServer runs on 127.0.0.1:38292 (AP_E2E_PORT) with release / collect disabled;
  - the port connects as Claude, opens the crate ("Sent Map Width to Bob."), and gets the
    bottle in free play;
  - `battle 539` from the console with `--battle-end 60:0` records the goal;
  - afterwards a Tracker-tag login reads `checked_locations == [crate]` and
    `_read_client_status_0_2 == 30`.
  - Run it with `SS_DISC1=... tools/ap_e2e_so2.sh engine/build-enemy/shattered-stars`.
- CHECKLIST Phase 2 is ticked.
- To add, as the world agent asks: phase-3 steps in ap_e2e_so2.sh.
  - Claude.yaml plando puts "Celine" and a trap ("Fol Loss Trap") on Bob's tiles, with
    `character_items: true`;
  - a drop check with `drops_as_checks: first_drop`. The world agent suggests `battle 535`
    (Berle, type 197, odds 255), but the APWorld makes no drop location for Berle: checked,
    type 197 is not in field.json enemy_drops, because Berle is met only in scripted fights.
    Use a random-encounter type instead (e.g. Gerel, type 17, groups 1 / 5; find a sure
    drop), or `ap_check` the drop location's id from the console.

### Tasks 2 + 3: written (APWorld side)
- **`tools/ap_extract.py`** (field.json regenerated, 1.08 MB, ids of old records unchanged):
  - `LOCATION_TYPES` gains drop 8, boss 9. Their ids: `BASE + (8 << 21) + enemy type`,
    `BASE + (9 << 21) + encounter`. The world agent put both in seed_format.md.
  - New keys:
    - `bosses`: 47 records, with constant scripted sites, context, window / late, region,
      area;
    - `enemy_drops`: 89 types with a drop that random encounters meet;
    - `encounter_groups`: the field program's 0x80074fe0 table, groups 1-28 and 30;
    - `world_map_groups`: the 9 x 7 grids at 0x80074ef0 / 0x80074f70 (+1 for terrain 5,
      +2 for Nede terrains 10-12);
    - `encounter_sites`: the 306 calls to each map's set-encounters library routine, which
      takes rate -> G[0x25], stage -> G[0x12] and group -> G[0x14]. field_flags is the same
      block as the script globals.
  - The second site of a recruitment record now has its own id: `gift_site_ids(joins,
    "recruitment")`. Ernest 479 0x7c30 = 1354333187.
  - New functions: field_program_image, encounter_groups, world_map_groups, battle_sites,
    encounter_sites, bosses, enemy_drops.
  - Regenerate (about 1 min):

        python3 tools/ap_extract.py D1.cue D2.cue --no-battles -o docs/ap/field.json

- **`tools/ap_world_tables.py`** builds data.py with 560 rows: 311 chests, 101 gifts, 41
  bosses, 18 joins, 89 drops.
  - **Bosses.**
    - STORY_BOSSES (21): never missable, since the story fights them and a loss is a game
      over.
    - OPTIONAL_BOSSES (4): Flarelizard, Nightmare, XINE, Ghost; reason "optional quest".
    - losable (3): param 0 (204, 517, 530).
    - cave (13).
    - Left out: 539 / 540 (the goal), and 0 / 263 / 534 / 561 (no constant site).
    - Areas come from the tables; the cave's from the floor banners. Map 463 (Azamgil) is
      forced to Expel.
  - **Joins.** JOIN_SITES lists 18 sites, agreed with the world agent:
    - Celine 124 / 125; Bowman 208; Dias 130 / 439 / 229 0x7c9c; Precis 209;
    - Ashton 71 x3; Leon 230 / 229 0x73dc; Opera 374 + 479 x3; Ernest 479 0x7c30;
      Chisato 242.
    - Not listed (temporary scenes or restores): 74, 437, 434, 468 / 477, 126 / 462 / 524 /
      422 / 548 / 234. These leaves are proven by `scratchpad/enemy/ap3/removes.py`, which
      scans calls to the library leave routine (character, flag).
    - Noel is not shuffled.
  - **Drops.** Each drop's region is the earliest region where the type is met. The pool item
    is the type's first drop.
  - **Missable reasons.** SOFT_REASONS = window, time-limited. Everything else is "hard":
    private action, optional member, join, optional quest, losable, and protagonist-only
    under "any".
- **APWorld** (`apworld/so2`):
  - **options.py.** New options: boss_checks (default on), character_items, drops_as_checks
    (off / first_drop), traps (0-100 %), exp_multiplier, fol_multiplier (1-10),
    encounter_rate (0-200). Three option groups.
  - **items.py.**
    - Character items: CHARACTER_BASE = ITEM_BASE + 0x20000, plus the party id (1-12). They
      are Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest, Chisato. They are
      classified useful, never progression, because every member-gated location is
      excluded.
    - Traps: TRAP_BASE = ITEM_BASE + 0x30000, plus 1-5: "Ambush Trap", "Fol Loss Trap"
      (10 %), "Poison Trap", "Paralysis Trap", "Stone Trap".
    - seed_item writes the kinds character / trap.
  - **locations.py.**
    - choose(): boss / join / drop rows follow their options.
    - A hard reason means EXCLUDED, even with missable "include". This fixes an unsoundness
      in phase 2b: "include" let two mutually exclusive locations both hold progression.
    - "remove" never removes join sites.
    - seed_check now covers boss, join and drop.
  - **world.py.**
    - choose_party(): one of Bowman / Precis; Ashton or Opera + Ernest; Celine; Chisato;
      Leon (claude) / Dias (rena). At most 5 beside Claude, Rena and Noel; when over, Ernest
      or a single drops out.
    - create_items: characters replace join locations' Blueberries; traps % of the filler
      becomes traps.
    - slot_data and the seed's options gain boss_checks, character_items, characters,
      drops_as_checks, traps and the multipliers.
  - **rules.py.** Drops get an item rule: never progression.
  - **test/test_world.py** (rewritten): 14 classes, 55 tests pass (3794 subtests).

      cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test

- **Still to do for 2 + 3:**
  - docs/ap/apworld.md: new options, locations, characters, the include change, ids;
  - field.md "Location ids": types 8 / 9, per-site join ids, the new keys;
  - archipelago_plan.md phase 3 status;
  - the e2e phase-3 steps;
  - task 5.

### Task 4: logic soundness (method ready, not run)
- **The problem.** Every non-excluded chest or gift is assumed reachable forever. A player who
  skips one before its map closes (an Expel map after the disc switch, a Nede map later on)
  loses it unless that map reopens at chapter 60. `late` (ap_extract late_maps) counts
  unknown flags as possible, so it is not a proof. Story bosses are exempt (they can't be
  skipped). Cave bosses and cave chests need the proof, because the cave is reached only
  through Virtual Expel.
- **The method: an event-sweep BFS at chapter 60** (tested).
  - The script: `scratchpad/enemy/ap3/late/sweep1.sh MAP` (env GLOBALS, FLAGS, SEED, DISC,
    FRAMES). One headless `--map MAP` run with SS_EVENT_SWEEP=1, SS_BATTLE_STUB=1,
    SS_SCRIPT_GLOBALS="a0=60,a4=0" (use a4=0xbb8: step 3000 is the state at the Final Save
    Point, after Vesper and before Cyril), SS_SWEEP_FLAGS=730 and SS_EVENT_SWEEP_DISC=2.
    - Its copy of the binary is `scratchpad/enemy/ap3/late/bin/shattered-stars`; refresh it
      after builds.
    - Each run takes 6-30 s.
  - **Edges.** The sweep fires each registered entry from a snapshot and logs `entry N Tk end
    map X` when it transfers.
    - Count T and K entries; skip P entries, which are Private Action mode.
    - World maps sweep too: `--map 2` gives T entries to every town, e.g. map 2 to 317, 306,
      549, 449, 686, 687, 463, 579, 235, 260, 277, 419, 629, 597, 279...
    - Map 671 leads to 651 and 684.
  - **Roots.** Map 671 (the Final Save Point). The BFS follows real transfers to Nede, to
    Fun City's arena (303: K25, then map 18 Virtual Expel), and on to Expel and the cave.
    Optionally root 18 too, which phase 2b proved.
  - **Rena's game.** New hook (mine, flagged): SS_SWEEP_FLAGS takes "!n" to clear flag n.
    `FLAGS='730,!1'` plays Rena's game from a --map start (--map always sets flag 1, Claude).
    Built and in the bin copy.
  - **Locations.** Generate an all-locations solo seed: missable include, cave on, boss
    checks, one with protagonist claude and one with rena. Sweep with `SEED=...`. A chest or
    gift that fires logs `[ap] check ID`. Proven = checked in a sweep of a BFS-reached map,
    in that protagonist's game.
  - **Cave bosses.** The battle stub skips battle_exit_to_field, so no boss check is logged.
    Either count a cave boss proven when its map is reached and its battle starts (look for a
    trace of the encounter; SS_TRACE?), or keep cave bosses no-progression.
  - **Output.** Write a committed tool `tools/ap_late_proof.py`, the BFS driver (jobs 6, the
    event_sweep.py patterns), and `docs/ap/late_proof.json` with: state, roots, maps reached
    per game, edges, location ids proven per game.
  - **Wiring.** ap_world_tables reads late_proof.json and adds `late_proven` per row. In
    rules.py, a chest / gift / cave boss that is not proven gets the no-progression rule, the
    one drops have. expel_progression stays as an extra switch.
  - **Conservative, by design.** The sweep starts with only chapter / step / 730 set, so
    doors behind story flags may stay shut: those locations become no-progression.
- **The include fix** above (hard reasons always excluded) is part of task 4. So is "characters
  are useful only": member-gated logic would be non-monotonic, since Ashton rules out Opera.

### Phase 4 seed fields proposed by the world agent (a964190b3651ef72b): CONFIRMED (fourth AP agent)
Confirmed as written, 2026-10-23 (seed_format.md "Phase 4", "Confirmed by the APWorld"); no
port change needed. Keys only on the 66 world-map entrances (35 key groups as the default),
shop slot ids type 10 (type 5 stays the shop-site record), `item` always written for shop
slots. Phase-4 logic is not built yet (after phase 3). The proposal as it was:
1. **Doors.** tools/ap_doors.py writes docs/ap/doors.json: every constant map transfer, with
   id = map * 65536 + word index of the transfer, its target, requires / via,
   world_entrance (66 chunk-script transfers) and a suggested key_group. Doors are not
   locations.
2. **Keys** (options.world_mode = "open_world").
   - Item kind `{"kind": "key", "key": N, "name": "Salva Pass"}`, AP id ITEM_BASE + 0x40000 +
     N.
   - Seed top-level `"doors": [{"id": D, "key": N}]`: a door is blocked at its trigger
     without the key.
   - Default plan: one key per world-map area, about 35 groups of the 66 entrances. The
     APWorld picks the final list and names.
3. **The Expel teleporter** (options.expel_teleporter, default on in open_world). Two built-in
   edits make Fun City's arena (303) and Arlia (18) connect both ways from chapter 36. The
   logic: Expel is reachable from Fun City.
4. **Shop shuffle** (options.shuffle_shops off / specials / full).
   - check `{"type": "shop", "shop": S, "index": I, "item": orig}`, type 10, id 0x50320000 +
     (10 << 21) + (S << 8) + I.
   - The first copy bought is the check. The APWorld defines "specials".
5. **Entrance shuffle groundwork.** A seed door may carry `"to": {map, x, y, z, dir}`;
   apworld/so2/doors.py (theirs) validates it. No shuffle yet.

### Files changed this turn (none committed)
- New: tools/ap_e2e_so2.sh.
- Changed: tools/ap_extract.py, tools/ap_world_tables.py, docs/ap/field.json,
  apworld/so2/{data.py (generated), items.py, locations.py, options.py, rules.py, world.py,
  test/test_world.py}.
- Hook in another agent's file: engine/src/game/field/event_sweep.cpp (SS_SWEEP_FLAGS "!n";
  the menu agent's file, flagged `[enemy agent, AP late proof]`).
- Docs: docs/CHECKLIST.md (Phase 2 ticked), docs/handoff/enemy.md.
- Scratch:
  - `scratchpad/enemy/ap3/`: build.log, field_new.json, data_before.py, bossctx.py /
    removes.py / dis.py (analysis), e2e/ (first manual generation);
  - `scratchpad/enemy/ap3/late/`: sweep1.sh, bin/, logs/ (18, 2, 671), xdg/;
  - `scratchpad/ap_e2e_so2/`.
- The build in engine/build-enemy is current (with the hook). Suites not re-run since: run
  ss_tests / ss_game_tests / smoke before the next report.

## Archipelago phase 2b (new agent, 2026-10-23; read this first, then the section below)

**Tasks, in order** (from the coordinator):
1. Finish tools/ap_extract.py and regenerate docs/ap/field.json:
   - one chest per opened flag (flag 2459 differs on maps 507 / 508);
   - one id per gift item site;
   - fallback area names: Fienal 647-685, Mountain Palace 340-380, Lasguss 381-385, and the
     El beach / Eluria for 229 / 230 / 548.
2. Check headless: Virtual Expel through map 625 (chests share flags?), and Leon's Rena path.
3. Write the APWorld in apworld/so2 (AP 0.6.8). It uses scratchpad/ap (uv, Python 3.13,
   ap-venv; always `SKIP_REQUIREMENTS_UPDATE=1`). Delete scratchpad/ap_venv when done.
   Tests: seeds with ChecksFinder, AP's world tests, 50 seeds, and one .apso2 in the port.

**Done so far.**
- **Leon (task 2b):** done.
  - Claude-only in the logic, as the user and Prima say. His join is map 230 0x6c58
    (flag[1]).
  - Script fact, verified headless: in Rena's game, a party of Claude and Rena alone at the
    shipwreck gets Leon at map 229 0x73dc. The run: map 548 → 229 → 548 → 229; the slots end
    as Leon, Rena, Claude (reserve), chapter 31.
  - Run: `scratchpad/enemy/ap2/run.sh` with `SS_SCRIPT_GLOBALS="a0=29,a4=30" SS_EVENT_SWEEP=1
    SS_SWEEP_PARTY=1 SS_SWEEP_PLAY=9999:T0 SS_DEBUG_INPUT="60:flag 1 0" --map 548 --debug`.
  - The coordinator agreed. field.md is updated (Leon and Dias, table and Findings).
- **Patch helper agreed with the world agent (aa4d2f9448eaa9d2f).** It writes
  `apworld/so2/patches.py` and records the API in seed_format.md:
  - `placeholder_for(n, role)`;
  - `placeholder_patches(sites, placeholder)`;
  - `placement_patches(locations, start=0)`.

  Each site is `{map, word, expected, form, role}`:
  - `word` is already resolved: value_at/4 for imm24 / inline give, value_at/4+1 for word
    and for inline add_money.
  - The Fol placeholder range is now 0xF00000-0xFFFFFF (it fits imm24).
  - Key-item flag gifts (value >= 10000) stay vanilla, with no placeholder.
  - A vanilla gift still needs a placeholder, because the port checks gifts only by
    placeholder.

- **Task 1: done.** tools/ap_extract.py and docs/ap/field.json are regenerated (43 s; byte-identical
  with PYTHONHASHSEED=7).
  - Chests are one record per flag (313), with `sites`. 25 ids are retired.
  - Gifts have one id per site (133 sites in 109 events; 24 new ids).
  - Every chest and item / Fol gift site carries `word` / `expected` / `role`; all 735 pass
    patches.py.
  - PA gifts are tagged (`private_action`, `pa_partners`).
  - `GUIDE_AREAS` adds the area names.
  - 784 ids in all; none changed meaning; the test ids are unchanged.
  - Flag 2459: map 507 (Peep Half) before map 509's switch sets flag 149, map 508 (Walloon
    Sword) after. The pool takes Peep Half.
  - field.md is updated: the JSON fields, the counts, Location ids, Findings, open items 1
    and 4 closed.
- **Task 2a, Virtual Expel: done** (field.md Findings).
  - The route is Fun City's arena (map 303, K25) once flag 730 is set (map 671's save
    point) → Arlia 18 → the Expel world map. Not map 625.
  - Chests at chapter 60 open and check on maps 18 / 355 / 495; chests already opened stay
    shut.
  - `late_maps` (708 maps) and `late` per record come from a three-valued condition
    evaluator in ap_extract.py (`tri_eval`, `late_atom`, `late_maps`).
  - Late: 308 / 313 chests, 105 / 109 gift events. Not late: old Clik 117 / 119 and map
    387.
  - The story needs no inventory items besides key flags (field.md).
- Scratch: scratchpad/enemy/ap2 (run.sh, xdg, logs, field_before.json, vexpel_seed*.json).
- **Task 3, the APWorld: done** (docs/ap/apworld.md has everything).
  - Package and tables:
    - `apworld/so2/`: world.py, items.py, locations.py, regions.py, rules.py, options.py,
      data.py (generated), archipelago.json, docs/, test/;
    - patches.py is the world agent's, imported.
    - `tools/ap_world_tables.py` writes data.py from field.json + battles.json.
  - Story mode: 412 locations (311 chests, 101 gift sites); 2 key-item chests and 33
    key-flag sites stay vanilla. The pool is the vanilla contents. Events: Energy Nede, the
    Final Save Point, Victory.
  - Options: goal, protagonist, missable_locations, expel_progression, cave_of_trials,
    death_link.
  - Tests:
    - so2's own: 30 passed;
    - AP's test/general: 379 passed;
    - 50 seeds (2 SO2 + ChecksFinder): all fill (100 .apso2 files, 32 option combinations);
    - in the port: chests on maps 18 / 67 / 402 and gifts on 402 / 233 pay the spoiler's
      items; Alice's item is "sent"; start_inventory is given.
  - The world is linked at scratchpad/ap/Archipelago/worlds/so2 (a symlink).
  - scratchpad/ap_venv is deleted. pytest and pytest-xdist were added to scratchpad/ap/ap-venv.
- **Agreed with world agent a6cb3fe962f8c58f8 (phase 3, port side):**
  - its seed names for character / trap items, join / drop checks and multipliers;
  - drop ids use type 8;
  - I owe per-site join ids in ap_extract, and a consistent party in the pool.
- **Open:**
  - phase 3 in the world (characters, drops, traps);
  - boss checks (47);
  - the port enforcing the protagonist;
  - map 463 / hub 625 (field.md open item 3);
  - the 129 computed reward calls.

## Archipelago phase 2 (context rollover 2026-10-23 ~01:00)

**The coordinator's order.**
1. **Close field.md's logic items.** Recruit exclusions, shops without a site, map names
   (map 671 too), PA flags that feed the story. Mostly done; see below.
2. **The SO2 APWorld.** A Python package in `apworld/so2/` following Archipelago's World API:
   - `__init__.py`: the World class, the item and location name tables, create_items,
     create_regions, set_rules, fill_slot_data, and generate_output writing `.apso2` per
     docs/ap/seed_format.md;
   - `items.py`, `locations.py`, `regions.py`, `rules.py`, `options.py`;
   - tables generated from docs/ap/field.json + battles.json by a script.

   Story mode first: chests and gifts shuffled, story checkpoints always reachable, missables
   excluded or handled by an option, goal = the final boss.
   - **Archipelago install:** in the scratch venv `scratchpad/ap_venv`. Tell the setup agent
     the path; it needs one for its client tests.
   - **Seeds:** generate them locally with our world plus a built-in one, and run AP's own
     generation tests over a batch of seeds.
   - **Port check:** load one `.apso2` headless with `--ap-seed` and check the patched chests
     pay what the spoiler says.
3. **Rules.**
   - Scratch settings only; no network beyond package downloads; never contact
     archipelago.gg's servers.
   - Log here every 20 minutes. No commits.
   - Headless only, XDG_CONFIG_HOME pointed at scratch, --no-memcard. At most 3 Redux
     instances. Builds `-j8` in engine/build-enemy.

**Item 1, status: done except the open points below.** The findings are written up in
docs/ap/field.md, "Findings since Phase 1", with Prima page references.
- **Exclusions.**
  - Precis rules out Bowman: flag 763 at map 195, step 4000 vs 5000.
  - Ashton rules out Opera and Ernest: flag 141 cleared at maps 43 / 44; map 174 needs
    has(Ashton) == 0.
  - Ernest only with Opera (map 479, choice 0). Dias only in Rena's game (map 130 choice;
    map 229).
  - Leon: Claude's game, plus a possible Rena path at map 229 / flag 765 (unverified).
  - Chisato: the G[0x214] chain, then the choice at map 242. Precis: the G[0x1e4] PA chain,
    then map 209.
  - No join tests the party size.
- **Shops.** 90-94 are the Familiar order shops (menu_specialty.cpp). 14, 45, 51, 66, 79 and
  80 are never opened.
- **Names.** `map_names` / `map_areas` are in field.json (regenerated 2026-10-23 with them).
  Map 671 is Fienal 9F, the Final Save Point (Prima p. 127, confirmed by the user). It has no
  banner, so the tool gives it no area yet. To add (open item 4 in field.md) as fallbacks in
  `areas()` in tools/ap_extract.py:
  - Fienal = maps 647-685, the final boss on 685, three staircase rooms above 671;
  - the Mountain Palace = 340-380;
  - Lasguss Mountain = 381-385.
- **PA flags.** 17 of 99 PA-set flags gate joins, gifts or shops elsewhere (field.md lists
  them).
- **Virtual Expel** (Prima "Filia, Virtual Expel, and the Cave of Trials"): Expel's maps can
  be revisited late (chapter 50, through map 625). Chests share their flags, so Expel's
  chests are probably not lost at the disc switch. Verify in play before the logic relies on
  it.
- **Not done yet in tools/ap_extract.py** (field.md open items 1 and 4):
  - one chest location per opened flag: 313 flags; 25 flags sit on two map versions, and
    flag 2459 pays differently on maps 507 / 508;
  - one gift id per item site (`sites[]`);
  - the Prima fallback area names.

  Then regenerate docs/ap/field.json:

      python3 tools/ap_extract.py DISC1.cue DISC2.cue --no-battles -o docs/ap/field.json

  It takes about 2.5 min and is deterministic.

**Item 2, status: just started.**
- **Archipelago 0.6.8** (superseded, 2026-10-23): scratchpad/ap_venv is deleted. Use
  scratchpad/ap (tools/ap_test_server.sh's install: uv, Python 3.13, `ap-venv`) with
  `SKIP_REQUIREMENTS_UPDATE=1`. The old install hung because ModuleUpdate waited at a prompt.
- **The plan for the world:**
  - Put the package at `apworld/so2/` in the repo. Symlink or copy it into
    `Archipelago/worlds/so2` (or `custom_worlds/`) to run.
  - Game name: "Star Ocean: The Second Story". Item id = 0x50320000 + game item id; Fol
    bundles get their own ids. Location ids come from field.json (`location_id`).
  - **Items:** the vanilla contents of the included locations make the pool. All are
    filler / useful: story key items are flags (66-91) and are not shuffled.
  - **Regions:** Menu → Expel → (disc switch, chapter 35) → Energy Nede → Fienal (maps
    647-685; 625 / 687 nearby). The goal is the final boss event, encounter 540
    (seed_format's `goal` key; default [540]). The Cave of Trials is postgame and off by
    default.
  - **Options:**
    - `missable_locations` (excluded by default: those with `missable` or partner /
      `has()` conditions);
    - `expel_locations`: by default no progression items in Expel locations
      (`add_item_rule(... not item.advancement)`) until Virtual Expel is verified;
    - `include_pa_rewards`, `prize_tables` (off), `goal`.
  - **Patches** follow seed_format.md's new **placeholders**:
    - items 0x400-0x270f go in an item push; Fol placeholders are 0x7f000000+;
    - `script MAP WORD EXPECTED VALUE` with word = value_at / 4 for an imm24 push (VALUE =
      0x12000000 | placeholder);
    - for a word push the value is the next word (word = value_at / 4 + 1);
    - inline give_item has the item in bits 0-15 of its word.

    Check against `tools/so2_mod.py chest` and the port's `ap.cpp` before relying on these.
  - **The test:** generate a seed with our world plus a built-in one (for example
    "Clique"), run AP's tests (`python -m pytest test/general` with our world installed),
    then run the port headless with `--ap-seed FILE` (SS_DISC1, XDG scratch, --no-memcard)
    and open a patched chest: smoke_field_events.sh's `ap_seed` / `ap_placeholder` cases
    show how.

**The Prima guide.** `docs/Star Ocean The Second Story_text.pdf` (git-ignored: never commit
it or copy its text). Its OCR text is at `scratchpad/guide/guide.txt`; read pages with the
Read tool's `pages`. Write facts only, in our own words, with "Prima p. N" (the printed page;
PDF page = printed + 1). The disc wins over the guide.

**My tools.**
- `tools/ap_extract.py`.
- `scratchpad/enemy/ap/ax.py`: a cached loader of every map; `python3 -c "import ax;
  maps, roles, lib = ax.load()"`. The cache is ax.pkl, about 200 MB: delete it when done.
- `scratchpad/enemy/ap/trace.py`: `python3 trace.py 113 g0x1bc` lists where a flag or a
  global is stored, with conditions.
- `scratchpad/enemy/e9/run35.sh` and `e5run.sh`: the E9 / E5 checks.

**Verification commands.**
- Suites:

      cd engine/build-enemy && ./ss_tests && SS_DISC1=... ./ss_game_tests
      tools/smoke_field_events.sh engine/build-enemy/shattered-stars

  Last results: 78/0, 82/0, smoke 21/21, with XDG_CONFIG_HOME=scratchpad/enemy/xdg and SS_DISC1
  set.

**Nothing of mine is running.** The Redux runs in scratchpad/battle belong to the battle
agent.

## Archipelago field tables (done 2026-10-23 00:00; uncommitted)

Deliverables (on disk):
- **tools/ap_extract.py.** It writes deterministic JSON (checked with two hash seeds), about
  70 s per disc.
  - `--no-battles` leaves out ap_battles.py's tables; `--full` adds every transfer and
    story-global store; `--summary` prints a summary.
  - `docs/ap/field.json` (765 KB, both discs, no battles) is the committed table.
- **docs/ap/field.md.** The method, the JSON fields, what the discs hold, the recruitment
  table, Private Actions, the story order and the point of no return, the regions, the
  location ids, the port hooks and the open items.
- **Other docs.** docs/archipelago_plan.md has a "Field locations and hooks" section.
  docs/catalog/formats.md has "Story globals and notable flags". The globals.csv game_state
  row covers chapter / step / map / treasure count.

Numbers (NA discs; all 820 scripts identical on both):
- 338 chests (249 Expel / 89 Nede), 109 gift events, 171 prize-table entries, 1 Fol gift
  (3 fees), 86 join sites, 62 shop sites (55 shop ids, 64 stock lists), 741 PA events
  (18 give items);
- 785 location ids;
- chapters 4-60 stored once each; the disc switch at map 625 0x57a0 (chapter 35, then map
  618) is the point of no return.

Findings:
- Flag 1 set = Claude's game.
- Flags 2 and 4 = Private Action mode.
- Flags 758-769 = "member n was in the party" (restore after splits).
- Flags 66-91 = key items (the give routine with item 10000 + n).
- The world script's jump list = warps (not play).
- Chisato's and Precis's joins are inside PAs.

Open items (field.md): recruitment exclusions (trace flags 113 / 141 / 243 / 758-769 and
G[0x1bc] / 0x1be / 0x214 / 0x2e0), the 129 computed reward calls, shops 14 / 45 / 51 / 66 /
79 / 80 / 90-94, map 463 and the hub map 625, town names, PA flags that feed the story.

Scratch: scratchpad/enemy/ap (ax.py harness, field_full.json); scratchpad/enemy/e9 (E9 / E5
runs).

## State and next step (2026-10-22 evening, phase 2 done, part 2 done)

- Nothing running (no helpers, no emulators, no sweeps).
- Phase 2 done on disk (reported as verified; the coordinator folds it into the hourly
  commit): the seven spell modules, enemy_shots.cpp, bload.{h,cpp}, voice_player.{h,cpp},
  the original-bug fixes, the hooks listed above, catalog rows, notes.
- Last checks: ss_tests 78/0, ss_game_tests 74/0, smoke 21/21; all references re-run (48
  exact; the boss fights as described); sweep of every disc-1 encounter with the voice
  player: 564/564 clean.
- Open, not mine: the party's voice calls and the battle voice table patches (battle
  agent; they can call voice_play_entry now); the remaining spell modules (world agent).
- Comparisons: `scratchpad/enemy/verify_all.sh [PATTERN]`; boss fights need `CMP_FROM=60
  CMP_R=1 CMP_DMAX=80` (537: `CMP_R=2`); HP pokes: BW_ENEMY_HP / SS_ENEMY_HP; tough Claude:
  redux*/run_tough.sh + `TOUGH=1 ./run_port.sh`.
