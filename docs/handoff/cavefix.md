# Handoff: Cave of Trials learn chests (the 0x3c records OOB) — agent "cavefix"

Started 2026-10-25 by the coordinator. Found by "skillsanity" (docs/handoff/skillsanity.md).

## The task
- Port: bounds-check ext 0x73, ext 0xfc, A 0x21 and any other script access indexing a member
  record's +0x3c table by a script value (index < the original's table size). Out of range:
  nothing written, a log line; valid indices byte for byte as before. ASan proof + unit test.
- APWorld 0.2.0: drop the three "Cave of Trials Level 1: Portrait F / G / J chest" rows (maps
  740 / 798 / 799, flags 2675 / 2657 / 2721) from tools/ap_world_tables.py, fix the area names,
  regenerate data.py (cmp identical), world tests + AP general tests + the batch.
- Docs: release-notes line, apworld.md Changes, original_bugs.md only if the original is at fault.

## Rules
Headless only; own build dirs engine/build-cavefix (+ -asan); memcap.py for every game process,
at most 2 at once; scratch XDG_CONFIG_HOME; SDL_AUDIO_DRIVER=dummy; --debug runs stdin </dev/null;
SS_AP_ANY_SAVE=1 for ordinary saves with a seed; scratch in scratchpad/cavefix/; no commits;
suites with counts before "verified" (baseline ss_tests 99/0, ss_game_tests 174/0, smoke 230/0);
log every 20 min; past ~400k tokens update this log and stop. Enhance, never degrade.
Shared files (ap_world_tables.py, data.py): small hunks, dated notes in hubproof.md / shopboth.md.

## Done (log)
- 2026-10-25 started; reading skillsanity's research.
- ~16:50 cause confirmed. Original (REA, prog3100 vm_exec_ext 0x8006241c, jump table 0x8007359c):
  ext 0x73 case 0x80064a30 (slot search, then 0x80064eb8: records 0x80075280 + slot * 0xd0 +
  a1, sb 1 at +0x3c; no bound on a1), ext 0xfc case 0x80064a88. The table is u8[0x20] (record
  +0x3c..+0x5b; +0x5c skill levels, +0x8c u16[0x20] proficiency, +0xcc u8[4] assigned moves;
  class_spell_count 0x8006b1ab: Rena 23, Leon 19, Celine / Noel 21). Every original call site of
  ext 0x73 / ext 0xfc / A 0x21 on both discs (scratchpad/cavefix/scan_ops.py) passes < 0x20.
- ~17:00 unfixed ASan proof (scratchpad/cavefix/unfixed_740.log): late save (copy), released
  0.1.1 seed (elgate gate2/10 Claude, cave_of_trials on, map 740 patch 0x1200000c -> 0x12000501),
  flag 2675 cleared, warp 740 -349 0 -533, up, circle: heap-buffer-overflow READ in A 0x21
  (script_table_a.cpp:180), 509 bytes after the 0x680 records block. Needs
  ASAN_OPTIONS=redzone=2048: with the default redzone the address lands in another live block
  (no report; the scene then says "Leon has learned the spell Extinction!").

- ~17:40 port fix: script_vm.h / .cpp vm_move_index_ok (kRecordMoveSlots 0x20; log line);
  script_ext.cpp ext 0x73 (outside: nothing written, then ap::on_give_item(index): a 0.1.x
  seed's placeholder gives / sends its location's item) and ext 0xfc (outside: nothing);
  script_table_a.cpp A 0x21 (outside: R = 0, nothing read). Tests (test_script_vm.cpp):
  script_vm_learned_move_index_bounds, script_vm_learn_chest_placeholder_is_the_location; both
  FAIL against the HEAD handlers (built once, restored), PASS fixed.
- ~17:50 fixed ASan runs (redzone=2048, same scenario): fixed_740.log (Leon; Rena's Shield
  Sword sent, banner "Sent Shield Sword to Rena.", flag 2675 set, rc 0, no ASan report) and
  fixed_798.log (Ashton; own Fruit Nectar given, "Got Fruit Nectar."). Message window: the
  routine waits in ext 0x7f after wait(400): confirm (circle in this save) at ~1200.
- ~17:55 APWorld: tools/ap_world_tables.py LEARN_CHESTS (the three, with their floors: Level 4 /
  10 / 10) skipped in build(); regenerated data.py differs from the baseline by exactly the
  three rows (scratchpad/cavefix/x_base.py vs x_new.py).

- ~18:30 suites (engine/build-cavefix): ss_tests 99/0, ss_game_tests 176/0 (174 + the two new),
  smoke 246/0 (the tree's current count). AP: world tests 318 passed; general 379 passed.
- ~18:40 batches, with two private Archipelago copies (current apworld/so2; data.py before /
  after), so only this change differs: the 96-seed batch (apworld2 players/) 96/96 exit 0 both;
  check.py on after: 192 files, 0 errors. after vs before: the 31 seeds without a cave_of_trials
  player byte-identical (62 files); the 65 with one differ (130 files: placements; each cave
  file has exactly 3 locations fewer, none of the three ids). Story identity (players_base, the
  16 all-story seeds) vs 0.1.1 (elgate gate2): 6 files identical but options (seeds 39 / 71 / 96,
  no cave), 26 differ (the 13 seeds with a cave player). Before this change that was 27 / 5: 5
  files (seeds 14 P3, 51, 68) already differed in locations from another change in the tree
  (likely "gift sites never hold progression"), not from this one.
- ~18:45 audit (scratchpad/cavefix/audit_sites.py): of every chest / gift patch site of data.py,
  exactly these three led to a routine with ext 0x73 / ext 0xfc / A 0x21; none after.
- Docs: apworld.md (counts 1110 rows, 308 chests / 75 in the cave, 78 cave chests and gifts; Left
  out; Changes 0.2.0 "Fixed: the Cave of Trials' learn chests are no locations"); the player
  guide's counts; script_opcodes.csv ext 0x73 / ext 0xfc / A 0x21 (addresses, the bound, the port
  guard); field_part4.md the record layout (+0x3c u8[0x20], +0x5c u8[0x30], +0x8c u16[0x20]).
  original_bugs.md: not touched (the original passes valid indices only).
- Release-notes line (for 0.03.00): "Archipelago: so2.apworld 0.1.x seeds with cave_of_trials no
  longer corrupt memory at the Cave of Trials' three learn chests (Leon's Extinction, Ashton's
  tri-Ace, Celine's Meteor Swarm): nothing is learned, and the chest gives or sends the item its
  location holds. so2.apworld 0.2.0 drops those three locations."

## In progress
- Nothing: done, not committed (no commits by rule).

## Next steps
- Skillsanity: bring the three back as learn points (LEARN_CHESTS has member, move and floor).
- The other 75 cave chests still say "Cave of Trials Level 1" (the released-name policy,
  RELEASED_AREAS); renaming them by floor is the user's call (names, trackers, every cave seed's
  fill).

## How to verify
    cmake --build engine/build-cavefix -j8
    SS_TEST=script_vm_learn engine/build-cavefix/ss_game_tests     (2 PASS)
    python3 tools/ap_world_tables.py -o scratchpad/cavefix/x.py && cmp scratchpad/cavefix/x.py apworld/so2/data.py
    ASan proof: scratchpad/cavefix/env.sh + the late save copy + released.apso2; the commands are in
    the logs above (unfixed_740.log / fixed_740.log / fixed_798.log), ASAN_OPTIONS=redzone=2048:
    SS_DEBUG_INPUT="100:flag 2675 0;150:warp 740 -349 0 -533" --input "520:up,570:none,600:circle,
    610:none,1200:circle,1206:none,1260:circle,1266:none" --frames 1750 (SS_LOAD_SAVE=0).
