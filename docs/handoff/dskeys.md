# Handoff: dskeys (B5: the port publishes map and story position to AP data storage)

Agent "dskeys", started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md). The plan is
docs/plans/trackers.md sections 3.5, 4.5, 5 B5 and 6 (Phase C adds an entrances key on top of this).
The user (2026-10-27): "Next: port 0.03.00 + world 0.2.2, tracker support, from
docs/plans/trackers.md." Decision D6 (the user): yes, in port 0.03.00, only while connected with a
seed.

## The brief
1. `ap_net::Link::set(key, json)`: apclientpp's `Set` with a `replace` operation (and a default), in
   the real client and in the tests' fake Link.
2. In game/ap after the per-frame poll, only while a seed is loaded and `r.link && r.net_ok`:
   - on a map change write `so2_map_{team}_{slot}` = `{"map": id}`, plus `"x"` / `"z"` on a world map
     (maps 1 and 2), throttled (a few writes a second at most, only when the value changed);
   - on a chapter / step change write `so2_story_{team}_{slot}` = `{"chapter": c, "step": s}`;
   - rewrite both after every (re)connect; team and slot from the Connected packet.
   - Hub, story and open world all write the same keys. A marked spot (comment) for Phase C's
     `so2_entrances_{team}_{slot}`; not built.
3. AP off / no seed / offline seed runs behave and save exactly as today: no new state in saves or
   the sidecar, no RNG or timing change. Writes are fire-and-forget network messages only.

Tests: unit (fake Link: change only, throttled, never offline, never without a seed, rewritten on
reconnect, key names); AP-off and no-seed smoke runs unchanged; an e2e step in tools/ap_e2e_so2.sh
(or a companion tools/ap_ds_* script) where a Python client reads both keys back. Own build dir
engine/build-dskeys.

Docs: docs/ap/data_storage.md (new), docs/release_notes/0.03.00.md (DRAFT, shape of 0.02.51.md),
this log.

Files I may touch: engine/src/ap_net/**, engine/src/game/ap/** (small, self-contained),
engine/tests/**, CMake lists only for a new test file, tools/ap_e2e_so2.sh and new tools/ap_ds_*,
docs/ap/data_storage.md, docs/release_notes/0.03.00.md, docs/handoff/dskeys.md. NOT apworld/,
docs/ap/seed_format.md, docs/ap/apworld.md, docs/release_notes/0.2.2.md (agent "utworld" works there
in parallel and may commit at the same time).

## Rules given
- Headless only (`--headless`, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); never a window
  or audible audio. Every game process under tools/memcap.py (4 GB); at most 2 game processes at
  once; builds -j6; scratch XDG_CONFIG_HOME; never a config-saving flag (no --audio / --no-audio),
  never ~/.config/shattered-stars/config.ini or the user's saves; kill only by exact PID.
- Disc 1: SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue"
  (disc 2 beside it, "(Disc 2)").
- Commit on main after each verified piece, docs first, staged by path (never git add -A / -u);
  message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never
  push, tag or publish. index.lock: wait and retry, never delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- Cap command output; long jobs in the background with deadlines; scratch under scratchpad/dskeys/
  (not /tmp, which is RAM).
- Keep this log current after each finished step and at least every 30 minutes (done / in progress
  with files / next / how to verify / rules given). Roll over at ~400k context: update and stop.
- AP off = the disc's game untouched; no debug hooks in AP runs.

## State
- Done (committed: 3a91e09 docs, 802935b code + tests + tools):
  - `ap_net::Link::set(key, value)` (link.h, pure virtual; value = JSON text). Real client
    (client.cpp): a `sets` map (latest value per key) under queue_mutex, sent in flush() as
    `Set(key, default=value, want_reply=false, [replace value])` only while SLOT_CONNECTED, else
    dropped; wakes the worker. test_ap.cpp's FakeLink: a no-op set.
  - game/ap/ap.cpp: Runtime gains ds_team / ds_slot / ds_map / ds_story / ds_map_id / ds_map_at /
    ds_story_at (memory only). kConnected (the seed's own room) stores team / slot and clears the
    last values (both rewritten). `publish_tracker_keys` (anonymous namespace just above
    `vblank()`, called at its end): needs net_ok and link state kSlotConnected, a game_state, not
    the title, map > 0; `so2_map_{team}_{slot}` = {"map":M} (+ "x","z" on maps 1-3: the leader
    actor >> 12 in the field, else game_state +8 / +0x10), `so2_story_{team}_{slot}` =
    {"chapter":C,"step":S}; each on change, at most every 30 vblanks (kTrackerKeyVblanks), log
    lines `[ap] tracker: <key> <value>` (map: only when the map id changes). Phase C comment for
    `so2_entrances_{team}_{slot}` at the end of the function.
  - engine/tests/test_ap_ds.cpp (new; CMakeLists.txt one target_sources line): 9 cases
    (never_without_seed, never_offline, key_names, on_change_only, throttled,
    world_map_position, rewritten_on_reconnect, no_game, every_mode) -- 9/9 PASS.
  - tools/ap_ds_read.py (new): tracker login, Get both keys (names from Connected), --watch N
    with SetNotify prints SetReply values; --done FILE ends the watch.
- Baseline was HEAD 6d2c518 (its binaries, scratchpad/dskeys/ss-base / tests-base, deleted after the A/B).
  World-map walk recipe: `--map 1 --map-pos 51200,0,18500 --input "200:up,500:left,700:none"`.
  Runner: scratchpad/dskeys/run.py BIN LOG TIMEOUT ARGS (memcap, offscreen, dummy audio, scratch xdg).
- Verified so far:
  - ss_game_tests full: 216 tests, 0 failed (207 before + 9 new; scratchpad/dskeys/gametests_new.log).
  - A/B (scratchpad/dskeys/ab.sh, results ab.out): baseline ss-base vs new ss-new, six headless runs
    each (no seed: Arlia 18 -> 26 with SS_TRACE + captures, a camp-menu save, world map 1 walk +
    captures; offline seeds: the tiny chest seed's check + console item + save with sidecar, an
    open-world seed on world map 1 and on Arlia's walk): 20 files (logs, PNG captures, .mcd cards,
    the sidecar) compared, 0 differ.
  - Full smoke tools/smoke_field_events.sh on ss-new (scratch XDG_CONFIG_HOME): 373 PASS, 0 FAIL
    (scratchpad/dskeys/smoke_new.log).
  - e2e tools/ap_e2e_so2.sh engine/build-dskeys/shattered-stars (AP_E2E_WORK=scratchpad/dskeys/e2e):
    49 PASS, 0 FAIL, rc 0; the four game runs under the cap. Step 9: Claude = team 0 slot 2, Dias
    slot 3; the watcher saw {"map":18} (sessions 1, 2), {"map":26} (session 2's walk), then 22
    distinct world-map positions in 22 writes over session 4's 1500 frames (about 176 map units
    apart: the 30-vblank throttle); Get after the runs: so2_map_0_2 {"map":1,"x":50153,"z":18152},
    so2_story_0_2 {"chapter":3,"step":0} (a --map debug start is chapter 3 step 0), so2_map_0_3
    {"map":18}.
  - test_ap_net against tools/ap_test_server.sh (the client's flush changed): 5/5 PASS (TLS skipped
    without a wss server); server stopped by the script.
  - Mutation check: dropping the link-state test, the throttle (0) and the clear on kConnected
    made exactly never_offline, throttled and rewritten_on_reconnect fail; source restored
    (cmp with scratchpad/dskeys/ap.cpp.keep) and rebuilt: 9/9 again.
  - ASan: not run (no agent's flow uses it for the net code; the client's change is a map under
    the existing queue_mutex, like checks / says).
- Docs: docs/ap/data_storage.md (new), docs/release_notes/0.03.00.md (DRAFT: TODO markers for the
  map-render command, world 0.2.2, pack 1.0.0, sums).
- Next: nothing for B5; reported to the coordinator.
- Open / for later agents: B6 decides how UT uses the map key (map_page_setting_key and
  location_setting_key on the same key; the throttle keeps SetReplies to 2/s). Phase C adds
  so2_entrances_{team}_{slot} at the marked spot in publish_tracker_keys. The release notes need
  the map-render command, the world / pack links and the known-limitations list.

## Log
- 2026-10-27: log created.
- 2026-10-27: Link::set, the publisher, 9 unit tests (pass), the reader script.
- 2026-10-27: game tests 216/216, A/B 20/20 identical, smoke 373/0, e2e 49/0, ap_net 5/5,
  mutation check; docs written; committing.
- 2026-10-27: committed 3a91e09 (docs) and 802935b (port, tests, e2e); B5 done.
