# Field program part 2: 0x80040c2c-0x80053e8c

This range covers the field main object, its frame loop and state machine, and the subsystem
classes it creates: Screen, ActorSystem, LoadQueue, ScreenFade, BattleTransition and TextPanel.
It also holds the map-container parser and the music, party and sprite-pack helpers. Function
rows are in `part2.csv` (144 rows). Addresses are field-program addresses (archive entry
#3100 at 0x8002f810).

Struct names used in signatures: `Field`, `Screen`, `ScreenBuffer`, `ActorSystem`, `Actor`,
`LoadQueue`, `LoadCallback`, `ScreenFade`, `BattleTransition`, `TextPanel`, `VsyncCounter`,
`SpriteGroup`, `MapScene`.

Conventions: g++ 2.x vtables are arrays of 8-byte entries `{s16 delta, s16 0, u32 fn}`. A
virtual call through entry +N calls `fn@(N+4)(this + delta@N)`. "Task prio n" means the
object is registered in the field task list with priority n (see below).

---------------------------------------------------------------------------------------------

## Classes and objects at a glance

| Global | Class | Size | vtable | Ctor | Task prio | Role |
|---|---|---|---|---|---|---|
| 0x8002f8dc / 0x80075284 | Field | 0x50 | 0x800730a4 (+8 field_run) | field_construct 0x8004880c | - | program object, state machine |
| 0x80075290 | Screen | 0x10c | none | screen_construct 0x80048154 | - | double buffer, OT, primitive arena |
| 0x80075294 | ActorSystem | 0x490 | 0x80072e4c (+8 actor_sys_update) | 0x80043674 | 2 | owns field_actors[194]; sprite VRAM cache; view matrix |
| 0x80075298 | ScreenFade | 0x98 | 0x80072f7c (+8 fade_update) | 0x80046910 | 1 | fades, wipes, iris, cross-fade |
| 0x8007529c/a0/a4 | TextPanel | 0x28 | 0x80072e5c (base Window 0x80074038) | inline in field_create_subsystems | 4 when open | text panels |
| 0x800752e4 | TextPanel[4] | 4 x 0x28 (new[], count at -8) | 0x80072e5c | inline in field_update | 4 | post-battle stat panels |
| 0x800752b0 | LoadQueue | 0x160 | 0x80072dfc (+8 load_queue_update) | 0x80043050 | 1 | asynchronous archive reads |
| +0x150 of it | LoadCallback | 8 | 0x80072e0c (+0x18 load_callback_done) | 0x80043640 | - | loader completion hook |
| 0x800752bc | VsyncCounter | 0xc | 0x80073084 (+0x18 vsync_counter_tick) | inline | pad vsync callback | frame-time measurement |
| 0x800752d0 | BattleTransition | 0x18 | 0x80072e84 (+8 battle_transition_update) | 0x80045c8c | 1 | encounter screen effect |
| 0x800752ac | ScriptCtx | 0x11cc | (FUN_800621e0) | other range | 0 | event-script VM |
| 0x80075288 | MsgSystem | 0x20c | (FUN_8005835c) | other range | 4 | message windows |
| 0x8007528c | BG renderer | 0x128 field / 0x5a0 world | in the 0x8007e000 overlay | FUN_8008672c / FUN_80088814 | ? | map background (prerendered BG + scene) |
| 0x800752b4 | ? | 0x17c | overlay FUN_8008e100 | field maps only | ? | overlay helper |
| 0x800752c4 | ? | 0x88 | overlay FUN_8007e000 | world map only | ? | overlay helper |
| 0x800752c0 | ? | 0x3c | 0x80073194 (FUN_800574e0) | other range | 0 | ? |
| 0x800752e0 | ? | 0x24 | (FUN_800313c8) | other range | 1 | sound or jingle controller (stopped before music loads) |
| 0x800752d8 | ? | 8 | (FUN_8005f5c8) | other range | 1 | used by actor_sys_draw when OT layers are active |
| 0x800752a8 | ? | 0x6c | (FUN_80055cd4) | other range | - | ? |
| 0x800752d4 | camp menu | 0x3c | FUN_80030470 | created in field_update | - | camp menu (state 3) |

Field-mode objects created by field_load_map (other range) live in the code overlay at
0x8007e000. That overlay is loaded by `field_load_code_overlay` from the map descriptor's
overlay entry. Normal maps and the world map use different overlays, reloaded when
field_flags+0x68 changes (tracked in 0x80075358).

---------------------------------------------------------------------------------------------

## Field frame flow

`overlay0_start` → `field_program_create` (0x800487e4) → `field_run` (vtable +8, 0x80048a84):

1. DrawSync; `field_bind_system` (System sub-objects; handoffs 2 config, 6 inventory, 3 party,
   4 characters); clears config+0x181/0x182; FUN_800134d8(0) (vsync hook off).
2. If there is no handoff 9 (first entry): FUN_8005ea4c / FUN_8005ea54 on a local, then spin
   while FUN_80018f20(sound) != 0.
3. `field_bind_state_block` (handoff 9: script globals = field_flags at +0x180, game_state at
   +0x220, script flags at +0x8c0).
4. If 0x80076197 is set and the inserted disc is wrong: `field_prompt_disc_change`.
5. **Returning from another program** (field_flags+0x24 != 0): sets 0x80076278 (post-battle
   checks). Handoff 0xf → System+0x5ac0 (sound state). R = config+0x178. Re-allocates handoffs
   0xb, 0x10 and 0xd and copies 0xc (0x22 bytes) to defragment the heap. Then
   `field_restore_preloads` (handoff 10 blob → preload cache) and `party_restore_slot_ids`.
6. Item table: handoff 0x11, or a new 0xc28-byte table (0x80076228).
7. Battle setup block (0xc0, 0x800752b8); music sequence buffer (0x4800, 0x80075734);
   `field_create_subsystems`. When returning, FUN_8003199c(0x800752e0).
8. `field_main_loop` (never returns; the program ends through boot_run_main_program).

`field_main_loop` (0x80049bfc):

```
field_load_system_data()                 // entry #3110: strings, window/icon textures, 2 CLUTs
screen_alloc_arena(); FUN_8006ff3c(); field_setup_screen(); screen_set_clear_colour(0,0,0)
2 x { screen_flip; VSync(0); PutDrawEnv; PutDispEnv }
pad_set_port_mask(3); pad_set_repeat_delay(12); load_queue_set_streaming(q, 0)
field_load_map(f, game_state.map(+0x1a), returning, returning)
ResetRCnt(RCntCNT2); f->present = 1
loop forever:
  if no sub-program (0x800752c8 == 0): screen_flip(screen)       // swap buffers, rewind arena, ClearOTagR
  pad_set_option(pad, sub==0)
  screen_clear_ot(screen)                                         // link 0x82f-entry forward OT
  pads: current/pressed/repeat for ports 0 and 1 → 0x80075748..0x8007575c; pad_clear both
  analog: 0x800762fc = FUN_80013cb8(pad, 0, 0x80076300..0c)
  disc_error = !battle_transition && (FUN_80011bbc(loader) >= 300 || FUN_80011be0(loader))
  if !disc_error:
      if msg window 5 is up and not closing (state not -1, 9 or 10): close it; resume music
      field_update(f)                           // state machine below
      field_update_encounter_streaming(f)
  else:
      pause music; msg_window_open(msg,5,8,30,1, strings[lid open ? 1 : 0],5,1); dim screen
      (fade_draw_grey(fade, 0x20, semi, abr 2, OT 0x80c))
  if no sub-program:
      task_run_all()   (FUN_800561a4)            // see the task list
      if 0x800752e8: overlay 0x801f999c update
      DrawSync(0); VSync(0x8007572d)              // 0 → 60 Hz (field), 2 → 30 Hz (world map, battle intro)
      g_frame_ticks (0x8007531c) = vsync->frames (1..3); 0x80075320 = vsync->gpu_busy; reset counter
      if f->present: PutDispEnv(cur.disp); PutDrawEnv(cur.draw) else f->present = 1
      if 0x80076279: overlay 0x801ead88
      if draw_enabled (0x8007572c): screen_compact_ot(); DrawOTag(cur.ot)
  else (sub-program 0x800752c8, code at 0x801eXXXX):
      overlay 0x801eb6f0 decides whether to keep running; 0x801ea1d0 draws; 0x801ebac8 / 0x801e9000
      update; when it ends the object is deleted (vtable at +0x4c, entry +8) and the field resumes
  ResetRCnt(RCntCNT2)
```

Order inside a frame: the CPU builds buffer N while the GPU still runs buffer N-1's
DrawOTag. DrawSync then waits for it, VSync paces the frame, PutDispEnv shows N-1 and
PutDrawEnv targets N, and DrawOTag(N) starts.

**Time step:** every update that animates uses `g_frame_ticks` (0x8007531c), the number of
vblanks since the previous frame (1-3), measured by `vsync_counter_tick`. The port must
reproduce this variable-step behaviour, or fix it at 1 for 60 Hz and 2 for 30 Hz.

### Task list (other range, 0x80056178-0x80056374; drives most subsystems)

- Table: 0x8007591c `Task *obj[24]`, 0x8007597c `u8 prio[24]`, 0x80075994 `u16 count`.
- `FUN_80056270(obj, prio)` registers an object; `FUN_80056304(obj)` unregisters it;
  `FUN_80056178` clears the table.
- `task_run_all` (FUN_800561a4): for prio 0..4, for each task with that priority, calls the
  vtable +8 method.
- Registered priorities:
  - 0: ScriptCtx (FUN_80062260), object 0x3c (0x800752c0).
  - 1: LoadQueue, ScreenFade, BattleTransition, object 0x24 (0x800752e0), object 8 (0x800752d8).
  - 2: ActorSystem, per-map objects (FUN_8005ee9c).
  - 4: MsgSystem, open TextPanels.
- So each frame the order is: VM → CD queue / fades / sound → actors (update, sort, draw) →
  message windows and panels. `field_update` and `field_update_encounter_streaming` run before
  all of these.

### `field_update` state machine (Field+0x14)

| State | Meaning | Exit |
|---|---|---|
| 0 | Normal play. Leader on the save-point actor (field_actors[0x80]) → f+0x38, sfx 0xc on entry. Poison damage every 400 distance units (unindexed 0x8004c250, green flash). Post-battle level-up display when returning from battle (entry 0xfc2, 4 TextPanels → 0xf). Menu button (config[2]) → camp menu → 3. World-map vehicle mount/dismount (0x9 / 0xc / 0x16). Random encounters: walking distance field_flags+0x18 vs threshold +0x1c (rate +0x25, modifiers game_state+0x266/0x267, field_flags+0x84, ability bits 0x80076190) → field_prepare_encounter preloads; when ready → field_start_battle → 4 | 1, 3, 4, 9, 0xc, 0xf, 0x16, 0x1b |
| 1 | Wait for fades, then field_load_map(f, f+0x24, 0, 0) | 0 |
| 2 / 4 | Wait for BattleTransition done (+0x10), then delete it; draw disabled | 5 |
| 3 | Camp menu: snapshot game_state (0x2a0) and script flags (0x170) into handoff 0xe (0x440). Run the menu (0x800752d4, FUN_800304f0 and friends). Result 1 → load game (restore from 0xe, field_load_map(map, 1, 1)). 2 → field_begin_game_over. 3 → 0x21. 4/5 → side-load events. 6 → field_load_map(0x2c0) | 0, 6, 0x11, 0x1b, 0x21 |
| 5 | When #2129 (battle program) is loaded and the encounter preloads are complete: field_exit_to_program(f, 0) | - |
| 6 | After fades: game-over timer 0x21c; music fade 70 | 7 |
| 7 | Game-over sequence: actors fade out, then the logo from #3110 is drawn as an FT4 at OT 0x818 | 8 |
| 8 | When entry 1 (title) is loaded: full teardown, re-creates the System exactly like boot_main, boot_run_main_program(title) | - |
| 9-0xe | World-map vehicle mount/dismount animations (parked vehicle actor 0x80075394, music 0xa86) | 0 |
| 0xf / 0x10 | Post-battle stat-change windows per party member (stat snapshot 0x800761a0 vs party slots) | 0 |
| 0x11-0x15 | Vehicle music swap (0xa92) via side-load | 0 |
| 0x16-0x1a | Vehicle summon/dismiss (actor 0xf, music 0xa89 / 0xa8a) | 0 |
| 0x1b-0x20 | Helper-actor event (actor 0xe, sprite from table 0x800744ea[game_state+0x288]): walks in, gives the up-to-10 items queued at game_state+0x250 (inventory_add + message), leaves | 0 |
| 0x21 | Forced battle from the menu: stream encounter data; field_start_battle(f, 2) | 2 |

### How the field leaves

- **To battle** (#2129, 0x851): `field_start_battle` → states 4 → 5 → `field_exit_to_program(f, 0)`.
  Handoffs written:
  - 5: battle setup 0xc0, with pointers to the preloaded data.
  - 0xb: actor snapshot.
  - 0xd: copied.
  - 0xf: sound object FUN_8006f90c.
  - 0x10: blob 0x80075728.
  - 0x11: item table.
  - field_flags+0x24 = 1, so the next field_run knows it is returning.
- **To title** (#1): `field_begin_game_over` → states 6-8.
- **Script exit** (table A 0x2d): field_exit_to_program with the preloaded program.
- **Map change:** VM wait state 1 calls `fade_capture_screen` (cross-fade type 0x29), then
  `field_load_map` directly. Field state 1 does it after a fade.

---------------------------------------------------------------------------------------------

## Subsystems

### Field (main object, 0x50 bytes, vtable 0x800730a4)

vtable: +8 `field_run`, +0x10 FUN_8002f890, +0x20 FUN_80055d98. Base vtable 0x8002f8c4.
`field_destruct` (0x80048a50) is called directly.

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vtable* | 0x800730a4 |
| +0x0c, +0x10 | u32 | zeroed in the ctor; not used in this range |
| +0x14 | int | field_update state |
| +0x18 | int | state timer (counts down by g_frame_ticks) |
| +0x1c, +0x20 | int | vehicle hover/jump parameters |
| +0x24 | int | pending map for state 1 |
| +0x28 | u32 | last encounter rate seen by field_update_encounter_streaming |
| +0x2c | u32 | present flag (0 = skip PutDispEnv/PutDrawEnv for one frame) |
| +0x30 | int | random extra encounter distance (300-599 << 12) |
| +0x38 | int | leader touches the save point (enables Save in the menu) |
| +0x3c | int | saved controlled-actor index across menu/events |
| +0x40..+0x48 | int[3] | dismount target position |

Key functions: `field_create_subsystems` (creation order is the table above),
`field_destroy_subsystems` (reverse, plus per-map objects), `field_update`, `field_unload_map`,
`field_load_map_sections`.

### Screen (0x10c bytes, global 0x80075290)

Two `ScreenBuffer`s of 0x7c bytes:

| Off | Type | Meaning |
|---|---|---|
| +0x00 / +0x7c | DRAWENV | draw env of buffer 0 / 1 |
| +0x5c / +0xd8 | DISPENV | disp env of buffer 0 / 1 (disp0 shows buffer 1's image and vice versa) |
| +0x70 / +0xec | u32* | OT: 0x830 words, forward-linked; entry 0x82f → terminator 0x800756d0 |
| +0x74 / +0xf0 | u8* | primitive area of the buffer |
| +0x78 / +0xf4 | u32* | aux OT (16 entries) for immediate DrawOTag (cross-fade) |
| +0xf8 | ScreenBuffer* | current buffer |
| +0xfc | u8* | primitive cursor (every drawer appends here and writes it back) |
| +0x100 | u8* | primitive area end (not checked by the drawers in this range) |
| +0x104 | u8* | extra arena after both buffers (palette backups, etc.) |
| +0x108 | u8* | extra-arena cursor |

- Arena: one 0x40c40-byte allocation (0x80075a7c). Per buffer it holds 0x2100 bytes of OT
  plus aux OT, then 0x14520 bytes of primitives (field maps) or 0x1c520 (world map). The
  extra arena gets the rest: 0x14000 on field maps, 0x4000 on the world map.
- Layout from `field_setup_screen`:
  - buffer 0 draws at (0,0) and buffer 1 at (0,224), both 320x224;
  - display 320x240 with 8-line top and bottom borders (disp.screen y = 8, h = 224).
- `screen_flip` swaps the buffers and rewinds the cursors. `screen_clear_ot` relinks the OT.
  `screen_compact_ot` short-circuits empty entries before DrawOTag.
- **OT order:** the table is forward-linked and DrawOTag starts at entry 0, so higher indexes
  draw later (on top). Fixed slots used in this range:
  - 0x80c: disc-error dim tile, poison flash;
  - layer slot + 0x11: OT-layer splice point (slot ids in 0x800761b4);
  - 0x818: game-over logo;
  - 0x81e: transition background SPRTs;
  - 0x81f: transition FT4 tiles;
  - 0x820: fades, wipes and iris.

### ActorSystem (0x490 bytes, vtable 0x80072e4c, task prio 2, global 0x80075294)

ActorSystem owns `field_actors[194]` (0x80075360):

- 0..0x81: script actors.
- 0x80: the save point.
- 0xd: parked vehicle.
- 0xe: helper actor (also 0x80075398).
- 0xf: vehicle (0x8007539c).
- 0x82..0xc1: extra actors (effects; 0x80075568).

Actors are objects of an overlay-defined derived class:

- 0x3fc bytes on field maps (ctors FUN_8007e478/4b8/540);
- 0x44c bytes on the world map (FUN_80082d8c/dd4/e5c).

The vptr is at actor+0x3f8. Base-class code is in 0x8003d53c (delete), 0x8003d750 (init),
0x8003d95c (update and command interpreter) and 0x8003f948 (draw).

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vtable* | 0x80072e4c (only +8 = actor_sys_update) |
| +0x08 | u32 | current scene colour-cycle value; +0x0b u8 cycle timer |
| +0x0c..+0x0e | u8 | global actor brightness r,g,b; +0x0f u8 brightness-changed flag |
| +0x10 | SVECTOR | camera rotation (from scene +0xdc/+0xde/+0x30) |
| +0x18 | MATRIX | view matrix (rotation, plus translation = scene eye) |
| +0x38 | int | camera yaw = ratan2(eye.z-at.z, eye.x-at.x) & 0xfff |
| +0x3c, +0x3e | s16 | sprite cache A shelf cursor x,y (reset 0x141, 1) |
| +0x40 | {u32,u32}[32] | cache A secondary keys |
| +0x140 | RECT[32] | cache A VRAM rectangles |
| +0x240 | u32[32] | cache A primary keys (0 = free) |
| +0x2c0 | u8[32] | cache A lifetimes (decremented per frame) |
| +0x2e0 / +0x2e1 | u8 | cache A shelf height / next victim |
| +0x2e2, +0x2e4 | s16 | cache B cursor x,y (reset 0x141, 0x101) |
| +0x2e6.. | RECT[32] | cache B rectangles |
| +0x3e8 | u32[32] | cache B keys; +0x468 u8[32] lifetimes; +0x488/+0x489 shelf height / victim |
| +0x48a | u8 | per-frame flag |
| +0x48c | s16 | half-FOV angle ratan2(screen_w/2, H) |

- **Sprite VRAM cache** (allocators in unindexed 0x80044754 / 0x80044c8c):
  - shelf packing from x 0x141 to 0x1ff;
  - cache A rows y 1..0xff, cache B rows 0x101..0x1ff;
  - cache B also owns a 256-colour CLUT row 0x1f3+slot;
  - slots are reused round-robin when their lifetime reaches 0.
- `actor_sys_update` (per frame, when 0x80075355 is set):
  - If field_flags+0x81, resets cache B.
  - If the camera target (field_flags+0xe) is -1 on a normal map, calls the overlay
    FUN_800867d8(bg) (BG scroll update, also called by field_load_map after placing the leader).
  - Unless there is a disc error, for actors 0..0x81: `FUN_8003d95c(actor, player_input,
    is_camera_target)`. player_input is set when the slot is field_flags+0x10 and input is
    not locked (+0x26).
  - Then `actor_sys_draw`.
- `actor_sys_draw`:
  1. Ages the cache slots.
  2. Steps the scene colour cycle by g_frame_ticks (scene +0x98 count, +0x99 ping-pong,
     +0x9c entries `{u24 value, u8 frames}`).
  3. Collects existing actors, extras first.
  4. Calls each actor's vtable +8 method with argument 1.
  5. Sort key: depth +0x314, or -(+0x76) when field_flags+0x80 is set. Selection sort by
     ascending key, ties by slot.
  6. Draws with `FUN_8003f948(actor, 0, -1, 0)`. When OT layers are active (0x800761a8),
     actors whose +0x22 matches one of 4 layer ids (0x800761b4) are drawn into that layer's
     private OT (0x800761c4[i], 0x20c0-byte OTs). The layer is spliced into the main OT at
     index id + 0x11, then FUN_8005f814(0x800752d8) runs.
  7. If 0x800761e8 is set, a second pass `FUN_8003f948(actor, 1, ...)` in reverse order.
- `actor_sys_setup_view` (called after the scene loads and after menus):
  - SetGeomOffset(w/2, h/2) and SetGeomScreen(H) from the scene;
  - R = RotZ(roll) · RotX(rx) · RotY(ry) times the constant matrix at 0x80072e2c;
  - TransMatrix = -eye;
  - SetRotMatrix and SetTransMatrix;
  - stores eye and target << 12 in 0x80075a84..0x80075a98 (used for camera-relative input).

**Actor fields used in this range** (Actor = 0x3fc / 0x44c; position is 20.12 fixed point):

| Off | Meaning |
|---|---|
| +0x00/+0x04/+0x08 | x, y, z (<<12); +0x0c 4th word copied with them |
| +0x10/+0x12/+0x14 | animation id; frame; timer (actor_set_anim) |
| +0x16 | sprite/depth-bias field (0x7fff for attachments; 199 or the leader index for vehicles) |
| +0x1a/+0x1c/+0x1e | animation state saved by actor_save_anim |
| +0x20 | direction (angle) |
| +0x22 | OT-layer id |
| +0x24 | u32 busy flags (mask 0xff00ff00) |
| +0x25 | animation playing; +0x26 anim changed; +0x27 move state (3 walk, 4 jump) |
| +0x28/+0x2c/+0x30 | move target x,y,z; +0x50 move arg; +0x5c/+0x5e move frames; +0x6c/+0x70 per-frame velocity |
| +0x4c | flags (0x20000 = ignore in collision; 0x40000 = locked facing; 0x80000; 0x100; 0x6000000; 0x800000; 0x1000000; 0x200004 leader) |
| +0x54 | speed (actor_walk_frame_delay) |
| +0x5a | attachment sprite id (-1 = none) |
| +0x64/+0x68 | sprite scale x/y (0x1000 = 1.0) |
| +0x74/+0x76 | screen x / y of the sprite (s16) |
| +0x78..+0x7a | colour r,g,b; +0x7b fade active; +0x7c..+0x7e fade start; +0x7f fade mode; +0x80..+0x82 fade target |
| +0x83/+0x85/+0x86 | misc state bytes (vehicle mode in +0x85) |
| +0x84 | wait type (0 none; 6 = wait for colour fade) |
| +0x88 | s16 fade frames; +0x8a fade timer |
| +0x94 | walk speed used for encounter timing |
| +0x9c/+0x9e | set to 0x800 / 900 for party actors in field_begin_game_over |
| +0xa2 | u16 cmd[256] command ring; +0x2a2 read index; +0x2a4 write index |
| +0x2d6 | state pushed (actor_push_state) |
| +0x2d8 | collision radius |
| +0x2fc | frame table; +0x300 cached frame (0 = dirty) |
| +0x314 | view depth (sort key; scales the helper actor) |
| +0x3b4 | saved direction; +0x3be..+0x3c5 saved animation |
| +0x3cd | turn direction for pop |
| +0x3d0 | Actor* attachment (child actor showing sprite +0x5a) |
| +0x3dc | pushed-state buffer |
| +0x3e1 | encounter-modifier byte (read for the leader) |
| +0x3f0/+0x3f4 | actor_set_special values |
| +0x3f8 | vptr (+8 = per-frame method called by actor_sys_draw) |
| +0x3fc / +0x410 / +0x425 / +0x428..+0x430 | world-map class fields (vehicle) |

**Actor command queue.** The ring of u16 is consumed by FUN_8003d95c via `switch(cmd-1)`.
Commands queued from this range:

| Cmd | Args | Function | Effect |
|---|---|---|---|
| 5 | - | actor_queue_wait_fade | wait type 6 (fade finished) |
| 0x10 | - | actor_queue_delete | actor_delete(self) |
| 0x19 | x, y, z, arg, frames | actor_queue_move_to | absolute move; jump if y differs |
| 0x1a | dx, y, dz, arg, frames | actor_queue_move_by | relative move |
| 0x1b | r, g, b, frames, mode | actor_queue_fade_colour | colour fade |

Other cases seen: 1 wait N frames; 2-4 waits; 5-7, 0x11 turn to angle/actor/point; 0x12 set
attachment; 0x13 delete attachment; 0x14 VM call.

**Actor save/restore** (handoff 0xb, written before every program exit): see Data formats.
`actor_push_state` / `actor_pop_state` take a temporary snapshot around talks: talk code
FUN_80069d8c turns the player and NPC to face each other with
`actor_push_state_and_turn(a, angle, dir)` and `(b, angle+0x800, 0)`.

### Sprite packs (resource table 0x8007585c, 16 slots of {void *data; int id; int size})

- `sprite_pack_register(slz, id, first_slot)` unpacks a pack into a free slot.
- Who uses the slots:
  - slots 0/1: the map descriptor's two packs (ids = archive entries, loaded with
    `field_load_entry` + `field_require_sprite_pack`);
  - map section 2: id -1, slot >= 2;
  - world map party: ids -2..-n (`field_register_party_sprites`);
  - world-map extra packs: id 30000;
  - event packs: 0xfc2 and 0xfc3+n.
- `sprite_pack_find_group(id, key)` is used by actor drawing (FUN_8003f6dc).
  `sprite_pack_find_anim` and the `sprite_anim_set_*` functions back script table B opcodes
  that edit animation timing at run time.
- `sprite_packs_tint` / `sprite_packs_restore_palettes` give all sprites a sepia/greyscale
  tint (memories, flashbacks). The format is under Data formats.

### LoadQueue (0x160 bytes, vtable 0x80072dfc, task prio 1, global 0x800752b0)

| Off | Type | Meaning |
|---|---|---|
| +0x08 | Req[16] (0x14 each) | {int entry; u8 active; pad; int bytes; void *dest; u8 *done} |
| +0x148 / +0x14c | u32 | read / write index (mod 16) |
| +0x150 | LoadCallback* | completion hook object (vtable 0x80072e0c: +0x18 load_callback_done) |
| +0x154 | u8* | done flag of the read in progress |
| +0x158 | u32 | busy |
| +0x15c | int | entry being read |

- `load_queue_update`: when the loader is idle and a request is active, calls
  loader_select_entry, loader_set_callback(FUN_80011a94), then loader_start_read(dest, bytes).
- When the queue is empty, it services background **battle-data streaming** (0x8007599c
  enable, 0x800759a0 state 1→2→3, timer 0x80075998, helpers 0x80078e0c..0x80079a7c in another
  range). This streaming reads the formations chosen by field_prepare_encounter during
  exploration, so a battle starts without a long load.
- Blocking loads (`field_load_entry` with done = 0) spin on the LoadQueue vtable +8 method
  (`load_queue_update`) until the done flag is set.
- `field_load_entry(entry)` always loads into `g_load_buffer` (0x80075738, size 0x8007573c),
  unless the preload cache (0x80056acc, other range) already holds the entry.

### ScreenFade (0x98 bytes, vtable 0x80072f7c, task prio 1, global 0x80075298)

| Off | Type | Meaning |
|---|---|---|
| +0x08 | Cmd[8] (0xc each) | {s16 type; s16 frames; s16 elapsed; u8 active; pad; s16 p4; s16 p5} |
| +0x68 / +0x69 | u8 | write / read index (mod 8) |
| +0x6a | u8 | hold after a fade-out: 0 none, 1 white screen, 2 black + SetDispMask(0) |
| +0x6c | u8*[10] | cross-fade capture tiles (64x112 each, 0x3800 bytes) |
| +0x94 | u32 | cross-fade phase (1 captured → apply brightness; 2 draw capture) |

Command types (`fade_update`; elapsed += g_frame_ticks):

| Type | Effect |
|---|---|
| 0 | nothing |
| 1/2 | fade in: full-screen tile, level 255→0. Odd types subtract (from black), even types add (from white) |
| 3/4 | fade out: level 0→255, subtract or add. At the end: hold 2 (type 3) or hold 1 (type 4) |
| 5..0x24 | wipe: direction table 0x8007448c[(type-5)/2] = {2,3,3,2,1,0,0,1,7,4,6,5,5,6,4,7}. Bit 0 of (type-5)/2 = reverse progress; odd type = white |
| 0x25-0x28 | iris close/open around (p4,p5), radius 0x3c0→0 with brightness ramp |
| 0x29 | cross-fade: fade_set_scene_brightness ramp plus the captured screen drawn on top with decreasing brightness; frees the tiles at the end |

When no command is pending, the hold colour is drawn, and the BG tint (0x80075356,
0x80075aba..be) is applied to the BG renderer every frame (vtable +0x28/+0x30).

### BattleTransition (0x18 bytes, vtable 0x80072e84, task prio 1)

Layout: +8 type (0 swirl 0x5f frames, 1 split, 2 long swirl 0xf0 frames); +0xc timer;
+0x10 done (set with SetDispMask(0)); +0x14 accumulated rotation.

- Frame 0: black clear colour; `screen_capture_to_vram` copies the screen to VRAM (0x140,0)
  with STP set.
- Every frame:
  - `screen_draw_previous_frame` with decreasing brightness (motion-blur trail);
  - then the captured screen as 4x4 POLY_FT4 tiles of 80x56, tpage 0x160|tx (15-bit, abr 3),
    rotated and zoomed through the GTE (RotTransPers4, GeomOffset 160,112, H 0x200).
- Split type: two FT4 halves sliding apart (tpages 0x165/0x167).

### TextPanel (0x28 bytes, vtable 0x80072e5c, base Window vtable 0x80074038)

vtable: +8 `text_panel_draw`, +0x10 `text_panel_open`, +0x18 FUN_8006ef28 (base close),
+0x20 `text_panel_destruct`.

Layout: +0x0c/+0x0e s16 w/h; +0x10 u8 = 0xc; +0x14 enabled; +0x18 = 1; +0x1c text y; +0x20
text x; +0x24 max line width.

Text is printed every frame (`text_panel_print`, FUN_8006f5b4 in another range). The draw task
sizes the window to the text and draws it with a temporary gradient (config+0x30..+0x3c =
0x400000, 0x200000, 0x200000, 0), the same gradient fields as title window_draw_frame.

### VsyncCounter (0xc bytes, vtable 0x80073084)

Registered with `FUN_800141bc(pad, obj, 0)` (pad vsync callback list); removed with
FUN_80014230.

`vsync_counter_tick` on each vblank: global 0x80075324++, +8 = min(+8 + 1, 3), +0xa += GPU
busy. The main loop reads and resets +8 and +0xa every frame.

### Music (field_install_music and related)

- Music entries: `u32 bank_offset`, SLZ sequence at +4, SPU bank at bank_offset. The format
  matches #2674 in formats.md.
- `field_load_music_sync(entry)`:
  1. Loads the entry into g_load_buffer.
  2. Waits for `sound_bank_busy`.
  3. `field_install_music`: sound_stop, sound_load_bank(bank), unpack the sequence into
     0x80075734.
  4. Marks music pending (0x80075744) and sets game_state+0x1c = entry.
- `field_start_pending_music` starts the sequence once the SPU transfer is done:
  sound_play_sequence with fade 0x3c, or the resume variant FUN_8001a65c when returning.
- Event music swaps use the side-load buffer: `field_side_load(entry)` (0x800762e8, done flag
  0x800762e4), then `field_install_side_music` and `field_play_installed_music`.

### Party helpers

- `party_count_members` (script ext 0xd5).
- `party_swap_characters` (ext 0xb0) and `party_move_character_to_slot` (table A) swap the
  0x60-byte slots and 0xd0-byte records, then refresh battle preloads (FUN_8007972c).
- `party_validate_before_battle` keeps a member in slots 0-3. config+0x41 = battle leader slot.
- `party_stash_slot_ids` / `party_restore_slot_ids` save the slot character ids across a battle
  in field_flags+0x38.

### Map loading pipeline (this range's part)

`field_load_map` (0x80053e8c, other range) gets a descriptor from FUN_80061888:

| Index | Field |
|---|---|
| [0] | world-map extra entry |
| [1] | map container entry |
| [2] / [3] | sprite pack entries |
| [4] | code overlay entry |

Then:

1. Sprite packs: `field_load_entry` + `field_require_sprite_pack`, slots 0/1, reused when the
   ids match.
2. World map only: `field_load_extra_pack` for entries 0x10e3 / 0x10e2 / 0x10e4 (vehicle
   flags in script flags byte 1 bit 3, byte 1 bit 7, byte 2 bit 0).
3. If the world-map flag changed: `field_load_code_overlay(descriptor[4])` → 0x8007e000.
4. BG objects: `new FUN_8008672c` + `FUN_8008e100` (field), or `FUN_80088814` +
   `FUN_8007e000` (world map, after `field_register_party_sprites`).
5. `field_load_entry(descriptor[1])`, then `field_load_map_sections(f, descriptor[1])`.
6. Embedded code: section 5 data → unpacked to 0x801e9000 (0x15000 bytes reserved).
   Sections 7-10/0xc → 0x801f9800 (0x4800). Section 0xb → buffer stored at inventory+0x820.
7. Leader actor creation, camera, screen layout (`screen_layout_arena(field ? 1 : 0)`), clear
   colour from scene +0x6f..+0x71, 2 x flip + clear OT.

---------------------------------------------------------------------------------------------

## Globals (new in this range; see docs/catalog/globals.csv for f_system etc.)

| Address | Proposed name | Type | Meaning |
|---|---|---|---|
| 0x8002f8dc | g_program_object | Field* | current program object (set by field_construct and after the menu) |
| 0x80075260 | f_slz_ctx | void* | System+1 (slz_unpack context) |
| 0x80075264 | f_sys_2 | void* | System+2 (unused here) |
| 0x80075268 | f_pad | Pad* | System+4 |
| 0x8007526c | f_sys_5a18 | void* | System+0x5a18 |
| 0x80075270 | f_config | Config* | handoff 2. +0/+4/+8 confirm/cancel/menu button masks; +0x14 program-swap counter; +0x30..+0x3c window gradient; +0x41 battle leader slot; +0x4c disc-1; +0x178 start mode (R at entry); +0x181/0x182 bytes. globals.csv calls it game_counters |
| 0x8007528c | f_bg | BgRenderer* | overlay BG object. vtable +0x10 delete, +0x18 suspend, +0x20 resume, +0x28 tint on/off, +0x30 tint rgb |
| 0x80075290 | f_screen | Screen* | |
| 0x80075294 | f_actor_sys | ActorSystem* | |
| 0x80075298 | f_fade | ScreenFade* | |
| 0x8007529c/a0/a4 | f_text_panel[3] | TextPanel* | |
| 0x800752a8 | f_obj_6c | ? | object 0x6c (FUN_80055cd4) |
| 0x800752ac | f_script | ScriptCtx* | VM context 0x11cc |
| 0x800752b0 | f_load_queue | LoadQueue* | |
| 0x800752b4 | f_bg_helper | ? | overlay object 0x17c (field maps) |
| 0x800752b8 | f_battle_setup | u8[0xc0] | battle setup → handoff 5. +0x3c.. entry list; +0x70 count; +0x74.. data pointers; +0xa8 flags |
| 0x800752bc | f_vsync_counter | VsyncCounter* | |
| 0x800752c4 | f_world_helper | ? | overlay object 0x88 (world map) |
| 0x800752c8 | f_subprogram | Obj* | modal sub-program in overlay 0x801eXXXX (vtable at +0x4c) |
| 0x800752d0 | f_battle_transition | BattleTransition* | |
| 0x800752d4 | f_camp_menu | Menu* | |
| 0x800752e0 | f_sound_ctl | ? | 0x24-byte task (FUN_800313c8) |
| 0x800752e4 | f_stat_panels | TextPanel* (array of 4) | |
| 0x800752ec | f_map_objects | Obj*[12] | per-map objects (FUN_8005f018 dtor) |
| 0x8007531c | g_frame_ticks | int | vblanks since the previous frame (1-3); the time step |
| 0x80075320 | g_gpu_busy_ticks | int | vblanks while the GPU was busy |
| 0x80075324 | g_vsync_count | int | vblank counter |
| 0x80075328 | g_sys_icons | u8* | #3110 image info (+0x1ba..0x1bd game-over logo UV rect) |
| 0x8007532c | g_sys_clut | u16* | #3110 CLUT |
| 0x80075330 | g_scene | MapScene* | map section 0 block 0 |
| 0x80075334 / 0x80075338 | g_scene_zones / g_scene_zone_base | u8* | walk/trigger structure (scene + [0x80] (+4)) |
| 0x8007533c / 0x8007533d | encounter preload ready / requested | u8 | |
| 0x80075340 / 0x80075344 | last encounter group / arg | int | |
| 0x8007534c | g_encounter_file_count | u8 | |
| 0x8007534d | g_next_program_loaded | u8 | done flag for 0x80075350 |
| 0x80075350 | g_next_program | u8* | packed program (#2129 battle or #1 title) |
| 0x80075354 / 0x80075355 / 0x80075356 | scene-loaded / field-active / bg-tint-active | u8 | |
| 0x80075358 | g_overlay_world_flag | int | world-map flag the current code overlay was loaded for |
| 0x80075360 | field_actors | Actor*[194] | (known) |
| 0x80075394 | g_parked_vehicle | Actor* | |
| 0x80075398 / 0x8007539c | field_actors[0xe] / [0xf] | Actor* | helper / vehicle actor |
| 0x80075560 / 0x80075568 | field_actors[0x80] / [0x82..] | | save point / extra actors |
| 0x8007567c / 0x80075680 | message_windows[5] / [6] | MsgWindow* | disc-error / system message windows |
| 0x800756b8 / 0x800756c0 | g_overlay_a / g_overlay_b | void* | 0x801e9000 (0x15000) / 0x801f9800 (0x4800) reservations |
| 0x800756bc / 0x800756c4 / 0x800756c8 / 0x800756f4 | packed copies from map sections 5 / 7-0xc / 0xb / 6 | u8* | |
| 0x800756cc | g_entry2_data | u8* | section 0xb data (→ inventory+0x820) |
| 0x800756d0 / 0x800756d4 | g_ot_terminator / g_ot_dummy | u32 | |
| 0x800756d8 | g_force_streaming | u8 | |
| 0x800756f8 | g_map_script | u8* | section 1 block 0 |
| 0x800756fc / 0x80075700 | g_state_block / +0x100 | u8* | handoff 9 |
| 0x80075714 / 0x80075718 | g_map_font / g_map_glyphs | u8* | section 1 block 1 / its bitmaps |
| 0x8007571c | g_sys_strings | u8* | #3110 text (0 / 1 disc errors; 2 level-up; 3+ item messages) |
| 0x80075720 | g_sys_data | u8* | #3110 unpacked |
| 0x80075724 / 0x80075728 | actor blob (handoff 0xb) / blob (handoff 0x10) | u16* | |
| 0x8007572c | g_draw_enabled | u8 | |
| 0x8007572d | g_vsync_interval | u8 | 0 = 60 Hz, 2 = 30 Hz |
| 0x80075730 / 0x80075732 | draw y of buffer 0 / 1 | s16 | |
| 0x80075734 | g_music_seq | u8[0x4800] | |
| 0x80075738 / 0x8007573c | g_load_buffer / size | u8* / int | |
| 0x80075740 | g_music_src | u8* | music entry kept until the SPU is done |
| 0x80075744 / 0x80075746 | music pending / music entry | u8 / s16 | |
| 0x80075748..0x8007575c | g_pad_cur0, g_pad_new0, g_pad_rep0, g_pad_cur1, g_pad_new1, g_pad_rep1 | u32 | |
| 0x80075764 / 0x80075766 | scene H / scene scale | s16 | scene +0x6a / +0x68 |
| 0x80075768 | g_preload_cache | Preload[12] (0x14 each) | {u8 state; u8; u8; pad; s16; int entry; int; void *data} |
| 0x8007585c | g_sprite_packs | {void*; int id; int size}[16] | |
| 0x8007591c / 0x8007597c / 0x80075994 | task table / priorities / count | | |
| 0x80075998 / 0x8007599c / 0x800759a0 | streaming timer / enable / state | | battle-data streaming |
| 0x800759a4 / 0x800759a5 | disc error now / last frame | u8 | |
| 0x800759a8 / 0x800759dc / 0x80075a10 / 0x80075a44 | encounter preload entries / sizes / counts / order | int[] | |
| 0x80075a78 / 0x80075a7c | arena size (0x40c40) / arena | | |
| 0x80075a80..0x80075a98 | camera valid, eye xyz, target xyz (<<12) | int | |
| 0x80075aba/bc/be | BG tint r,g,b | s16 | |
| 0x80075c38 | vehicle hover offset | int | |
| 0x80076190 | g_ability_flags | u32 | encounter modifiers (bits 1/2/4 rate; 8/0x10 formation filters) |
| 0x80076196 / 0x8007619a | menu request mode / party slot | u8 / s16 | |
| 0x800761a0 | g_stat_snapshot | u8[8][0x30] | post-battle compare |
| 0x800761a4 | g_preload_reserve | int | free memory kept when preloading (0x14000) |
| 0x800761a8 / 0x800761b4 / 0x800761c4 / 0x800761e8 | OT layers: count / ids / buffers / second pass | | |
| 0x80076228 / 0x8007622c | item table (0xc28) / came from handoff | | |
| 0x80076230 / 0x80076240 / 0x80076244 / 0x8007624c | extra-pack data / tag / tpage / CLUT [4] | | |
| 0x80076254 / 0x80076256 | next extra CLUT row (0x1f8) / next texture x (0x280) | s16 | |
| 0x80076274..0x80076277 | stat-display cursor | u8 | |
| 0x80076278 | g_returned_from_battle | u8 | |
| 0x80076279 | sub-program draw flag | u8 | |
| 0x800762e4 / 0x800762e8 / 0x800762ec | side load: done / buffer / entry | | |
| 0x800762f0 / 0x800762f4 / 0x800762f8 | side-loaded font / glyphs / script | | |
| 0x80076314 | g_poison_next_distance | int | |

**field_flags = script globals (0x8007570c == 0x80075708)**. Offsets used in this range:

| Off | Meaning |
|---|---|
| +0x00 | R |
| +0x0e | camera-target actor (-1 = free camera) |
| +0x10 | controlled actor (-1 = none) |
| +0x12 / +0x14 | encounter args (+0x14 = encounter group) |
| +0x18 | distance walked (20.12) |
| +0x1c | encounter threshold |
| +0x20 | distance at the last encounter roll |
| +0x24 | returning from another program |
| +0x25 | encounter rate (0 = off, minimum 10) |
| +0x26 | input lock |
| +0x27 | sprite palettes tinted |
| +0x38..+0x64 | event scratch |
| +0x68 | world-map mode (maps 1-3) |
| +0x7e / +0x7f | exit flags |
| +0x80 | sort actors by screen y |
| +0x81 | reset sprite cache B |
| +0x84 | s8 encounter modifier |
| +0x89..+0x91 | VM restore list |
| +0x92 / +0x93 | menu lock |

**game_state (0x80075710)**:

| Off | Meaning |
|---|---|
| +0x08..+0x10 | position |
| +0x18 | direction |
| +0x1a | map |
| +0x1c | music entry |
| +0x1e | world-map heading |
| +0x24 | leader actor slot |
| +0x244 | counter |
| +0x250 | u16[10] items to hand over |
| +0x266 / +0x267 | encounter modifiers |
| +0x268..+0x29c | vehicle positions |
| +0x288 | helper kind |
| +0x294 | timer |

---------------------------------------------------------------------------------------------

## Data formats

### Map container (archive entry from the map descriptor; formats.md: 0xC87 + map)

`u32 count; {u32 id; u32 offset}[count]`. Checked on #3210 and #3300 (both have sections
0, 1 and 2). Parser: `field_load_map_sections`.

| Id | Contents | Handling |
|---|---|---|
| 0 | SLZ chain: textures, CLUTs, scene | see below |
| 1 | SLZ chain: block 0 event script; block 1 map font | g_map_script / g_map_font; then VM install (FUN_80062260) |
| 2 | SLZ sprite pack | sprite_pack_register(id -1, slot >= 2) |
| 3 | raw BG data | overlay FUN_80088a3c(bg, data); then actor restore when returning |
| 5 | copy of entry 0xc1f (#3103), size = that entry's size | → later unpacked to 0x801e9000 |
| 6 | copy of entry 0xc23 (#3107) | 0x800756f4 |
| 7 / 8 / 9 / 10 | copy of #3109 / #3108 / #3106 / #3104 (table 0x800744d6) | → unpacked to 0x801f9800 |
| 0xb | copy of entry 2 | unpacked; pointer stored at inventory+0x820 |
| 0xc | copy, sized as the map entry itself | → 0x801f9800 |

Sections 5-0xc are disc-seek savers: the map carries a copy of a fixed archive entry. It is
copied only when no copy is pending (`if (buffer == 0)`); field_load_map unpacks and frees it.

### Section 0 (SLZ method 2 chain; block numbers within the chain)

- **Block 1, texture header:** `s32 tex_count; s32 clut_count; u32 ?(0x10); u32 ?(0x20);`
  then `tex_count` x 8-byte records `{u8 pos; u8 depth; u8 pad[6]}`.
  - VRAM position: x = 0x200 + (pos & 7) * 64, y = (pos >> 3) * 256.
  - depth: 0 = 4-bit, 1 = 8-bit, 2 = 16-bit.
  - Both samples: 2 textures, depth 2, pos 0 and 4, i.e. two 256x256 16-bit pages at (512,0)
    and (768,0). That is a 512-px-wide prerendered background.
- **Blocks 2 .. tex_count+1:** texture pixels, 256x256 at that depth
  (`upload_texture(p, depth, 0, x, y, 256, 256)`); 0x20000 bytes each for 16-bit.
- **Block tex_count+2** (only if clut_count > 0): clut_count x 0x204-byte records
  `{u8 mode; u8 pad[3]; u16 clut[256]}` → CLUT row 0x1e0+i (x 0).
  - mode 0: colour 0 of each 16-colour bank is set to 0 (transparent);
  - mode 1: only colour 0 is cleared.
- **Block 0, scene header** (kept as g_scene; 0xc88 / 0x1004 bytes in the samples). These
  fields were checked on both samples:

| Off | Type | Meaning |
|---|---|---|
| +0x04 / +0x06 | u16 | 320 / 240 in both samples (view size?). +6 > 240 sets BG +0x122 (taller background) |
| +0x08/+0x0c/+0x10 | s32 | camera eye x,y,z (world units) |
| +0x14/+0x18/+0x1c | s32 | camera target x,y,z (also the default start position) |
| +0x20/+0x24 | s32 | 160 / 120 (screen centre?) |
| +0x30 | s32 | roll (low 16 bits used) |
| +0x34/+0x36 | u16 | screen w/h (320/240) → GeomOffset w/2,h/2 |
| +0x38..+0x4b | s16[3][3] | precomputed rotation matrix (not read in this range) |
| +0x4c..+0x57 | s32[3] | eye again (translation) |
| +0x58 | s16 | default start y |
| +0x68 | u16 | world scale in percent (45, 60): walking distance and sprite scale |
| +0x6a | u16 | projection distance H (640, 977) |
| +0x6c..+0x6e | u8 | 0x80 0x80 0x80 (light/colour?) |
| +0x6f..+0x71 | u8 | background clear colour |
| +0x78..+0x94 | u32[8] | sub-block offsets (end offset = scene size); +0x80 = walk/trigger block |
| +0x98 | u8 | colour-cycle entry count; +0x99 u8 mode (1 = ping-pong) |
| +0x9c | u32[n] | colour-cycle entries; byte 3 = duration in frames |
| +0xdc/+0xde | s16 | camera rotation x / y |

- **Walk/trigger block** at scene + [0x80]. Partly understood (likely / guess):
  - word 0 = 0x405 in both samples; zones of 0x60 bytes from +4.
  - Zone fields: +0x1a u16 n_tri (0x24-byte items at base + zone[0x54]); +0x1c u16 n_area
    (0x58-byte items at base + zone[0x58]). Overlay functions FUN_80085cc4 (find zone) and
    FUN_80083944 (test item) read them.
  - Area (0x58 bytes): s16 bounding box at +0 (y_min, y_max, x_min, z_min, x_max, z_max),
    +0xc u8 type, +0xd u8 tag ('~' marks the default entry area), triangle vertices as SVECTOR
    at +0x10 / +0x18 / +0x20; then unknown fields (u16 list at +0x44 = 0xc, 0xe, 0, 2, 4, 6,
    8, 0xa in both samples).
  - field_load_map starts the player at the centroid of the first '~' area (or area 0) when no
    explicit position is given.
  - **These areas are the best candidate for the event-trigger table** (doors and chests to
    script functions), still open in the memory notes.

### Section 1

- **Block 0:** event script (formats.md). Code base = block + 0x1c + hdr[1]*4.
- **Block 1, map font:** `u32 n; u32 height (12); u8 width[n];` then n bitmaps of 0x18 bytes
  (12 rows x u16, 1 bit per pixel, bit 15 = leftmost). Glyph k (1-based) is at
  `block + 8 + n + (k-1)*0x18`; g_map_glyphs = block + [0] + 8. Checked: #3300 has n = 276,
  #3210 has n = 400, and the sizes match exactly. The message renderer (0x8005e1a4) expands
  each 4-bit nibble through a 16-entry u16 table into four 4-bpp pixels.

### Sprite pack (section 2; entries from the map descriptor; side-loaded event packs)

Unpacked layout (structure checked on #3210 and #3300):

```
u32 group_count; u32 group_off[group_count]          // relative to pack start
group: u32 record_count; Record rec[record_count]    // offsets below are relative to the group
Record (28 bytes): u32 key (character/sprite id: 2, 3, ...); u32 id_lo; u32 id_hi
                   (animation id range, e.g. 0..0xd, or 0xff..0xff); u32 ids_off; u32 anims_off;
                   u32 frames_off; u32 palette_off
ids:     u8 count; u8 anim_id[count]                 // anim id → index
anims:   u32 count; u32 off[count] (relative to anims)
anim:    u16 frame_count; u8 b2; u8 loop_frame; then frame_count x {u16 frame; u8; u8 duration (likely)}
frames:  starts u16 0x15, u16 1 (or 1, 1) in samples // sprite frame definitions, not decoded here
palette: u32 rel → {u16 count(|0x8000 flag); u16 0; u16 colour[count]}   // 16 colours (4-bit sprites)
```

Lookup: `sprite_pack_find_group(id, key)` finds the group with a record where rec.key == key
and id_lo <= id <= id_hi. Then `sprite_pack_find_anim` finds the record with that key whose
id list contains id; the animation is `anims + anims.off[index]`.

### Field system data, entry #3110 (0xc26), one SLZ block (checked)

```
+0x00 u32 text_off   → text: u32 off[] relative to text start; strings are u16 glyph codes
                       (#0 "CANNOT READ DATA FRO..." disc error, #1 lid open, #2 level-up, #3+ item notices)
+0x0c u32 a_off      → A: u32 clut_rel (CLUT at A+clut_rel+4, 256 colours → VRAM (0,0x1ff), colour 0 of
                       every bank cleared); u32 img_rel (image {u16 w; u16 h; 4-bit pixels} at A+img_rel →
                       VRAM (0x180,0x100); 256x256); A+0x0c = UV/icon table (g_sys_icons)
+0x10 u32 b_off      → B: u32; u16 clut[256] at B+4 → (0,0x1fe) (colour 0 of every bank cleared); 4-bit pixels
                       256x48 at B+0x208 → (0x180,0x1c8)
```

After the uploads, the block is shrunk to end at A's image (FUN_80013310).

### Extra pack (world-map vehicles; entries 0x10e2-0x10e4; `field_load_extra_pack`)

Tagged list `{u32 tag; u32 offset}...` ending in tag 0:

- tag 5: SLZ data block → slot data;
- tag 1: SLZ image `{u16 flags (depth = flags & 3); u16 clut_count; u16 clut[clut_count];
  u16 w; u16 h; pixels}`. CLUT → row 0x1f8+n; image → (0x280 + 0x40n, 0x100), width
  `w << (2 - depth)` px;
- tag 9: sprite pack registered with id 30000.

### Actor snapshot (handoff 0xb; actor_sys_save_all / actor_sys_restore_all)

```
u16 total_size; u16 n; u16 slot[n]; u16 offset[n] (from blob start; first = 4 + 4n);
record: actor bytes +0x00..+0xa1 (with +0x5a = attachment sprite id), u16 queue_count at +0xa2,
        2 bytes, u16 queue[queue_count] from +0xa6
```

### Other handoffs touched here

| Handoff | Contents |
|---|---|
| 0xe | 0x440-byte save snapshot: game_state 0x2a0, then script flags 0x170 (camp menu / load game) |
| 0x11 | item table 0xc28: u16[0x400] at +0; u16[16] at +0x800; u8[0x400] at +0x824 (FUN_8003c8a0(t, i, 0xffff)); u8 at +0xc24 |
| 10 / 0xc | preloaded entries from the previous program: 0xc = 0x22 bytes, s16 entry[16] (negative = preloaded; [0] = music) + s16 count at +0x20; 10 = their data back to back. field_restore_preloads parks the data in VRAM (x 0x140, 0x2c0-halfword rows) to defragment the heap, then reads it back. Order table 0x800744cc = {2,4,3,0,5,1} |
| 5 | battle setup 0xc0 (from FUN_800788b0) with resolved preload pointers |

### VRAM map (field program)

| Area | Use |
|---|---|
| (0,0)-(319,223), (0,224)-(319,447) | framebuffers |
| x 0x141-0x1ff, y 1-0xff | actor sprite cache A |
| x 0x141-0x1ff, y 0x101-0x1ff | sprite cache B (overlaps the #3110 texture area: B is probably the world-map class) |
| (0x140,0) 320x224 | screen capture for the battle transition (sprites no longer needed) |
| (0x1c0,0x170) 64x112 | cross-fade tile scratch |
| (0x180,0x100)-(0x1ff,0x1ff) | #3110 window/icon textures (A 256x256 4-bit; B 256x48 at y 0x1c8) |
| (0x200,0)-(0x3ff,0x1ff) | map textures, pos-indexed 64x256 pages (16-bit BGs use pages 0-3 and 4-7) |
| (0x280+, 0x100) | world-map extra-pack textures |
| y 0x1e0+ | map CLUTs (0x1e0+i) |
| y 0x1f3+slot | cache B CLUTs |
| y 0x1f8+ | extra-pack CLUTs |
| y 0x1fe / 0x1ff | #3110 CLUTs |

field_prompt_disc_change preserves (0x180,0x100) 128x256 and (0,0x1d8) 256x48 (CLUT rows).

---------------------------------------------------------------------------------------------

## Functions in this range missing from index.csv

Ghidra did not create these functions, so they have no CSV rows. All are called only from
overlays or through jump tables or callbacks. Ends were found by prologue/`jr ra` scan.

| Address | End | Notes |
|---|---|---|
| 0x800410cc | 0x80041310 | uses SquareRoot0: another actor distance test (like actor_touches_save_point) |
| 0x8004185c | 0x80041918 | creates an attachment actor (FUN_8007e478 + FUN_8003d750): variant of actor_create_attachment |
| **0x80041cec** | 0x80042ca0 | **actor sprite renderer** (~4 KB). Uses actor +0x2fc frame table, +0x64/+0x68 scale, +0x314 depth, scene H/scale; calls the cache allocators 0x80044754 / 0x80044c8c and the stub 0x8004872c |
| 0x80044754 | 0x80044c8c | sprite cache A: find or allocate a VRAM slot, LoadImage (FUN_80061cf8) |
| 0x80044c8c | 0x80045284 | sprite cache B: slot plus CLUT (upload_clut256 row 0x1f3+n; FUN_80012f28) |
| 0x800452b4 | 0x8004541c | PushMatrix / SetRot / SetTrans / 0x800709e4 / PopMatrix: projects a point with the view matrix |
| 0x800458f4 | 0x80045920 | ratan2 helper |
| 0x80045968 | 0x800459c0 | calls actor_sys_mark_all_dirty |
| 0x80045c20 | 0x80045c78 | calls text_panel_clear |
| 0x8004872c | 0x80048734 | empty stub (`jr ra`) |
| 0x8004b3c4 | 0x8004b418 | FUN_80061888 + FUN_800563a0: preload a map's data by map id |
| 0x8004b760 | 0x8004b7a8 | vsync hook installed with the camp menu (FUN_800134d8(0x8004b760)) |
| 0x8004b7a8 | 0x8004c0cc | post-battle stat-panel window callback (msg window +0x158): field_install_side_text, msg_window_open x3, text_panel_print, SetDrawArea |
| 0x8004c250 | 0x8004c334 | poison step damage: party slot status (word0 & 0xf0000) == 0x80000 → HP(+0x1c) -= max(1, maxHP(+0x18)/33), floor 1; green flash tile at OT 0x80c |
| 0x8005161c | 0x8005166c | encounter preload status: 0 not ready, 1 one file left, 2 all ready (FUN_80056c68(1) vs 0x8007534c) |
| 0x800537ac / 0x8005381c | 0x8005381c / 0x800538a8 | VRAM save / restore via a helper 0x800538a8 (StoreImage / LoadImage pairs) |
| 0x80053d7c | 0x80053e10 | calls 0x80053e10 four times (0x80053e10-0x80053e8c is also unindexed) |

0x80042d64, 0x80043018, 0x80046fa4-0x800471f4, 0x8004a40c, 0x80048134 and 0x8004c8b0-0x80050dec
are jump-table tails of the indexed functions (fade_update, field_main_loop, field_update);
their code appears in those functions' decompilations.

---------------------------------------------------------------------------------------------

## Porting notes

**GPU (libgpu).** These calls and primitives must be implemented or emulated:

- Calls: DrawSync, VSync(0/2), PutDrawEnv, PutDispEnv, SetDefDrawEnv, SetDefDispEnv, ClearImage,
  LoadImage, StoreImage, DrawOTag, ClearOTagR (one entry), SetDispMask, SetDrawMode, ResetGraph,
  SetDrawArea.
- Primitives built by hand:
  - TILE (0x60-0x63) full-screen;
  - SPRT (0x64/0x66) with DR_MODE;
  - POLY_F4 (0x29 opaque in wipes);
  - POLY_G4 (0x3b semi);
  - POLY_G3 (0x32 semi, iris);
  - POLY_FT4 (0x2e semi; tpage 0x160+, abr 3);
  - DR_TPAGE words `0xe1000200 | abr << 5`.
- The OT is forward-linked (index 0 first) with 0x830 entries, plus a 16-entry aux OT.
  OT-layer splicing writes OT entry words directly. A native port should keep a sorted draw
  list keyed by OT index (stable within an index: prims are prepended, so the last added
  draws first within a slot).
- **Framebuffer capture is required**: battle transition, cross-fade and "previous frame"
  trails read back the displayed frame (StoreImage, set STP, LoadImage) and draw it as a
  15-bit texture with blending. A renderer that emulates VRAM (1024x512 16-bit) is simplest.
  Otherwise use render-to-texture for both framebuffers.
- Blend modes used: abr 1 (add) and abr 2 (subtract) for fades, abr 3 (+B/4) for transition
  tiles.

**GTE (libgte).**

- RotTransPers4, MulMatrix, SetRotMatrix, SetTransMatrix, SetGeomOffset, SetGeomScreen,
  ratan2, SquareRoot0.
- Game wrappers: 0x8006e71c / 0x8006e8e8 / 0x8006eab8 RotMatrixX / Y / Z; 0x8006e52c
  RotMatrix(SVECTOR); 0x80070654 ApplyMatrixLV-style transform; 0x8006e404 rsin and
  0x8006e498 rcos over the table at 0x80077398 (0x401 entries).
- Fixed point: positions 20.12, angles 0..0xfff.

**Timing.** Variable time step `g_frame_ticks` (vblanks per frame, 1-3) from the vsync callback.
Field maps run with VSync(0) (60 Hz target), the world map and battle intro with VSync(2).

**Memory.**

- Allocators: mem_alloc_b (temporary/large), mem_alloc_c (persistent), mem_alloc (operator
  new). field_restore_preloads moves data through VRAM to avoid heap fragmentation; a port can
  keep the buffers.
- Fixed overlay addresses 0x8007e000, 0x801e9000 and 0x801f9800 hold code. The port needs
  native implementations of those overlays, selected by entry id.

**Libraries used from boot:** loader_* (select / start / poll / cancel / entry_size /
load_sync / is_disc; FUN_80011a94 / FUN_80011aac callback; FUN_80011bbc / FUN_80011be0 error
state), slz_alloc_output / slz_unpack, handoff_*, pad_* (current / pressed / repeat / clear /
option / port mask / repeat delay; FUN_800141bc / FUN_80014230 vsync callback;
FUN_80013cb8 analog), sound_* (stop, load_bank, bank_busy, play_sequence, fade_out, play_sfx;
FUN_8001a65c resume; FUN_8001a8ec / FUN_8001a900 pause / resume), upload_texture,
upload_clut256, ClearOTag_game, mem_*, rng_next, boot_run_main_program, FlushCache
(0x8001054c).

**Dependencies on other field ranges:**

- Actor base class: 0x8003d53c delete, 0x8003d750 init, 0x8003d8e4 re-init, 0x8003d95c update
  and command interpreter, 0x8003f240 turn, 0x8003f8f8 queue space, 0x8003f948 draw,
  0x8003fd68 / 0x8003fe20 / 0x8003fe7c movement, 0x80040580 jump, 0x8004092c / 0x800409b4.
- Task list: 0x80056178 / 0x800561a4 / 0x80056270 / 0x80056304.
- Preload cache: 0x80056acc lookup, 0x8005703c release, 0x80056728 flush, 0x800563a0 request,
  0x80056c68 count, 0x80056a3c, 0x800570e8, 0x8005711c.
- Msg system: 0x8005835c ctor, 0x80058838 open, 0x800589a0 close, 0x8005e7c8 state,
  0x800592a8, 0x80058e58.
- VM: 0x800621e0 ctor, 0x80062260 install script, 0x800697bc save, 0x80069aac restore,
  0x8006a6d0 call, 0x8006ad40.
- Others:
  - 0x80061888 map descriptor; 0x80061c4c party mask;
  - Window base 0x8006ef10 / 28 / 48 and print 0x8006f5b4;
  - insert-disc screen 0x8006f958; sound object 0x8006f90c;
  - camp menu 0x80030470..0x80030908;
  - battle setup and streaming 0x800788b0, 0x80078e0c..0x80079a7c, 0x8007972c;
  - fatal error 0x80055da0;
  - overlay classes (BG renderer, actor subclasses, terrain query 0x8008d478).

**Order of work to get a map on screen.**

1. Archive + SLZ (done in boot).
2. Map descriptor (0x80061888).
3. `field_load_map_sections`: upload the two 16-bit BG pages and parse the scene header and
   camera.
4. Screen / OT and a VRAM emulation.
5. BG renderer from the 0x8007e000 overlay. It is not in this image: the 16-bit pages at
   (512,0) are the prerendered background, and section 3 is presumably its tile/scroll data.
6. Sprite packs plus the sprite renderer (0x80041cec) for actors.
7. `actor_create` / `actor_sys_update` with player input.
8. `field_main_loop` pacing.
