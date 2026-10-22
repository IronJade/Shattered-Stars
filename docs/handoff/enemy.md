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
| cbdc2116f3 | 0x1c Star Flare | Breakwing (m528) | spell_mod_star_flare.cpp | helper C | running |
| a6a642e880 | 0x24 Daemon's Gate / 0x41 The Scream | Weirddevil (m430) | spell_mod_daemons_gate.cpp | helper C | running |
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
2. Original bugs (docs/notes/original_bugs.md): done on disk: E1 E2 E3 E4 E8 fixed, E7 not a
   bug (stun immunity), E10 = E8's cause, E13 (Noah's swapped lists) fixed, E14 new (decide).
   Opinions on E5 E6 E9 E14 go to the coordinator with commit point 4. Not yet re-run
   against the references (E1 changes Arcmene's choices: m50 will differ by design).
3. Shot kinds 0x27 / 0x2d, shot_draw_frame_quad, bload_request, then the voice player
   (voice_play_entry, the boss intro voice's RNG and hold).
4. If time allows: Bark / Indalecio references with their HP poked low (kinds 0x36 / 0x3b).

## State and next step (2026-10-22, after commit point 3)

- Nothing running (no helpers, no emulators); all work on disk; nothing of mine uncommitted
  except docs/notes/battle_enemies.md (the extended verification table).
- The enemy code is done as far as Redux can check it now. Next: when the world agent ports
  spell modules 12 / 16 / 22 / 28 / 51 / 55 / 56 / 57, or the battle agent Shot kind 0x27 /
  the voices, rerun `scratchpad/enemy/verify_all.sh m` (boss fights: `CMP_FROM=60 CMP_R=1`,
  537 `CMP_R=2`, `CMP_DMAX=80`) and extend the table. Bark's kind 0x36 and Indalecio's
  transformation still need a reference where the monster loses HP (poke its HP low in both
  the Redux run and the port).
