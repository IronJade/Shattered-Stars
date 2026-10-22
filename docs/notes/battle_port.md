# Battle port notes (engine/src/game/battle)

Notes from the battle worker. The function and global rows were merged into `docs/catalog` (332 rows,
319 functions and 13 globals). Each ported function lists its engine file in `port`, and partial ports are
marked "[port partial: ...]". This file is about the port itself: what it does differently from the
console, how it was checked, and what is still missing.

## Layout

| File | Contents |
|---|---|
| `battle.h`, `battle_program.cpp` | program entry / run / main loop / exit, BattleGlobals (addresses in comments), tasks, OT layers, end checks, victory / defeat |
| `battle_debug.{h,cpp}` | `--battle ENC[:STAGE]`, `--battle-end FRAMES[:OUTCOME]`, `SS_BATTLE_RAM`, `SS_BATTLE_STRONG` |
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
| `bsfx.{h,cpp}` | battle sound front end (8 channels, 3-D pan, sound-test record) |
| `vram.{h,cpp}` | CLUT / texture upload helpers |

## Deviations from the console (deliberate)

- **Overlays.** All code overlays are native: #1984 blocks, #1981, the party-character modules, and the
  encounter packs' enemy code. Only data is read from the disc.
  - Monsters whose class lives in an encounter pack (the "Module" family) run the EnemyUnit base
    behaviour. This is logged once per monster type.
  - The party-character modules are not ported, so Claude etc. use the PartyUnit base. Normal attacks
    need nothing from the module. Killer moves (slots 35-37, 42, 45) do.
- **Synchronous loads.** Loads are synchronous: the party loader, the field preloads, and the intro
  voice banks. The bload queue, spell-data cache and voice clips are not ported.
- **Heap budget.** `battle_queue_field_preloads` uses only the 0xa0000 cap, not "free heap - 0x4000".
  The field also assumes the host has room for every preload. On disc 1 the list is about 430 KB, so all
  of it is kept and the field music resumes.
- **Arenas.** Pool A's 62000-byte PS1 budget is emulated through `pool_a_bytes_used()`, counted in PS1
  primitive sizes. The host arenas are doubled.
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
  field state poked to a forced battle (encounter 2, stage 3, two Lizardaxes), and Cross pressed every 45
  vsyncs from vsync 3810. Captures were taken every 10 vsyncs, with RAM dumps at `field_exit` and at
  `battle_main_loop`.
- **Port run.** The same battle is started with
  `SS_BATTLE_RAM=<field_exit dump> --battle 2:3 --input <Cross every 45 vblanks from 5>`. The dump
  supplies handoffs 2/3/4/6, the field's setup block and the RNG state. The RNG state goes through
  `rng_restore`, a small hook in `boot/rng`.
- **Result.** At every checked frame (100 to 800), port frame n-2 shows the same scene as ref b<n>: the
  intro fly-over, the camera cuts and the units' positions and animations. Damage numbers also match
  (10 dealt; 51, 52 and 49 taken), and Claude's KO falls on the same frame. The remaining pixel
  differences are the missing top HUD window and dithering. (The captures were kept in the session
  scratchpad, not in the repository.)
- **Field round trip.** `--map 10` with left/right walking reaches a random battle at vblank ~1915. The
  game returns to the field with the outcome in config+0x178, handoffs 0xc / 10, and the map music
  resumed (checked with a WAV recording).
- **Victory path.** `SS_BATTLE_STRONG=1 --battle 2:3` with Cross mashing kills both enemies through the
  dying animations, then victory (outcome 0) and exit.

## Not ported yet (in rough priority order)

1. **#1981 and the HUD.**
   - The result screen: EXP / Fol award, drops, level-ups, learn tables, write-back to handoffs 2/3/4/6.
   - `battle_fade_task`, and `battle_free_party_models`.
   - The victory jingle. The "Destroyed" banner (`battle_message_show`) is only logged.
   - The HUD (`hud_init` / `hud_draw`, 6 KB), with the window system and the #1980 text renderer
     (`window_*`, `text_*`, 64-row buffer at VRAM 0x3c0,0x1c0).
2. **Killer moves.** The party loader phases 1-4 (killer-move data) and the character modules.
   Command 2/3 logs a TODO.
3. **Spells and items.** `spell_cast`, the SpellCast tasks and spell data, `unit_begin_cast`, enemy
   `try_cast`, and `item_use_battle`. The ring menu and list menus, the member select (input mode 1), and
   escape.
4. **Shot and Particle pools.** Projectiles, the stun stars (`unit_apply_stun`), thrown weapons, and
   confetti / debris particles.
5. **Party AI.** Fighter / healer / caster AI for the non-controlled members.
   `pc_update_effects` beyond `unit_update_status_effects`.
6. **Voices.** Voice clips and voice tables, including `voice_pick_victory`, which draws from the RNG
   at victory, and the opening voices.
7. **Stage extras.** Stage code blocks, stage-8 lightning, and the lens flare.

## Hooks outside engine/src/game/battle

- **system.cpp.** `run_program`'s Battle case calls `battle_entry` unless `SS_BATTLE_STUB` is set.
- **main.cpp.**
  - `--battle ENC[:STAGE]` and `--battle-end FRAMES[:OUTCOME]`.
  - Without `--battle`, `--battle-end` applies to every battle, including ones the field starts.
- **boot/rng.{h,cpp}.** `rng_restore(table, index)`, a debug-only state restore used by `SS_BATTLE_RAM`.
- **CMakeLists.txt.** The battle sources are in `ss_game`, and `tests/test_battle.cpp` is in
  `ss_game_tests`.
