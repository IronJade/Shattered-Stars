# Handoff: partybuild (build heroslot D1-D4 and the 12-member bench, port side)

Agent "partybuild", started 2026-10-26. Worktree `scratchpad/wt/party` on branch `party-bench`
(from main cd5f1e4). Build dir `<worktree>/engine/build-party` (RelWithDebInfo, AP net + setup,
deps from engine/build-hubleader/_deps via FETCHCONTENT_SOURCE_DIR_*; configure line below).
Scratch (on disk): `<main repo>/scratchpad/partybuild/`; the studies' scratch beside it
(scratchpad/heroslot/, scratchpad/bench/).

Specs: docs/handoff/heroslot.md (D1-D4 decided), docs/handoff/bench.md (B1-B8 decided).

## Rules given

- Work and commit ONLY in the worktree, branch party-bench; never touch the main checkout's
  working tree (an uncommitted fix the user holds back). Never push, never commit on main.
- Port only (engine/, tools/smoke_field_events.sh, docs). **No apworld/ changes** (the APWorld
  part waits for Phase A). Test seeds: hand-edited .apso2 files.
- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); every game
  process under the 4 GB cap (tools/memcap.py or prlimit), at most 2 at once; builds -j8.
  Scratch XDG_CONFIG_HOME; never a config-saving flag; never the user's config, pcsx-redux or
  saves; SS_AP_ANY_SAVE=1 only for headless loads of ordinary saves with a seed. Kill only by
  exact PID. Wait loops with deadlines; cap output.
- Enhance, never degrade; AP off = the disc's game untouched; save card bytes byte-compatible;
  no game data in git. REA for native MIPS; catalog anything newly reversed.
- Comments match the surrounding code; new blocks tagged [partybuild agent].
- Commit after each verified piece, docs first, message ending with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
- Suites before each commit: ss_tests, ss_game_tests, tools/smoke_field_events.sh (all PASS).
- Keep this file current (after each piece, at least every 30 min). Roll over at ~400k context
  (update this file, commit what is verified, end with a short status); past ~600k stop.
- Blocked on a design question that changes the play state: write it here and stop.

## Design decisions (beyond the specs)

- **The bench is a separate, port-owned store** (the user, 2026-10-26, verbatim: "Lets go with a
  separate store from the start, as then we own the method to maintain the benched members
  equipment and settings. Then we can fill in the 8 slots the game save and what not call for
  from the store."). So: the game's party / record blocks stay 8 places (create_game_state keeps
  0x300 / 0x680; no original code sees more). The store is four entries of 0x130 bytes (slot
  0x60 + record 0xd0), places 8-11, in ap::SaveState, saved in the sidecar (`bench`,
  `bench_hash`). A Party Order swap fills a game slot from the store when a member comes in and
  empties a slot into the store when one goes out.
  - B2 (benched members in Equipment / Skills / Status / Specialty / Tactics): a port-side path
    that points the selected-member accessors (party_select / party_selected_slot /
    party_selected_record) at a store entry while the page works on it. A store entry is never
    written into the game's 8-slot blocks just to show it.
  - Party Order: a working view over the 8 slots plus the store, committed back on close.
- D2 / D3 (heroslot) only with a starting member who is not the hero (open world / hub): every
  other seed plays today's paths byte for byte. The memory ("the lead", "the slot-0 member") is
  set at D1's swap and at every camp-menu close outside the locked states below; it is applied
  at free play (after party_v2_update). Locked (nothing applied, the memory not updated): a
  Private Action (flags 2 / 4 / 746), a split window keeping everyone away, a window needing a
  member for a while, temporary members / a stash present, the endings, a hub story view.
  Sidecar keys `leader` / `slot_0` (party ids, written only when set).
- **Benched members are managed, not used** (the user, 2026-10-27, verbatim: "Benched members
  are not live in the game, so follow your recommendation on that. I think introducing them as
  viable when they are not currently, until they enter the party or the reserves breaks the play
  of the game. They can be there, they can be brought in, but they are not there without
  explicitly bringing them in to be used."). Allowed for a benched member (B2): Equipment,
  Skills (spending their SP, battle skills on / off), Tactics orders, Status, the Specialty page
  for viewing. Refused: every Specialty action (Oracle, music, Practice, Scout, Familiar, any
  other specialty use), field spells cast by them (their MP, on anyone), anything else where a
  benched member does something in the game: the menu's refusal sound and, where it fits the
  page, a short help-bar note.

## Plan (pieces, each committed once verified)

1. heroslot D1-D4: the join swap, the lead and slot-0 follow-up across scenes, menu-close memory.
2. The bench store, sidecar `bench` + `bench_hash`, sidecar version 3 (only with members
   benched), load / save round trip, the load guard; options.bench, seed versions 1-4.
3. The AP join path to the bench; bench-aware lookups (receive_move, the killer scan, the median
   over the 8 slots).
4. Party Order: B1 changed by the user (below): Option A of the mock-up (two columns, the right
   one flips reserve / bench with L1 / R1); the refusal rules.
5. The other camp pages (Equipment, Skills, Status, Specialty, Tactics) for bench members.
6. Lending for solo fights (B4), inns not healing the bench (B8), endings unchanged (B5).
7. Docs: seed_format.md, engine/README.md, docs/catalog.

## Status

Agent "partybuild" stopped at ~394k context (a permission prompt that never reached the user);
agent "partybuild2" continues from here (2026-10-26).

- Build dir configured and the baseline (cd5f1e4) built (rc 0) under scratchpad/partybuild/base/.
- **Piece 1 (heroslot D1-D3): done, committed on party-bench** (partybuild2: ss_tests 103/0,
  ss_game_tests 194/0, smoke 300/0 with the build-party binary). Files: engine/src/game/ap/ap.{h,cpp}, engine/src/game/field/menu_camp.cpp,
  engine/tests/test_ap.cpp, tools/smoke_field_events.sh, docs/ap/seed_format.md,
  engine/README.md, apworld/so2/docs/en_Star Ocean The Second Story.md (the player guide's
  starting_character paragraph: port behaviour), this file.
  - D1: at New Game's first free play the starting member takes slot 0, the hero slot 1, and the
    starting member leads. D2 / D3: `leader` / `slot_0` (sidecar) remembered at the camp menu's
    close or a save; put back at the next free play after a scene reorders. Not during PAs,
    split windows, temporary members / a stash, the endings, hub story views. Debug switch
    SS_HEROSLOT_ORDER=LEAD,FIRST (environment) for `--map` scene tests.
  - Checked: ss_tests 103/0, ss_game_tests 194/0 (new ap_heroslot; that run predates the last
    small additions to it, which pass alone). Against the cd5f1e4 baseline: Claude's opening with
    Ashton differs only by the two swapped slots and the leader slot (rng, vblank identical);
    no-starting-member seeds and story mode byte-identical; tournament 477 byte-identical inside
    its window. Map 12 puts Ashton back in slot 0; Linga with a battle: Opera leads again; a
    debug battle: Ashton controlled, both gain EXP. New smoke cases ap_hs_m12, ap_hs_linga,
    ap_hs_t477, ap_hs_save, ap_hs_load pass. Full suites (partybuild2, 2026-10-26): ss_tests
    103/0, ss_game_tests 194/0, smoke 300/0 (scratchpad/partybuild/smoke2.log).
- **Merge of main (c445867) committed** (8be778b; no conflicts): ss_tests 103/0,
  ss_game_tests 194/0, smoke 303/0.
- **Piece 2 (the bench store): done, committed** (23b403f; smoke 303/0, smoke5.log). New files
  engine/src/game/ap/bench.{h,cpp} (struct Bench: places 8-11 of 0x130 bytes; bench_json /
  bench_from_json; bench_digest). ap.h SaveState `bench` + `bench_hash`; ap.cpp sidecar
  (version 3 only with someone benched; keys only then), on_saved (bench_hash), on_loaded
  (taken as is), load_allowed (bench_hash must match: edited / stripped / moved refused),
  bench_on / bench_enabled. seed.{h,cpp}: options.bench (only with party_rules open in open
  world, and the hub), versions 1-4. launcher stamp_sidecar no longer lowers a version 3.
  Tests: ap_bench_store; version messages in ap_seed_errors and test_script_vm. Docs:
  seed_format.md (sidecar keys, the guard, the version rule, "The bench" section).
  ss_tests 103/0, ss_game_tests 195/0.
- **Piece 3 (the join path to the bench): done, committed** (ss_tests 103/0, ss_game_tests
  196/0, smoke 312/0: smoke6.log). Files: ap.cpp join_bench / bench_index,
  join_waiting, on_party_add, scan_killer_moves, party_line, status_text, on_loaded's log;
  skills.{h,cpp} member_init + on_bench_member_init, receive_move; test_ap.cpp ap_bench_join;
  smoke ap_bench_join / ap_bench_save / ap_bench_load / ap_bench_guard; engine/README.md and
  seed_format.md "The bench": Joins, a benched member). ap_bench_save fills the 8 with ap_give
  (one banner each, ~350 frames), so its save input starts 2700 frames late.
  - Join order with the 8 taken: a free bench place, then (as before) a reserve slot of a
    member the port does not keep, then wait. Never the hero, never a temporary member (B3).
    Built as the game's join: field::party_slot_init into the place, the join level over the 8
    slots (B6), skillsanity's pass, the bond bytes cleared. Banner "Noel joined the party
    (bench)"; log "Noel joins the bench (place 8): ... | bench: Noel".
  - A benched member counts as in the party (a queued join of theirs is dropped); the story's
    join of a benched member adds nobody (B4, logged "on the bench: stays there"); a story
    leave finds no slot (no-op). A story join of a new kept member with every slot taken goes
    to the bench.
  - KNOWN GAP until piece 6: a split window that needs a benched member alone (Claude in
    Shingo Forest in Rena's game, Dias / Claude at the tournament) does not lend them yet: the
    story's join is refused like any other (the fight would start without them).
- Leftovers: symlink `analysis` in the worktree root -> the main repo's analysis/ (the North
  City smoke case needs the user's save). UNTRACKED, not ignored: never commit it; remove it
  when the work is done (or a local exclude in the worktree's info/exclude).
- Rule update (partybuild2): main has c445867 (the Salva Drift door fix: ap.{h,cpp}
  door_shut_at_trigger, script_triggers.cpp ShutDoor, test_ap.cpp, smoke ap_hub_mansion /
  ap_p4_door, seed_format.md, salvadrift.md, options.py docstring). After piece 1's commit:
  `git merge main` into party-bench (merge commit, no rebase), keep both sides, rebuild, all
  three suites, commit the merge. The main checkout's working tree has another agent's
  uncommitted apworld/so2/test/ files: still never touch it.

- **partybuild2 rolled over here** (~350k context, 2026-10-26): pieces 1-3 and the merge are
  committed; next is piece 4 (notes below). A fresh agent starts from this file.
- **Agent "partybuild3"** continues from here (2026-10-26), pieces 4-7.
- **B1 changed (the user, 2026-10-26, verbatim):** "In scratchpad/bench/shots/mock_party_order.png
  it shows Option A as 2 pages of 2 columns. The party(active) and the reserve, and on an R1/L1
  press it changes page and shows the reserves and the bench. That I believe is better than the 3
  columns that cut off text." Built as the mock's panels A: the left column always the front four
  (slots 0-3, full boxes), the right column the reserve (4-7) or, after L1 / R1, the bench
  (8-11); a first pick survives the flip. **Alternative reading (for the user to say if meant):**
  "page 2 shows the reserve and the bench side by side" (the front four off screen on page 2).
  The mock shows the front four plus the bench, and the user pointed at the mock, so the mock
  was built.
- **Piece 4 (Party Order with the bench): done** (ss_tests 103/0, ss_game_tests 197/0, smoke
  slice ap_bench_* all PASS; full smoke: see below). Files: menu_combat.cpp (BenchOrderPage,
  used only when ap::bench_enabled(); PartyOrderPage untouched but draw_member_box's body moved
  to draw_order_box), party.{h,cpp} (PartyPlaces: party_bind_places / party_bound_places /
  party_place_count / party_place_slot / party_place_record; party_select(8-11) when bound),
  menu_widget.cpp (draw_party_status via party_place_slot; add_party_character scans the bound
  places), menu_gfx.{h,cpp} (set_extra_sprite_packs / sprite_gfx_cached: sprite_find_pack falls
  back to extra packs), menu_camp.cpp (bench_sprite_packs: the camp menu reads the packs of
  members the field did not load, 0xC27 + 2k / 0xC67 + b as map.cpp), ap.{h,cpp} (bench_store,
  bench_locked = order_locked, bench_swap_refused, on_bench_order_closed), test_ap.cpp
  (ap_bench_swap_rules), smoke ap_bench_swap, seed_format.md ("Party Order", "Refused moves",
  "Checked"), engine/README.md.
  - The flip uses the menus' page buttons (config buttons 4 / 5 = L1 / R1 by default; Widget's
    on_shoulder is config 6 / 7 = L2 / R2, the member switch, not used here). The hint replaces
    the logo in the help bar (logo_state pinned to 2): `L1/R1: Reserve 5-8` / `L1/R1: Bench
    9-12` (8x8 font, 144 px from 0xA4), second line `Held: Precis` when the held pick is off
    screen, else `Bench locked` when locked. The held hand: the first cursor's flag 1 cleared
    (Cursor::update skips its draw) while its column is off screen.
  - Working copy: 12 x 0x60 + 12 x 0xd0 in the page; party_bind(work) + party_bind_places(work
    8-11) for the page's life (nothing else runs during the menu: menu_run_camp is a blocking
    loop; AP gives only in free play). The destructor copies back (8 slots, then the store,
    bench bonds cleared), rebinds what was bound before, then the original close's steps.
  - Proofs (scratchpad/partybuild/p4/): bcard.mcd (Ashton, Claude, Rena, Celine | Bowman,
    Precis, Leon, Opera | bench Noel, Ernest; made by ap_give on map 18, bench.apso2). po2 vs
    po2c (control, picks cancelled): memb.py: every member's 0x130 bytes identical except the
    stat pass's derived figures (slot +0x2c..+0x4c, record +0x2..+0xa; StatCalc::all runs for
    the 8 when the camp menu page is rebuilt, item bonuses only once item_data is in); state /
    config / inventory identical. No seed (bench study's eight.mcd, swap 0 <-> 7 with L1 / R1
    pressed): state dump and three captures byte-identical to the baseline binary (base/).
  - Captures for the user: scratchpad/partybuild/shots/po1..po7 and po_sheet(_x2).png (reserve
    page, bench page after R1, Precis picked, R1 with Precis held off screen ("Held: Precis"),
    Precis benched, Noel held with L1 back on the reserve, the camp menu with Noel in).
  - Note for piece 5: StatCalc::all covers the 8 only, so a benched member's derived figures are
    those from when they left the 8 (or from their join); B2's camp menu should run the stat
    pass over the bench too.

- partybuild3 stopped at ~362k context (2026-10-26: an image Read denied by the session's old
  auto mode, since turned off). **Agent "partybuild4"** continues (2026-10-27).
- **Piece 4 committed** (7a43580; partybuild4): the full smoke rerun from the frozen binary
  bin_p4 and a frozen script copy passed 316/0 (scratchpad/partybuild/smoke8.log; smoke7.log
  had ended early when partybuild3's session stopped).
- **Piece 5 (B2: the other camp pages on benched members): verified** (ss_tests 103/0,
  ss_game_tests 198/0; full smoke: see below). Files: menu_camp.cpp (menu_run_camp binds the
  store's places 8-11 for the whole camp menu; CampMenu rows_ = party_place_count(): 12 rows),
  menu_widget.{h,cpp} (Panel::build_members over the places, members[12]: L2 / R2 through
  12), menu_stats.cpp (StatCalc's level pass and all() over the places), menu_combat.cpp
  (BenchTacticsPage: Tactics with the same flip; draw_flip_help / page_button shared with
  BenchOrderPage), menu_skills.cpp (SuperDetail's 8-slot grid starts at 0 for a benched
  member), menu_specialty.cpp (no voice reload for places 8-11), test_ap.cpp
  ap_bench_camp_pages, smoke ap_bench_camp / ap_bench_camp_load, seed_format.md ("The camp
  menu's other pages", "Checked (2026-10-27 ...)"), engine/README.md.
  - Readings (in seed_format.md; for the user to overrule): a benched member may use the
    Specialty page as a reserve member may (Oracle, music, Practice, Scout, Familiar, field
    spells with their own MP); spell targets, Link Combo partners (the front four, as the
    original), item use, Item Creation, super specialties and the Equipment Wizard at the
    menu's close stay with the 8. Link Combo from a benched member: as from a reserve member
    (party_find_link_partner scans the front four only, in the original too).
  - Proofs (scratchpad/partybuild/p5/; card p4/bcard.mcd: bench Noel, Ernest): eqtac run:
    Noel's weapon off (Equipment), his orders +1 (Tactics, bench page), save: in the sidecar
    only Noel's weapon / orders / EW bit and both benched members' derived figures differ
    (p5/benchdec.py before after). The stat pass filled in derived figures a bench join leaves
    at zero (Status shows them). Negative control: ap_bench_camp fails on bin_p4.
  - The flock of white sprites after the camp menu closes on map 18 (p5/eqw_sheet.png) is the
    map's own effect: the piece-4 binary shows it at the same frames (p5/eqw_p4_sheet.png).
  - Captures for the user: p4/eq_sheet.png (member list rows 9-12, Equipment for Noel, R2 to
    Ernest, Status), p5/tac_sheet.png (Tactics: reserve page, R1 bench page, Noel's orders, the
    new order kept across L1 / R1), p5/spsk_sheet.png (Specialty and Skills for Noel / Ernest,
    L2 / R2), p5/eqtac_sheet.png (the picker's figures for a benched member, the save).

- partybuild4 stopped (2026-10-27: its permission prompts failed). **Agent "partybuild5"**
  continues (2026-10-27): commit piece 5, then piece 5b (below), 6, 7.
- State partybuild4 left (from its final report):
  - Piece 4 committed 7a43580: smoke 316/0 (smoke8.log), ss_tests 103/0, ss_game_tests 197/0.
  - Piece 5: ss_tests 103/0, ss_game_tests 198/0 (new ap_bench_camp_pages); smoke
    ap_bench_camp / ap_bench_camp_load pass (bench slice 16/16; they fail on the piece-4
    binary); the sidecar proof (only Noel's weapon / orders / EW bit and both benched members'
    derived figures changed). The full smoke from frozen bin_p5 + smoke_p5.sh into smoke9.log
    had finished unread: **read by partybuild5: 319 PASS / 0 FAIL, rc=0**. Patch for piece 5
    alone (with its docs and handoff notes): scratchpad/partybuild/piece5.patch
    (`git apply --cached ../../partybuild/piece5.patch` from the worktree).
  - Piece 6 (loans for solo fights) written in ap.h / ap.cpp by partybuild4, NOT compiled: a
    sidecar key `lent` (version 3 when a loan is out); the benched member's own bytes go into
    the slot the game's join would use (else a borrowed reserve slot), their bench place kept
    empty; at free play they go back with what the fight gave them, once the window ends or
    they are set aside; Party Order stays locked while a loan is out. Inns not healing the
    bench (B8) and the endings ignoring it (B5) need no code: those routines read the 8 slots.
  - Captures for the user: shots/po_sheet.png, p4/eq_sheet.png, p5/tac_sheet.png,
    p5/spsk_sheet.png, p5/eqtac_sheet.png.
  - The white-sprite flock after the menu closes on map 18 shows with the piece-4 binary at
    the same frames: not from this work.
- **Piece 5b (new, the user's decision of 2026-10-27, see the design section): benched members
  are managed, not used.** Equipment, Skills (SP, battle skills on / off), Tactics, Status and
  viewing the Specialty page stay; every Specialty action and field spells cast by a benched
  member are refused (the menu's refusal sound, a help-bar note where it fits). This replaces
  piece 5's reading "a benched member may use the Specialty page as a reserve member may".
- Merge care (for the coordinator): main is being changed by "pendingsave" (the give queue and
  the save sidecar in engine/src/game/ap/ap.cpp) and "skilllogic2" (apworld/). ap.cpp, the
  sidecar keys (`bench`, `bench_hash`, `leader`, `slot_0`, `lent`, version 3) and the player
  guide under apworld/so2/docs/ will need care when party-bench is merged. Don't merge main
  into party-bench from here.

## Working notes (partybuild2)

- **Never edit tools/smoke_field_events.sh while a smoke run is going**: bash reads the script as
  it runs, and an edit broke a run mid-way (smoke4.log). Run a frozen copy instead
  (`git show HEAD:tools/smoke_field_events.sh > scratchpad/partybuild/smoke_pN.sh`, then
  `bash smoke_pN.sh engine/build-party/shattered-stars` from the worktree root).
- Pipeline used: build a piece, run ss_tests + ss_game_tests, snapshot it
  (`git add -N` new files; `git diff > scratchpad/partybuild/pieceN.patch`), start the smoke in
  the background, write the next piece meanwhile (no builds while a smoke runs: same binary);
  when the smoke passes, `git apply --cached pieceN.patch` stages exactly that piece; commit.
- `scratchpad/partybuild/syncheck.py FILE...` (paths under engine/): -fsyntax-only with
  build-party's flags, safe while a smoke runs.
- Smoke runs: `TMPDIR=scratchpad/partybuild/tmp XDG_CONFIG_HOME=scratchpad/partybuild/config
  SS_DISC1=<disc 1 cue> timeout 14400 tools/smoke_field_events.sh engine/build-party/shattered-stars`
  (about 50 minutes, ~305 checks). Unit suites: the same XDG_CONFIG_HOME and SS_DISC1,
  SDL_*_DRIVER offscreen / dummy, `ulimit -v 4194304`.

## Next pieces: notes for the next agent

- **Piece 4, Party Order (B1: three columns that scroll two at a time).** Today's page:
  engine/src/game/field/menu_combat.cpp PartyOrderPage (8 TextBoxes 0x9c x 0x32 at
  (col * 0x9c, row * 0x32), make_member_boxes; cursor->add_grid(&choice_, 1, 2, 4, 0, 0x20,
  0x9c, 0x32, nullptr); box -> slot by table 0x800808e0; swap_members copies slot + record;
  the destructor reloads front voices, clears bond bytes of slots 4-7, re-points config +0x41).
  Reading of B1 (to confirm in the first capture): three columns of four (front 0-3, reserve
  4-7, bench 8-11); two fit the screen (2 x 0x9c = 312 px); the view slides one column
  (TextBox dx, as OptionsPage::scroll moves its screens with dy, menu_options.cpp ~239) when the
  cursor goes into the column off screen. A variant page used only when ap::bench_enabled() (the
  original class untouched otherwise); a 12-entry working copy (8 slots + the store) committed
  on close before the destructor's steps (bond clear for 4-11). Refusals (sfx 7; bench.md
  Design 2): the hero to the bench; a temporary member to the bench; a set-aside slot (negative
  id) with a bench place; any bench move during a PA (flags 2 / 4 / 746), a window keeping
  everyone away, a stash, the endings (0x2f1), a hub story view (ap.cpp order_locked has the
  same tests: expose one predicate); a member a window keeps away into the 8; and, as today, a
  swap leaving no front member able to fight. draw_party_status reads party_slots() + index *
  0x60 (menu_widget.cpp ~717): a bench box needs its own status source (party_bind to a 12-slot
  working block is the study's idea). Captures: --capture / SS_VRAM_PNG.
- **Piece 5 (B2):** Equipment / Skills / Status / Specialty / Tactics for bench members: a
  port-side path pointing party_select / party_selected_slot / party_selected_record
  (game/common/party.h) at a store entry while the page works on it; never copy a store entry
  into the 8 just to show it. The camp page's member list then lists 12.
- **Piece 6:** lending (B4): window_needing(id) for a benched member -> lend into a free slot (or
  borrow_slot) for the window, back to its bench place at the window's end (a sidecar list like
  `temporary`, e.g. `lent`); inns (B8 no): party_restore_hp_all / _mp_all touch the 8 slots only
  (confirm, nothing to do); endings (B5): benched members get none (nothing to do; confirm the
  ending selection reads the 8 only).
- **Piece 7:** docs (seed_format.md "The bench" grows per piece; engine/README.md; docs/catalog
  for anything newly reversed; apworld's part waits for Phase A: PARTY_SIZE 12, options.bench
  default on, version 4).

## Configure

    D=<main repo>/engine/build-hubleader/_deps
    cmake -S engine -B engine/build-party -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSS_BUILD_AP_NET=ON -DSS_BUILD_SETUP=ON -DFETCHCONTENT_SOURCE_DIR_APCLIENTPP=$D/apclientpp-src \
      -DFETCHCONTENT_SOURCE_DIR_ASIO=$D/asio-src -DFETCHCONTENT_SOURCE_DIR_IMGUI=$D/imgui-src \
      -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$D/nlohmann_json-src \
      -DFETCHCONTENT_SOURCE_DIR_WEBSOCKETPP=$D/websocketpp-src -DFETCHCONTENT_SOURCE_DIR_WSWRAP=$D/wswrap-src
    cmake --build engine/build-party -j8
