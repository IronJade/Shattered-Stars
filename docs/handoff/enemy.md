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
at HEAD e37c3a5 + the world agent's spells (1500 vsyncs each, or to the battle's end):

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
  landed at once); in HEAD since e37c3a5.
- Not exercised by these references (Claude stands idle and does no damage): Bark's
  kind-0x36 SpellCast (every tenth of its HP lost) and Indalecio's transformation (kind 0x3b).

## Committed

- 22ac0e8 (commit point 1): families, enemy_support, enemy_modules registry, Guardian.
- a56ba4b (commit point 2): all 43 modules, catalog rows, notes.
- e37c3a5 (the user, unverified): the world agent's spell wiring in enemy_support and seven
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
- **Next: task 3, the APWorld.**

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
- **Archipelago 0.6.8.** A git tag shallow-cloned to `scratchpad/ap_venv/Archipelago`. The
  venv `scratchpad/ap_venv/venv` uses Python 3.13 (`~/.local/bin/python3.13`; the system
  3.14 is too new) with requirements.txt installed.
- **Always `source scratchpad/ap_venv/env.sh` first.** It points HOME, XDG_* and TMPDIR at
  scratchpad/ap_venv, puts the venv on PATH and cds into the Archipelago tree.
- **`python Generate.py --help` hung** until the timeout killed it. It is most likely
  ModuleUpdate waiting at an install prompt for other worlds' requirements. Next try:
  `python ModuleUpdate.py --yes` (or run `Generate.py` with `--skip_prog_balancing`
  after the update) with stdin closed.
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
