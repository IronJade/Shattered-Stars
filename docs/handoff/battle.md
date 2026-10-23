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
- **Bowman (verified, all 10 moves).** `pc_bowman.cpp` (#2139, 10 moves and 10
  callbacks) and `projectile_arc_velocity` 0x8004906c (unit_move.cpp) are registered
  (pc_modules case 4, CMake).
  - Refs `ref/bowman_l<byte>` for bytes 1-10. Every run has RNG equal to the exit (or the
    whole run), at most 3 unit samples off (HP sampling), and frames under 1-2% apart from
    the end fade.
  - Suites on this build: ss_tests 78/0, ss_game_tests 74/0 (SS_DISC1), smoke 21/21.
  - B9 (z * 24 in projectile_arc_velocity) was verified faithful with move 3. The user
    decided to keep the original (2026-10-22, revised: the leap may miss, like Headsplitter),
    so the z * 24 line is live again and z * speed is only a comment.
  - The compare script: `scratchpad/battle/pcrun.sh CHAR BYTE REF OUT` (the port run, rngcmp,
    unitcmp and framecmp in one).
- On disk, uncommitted: `battle_debug.cpp` (SS_BATTLE_CAM res flags), `pc_bowman.cpp`,
  `unit_move.{h,cpp}`, `pc_modules.cpp` (case 4), `engine/CMakeLists.txt`, the catalog CSV,
  and the scratch tools (explore.lua `BW_REG_BP`; per-run cards in the run scripts).

## Since the 22:14 resume

- **The attack-spell choice (0x8003a20c): verified.** `ref/caster35`: Celine as an AI
  caster in slot 1 next to Claude in encounter 35.
  - She chants (state 14) at b545 and b775 in the original and at port frames 539 / 769
    (D 6), with the RNG equal through her spells up to b840.
  - The split at b850 is the enemy agent's fix of original bug E9 (Carlaeagle feathers start
    0x78 above y 0 on the console; the fix starts them above the bird).
    - `ref/caster35s` (BW_SHOTS) shows the original's five feathers spawning at y -179 and
      hitting the airborne Claude at frame 843.
    - In the port they spawn at y -300, right at his height, and all five hit at once in
      frame 837, before his air attack's hit on the eagle (frame 839).
    - A deliberate difference, not a port bug.
  - New: `SS_BATTLE_SHOTS=LO:HI` (battle_debug.cpp) and explore.lua's `BW_SHOTS` print every
    live Shot each frame.
- **B5 fixed** (the user's decision): lens_flare_render works on a per-call copy of the
  flare set. The draw is identical by construction, since each call patched the table
  before using it; only the shared table no longer keeps the patched values.
  original_bugs.md is updated.
- **docs/ap/battle.md written** (the tool, the JSON shape, findings, the battle hooks).
  Linked from docs/archipelago_plan.md ("Battle hooks").
- **dias_l2 b470-490 (the effects agent's note): not the B3 fix as such.**
  - A build with the stale-OTZ line depth gives the same 4%.
  - The lines are at the right frame (best match at b-7).
  - Particle kind 1's update matches 0x8005e618 instruction by instruction.
  - What remains is which streak lines are drawn and where: the original culls lines by a
    stale OTZ (`0x5f9 < otz >> 5`), whose value depends on the console's last AVSZ. The
    port can't reproduce that value, and with B3 fixed it no longer depends on it. Expected
    under the enhance policy.

## M7 encounter sweep (current assignment, from 2026-10-22 late)

- **B9 is back to the original** (user, revised: keep z * 24; the leap may miss like
  Headsplitter). The z * speed version in unit_move.cpp is only a comment now.
  - The B9 measurement was skipped, on the user's word. One thing seen before that: after a
    cross attack, Bowman's leap takes off level with the target in depth (dz 0), so the
    two versions differ only on leaps from off to the side.
  - Checked again against `ref/bowman_l4` (move 3): RNG equal to the exit (b1192), 3 of 1441
    unit samples off, frames over 1% apart only from b1260 (the end fade).
  - Suites: ss_tests 78/0, ss_game_tests 81/0, smoke 21/21.
  - B1, B2, B3 and B5 are visual only. They change the trail vertices, the afterimage draw
    order, the line depth and a per-call copy of the flare tables, all of them drawing only.
- **New debug options** (battle_debug.{h,cpp}):
  - `SS_BATTLE_QUIT_AT_EXIT=1`: the run ends at battle_exit_to_field (runtime::QuitRequested).
    battle_debug_exit now prints `[battle] exit: encounter E outcome O battle frames B
    fight frames F`. It runs after the "all down → defeat" fix-up.
  - `SS_BATTLE_PARTY=1,2,3,4`: fills party slots 0..3 with field::party_slot_init.
  - `SS_BATTLE_STRONG=H:A`: other HP / attack factors. A plain value keeps x10 / x8.
  - When the boot disc lacks a load-list entry, `--battle` inserts the other disc
    (loader_insert_disc, the marker-entry test). It prints `[battle] entry N is not on this disc`.
- **tools/battle_sweep.py** (new). For each encounter:
  - The stage comes from the scripted sites and preloads, else from the random groups (the
    field program's 0x80074fe0 lists with encounter_maps.md's group → stage), else 3.
  - The party is Claude / Rena / Celine plus a rotating fourth, with the AI driving all
    (config +0x181), HP x20, ATK x8, MP 200, and killer moves on L1 / R1.
  - Cross is pressed every 30 vblanks, with a 15000-vblank cap and XDG in OUT/xdg.
  - At most 5 jobs run at once, on a copy of the binary.
  - `--stages LO-HI` runs one encounter on each stage.
  - Output: results.json / csv, missing.json (ranked), summary.md, logs/.
  - Disc paths: `. scratchpad/battle/discs.sh` (SS_DISC1 / SS_DISC2).
- Pass 1 (release build before the ports below, all 564) is in `scratchpad/battle/sweep/pass1`.
  - The trial SIGSEGVs (encounters 450 and 539) were the sweep's own doing. With HP x20 a
    member had over 9999 HP, and pc_ai_choose_heal (0x8003926c) starts its search at 10000,
    so `worst` stayed null. That can't happen in the game (HP caps at 9999), so
    SS_BATTLE_STRONG now caps HP at 9999 and the AI code stays faithful.
  - Pass 1 has no crashes. The missing pieces it found, by encounters hit:
    1. equipment effects (every encounter);
    2. the victory voice (the enemy agent's);
    3. stage code blocks (stages 0x24 / 0x10 / 0x29 / 0xe / 0x1a / 0x33);
    4. stage 8 lightning.
    "NO CASH" (the sprite texture cache is full) is the console's own message, logged once.
- **Ported and verified: pc_apply_equip_effects 0x800818dc** and its stat helpers
  0x800823d4-0x800827b4 (party.cpp):
  - the config modifiers;
  - about 95 ability-bit effect ids (the kAbilityBits table);
  - the stat effects, the 0x92 affection bonus and the 0x86 boost (it saves acc_hp_max /
    acc_mp_max).
  - Also: PartyUnit::status_test (pc_has_effect 0x8003b8f4) now returns 0 while +0x135
    (status_enabled) is clear, as the console does. The party AI clears it while scoring.
  - Redux `ref/equip_9f_86`: Claude with accessories 118 (0x9f, 5% evade, an RNG draw on
    each enemy hit) and 91 (0x86); the party idles until vsync 4710, then mashes.
    - Port (pcrun with `PC_IN` cross from 630 and `PC_EXTRA_POKE=",4:0x16=0x76/2,4:0x18=0x5b/2"`):
      RNG equal to the exit (b1326), 3 of 1642 unit samples off, frames 1.3-1.5% at
      b650-910.
    - The pre-port binary splits at b800 with 416 samples off, so the test is sensitive.
  - Input mapping: a Redux mash at vsync 3810 + 45k is pressed at port vblank 45(k - 6),
    pcrun-style with SS_BATTLE_TICKS=1.
- **Ported, on disk, Redux pending: pc_update_effects 0x8003adc8**
  (PartyUnit::update_status_effects): fury, attack boosts, stun, MP / HP drains, the guts /
  evasion / intelligence boosts, the 300 / 600-tick regenerations and the timed effects.
  - New PartyUnit fields: +0x2fe regen_ticks, +0x300 regen_mp_ticks, +0x302 mp_drain_ticks.
  - Ref `ref/equip_71_85` (accessories 67 = 0x71 HP regen and 90 = 0x85 MP regen, MP 50 of
    200) is running.
- **Ported, Redux pending: stage 8 lightning** (director.cpp): stage_random_point plus
  effect_spawn_streak_at(from 0x76c up, 0x101010 / 0xffffff, 0x60, 0x1000, 0x1000, puff 1).
  - It needed kind 0x12's +0x10c 1 branch (the world agent's spell_shots.cpp, a 3-line
    edit): size 0x10, and the streak ends where unit_check_move(s, vel, 7) is blocked.
  - Ref `ref/stage8_light` (encounter 2, stage 8) is running.
- **Opera move 7 trails (the effects agent's question)**: in progress.
  - New dumps: explore.lua `BW_TRAIL=LO:HI` (the breakpoint object must stay referenced or
    Redux drops it) and the port's `SS_BATTLE_TRAIL=LO:HI` (battle_debug_trail).
  - Suspect: the console keeps the trail pointer at Shot +0x134, and the trail's back
    pointer clears it when the trail dies, possibly in a reused pool slot. The port uses
    p134 = 1 as a flag, with the back pointer on `s->trail`, so a cleared slot never
    re-creates a trail.
  - Ref `ref/op_m8_trail` is running.
- **Next: the stage code blocks.**
  - ELFs with each #1984 block 2-10 patched in at 0x8008c1f4 are in
    `scratchpad/battle/stage_blocks/blk<n>.elf` (`blk0e.elf` holds the image's kind-0x19
    handler).
  - Ghidra decompiles go to `scratchpad/battle/decomp/` (stage_blocks/decomp_all.sh).
  - battle_run's stage objects are in analysis/decomp/battle/8007160c (lines 165-280).

## Next steps

1. **Dias (verified, all 10 moves)** (#2143, char05, vtable 0x801f11d4).
   - `pc_dias.cpp` (10 moves, 8 callbacks) is registered (pc_modules case 5, CMake).
     Catalog rows are in.
   - Bytes 1-10 (moves 0-9) verified: RNG to the exit, units at most 3 sampling
     differences.
   - One visual difference in move 2's sword wave: its tinted ground shadow
     (shot_draw_and_box colour 0x2080: code 0x2e, tpage 0x0f, so blend mode 0) is dark
     olive on the console and brownish in the port. bobj_draw_shadow builds the same code
     and tpage as the console (0x800443f8, checked). Suspects: the GPU's modulated
     semi-transparent texturing, or the shadow blob's texels.
   - Frames differ only where the B2 fix (afterimage copies) and the B3 fix (line depth)
     change the picture.
   - Refs are `ref/dias_l1`..`dias_la`. `queue_runs.sh CHAR NAME BYTE...` queues such runs
     with at most 3 of my Redux runs at once. The first dias_l3 run hung at boot (no "clock starts" after 35
     min); I killed it by PID and requeued it.
   - An ASan run is clean. It covered Dias's move 9, an AI Rena and the battle exit with
     effect_tasks_free_all.
2. **Party AI (verified: healer, fighter, caster paths).** New `party_ai.{h,cpp}` covers all of
   0x80038114-0x8003a9cf plus 0x8003b6d8 / 0x8003bc9c. It is hooked into pc_ai_update
   (party_states.cpp), including the forced random run.
   - Refs: `ref/rena_ai` (Claude at 500 HP; Rena casts Cure Light at b281, as in the port)
     and `ref/rena_ai1` (tactic 1, no cast).
   - RNG is equal to past the battle exit in both. Units 1875/1880 and 1454/1456, all HP
     sampling. Frames match apart from the end fade.
   - Fighter and caster, verified:
     - `ref/fighter_ai`: Bowman in slot 1 (class copied from Claude). His AI approaches and
       attacks at the same frames. RNG equal to the exit, units 1414/1418.
     - `ref/caster_ai`: Celine (class 10, five spells learned, tactic 0). She never casts in
       either version (the RNG never passes both 1/8 checks), but keeps away the same way.
       RNG equal to the exit, units 1795/1801.
     - The attack-spell choice (0x8003a20c) has not been exercised against Redux yet.
   - Test setup: `SS_BATTLE_COPY` (new, battle_debug.cpp) copies slot 0's stats and record
     to slot 1. Then `SS_BATTLE_POKE` sets id 2, class 9, MP and the learned spells (record
     +0x3c..); see `scratchpad/battle/run_rena_ai.sh`.
   - Redux runs use explore.lua's `BW_COPY` (copies at vsync 4047, pokes at 4048).
3. B1 / B2 / B3 / B5 fixed (visual only). B9 kept as the original (user, revised).
4. Voices: the enemy agent took the voice player (voice_play_entry, intro voices).
5. Claude's move 5 (byte 6): verified against `ref/claude_l6` (RNG to the exit, units at
   most 3 sampling differences).
6. Done on disk (unverified against Redux; they build):
   - input_free_run's left-stick branch (pad_stick, speed * deflection / 0x50; the d-pad
     when the stick is centred).
   - effect_tasks_free_all (killer.cpp, called from battle_free_all): frees the
     Afterimage / Ghost / RibbonTrail tasks still running when the battle ends.
   - Unit +0x1f8 is still `effects[1]` (an Afterimage*). No ported module uses it yet.

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
- From the enemy agent: Claude's approach walk seemed to drift about 1 unit per 10 frames
  (encounter 10, Cross mash, from b340). That was the clock alignment: at the right D (and
  with SS_BATTLE_TICKS for the 1-tick group) every Claude sample in encounters 2, 35 and
  the module runs matches. +0x12a is
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
