# Battle program (archive entry #2129)

Summary of the battle program's structure. Details live in the range notes `parts/p1.md` ... `parts/p9.md`;
the names are in `battle.csv`, `battle_overlays.csv` and `battle_globals.csv`.

## 1. Identification

Entry #2129 (`0x851`) is the **battle program**. Confirmed by:

- the field queues it in `field_start_battle` (field 0x80051a34: `loader_entry_size(loader, 0x851)`, `mem_alloc_b`,
  `LoadQueue` read into `DAT_80075350`) and runs it from `field_exit_to_program` (field 0x80051c9c:
  `boot_run_main_program(system, DAT_80075350, 1)`) after storing the battle setup in handoff 5;
- the program reads that setup (handoff 5: encounter id, stage id, the archive entries the field preloaded),
  builds party units from handoffs 3/4 and enemy units from the formation entry, runs a fight loop with
  victory / defeat / escape / time-up outcomes, writes the outcome to `config+0x178` (which the field copies into
  the event script's return register R) and swaps back to the field program (entry `0xa10` = #2576 = #3100).

It is one SLZ method-2 block, 381148 (`0x5d0dc`) bytes unpacked, loaded at `0x8002f810` (end `0x8008c8ec`), like the
title (#1) and the field (#3100). Image used here: `analysis/archive/disc1/2129.0.unpacked`.

## 2. Memory map and code overlays

| Range | Holds | Lifetime |
|---|---|---|
| `0x80010000-0x8002f800` | boot EXE (`boot` module): loader, SLZ, heap, pad, sound, PsyQ libs | always |
| `0x8002f810-0x8006b054` | resident battle code | whole battle |
| `0x8006b054-0x800715d0` | resident data: rodata, vtables (`0x8006b030-0x8006d200`), globals (`0x8006e000-0x80070000`) | whole battle |
| `0x800715d0-0x80081728` | #2129 init code (`battle_run`, data loaders, monster factory) then **#1984 block 0** (main battle code, 65880 bytes) | init code until `battle_main_loop` starts |
| `0x80081728-0x80085518` | #2129 party-data loader task code, then **#1984 block 1** (15856 bytes) | loader until battle state 1 |
| `0x80085518-0x80088054` | #2129 spell-cast code (0x80085560 spell object API, used by resident code and by party / enemy modules), then **#1981** (result screen + exit, 11068 bytes) | spell code until state 4, then #1981 |
| `0x80088054-0x8008c1f4` | **enemy-code slots**: A `0x80088110`, B `0x80089f18`, C `0x8008b1f0`, D `0x80088900` (Indalecio only). The image holds default modules in A / B / C (still required by 8 encounters); the encounter pack replaces them | whole battle |
| `0x8008c1f4-...` | **stage code slot**: #1984 blocks 2-10 for special stages; on stage 0xe (no block) the image's own function there is the Shot kind-0x19 handler | whole battle |
| `0x8008cc00`, `0x8008e600` | **spell code slots** (two): spell code blocks unpacked by the spell-cast API (0x80085560, slots tracked at 0x8006fe08) | per spell |
| heap | per-member model buffers (0xb000 each), pools, two 0x10c00 load buffers, ... | |
| `0x801ee000-0x801fe000` | **party-character code slots**: 4 x 0x4000 (slot k at `0x801ee000 + 0x4000*k`), a 0x10000 heap block forced to this address | whole battle |

The overlay code is unpacked with `slz_unpack(b_slz_ctx, packed, dest, block)` followed by `icache_flush`
(boot 0x8001054c: runs 4 KB of nops to flush the R3000 instruction cache).

- **#1984** (load-list index 9, copied to `g_ovl_battle_code` 0x8006e8c0): an 11-block SLZ chain. Block 0 -> 0x800715d0
  when `battle_main_loop` starts; block 1 -> 0x80081728 when the battle state is 1; blocks 2-10 -> 0x8008c1f4 by
  stage id (`0x1a`:2, `0x1f`:3, `0x10`:4, `0xd`/`0x38`/`0x39`:5, `0x17`:6, `0x15`/`0x28`:7, `0x24`:8, `0x29`:9, `0x33`:10),
  unpacked in `battle_run`.
- **#1981** (load-list index 5, copied to `g_ovl_result_code` 0x8006e8c4): one block -> 0x80085518 when the victory
  result starts.
- **Party-character code**: entries #2131, #2133, #2135, #2137, #2139, ... hold four SLZ blocks each: the same code
  linked for the four party slots (internal calls go to `0x801ee05c`, `0x801f205c`, `0x801f605c`, `0x801fa05c`). The
  party-data loader task unpacks the right variant to `slot_code_base[slot]` (table `0x800851b0`:
  0x801ee000, 0x801f2000, 0x801f6000, 0x801fa000). The even entries (#2130, #2132, ...) are the matching data.
- **Enemy code**: comes from the encounter pack itself (archive entry 0x57d + encounter = #1405-#1968; load-list
  index 7). Its code list names SLZ blocks by kind: 0 -> slot A 0x80088110, 1 -> slot B 0x80089f18, 2 -> slot C
  0x8008b1f0, 3 -> 0x80088900 (Indalecio only). Offset 0 of each block is the constructor of an EnemyUnit
  subclass; the monster factory 0x80075ad8 calls it (or stores a vtable inside the module). E.g. #1942
  (encounter 537) has a block @0x21bf8 for slot A and @0x228bc for slot B.

**Caution: much of the #2129 image above 0x800715d0 is not live code** (`init_image_map.md`, parts/p6.md, p7.md):

| Image range | Status |
|---|---|
| 0x800715d0-0x800753bc, 0x80075ad8-0x800769d8 | live, init phase (battle_run, loaders, monster factory, stats reader) |
| 0x800753bc-0x80075ad8, 0x800769d8-0x80078f38 | dead: a copy of the field's voice / party library linked into the image, never called |
| 0x80078f38-0x8007a4b0 | data (jump tables, BattleProgram vtable, voice tables) |
| 0x8007a4b0-0x8007b810 | stale copy of the image's own party-loader code |
| 0x8007b810-0x8007cadc, 0x8007d810-0x8007de84, 0x8007f810-0x800801d4 | stale enemy code blocks (#1942, #1933, #1467 family; linked for slot A) |
| 0x8007cc10-0x8007d810, 0x8007de84-0x80081160, 0x800851c0-0x80085518 | stale party-character code (#2147, #2131, #2139) |
| 0x80081160-0x80081728 | stale, unattributed |
| 0x80081728-0x80084b00 | live until state 1: party-data loader, party construction, equipment stats |
| 0x80085518-0x80088110 | live until the result screen: SpellCast system |
| 0x80088110-0x8008c1f4 | live: default enemy modules in slots A / B / C |
| 0x8008c1f4-0x8008c8ec | stage-0xe Shot handler (live on that stage only) |

Resident code calls into 0x800715d0-0x80081728 always mean #1984 block 0. Ghidra's call-target recovery created
bogus function starts at those addresses inside the init code; the eight big ones were merged back, and the rest are
listed in `battle_index_artifacts.csv`. Two decompile views exist: `analysis/decomp/battle` (image as loaded) and
`analysis/decomp/battle_ovl` (runtime view with #1984.0, #1984.1 and #1981 patched in).

## 3. Entry, init and main loop

```
battle_entry (0x8002f810)                      same code as the title / field entry
  battle_program_create (0x800715d0)           new BattleProgram (12 bytes, vtable 0x80079098) -> g_current_program
  vtable[2] program_set_system(sys)
  vtable[1] battle_run (0x8007160c)
    bind System pointers (0x8006e8a0..0x8006e8bc); screen_init; pad_set_option(pad, 1); ClearImage(VRAM)
    copy handoff 5 (0xc0 bytes) to the stack, handoff_clear(5), free it
    g_encounter_id = setup+0 ; g_stage_id = setup+4 ; b_config = handoff 2 ; b_inventory = handoff 6
    g_preload_list = new 0x22 bytes, rebuilt by 0x8006a11c (handed back in handoff 0xc)
    if inventory_verify(inventory) != 1: load entry 0x542, show it full screen, hang (anti-tamper)
    battle_init_state (0x80074a74) ; battle_init_objects (0x80074168) ; stage_load_ctx_init
    for each load-list index i while setup+0x3c[i] != 0 (entry = setup+8[i]):
        data = setup+0x74+4i (preloaded by the field) or loader_load_sync into mem_alloc_b
        switch (i): 0 load_battle_music_pack  1 load_victory_music  2 load_stage  3 load_ui_sprites (#1979)
                    4 load_battle_text (#1980)  5 load_result_code (#1981)  6 load_item_data (#1983)
                    7 load_formation (0x8007344c: encounter pack; monster factory 0x80075ad8)
                    8 load_effect_sprites (#1982)  9 load_battle_code (#1984)  10/11 load_intro_voice
        start the battle music once the bank is in (unless config+0x180)
        free the buffer if the battle loaded it
    free preloaded buffers flagged at setup+0xa8+i
    stage code block (#1984 block 2-10) for special stages; stage objects (0x800543bc kinds 0x19..0x2b)
    voice / sfx id overrides (config+0x4e, random, encounters 0, 0x219-0x21c)
    delete self ; battle_main_loop()
battle_main_loop (0x800459fc), never returns
    unpack #1984 block 0 -> 0x800715d0 ; force the 0x10000 block at 0x801ee000 (party code slots)
    move #1984 and a battle buffer to fresh heap blocks (inventory+0x820 = that buffer)
    party_loader_create (0x80082bd8, 0x54 bytes) registered at priority 0 ; battle_separate_units ; battle_scene_start
    open the HUD window g_hud (0x8006fa9c)
    loop:
        GetRCnt ; screen_flip ; text_frame_begin ; clear 12 words at 0x8006fe10 ; deferred_delete_update
        clear the OTs of the 8 layer objects (g_ot_layers)
        freeze timers (0x8006fb15 / 0x8006fb16 set flag 8 of g_battle_flags)
        end checks while fighting (state 2), see 3.2
        bobj_texcache_begin_frame (when g_special_obj0) ; voice_update ; bload_update ; spell_data_update ;
        battle_frame_nop ; input_update(g_input)
        camera vtable[1] ; camera_update_matrices ; battle_frame_director (#1984.0 at 0x800715d0) ; bsfx_update
        lens_flare_draw when the stage has a sun
        state 4 and the result screen released (g_result_screen == 0, camera idle): battle_fade_task_ctor (0x800872f8, #1981) once
        victory jingle (g_victory_music) once the result screen is up
        tasks by priority 0..4 (vtable[1] of each g_tasks entry)
        state 1: unpack #1984 block 1 -> 0x80081728, free #1984
        pools: 160 x 0x13c objects, 296 x 0xb8 objects (vtable[1] when active), 200 x 0x48 records (0x8005e1b0)
        stage_draw_and_animate unless 0x8006f001 ; state > 1: clash_resolve (#1984.1)
        end countdown (g_end_countdown, 90 frames) then state 4: battle_free_party_models; preload the field
            program (#2576) and, if it fits, park it in VRAM; unpack #1981 -> 0x80085518; result_screen_ctor
            (0x80085f4c, 0x1ec bytes) ; vtable[6] result_screen_open
        text_frame_upload ; screen_compact_ot(layer 0) ; DrawSync ; VSync(2)  (30 fps)
        g_frame_ticks = vblanks since last frame ; g_battle_frames += ; g_fight_frames += in state 2
        both DRAWENV clear colours = stage clear colour (0x8006eff7, or 0x8006effa for a band of pitches) x stage
            brightness ; screen_put_env ; SetDispMask(1) after the delay
        draw the 8 OT layers from 7 to 0
    state > 4: battle_exit_to_field (0x80087644, #1981)
```

### 3.1 Battle states (`g_battle_state` 0x8006efec)

| State | Meaning | Set by |
|---|---|---|
| 0 | loading: the party-data loader task streams each member's model / code / voices | battle_init_state |
| 1 | loading done: #1984 block 1 is unpacked over the loader code; camera intro (mode 1 etc.), party appears | loader task 0x80082cdc |
| 2 | fighting | `pc_appear_update` after the intro (members flicker in for 40 ticks) |
| 3 | battle over: music fade (0x1e0), `g_end_countdown` = 90 | end checks |
| 4 | victory result screen (#1981) | after the countdown |
| 5 | leave: battle_exit_to_field | `battle_fade_task` (#1981, 0x14 bytes) after the result screen released itself: black band, darken, state 5 |

### 3.2 End conditions and outcome (`g_battle_outcome` 0x8006fb19 -> config+0x178)

Checked in state 2 when the battle is not frozen (`g_battle_flags & 0x40` clear):

| Outcome | Condition |
|---|---|
| 0 victory | all enemies down (result screen; after a 90-frame countdown that also waits for every enemy body / effect to finish) |
| 1 escaped | `party_count_standing() == 0` but some members fled (`unit+0x12f == 2`) and escape is allowed (0x8006f6cf) |
| 2 defeat | `party_count_standing() == 0` otherwise (also forced at exit if every member is down) |
| 3 ran off | rule `config+0x181 == 2`: an enemy came within 0x30000 of (0x64, 0, 0x12c) (escape race) |
| 4 time up | rule `config+0x181 == 3`: `g_rule_timer` passed 0xe10 frames (60 s at 60 Hz ticks) |

`unit_is_down(u)` (0x80053e50) is true for a missing unit, a status with bits 0-2 set (`*(u->+0x1fc)+2 & 7`) or
`u->+0x12f != 0`. The field reads the outcome back as the event script's return register (`R = config+0x178`;
outcome 1 also increments `game_state+0x244`).

## 4. Field -> battle handoff

### 4.1 Transition (field side)

`field_update` (field 0x8004c334):

- state 0: random encounter: `field_prepare_encounter` (0x80050f64) has already picked a formation from the
  area's encounter table and queued its archive entries for background preloading; when the walking distance
  passes the threshold and the preloads are far enough (`0x800759a0 >= 3`), `field_start_battle(f, transition)`
  -> state 4.
- state 0x21: forced battle (scripts / menu: encounter `-game_state+0x99`, stage `field_flags+0x12`):
  `field_prepare_encounter`, `preload_start` for each entry, `field_start_battle(f, 2)` -> state 2.
- states 2 / 4: when the BattleTransition effect reports done (`+0x10`), **`field_destroy_battle_transition`
  (0x80051c68)** deletes it, drawing is disabled, state 5. (0x80051c68 is only this cleanup; it is not the battle
  start.)
- state 5: when #2129 has finished loading (`DAT_8007534d`) and every preload is complete, `field_flags+0x7e = 1`,
  **`field_exit_to_program(f, 0)`** (0x80051c9c).

`field_start_battle` (0x80051a34): validates the party, unloads the map, frees the map buffer, loads #2129
(0x851) asynchronously into `DAT_80075350`, saves the VM state (handoff 0xd) and the actors (handoff 0xb),
records the leader's position, clears tasks, creates the BattleTransition (type 0 swirl, 1 split, 2 long swirl),
sfx 0x47, music fade 0x46, 30 fps, stashes party slot ids.

`field_exit_to_program(f, 0)`: config+0x181 = 0 (normal rule), swap counter++, Callways song cleanup and battle
bonuses, field_flags+0x24 = 1 (returning), frees the map, copies the battle setup to a fresh 0xc0 block,
item table -> handoff 0x11, actor snapshot -> handoff 0xb, blob 0x80075728 -> handoff 0x10, VM state -> handoff
0xd, sound bank holder -> handoff 0xf, the preloaded entries' buffers -> setup+0x74.. (and the "owns buffer"
flags at setup+0xa8..), **setup -> handoff 5**, deletes the field and calls `boot_run_main_program`.

### 4.2 Battle setup block (handoff 5, 0xc0 bytes; built by field `battle_build_load_list` 0x800788b0)

| Off | Type | Meaning |
|---|---|---|
| +0x00 | s32 | encounter (formation) id; `g_encounter_id` |
| +0x04 | s32 | stage (battle background) id; `g_stage_id` |
| +0x08 | s32[13] | archive entries in load-list order (copy of +0x3c before the field sorted it), 0-terminated |
| +0x3c | s32[13] | the same entries, sorted by the field for preloading (negated when they did not fit); the battle only uses it as the list terminator (loop while +0x3c[i] != 0) |
| +0x70 | s32 | entry count: 9, 11 or 12 |
| +0x74 | void*[13] | preloaded data per load-list index (0 = the battle loads it itself) |
| +0xa8 | u8[13] | the battle frees the buffer after use (first piece of a multi-entry preload) |
| +0xb8 / +0xbc | s32 | party slots chosen for the two intro voices (`battle_pick_intro_voices`) |

Load-list (index: entry, battle handler):

| i | Entry | Handler (init code) | Contents |
|---|---|---|---|
| 0 | #1977 (`0x7b9`), #1969 for 31 boss encounters (table 0x8007b91c), or an override (pairs at 0x8007b95c) | `load_battle_music_pack` 0x80072190 | u32 bank offset; SLZ music sequence at +4; sound bank at +bank offset |
| 1 | #1978 (`0x7ba`) | `load_victory_music` 0x8007221c | SLZ victory music sequence |
| 2 | #1347 + stage id (`0x543 + stage`) | `load_stage` 0x80072270 | stage command list (textures, models, lights, fog, positions) |
| 3 | #1979 (`0x7bb`) | `load_ui_sprites` 0x80072f70 | two sprite banks (UI / HUD sprites): textures to VRAM (0x3c0, 0) and (0x3c0, 0x100), CLUT rows 0x1fe / 0x1f5 / 0x1ff |
| 4 | #1980 (`0x7bc`) | `load_battle_text` 0x80074fd0 | string table, font, text CLUT (row 0x1f9) |
| 5 | #1981 (`0x7bd`) | `load_result_code` 0x800752c0 | result-screen code overlay |
| 6 | #1983 (`0x7bf`) | `load_item_data` 0x800750b8 | item table: 1023 records x 0x30 bytes (item id - 1) |
| 7 | #1405 + encounter (`0x57d + id`) | encounter loader 0x8007344c | encounter pack: sprite sets, CLUTs, enemy code blocks, formation, per-monster stat records (6.4) |
| 8 | #1982 (`0x7be`) | `load_effect_sprites` 0x800731f4 | common effect sprite bank (g_special_obj0's sprites) |
| 9 | #1984 (`0x7c0`) | `load_battle_code` 0x80075214 | battle code chain (section 2) |
| 10, 11 | intro voice banks (per character table field 0x8007be00) | `load_intro_voice` 0x8007536c | voice clip bank |

### 4.3 Handoff slots used by the battle

| Slot | Read | Written |
|---|---|---|
| 2 config (0x2a0) | +0x41 leader slot, +0x4e, +0x17e, +0x180 no-music, +0x181 battle rule | +0x178 outcome (written in place) |
| 3 party slots (8 x 0x60) | slots 0-3 build the party units (`g_party_slot`) | HP / MP / stats after the battle, in place (result code) |
| 4 character records (8 x 0xd0) | equipment, skills, killer-move assignments +0xcc.. (`g_party_record`) | experience / levels, in place (result code) |
| 5 battle setup | everything above; freed at start | - |
| 6 inventory (0xc28) | `inventory_verify`, item data, +0x820 | items used / won |
| 9 field state block | by 0x8006a11c / 0x8006a4e0 | - |
| 0xc | - | `g_preload_list` (0x22 bytes): entries the field should reload |
| 10 | - | `g_handoff10_blob` (0x8006fdfc) |

Untouched (field-private, survive the battle): 0xb actor snapshot, 0xd VM state, 0xf sound bank holder,
0x10 blob, 0x11 item table, 0xe camp-menu snapshot. The only `handoff_store` calls in live battle code are the two in
`battle_exit_to_field`; the image's 0x80075918 / 0x80075944 / 0x80077ab0 (which store 2/3/4/6) are stale copies
of other code and never run (at run time 0x80075944 is a #1984.0 constructor).

### 4.4 Battle -> field

`battle_exit_to_field` (0x80087644, #1981 code): if the outcome is 0 but all four members are down and none fled,
outcome = 2; vibration off; `config+0x178 = outcome`; free the 0x801ee000 block; close the message window
(0x8006faa8); stop voices (0x80048994); free everything (0x80087884); flush deferred deletes; `ResetGraph(1)`;
`SetDispMask(0)`; make sure the field program (entry 0xa10) is loaded (read it back from VRAM if it was parked
there during the result screen); handoffs 0xc and 10; `boot_run_main_program(system, field_program, 1)`.

## 5. Classes, vtables and object layouts

g++ 2.x vtables: 8-byte entries `{s16 delta, s16 0, u32 fn}`, entry 0 zero, vptr at object +4; "slot n" = entry
at vptr + 8n (the per-frame update is slot 1). Full slot tables and layouts are in `parts/p1.md` (PartyUnit,
Camera, Sprite), `parts/p2.md` (EnemyUnit and families), `parts/p3.md` (Unit base), `parts/p4.md` (Shot and
pools), `parts/p5.md` (windows, HUD), `parts/p8.md` (menus, effect tasks), `parts/p9.md` (result screen).

### 5.1 Hierarchy

```
BObj 0x88 (vt 0x8006cd10, ctor 0x80041d74)            position 20.12 at +8, velocity +0x28.., screen +0x38/+0x3a,
 |                                                     anim set +0x5c, flags +0x69, scale +0x74/+0x78, RenderContext +0x84
 +-- Collider 0x108 (vt 0x8006b4a8)                    body boxes +0x88/+0xa8, attack boxes +0xc8/+0xe8 (enable bits +0x6a)
 |    +-- Unit 0x200 (vt 0x8006cde0, 21 slots, ctor 0x8004dd30, dtor 0x8004def0; copy ops 0x80054308/0x8005436c)
 |    |    +-- PartyUnit (vt 0x8006b6d0, 46 slots, ctor 0x80034ed0, >= 0x360 bytes)   player characters
 |    |    |      subclass ctor + overrides live in the character module (0x801ee000 + 0x4000*slot)
 |    |    +-- EnemyUnit (vt 0x8006b908, 36 slots, ctor 0x8003c8c8, 0x26c bytes)      monsters
 |    |           16 family subclasses with resident vtables + modules in the enemy-code slots
 |    +-- Shot 0x13c (vt 0x8006ce90)                    effect / projectile actor, 160 in g_pool_13c, kind +0x108
 +-- Sprite 0xb8 (vt 0x8006b030)                       effect / HUD sprite, 296 in g_pool_b8
Task-like objects (registered with task_register(obj, prio), slot 1 called by battle_main_loop):
 Window (vt 0x8006d160, title layout; RenderContext +0x1c) -> BattleHud 0x88 (0x8006d130), SpellClashWindow 0x30
   (0x8006cfa4), SkillNoticeWindow 0x88 (0x8006d0a8), ScanWindow 0x3c (0x8006d0e8), DebugWindow 0x470 (0x8006b848),
   ListMenu (0x80080fec) -> SpellMenu (0x80080e9c), ItemMenu (0x80080f04), TacticsMenu (0x8008103c);
   MessageWindow 0x30 (0x80080f7c); ResultScreen 0x1ec (0x80087f5c, #1981)
 DamagePopup 0x2c (0x8006cf64, 16), PolyShard 0x74 (0x8006cf94, 192), Afterimage / Ghost / RibbonTrail / LineTrail
   (0x80080fdc / fcc / fbc / fac, #1984.0), party-data loader task 0x54 (0x80084e9c), battle_fade_task 0x14
   (0x80087f4c), VsyncCounter 0xc (0x8006cd50)
Plain records: Particle 0x48 (200 in g_pool_48, kind table 0x8006ffe8), Camera 0xa4 (vt 0x8006b484, g_camera)
RenderContext ("PrimPool") 16 bytes: +4 arena slot, +8 OT slot, +0xc OT head; pool A 62000 bytes / 0x621 OT
 (3-D world), pool B 0xc80 / 12 (HUD sprites), one per window; registered in g_ot_layers[8], drawn 7 -> 0.
```

### 5.2 Unit fields every subsystem uses (details: parts/p1.md, p3.md)

| Off | Meaning |
|---|---|
| +0x120 u8 | command request (1 attack, 2/3 killer move L/R, 4 cast) |
| +0x123 u8 | 0 party, 1 enemy |
| +0x124 u8 | action state (party: 0 idle, 1 approach, 2 leap, 3-4 knockback, 5 KO, 6 knocked off, 7 victory, 8 flee, 9/12 attack, 10/11 air attack, 14 chant, 15 release, 0x10-0x1a killer moves) |
| +0x125 u8 | party slot |
| +0x12e u8 | spell / skill id |
| +0x12f u8 | removed: 1 dead enemy, 2 fled |
| +0x132 u8 | camera follow kind |
| +0x1d4 Unit* | target |
| +0x11c u32 | buff / debuff bits rebuilt each frame (DEF halved, cannot cast, DoT, slow, ATK +50%, ...) |
| +0x127 / +0x128 | pending reaction (1 hit, 2 evade, 3 bumped) / pending death (1 KO, >= 3 vanish) |
| +0x150 | action recovery gauge (60 frames) |
| +0x154 / +0x164 | super armour / invulnerable timers |
| +0x16c / +0x170 | run speed current / base |
| +0x190 | last damage dealt; +0x1e8 last attacker |
| +0x1fc | stats record (PartySlot layout for both sides: +0 id, +2 status bits (0 KO, 1/2 incapacitated, 3 poison, 0x40 stun), +3 class, +4..+0xd element codes, +0xe.. status resistances, +0x18/+0x1c HP max / current, +0x22/+0x24 MP max / current, stat triples {base, equipped, battle}: ATK +0x2a, DEF +0x30, evasion +0x36, accuracy +0x3c, INT +0x42, guts +0x48; +0x5a crit rate, +0x5c weapon element bits). Party: the handoff-3 slot itself (damage lands there directly); enemies: a 0x60 record at 0x8006fa04[i] built from the encounter pack |
| +0x200.. | subclass fields: PartyUnit (+0x324 handoff-4 character record, +0x314 equipment property words, attack tables +0x330 / +0x350; parts/p1.md), EnemyUnit (+0x23a drops, +0x23e aggro radius, +0x249 spells, +0x24d behaviour odds, +0x252 name, +0x268 Fol; parts/p2.md) |

## 6. Data formats

Archive layout for battles: #1347 + stage (stage lists, 6.1), #1405 + encounter (encounter packs, 6.4),
#1969-#1977 music packs, #1978-#1984 battle-wide data and code, #2129 the program, #2130-#2160 party-character
data / code pairs (6.5), #2271 / #2272 spell / item description texts, entries 0x8e1-0x981 spell effect data
(table 0x8006d1f2, by caster variant), voice banks per character (tables in #1984.0, parts/p8.md).

### 6.1 Stage command list (archive entry 0x543 + stage id = #1347-#1404, raw; `load_stage` 0x80072270; parts/p6.md)

`u32 op` then arguments; offsets are relative to the entry start; op 0 ends. All 58 stages parse to the end with
these sizes. CLUT uploads take the next row of the loader context (x 0, rows from 0x1e0 up; ops 1, 3, 12).

| Op | Bytes | Arguments | Effect |
|---|---|---|---|
| 1 | 12 | u16 x, u16 y, u32 off | SLZ texture `{u16 mode (bits 0-1: 4/8/16-bit), u16 clut_n, clut[clut_n], u16 w (halfwords), u16 h, pixels}`: CLUT to the next row (g_stage_tex_clut[n++]), image to (x, y). 252 uses |
| 2 | 8 | u32 off | SLZ stage model block appended to g_stage_models 0x8006ef88[] (drawn by `stage_draw_and_animate`, format 6.3) |
| 3 | 12 | as 1 | panorama / background texture (CLUT -> g_panorama_clut; remembers x, y) |
| 4 | var | u16 mode, a, b, c, then {u16 x, y, w, h} until s32 -1 | panorama layout rects (0x8006f6a4..0x8006f6cd); only stage 0 |
| 5 | 12 | u8 r g b, pad, u32 v | fog colour 0x8006eff3 (flag 0x8006eff6), fog near 0x8006f086 (unused by the data) |
| 6 | 20 | u8 r g b, pad, u32 v, s16 x0 z0 x1 z1 | clear colour 0x8006eff7; v (1000 / 0) -> back-face threshold 0x8006f08a; arena rectangle g_stage_bounds 0x8006d4fc (<<12). Every stage has one |
| 7 | 8 | u8 r g b, pad | ambient colour 0x8006efef |
| 8 | 16 | s16 dx dy dz, pad, u8 r g b, pad | directional light n (dirs 0x8006ef9c, colours <<2 0x8006efb4, mask 0x8006efee) |
| 9 | 8 | u32 off | SLZ data block -> 0x8006efe4 |
| 10 | var | s16 dst_x, dst_y, then {u32 duration, u16 x0 y0 x1 y1} until u32 0 | texture animation group (VRAM copy frames; 0x8006d50c / 0x8006f544 / 0x8006f644 / 0x8006f684..) |
| 11 | 16 | s16 min x y z, max x y z | obstacle box kind 1 (0x8006e35c / 0x8006e4dc, kind 0x8006fb40, count 0x8006fb1a; max 24). 163 uses |
| 12 | 8 | u32 off of a TIM | CLUT at TIM+0x14 -> next row |
| 13 | 20 | u32 off0..off3 | stage sprite set (layout copy, animation table, frames, sheet: CLUT row 0x1e8, image (0x280, 0)) -> shared sprite bank 0x8006f930 |
| 14 | 12 | u16 azimuth, elevation (degrees), u8 r g b, pad | directional light from angles (colour <<4) |
| 15 | 20 | s16 x y z, u16 size, u32 colour, u16 b, s16 angle | sun / lens flare source (0x8006d52c, 0x8006f698..0x8006f6a1) |
| 16 | 8 | u8 r g b, pad | alternate clear colour 0x8006effa (flag 0x8006effd) for a band of camera pitches |
| 17 | 16 | as 11 | obstacle box kind 2 |

### 6.2 Battle text, font and UI art

- #1980: block 0 string table (`{u16 id, u16 offset}`, 0xffff end, glyph codes = glyph + 1, two bytes above 0x7f).
  Ids: 0-0x33 battle messages, 0x3e8.. spell names (0x3e7 + spell), 0x7d0.. tactics, 0xbb8-0xfb6 item names
  (2999 + item), 0x1388.. killer moves (0x32 per character), 0x1f40-0x1f4d battle skills. Block 1 font (104 glyphs,
  height 12, same format as the title's #14 block 1). Block 2 CLUT row 0x1f9.
- #1979 (UI sprites) and #1982 (common effect sprites): sprite banks, each `{offsets: layout, animation table,
  ..., image block, CLUT block}` (5 offsets in #1979, 7 in #1982); #1979's atlas goes to VRAM (0x3c0, 0) and
  (0x3c0, 0x100), CLUT rows 0x1fe (animated, copy at 0x8006f6d4), 0x1f5, 0x1ff. The banks become the AnimSets of
  the shared sprite-set BObjs (0x8006f920-0x8006f930; `battle_setup_sprite_banks` 0x800747a0). AnimSet format:
  parts/p2.md.
- #1983: item table, 1023 records x 0x30 bytes indexed by item id - 1 (the battle's copy of the item data that
  `inventory_item_data` reads).
- Music packs #1969-#1977: `u32 bank_offset`, SLZ sequence at +4, sound bank at bank_offset (same layout as the
  title's #2674). #1978: SLZ victory sequence.

### 6.3 Battle models (`model_render` 0x8007843c; parts/p4.md, p8.md)

`u32 part count`, then per part a `{u32, u32 count}` header and 20 counted primitive sections in a fixed order:
lit F3, G3, F3 coloured, G3n, FT3, GT3, F4, G4, (skipped), G4n, FT4, GT4, then unlit F3, G3, FT3, GT3, F4, G4,
FT4, GT4. Each primitive embeds its SVECTOR vertices / normals / colours; sizes in words per primitive (lit 9, 13,
11, 15, 11, 15, 11, 17, 12, 20, 13, 19; unlit 7, 9, 10, 12, 9, 12, 12, 15). Drawn with NCLIP + NCCS/NCCT (or the
fog variants) into pool A at OT 0x610 + bias - otz/8. Exact per-field byte layout still to decode.

### 6.4 Encounter pack (archive entry 0x57d + encounter, #1405-#1968; parts/p2.md)

Read by the encounter loader 0x8007344c and the monster factory. All u32 / s32 little-endian, offsets from the
entry start:

1. Sprite-set list: `{offset, count}` pairs (an SLZ sprite set shared by `count` formation members), -1 ends.
2. CLUT list: `{clut index, offset}` pairs (0x80-byte CLUT to row 0x1fa + index), -1 ends.
3. Code list: `{kind 0-3, offset}` pairs (SLZ code block -> enemy-code slot of that kind), -1 ends.
4. Formation: 12-byte records `{s16 type, s16 x, s16 z, s16 y, s16 sprite set, u16 flags}`; a record with type 0
   ends the list and its +2 holds the encounter flags (bit 0 clear: 0x8006efea; bits 1-3: 0x8006efeb = 1/2/3 =
   back / surrounded / raid attack). x, z are relative to the stage bounds; y != 0 = flying; flags bit 0 faces +x,
   bit 1 immune to status kind 0, bit 2 immobile.
5. Then one **0x5c-byte stat record** per formation member: +0 HP, +4 MP, +8 EXP, +0xc Fol, +0x10..+0x14 body
   half-extents, +0x16 size/speed, +0x1c ATK, +0x1e DEF, +0x20 INT-like triple, +0x22 move speed, +0x24 level-like,
   +0x26 / +0x28 evasion / accuracy, +0x2a u8[12] element and status resistances, +0x37 / +0x38 drop chance bytes,
   +0x39 recovery, +0x3a aggro radius, +0x3c u8[4] spells, +0x40 u8[5] behaviour odds (per 1024: attack, cast,
   counter, aggro, wander), +0x45..+0x4b misc (crit, knockback, element bits), +0x4c char[16] name. Drop item ids
   are at unit +0x23a / +0x23c (record +0x18 / +0x1a). Difficulty (config+0x4f) 1: ATK and HP x1.5; 2: ATK x5/3,
   HP and MP x2 and stat boosts.

Details (parts/p6.md, which also has a parser description; all 564 entries parse):
- Placement position: `x_world = x * ((x1 - x0) >> 12) / 0x1004 + centre` (fractions of the op-6 arena; the party
  uses the same scheme with `g_party_formations` 0x80084fd8[config+0x40][slot]).
- Formation flags: bit 0 = no escape (g_can_escape 0x8006efea = 0; bosses); bits 1-3 -> g_intro_type 0x8006efeb
  (probably Back / Surrounded / Raided attack, #1980 strings 9 / 10 / 17): intro camera and party start shifts.
- Code slots used: 181 formations load slot A, 113 slot B, 62 slot C, 2 slot D. **The #2129 image's own modules in
  slots A / B / C are required defaults**: encounters 457, 458, 501, 512, 525, 527, 558 (Guardian / Geo-guardian /
  Owlbear) load no code and use the image's slot-A module (vtable 0x80089df0), and encounter 536 (Ruprecht /
  Nicolus / Jibril) loads only slot A and takes B (0x8008b0c8) and C (0x8008c0c4) from the image.
- Sprite pack: `u32 nA | 0x80 (extra list), u32 nB, u32 off_layout, u32 off_anims, {off_frames, off_sheet} x
  (nA + nB) [, extra offsets until 0]`; sheets `{u32 clut_off, u32 image_off, rects ...}`, 4-bit images packed into
  four VRAM pages (0x340 / 0x380, 0 / 0x100), CLUT rows 0x1fa + kind (built by `clut_build_pair`). They become the
  unit's AnimSet (format in parts/p2.md).
- Encounter 0 (#1405) is a test fight against Cyril.

The monster type id -> class table (16 resident families: knight, guard, plant, kobold, thief, magius, axe,
sandglass, ooze, beastmaster, wolf, giant, goat, archer, shout, wizard; everything else including all bosses in the
encounter's own code module) with monster names is in parts/p2.md.

### 6.5 Party-character data and code (#2130 / #2131, #2132 / #2133, ...; parts/p7.md)

The party-data loader task (`party_loader_update` 0x80082cdc, the only driver of battle state 0) handles each
member in phases:
- Phase 0: entry `g_char_battle_entries[character][variant]` (0x80084ea8; variant = item field 0x2a of the
  weapon) read together with the next entry (load group 6): the even entry is the data, the odd entry the code.
  Code block <slot> is unpacked to `0x801ee000 + 0x4000 * slot` (four variants linked for the four slots) and
  `pc_construct_slot` 0x80083fc8 calls its offset 0 (allocates the PartyUnit subclass, 0x360 bytes for Claude,
  which calls the ctor 0x80034ed0 at block offset 0x68 and installs its vtable: overrides of slots 35-37, 42, 45).
  Then per-character setup 0x80084538.. (body box, speed, reach, class byte), start position, and
  `pc_calc_equipment_stats` / `pc_apply_equip_effects` (stat table in parts/p7.md; equipment effect ids 0x69-0xfa
  set ability bits in unit +0x314 / +0x318 / +0x31c).
  The data block (SLZ, streamed at g_frame_ticks * 0x3000 bytes a frame) has an 8-offset header -> unit +0x108,
  +0x114, +0x110, +0x10c, +0x210, +0x20c, +0x218, +0x214 (animation layout, animation table, frames, two body
  texture blocks, weapon texture block); texture blocks `{u32 clut_off, u32 image_off, flags, u16 rect count,
  rects}`; CLUTs to row 0x1f1 + slot (weapon 0x1ed + slot), images to fixed VRAM columns per slot; the headers are
  copied into the member's 0xb000 buffer (`g_member_buf`; more is fatal).
- Phases 1-4: killer-move data for the four assigned killer moves (record +0xcc..+0xcf; entry from per-character
  tables, `killer_move_data_entry` 0x80083db0) -> unit +0x250.. / +0x290.. / +0x330 / +0x350, CLUT row
  0x1cf + slot * 4 + phase.
- Finish: equipment effects applied, units registered as tasks (priority 1), `g_item_table` freed,
  `g_battle_state = 1`, the loader deletes itself (its code is then overwritten by #1984 block 1).

### 6.6 Spell data (entries 0x8e1-0x981; table 0x8006d1f2 = 5 entries per spell by caster variant; parts/p3.md, p7.md)

A list of u32 commands executed one per frame by `spellcast_load_step` (0x80086c68): 0 end, 3 texture SLZ block,
4 model, 5 sound clip, 6 code module (SLZ block unpacked to 0x8008cc00 / 0x8008e600 and called every frame instead
of the built-in kind handler), 7 / 9 extra model part, 8 raw block. The battle load queue keeps at most three
spell-data entries cached (`spell_data_*`, 0x80048xxx).

## 7. Subsystems (summaries; details in the part notes)

- **Camera** (parts/p1.md): 17 modes with priorities (table 0x8006b494): intro fly-over (1, 8, 9, 13, 16, 17),
  follow controlled member (2), party view (3), track all (4, 14), follow target / action camera (5, 7, 15),
  victory sweep (6), scripted (10), overview (11), orbit (12). `battle_frame_director` (#1984.0 at 0x800715d0)
  picks modes each frame. Live view: eye 0x8006f020 / look-at 0x8006f02c / zoom 0x8006f038;
  `camera_compute_view` + `lights_build` build the GTE matrices.
- **Rendering** (p5, p8): screen module identical to the field's; RenderContexts in 8 OT layers; 30 fps; stage model
  via `stage_draw_and_animate` 0x800471a4 + `model_render`; lights 3 directional + ambient + fog, scaled by the
  stage colour; lens flare; procedural primitives (sphere, beam, vortex, wave grid, textured patch).
- **Effects** (p4): Shot kinds (table 0x8006fea8, 0x34 kinds: spells 6-0xf/0x1d, items 0x2e-0x30, monster
  projectiles 0x16 from a 0x74-byte ShotDesc, callback kinds 0x21/0x33, stage kinds 0x18-0x2b in the stage blocks,
  kinds in character / enemy modules); DamagePopup; Particle; PolyShard (`model_shatter`); Sprite pool; effect tasks.
- **Damage** (parts/p3.md, p4.md): physical: `unit_attack_targets` 0x800509e4 -> `unit_attack_hits` (miss if
  rng(256) < evasion - accuracy; auto-hit from behind / on airborne or disabled targets) -> `unit_calc_damage`
  0x800503f0 ((ATK - 1.5*DEF) * power >> 8, element codes, +-10%, clamp 9999) -> crit x1.5 (stats+0x5a / 512),
  equipment procs, absorb / reflect / barrier -> `unit_damage_hp` (guts survive) or `unit_damage_mp`; reactions
  through +0x127 / +0x128 and slot 5. Magic: same calc with magic = 1 (DEF 0), wrapped by `shot_apply_damage`
  0x800548c4 (status boosts, miss/guard, absorb/reflect, knockback, popup). Element multiplier table 0x8006cdc0 (x2: normal 2, weak 4,
  resist 1, immune 0, absorb -2).
- **Spells** (p5, p7): `spell_cast` 0x80062c24: MP cost table 0x8006b053, messages, then ten built-in spells as Shot
  kinds; every other spell creates a SpellCast (`spellcast_create` 0x80085560, 0x248 bytes, task priority 3, two
  slots 0x8006fe08; the second opens the SpellClashWindow; g_battle_flags |= 0x40 while one runs). Its spell data
  (6.6) may carry a code module unpacked to 0x8008cc00 / 0x8008e600; otherwise a built-in kind handler runs. The
  handlers are step machines over the SpellCast API (waits, palette dimming of the other side, flash, area / target
  hits, status areas). Two concurrent casts with paired elements clash (`spell_clash_check`, #1984.1).
- **Items** (p9): `item_use_battle` 0x80082dec (#1984.1), 98 effect kinds from the item effect table 0x8006fafc.
- **Party AI** (p1): tactics byte record+0x38; fighter / healer (Rena) / caster logic; healer order from the
  affection tables in config.
- **Enemy AI** (p2): `enemy_update` -> `enemy_think`; in state 0 `enemy_ai_tick` rolls the five behaviour odds
  every 8 ticks (attack, cast, counter, aggro, wander; jump table 0x8006b8f0); slot 26 chooses the target, slots
  27-31 are attacks 1-5 (states 9-13), spells go chant (14) -> name banner -> cast (15) -> `spell_cast`. Families
  and module classes override slots 5, 9, 24-29, 32, 36.
- **Player input** (p3): `input_update(g_input)` (0xa0-byte block 0x8006fad4): modes 0 idle, 1 member select,
  2 enemy target select, 3 ring command menu (spells / items / escape / tactics), 4 list menu open; buttons from
  config u16[0..7] (confirm/attack, cancel, ring menu, free run, killer moves 1/2, leader swap, tactic toggle);
  commands are written to the controlled unit's +0x120 / +0x1d4.
- **Battle load queue** (p3): 16 requests with groups, priorities (aging) and owner tokens (0x8006fbac-0x8006fdf4;
  `bload_request` 0x800475c0, `bload_done` 0x80047ac0, `bload_update` 0x80047c08); group 6 reads two entries at
  once; sector requests `(file+1) << 16 | sector`. The spell-data cache sits on it (max 3 loaded).
- **Sound** (p8): `bsfx_play` (8 tracked emitters, 3-D pan), voice clips in two load slots, voice tables per
  character (battle bank, victory, healer thanks, ally call-outs); sound-test bitset config+0x1a0.
- **UI** (p5, p8): BattleHud, list menus (spell / item / tactics), message banner, skill-notice banner, scan,
  spell-clash; 8x8 UI font + proportional text renderer (title code, #1980 data, 64-row buffer at VRAM 0x3c0,0x1c0).
- **Rewards and exit** (p9): result screen states, EXP table (`exp_table_build`: sum of (i^2+1)*((i-1)/10+5) below
  100, (i^2+1)*15 from 100; cap 255), Fol (full to config+0x18), EXP split over non-KO members (compounding bonus
  bug), drops (byte chance + luck/16 - 7), level-up growth table 0x80087f94, learn tables; write-back list in
  parts/p9.md.

## 8. Port order: "a battle starts, renders and can be won"

Each step is testable on its own against PCSX-Redux captures of one simple battle (e.g. the first random
encounter near Arlia: a resident-family monster such as a Kobold or Wolf, no stage block, no enemy module).

1. **Program swap and handoff.** `boot_run_main_program` already exists. Add a native BattleProgram selected by
   entry #2129: read handoff 5 / 2 / 3 / 4 / 6, run, write `config+0x178`, store handoffs 0xc / 10, swap back to
   the field. Start with a stub that immediately "wins" (outcome 0) so the field's return path (handoff restore,
   R register, map reload) can be ported and tested first.
2. **Data loading.** The load-list walk of `battle_run` with the preloaded-pointer rules, SLZ, the loaders for
   #1977 (music), #1978, #1979 (UI atlas, CLUTs), #1980 (strings, font, CLUT), the stage list (6.1) and the
   encounter pack (6.4). Write a data-dump test that loads every stage list and every encounter pack on disc 1.
3. **Frame loop and rendering core.** `battle_main_loop` skeleton (30 fps, g_frame_ticks), the screen module and
   RenderContexts / 8 OT layers (shared with the field port), camera_compute_view + lights_build + obj_set_matrix,
   `model_render` (the stage model), BObj animation drawing (`bobj_anim_draw` 0x80042120, AnimSet, TexCache) and
   the Sprite pool. Milestone: the stage and the idle monsters / party sprites render with a fixed camera.
4. **Units.** Unit base (layout, stats record, movement, collision, `unit_check_move`), EnemyUnit with the base AI
   and the resident families used by the test battle, PartyUnit states 0/1/2/9/14/15 + pc_think + the player input
   (`input_update`, mode 0 and target select), the camera modes 1, 2, 3, 5, 7, 11 and `battle_frame_director`.
   The party-character module code (#2131...) is needed for killer moves and per-character attack data: begin by
   hand-porting one character (Claude) and stub the module hooks (slots 35-37, 42, 45) for the others.
5. **Combat rules.** The damage pipeline (5.2 / section 7), damage popups, `unit_is_down`, reactions, KO / vanish,
   the end checks (`battle_victory_begin`, `party_count_standing`), states 2 -> 3 -> 4.
6. **Winning.** #1981: the result screen (EXP table, award, drops, level-ups, learn tables), the HUD and message
   banner, `battle_fade_task`, `battle_exit_to_field`. Milestone: a battle starts from the field, renders, the
   monster can be killed, rewards are applied to handoffs 3 / 4 / 6 / 2 and the field resumes.
7. Then: spells (`spell_cast`, Shot kinds, SpellEffect modules), items (`item_use_battle`), menus, party AI, sound
   front end and voices, stage blocks, enemy-code modules (bosses), particles / afterimages / polygon shards.

Porting notes:
- The fixed-address module ABI (#1984.0 API used by hundreds of module entries, enemy-code slots, party slots at
  0x801ee000, spell code at 0x8008cc00 / 0x8008e600) only matters for a recompiling port. A hand port replaces
  every module with native classes keyed by monster type / character / spell / stage id, and can ignore the
  load addresses entirely (but must keep the data formats).
- Keep the 0x26c / 0x200 layouts only where data is copied wholesale (the stats record is the handoff-3 slot and
  is written back as-is).
- Reproduce the variable time step (g_frame_ticks) and the RNG call order (rng_range / rng_between) for
  deterministic comparisons with the original.

## 9. Open questions (merged from the part notes)

1. **Model and sprite data** (p4, p7, p8): per-section byte layouts of the battle model primitives, and the
   meaning of the party data header fields +0x108 / +0x110 / +0x114 / +0x210 / +0x218.
2. **Stat and status names** (p3, p6, p9): SO2 names for the stat triples at +0x36 / +0x3c / +0x48 and
   +0x52 / +0x58; status bits 1 / 2 of stats+2 (paralysis / petrify?); element affinity codes 1 / 2 / 3 (weak /
   resist / null is the working guess, 4 = absorb fits).
3. **Monster record bytes** +0x22, +0x37..+0x3b, +0x45..+0x4b (AI tuning; partly mapped to unit fields).
4. **Intro types** g_intro_type 1 / 2 / 3: probably Back / Surrounded / Raided Attack (not checked against the
   banner code).
5. **Module interfaces.** Which character each code entry #2131.. is; what the character modules put in
   PartyUnit slots 35-37 / 42 / 45 and attack tables +0x330 / +0x350. Shot kinds 0x14 / 0x17 / 0x24 / 0x25 point
   into party slot 0's module and kinds 0x26 / 0x29 into enemy slots, so the module layouts must match or the
   table is patched. Spell kinds 0x36 / 0x3b point into the enemy area.
6. **Possibly dead code:** `clash_register` 0x80081728 (#1984.1) has no caller; neither have
   `projectile_arc_velocity` 0x8004906c, goat slot 37, `model_find_vertices_near` 0x80061694 or 0x80058248 (they may
   be called from module code that was not scanned).
7. **g_item_table** is freed at state 1 while inventory+0x820 still points at it; who reads item data later?
8. **Bugs to keep or fix in a port:** compounding EXP bonus in `result_award_exp_money`; `stat_mod_attr_3e` clamps
   +0x40; item field 0xe feeds two stat groups; the cart popup shows 50 but deals 100.
9. Party slot +0x26 (battle level copy; also picks result-voice variants); config+0x42 / +0x43 (alternate
   Claude / Rena voice columns: renamed heroes?).

## 10. Files

- `battle.csv`: functions of the #2129 image (module `battle`), with lifetime notes; `[stale]` / `[dead]` rows are
  copies of other modules or never-called library code at their image address.
- `battle_overlays.csv`: functions of #1984 block 0 / 1, #1981 and the stage blocks (#1984 blocks 2-10), which run
  at addresses that the #2129 image also uses (modules `battle_ovl1984_N`, `battle_ovl1981`).
- `battle_index_artifacts.csv`: Ghidra index entries that are not functions (bogus starts, phantom call targets).
- `battle_globals.csv`: globals, data tables and vtables.
- `parts/p1.md` .. `parts/p9.md`: the detailed notes this file summarises (layouts, slot tables, formulas).
- `init_image_map.md`, `CONTEXT.md`: the image map and the brief the range analysis worked from.
- `tools/`: bv.py (decompile viewer with names), bdis.py / blobdis.py / bootdis.py (capstone), xref.py, globrefs.py,
  gapfuncs.py, match3.py / fuzzy.py (field / title matching), merge_parts.py + fill_stragglers.py + post_fixups.py
  (rebuild the CSVs from the part files: run them in that order).
- `blobs/`: unpacked #1978-#1984 blocks and the runtime image.
