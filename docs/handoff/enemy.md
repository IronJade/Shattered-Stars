# Enemy classes: continuation log

Worker: enemy agent (the battle program's monster classes: the 16 resident families, the
#2129 image's default enemy-code modules and the encounter packs' modules). Build dir:
`engine/build-enemy`. Scratch (on disk, git-ignored): `scratchpad/enemy/`. The worker does not
commit; the coordinator does.

## Layout (new files, all mine)

| File | Contents |
|---|---|
| `battle/enemy_classes.h` | `enemy_class_create(type)` (the factory's class choice), `enemy_modules_reset/load` (the pack's code list), `enemy_try_approach` |
| `battle/enemy_families.cpp` | the 16 resident families (0x8003e248-0x80041600) |
| `battle/enemy_modules.{h,cpp}` | module registry: the 40 pack blocks (FNV-1a 64 of the unpacked bytes) + 3 image defaults, slot state, the factory switch for module types, `enemy_module_u8/s16/s32` (a module's own tables by link address) |
| `battle/enemy_support.{h,cpp}` | resident helpers only the monsters need so far (bobj_front_x, unit_play_anim_ex, unit_throw_weapons, gte_set_view_matrix, draw_tri_g3, ShotDesc + shot_desc_init, stand-ins: shot_spawn_projectile, sound_handle_pitch_down), `SS_ENEMY_TRACE` |

## Edits to other agents' files (hooks only)

- `encounter.cpp`: `enemy_create_by_type` builds `enemy_class_create(type)` (the family sfx
  switch and the "module not ported" log moved into the classes / enemy_modules.cpp);
  `load_formation` (3) calls `enemy_modules_reset()` then `enemy_modules_load(kind, block)`
  for each code-list entry; include + header comment.
- `enemy_ai.cpp`: `enemy_think` starts with `enemy_debug_trace(e)` (SS_ENEMY_TRACE); include.
- `engine/CMakeLists.txt`: enemy_support.cpp, enemy_families.cpp, enemy_modules.cpp.

## Done

- Step 1 (families): ported, builds, tests green (ss_tests 76, ss_game_tests 71, smoke).
  Redux comparisons (forced battle on stage 3, Claude with the STRONG poke standing idle so
  the monsters attack him; RNG draws and every enemy's state / position / HP / scratch words
  every 10 vsyncs; scratchpad/enemy/cmp.py):
  - 315 Bugbear (kobold family): 150/150 RNG samples, 150/150 enemy samples exact over 1500
    vsyncs (Claude killed at ~b1090 by the flurries; KO state one sample late).
  - 47 two Sandglass: RNG equal (31 samples one frame apart, none off), 300/300 enemy samples.
  - 412 two Dreamshade (time stop, heal): RNG and 212/212 enemy samples equal up to the
    battle's end (Claude KO, b1060).
  - 280 Darkcrusader (knight): equal until b590, when the sword waves hit Claude in the
    original; Shot kind 2 is not ported (the port spawns it and shot.cpp removes it).
  - 10 Kobold / 41 Beastmaster with Cross mashing: equal until the beastmaster's first spell
    (state 14 chant; spells are the battle agent's) and Claude's walk drifting by 1 unit (see
    findings).
- Step 2: Guardian (image slot A) ported (enemy_mod_guardian.cpp). Nicolus / Jibril (image B
  / C) and the 40 pack blocks are being ported by six helper agents (see "In progress").

## In progress (uncommitted)

- All files in the layout table, plus `enemy_mod_guardian.cpp` (in CMake).
- Helper agents write `battle/enemy_mod_<name>.cpp` (not yet in CMake / the registry) and
  `scratchpad/enemy/catalog/<name>.csv`; guide `scratchpad/enemy/GUIDE.md`. Groups: (1)
  carlaeagle gelatin arcmene hounddog salamander kitty ricki ogre snowman bunny; (2) ericodus
  xine gargoyle stingray gerel controller niquia azamgil guardbox lover; (3) visseyer slime
  miel shin magichand succubus ghost berle; (4) cyril dias zand bark synard celesta; (5)
  marsilio shigeo ruprecht vesper decus; (6) nicolus jibril indalecio.
- Pending catalog merge: `scratchpad/enemy/catalog_mine/step1.csv` (port column of the
  families and the helpers) via `scratchpad/enemy/catalog_update.py`.

## Next steps

1. As each helper reports: review its files, add them to CMake (after enemy_mod_guardian.cpp)
   and the registry pointer in enemy_modules.cpp (`kModules[]`), build, test.
2. Move any locally ported resident helpers into enemy_support.
3. Redux-verify a sample of modules (encounters in scratchpad/enemy/type_map.txt).
4. Merge the catalog rows; notes in docs/notes/battle_enemies.md.

## Findings

- 40 distinct enemy code blocks on the two discs (disc 2's packs are byte-identical to disc
  1's). Each serves exactly one factory entry (a constructor at a slot base or a vtable inside
  the slot). The #2129 image's slot A module is Guardian / Geo-guardian / Owlbear (0xbe-0xc0,
  encounters 457 458 501 512 525 527 558), B is Nicolus (0xc9, enc 536), C is Jibril (0xca,
  enc 536). The catalog's enemy_modb/modc "factory types" lists name the pack modules' types.
- Vtable-type modules (e.g. Snowman vt 0x80088418) have no constructor at the slot base: offset
  0 is an ordinary method (Snowman: slot 26).
- Thunks shot_place_at_unit_thunk 0x800553f0 / sprite_place_near_thunk 0x80030050 pass dz = 0.
- enemy_thief_attack1 animates twice a frame (enemy_attack_step, then slot 22 again).
- enemy_sandglass_attack3 freezes the party every frame past frame 9 (no once-flag).
- Forcing an encounter in Redux: the field reads the encounter from game_state +0x264 (s16,
  `*0x80075710 + 0x264 = ENC`; field_prepare_encounter takes -(+0x264), so a positive value
  is the encounter itself). The battle agent's scripts poke +0x99, which does nothing: their
  "forced" battles are random encounters of map 7's group 1 (Lizardaxes).
- The action kind +0x12a is uninitialised heap on the console until first set (cmp.py
  ignores it).
- Claude's approach walk in the port drifts from the original by about one unit per 10
  frames (enc 10, from b340); PartyUnit code, reported to the coordinator.

## Tools (scratchpad/enemy)

- `scan_packs.py` (code lists, types, names; blocks to `modules/`), `type_map.py`, `entries.py`
  (function starts per block), `ghd_modules.sh` + `ghidra_scripts/ModuleDecompile.java`
  (patches a block into a copy of the analysed battle runtime program, read-only, and writes
  `decomp/<block>/*.c` + `asm.txt`), `show.py MODULE [ADDR..]` (decompiles with catalog names).
- `redux/run.sh ENC STAGE OUT` (Redux reference: forced battle, STRONG poke, Cross mash,
  RNG counts and `[bw] enemy` lines every 10 vsyncs), `SS_ENEMY_TRACE=1` on the port side.
