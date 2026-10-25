# Field program, part 4: event-script VM core and what follows (0x80064f30-0x8007c00c)

Range: 125 functions in `analysis/decomp/field/index.csv`. Rows are in `part4.csv` (96 confirmed, 25 likely,
4 guess). Disassembly helpers used: `scratchpad/p4/fdis.py` (field image), `tree.py` (walks a compare-tree
dispatch for each value), `usage.py` (opcode usage over all 951 parseable disc-1 map scripts).

## 1. The big correction: one script thread, 58 trigger regions

The VM is **single-threaded**. `ScriptCtx` (class ScriptVM, 0x11cc bytes, vtable 0x80074028, built by
FUN_800621e0, global `0x800752ac`) holds exactly one execution context in its first 0x4c bytes. The
"thread slots" at `ctx+0x4c + n*0x48` (n < 58) are **trigger regions**: convex quads on the map floor, each
bound to a script function. Ext 0x74/0x75 ("thread_start") are really `trigger_add`; `vm_alloc_thread` is
`trigger_alloc_slot`. Events (talk, step-on, Private Action, menu hooks) do not run in parallel: they
*interrupt* the main thread by pushing a call frame and jumping, and `return` at call depth 0 resumes the
interrupted code (with its saved wait counter).

## 2. ScriptCtx layout

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vtable* | 0x80074028: entry 1 (+8 delta, +0xc fn) = `vm_thread_update`. No virtual destructor (A 0x2d deletes it directly) |
| +0x08 | u32 | current instruction (set by vm_exec_ext; vm_fetch_args reads inline args from it) |
| +0x0c | s32 | wait counter in frames (ext 0x7a adds); decremented by frame delta `0x8007531c` |
| +0x10 | u32* | IP |
| +0x14 | u32* | code base = script + 0x1c + hdr[1]*4 (hdr[1] is 0 everywhere) |
| +0x18 | u8* | locals top: globals + hdr[5] (rounded to 4) at start; `frame` (0x18) grows it. Local n = `*(top - n - size)` |
| +0x1c | u8* | local flags top: flags + hdr[6] at start; `frame` grows it |
| +0x20 | u32* | call stack pointer (base `0x800756fc`), grows up, 4 words per frame |
| +0x24 | u32* | value stack pointer (base `0x80075700`), grows up |
| +0x28 | u16* | string offset table (script + 0x1c + hdr[0]) |
| +0x2c | u16* | text (table + hdr[3]*2) |
| +0x30 | s32 | wait state (section 4) |
| +0x34 | s32 | wait parameter (map id, window, actor, entry...) |
| +0x44 | s32 | map transfer: arrival direction (arg 5) |
| +0x48 | u8 | map transfer: hero spawn param (0x80, or arg 6 of ext 0xda) |
| +0x49 | u8 | map transfer: transition type (arg1 & 0xf) |
| +0x4a | u8 | map transfer: arg1 & 0x80 (passed to field_load_map as flag 2) |
| +0x4c | Trigger[58] | trigger regions (0x48 each), section 3 |
| +0x109c | s32 | active trigger (slot whose event runs; -1 none). Event preloads are owned by it |
| +0x10a0 | s32 | last trigger entered (no re-fire until the leader leaves; button triggers reset it) |
| +0x10a4 | s32 | sync mode (vm_call_sync): waits and wait states are skipped |
| +0x10a8 | s16 | music being loaded (-1 none); +0x10aa u8 loaded, +0x10b1 u8 load-done flag written by FUN_8004a654 |
| +0x10ab | u8 | talk event active; +0x10ac u8 talk event returned; +0x10ae s16 NPC actor talked to (-1 = pickpocket) |
| +0x10b0 | u8 | 1 after vm_init: the first fade-in (ext 0x7c/0xb5) gives control to the hero actor |
| +0x10b2 | u8 | music change uses fade (ext 0xc5): +0x10b8 u32 / +0x10b4 s16 parameters |
| +0x10bc..+0x10c2 | s16 x4 | choice: option count, first line, current selection, cancel allowed |
| +0x10c4 | {s32 x, s32 y}[16] | choice cursor positions per line; +0x1144 s16 slide counter; +0x1146 s16 lines in option |
| +0x1148 / +0x1168 | s16[16] | choice line widths / heights |
| +0x1188 | s16 | close window after choice (cleared by choice setup; set elsewhere/overlay) |
| +0x118c | s32 | call depth (events start at 1; `return` at < 1 ends the event) |
| +0x1190 | u8 | sync call: return from vm_thread_update at depth 0 |
| +0x1191 | u8 | pickpocket +5% (accessory 0x1bc) |
| +0x1192 | u8 | restart-update flag (cleared each frame; checked after every ext op; no writer in this image) |
| +0x1194 | s32 | controlled actor saved while an event has control |
| +0x1198 | s16 | menu-close hook: script entry run by vm_call_sync when the field menu closes (B 0x5c) |
| +0x119a | s16 | dwell counter for triggers with +5 set |
| +0x119c | s32 | pickpocket skill level; +0x11a0 s32 thief's party slot |
| +0x11a4 / +0x11a8 | s32 | hdr[5] (aligned) / hdr[6]: start of map-local vars / flags inside script_globals / script_flags |
| +0x11ac | s16[16] | choice: lines per option (A 0x41-0x48; reset to 1 after each choice) |

Script header (map section 1): hdr[0] code bytes, hdr[1] code offset (words, 0), **hdr[2] main entry (word
index)**, hdr[3] string count, hdr[4] text bytes, **hdr[5] global variable bytes (0x344-0x364)**, **hdr[6]
global flag bytes (always 0x170)**. Variables/flags below these are save-game globals; above, per map.

### Trigger slot (0x48)

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u32* | step-on code (absolute pointer; saved as word index by vm_save_state) |
| +0x04 | u8 | active |
| +0x05 | u8 | dwell mode: fire only after the leader has moved (actor flag 0x400000) inside for +0x08 frames (ext 0x9a, unused in scripts) |
| +0x06 | u8 | also fires while script flags 12/14 are set (A 0x1d) |
| +0x07 | u8 | world-map flag (A 0x27, used on maps 909+); read by the world-map overlay test 0x80082b6c, not here |
| +0x08 | s16 | dwell frames |
| +0x0a | s16 | Private Action entry (word index, -1 none; B 0x0e) |
| +0x10 | u32 | button mask (-1 = fires on entry). Pad bits plus 0x10000/0x20000/0x40000/0x80000 for config buttons 0-3 (ext 0xd8/0xd9) |
| +0x14 | s32[8] | quad corners x0,z0..x3,z3 in map units (world >> 12), clockwise so every edge cross product is <= 0 inside |
| +0x34 / +0x36 | s16 | y range (default -0x8000..0x7fff; ext 0x75/0xd8) |
| +0x38..+0x44 | s32[4] | bbox minx, minz, maxx, maxz (trigger_compute_bbox) |

Leader position used: actor +0x74/+0x76 (s16, set by scripts with B 0x3e) when `field_state+0x68 == 0`,
else world position >> 12. Example (map 4): `thread_start(66,473, 62,479, 217,479, 216,473, 0, 4804, 0)` =
a 6-unit-deep strip at the map edge running function 0x12c4 (a door).

## 3. Per-frame scheduling (`vm_thread_update`)

Called once per frame through the vtable by the field update FUN_8004c334 (field mode 0xd), after
the main loop has latched the pads (`0x80075748` held, `0x8007574c` pressed, `0x80075750` repeat) and
`0x8007531c` = vsyncs since the last frame.

1. `ctx+0x1192 = 0`; `field_state+0x32` countdown -1.
2. `0x800759a4` (disc error / lid open) set: return.
3. If not sync mode and the player controls an actor (`field_state+0x10 != -1`):
   - talk: Cross (config[0]) or Square (config[3]) pressed, input not locked (`field_state+0x26 == 0`),
     no trigger event, no talk event, `field+0x14 == 0` -> `field_try_talk(ctx, cross)`; on success
     `+0x10ab = 1`, controlled actor saved in +0x1194 and `field_state+0x10 = -1`.
   - still in control and unlocked and no trigger event -> `trigger_check_regions(ctx)`.
4. Wait counter: if not sync and `+0xc > 0`: subtract the frame delta (clamp 0) and **return**. (A new
   event starts only after the interrupted code's pending wait expires.)
5. Pending music (+0x10a8): once the bank is loaded and sound idle, start it (FUN_8001a5d8, or
   FUN_8001a65c with the fade parameters), free buffers 0x80075740/0x80075738, +0x10a8 = -1.
6. Wait state service (section 4). Unsatisfied: return.
7. Fetch/execute loop: opcodes 0x01-0x63 inline (unchanged from formats.md), anything else ->
   `vm_exec_ext` (which dispatches 0xfe/0xff to the tables). After each instruction go back to step 4, so a
   frame runs instructions until a wait counter or wait state stops it. If an ext op sets +0x1192 the loop
   restarts at step 2. Wait state 1 (map transfer) also restarts at step 2 after loading the new map, so the
   new map's script starts in the same frame.

Core-op details missed by formats.md: typed operands whose `instr & 0x80ffff == 0` use the address computed
by the last index op (0x09/0x0a/0x10) - the disassembler prints these as `[indexed]`; `push flag` 0x0e with a
zero operand pushes R; `call` (0x14) pushes a frame, depth+1 and **zeroes the wait counter**; `return`
(0x17) pops IP, wait counter, flags top, locals top (the last two rebased on hdr[5]/hdr[6]), depth-1. At
depth < 1: talk event -> wait state 2 (all windows) and +0x10ac = 1; trigger event -> `+0x109c = -1` and
control restored from +0x1194; sync call -> clear sync mode, return from the update.

Call frame (vm_push_frame), pushed in this order: locals offset (`top - globals - hdr5`), local-flags offset,
wait counter, IP - code base.

## 4. Wait states (`ctx+0x30`, parameter `+0x34`)

Set by vm_exec_ext / the tables, serviced at step 6. In sync mode states 2-6 are cleared immediately.

| State | Set by | Released when |
|---|---|---|
| 1 | ext 0x7d/0xa4/0xda map transfer (param map) | screen fade (0x80075298) done unless transition 1/2; transition 1 forces FUN_80047ad8; then `field_load_map(field, map, 1, flag)`, hero actor recreated at game_state+8 (dir +0x44, param +0x48) unless flag 12 && 3D mode; loop restarts |
| 2 | ext 0x7f (window), end of talk event (-1) | FUN_80058b00(msg, window) returns 1 or -1. Talk cleanup: NPC (+0x10ae) and leader restore facing (FUN_80041aa4), control back to +0x1194 |
| 3 | ext 0xbd | fade FUN_80047260(fade, param) == 0 |
| 4 | ext 0xbe | camera script object 0x800752b4: byte +9 == +8 |
| 5 | ext 0xa2 (actor) | actor command queue empty and idle (FUN_80041450 != 1) |
| 6 | ext 0xbb | map object / entry `param` loaded (FUN_8004a830) |
| 8 | ext 0xb8/0xc5 music change | +0x10a8 == -1 |
| 9 | ext 0xdc-0xe0 choice (window) | choice_draw_cursor each frame; Up/Down (0x1000/0x4000 repeat, wrap on fresh press) move with sfx 1; Cross: sfx 2, R = selection; Circle: R = -1 + sfx 6 (sfx 7 and stay if cancel not allowed). Window +0x13f/+0x13d close flags |
| 0xb -> 0xc -> 0xd | ext 0xb7/0xbc/0xed battle | voice cache ready (`0x800759a0 >= 3`) and preloads done (FUN_80056c68(1) == `0x8007534c`) -> FUN_80051a34 (calls vm_save_state); then `0x800752d0+0x10`; then `0x8007534d` -> `field_exit_to_program(field, 1)` |
| 0xe | table A 0x3d, B 0x69/0x6d/0x7a/0x7f (menu requests) | `0x80076196 == 0` |
| 7, 0xa | - | unused: no handler, execution continues |

## 5. How events start

The binding table the checklist asks for is not data: **each map script builds it at load time**.

1. **Map entry.** vm_init (FUN_80062260, other range) pushes a frame whose IP is hdr[2] and points IP at
   code word 0, which is always `return`; the first frame's `return` "returns" into the main function.
   (After a battle, vm_restore_state supplies the saved stack instead.) Main (map 4: 0x4fc0) calls common
   setup (A 0x19, B 0x5c menu hook, flags from equipped accessories via B 0x5f), registers regions with ext
   0x74/0x75/0xd9 and actors with ext 0x70 / B 0x6c, starts music, fades in (first fade gives control),
   then loops forever calling an idle function that ends in `wait(1)`.
2. **Doors and exits:** trigger regions whose step-on code preloads the next map (B 0x48), plays effects
   and runs ext 0x7d `map_transfer`. Fired by trigger_check_regions; button-mask regions (ext 0xd9) need
   the configured button while inside.
3. **NPC talk:** ext 0x70 `actor_create(slot, model, x, y, z, dir, param, talk_lo, talk_hi)` passes the talk
   entry (word index), stored at actor+0x52 (pickpocket entry +0x90 via other ops). field_try_talk on
   Cross; the NPC and leader turn to face each other; the talk function runs; window close -> control back.
4. **Chests:** B 0x6c `chest_create(slot, model, x, y, z, param, flag, entry)`: if script flag `flag` is set
   the chest is created open (anim 0x25) with no entry, else closed (anim 0x26) with examine entry `entry`.
   The common chest routine (map 4: 0x1f18) plays the open animation, gives the item or money, calls A 0x01
   (treasure counter) and sets the flag. Examined with the talk button like an NPC.
5. **Private Action:** a region with slot+0xa set (B 0x0e) shows the PA prompt top right; holding config[3]
   (Square by default) sets script flag 2 and jumps to that entry.
6. **Pickpocket:** config[3] near an NPC with +0x90 != -1 when a member has accessory 0x1b5/0x1bc and talent
   bit 2; the script uses B 0x56 (roll) and B 0x59 (relation penalty).
7. **Hooks run synchronously** (vm_call_sync): menu-close entry +0x1198, and actor command-queue entries
   (actor command run by FUN_8003d95c).
8. **World map (maps 909+):** regions are added by the overlay (ext 0x76/0x77 -> 0x800829fc, R = slot) and
   tested by overlay 0x80082b6c; A 0x27 and B 0x0e decorate them.

## 6. Opcode semantics: table A (0xfe) and table B (0xff)

Args: inline/stack counts from script_opcodes.csv unless marked. "char" = character id (0 Claude, 1 Rena...;
party slots store id+1, negative = reserve). Actor = `field_actors[n]`, window = `message_windows[n]`.
Renames in **bold**.

### Table A

| Sub | Name | Args | Semantics |
|---|---|---|---|
| 00 | nop | 0 | |
| 01 | **add_treasure_count** | 1 | game_state+0x248 (s16) += a. Called with 1 by the chest routine in every map (was add_money) |
| 02 | **get_treasure_count** | 0 | R = game_state+0x248 |
| 03 | get_pickpocket_tries | 0 | R = game_state+0x24a (B 0x56 increments) |
| 04 | get_pickpocket_wins | 0 | R = game_state+0x24c |
| 05/06/07/09/0b/0d | get_cfg20/28/24/1c/14/2c | 0 | R = u32 config+0x20/+0x28/+0x24/+0x1c/+0x14/+0x2c (counters in the handoff-2 block) |
| 08 | get_state244 | 0 | R = game_state+0x244 |
| 0a | char_add_1a | 2 | record(a0)+0x1a += a1, clamp 0..999 (active or reserve) |
| 0c | sound_stop_voices | 0 | FUN_8001aac4(sound, 0xffffff) (guess) |
| 0e / 0f | actor_set_8c / actor_set_a0 | 2 | actor[a0]+0x8c / +0xa0 = a1 |
| 10 / 11 / 34 | ovl_8008ea0c / 8008ead8 / 8008eb60 | 0 | overlay 0x8007e000 calls |
| 12 | ovl_effect_8008a7ec | 5 | (a0 & 0xff, (a1,a2,a3)<<12, a4) |
| 13 | ovl_effect_8008d2dc | 12 | (a0 & 0xff, (a1,a2,a3)<<12, a4..a11) |
| 14 | get_equipment | 2 | R = char_get_equipment(a0, a1) |
| 15 | party_compact | 0 | swaps active members up over empty/reserve slots |
| 16 | party_save_order | 0 | game_state+0x280[i] = abs(slot[i].id) |
| 17 | party_reload_changed | 0 | slots whose id differs from the saved order: FUN_800531dc(saved-1, slot) |
| 18 | cfg_set_4e | 1 | config+0x4e = a |
| 19 | set_point | 5 | a0 == 0: game_state +0x26c/+0x28c/+0x270 = a1/a2/a3, +0x27c = a4; else +0x274/+0x290/+0x278, +0x27e. Every map's setup (meaning open) |
| 1a | actor_set_speed | 3 | actor[a0]+0x64 = a1, +0x68 = a2 |
| 1b | actor_attach_effect | 6 | new 0x88 object FUN_8005ee9c(actor[a0], a1..a3, a4&0xff, a5&0xff) into 12-entry list 0x800752ec (guess: emotion icon) |
| 1c | actor_detach_effects | 1 | deletes the list objects attached to actor[a0] |
| 1d | trigger_set_flag6 | 1 | trigger[a]+6 = 1 |
| 1e | **get_mp** | 1 | R = slot(char)+0x24 (was get_hp) |
| 1f | **set_mp** | 2 | slot+0x24 = min(a1, slot+0x22) (was set_hp) |
| 20 | get_max_hp | 1 | R = slot+0x18 |
| 21 | get_skill | 2 | R = (s8) record(a0)+0x3c+a1 (ext 0x73 sets it to 1) |
| 22 | set_status | 2 | slot(a0)+2 = a1 |
| 23 | field_80053158 | 0 | FUN_80053158() (also in exit_to_program) |
| 24 | worldmap_add_entry | 14 | up to 4: {a0,a1} at 0x8007470c+8n, a2..a13 (s16) at 0x80076284+0x18n, count 0x80076282 (maps 909+) |
| 25 | get_killer_move | 3 | R = record(a0)+0xcc + a1*2 + a2 (u8), 0 if absent |
| 26 | get_slot_char | 1 | R = slot[a].id - 1, -1 if empty/reserve |
| 27 | trigger_set_flag7 | 1 | trigger[a]+7 = 1 (world map) |
| 28 | menu_enable | 1 | field_state+0x93 = (a == 0) |
| 29 | get_frame_delta | 0 | R = vsyncs this frame (scripts scale per-frame steps) |
| 2a / 2b | set_76220 / set_7621d | 2 | globals 0x80076220/22 (s16), 0x8007621d/e (u8) |
| 2c | actor_flag_3e1 | 1 | R = actor[a]+0x3e1 != 0 |
| 2d | exit_to_program | 0 | tears the field down and runs the preloaded program |
| 2e | set_state268 | 4 | game_state +0x268/+0x298/+0x29c = a0..a2, +0x28a = a3 |
| 2f, 30 | nop | 0 | |
| 31 | **actor_walk_to** | 5 | queue actor cmd 0x1c: target (a1,a2,a3)<<12, speed a4 |
| 32 | loader_cancel_flag | 0 | loader_cancel; 0x800762ff = 1 |
| 33 / 37 / 40 | ovl_801f9fb8 / 801f9f54 / 801f9a64 | 1/0/1 | overlay 0x801f9800 (R for 33/37) |
| 35 | ovl_801f9800_object | 3 | a0 == 0 destroys object 0x800752e8 (flags 19/20 off), else creates it (0xb0 bytes, +4 a1, +8 a2, flag 20) |
| 36 | set_flag_19 | 0 | script flag 19 = 1 |
| 38 | get_char_5c | 2 | R = (s8) record(a0)+0x5c+a1 |
| 39 | item_count | 1 | R = count of the item's inventory entry (no -1 check: absent item reads entry -1) |
| 3a | actor_set_pos | 4 | actor[a0] pos = (a1,a2,a3) << 12 |
| 3b | cfg_add_14 | 1 | config+0x14 += a, clamp >= 0 |
| 3c | ui_80057734 | 0 | FUN_80057734(0x800752c0) |
| 3d | menu_request_1 | 1 | `0x8007619a` = slot of char, `0x80076196` = 1, wait 0xe |
| 3e | get_80030c20 | 0 | R = FUN_80030c20() |
| 3f | party_slot_refresh | 1 | FUN_80030c64(party_find_slot(a)) (after ext 0xae join) |
| 41-48 | choice_set_lines | 1-8 | ctx+0x11ac[0..15] = scratchpad args 0..15 (lines per option) |

### Table B (handler addresses from `tree.py 80064f58`)

| Sub | Name | Args | Semantics |
|---|---|---|---|
| 00, 08, 17, 52, 72, 7e | nop | 0 | |
| 01 | actor_distance | 2 | R = distance actor a0..a1 (>> 12, SquareRoot0) |
| 02 | name_equal | 2 | R = names game_state+0x28+0x14*a0 and +0x14*a1 equal |
| 03 | msg_close | 1 | window[a]+0x13f = 1 |
| 04 | **actor_color_fade** | 6 | queue actor cmd 0x1b: RGB a1..a3 over a4 frames, mode a5 (+0x7f) |
| 05 | **actor_wait_color** | 1 | queue actor cmd 5: queue waits until the colour fade ends |
| 06 | ovl_8008df74 | 2 | FUN_8008df74(ctx, a0, a1) |
| 07 | get_screen68 | 0 | R = u16 screen_params+0x68 |
| 09 | msg_set_13d | 2 | window[a1]+0x13d = a0 != 0 (close flag; set before ext 0x7f waits) |
| 0a | random | 1 | R = rng % a (0 if a == 0) |
| 0b | ovl_effect_8008a3a8 | 11 | (a0&0xff, (a1..a3)<<12, (a4..a6)<<12, a7..a10) |
| 0c | msg_set_140 | 2 | window[a1]+0x140 = a0 (input flag, toggled with 0x09) |
| 0d | actor_stop | 1 | clears move/fade state and the command queue |
| 0e | **trigger_set_pa** | 2 | trigger[a0]+0xa = a1 (Private Action entry) |
| 0f | mapobj_apply | 3 | FUN_800525ac(field, a0..a2) + actor draw reset |
| 10 / 12 | relation_a_add / relation_b_add | 3 | config+0x58 / +0xe8 [a0*12 + a1] += a2, clamp 0..15 |
| 11 / 13 | relation_a_get / relation_b_get | 2 | R = that byte |
| 14 / 15 | party_restore_hp_all / _mp_all | 0 | |
| 16, 70 | ovl_801eac28 | ? | overlay 0x801ea000 (fetches its own args) |
| 18 | mapobj_refresh | 0 | FUN_80052838(field) |
| 19-20 | mapobj_set_bytes | 3-10 | FUN_800522d4(field, a0, a1, n-0x18 values) |
| 21 / 22 | mapobj_set_frame / mapobj_set_byte2 | 3 | map object entry (a0,a1): byte+3 = a2 (-1 = last) / byte+2 = a2 |
| 23 | actor_reset_pose | 1 | FUN_80041918 |
| 24 | actor_restore_facing | 1 | FUN_80041aa4(actor, 0) |
| 25 / 26 | actor_get_22 / actor_set_22 | 1/2 | u16 actor+0x22 (R -1 if no actor) |
| 27 | actor_set_16 | 2 | actor+0x16 = a1, +0x2ce = -1 |
| 28 | actor_exists | 1 | R = actor[a] != 0 |
| 29 / 2a | party_isolate_hero / party_restore_saved | 0 | |
| 2b / 2c | saved_party_count / saved_party_has | 0/1 | R = field_state+0x6b / char in saved list |
| 2d | music_preload | 2 | entry music_table[a0]; a1 != -1 = owner trigger for the preload |
| 2e / 2f | actor_get_74 / actor_get_76 | 1 | R = s16 actor+0x74 / +0x76 |
| 30 | bg_color_enable | 1 | background clear colour = screen_params+0x6f..0x71 or black |
| 31 | actor_draw_reset | 1 | 0x80075355 = a; a != 0 resets actor draw lists |
| 32 / 33 | actor_view_x / actor_view_y | 1 | R = actor+0x74 / +0x76 minus camera offset (overlay FUN_80086bbc) |
| 34 | leader_can_reach | 3 | R = path test of leader to (a0,a1,a2)<<12 succeeds (2D mode only) |
| 35 / 37 | char_get_status / char_get_hp | 1 | |
| 36 / 38 / 45 | msg_set_139 / _13b / _14b | 2 | window[a1] byte = a0 (style / flags) |
| 39 | actor_create_ex | 5 | FUN_800439d4: overlay class 0x3fc (2D) / 0x44c (3D) or re-init |
| 3a | actor_set_speed2 | 2 | actor+0x64 = +0x68 = a1 |
| 3b | actor_delete | 1 | -1 = all 194 |
| 3c | actor_find_free | 2 | R = first empty index in [a0, a1), 0 if none |
| 3d | actor_set_anim | 2 | actor+0x10 = a1, frame reset |
| 3e | actor_set_74_76 | 3 | actor+0x74 = a1, +0x76 = a2 (logical position used by triggers) |
| 3f | char_get_skill_level | 2 | R = FUN_80030ba8(slot of a0, a1) or -1 |
| 40 / 41 | camera_center_x / _y | 0 | R = camera offset + 0xa0 / + 0x78 |
| 42 | camera_set_target | 2 | camera+8/+0xa = a0/a1; field_state+0xe = -1 |
| 43 / 44 | actor_render_override on / off | 3/0 | 0x80075aba/bc/be, 0x80075356 = 1; off calls camera vtable +0x2c |
| 46 | msg_follow_camera | 2 | FUN_8005e78c(window[a1], a0 != 0) |
| 47 | mapobj_set_entry | 4 | FUN_80052364(field, a0..a3) |
| 48 | map_preload | 2 | FUN_80061888(map a0) -> vm_preload_entry of its data (+section sizes in 3D mode); a1 owner trigger |
| 49 / 4a / 4b / 4c | camera_set_114 / 122 / 124 / 126 | 2/1/1/1 | camera fields |
| 4d / 78 / 7b / 7c / 7d | ovl_effect_8008a684 / 8008a5a8 / 80089ea4 / 8008a2c0 / 8008a4b4 | 15/10/13/11/12 | effects with <<12 vectors |
| 4e | get_61c4c | 0 | R = FUN_80061c4c() (party bitmask) |
| 4f | entry_ready | 1 | if the loader is idle: R = FUN_80011988(loader, 0, table_0x800751ec[a]) |
| 50 | ovl_801eaa30 | ? | 0x800762ff = 0 then overlay |
| 51 / 53 | ovl801ea_loaded / ovl801ea_value | 0 | R = 0x800752c8 != 0 / 0x801ecb90 (or -1) |
| 54 | get_current_map | 0 | R = game_state+0x1a |
| 55 / 62 | set_field_83 / set_field_81 | 1 | field_state bytes |
| 56 | pickpocket_roll | 1 | chance = skill*16/a (+5); tries++; R = rng(100) < chance; wins++ |
| 57 / 58 | actor_free_3d0 / actor_has_3d0 | 1 | attached object at actor+0x3d0 |
| 59 | pickpocket_penalty | 0 | other members in FUN_80061c4c mask lose 1 relation point toward the thief, 10% each |
| 5a | sound_bank_holder_alloc | **0** (csv: unknown) | stops 0x800752e0's stream, restores old handoff 0xf, stores a new sound_bank_holder |
| 5b | sound_bank_holder_free | 0 | restores Sound+0x1c from handoff 0xf, frees the holder |
| 5c | set_menu_close_entry | 3/1 | ctx+0x1198 = entry (inline a1) |
| 5d | char_set_hp | 3/2 | slot+0x1c = min(a1 \| a2<<16, slot+0x18) |
| 5e | has_item | 1 | |
| 5f | **party_has_equipped** | 1 | any active member has the item equipped (was item_query) |
| 60 | give_item | 1 | inventory_add(item, 1, 1); 0x80076278 = 1 |
| 61 | leader_flag_3e1 | 0 | R = leader+0x3e1 != 0 |
| 63 | **remove_item** | 2 | inventory_set_count(find(a0), a1) **subtracts** a1 (was set_item_count) |
| 64 | actor_get_anim | 1 | R = actor+0x10 |
| 65 / 68 / 6e / 77 | camera_86d20 / 86e70 / 86bf8 / 86c4c | 2/1/1/2 | overlay camera calls (R for 68/6e) |
| 66 | marker_create | 4 | actor 0x80 (model 0x7ffe) at (a0,a1,a2), param a3 |
| 67 | set_76195 | 1 | |
| 69 / 6d / 7a / 7f | menu_request_5 / 2 / 4 / 3 | 0/1/1/1 | `0x80076196` = n (param `0x8007619a` = a or its party slot); wait 0xe |
| 6a | disc_change_2 | 0 | config+0x4c = 1; FUN_80052ce8(field, 2, 1) -> disc_change_screen(2) |
| 6b | actor_set_3f0 | 3 | FUN_80042ffc |
| 6c | **chest_create** | 8 | section 5 |
| 6f | ovl_801ea374_reload | 8 | if the overlay is absent: FUN_801ea374 then reload current map; else R = 0 |
| 71 | pad_vibrate | 3 | FUN_80013e14(pad, a0..a2) |
| 73 / 74 | char_unequip_slot / char_set_equipment | 2/3 | |
| 75 | relation_hash | 1 | R = relation_hash_mod(a) |
| 76 | best_friend_of | 1 (+2) | R = member with the highest relation sum toward a (ties by hash); **falls through into 0x79** and fetches two more args (missing break). Unused in scripts |
| 79 | actor_set_7f | 2 | actor+0x7f = a1 |

### Ext ops tied to the VM core (implemented in vm_exec_ext, other range)

| Op | Name | Notes |
|---|---|---|
| 0x70 | actor_create | (slot, model, x, y, z, dir, param, talk_lo, talk_hi); talk entry -> actor+0x52 |
| 0x74 / 0x75 | **trigger_add / trigger_add_y** | inline 11/13: corners a0..a7, a8 unused, code a9 \| a10<<16, (y a11..a12); stack 9/11: code a8. R = slot |
| 0xd8 / 0xd9 | **trigger_add_button(_y)** | mask a1 \| a2<<16, corners a3..a10, code a11 \| a12<<16, y a13/a14 (stack: mask a0, corners a1..a8, code a9) |
| 0x9a | trigger_set_dwell | slot+5 = 1, +8 = frames |
| 0xa3 | trigger_remove | |
| 0x76 / 0x77 | worldmap trigger add | overlay 0x800829fc |
| 0x7a | wait | ctx+0xc += a |
| 0x7b / 0x84 | player_lock / player_unlock | field_state+0x26; unlock gives control to the leader |
| 0x7c / 0xb5 | fade | first one after vm_init hands control to the hero |
| 0xbb | **mapobj_load_wait** | loads entry table_0x80077384[a] (0xfc9..) into the map-object slots, wait 6. Not a map transfer |

## 7. Other subsystems in the range

- **Party / characters** (0x80068f10-0x8006ab4c, 0x8007a20c-0x8007b1f0). PartySlot 0x60 at 0x8007527c:
  +0 s16 id+1 (negative reserve, 0 empty), +2 u8 status bits (bit 0 cleared by full heal), +3 u8 class
  (>= 9 casters: Rena 9, Leon 0xa, Celine 0xb, Noel 0xc), +0x14/+0x18/+0x1c HP base/max/current, +0x20/+0x22/+0x24
  MP, +0x26 level-like (battle voice average), +0x28 level. Character 0xd0 at 0x80075280: +0xc..+0x18 equipment
  slots 0-6 (5/6 = accessories), +0x1a s16 0..999, +0x20 talents, +0x24 name, +0x3c u8[0x20] learned
  killer moves / spells (by the move's or the class spell list's index; 1 learned, negative switched
  off; ext 0x73 / 0xfc and A 0x21 index it with a script value, unchecked: field 0x80064eb8),
  +0x5c u8[0x30] skill levels (by skill id), +0x8c u16[0x20] move proficiency, +0xcc u8[2][2]
  assigned killer moves. Ids: 1 Claude, 2 Rena, 3 Celine, 4 Bowman, 5 Dias,
  6 Precis, 7 Ashton, 8 Leon, 9 Opera, 10 Ernest, 11 Noel, 12 Chisato. Script flag 1 = Claude's story.
  Every party change of slots 0-3 calls voice_slot_invalidate + FUN_8004358c(0x800752b0, 0) and clears
  `0x800759a0`.
- **Relations**: config (handoff 2) +0x58 and +0xe8, two 12x12 byte matrices (who-likes-whom), clamped 0..15
  by the script ops.
- **Battle voice / SPU queue** (0x80078e0c-0x80079e58): before a battle the party's killer-move voice banks are
  streamed to SPU RAM. Queue 0x8007bfa0[24] entries / 0x8007bfd0[24] targets (slot | move<<4 | shared<<8),
  0x8007c000 loading, 0x8007c001 mode (0 battle, 1 field clips), 0x8007c004 read buffer, 0x8007c008 buffer
  in transfer. `0x800759a0` cache state, `0x80075998` budget.
- **Battle load list** (0x800788b0-0x80078d18): BattleLoad at 0x800752b8: +0 encounter, +4 background, +8 list
  of 13 entries, +0x3c.. ids, +0x70 count, +0xa8 13 flag bytes, +0xb8/+0xbc intro speakers.
- **Window base class** (vtable 0x80074038, 0x28 bytes; every user overrides with 0x80072e5c): +8 rect,
  +0x10 u8 line height 0xc, +0x14/+0x18 visible flags. Same layout as title Window.
- **Disc change** (0x8006f958-0x8006fc64): DiscPrompt 0xc bytes {+0 prompt pack #0x11, +4 background, +8 SLZ ctx}.
- **PsyQ**: libgte (InitGeom, SetFogNearFar, SetDQA/B, matrices, RotTransPers4, ratan2, TransRot_32,
  _patch_gte), RotMatrix/X/Y/Z and sin4096/cos4096 compiled as C, libcard BIOS stubs, libapi file I/O and
  the firstfile workaround, libc strcmp/strcpy. Data 0x80071600-0x800788b0 holds vtables, tables and the
  sin table 0x80077398.

## 8. Globals

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x800752ac | g_script_vm | ScriptCtx * | the VM |
| 0x800756f8 | script_blob | u32 * | unpacked map script (header at +0) |
| 0x800756fc / 0x80075700 | script_call_stack / script_value_stack | u32 * | stack bases |
| 0x8007570c | field_state | FieldState * | +4/+6/+8/+0xc/+0x34/+0x36 saved VM offsets; +0xe s16; +0x10 s16 controlled actor (-1 = event has control); +0x24 u8 resume-VM flag; +0x26 input lock; +0x28 s16[4] saved map objects; +0x32/+0x33 countdown; +0x68 3D/world mode; +0x6a count; +0x6b/+0x6c saved party; +0x7e battle param; +0x81/+0x83 script bytes; +0x93 menu disabled |
| 0x80075710 | game_state | GameState * | +8/+0xc/+0x10 arrival pos; +0x1a s16 current map; +0x1c s16 current music; +0x1e s16 (ext 0xa4); +0x24 u8 leader actor; +0x28 names (0x14 each); +0x248 treasure count; +0x24a/+0x24c pickpocket tries/wins; +0x268.. points (A 0x19/0x2e); +0x280 u8[8] saved party order |
| 0x80075270 | config | Config * | handoff 2 (0x2a0): +0/+2/+4/+6 buttons confirm 0x40 / cancel 0x20 / menu 0x10 / PA-pickpocket 0x80, +8..+0xe L1 R1 L2 R2; +0x14..+0x2c counters; +0x30..+0x3c window colours; +0x4c disc 2; +0x4e; +0x58 / +0xe8 relations; +0x181/+0x182 |
| 0x80075748 / 0x8007574c / 0x80075750 | pad_held / pad_pressed / pad_repeat | u16 | port 0, latched once per frame |
| 0x8007531c | frame_delta | s32 | vsyncs since the last frame (A 0x29) |
| 0x80075324 | frame_counter | s32 | blink timer |
| 0x800759a4 | vm_suspended | u8 | disc error shown; VM does nothing |
| 0x80076196 / 0x8007619a | menu_request / menu_param | u8 / s16 | menus requested by scripts (wait 0xe) |
| 0x8007528c | g_camera | Camera * | overlay object (vtable; +8/+0xa target; +0x114..+0x126) |
| 0x800752b4 | g_camera_script | ? | ext 0x85-0x93 queue; wait 4 |
| 0x80075290 | g_render | Render * | +0xf8 render context (OT at +0x70), +0xfc / +0x100 primitive pointers |
| 0x80075294 | g_actor_mgr | ActorMgr * | create (FUN_80043890), delete (FUN_8004541c) |
| 0x80075298 | g_fade | Fade * | FUN_800471f4 start, FUN_80047260 busy |
| 0x800752b0 | g_task | Task * | voice/model refresh (FUN_8004358c), pumped via vtable +0xc while waiting |
| 0x800752b8 | g_battle_load | BattleLoad * | |
| 0x800752c8 | ovl801ea_loaded | int | |
| 0x800752e0 | g_stream | ? | stopped by B 0x5a / music change |
| 0x800752e8 | ovl801f9800_obj | void * | A 0x35 |
| 0x800752ec | attached_effects | void *[12] | A 0x1b/0x1c |
| 0x80075328 / 0x80075330 | ui_atlas / screen_params | | UV table; +0x34/+0x36 screen size, +0x68, +0x6a projection, +0x6f..+0x71 bg colour |
| 0x8007533c / 0x8007533d | preload flags | u8 | |
| 0x8007534c / 0x8007534d | battle preload count / ready | u8 | |
| 0x80075766 | talk_reach | s16 | probe distance (x 0.9) |
| 0x800759a0 / 0x80075998 | voice_cache_state / voice_budget | | |
| 0x80075360 | field_actors | Actor *[194] | (existing); [0x80] = 0x80075560 marker |
| 0x80074028 / 0x80074038 | ScriptVM vtable / Window vtable | | |
| 0x8007380c | table_a_jump | u32[0x48] | |
| 0x80077384 | mapobj_table | s16[] | ext 0xbb entries (0xfc9..); not map ids |
| 0x80077398 | sin_table | u16[0x401] | |
| 0x8007bfa0 / 0x8007bfd0 | voice_queue / voice_queue_target | s16[24] | |

## 9. Corrections to existing catalog entries

- formats.md "ctx+0x4c + n*0x48 thread slots" -> trigger regions; header hdr[2] = main entry, hdr[5]/hdr[6] =
  global var/flag sizes; add the `[indexed]` operand rule.
- vm_alloc_thread -> trigger_alloc_slot; ext 0x74/0x75 -> trigger_add; ext 0xbb is a map-object load, not
  map_transfer_table; globals.csv 0x80077384 likewise.
- Table A 0x01/0x02 = treasure counter, not money; 0x1e/0x1f = MP, not HP. Table B 0x63 = remove_item;
  0x5f = party_has_equipped; 0x5a takes 0 args (fix `tools/so2_script_args.json` "ff" 0x5a -> [0, 0]).
- functions.csv 0x8003c798 `inventory_set_count` subtracts: rename inventory_remove_at (other range).
- `field_flags` 0x8007570c is the field state block above; `game_counters` 0x80075270 is the config block.

## 10. Porting notes

- Port the VM as one context + 58 triggers, exactly in this order per frame: triggers -> wait counter ->
  music -> wait state -> instructions. The one-frame delay before an event (step 4) and the restart after
  map transfer (same frame) are observable.
- Keep script memory byte-addressed (globals/flags shared with the save game; locals live inside the same
  arrays at hdr[5]/hdr[6] and grow per frame). Store IP and stacks as offsets so vm_save_state is trivial.
- vm_call_sync re-enters the update recursively; a native port can run the interpreter loop to depth 0 with
  waits ignored.
- Bug compatibility: trigger_alloc_slot overflow (59th trigger corrupts +0x109c), table B 0x76 fall-through,
  char_set_equipment passing the item id as inventory index, A 0x39 reading entry -1, clamps 0..15 on
  relations. Decide per case; none is hit by the shipped scripts except possibly the equipment one (B 0x74,
  82 uses).
- Overlays: about 30 table entries jump into overlay code (0x8007e000 camera/actor classes and effects,
  0x8008xxxx, 0x801ea000, 0x801f9800); those opcodes need the overlay ports.
- Dependencies on other ranges: vm_exec_ext and vm_init (0x8006241c / 0x80062260), field_load_map, the actor
  class (command queue at +0xa2, interpreter FUN_8003d95c), message windows (FUN_80058b00 / msg_window_open),
  fade, map objects (FUN_80052xxx, FUN_8004a654/a830), loader preload (FUN_800563a0, FUN_80056830), sound
  (boot 0x80018f04.. / 0x8001ab48/58), field update FUN_8004c334 (menu, battle start, world-map triggers).
