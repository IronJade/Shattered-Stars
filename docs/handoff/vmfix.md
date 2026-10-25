# vmfix: field script ext 0xCB (sound_start_clip) argument count

Agent "vmfix", started 2026-10-25. Resumable log.

## Task
The port's ext 0xCB handler (engine/src/game/field/script_ext.cpp) fetches 5 arguments; the
game's only two call sites (map 642 0x6e3c, Courage's Guardian scene; map 575 0x761c, Berle's
scene in Eluria Tower) are stack-form and push 4. The value stack underflows (ASan
heap-buffer-overflow, with or without a seed). Found by hubchambers (docs/handoff/hubchambers.md,
"0xCB"; docs/catalog/script_opcodes.csv row ext 0xcb, status "conflict").

1. Reverse the original's ext 0xCB handler and its argument fetch (REA, native MIPS). Count and
   order of arguments; any difference in stack handling. Original bug -> correct it, list in
   docs/notes/original_bugs.md, play state unchanged. Catalog it (address order, LF).
2. Fix the port to match the original exactly. Prove with ASan (both scenes, no seed), and that
   the clip still plays (SS_VOICE_TRACE / SS_SOUND_TRACE).
3. Unit test in engine/tests/test_script_vm.cpp: ext 0xCB with the real call sites' argument
   count, stack balanced.
4. Suites: baseline ss_tests 99/0, ss_game_tests 170/0, smoke 161/0.

## Rules (from the coordinator)
- Own build dir engine/build-vmfix (configured like engine/build-hubcore). Shared source tree
  with "hubask" (engine/src/game/ap/, apworld/) and "hubproof" (proofs): keep it compiling after
  every edit, touch only the VM / field files this fix needs.
- SS_DISC1=".../Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second
  Story (Disc 1).cue".
- Headless only (`--headless`); PCSX-Redux only with `-cli`, scratch HOME, scratch card copies.
  Never save ~/.config/shattered-stars/config.ini: scratch XDG_CONFIG_HOME. SDL_AUDIO_DRIVER=dummy.
- Every game process under the memory cap (tools/memcap.py run_capped, or
  `prlimit --as=$((4<<30))`); ASan builds in a systemd-run MemoryMax scope. At most 2 game
  processes at once. Hitting the cap = a failure to investigate, never a retry.
- Seeded headless tests loading ordinary saves need SS_AP_ANY_SAVE=1.
- Enhance, never degrade: a fix never changes the play state; it matches the original.
- Scratch in scratchpad/vmfix/ (not /tmp). Cap output. Kill only by exact PID. Waits have
  deadlines; long jobs in the background.
- No commit, no push. Update this log at least every 20 minutes.
- Permission refusal: stop and report the exact command. Past ~400k context: update the log
  and end the turn.

## Findings
- REA on analysis/elf/SO2_boot+prog3100.elf (field program + boot). vm_exec_ext 0x8006241c:
  `srl s4,s3,24; addiu v1,s4,-0x64; sltiu v0,v1,0x9c`, jump table 0x8007359c (index op-0x64).
  Entry for 0xCB (0x80073738) = **0x80063688**; 0xCC at 0x800636d8.
- Case 0xCB, 0x80063688-0x800636d4: `move a0,s1; jal 0x80068db8 (vm_fetch_args); li a1,5`, then
  a1=args[0], a2=args[1]&0xff, a3=args[2]&0xff, sp+0x10=(s16)args[3], sp+0x14=args[4]&0xff,
  a0=*(0x80075274) (Sound); `jal 0x8001a4b8` (sound_start_clip). **The original fetches 5**, no
  optional argument, no count in the instruction (0xcb800000: bit 23 = stack form, low bits 0).
- vm_fetch_args 0x80068db8: stack form pops `count` words from ctx+0x24 into args[count-1..0]
  with no bound check; the port's copy matches.
- sound_start_clip 0x8001a4b8 (sound, slot, volume, pan, pitch_scale, invert): effect =
  Sound+0x112c+0x34*(slot-1); returns if its +0 (script) word is 0; else word +0x18 =
  volume<<24 | pan<<16 | (s16)pitch, +0x1c = 1 (update), +0x1d = invert. No slot range check.
- Call sites (tools/so2_script.py dis extracted/disc1/SO2.BIN): 642 0x6e2c..0x6e3c and 575
  0x760c..0x761c: `play_sfx(45, pan, 0x7f40)` -> R stored in a local; later push local (the clip),
  push 0x20 / 0x40 (575: 0x40 / 0x40), push 0x2000 (575: 0x1068), ext_cb stack form. 4 values.
  Intended (by the values): sound_start_clip(clip, volume 0x20/0x40, pan 0x40, pitch 0x2000/0x1068)
  with invert missing.
- So in the original: args[0] = the word BELOW the value stack (value stack = state block +0x100,
  so state block +0xfc, the call stack's last word), args[1..4] = the 4 values (slot <- that word,
  volume <- clip & 0xff, pan <- 0x20, pitch <- 0x40, invert <- 0x00 / 0x68). The state block is
  memset to 0 at every field start (field_state_create 0x8005ea54: mem_alloc(0xa40) + memset),
  and +0xfc is only written at call depth 16 or by an earlier underflow, so the slot is 0 in
  practice. Slot 0 -> Sound+0x10f8 (reverb.depth) as the in-use word; if non-zero it writes over
  Sound+0x1110..0x1115 (common.mvolx, cd.volume.left), a copy applied only at sound init
  (sound_init_common_attr 0x80015120): no audible effect. The playing clip is never touched.
- Then value_sp stays one word low (offset -4) until the map changes. vm_save_state 0x800697bc
  stores the offset as u16 (sh) and vm_restore_state 0x80069aac reloads it with `lhu` (0x94a30034):
  after a battle on that map the stack is at value_stack + 0xfffc, 64 KB past the 0xa40 block.
  The original then writes script values into whatever lies there.
- Port: same 5-pop; slot 0 reaches sound_start_clip -> effects[-1] (UB in the port).
- Port crash path, seen in the VM trace (SS_VM_TRACE, baseline build): value_sp -1 word after
  ext 0xcb (642 w7055 / 575 w7559), then the battle (642: ext 0xb7 at 0x4f70 in the routine
  called at 0x7464; 575: ext 0xed `battle_c` at 0x76f0) saves 0xfffc, and the restore puts the
  stack at +16383 words: the pushes write 64 KB past the 0xa40 block (ASan heap-buffer-overflow
  WRITE in push; plain build: "double free or corruption (!prev)" at exit, rc -6).
- Unrelated port bug found on the way (NOT fixed, menu code): with 575 run on past the disc-1-end
  save prompt (map 618, after vblank 13758), ASan reports a heap-buffer-overflow READ in
  Gpu::load_image <- MenuScene::upload_tim (menu_scene.cpp:190) <- MenuScene::MenuScene
  (menu_scene.cpp:135) <- menu_run_save (menu_camp.cpp:834): it reads 0 bytes after a
  23624-byte SLZ-unpacked buffer (slz.cpp:22). Log scratchpad/vmfix/runs/asan_fix_berle.log.

## Fix (done)
- script_ext.cpp case 0xCB: ORIGINAL BUG marker (original_bugs.md W4), original fetch kept as a
  comment; inline form or >= 5 values on the stack: vm_fetch_args(ctx, 5) as before; fewer:
  pop only what the stack holds into the top arguments, the ones below read 0.
- vm_services.cpp vm_svc_sound_start_clip: skips slots outside 1-8 (the port has no memory
  before effects[0]); the TRACE still records the call.
- Tests: test_script_vm.cpp script_vm_ext_cb_sound_start_clip_stack (4 pushed, sentinel 7 at
  block +0xfc, stack balanced, trace "sound_start_clip(0,3,32,64,0)", save/restore round trip
  with saved offset 0) and script_vm_ext_cb_five_arguments (5 on the stack over a 6th, and the
  inline form).
- docs/notes/original_bugs.md W4.

## Done and verified
- engine/build-vmfix configured (like build-hubcore, deps from build-hubvm/_deps) and built
  (baseline source, rc 0); baseline binary copied to scratchpad/vmfix/shattered-stars.base.
- engine/build-vmfix-asan configured (gcc ASan, libasan copied to scratchpad/vmfix/asan).
- scratchpad/vmfix/run_scene.py: no-seed hub-state runs like hubchambers' story_run.py, with a
  filtered SS_VM_TRACE (ext 0xCB and value stack offsets outside 0..31).

- Catalog (LF, address order): globals.csv field 0x8007359c k_vm_exec_ext_cases (jump table),
  0x800756fc state_block, 0x80075700 value_stack (new rows); script_opcodes.csv ext 0xcb
  conflict -> confirmed (handler 0x80063688, W4); functions.csv sound_start_clip 0x8001a4b8 note
  (1-based slot, no range check, slot 0). (globals.csv's 0x800762ff-after-0x80076300 order
  slip is pre-existing in HEAD, not touched.)
- Proof (all no seed, hub state with CH/ST, scratchpad/vmfix/runs/):
  - VM trace (SS_VM_TRACE, filtered): baseline value_sp -1 after ext 0xcb, 16383 after the
    battle (642 base_courage_vm / base_courage_full, 575 base_berle_vm); fixed: 0 after ext 0xcb,
    never outside 0..31 (fix_courage_vm, fix_berle_vm).
  - ASan, unfixed (engine/build-hubchambers-asan): asan_base_courage (642) and
    asan_base_berle13 (575): heap-buffer-overflow WRITE in push after the battle, rc 1.
  - ASan, fixed (engine/build-vmfix-asan): asan_fix_courage (642, 12000 frames: scene, battle,
    625, back to 642) rc 0 clean; asan_fix_berle13 (575, 13000 frames: scene, battle, 576, 625)
    rc 0 clean. (asan_fix_berle, 16000 frames, hits the unrelated menu bug above at 618.)
  - Plain build, baseline vs fixed: sweep logs identical (incl. "rng 22 a60ae20e draws 5687" at
    the play's end on 642); state dumps equal except VM scratch (block +0xfc, the value-stack
    words, field_flags +0x34 the saved stack offset); SS_SOUND_TRACE identical (575: 16554
    lines, cmp equal; 642: equal up to tick 19885 of 20015 where the baseline's buffer died
    with it: "double free or corruption (!prev)", rc -6). ASan-fixed 575 trace = baseline prefix.
- Suites (engine/build-vmfix, SS_DISC1, scratch XDG/TMPDIR): ss_game_tests 172/0 (170 + 2 new),
  ss_tests 99/0, smoke 161 pass / 0 fail rc 0 (scratchpad/vmfix/{game_tests1,ss_tests1,smoke1}.log).
- REA session closed; the baseline binary copy deleted (rebuild from HEAD's script_ext.cpp to
  rerun a "before" case, or use engine/build-hubchambers-asan, which predates the fix).

## In progress
- Nothing. DONE (not committed, per the rules).

## Next steps
- Possible follow-ups for the coordinator / user: (1) the menu TIM overflow above
  (menu owner); (2) whether to apply the scripts' intended clip change (volume / pitch) as an
  enhancement (W4 says the user decides); (3) other stack-form ext calls with a short push
  were not searched statically (only these two hit ASan in hubchambers' 46 chambers).

## How to verify
- `SS_DISC1=<cue> python3 scratchpad/vmfix/run_scene.py NAME BIN claude 641 12000
  "641:T2,642:T1:1" CH=42 ST=0 [SS_VM_TRACE=1] [SS_SOUND_TRACE=x.snd]` (642) and
  `... claude 575 13000 "9999:T0" CH=33 ST=20` (575: map 575's set-up plays Berle's scene).
  BIN = engine/build-vmfix-asan/shattered-stars for ASan (memcap puts it in a systemd scope).
- Unit tests: ss_game_tests script_vm_ext_cb_sound_start_clip_stack / _five_arguments.
