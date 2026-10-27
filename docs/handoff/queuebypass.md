# Handoff: queuebypass (a full inventory no longer blocks the give queue)

Agent "queuebypass", started 2026-10-27. Build dir `engine/build-pendingsave` (reused, incremental,
-j4). Scratch: scratchpad/queuebypass/.

## The task (the user, 2026-10-27, verbatim)

"This NEEDS to be in place. the queue, waiting on a stuck "full" slot needs to be temporarily
bypassed until that item can be delivered. This WILL occur with filler items, I am sure.
Blackberries, Blueberries and the like. The ability to bypass a "full" item slot and continue to
recieve items is a mandatory fix."

Also from the user: getting every item already found or sent when a slot starts from New Game is
intended Archipelago behaviour. Don't change it.

Starting point: docs/handoff/pendingsave.md ("noted but not changed": the front item blocks the
queue when its kind is full; a count above 1 that only partly fits loses the extra copies).

## Rules given

- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy). At most 1 game
  process at a time, builds -j4, every game process under the 4 GB cap (tools/memcap.py, prlimit,
  or the smoke script's ulimit).
- Scratch XDG_CONFIG_HOME; never a config-saving flag; never ~/.config/shattered-stars/config.ini or
  the user's saves. Kill by exact PID.
- Enhance, never degrade. Save card bytes byte-compatible. Same-state rule: nothing stuck, behaviour
  and save / sidecar bytes as now; AP off untouched.
- Stay out of apworld/ and tools/ap_batch* (agent "skilllogic2", uncommitted files there) and the
  worktree scratchpad/wt/party (agent "partybuild5", branch party-bench, also edits ap.cpp): keep the
  change self-contained, list the functions changed below for the merge.
- Smoke: don't edit the script while a smoke run reads it; run from a frozen copy.
- Commits on main after the verified fix, docs first (seed_format.md if the sidecar changes,
  engine/README.md if it describes receiving items, this file), staged by path; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push.
- A denied tool call: don't retry or work around; write the blocked step here and stop.
- Keep this file current after each step; roll over at ~400k context.

## Status

- [x] 1. Design (below)
- [x] 2. Code (game/ap/ap.{h,cpp}), built in build-pendingsave. Unfixed binary kept as
  scratchpad/queuebypass/ss-baseline (HEAD 9a44244's build).
- [x] 3. Unit tests ap_full_inventory, ap_full_inventory_join (end of engine/tests/test_ap.cpp):
  PASS; ss_game_tests 196/196.
- [x] 4. Smoke case ap_aside_shop / ap_aside_save / ap_aside_load in tools/smoke_field_events.sh
  (after ap_shop_full; reuses its SHOP1 / SHOP2SELL inputs). Standalone runner
  scratchpad/queuebypass/smoke_case.sh BIN: 15/15 PASS on the fix, 11 FAIL on the baseline
  (evidence/smoke_case_on_{fix,baseline}.txt). Same-state (same_state.sh: items that all fit, a
  chest check, a save, a load, both builds): cards, sidecars, [ap] / [leader] / [debug] lines
  identical (evidence/same_state.txt). Probed in scratchpad/queuebypass/probe.sh (BIN WORK
  shop|save|load|capture): the fix sets the Revival Card aside, gives the key and Fol, gives the card
  after Hearn buys one back (w1/ap_full_shop.log); the baseline holds all three until the sale
  (w0/ap_full_shop.log, `waiting 3` at 900). Save with it set aside: sidecar version 3, `pending`
  [{"from": "Bob", "item": {... 21}}]; a new session loads it, sets it aside again, the sale gives it
  (w1/ap_full_load.log). Frames w1/cap/: the banner "Inventory full: Revival Card waiting" (231 of
  236 px) shows in full. Widths (w2/m.log): Blackberries 225 px, Blueberries 218; longer names fall
  back to "X waits for room", then "No room for X", then "Inventory full: an item waits".
- [x] 5. Full suites on the fix: ss_tests 103/103, ss_game_tests 196/196, smoke (frozen copy
  scratchpad/queuebypass/smoke_frozen.sh, binary ss-fix, TMPDIR on disk) 307 PASS, 0 FAIL, no
  SKIP (292 before + the 15 new; log scratchpad/queuebypass/smoke_full.log).
- [x] 6. Docs written (below); committed on main, docs first, then the port change. Not pushed.

## Design

- `SaveState::Give` gets `aside` (runtime only, never written): the item was set aside, its kind
  full (20 of a kind at most; inventory_add's rule).
- Free play's give step (`give_next`): the first queue entry that can go in (an entry set aside is
  passed over while `inventory_room` says its kind is full), else the server's next item. An item
  that does not fit is set aside where it stands (its order among the others kept), with one banner
  ("Inventory full: X waiting", shorter forms when too wide for the banner; none when another of
  its kind already waits). A server item that does not fit: `received_index` moves on and the item
  goes to the queue set aside (with its sender), so the sidecar's `pending` keeps it (never given
  twice). A count that fits in part: what fits is given (its banner), the rest stays owed (the
  entry's count reduced; a location's item then kept as the item itself).
- Retries: every free-play give step checks the set-aside entries (read only); a menu closed, an
  item used / sold / made, a battle over all return to free play. No banner or log on a failed retry.
- Sidecar: unchanged format (owed items are `pending` entries like any other; `aside` not written:
  a load retries them and sets aside again with one banner).
</content>
</invoke>

## Docs written

- docs/ap/seed_format.md: "Received items" (set aside, banners, retries, partial counts,
  `ap_status`), "The save sidecar" `pending` (set-aside entries, server items, the rest of a count;
  `aside` not written). Format unchanged: no version change.
- engine/README.md: Offline paragraph (a full kind is set aside, the rest keeps coming).
- Release notes: no draft for the next port version exists (0.2.1.md is the apworld-only release,
  0.02.50 is released): nothing added, said in the report.

## For the party-bench merge (partybuild5, scratchpad/wt/party)

Functions changed in game/ap/ap.cpp:
- `give` became `give_some` (returns the copies given; same body but the last line); a one-line
  `bool give()` wrapper follows it, so party-bench code calling `give()` still compiles.
- New after it (anonymous namespace): `whole_count`, `inventory_room`, `owe_rest`.
- `give_start`: the start-items line (owe_rest).
- New after give_start (anonymous namespace): forward declarations of `full_text` /
  `banner_text_width`, `set_aside`, `give_next`.
- `free_play_update`: the comment above the banner step and the step's pending / server branches
  (now `give_next(r)`); new public `give_free_play_step()` right after it.
- `placeholder_hit`, `on_shop_buy`: the own-item give lines (owe_rest).
- New `full_text` after `banner_text_width`.
- `status_text`: the "inventory full, set aside" line after the ambushes line.
game/ap/ap.h: `SaveState::Give::aside` (end of Give), `give_free_play_step` declaration after
`free_play_update`. engine/tests/test_ap.cpp: two tests appended at the end (ap_full_inventory,
ap_full_inventory_join; helpers kFullSeed, held, use_one, step_is, server_items). tools/
smoke_field_events.sh: ap_aside_* after ap_shop_full.
Expected conflicts: none structural; party-bench's own edits to free_play_update (if any) sit
beside the replaced branches.
