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
  Committed in b906414. Builds; not yet exercised by a battle that spawns Shots.

## In progress (uncommitted, all build)

- `battle/killer.{h,cpp}` (new): Afterimage (0x80075080..), Ghost (0x800756e0..),
  pc_attack_anim_step, pc_bind_shot, pc_move_charge_level, pc_camera_frame_target,
  pc_attack_hit, unit_mp_cost, command_killer_move, `Unit::release_effects` (sets the
  afterimages' ending flag).
- `battle/particle.{h,cpp}` (new): Particle pool (200), kinds 1-3 drawn (homing line,
  spark line, ring); 4 / 5 spawn (with their RNG draw) but are removed unseen.
- `battle/pc_modules.h` (new) and `battle/pc_claude.cpp` (new, NOT yet in CMake, not
  yet compiled): Claude's module (all 11 moves, 8 Shot callbacks, slots 23/25/35/36/37/45).
  Still needed before it builds: `pc_modules.cpp` with `pc_module_create` (factory used by
  pc_construct_slot), `pc_module_u8` (reads the code entry's block 0, base 0x801ee000),
  `pc_claude_create`; `effect_spawn_sparks_at/on` (Shot kind 0x11 spawners, 0x80063824 /
  0x800639ac), `vec_random_around` (0x80048f60: rng_range(0x1000) then rng_spread(0x400)),
  `unit_play_anim` (0x80053ee0), `pc_camera_behind_start` (0x80037c2c),
  `vec_velocity_toward_xz` (0x80048e28) if missing.
- `battle/party.cpp`: phases 1-4 (killer_move_data_entry 0x80083db0, Claude's in-RAM
  upgrades) fill alt_layout/anims/frames/sheet, attack_data, move_data, move_parts and the
  CLUT rows 0x1cf + slot*4 + phase; the ctor allocates the 5 model sets.
- `battle/party_states.cpp`: begin_action's killer branch (MP cost slot 36, use counter).
- `battle/input.cpp`: command_killer_move from the target select, the chain and free run;
  the counter / chain ghost.
- `battle/unit.h`: PartyUnit move_parts / attack_data / move_data / v2d8 / v2da; slots 36,
  39, 45; effects typed `Afterimage*`.
- `battle/battle_program.cpp`: particle pool create / update (after sprites) / destroy.
- `engine/CMakeLists.txt`: killer.cpp, particle.cpp (pc_claude.cpp / pc_modules.cpp to add).

## Next steps

1. Write `pc_modules.cpp` and the missing helpers listed above; add pc_claude.cpp and
   pc_modules.cpp to CMake; pc_construct_slot uses `pc_module_create(character)`.
2. Build; test Phase Gun (Claude's starting move, byte 3 = state index 2) in the forced
   fight: a Redux run with L1 presses (`BW_MASH` on l1) and the STRONG poke, RAM dumps and
   rng counts (`run_victory.sh` pattern), against the port's `SS_BATTLE_RNG` trace.
3. Shot kind 0x11 handler (0x80058b78, GTE ring of textured quads; asm needed, the decomp
   is garbled) and draw_vortex / lens_flare_draw_small for move 9.
4. The other modules (casters' small ones first, then the fighters), each verified.
5. Step 4: spells and items.
6. input_free_run's analog branch: use `pad_stick()` (boot pad, 0x80013cb8, 7f41c89).

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
