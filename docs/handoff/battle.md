# Battle: continuation log

Worker: battle agent (phase 2: HUD, result screen, killer moves, spells and items).
Build dirs: `engine/build-battle` (normal) and `engine/build-battle-asan` (ASan; run with
`LD_PRELOAD=scratchpad/battle/asan/usr/lib64/libasan.so.8`). Scratch (on disk):
`scratchpad/battle/`. The worker does not commit; the coordinator does.

## Done and verified

- **Step 1, HUD / windows / #1980 text / message banner**: `battle/window.{h,cpp}`,
  `battle/hud.{h,cpp}`. Committed (1a838d8). Compared with Redux: both HUD layouts,
  "Phase Gun" in the #1980 font; banner state machine in `battle_message_banner`.
- **Step 2, #1981 result screen**: `battle/result.{h,cpp}`, `battle/voice.{h,cpp}`
  (victory and result voice picks with their RNG draws). Committed (aad2b67). Redux
  victory reference `scratchpad/battle/ref/victory_2_3` (forced fight, Claude poked like
  `SS_BATTLE_STRONG`): RNG draw counts equal at every 10 vsyncs (335), result screen and
  exit on the same vblanks (946 / 1236), handoffs 3 and 4 byte-identical at
  battle_exit_to_field, 6 identical except the item-table pointer at +0x820, 2 identical
  except the field's swap counter (+0x14) and the voice clips' sound-test record.
- **Step 3a, the Shot pool core** (`battle/shot.{h,cpp}`): pool of 160, spawn / reset /
  kill / fade / draw-and-shadow / hit_melee / place_at_unit, callback kinds 0x21 / 0x33,
  kind 1 (stun stars; `unit_apply_stun` moved here from combat.cpp). Other kinds log once
  and are removed. Camera target / focus widened to `BObj*` (a Shot can be followed).
  Committed in b906414.

## Ownership split (from the coordinator, 2026-10-22)

The battle ENEMY classes now belong to the enemy agent (resident families 0x8003e000-
0x80041fff, slot A/B/C/D modules 0x80088000-0x8008cfff, encounter-pack enemy code, the
0x80075ad8 factory hook) in their own files. This worker keeps party AI, killer moves,
spells (including `enemy_try_cast` / `enemy_state_chant` / `unit_begin_cast`, which enemy
code calls), Shot / Particle and voices. This worker is NOT restructuring `EnemyUnit` or
the enemy factory. Its `unit.h` edits are confined to `Unit::effects` / `release_effects`
and `PartyUnit` (fields at +0x290..+0x35x, virtual slots 36 / 39 / 45).

### Coordination with the enemy agent (coordinator, 2026-10-22)

- Its files: `battle/enemy_classes.h`, `enemy_families.cpp`, `enemy_modules.{h,cpp}`,
  `enemy_support.{h,cpp}`. `enemy_support` ports draw_beam 0x8007b3c0,
  draw_textured_patch 0x8007d2d4, draw_quad_gt4 0x8007d808, gte_set_view_matrix 0x8007c020,
  draw_tri_g3 0x8007c110, vec_random_in_disc_xz, bobj_poly_tag, bobj_set_pos,
  collider_set_attack_box: include `enemy_support.h` instead of porting them again. Before
  porting any shared #1984.0 / resident helper, grep `engine/src/game/battle` for its address.
- Mine, owed to it:
  - `sound_handle_fade_out / duck / pitch_up / pitch_down` (0x8007fd98-0x8007feb0) in
    bsfx, with clip-handle tracking. Then delete the logging stand-ins in
    `enemy_support.cpp` and tell the coordinator.
  - Shot kinds 0x16 (projectile; high priority: plants, oozes, archers, wizards, shouts
    and about 19 encounter modules wait on it), 2 (sword wave), 0x2c (thrown weapons),
    0x32 (beam / effect_spawn_beam_at), and spells. With kind 0x16, move `shot_desc_init`
    / `shot_spawn_projectile` (0x8005a0cc / 0x8005a158) from `enemy_support.{h,cpp}` into
    `shot.{h,cpp}`, delete them there, and flag the edit to the coordinator.
- `fx_draw.*` and `bsfx.*` stay mine; the enemy agent won't edit them.

## Step 3 so far: done and verified, NOT committed yet (reported to main)

Builds in build-battle and build-battle-asan; ss_tests 76/76, ss_game_tests 71/71 (also
under ASan); an ASan run of the Sword Bomber battle is clean.

- New files: `battle/killer.{h,cpp}` (Afterimage, Ghost, pc_* helpers, command_killer_move),
  `battle/particle.{h,cpp}` (pool of 200, kinds 1-3 drawn, 4/5 spawn only),
  `battle/pc_modules.{h,cpp}` (factory, module data access), `battle/pc_claude.cpp` (Claude:
  all 11 moves, Shot callbacks, vtable slots 23/25/35/36/37/45), `battle/fx_draw.{h,cpp}`
  (draw_vortex, project_to_screen, draw_flare_ring / _polygon, lens_flare_draw / _small,
  colour_modulate).
- Edited: `shot.{h,cpp}` (kind 0x11, sparks spawners, spark_owner), `party.cpp` (phases 1-4,
  module factory), `party_states.cpp` (killer begin_action), `input.cpp` (killer commands),
  `unit.h`, `unit_move.{h,cpp}` (vec_velocity_toward_xz, vec_random_around,
  vec_to_angles), `render.{h,cpp}` (colour_lerp moved here from hud.cpp), `hud.cpp` (raw
  window pointers), `bobj.cpp` (NO CASH / NO CASH TAG message as the console picks it),
  `sprite.h`, `bobj.h`, `combat.cpp`, `battle_program.cpp` (particle pool; stage lens flare
  call), `battle_debug.{h,cpp}` (`SS_BATTLE_POKE=TAG:OFF=VAL/SIZE,...`), `CMakeLists.txt`.
- Verified against Redux:
  - `ref/phasegun_2_3` (L1 Phase Gun every 45): RNG equal to b1600; frames equal but dither.
  - `ref/phasegun_exit` (L1 then Cross): RNG equal to the exit (b1236); handoffs equal but the
    12 known bytes; handoff 3 identical.
  - `ref/pg_cash`: texcache_alloc fails 8x/frame at b582-586, where the port logs NO CASH.
  - `ref/moves_9_5` (Sword Bomber on L1 via pokes): RNG equal to b1600; vortex, flare and
    kind-0x11 fire rings look the same.
- Catalog: `analysis/battle_catalog/port_updates.csv` regenerated (466 rows) with
  `scratchpad/battle/gen_catalog.py`; notes in `docs/notes/battle_port.md` (synced to
  `analysis/battle_catalog/port_notes.md`).

## Next steps

1. Shot kind 0x16 (projectile) with ribbon trails: asm already read and written down in
   `scratchpad/battle/notes/k16.md` (shot_spawn_projectile, shot_k16_projectile,
   ribbon_trail_create / update). Still to read: ribbon_trail_destroy 0x80075a10 (clears
   the owner back pointer, task_destroy), shot_hit_magic 0x800547d4, shot_apply_damage
   0x800548c4, 0x80063718 (effect spawner of impact kind 2), 0x800443f8 (check the port
   name), tables 0x8006ffb8.. / 0x8006cf24 / 0x8006cee4 (battle_data). Then move
   `shot_desc_init` / `shot_spawn_projectile` from `enemy_support.{h,cpp}` into
   `shot.{h,cpp}` and tell the coordinator.
2. Test Claude's move 5 (Energy Sword, byte 6: kind-0x11 rings variants 0/1) the same way.
3. Sound handles (fade / duck / pitch) in bsfx; then delete the enemy agent's stand-ins.
4. Shot kinds 2, 0x2c, 0x32; the other character modules (casters first), each verified.
5. Step 4: spells and items.
6. input_free_run's analog branch: use `pad_stick()` (boot pad, 0x80013cb8, 7f41c89).

## Tooling notes

- Disassembly ELF without stale symbols: `scratchpad/battle/modules/char01_live.elf`, built
  from a Redux RAM dump with `functions_live.csv` (docs/catalog minus the "battle" rows in
  0x800715d0-0x80088054: #2129's own bytes there are replaced by #1984 / #1981 at run time,
  and their symbols split the #1984 functions in Ghidra).
- `scratchpad/battle/framecmp.py REF PORT [from to step]`: per-frame mismatch, with 4x4
  block means that cancel the dither.
- `explore.lua`: `BW_HIT_BP=ADDR:TAG,...` logs each hit; `run_victory.sh` takes
  `BW_EXTRA_POKE`. Input alignment: Redux mash entry k at vsync 3810 + 45k matches port
  vblank 5 + 45(k - 6).

## Findings not yet in the catalog / notes

- Character → code entry: Claude 2131 (2133 identical), Rena 2135, Celine 2137, Bowman
  2139, Dias 2143 (= 2145), Precis 2155, Ashton 2147 (= 2149), Leon 2141, Opera 2153,
  Ernest 2151, Noel 2157, Chisato 2159. Code sizes 0.7-16.4 KB (casters tiny).
- Claude's vtable 0x801f178c overrides 23, 25, 35, 36, 37 (dispatch table 0x801f1924, 11
  states), 45, 46. MP costs by move byte at 0x8006b213 (4 7 0 9 15 11 17 12 28 32 38).
  Starting move byte 3 = Phase Gun (raises stats +0x2e to 0x2a8 for its hit).
- Afterimage draws `(head + 2i + 2) % count` (verified in the asm), i.e. half the copies
  twice with an even count.
- Particle lines read the GTE OTZ register after RTPS (stale: the last AVSZ's value).
- `battle_data` reads #2129; #1984 block 0 (0x800715d0) and #1981 (0x80085518) data come
  from the overlay images (`ovl1984_data` / `ovl1981_data`).
- ribbon_trail_update passes the start angle where the end angle's cosine belongs (xy1 / xy3
  use cos(start), sin(end)); the port keeps it. Shot +0x124 holds the trail colour until
  the first frame replaces it with the trail pointer (pointers are negative as s32).
- lens_flare_draw patches the shared stage flare tables in place (sizes[0..1], intensity[0]).
