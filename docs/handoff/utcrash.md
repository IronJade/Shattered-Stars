# utcrash: Universal Tracker 0.3.6 drops the connection at connect (0.03.00 RC Windows recheck, item 10)

Agent "utcrash", worktree branch `worktree-agent-a8de0c5db8626d9dc` (fast-forwarded to main d9ae923 first:
the worktree started at the pre-restructure 2c38965). Started 2026-10-28.

## Rules given
- Worktree only; the main tree read-only (its scratchpad/ap, scratchpad/ap_ut, dist); stay out of port/src
  (agent "loadsend" is in port/src/game/ap/ap.cpp). Never patch or monkeypatch UT; the fix goes in apworld/so2.
- `nice -n 10`; no GUI on the user's display (Kivy offscreen or headless); never touch
  ~/.local/share/Archipelago or ~/.config/Archipelago. Scratch in scratchpad/utcrash/. Ports: not 38281,
  38282, 38461 (the test's default is 38471, the old room 38472).
- Commit on the branch (docs first); never push; don't merge; the coordinator merges and edits
  scratchpad/release/rc-0.03.00.sh.

## Log
- tracker.apworld in scratchpad/ap_ut is the Tracker_v0.3.6 asset (sha256 39e9bc0f..., same as `gh release view
  Tracker_v0.3.6 -R FarisTheAncient/Archipelago`).
- Root cause read from UT 0.3.6 (TrackerClient.py): see the report section below once written.
- trackers/tools/ap_ut_connect.py written: UT's own GUI client (Kivy on SDL's offscreen driver), a real
  MultiServer room, the port's writes from a slot client; one UT session per case.
- utcrash's runs (scratchpad/utcrash/run_before.log; its run_after.log was overwritten by utcrash2's
  first run, so its results are only the ones summarized here): before the fix 5 of the 6 counted
  cases dropped the connection with the Windows traceback (`AttributeError: 'VisualTracker' object has
  no attribute 'location_icons'`, TrackerClient.py 1885 > 1916 > 1520); with the fix launch, room-key,
  world-key, dialog and old-room pass (markers at (948,1250) Expel / (720,1000) Energy Nede); `cancel`
  hung: a test bug (ctx.maps[66] read with ctx.maps empty, the dialog cancelled: IndexError, then
  asyncio.run waiting forever on Kivy's task since the window was never stopped). Stopped there
  (the user's editor crashed).

## utcrash2 (2026-10-28): finishing it
Same rules, plus: start MultiServer directly with the main tree's ap-venv (never `uv` or
apworld/tools/ap_test_server.sh); ports not 38281, 38282, 38461, 38471 (this one runs 38475/38476);
long runs as `systemd-run --user --unit=utcrash2-<name>`; keep the player marker; the Auto Tab case
is a known issue (UT's), documented with its workaround.

- Done: ap_ut_connect.py: the default page only when 0 <= map_page_index(None) < len(ctx.maps);
  `drive` = `steps` + finish() always (a step that raises prints a FAIL, then the cleanup);
  finish() tolerates no ctx / no ui; the client's log records echoed as `log  ` lines in each
  case-NAME.log. scratchpad/utcrash/connect_run.sh NAME ARGS... runs it headless (run_NAME.log,
  run_NAME.rc the done marker).
- Done: the full set with the fix, the repo's world (run_after.log, ut_after/): **0 cases failed**:
  launch 37 ok, room-key 20, world-key 15, dialog 16, cancel 7, old-room 15 (0 FAIL each);
  auto-tab-off (informational) still drops the connection, the same AttributeError.
- Done: the full set with the PRE-fix ut_map.py (HEAD's, swapped in and restored: cmp identical
  to the fix; run_before2.log): **5 cases failed**: launch, room-key, world-key, dialog, old-room,
  each "UT dropped the connection: AttributeError: 'VisualTracker' object has no attribute
  'location_icons' (TrackerClient.py:1885 on_package; 1916 update_location_icon_coords; 1520
  update_location_icon_widgets)"; cancel 7 ok (no map tab, no crash, as before).
- Done: the builds (scratchpad/utcrash/build.sh, build.log): apworld/tools/build_apworld.sh
  scratchpad/utcrash/dist/apworld: so2.apworld 282,098 bytes, sha256 2f7cf041...a66c9, no
  __pycache__, its ut_map.py = the fix; trackers/tools/build_poptracker.sh all: 1201 files, zip
  5,559,703 bytes, sha256 8154d618...db091f716 (the main tree's dist zip, byte for byte), "Validated
  11 files" for the zip and the folder, versions.json ok, ap_poptracker_test.py 43 OK.
- Added to ap_ut_connect.py's informational auto-tab-off case: the way back (/toggle_auto_tab on
  again, /connect) and Auto Tab off once connected while the game moves (both "info Auto Tab" lines).
- Done: ap_poptracker_utmap.py 16/16 in all four (repo world / packaged x pack zip / UT zip).
- Done (scratchpad/utcrash/tests.sh, t/summary.txt and t/*.log): ap_ut_check.py with the packaged
  apworld (port 38477): UT MATCHES THE WORLD (59). ap_ut_connect.py as the RC step (packaged apworld,
  the built pack zip, the UT zip, the 0.2.1 room; ports 38475/38476): passed, 0 cases failed, 115 ok
  (launch 37, room-key 20, world-key 15, dialog 16, cancel 7, old-room 15, auto-tab-off 5); its info
  lines: Auto Tab off at the first connect drops the connection (the AttributeError); back on +
  /connect: stays connected, world_expel laid out; off once connected, the game moving: stays
  connected, the marker at (948, 1250). The world's tests as ph_world, in the worktree's farm:
  worlds/so2/test 882 passed, 3 skipped (68023 subtests); AP_TEST_WORLDS=so2 test/general + world
  935 passed, 3 skipped (73284); test/general 379 passed (117752); SO2_SLOW_TESTS 885 passed (68708).
- Docs: the known issue (Auto Tab off before connecting) in docs/release/notes/0.2.2.md "Known
  limitations" and docs/apworld/apworld.md (with the results bullet); the checklist row in 12.3.
- Next: commit (docs first, then code and tests); the coordinator merges, adds the RC step below,
  and may report the Auto Tab issue upstream (UT 0.3.6: load_map(None) returns early with Auto Tab
  off, so VisualTracker never gets location_icons before update_location_icon_widgets runs).

## How to verify
`scratchpad/utcrash/connect_run.sh NAME --zip PACKZIP --zip UTZIP --old-apworld .../apworld-0.2.1/so2.apworld
--port 38475` (headless; run_NAME.log, `scratchpad/utcrash/summarize.sh run_NAME.log` per case), or the
RC step below; the pre-fix check: d9ae923's apworld/so2/ut_map.py swapped in (then restored), 5 cases fail.

## The RC step (for the coordinator: scratchpad/release/rc-0.03.00.sh, not edited here)
In `ph_extra`, after `ut_map_pkg_port`'s note (it needs `render_capture`'s `$DIST/linux-render/$UTZIP`
and `ph_build`'s `$DIST/apworld`, `$DIST/poptracker`); port 38471 and 38472 (the 0.2.1 room); about a
minute:

```bash
  # UT's own client and window through a real connection (trackers/tools/ap_ut_connect.py; utcrash): the
  # packaged apworld, the pack zip preset, the render's UT zip through UT's dialog, the 0.2.1 room.
  step ut_connect bash -c "cd '$MAIN' && env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS prlimit --as=$((8<<30)) timeout 3000 '$VENV/bin/python' trackers/tools/ap_ut_connect.py --tracker '$MAIN/scratchpad/ap_ut/tracker.apworld' --apworld '$DIST/apworld/so2.apworld' --old-apworld '$MAIN/dist/apworld-0.2.1/so2.apworld' --zip '$DIST/poptracker/so2_shattered_stars_$PACKV.zip' --zip '$DIST/linux-render/$UTZIP' --ut-dir '$DIST/ap_ut_connect' --port 38471 > '$DIST/ut_connect.log' 2>&1; rc=\$?; tail -3 '$DIST/ut_connect.log'; [ \$rc = 0 ]"
  note "UT connect: $(grep -a -m1 -E '^(passed|FAILED)' "$DIST/ut_connect.log" || echo 'no verdict line found'); $(grep -ac '^  ok ' "$DIST/ut_connect.log") ok, $(grep -ac '^  FAIL' "$DIST/ut_connect.log") FAIL; auto-tab-off (informational): $(grep -a -m1 '^info auto-tab-off: Auto Tab off at' "$DIST/ut_connect.log" | sed 's/^info auto-tab-off: //' | cut -c1-110)"
```

and `"$DIST/ap_ut_connect"` in `ph_cleanup`'s `rm -rf` list (two Archipelago farms and the rooms).
Expected: `passed: 0 case(s) failed`; 115 ok, 0 FAIL; auto-tab-off "Auto Tab off at the first connect: UT
dropped the connection: AttributeError: ..." (informational: UT's issue, the 0.2.2 notes' known
limitation). Checklist row: docs/release/release_checklist.md 12.3, "UT's map tab through a real
connection (utcrash)".
