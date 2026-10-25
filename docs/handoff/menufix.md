# menufix: heap-buffer-overflow READ in MenuScene::upload_tim (save prompt, map 618)

Agent "menufix". Rules: headless only; PCSX-Redux only -cli with scratch HOME; own build dirs
engine/build-menufix + an ASan build; -j8; tools/memcap.py for every game process, at most 2 at
once; scratch XDG_CONFIG_HOME; SDL_AUDIO_DRIVER=dummy; --debug runs stdin </dev/null; scratch in
scratchpad/menufix/; no commits; enhance never degrade (fix never changes the play state);
suites with counts (baseline ss_tests 99/0, ss_game_tests 181/0, smoke 261/0). Hunks limited to
menu / GPU / SLZ code. Other agents: skillsanity2 (engine/src/game/ap, apworld, field VM, smoke
script), hubproof2 (proof runs).

Bug (from docs/handoff/vmfix.md "Unrelated port bug"): ASan heap-buffer-overflow READ in
Gpu::load_image <- MenuScene::upload_tim (menu_scene.cpp:190) <- MenuScene::MenuScene (:135) <-
menu_run_save (menu_camp.cpp:834), 0 bytes after a 23624-byte SLZ-unpacked buffer (slz.cpp:22).
Log scratchpad/vmfix/runs/asan_fix_berle.log.

## Done
- (started 2026-10-25) log created.
- Cause found (static): every menu kind's resource pack (entries 0xba4/0xba8/0xbac/0xbb0/0xbb4,
  all the same 23624 bytes unpacked, md5 0279389f...) holds two 4-bit TIMs: TIM 0 at +0x8,
  image 64x128 halfwords (0x4000 bytes at +0x228..+0x4228); TIM 1 at +0x4228, image 64x48
  (0x1800 bytes at +0x4448..+0x5c48 = the buffer's end). The port's MenuScene ctor uploads
  TIM 0 with rect {0x180,0x100,0x40,0x100} (256 rows = 0x8000 bytes), reading 0x25e0 bytes past
  the buffer. The original (field menu_scene_create 0x8002f9f8: rect at sp+0x28, h = s1 = 0x80
  set at 0x8002fc18, stored at 0x8002fc68; title 0x8002fff4: s1 = 0x80 at 0x800301f8, stored
  0x80030248) passes h = 0x80: it reads exactly the TIM's image. Not an original bug; a port
  transcription slip (the MoveImage rect {0x180,0x100,0x40,0x100} copied). menu_upload_tim
  (field 0x800303d4 / title 0x80030974) matches the port's upload_tim. Every menu open (camp,
  shop, member, query, save, title Continue) runs this ctor, so all menus overflowed.
- VRAM effect of the slip: x 0x180..0x1bf, rows 0x180..0x1c7 got TIM 1's header/pixels shifted
  and heap bytes past the buffer, rows 0x1f8..0x1ff heap bytes; rows 0x1c8..0x1f7 are rewritten
  by TIM 1 either way. Fixed: those rows keep what was there (as on the console).

- Builds: engine/build-menufix (plain, like build-hubcore, deps from build-hubvm/_deps) and
  engine/build-menufix-asan (vmfix-asan's flags; libasan copied to scratchpad/menufix/asan).
  Unfixed binaries kept: scratchpad/menufix/ss.base, ss.asan.base.
- Reproduced (unfixed ASan): `run.py asan_base_camp ss.asan.base -- --frames 500 --map 18
  --input 300:triangle,306:none` (camp menu): same heap-buffer-overflow READ, 0 bytes after the
  23624-byte pack, upload_tim <- MenuScene ctor :135 <- menu_run_camp. Every menu open does it.
- Unfixed VRAM around the ctor's uploads (gdb.minimal batch, scratchpad/menufix/vram.gdb):
  runs/base_pre.bin (at upload_tim #0 entry), runs/base_post.bin (after upload_tim #1).
- Other TIM uploads checked (scratchpad/menufix/tim_scan.py over entries 0xba4..0xbd6): every
  page TIM (0xba7, 0xbab, 0xbb7, 0xbb8, 0xbbf, 0xbc1, 0xbc9, 0xbcb, 0xbd1) is 64x64 + 16 CLUT
  rows = 0x2220 bytes; the pages' uploads (menu_camp/options/combat/items/creation, menu_save)
  are 0x40x0x40 at +0x220: in bounds. Only the scene's TIM 0 rect was wrong.
- Fix written: menu_scene.h kWindowArt[2] (TIM 0 {0x180,0x100,0x40,0x80}, TIM 1 as before),
  the ctor loops over it; MenuScene::upload_tim static, bounds-checked against the pack (skips
  and logs a TIM that doesn't fit; never with the shipped packs). Test ss_game_tests
  menu_window_art_fits_pack (SS_DISC1): packs 0xba4/0xba8/0xbac/0xbb0/0xbb4/0xbd0 = 23624 bytes,
  TIM headers = kWindowArt sizes, upload = the pack bytes, a pack one pixel short not uploaded.
- Catalog: functions.csv field 0x8002f9f8 / title 0x8002fff4 menu_scene_create evidence notes
  (under flock scratchpad/catalog.lock). Not an original bug: no original_bugs.md entry.

- Fixed binaries built (rc 0, no warnings in the touched files): scratchpad/menufix/ss.fix,
  ss.asan.fix.
- ASan, fixed: asan_fix_camp (camp menu, 500 frames) rc 0, no report; asan_fix_berle (vmfix's
  575 case, 16000 frames, "map 575 vblank 13758 now on map 618" then on to 16000) rc 0, no report.
- VRAM (gdb dumps runs/{base,fix}_{pre,post}.bin): before the uploads identical; after them
  4884 halfwords differ, all at x 0x180..0x1bf rows 0x180..0x1c7 and 0x1f8..0x1ff (outside the
  64x128 rect): the unfixed upload wrote 3344 halfwords of TIM 1's header/pixels there (all
  match the pack at +0x4228..) and 1776 from past the pack's end; the fixed one leaves the
  rows as they were (= the pre dump, as on the console). TIM 0's 64x128, TIM 1's 64x48 and
  the CLUT rows (0x100..0x11f, 0x1e0..0x1ef) are byte-identical; nothing outside x
  0x180..0x1bf differs.
- Display: camp-menu captures at vblanks 330/360/400/450/499 (runs/cap_camp_{base,fix})
  byte-identical PNGs; the end-of-run VRAM PNGs identical (the scene's destructor moves the
  page back over those rows at exit).
- Suites (engine/build-menufix): ss_tests 99/0, ss_game_tests 182/0 (181 + the new test).

- ASan save prompt shown to open: asan_fix_berle2 (same case + SS_MENU_DUMP_OPEN) rc 0, no
  report; the menu-open dump written ~4 s before the end of the 61 s run, after the move to
  618 (vblank 13758) -- vmfix's unfixed run died there in menu_run_save.
- ASan shop: asan_fix_shop (the smoke's mod_shop route on map 18 without mods, 1710 frames)
  rc 0, no report; gdb_fix_shop (plain) shows menu_run_shop(shop=1) -> MenuScene ctor ->
  upload_tim on that route.
- Title Continue screen: same ctor; its pack 0xbd0 has the same TIMs (unit test covers it).

- Smoke (tools/smoke_field_events.sh engine/build-menufix/shattered-stars): 270 PASS / 0 FAIL,
  rc 0 (the working tree's script has grown past the 261 baseline with skillsanity2's cases).
- Final incremental rebuild rc 0 (one warning, menu_shop.cpp:74 ap_name initializer: another
  agent's hunk, not this fix); ss_game_tests 182/0, ss_tests 99/0 on it.
- Scratch cleaned: only scratchpad/menufix/ss.base (unfixed plain binary) kept, plus runs/.
- No commit (rules). REA sessions closed.

## In progress
- Nothing. DONE.

## Files changed
- engine/src/game/common/menu_scene.h: WindowArt / kWindowArt[2]; upload_tim static, returns bool.
- engine/src/game/common/menu_scene.cpp: the ctor uploads kWindowArt (TIM 0 64x128); upload_tim
  bounds-checks against the pack.
- engine/tests/test_menu.cpp: menu_window_art_fits_pack.
- docs/catalog/functions.csv: notes on field 0x8002f9f8 / title 0x8002fff4 menu_scene_create.
- docs/handoff/menufix.md (this log).

## Next steps
- None for this bug. (Optional: a PCSX-Redux VRAM capture of x 0x180..0x1bf rows 0x180..0x1c7
  with a menu open, to show the console keeps the field page there too; the code is clear.)

## How to verify
- ASan: build engine/build-menufix-asan, then
  `python3 scratchpad/menufix/run.py NAME engine/build-menufix-asan/shattered-stars -- --frames 500
  --map 18 --input 300:triangle,306:none` (camp menu; unfixed: heap-buffer-overflow READ in
  upload_tim), and `SS_DISC1=<cue> python3 scratchpad/menufix/run_scene.py NAME
  engine/build-menufix-asan/shattered-stars claude 575 16000 9999:T0 CH=33 ST=20` (save prompt).
- VRAM: `python3 scratchpad/menufix/run.py NAME BIN GDB_SCRIPT=scratchpad/menufix/vram.gdb --
  --frames 500 --map 18 --input 300:triangle,306:none` writes runs/vram_pre.bin / vram_post.bin
  (1 MB raw VRAM before / after the ctor's uploads); compare with runs/base_*.bin.
- Unit: ss_game_tests menu_window_art_fits_pack (SS_DISC1).
