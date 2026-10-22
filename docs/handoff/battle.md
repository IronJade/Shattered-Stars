# Battle: continuation log

Worker: battle agent (phase 2: HUD, result screen, killer moves; spells and items moved to the
world agent).
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

## Committed since: kind 0x10 / draw_sphere / lights_build_oriented, pc_casters (bbed220)

## Queue change (coordinator, 2026-10-22)

Step 4 (spells and items) now belongs to the world agent: spell_cast and the spell routines,
the #2129 spell-cast object API (0x80085518-0x80088054), the spell data loader, the unit cast
functions (0x80052000-0x80053fff), shot_hit_magic 0x800547d4 (a flagged addition in shot.cpp),
the spell-clash / skill-notice / scan windows, the enemy spell path in enemy_ai.cpp, and the
battle command ring / item list menus, in new files (battle/spells*, battle/spellcast*). This
worker keeps pc_state_air_attack, the big character modules, the Energy Sword compare, party
AI, voices, the analog branch and the effect-task leaks; character modules call spells through
the existing stand-ins until then. Answer the world agent's questions about the party cast
states and how to get a party setup with Rena.

## Clock alignment: no camera fix (coordinator correction, 2026-10-22)

The "port one frame late in the intro" report was a sampling-convention misread, and the
original's phase varies between references. Nothing in camera.cpp changed. Method written into
docs/notes/battle_port.md (Verification, "Clock alignment"): pick D per reference (zero RNG
mismatches; rngcmp.py and scratchpad/enemy/cmp.py take CMP_D), and read Redux samples as
the finished frame with its in-frame g_battle_frames value. Encounter 10's camera fields equal
the port's sample for sample (`ref/dense10`, `ref/order10` with loop-order breakpoints; port
`SS_BATTLE_CAM=1`, new in battle_debug.cpp).

## Verified since e37c3a5 (the user's commit of the unverified work)

e37c3a5 also holds SS_BATTLE_BOXES, SS_BATTLE_TICKS, the opening wipe (BurstFx in hud.cpp)
and the clock-alignment docs, which were on disk at the cutoff. Checked since:

- **Air attack and encounter 35 (verified).** The b470 miss was clock phase. The original's
  setup ended mid-way through a 2-vblank period (frames 2, 1, 2, ...). The intro was 5 ticks
  shorter in its battle frames, so the eagles' walk cels (body boxes) ran half a cycle apart.
  With `SS_BATTLE_TICKS=1` (D = 6/7, inputs at port vblank 45i):
  - RNG equal at every sample to the exit (b1048).
  - Units equal in 925/930 samples; the 5 others are HP caught a frame apart.
  - Claude's air attack (state 11) at b577 and the party's defeat at b807 match.
  - Frames under 1% from b160 (`ref/enc35`, `ref/enc35e`, port `scratchpad/battle/port/e35all`).
- **Opening wipe (verified).** BurstFx (0x8003bf18 / 0x8003c02c / 0x8003bf9c): intro frames
  b10-b150 went from 99% different to under 9%.
- **Ernest (verified, all 8 moves).** Refs: `ref/ernest_2_3` (byte 1), `ref/ernest_58`
  (byte 5), `ref/ernest_l3` (byte 3), `ref/ernest_l2/l4/l6/l7/l8` (bytes 2, 4, 6, 7, 8).
  - RNG is equal to past the exit in all seven. Units differ in at most 2 samples per run (HP
    sampling only).
  - Frames are under 1% through the moves, apart from a few % in the storm's streaks and the end
    fade.
  - Port command: `SS_BATTLE_TICKS=1 SS_BATTLE_POKE="3:0x0=10/2,3:0x20=200/2,3:0x22=200/2,3:0x24=200/2,4:0xcc=<byte>/1"`.
    Input: cross at 45i and l1 at 20 + 90i. Compare with CMP_D=7 rngcmp.py and unitcmp.py D 7.
  - Redux: `BW_EXTRA_POKE` needs hex values (`0x<byte>/1`). A plain `8/1` is silently
    skipped, which is why the first l8 / l7 runs did move 2.
  - R1 presses start no move in either the original or the port.
- **Bowman (in progress; 7 of 10 moves verified).** `pc_bowman.cpp` (#2139, all 10 moves and
  10 callbacks) and `projectile_arc_velocity` 0x8004906c (unit_move.cpp) are written,
  registered (pc_modules case 4, CMake) and build.
  - Verified against `ref/bowman_l<byte>` for bytes 1, 2, 4, 5, 6, 7 (moves 0, 1, 3, 4, 5,
    6): RNG equal to the exit, units at most 3 sampling differences, frames under 1-2%
    outside the end fade.
  - Move 3 matches with B9 kept faithful.
  - Refs for bytes 3, 8, 9, 0xa (moves 2, 7, 8, 9) are running.
  - The compare script: `scratchpad/battle/pcrun.sh CHAR BYTE REF OUT` (the port run, rngcmp,
    unitcmp and framecmp in one).
  - shot_place_at_unit_thunk 0x800553f0 passes dz 0.
- New original-bug candidate B9 (to add to original_bugs.md):
  - projectile_arc_velocity scales the arc's z by a constant 24 instead of the speed.
  - Bowman's move 3 calls it with speed 10, so his leap overshoots or undershoots in depth.
  - Kept faithful until the move is verified.
- On disk, uncommitted: `battle_debug.cpp` (SS_BATTLE_CAM res flags), `pc_bowman.cpp`,
  `unit_move.{h,cpp}`, `pc_modules.cpp` (case 4), `engine/CMakeLists.txt`, the catalog CSV,
  and the scratch tools (explore.lua `BW_REG_BP`; per-run cards in the run scripts).

## Next steps

1. Bowman: compare refs `bowman_l1/l4/l5/l6/l7` when they finish, then run bytes 3, 8, 9 and
   10. Then catalog rows (gen_catalog.py NEW, module battle_pc2139). Decompiled sources are
   in `scratchpad/battle/decomp/` (`decomp1.sh ELF ADDR` uses the private REA CLI's Ghidra).
2. Dias (#2143, char05, vtable 0x801f11d4: overrides 1, 35-37, 46-49).
3. B1 / B2 / B3 are fixed on disk (killer.cpp, particle.cpp; originals in
   scratchpad/battle/*.prebugfix). Report them as their own commit point. B5 and B9 are
   "decide".
4. Party AI (pc_ai_fighter 0x80038664 / healer 0x80038114 / caster 0x80038328), voices.
5. Claude's move 5 (Energy Sword) compare.
6. input_free_run's analog branch: use `pad_stick()` (boot pad, 0x80013cb8, 7f41c89).
7. Effect tasks (Afterimage / Ghost / RibbonTrail) are not deleted at battle end (leak), and
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
- `explore.lua`:
  - `BW_ELOG=LO:HI` logs per-vsync units in the enemy agent's format plus rng; compare with
    `scratchpad/battle/unitcmp.py REF PORT D` against the port's SS_ENEMY_TRACE=1.
  - `BW_BOXES=LO:HI` logs boxes and animation fields; the port's counterpart is SS_BATTLE_BOXES.
  - `BW_DENSE=LO:HI` logs the camera and clock every vsync.
  - The `[bw] frames` lines give D for rngcmp.py.
- The run scripts copy the cards and generated scripts into each OUT dir, so several Redux
  runs can go at once.
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
  twice with an even count. Fixed in the port (original_bugs.md B2).
- Particle lines read the GTE OTZ register after RTPS (stale: the last AVSZ's value).
  Fixed in the port (B3).
- `battle_data` reads #2129; #1984 block 0 (0x800715d0) and #1981 (0x80085518) data come
  from the overlay images (`ovl1984_data` / `ovl1981_data`).
- shot_apply_damage's redirect loop compares the random member with targets[tries],
  walking past the list (stack garbage on the console); the port compares only within it.
- ribbon_trail_update passes the start angle where the end angle's cosine belongs (xy1 / xy3
  use cos(start), sin(end)); fixed in the port (B1). Shot +0x124 holds the trail colour until
  the first frame replaces it with the trail pointer (pointers are negative as s32).
- lens_flare_draw patches the shared stage flare tables in place (sizes[0..1], intensity[0]).
