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

## Plan (pieces, each committed once verified)

1. heroslot D1-D4: the join swap, the lead and slot-0 follow-up across scenes, menu-close memory.
2. The bench store, sidecar `bench` + `bench_hash`, sidecar version 3 (only with members
   benched), load / save round trip, the load guard; options.bench, seed versions 1-4.
3. The AP join path to the bench; bench-aware lookups (receive_move, the killer scan, the median
   over the 8 slots).
4. Party Order: three columns scrolling two at a time; the refusal rules.
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
- **Piece 2 (the bench store): done** (committed once its smoke passed). New files
  engine/src/game/ap/bench.{h,cpp} (struct Bench: places 8-11 of 0x130 bytes; bench_json /
  bench_from_json; bench_digest). ap.h SaveState `bench` + `bench_hash`; ap.cpp sidecar
  (version 3 only with someone benched; keys only then), on_saved (bench_hash), on_loaded
  (taken as is), load_allowed (bench_hash must match: edited / stripped / moved refused),
  bench_on / bench_enabled. seed.{h,cpp}: options.bench (only with party_rules open in open
  world, and the hub), versions 1-4. launcher stamp_sidecar no longer lowers a version 3.
  Tests: ap_bench_store; version messages in ap_seed_errors and test_script_vm. Docs:
  seed_format.md (sidecar keys, the guard, the version rule, "The bench" section).
  ss_tests 103/0, ss_game_tests 195/0.
- Leftovers: symlink `analysis` in the worktree root -> the main repo's analysis/ (the North
  City smoke case needs the user's save). UNTRACKED, not ignored: never commit it; remove it
  when the work is done (or a local exclude in the worktree's info/exclude).
- Rule update (partybuild2): main has c445867 (the Salva Drift door fix: ap.{h,cpp}
  door_shut_at_trigger, script_triggers.cpp ShutDoor, test_ap.cpp, smoke ap_hub_mansion /
  ap_p4_door, seed_format.md, salvadrift.md, options.py docstring). After piece 1's commit:
  `git merge main` into party-bench (merge commit, no rebase), keep both sides, rebuild, all
  three suites, commit the merge. The main checkout's working tree has another agent's
  uncommitted apworld/so2/test/ files: still never touch it.

## Configure

    D=<main repo>/engine/build-hubleader/_deps
    cmake -S engine -B engine/build-party -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSS_BUILD_AP_NET=ON -DSS_BUILD_SETUP=ON -DFETCHCONTENT_SOURCE_DIR_APCLIENTPP=$D/apclientpp-src \
      -DFETCHCONTENT_SOURCE_DIR_ASIO=$D/asio-src -DFETCHCONTENT_SOURCE_DIR_IMGUI=$D/imgui-src \
      -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$D/nlohmann_json-src \
      -DFETCHCONTENT_SOURCE_DIR_WEBSOCKETPP=$D/websocketpp-src -DFETCHCONTENT_SOURCE_DIR_WSWRAP=$D/wswrap-src
    cmake --build engine/build-party -j8
