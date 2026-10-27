# Handoff: pendingsave (items still waiting to be given, across a save and a load)

Agent "pendingsave", started 2026-10-27. Build dir `engine/build-pendingsave` (configured like
build-hubleader: RelWithDebInfo, SS_BUILD_AP_NET / SS_BUILD_SETUP on, deps from
engine/build-hubleader/_deps). Scratch: scratchpad/pendingsave/.

## The bug (found by "skilllogic" reading the code; the user: "Lets fix that asap. That is a current bug and needs addressing.")

Items still waiting to be given aren't written to the save sidecar. A check that gives several
items (a skill guild set under skillsanity, a boss, a craft, a killer-move level...) puts them in
the port's give queue (`Runtime::pending`, game/ap/ap.cpp); they are handed out one per banner in
free play. A save made while some still wait doesn't record them; after a load (a new session)
they are gone, and the check is already marked done.

## Rules given

- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); every game
  process under the 4 GB cap; at most 2 game processes at once; builds -j4.
- Scratch XDG_CONFIG_HOME; never a config-saving flag; never ~/.config/shattered-stars/config.ini
  or the user's saves; SS_AP_ANY_SAVE=1 only for headless loads of ordinary saves with a seed.
  Kill by exact PID.
- Enhance, never degrade. Save card bytes stay byte-compatible. Same-state rule: nothing pending,
  the sidecar bytes and every run as today; AP off untouched.
- Stay out of apworld/ (agent "skilllogic") and scratchpad/wt/party (agent "partybuild5", branch
  party-bench, which also edits game/ap/ap.cpp): keep the change self-contained, list the
  functions changed (below) for the merge.
- Commits on main after the verified fix, docs first, staged by path; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push.
- A denied tool call: don't retry; write the blocked step here and stop.
- Roll over at ~400k context.

New limits from the coordinator after a crash (2026-10-27, swap full): at most 1 game process at a
time, builds -j4 with nothing else heavy alongside. skilllogic2 runs Python batches in apworld/.
Keep this file current after each step.

## Status

- Build dir configured and built at HEAD 9df71cb; the unfixed binary is kept as
  scratchpad/pendingsave/ss-baseline. Repro seed scratchpad/pendingsave/repro/multi.apso2 (four
  boss locations on encounter 19: three items and 4321 Fol); runner repro/run.sh (BIN NAME FRAMES
  CARD DEBUG_INPUT INPUT [args]; env SS_* from the caller). Probe (no save) works: `200:battle 19`
  with SS_BATTLE_STUB=1 checks all four; one give per ~190 frames (vblanks ~500, 690, 880, 1090).
- [x] 1. Pending-state table (below)
- [x] 2. Reproduced on the unfixed build (scratchpad/pendingsave/repro/repro.sh BIN TAG; evidence
  kept in scratchpad/pendingsave/evidence/: save_base.log, load_base.log, the sidecar, the seed).
  Save run: the four checks, `given Revival Card`, the save at ~1110 with the menu open, then
  ap_status `checks 4/4 | ... | waiting 3`. The sidecar has `checked` [7001..7004] and nothing
  else of them. Load run (new process): `loaded ...: 4 checks`, ap_status `waiting 0` at 60 and
  1000, no `given` line, Fol 0 to the end: Item 22, Item 23 and 4321 Fol are gone for good.
- [x] 3. Fix written and built (uncommitted, main checkout). The repro on the fixed build
  (scratchpad/pendingsave/evidence/save_fix.log, load_fix.log, card_fix.slot01.ap.json): the
  sidecar is version 3 with `pending` [7002, 7003, 7004] and `pending_hash`, save_hash the same
  as the unfixed build's (the card unchanged); the load logs `3 items waiting from the save`, gives
  Item 22, Item 23, 4321 Fol (Fol 4321 in the trace), ap_status `waiting 0`. Files: game/ap/ap.h (SaveState::Give,
  pending, pending_hash; comments), game/ap/ap.cpp (Runtime::Pending = SaveState::Give; the queue
  pushes in deliver_check, placeholder_hit, on_battle_exit, on_party_add, on_enemy_drop,
  on_shop_buy, on_craft, scan_killer_moves record the location; new pending_json / pending_digest /
  sidecar_pending / gives_again after sidecar_path; sidecar_json, sidecar_from_json, on_saved,
  on_loaded, load_allowed, vblank kConnected), game/ap/seed.{h,cpp} (parse_item_json, item_json
  at the end).
- [x] 4. Tests: unit test `ap_pending_save` (engine/tests/test_ap.cpp, in ss_game_tests) PASS; ss_game_tests
  194/194, ss_tests 103/103 on the fix. Smoke case ap_pend_save / ap_pend_load / ap_pend_edit added to
  tools/smoke_field_events.sh (after ap_guard_any): 7/7 PASS on the fix, 5 FAIL on the unfixed build
  (evidence/smoke_case_on_baseline.txt; standalone runner scratchpad/pendingsave/smoke_case.sh BIN).
  Same-state (scratchpad/pendingsave/same_state.sh: the smoke's ap_save run, nothing waiting, both
  builds): cards identical, sidecars identical, [ap] lines identical but the card's file name
  (evidence/same_state.txt). Next: the full smoke from a frozen copy, then ss_tests again.
- [x] 5. Docs written: docs/ap/seed_format.md ("The save sidecar": `pending`, `pending_hash`,
  version 3, the guard, the load's queue, connect), engine/README.md (Offline: items waiting kept,
  older-save reload). Release notes: no draft for the next port version exists (0.2.1.md is the
  apworld-only release, 0.02.50 is released): nothing added, said in the report.
- [x] Full suites on the fix: smoke (frozen copy scratchpad/pendingsave/smoke_frozen.sh, binary
  ss-fix2) 292 PASS, 0 FAIL, no SKIP (log scratchpad/pendingsave/smoke_full.log); ss_tests 103/103;
  ss_game_tests 194/194 (ap_pending_save included).
- [x] Committed on main: docs first, then the port change (see git log). Not pushed.

## 1. The pending states (game/ap, main at 9df71cb)

Held in memory between a check or a receipt and the actual give, and what a save then a load
(a new session) does with each today. "Sidecar" = SaveState in `<card>.slotNN.ap.json`.

| State | Where | Survives save + load today? | How / what is lost |
|---|---|---|---|
| **Give queue** `Runtime::pending` (own items of checks: deliver_check = guild sets, learn points, Float prize; boss victories; join sites; first drops; crafts; killer-move levels; a shop slot or a chest/gift placeholder with the inventory full; the courier's overflow; the console's ap_give) | ap.cpp Runtime | **No** (in-session loads keep it, a new session loses it) | Not in the sidecar. The check is in `checked`, so it never fires again: **the items are lost**. The bug. |
| Own items of non-chest checks given after the save, then an older save loaded (game over, quit without saving) | (gone with the discarded game) | **No** | The check stays checked (merged at load, or reported by the server at connect), the loaded game lacks the item, the check can't fire again: **lost**. Chests and gifts are re-reachable (their flags revert, the placeholder gives again). Same family as the bug. |
| Received server items past `received_index` (`net_items`) | ap.cpp Runtime | Yes | Sidecar `received_index`; the server re-sends the whole list on connect (items_handling 0b001: other worlds' items only). |
| Characters received, not joined (`characters_waiting`), starting character's join | SaveState | Yes | Sidecar `characters_waiting` / `characters`. |
| Starting character becomes Formation leader (`lead_start`) | ap.cpp Runtime | No (minor) | Only the leader choice is lost if a save falls between the start's queueing and the join (a banner showing at New Game). party-bench's heroslot persists the leader (`leader` / `slot_0`): left to that branch, not changed here. |
| Starting items, Fol, flags (`start_given`) | SaveState | Yes | Given in one frame at the first free play; `start_given` in the sidecar. |
| Ambush traps not fought (`ambushes`) | SaveState | Yes | Sidecar `ambushes`. |
| Keys, skills, moves received (`keys`, `skills`, `moves`; a move for an absent member waits for the join) | SaveState | Yes | Sidecar keys of the same names. |
| Temporary members, borrowed slots (`temporary`, `stash`) | SaveState | Yes | Sidecar. |
| Checks made offline (`checked`) | SaveState | Yes | Sent on connect. |
| Goal (`goal`, `goal_won`) | SaveState | Yes | Sent again on connect. |
| DeathLink received, not applied (`deathlink_in`) | Runtime | No, by design | A load starts the game again (dropped, logged). DeathLink out while offline is not queued (by design). |
| Banners waiting (`notices`, `override_lines`) | Runtime | No (cosmetic) | Their items were given before the banner was queued. |
| goal.flags / killer-move scan pending | Runtime | n/a | Redone after every load. |
| Item counts > 1 partly fitting (give() adds what fits, reports success) | give() | n/a | Copies past 20 of a kind are dropped; the apworld always writes count 1, so only a hand-made seed can hit it. Noted, not changed. |
| Inventory full at the queue's front | free_play_update | (delay, not loss) | The front item blocks everything behind it (server items too) until there is room. Noted for the user. |

## 3. The fix (design)

- `SaveState::Give {item, from, found, location}` (location -1: not a check's); `Runtime::Pending`
  becomes that type. Every queue push from a check records its location.
- Sidecar key `pending` (only when something waits): `{"location": ID}` for a check's own item
  (the item comes from the seed), else `{"item": {seed item object}, "from"?, "found"?}`. Then the
  sidecar is version 3 (an older port refuses it instead of dropping the items; party-bench's
  bench uses 3 the same way). Guard `pending_hash` = "sha256:" of the save's bytes (as save_digest
  takes them) + the canonical dump of `pending`; load_allowed refuses a mismatch, or one of the two
  without the other. Nothing waiting: no key, version 2, the same bytes as today.
- Load (same seed): queue = the save's `pending` + the own items of every check the running state
  has and the save lacks, unless chest / gift (re-reachable). The session's own queue is dropped
  (it belonged to the discarded game). Connect: a server-reported check new to the state and of
  such a type queues its own item too (decision for the user: listed in the report).

## For the party-bench merge (partybuild5, scratchpad/wt/party)

Functions changed in game/ap/ap.cpp: the Runtime struct (`Pending` is now `SaveState::Give`, with
`location`), deliver_check, placeholder_hit, on_battle_exit, on_party_add (only the queue push
before `if (!party_v2(r))`), on_enemy_drop, on_shop_buy, on_craft, scan_killer_moves (the push
line only), a new anonymous block after sidecar_path (pending_json, pending_digest,
sidecar_pending, gives_again), sidecar_json, sidecar_from_json, on_saved, on_loaded, load_allowed,
vblank (kConnected). game/ap/ap.h: SaveState (Give, pending, pending_hash at the end), the
on_saved / load_allowed comments. game/ap/seed.{h,cpp}: parse_item_json / item_json at the ends.
Expected conflicts, all mechanical:
- sidecar_json's version line: party-bench writes `s.bench.empty() ? 2 : 3`, main
  `s.pending.empty() ? 2 : 3`: merge as `s.bench.empty() && s.pending.empty() ? 2 : 3`.
- sidecar_from_json's version test: both now `version < 1 || version > 3`.
- Additions side by side at the ends of SaveState, sidecar_json, sidecar_from_json, after
  save_hash in on_saved, after `s.save_hash.clear()` in on_loaded, and a new `else if` after the
  save_digest branch in load_allowed (bench's and pending's: keep both).
