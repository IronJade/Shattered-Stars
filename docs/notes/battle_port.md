# Battle port notes (engine/src/game/battle)

Notes from the battle worker. The function and global rows are merged into `docs/catalog`. Each ported
function lists its engine file in `port`, and partial ports are
marked "[port partial: ...]". This file is about the port itself: what it does differently from the
console, how it was checked, and what is still missing.

## Layout

| File | Contents |
|---|---|
| `battle.h`, `battle_program.cpp` | program entry / run / main loop / exit, BattleGlobals (addresses in comments), tasks, OT layers, end checks, victory / defeat |
| `battle_debug.{h,cpp}` | `--battle ENC[:STAGE]`, `--battle-end FRAMES[:OUTCOME]`, `SS_BATTLE_RAM`, `SS_BATTLE_STRONG`, `SS_BATTLE_POKE`, `SS_BATTLE_RNG`, `SS_BATTLE_DUMP`, `SS_BATTLE_CAM`, `SS_BATTLE_BOXES`, `SS_BATTLE_TICKS` |
| `loaders.{h,cpp}` | the 12 load-list handlers, sprite banks, inventory check, the field reload list + preloads (handoffs 0xc / 10) |
| `stage.{h,cpp}` | stage command lists (ops 1-17), ground height, `stage_draw_and_animate`, `model_render` |
| `render.{h,cpp}` | ViewState, trig / sqrt / ratan2 copies, GTE setup, `camera_compute_view`, lights, `obj_set_matrix` |
| `camera.{h,cpp}` | Camera task, all 17 modes (setups and updates), motion model, schedule / focus helpers |
| `bobj.{h,cpp}` | BObj, AnimSet, TexCache, `bobj_anim_draw`, shadows |
| `sprite.{h,cpp}` | Sprite pool (296), spawns, effect makers, the boot `rng_between` family |
| `unit.{h,cpp}` | Stats, Collider, Unit, EnemyUnit / PartyUnit declarations, unit basics |
| `unit_move.{h,cpp}` | the swept-box path probe, approach points, path steps, vector helpers |
| `combat.{h,cpp}` | hit boxes, `unit_attack_targets` pipeline, HP / MP, reactions, knockback, vanish, damage popups, shake |
| `party.cpp`, `party_states.cpp` | party loader and member construction; PartyUnit state machine |
| `enemy_ai.cpp`, `encounter.cpp` | EnemyUnit state machine and AI; formation / stats loading |
| `input.{h,cpp}` | `input_update` / `input_dispatch`, target select, free run, HUD gauge and range panel |
| `director.cpp` | `battle_frame_director`, the off-screen target arrow, stage colour save / restore |
| `window.{h,cpp}` | the window base (BWindow: an arena and a 1-entry OT per window), frames, the 8x8 UI font, the #1980 proportional text renderer and its 64-row line buffer |
| `hud.{h,cpp}` | the top status bar (BattleHud), the debug window, the bottom message banner (`battle_message_show`) |
| `result.{h,cpp}` | #1981: the result screen (EXP / Fol, drops, level-ups and learning, the defeat / escape messages), `battle_fade_task` |
| `voice.{h,cpp}` | the voice picks that draw from the RNG (`voice_pick_victory`, `voice_play_result`); the clips are not ported |
| `shot.{h,cpp}` | the Shot pool (160 effect / projectile actors): spawn / kill / fade / draw helpers, the callback kinds 0x21 / 0x33, kind 1 (stun stars) and kind 0x11 (ring of textured quads) |
| `particle.{h,cpp}` | the Particle pool (200): spawners for kinds 1-5; kinds 1-3 drawn with the GTE |
| `fx_draw.{h,cpp}` | #1984 3-D effect drawers: `draw_vortex`, the lens flares (`lens_flare_draw`, `lens_flare_draw_small`, flare ring / polygon), `project_to_screen` |
| `killer.{h,cpp}` | killer-move helpers shared by the modules: afterimages and ghosts (#1984), `pc_bind_shot`, `pc_attack_hit`, charge level, the MP cost and `command_killer_move` |
| `pc_modules.{h,cpp}`, `pc_claude.cpp`, `pc_casters.cpp`, `pc_ernest.cpp` | the party-character modules: the factory used by `pc_construct_slot`, byte access to each module's data, Claude's module (#2131), the casters' small modules (Rena #2135, Celine #2137, Leon #2141, Noel #2157), Ernest's (#2151) |
| `bsfx.{h,cpp}` | battle sound front end (8 channels, 3-D pan, sound-test record) |
| `vram.{h,cpp}` | CLUT / texture upload helpers |

## Deviations from the console (deliberate)

- **Overlays.** All code overlays are native: #1984 blocks, #1981, the party-character modules, and the
  encounter packs' enemy code. Only data is read from the disc.
  - Monsters whose class lives in an encounter pack (the "Module" family) run the EnemyUnit base
    behaviour. This is logged once per monster type.
  - Ported party-character modules: Claude (#2131 / #2133, `pc_claude.cpp`), Rena, Celine, Leon
    and Noel (`pc_casters.cpp`) and Ernest (#2151, `pc_ernest.cpp`). Bowman, Dias, Precis, Ashton,
    Opera and Chisato use the PartyUnit base, which is logged once. Normal attacks need nothing from the
    module. Killer moves (slots 35-37, 42, 45) do. The ported module is a native subclass. Its data
    tables are read from the module image at its slot-0 link address (0x801ee000), whatever slot
    the character is in.
- **Synchronous loads.** Loads are synchronous: the party loader, the field preloads, and the intro
  voice banks. The bload queue, spell-data cache and voice clips are not ported.
- **Heap budget.** `battle_queue_field_preloads` uses only the 0xa0000 cap, not "free heap - 0x4000".
  The field also assumes the host has room for every preload. On disc 1 the list is about 430 KB, so all
  of it is kept and the field music resumes.
- **Arenas.** Pool A's 62000-byte PS1 budget is emulated through `pool_a_bytes_used()`, counted in PS1
  primitive sizes. The host arenas are doubled.
- **Overlay data.** The code of #1984 and #1981 is native, but their tables are read from the
  images (`ovl1984_data` / `ovl1981_data`): `battle_data` reads #2129, whose bytes at
  0x80071xxx-0x80088xxx the overlays replace at run time.
- **Model buffers at state 4.** `battle_free_party_models` keeps the buffers until
  `battle_free_all`. Only its visible effect is reproduced: the frame loop stops servicing
  g_special_obj0.
- **HUD disc notices.** `hud_draw` shows a notice when the disc lid is open or a read fails (strings
  0xc / 0xd) and pauses the music. The host disc never does either, so only the sound-test notice
  (string 0x10) is ported.
- **Probe stack garbage.** `unit_probe_path` returns stack garbage for x/z when it reaches the wall code
  with vx == 0. The port returns the unit's own x/z there.
- **Null owner flag.** `sprite_free` on the console dereferences a null owner flag. The port skips it.
  The input block's target marker owns its own flag byte, where the console uses the low byte of the
  pointer.
- **Scratchpad and GTE.** The GTE is used where the console's results depend on it (projections,
  model_render). The probe's outer products and the distance helpers are computed in C with the same
  16-bit input truncation and 32-bit wrap.

## Verification

- **Reference run.** A PCSX-Redux run of the original was recorded. It covers New Game as Claude, the
  field state poked to a forced battle on map 7, and Cross pressed every 45 vsyncs from vsync 3810.
  Captures were taken every 10 vsyncs, with RAM dumps at `field_exit` and at `battle_main_loop`.
  - The fight is encounter 2 (two Lizardaxes) on stage 3. That is a random encounter of map 7's group
    1, not the encounter the script asked for. The poke wrote game_state +0x99, which does nothing; the
    field takes the encounter from game_state +0x264 (s16). The script now pokes +0x264.
  - Every comparison below still holds, because the port started the same fight. The port reads the
    encounter and stage from the field's setup block in the dump.
  - Unit +0x12a (action kind) is uninitialised heap in the original until it is first set. Keep that
    in mind when comparing RAM.
- **Port run.** The same battle is started with
  `SS_BATTLE_RAM=<field_exit dump> --battle 2:3 --input <Cross every 45 vblanks from 5>`. The dump
  supplies handoffs 2/3/4/6, the field's setup block and the RNG state. The RNG state goes through
  `rng_restore`, a small hook in `boot/rng`.
- **Clock alignment (read this before comparing).** The original's g_battle_frames (0x8006fb80)
  starts when its battle setup ends, so it runs D vsyncs behind the battle's vsync count. The port's
  battle_frames equals its vblank count. Compare ref b<n> with port vblank n-D.
  - D is not a constant. It depends on how long the original's setup took (CD loads), which differs
    between references. Across the enemy agent's 25 references the offsets fall into two groups:
    D = 5/6 (encounters 10, 19, 34, 99, 107, 202, 205, 286, 320, 457) and D = 0..2 (encounters 2, 35,
    47, 285, 314, 326, 412, 420).
  - The setup's vsync phase also sets the tick pattern of the first frames. Encounter 10 runs 2, 2, 2
    ... from b5 (g_battle_frames even). Encounter 285 has a 2-tick frame at b6, then a 1-tick frame at
    b7 (g_battle_frames odd). The 190-tick intro motion (camera_motion_step integrates speed with
    frame_ticks) therefore ends on a different battle frame: step 0 -> 1 at frames 154 for
    encounter 10 and 149 for 285. The port does it at frame 154 in both.
  - A fixed shift in the port would fix one group and break the other, so the port's own clock
    stays as it is (2, 2, 2, ...).
  - **Reproducing the 1-tick start (`SS_BATTLE_TICKS`).** For a reference whose g_battle_frames is
    odd at its samples (the 2, 1, 2, 2, ... start; `BW_DENSE=0:12` shows it), run the port with
    `SS_BATTLE_TICKS=1`. Its first frame then waits one vblank, so frame 1 runs with 1 tick like the
    original's, and the intro camera, the animations' cel phase and every timer follow the
    original. Then compare at D = 6 at odd samples and 7 at even ones (port frame = the sample's
    g_battle_frames). Shift the input presses to port vblank = ref vsync - battle_main_loop vsync
    - 5. In general, port press = ref press (vsyncs since battle_main_loop) - (D_even - 2).
    - Encounter 35 (two Carlaeagles, Claude, Cross every 45 vsyncs; `ref/enc35`, `ref/enc35e`):
      without it, the best plain offset (D = 2) keeps the RNG equal only to b460. At b466 Claude's
      slash then hits an eagle in the original and misses in the port: the eagles' walk cels (and
      so their body boxes) run half a cycle apart, because the intro is 5 ticks shorter in the
      original's battle frames.
    - With `SS_BATTLE_TICKS=1` the RNG totals are equal at every sample through the battle's exit
      at b1048. Unit state is equal in 925 of 930 samples; the other 5 are HP changes caught one
      frame apart by the sampling. This includes Claude's air attack (state 11) at b577 and the
      party's defeat at b807. The frames match from b160 on. The fade-in before that is the
      unported start wipe.
  - **What a Redux sample shows.** Breakpoints on the loop top, the camera update, the VSync(2)
    call and the end-of-frame tick (encounter 10) give this order: the setup ends after b5; frame 1
    runs and waits; after b6 its tick is added (frames 2), and frame 2 runs to its VSync(2) before
    b7. From then on VSync(2) returns right after each even vblank, the tick is added, and the next
    frame runs and waits. explore.lua's GPU::Vsync callback runs at the vblank, before VSync
    returns. So samples b(2k+1) and b(2k+2) both show the same finished frame with its in-frame
    g_battle_frames value. That value is the port frame to compare with: the port's `[battle]` lines
    are printed at the end of each frame, before its tick. D is therefore b - frames: 5 at odd
    samples and 6 at even ones for encounter 10. "Step 1 at b159, frames 154" means the camera
    changed step in frame 154, the same frame as the port's. The camera's remaining and speed fields
    equal the port's sample for sample (b6 = port frame 0, b7 and b8 = frame 2, ...). A heavy frame
    can still be running at the odd sample, so prefer even samples for RNG totals.
  - `SS_BATTLE_CAM=1` prints the port's camera and clock fields once a frame: battle_frames,
    frame_ticks, state, flags, camera mode / step / remaining / speed / ticks. These are the same
    fields as explore.lua's `BW_DENSE=LO:HI`.
- **Result.** At every checked frame (100 to 800), port frame n-2 shows the same scene as ref b<n>: the
  intro fly-over, the camera cuts and the units' positions and animations. Damage numbers also match
  (10 dealt; 51, 52 and 49 taken), and Claude's KO falls on the same frame. The remaining pixel
  differences are the stage's dithering. (Captures were kept in the session scratchpad.)
- **HUD.** In the same run the HUD appears on the same frame as the reference (b330). Both layouts
  match: the normal bar (name, HP / MP digits and gauges, cursor, item / move icons) and the
  target-select bar ("Lizardaxe", Claude 130/130 and later 79/130, the L1 / R1 buttons, "Phase Gun"
  in the #1980 font). Of the opaque HUD pixels (glyphs, frames, icons), 0.1-2 % differ. The rest of
  the band is the translucent backdrop over the stage's dither. Below the HUD the frames are identical
  to the run without it.
- **Result screen (victory).** A second Redux reference: the same fight with Claude's slot poked
  as `SS_BATTLE_STRONG` does (HP 1300, ATK 80) right after `field_exit`, with rng_next calls
  counted. The RNG draw counts match at every 10 vsyncs from the battle start to the fade (335
  draws), including `voice_pick_victory` at the kill and the result screen's drops, level-up
  and result-voice draws. The pages match frame for frame (port n-2 against b<n>):
  "Experience Points 20 / Fol 36", "Necklace obtained.", "Claude has increased. / 1 skill points
  obtained.", the scroll-away, the shrink and the fade. HP 1288 / MP 25 after the level-up match.
- **Result screen (defeat).** The forced_1_3 reference (also encounter 2, see above): the "Destroyed..." message (string 3),
  its timing and the fade match. The battle exits at vblank 1048; the original switches to the
  field between b1060 and b1070.
- **Banner.** `battle_message_banner` (test_battle.cpp) checks the banner: centred, 14 ticks up to y
  0xc0, 61 held, 14 down, and a queued message following it. No banner appears in the reference fight.
- **Field round trip.** `--map 10` with left/right walking reaches a random battle at vblank ~1915. The
  game returns to the field with the outcome in config+0x178, handoffs 0xc / 10, and the map music
  resumed (checked with a WAV recording).
- **Victory path.** `SS_BATTLE_STRONG=1 --battle 2:3` with Cross mashing kills both enemies through the
  dying animations, then victory (outcome 0) and exit.
- **Killer move (Phase Gun).** A third Redux reference: the same poked fight with L1 pressed every 45
  vsyncs, so Claude uses his starting move, Phase Gun, again and again (`ref/phasegun_2_3`).
  - The RNG draw counts match at every 10 vsyncs from the battle start to b1600 (708 draws). That
    covers the move's sprite jitter and its 48 `vec_random_around` streaks per hit.
  - The frames match (port n-2 against b<n>): the charge, the blast, the streaks, the damage
    numbers (701, 688) and the result pages. The remaining differences are dither. Measured on 4x4
    block means, which cancel the dither, the result screen differs in 3.6 % of blocks, all in the
    stage background.
  - The exit matches too (`ref/phasegun_exit`: L1 for the fight, then Cross). The RNG counts
    are equal up to the exit at b1236, and the handoffs at the exit differ only in the 12 known
    bytes. Handoff 3 (party, MP, the move's use counter) is byte-identical.
  - The sprite texture cache runs out of room at the same moment. A Redux breakpoint on the
    failure path of `texcache_alloc` (0x8004516c) fires 8 times a frame at b582-586; the port
    logs its first failure at port frame 579-580. (The console's "NO CASH" text goes to an empty
    `debug_print`.)
- **Killer moves (Sword Bomber).** A fourth reference (`ref/moves_9_5`): the same fight, with
  `SS_BATTLE_POKE` / `BW_POKE` giving Claude move byte 10 (Sword Bomber, move 9) on L1, byte 6
  on R1 and 200 MP.
  - The RNG counts match at every 10 vsyncs to b1600, through the victory and the result
    screen at b879.
  - The frames show the same vortex funnel (`draw_vortex`), the small lens flare
    (`lens_flare_draw_small`), the kind-0x11 fire rings and the damage numbers.

- **Character modules and the party AI (the recipe).**
  - Each module was checked one killer move at a time against a forced encounter-2 fight.
    The Redux side runs `scratchpad/battle/queue_runs.sh CHAR NAME BYTE...`: run_victory.sh
    with Claude's slot 0 turned into character CHAR (handoff 3 +0 = id), MP 200 and L1 = the
    move byte (record +0xcc), with Cross every 45 vsyncs and L1 every 90.
  - Pokes need hex values (`0x8/1`); explore.lua skips a plain `8/1` silently.
  - The port side is `scratchpad/battle/pcrun.sh CHAR BYTE REF OUT`. It runs `SS_BATTLE_TICKS=1`
    with the same pokes through `SS_BATTLE_POKE` (cross at 45i, l1 at 20 + 90i), then rngcmp
    (CMP_D=7), unitcmp (D 7, the port's SS_ENEMY_TRACE) and framecmp (FRAMECMP_D=7).
  - A module passes when the RNG totals are equal to the battle exit and the units differ
    only where an HP change is sampled a frame apart (the port's trace prints from the first
    enemy's think, Redux at the end of the frame).
  - Verified this way: Claude (moves 0, 2, 5, 9), Ernest (all 8), Bowman (all 10), Dias (all
    10).
  - The party AI used a second member in slot 1. `SS_BATTLE_COPY` / explore.lua's `BW_COPY`
    copy slot 0's stats and record to slot 1, then pokes set the id, class (+3), MP and
    learned spells (record +0x3c..). See `scratchpad/battle/run_rena_ai.sh`,
    `run_fighter_ai.sh` and `run_caster_ai.sh`.
  - Rena healing a hurt Claude, Bowman fighting and Celine keeping away all match to the
    exit.

## Not ported yet (in rough priority order)

1. **Leftovers of the HUD and the result screen.**
   - The voice clips of the victory and the level-up pages (their RNG draws are ported).
   - The start burst (`battle_start_burst_create` 0x8003c02c, the white radial wipe).
   - The HUD greys a killer move by its MP cost (PartyUnit vtable [35]). The cost comes from the
     character modules, so it is 0 for every character but Claude.
2. **Killer moves.** The other eleven character modules, and the sound-handle duck / fade /
   pitch calls (no clip handles are tracked yet).
3. **Spells and items.** `spell_cast`, the SpellCast tasks and spell data, `unit_begin_cast`, enemy
   `try_cast`, and `item_use_battle`. The ring menu and list menus, the member select (input mode 1), and
   escape.
4. **Shot and Particle pools.** The pools, kinds 1 / 0x11 / 0x21 / 0x33 of Shot and kinds 1-3 of
   Particle are ported. The other Shot kinds (thrown weapons, the module kinds) log once and are
   removed. Particle kinds 4 / 5 (shards, confetti) spawn with their RNG draws but are not drawn.
5. **Party AI.** Fighter / healer / caster AI for the non-controlled members.
   `pc_update_effects` beyond `unit_update_status_effects`.
6. **Voices.** Voice clips and voice tables, including `voice_pick_victory`, which draws from the RNG
   at victory, and the opening voices.
7. **Stage extras.** Stage code blocks and stage-8 lightning. The stage lens flare is ported
   (`fx_draw.cpp`) but has not been compared yet (Shingo Forest has none).

## Hooks outside engine/src/game/battle

- **system.cpp.** `run_program`'s Battle case calls `battle_entry` unless `SS_BATTLE_STUB` is set.
- **main.cpp.**
  - `--battle ENC[:STAGE]` and `--battle-end FRAMES[:OUTCOME]`.
  - Without `--battle`, `--battle-end` applies to every battle, including ones the field starts.
- **boot/rng.{h,cpp}.** `rng_restore(table, index)`, a debug-only state restore used by `SS_BATTLE_RAM`,
  and `rng_draws()`, a debug-only draw counter (`SS_BATTLE_RNG`).
- **CMakeLists.txt.** The battle sources are in `ss_game`, and `tests/test_battle.cpp` is in
  `ss_game_tests`.
