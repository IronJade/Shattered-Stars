# Battle: continuation log

Worker: battle agent (phase 2: HUD, result screen, killer moves; spells and items moved to the
world agent).
Build dirs: `engine/build-battle` (normal) and `engine/build-battle-asan` (ASan; run with
`LD_PRELOAD=scratchpad/battle/asan/usr/lib64/libasan.so.8`). Scratch (on disk):
`scratchpad/battle/`. The worker does not commit; the coordinator does.

## Hand-over (2026-10-23 ~03:00, second rollover): start here

**State.** Every item of the assignment is done. M7 waits only on the effects agent's battle
talk layer (party_talk.{h,cpp}: opening lines, talk pose, provoke, ally reaction and fury,
flee and victory voices, weapon throws), which that agent owns and is verifying.
CHECKLIST.md's M7 and "Battle, part 2" lines say so. Tick them together with the effects
agent once its talk checks are in.

**Uncommitted files of mine** (the coordinator's checkpoints commit them):
- party.cpp (B14 model; battle_debug_loaded call)
- battle_program.cpp (party_note_stage_file)
- unit.h (declaration)
- stage_objects.cpp (B13)
- bsfx.{h,cpp}, bobj.{h,cpp} (B15)
- battle_debug.{h,cpp} (SS_BATTLE_CAM view line; SS_BATTLE_RNG_SKIP / battle_debug_loaded)
- enemy_ai.cpp (dead Weirdaxe todo removed: the enemy agent's file, flagged)
- docs: original_bugs.md B13/B14/B15, the catalog (operator_new; pc_state_approach;
  enemy_axe_knockback; bsfx_pan_update), battle_sweep.md, CHECKLIST.md, m8_audit.md (a short
  late-save note: the menu agent's file, flagged), docs/ap/battle.md (the final boss wording).

**Suites on this build** (before B15 and SS_BATTLE_RNG_SKIP; rerun before calling the step
verified):
- ss_tests 78/0
- ss_game_tests 104/0 (SS_DISC1)
- smoke 37/37
- After B15: refs stage14c, caster35, bowman_l1 and stage51 are unchanged.

**Leads for the successor** (from the coordinator, not yet looked at):
1. **ref_regress diverges on two stage refs.** In `tools/ref_regress.py`
   (`scratchpad/regress/full_head2/summary.md`), stage16c diverges at b270 and stage36_long at
   b1410 at HEAD. Check whether they are real or the suite's setup:
   - stage16c is a caster ref: it needs `cmpstage_caster.sh stage16c <S>` (S from its
     ram_battle_loop dump). With that, it was RNG-equal to b1840 this session.
   - stage36_long has no exit line, so cmpstage.sh's S comes out empty and the input offset
     becomes 4076 (no presses). That is how it was verified (1298 Shot frames equal).
     CMP_OFF=-1 makes the cart spawn differ.
   - Also check tools/refs/refs.csv for a missing poke or re-sync point.
2. **The world agent's no-seed check on ref/victory_2_3.** It shows small temporary RNG
   differences at b270-b400 that recover by b600 (equal b600-b1240); the ref used to match
   frame for frame.
   - Check whether this is new, and whether the talk layer (opening lines, ally reaction) or
     B13 / B14 is involved.
   - B13 is draw-only (A/B proved on stage36_long).
   - B14 on stage 3 gives Claude 0xc20ae6af (negative), so it behaves like the old 0.
   - So look at the talk layer first: build without party_talk's hunks, or set its env off.
3. **The tools agent's list** (docs/handoff/tools.md): v538_3, h531_3 and h539_3 at b50, and
   m524_3 / m537_3 / m538_3 / m539_3. These are possibly the party-load (state 0) RNG offset,
   which their generic run scripts don't re-sync (see "RNG after CD-timed loads" in
   battle_sweep.md).

## Session 2026-10-23 (new battle agent): progress

Assignment (coordinator):
1. Compare the stage refs.
2. Fix stage 0xe.
3. Run the ASan pass.
4. Check the party's one-frame-early hit reaction (eA-eF, encounter 340).
5. B13 popup.
6. Weirdaxe debris Shots (enemy_axe_knockback).
7. Update CHECKLIST.

The victory voice and the whole talk layer moved to the effects agent (party_talk.{h,cpp}). Don't tick M7 until that has landed and been verified.

- **Stages 0xd / 0x17 / 0x1f: verified.** Refs stage13 / stage23 / stage31, `cmpstage.sh`:
  - Shots equal in every frame (660 / 593 / 637). stage31's b7 is the first sample's tick phase only.
  - RNG equal to past the exit.
  - Units off only in the intro x samples (b28-b42, as in every stage ref) and HP sampling.
- **Stage 0xe: fixed and verified (B14).** The b960 split was not E9. Claude's first jump check reads PartyUnit +0x230, which nothing initialises:
  - On the console it holds a word of the raw stage file (stage14c: 0x04afbca6, positive, so he jumps at b951). The port had 0.
  - New: party.cpp `heap_word_at_unit` and `party_note_stage_file`; battle_program.cpp passes load-list entry 2 to it; the declaration is in unit.h.
  - Result: ref stage14c has RNG equal to the exit (b1954), 7 of 3177 unit samples off, and Shots equal in all 975 frames.
  - Rechecked after the change: stage13/23/31/26/41/36/21/8_light/16c are unchanged. caster35 (with `cmpstage_caster7.sh`, the 2-1-2 tick group) has RNG equal to b2060, past the exit.
  - Catalog: operator_new 0x8001fd94 added, and pc_state_approach's row now notes B14. original_bugs.md has B14.
- **Final boss (coordinator's question): answered.**
  - Map 685 tests flag 0x22c (set by Filia's event on map 239). Set: encounter 540. Clear: 539. One fight either way.
  - Told the world agent the goal default should be [539, 540]. Fixed the wording in docs/ap/battle.md.
- **ASan pass:** running (`scratchpad/battle/sweep/asan`, PID in sweep/asan.pid).
- **Stage 0x33: verified.** ref stage51 (`cmpstage.sh stage51`):
  - RNG equal to the exit (b1282).
  - Shots equal in all 639 frames.
  - 3 of 1558 unit samples off.
  - Frames about 1% at most until the exit.
- **B13: fixed (visual only).** stage_objects.cpp rolling_hits: the popup shows `damage` (100 for the cart; 50 for the 0x1a / 0xd boulders as before).
  - A/B on stage36_long: the RNG, units and Shots traces are byte-identical with and without the fix (`scratchpad/battle/b13/`).
  - original_bugs.md row updated.
- **Weirdaxe debris: verified.** The work was already done: EnemyAxe::start_reaction in enemy_families.cpp (the enemy agent's) matches 0x8003f37c instruction for instruction.
  - The todo in EnemyUnit::start_reaction (enemy_ai.cpp) was dead code. It is replaced by a pointer comment, and the unused todo() helper is removed (a small edit in the enemy agent's file).
  - Redux `ref/axe372` (encounter 372, Claude byte 1): the k2c debris equals BW_SHOTS in all 17 frames, and every Shot is equal in all 365 compared frames.
  - RNG equal to the exit (b734), 1 of 1449 unit samples off, b370 frame 0.35%. `shotcmp.py REF PORT D [KIND]` is new.
  - Catalog row updated.
- **Particle kind 4: verified visually.** The world agent's i11a sweep (Explode, `explode/cap.sh i11a 6 DIR B...`): the purple shards match, e.g. b760 at 0.8% blocks (`explode/pair_b00760.png`). The odd frames' big numbers are the slow-frame phase.
- **Particle kind 5 (confetti; item 0x2f9, effect 0x5e): in progress.**
  - Ref `ref/conf340` (no tick log): the confetti is there in both. The frames differ from b315 because of the console's slow frames (the port matches at D 4 there).
  - `ref/conf340t` (BW_TICK_LOG) was running at 01:05. Replay it with SS_BATTLE_TICK_MAP, as `explode/cap.sh` does.
- **The "one-frame-early party hit reaction" (world agent, it340 b1290 and rn340): not a port bug.**
  - In all four samples the state is equal and only the HP differs.
  - SS_ENEMY_TRACE prints the party at the first enemy's think, before the enemies act. An enemy's hit in frame N therefore shows in the port's party HP one sample later, while Redux samples at the end of the frame.
  - Dense ref stage13 b421-424: the ref drops HP at b421 and enters s3 at b423. The port shows s3 at b424 (the same 2-vblank update) and the HP at its next print.
- **ASan pass: done.** 325 victory, 82 defeat, 153 cap, and 4 ASan stops. All four were a
  heap-use-after-free in bsfx_pan_update: a SpellCast deletes itself while its own sound pans
  from it.
  - Fixed as B15: ~BObj → bsfx_forget_emitter moves the channel onto its internal emitter at
    the last position. Sound only.
  - Rerun (`sweep/asan_fix`): all four clean, same outcomes as pass 2.
- **Particle kind 5: verified.** Ref `ref/conf340d`, item 0x2f9 on Claude at b300.
  - The first big "camera" differences were the harness's mash 2 vblanks early. With the
    presses at 44 + 45i (`NO_MASH=1 PORT_INPUT=...`), the view (new
    `[battle] view` line in SS_BATTLE_CAM vs wredux explore.lua's `[bw] view`, `viewcmp.py`)
    is equal in all 71 frames, and the frames are 0.7-2.2% through the confetti.
  - My copy of the world agent's Redux tooling is in `scratchpad/battle/wredux/` (BW_ITEM,
    BW_TICK_LOG, BW_DENSE with the view line).
- **Late-save final boss (coordinator): closed.** The port's party load ends state 0 at b0,
  the console's at about b102 (CD-timed). The coordinator decided not to model it.
  - Re-synced with the new `SS_BATTLE_RNG_SKIP=1` (one draw at state 0→1): the generator
    equals Redux at all 250 samples b102-b600, the AI included (`scratchpad/battle/late/p2`).
  - The other one-draw difference is the field's: the voice queue on map 671 versus the walk
    rolls.
  - Documented in battle_sweep.md and m8_audit.md. The tools agent was told (it documented
    the re-sync in tools/refs/README.md).
- **prec_m1 "regression" (coordinator): not one.** The repro used USES=0, but the ref is the max-level move (uses 999).
  - With 999: prec_m1, as_m1, chis_m1, bowman_l1, dias_l1 and ernest_2_3 are RNG-equal past the exit.
  - op_m1 (move 0) splits at b400 as in the effects agent's own run (console slowdown).
  - `fxcheck/port_pc_b.sh` is a copy of its port_pc.sh writing to my scratch.

## Hand-over (2026-10-23, first rollover; done, kept for its tool notes)

Assignment (coordinator): finish M7 with an encounter sweep.

1. Write `tools/battle_sweep.py`: every encounter, port only, at most 5 jobs,
   XDG_CONFIG_HOME=<out>/xdg, --no-memcard; one ASan pass. **Done.**
2. Write `docs/notes/battle_sweep.md`, bosses marked. **Done** (passes 1 and 2).
3. Port the missing pieces, most-hit first, checking against Redux where it's cheap. **Mostly
   done**; see the status table in battle_sweep.md.
4. Update docs/CHECKLIST.md's "Battle, part 2" line. **Done**, 2026-10-23.

### State at hand-over

- **Sweep pass 1** (`scratchpad/battle/sweep/pass1/`):
  - 564 runs, no crashes; ranked list in battle_sweep.md.
  - Every piece found is ported except the victory voice, which the enemy agent owns.
- **Sweep pass 2** (`scratchpad/battle/sweep/pass2/`, `--god --strong 20:30:4`): finished.
  - Outcomes: 325 victory, 83 defeat, 156 cap. No crashes.
  - Missing pieces: only the victory voice, plus particle kind 4 from the binary snapshot
    taken before that port.
  - The numbers are in battle_sweep.md.
- **ASan pass: not run yet.** The ASan binary is up to date (built after the particle port).
  Command:

  ```
  . scratchpad/battle/discs.sh
  tools/battle_sweep.py --out scratchpad/battle/sweep/asan --god --strong 20:30:4 \
      --bin engine/build-battle-asan/shattered-stars
  ```

  It runs about 5x slower; the sweep script sets ASAN_OPTIONS=detect_leaks=0.
- **Ported this session and verified against Redux** (the M7 sweep section below has the
  numbers):
  - pc_apply_equip_effects 0x800818dc and its helpers;
  - pc_update_effects 0x8003adc8;
  - pc_has_effect's +0x135 test;
  - stage 8 lightning, plus kind 0x12's +0x10c 1 branch in spell_shots.cpp (world agent's
    file, 3 lines);
  - the stage code for stages 0x1a (ref stage26), 0x29 (stage41), 0x24 (stage36 and
    stage36_long, cart included), 0x15 (stage21) and 0x10 (stage16c, with an AI Celine
    casting).
- **Ported, check pending**:
  - Stage 0xe (kind 0x19). Ref stage14c (Claude plus AI Celine, encounter 35):
    - RNG splits at b960, and a kind-0x16 projectile differs at b970 (ref vel 117285,-83615;
      port 141960,21840).
    - The three k19 objects are equal up to there.
    - Encounter 35's Carlaeagle feathers carry the enemy agent's E9 change, so look there
      first. Compare `./cmpstage_caster.sh stage14c 4078` with `ref/caster35`.
  - Stages 0xd (ref stage13), 0x17 (stage23) and 0x1f (stage31): Redux runs were going at
    hand-over (PIDs 4070602 / 4073635 / 4079043). They end by themselves; then run
    `./cmpstage.sh stage13` etc.
  - Stage 0x33: not queued. Run `./queue_stages.sh 51`, then `./cmpstage.sh stage51`.
  - Particle kinds 4 / 5 (particle.cpp): visual only. XINE (encounter 199, stage 10) spawns
    kind 4.
- **Answered for the effects agent**: Opera move 7's trails. The trail state is equal; the
  difference is the B1 fix, which a faithful build confirms. Reported to main.
- **Original bugs logged**: B11 fixed (visual), B12 kept (play), B13 decide (the cart's popup
  shows 50 for 100 damage). B9 is back to the original by the user's decision.
- **Suites on the current build**: ss_tests 78/0, ss_game_tests 98/0 (SS_DISC1), smoke 37/37.

### Uncommitted files of mine (the coordinator's checkpoints commit them)

- New: `engine/src/game/battle/stage_objects.{h,cpp}`, `docs/notes/battle_sweep.md`.
- Modified:
  - battle_program.cpp: stage_objects_create; the unused `todo` removed.
  - director.cpp: the stage hook, lightning.
  - killer.cpp: the B1 comment.
  - particle.cpp: kinds 4 / 5.
  - shot.cpp: stage_shot_update.
  - engine/CMakeLists.txt: stage_objects.cpp.
  - docs: original_bugs.md, CHECKLIST.md, notes/README.md, this file.
- Already committed by the checkpoints: party.cpp, party_states.cpp, battle_debug.{h,cpp},
  unit.h, unit_move.{h,cpp}, spell_shots.cpp, tools/battle_sweep.py.

### Tools and scripts (all in `scratchpad/battle/` unless a path is given)

- **Sweep and discs**:
  - `tools/battle_sweep.py`; see its docstring and battle_sweep.md.
  - `discs.sh` exports SS_DISC1 / SS_DISC2.
- **Port compare against a run_victory.sh reference**:
  - `pcrun.sh CHAR BYTE REF OUT [FRAMES]`.
  - Env: `PC_IN` (input list), `PC_EXTRA_POKE` (",TAG:OFF=V/S"), `PC_TICKS` ("" for the
    2-2-2 group, default 1) and `PC_D` (default 7). Other SS_* variables pass through, e.g.
    SS_BATTLE_SHOTS and SS_BATTLE_COPY.
- **Special stages**:
  - `cmpstage.sh NAME [FRAMES]`: port run on ref/NAME, plus RNG with a base fix, units and
    Shots frame by frame. It finds the tick group and the input offset itself (press = 45i +
    4082 - S - D, S = the battle-start vsync; `CMP_OFF` overrides).
  - `cmpstage_caster.sh NAME S`: the same for the Claude + AI Celine refs. S is the vsync in
    ref/NAME/ram_battle_loop_<S>.bin.
- **Redux references**:
  - `run_victory.sh OUT`, with BW_ENC / BW_STAGE / BW_MASH / BW_EXTRA_POKE (hex values) /
    BW_SHOTS / BW_ELOG / BW_BATTLE_FOR / BW_TRAIL / BW_COPY.
  - `queue_runs.sh CHAR NAME BYTE...` (killer moves) and `queue_stages.sh STAGE...`.
  - `run_stage_caster.sh STAGE NAME [ENC]`.
  - All keep at most 3 of my Redux runs at once (they count `pgrep -af "^timeout [0-9]+
    .*pcsx-redux -cli.*battle/ref/"`).
- **Other compare scripts**:
  - `rngcmp.py` (CMP_D), `unitcmp.py REF PORT D`, `framecmp.py` (FRAMECMP_D);
  - `trailcmp.py REF PORT` (BW_TRAIL against SS_BATTLE_TRAIL);
  - `diffmap.py` / `crop.py` (frames to PNG).
- **Disassembly and catalog**:
  - `asm.py ADDR` (ASM_ELF picks the ELF) and `decomp1.sh ELF ADDR`, both through the
    private REA CLI.
  - Stage-block ELFs: `stage_blocks/blk<n>.elf` (symbols `stage_blocks/syms<n>.txt`; asm in
    `stage_blocks/asm/`).
  - `gen_catalog.py`, run from the repo root, writes analysis/battle_catalog/port_updates.csv
    (622 rows; the stage blocks come from battle_overlays.csv).
- **Debug variables, new this session** (battle_debug.h):
  - SS_BATTLE_QUIT_AT_EXIT, SS_BATTLE_PARTY, SS_BATTLE_STRONG=H:A:D (HP capped at 9999),
    SS_BATTLE_GOD, SS_BATTLE_TRAIL;
  - explore.lua: BW_TRAIL. A breakpoint object must stay referenced, or Redux drops it.

### Rules (from the coordinator; keep them)

- **Tools and builds**:
  - REA (the private CLI) for disassembly.
  - Catalog every function: gen_catalog.py.
  - Build only in engine/build-battle and engine/build-battle-asan, with -j8.
- **Runs**:
  - Headless only: `--headless`, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy.
  - At most 3 Redux instances; kill my own by exact PID.
  - Every game, test and sweep with XDG_CONFIG_HOME pointed at a scratch dir
    (scratchpad/battle/xdg), plus --no-memcard unless a card is named.
  - Never touch ~/.config/shattered-stars/config.ini or the user's saves.
  - Scratch on disk under scratchpad/battle/ (/tmp is RAM).
- **Process**:
  - No commits; the coordinator checkpoints hourly.
  - Keep this file and the catalog current at least every 20 minutes.
  - Run ss_tests, ss_game_tests (SS_DISC1) and smoke before calling anything verified.
- **Enhance, don't degrade**:
  - No slowdown modelling.
  - For a real original bug, comment the original as `// ORIGINAL BUG (original_bugs.md
    <id>): ...` and write the fix below it.
  - "We are fixing bugs, not breaking the play state": a fix must not change hit timing,
    the number of hits, damage, difficulty or pacing. If it would, keep the original and log
    it.
  - Verify the faithful version first when a bug changes what a Redux comparison measures.
  - Save data stays byte-compatible.
  - Visual-only start-point fixes offset the drawing, never the path (the enemy agent's E9
    pattern in shot.{h,cpp}).
- **Ownership**:
  - Enemy agent: enemy classes, voices, Shot kinds 0x27 / 0x2d, tools/ap_extract.py.
  - World agent: spells and items.
  - Effects agent: Precis / Ashton / Opera / Chisato and the GPU.
  - The rest of battle is mine: party AI, Bowman, Dias, Ernest, stage code, the sweep.

## Done and verified

- **Step 1, HUD / windows / #1980 text / message banner**: `battle/window.{h,cpp}`,
  `battle/hud.{h,cpp}`. Committed (b556298). Compared with Redux: both HUD layouts,
  "Phase Gun" in the #1980 font; banner state machine in `battle_message_banner`.
- **Step 2, #1981 result screen**: `battle/result.{h,cpp}`, `battle/voice.{h,cpp}`
  (victory and result voice picks with their RNG draws). Committed (c723033). Redux
  victory reference `scratchpad/battle/ref/victory_2_3` (forced fight, Claude poked like
  `SS_BATTLE_STRONG`): RNG draw counts equal at every 10 vsyncs (335), result screen and
  exit on the same vblanks (946 / 1236), handoffs 3 and 4 byte-identical at
  battle_exit_to_field, 6 identical except the item-table pointer at +0x820, 2 identical
  except the field's swap counter (+0x14) and the voice clips' sound-test record.
- **Step 3a, the Shot pool core** (`battle/shot.{h,cpp}`): pool of 160, spawn / reset /
  kill / fade / draw-and-shadow / hit_melee / place_at_unit, callback kinds 0x21 / 0x33,
  kind 1 (stun stars; `unit_apply_stun` moved here from combat.cpp). Other kinds log once
  and are removed. Camera target / focus widened to `BObj*` (a Shot can be followed).
  Committed in 7fc05c0.

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

## Step 3 part 1: committed (9517cb9)

Claude's module, Particle pool, Afterimage / Ghost, party loader phases 1-4, kind 0x11,
fx_draw (vortex, lens flares), SS_BATTLE_POKE; verified against Redux (`ref/phasegun_2_3`,
`ref/phasegun_exit`, `ref/pg_cash`, `ref/moves_9_5`; see docs/notes/battle_port.md).

## Committed since: kind 0x16 + shot_apply_damage + RibbonTrail (ec0d098); sound handles (5f04296)

## Committed since: kinds 2 / 0x2c / 0x32 (086946c)

## Committed since: kind 0x10 / draw_sphere / lights_build_oriented, pc_casters (ab70773)

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

## Verified since 416d7b2 (the user's commit of the unverified work)

416d7b2 also holds SS_BATTLE_BOXES, SS_BATTLE_TICKS, the opening wipe (BurstFx in hud.cpp)
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
- **Verified: pc_update_effects 0x8003adc8** (PartyUnit::update_status_effects): fury, attack
  boosts, stun, MP / HP drains, the guts / evasion / intelligence boosts, the 300 / 600-tick
  regenerations and the timed effects.
  - New PartyUnit fields: +0x2fe regen_ticks, +0x300 regen_mp_ticks, +0x302 mp_drain_ticks.
  - Ref `ref/equip_71_85` (accessories 67 = 0x71 HP regen and 90 = 0x85 MP regen; MP 50 of
    200): RNG equal to the exit (b1506), 4 of 1855 unit samples off. Claude's +39 HP regen
    lands at the same frame.
- **Verified: stage 8 lightning** (director.cpp): stage_random_point and
  effect_spawn_streak_at(from 0x76c up, 0x101010 / 0xffffff, 0x60, 0x1000, 0x1000, puff 1).
  - It needed kind 0x12's +0x10c 1 branch (the world agent's spell_shots.cpp, a 3-line edit):
    size 0x10, and the streak ends where unit_check_move(s, vel, 7) is blocked.
  - Refs `ref/stage8_light` and `ref/stage8_shots` (BW_SHOTS):
    - every streak spawns in the same frame at the same point and falls identically;
    - RNG is equal to b940;
    - after that, a cross press lands one frame apart (input parity), not a lightning
      difference.
  - Those refs are in the 2-2-2 tick group, so use `PC_TICKS="" PC_D=6` and inputs at 45i-1
    (pcrun.sh now takes PC_TICKS, PC_D, PC_IN and PC_EXTRA_POKE).
- **Opera move 7 trails (the effects agent's question): answered.** The trail state matches
  Redux on every update. The b480-500 difference is the B1 fix: the console's far edge uses
  quad 0's start angle for every quad. A faithful build drops it to 0.06-0.15%. b510-540 is
  the sphere / blob, not the trails. B1's comment and its row in original_bugs.md are
  corrected.
- **Ported: the stage code blocks and stage objects** (new `stage_objects.{h,cpp}`).
  - The hooks: stage1a_update, stage0d_update, stage15_update, stage24_update, stage29_update
    (director.cpp calls `stage_code_update()` unless flags & 0xc).
  - Shot kinds 0x18 / 0x19 / 0x1a / 0x1b / 0x1c / 0x1e / 0x1f / 0x20 / 0x22 / 0x23 / 0x28 /
    0x2a / 0x2b (shot.cpp's default case calls `stage_shot_update`).
  - battle_run's stage objects: `stage_objects_create()` after battle_setup_sprite_banks.
  - Obstacle slot 23 (the rolling objects) and 23 - index (the hanging / stacked pieces) live
    in StageData::obstacles. 0x8006fb57 is obstacles[23].kind.
  - Original bugs found:
    - B11: a hanging object's shadow comes from the wrong slot. Fixed, visual only.
    - B12: its fall check clears the wrong slot, so the first object never falls. Kept, play.
    - B13: the cart's popup shows 50 but the cart takes 100 HP. Decide.
  - Sources: decompiles in `scratchpad/battle/decomp/blk*.c` and disassembly in
    `scratchpad/battle/stage_blocks/asm/`. The catalog has 18 rows for them.
  - Redux checks (encounter 2 with BW_SHOTS, `cmpstage.sh`): Shots equal frame by frame,
    RNG equal to the exit, at most 8 unit samples off (HP sampling):
    - stage26: 503 frames;
    - stage41: 660 frames, flakes;
    - stage36: 525 frames;
    - stage36_long: 1298 frames, the cart, over the whole 2600 frames;
    - stage21: 549 frames, drops;
    - stage16c: 909 frames, Claude + AI Celine casting.
    - Open: stage14c (see the hand-over).
- **Debug additions**:
  - `SS_BATTLE_GOD=1` refills the standing party's HP every frame (for sweeps).
  - `SS_BATTLE_STRONG=H:A:D` adds a defence factor.
  - `tools/battle_sweep.py --god`.
- Sweep pass 2 (god mode, HP:ATK:DEF x20:30:4): see the hand-over at the top.
- Particle kinds 4 / 5 (particle.cpp, 0x8005ee08 / 0x8005f128) are ported, not yet checked.

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
