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

## Step 3 part 1: committed (403f921)

Claude's module, Particle pool, Afterimage / Ghost, party loader phases 1-4, kind 0x11,
fx_draw (vortex, lens flares), SS_BATTLE_POKE; verified against Redux (`ref/phasegun_2_3`,
`ref/phasegun_exit`, `ref/pg_cash`, `ref/moves_9_5`; see docs/notes/battle_port.md).

## Committed since: kind 0x16 + shot_apply_damage + RibbonTrail (162ad2d); sound handles (22ac0e8)

## Committed since: kinds 2 / 0x2c / 0x32 (3c4509e)

## In progress (uncommitted, all build)

- Kind 0x10 (`shot.cpp` shot_k10_sphere) with `fx_draw.cpp` draw_sphere 0x8007c520 and
  `render.cpp` lights_build_oriented 0x80076eec. Exercised by encounter 340 (wizard): the
  impact explosion looks like the reference; the wizard's sequence runs ~5 frames early in
  the port because of the enemy odds-batch phase (below), so no exact frame match yet.
- The small party modules: `pc_casters.cpp` (Rena #2135, Celine #2137, Leon #2141, Noel
  #2157), registered in `pc_modules.cpp`, `CMakeLists.txt`; `unit.h` gains PartyUnit slot 42
  `spell_hook`. Not run-time tested (spells and the party members' setups are not there).
- `loaders.cpp` / `docs/notes/battle.md`: mem_alloc_b -> mem_alloc_high (catalog rename).
- Catalog regenerated (501 rows).
- Found (reported): in every encounter compared (10, 340) the enemies' first odds batch
  (5 rng draws every 8 ticks, enemy_ai.cpp ai_ticks) fires on the first frame of state 2 in
  the port but about one cycle later in the original; the RNG totals re-sync by b300 but the
  decisions stay a few frames early. Enemy-side; routed to the enemy agent. Claude's
  approach drift in encounter 10 follows from it.

## Next steps

1. PartyUnit::state_air_attack (pc_state_air_attack 0x80037d08, jump attacks) is still a
   todo in party_states.cpp: port it (all members but Leon use it).
2. The bigger character modules (Bowman #2139, Dias #2143, Precis #2155, Ashton #2147, Opera
   #2153, Ernest #2151, Chisato #2159), each verified as Claude's was.
3. Claude's move 5 (Energy Sword) compare; the other character modules (casters first).
4. Step 4: spells and items (shot_hit_magic 0x800547d4 next to shot_apply_damage).
5. input_free_run's analog branch: use `pad_stick()` (boot pad, 0x80013cb8, 7f41c89).
6. Effect tasks (Afterimage / Ghost / RibbonTrail) are not deleted at battle end (leak), and
   `Unit` has no dtor destroying its effects (console unit_destroy 0x8004def0 does). Unit
   +0x1f8 is a RibbonTrail on the console (port: `effects[1]`, unused so far).

## Tooling notes

- `scratchpad/battle/asm.py ADDR...`: instructions through the private REA CLI
  (`scratchpad/battle/rea.sh`, ~30 s a call), saved in `scratchpad/battle/asm/<addr>.s`. The
  shared MCP REA session gets switched by other agents mid-batch.
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

- The Redux "forced" fights so far (`ref/forced_1_3`, victory, Phase Gun, moves) are all
  encounter 2 on stage 3: the old poke of game_state +0x99 did nothing (the field reads the
  encounter from +0x264, s16); `run_forced.sh` now pokes +0x264. The comparisons hold (both
  sides ran encounter 2).
- From the enemy agent: Claude's approach walk drifts about 1 unit per 10 frames from the
  original (encounter 10, Cross mash, from b340): PartyUnit code, to chase. +0x12a is
  uninitialised heap in the original until first set. Oozes / Alraunes never fire in those
  encounters (attack odds 0); archers (enc 65, 420: Robinfake), Shout (320) and the wizard
  (340) fire with an idle party: use them for kind 0x16. Darkcrusader (enc 280) uses kind 2.

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
- shot_apply_damage's redirect loop compares the random member with targets[tries],
  walking past the list (stack garbage on the console); the port compares only within it.
- ribbon_trail_update passes the start angle where the end angle's cosine belongs (xy1 / xy3
  use cos(start), sin(end)); the port keeps it. Shot +0x124 holds the trail colour until
  the first frame replaces it with the trail pointer (pointers are negative as s32).
- lens_flare_draw patches the shared stage flare tables in place (sizes[0..1], intensity[0]).
