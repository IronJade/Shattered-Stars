# Tools: reference regression suite (continuation log)

Worker: tools agent. Task: build a regression suite that re-checks the ~300 PCSX-Redux
reference recordings under `scratchpad/` against the port, so a regression like prec_m1's
(RNG now splits at b570; was equal to the exit) is caught automatically instead of by
chance. Never runs Redux; port-only. Nothing here is committed by the worker; the
coordinator commits hourly.

## 2026-10-23, ~04:15: resync / skip-reduction pass (this session)

Picked up from the previous tools agent's hand-over below (full set: 169 pass, 15 regress,
38 review, 117 skip). Assignment from the coordinator: fix the world_spell parallel race,
the enemy tag rules, encode the REVIEW rows' exact setups, add a re-sync mechanism for
CD-load-crossing references, and reduce skips where cheap. Also addressed, via the
coordinator, two messages from the battle and effects agents with their own findings for
this suite.

**Result:** `--all`: **219 pass, 3 regress, 1 review, 0 error, 116 skip** (of 339;
`scratchpad/regress/full_resync_pass/summary.md`), up from 169/15/38/0/117 at the start of
this pass. `--quick`: **28 pass, 0 regress, 0 review** in 34s
(`scratchpad/regress/quick_resync_pass/summary.md`), well under the 10-minute budget, and
still wired into `tools/checkpoint.sh` unchanged (`--quick --jobs 5` after smoke, its
one-line total folded into `$t4`).

### 1. world_spell parallel race (fixed)

Confirmed cause: `scratchpad/world/spell/run_port.sh` pointed every job at one shared
`SS_MEMCARD1` (`redux/cards/empty_port.mcd`); 5 processes reading/writing it at once
corrupted the port log, which `cmp.py` then failed to parse ("could not parse compare
output" on all 30 runnable world_spell rows). Fix (additive, default unchanged when
`SS_MEMCARD1` is already set): `run_port.sh`'s `SS_MEMCARD1=$W/redux/cards/empty_port.mcd`
became `SS_MEMCARD1=${SS_MEMCARD1:-...}`; `ref_regress.py`'s `h_world_spell` now copies that
file into the job's own scratch dir and points `SS_MEMCARD1` at the copy (same pattern
`fx_worker/battle/port_pc.sh` already used for its own memcard).

Rerunning `--area world_spell --jobs 5` after the fix: no more parse failures, but real,
previously-masked-by-the-race results surfaced underneath (16 pass, 13 regress, 1 review at
first). Investigated each:

- **9 refs (`eA/eB/eD/eF_3`, `i9b/i10b/i11a/i12_3`, `x11_3`) are a slow-run residual, not a
  bug:** every one shows real "[bw] tick" slow frames in its own Redux log, and the actual
  enemy/party state samples stay exact or near-exact throughout - only the RNG draw *count*
  drifts a little once the 3-vsync slow frames hit, same documented phenomenon as the
  existing `iCv_3`/`iNa_3` rows. `expected` now carries that note; judge() scores them
  lenient on the existing "slow run"/"tick" keyword.
- **`m9_3`/`m9b_3` (Rena navigating the battle ring menu with the pad, `BW_BATTLE_INPUT`, not
  a `BW_CAST` poke) were never actually replaying the recorded button presses:**
  `run_port.sh` only had a `PORT_INPUT` override and a separate cross-mash loop, neither of
  which reads `BW_BATTLE_INPUT` from the sourced cfg; with `NO_MASH=1` (required for every
  other sweep id) the port's Rena never cast at all. Added one additive default:
  `PORT_INPUT=${PORT_INPUT:-$BW_BATTLE_INPUT}` right before the existing `--input` handling.
  Result: the cast now fires (was: never), landing ~4 frames later than the reference
  (712/1612 vs 716/1616) with a real, if small, residual RNG drift after - the scripted
  button-press replay doesn't perfectly reproduce the console's menu-input timing. Reported
  as a real, unresolved lead for whoever owns pad-input-driven refs (not fixed; this is
  outside "port code" and outside what a resync constant can honestly paper over, since the
  in-game spell *target* differs by one slot at one point, not just a frame count).
- **`s9a_3` (caster sweep, spell 0x39 "Growth") is a new, real lead:** no slow frames in its
  own log at all, yet RNG drifts from b3320 with every unit sample still exact. Not
  battle/enemy/effects agent code I should touch; reported.
- **`s11_3`'s own Redux recording is broken, not a suite or port issue:** `ref/s11_3/log.txt`
  is 79 lines, no `[bw] rng` line at all, ends in a stream of "Unknown command for pad: 43"
  - the recording itself never got into the battle. Needs a re-record by the world agent,
  not a fix here.
- `m111_3` (boss spell check, Goathead's Thundercloud): already documented in
  `docs/handoff/world.md` ("Findings": the port's spell data is ready a frame sooner than
  the console's CD read, ref b602 vs port b600, "not a port bug"); `expected` now carries
  that split (`b602`) instead of a bare `rng_equal_to_exit`.

### 2 + 3. Enemy tag rules and the REVIEW rows' setups (fixed)

`verify_all.sh`'s own comment only ever documented tags `i`/`m` (idle + STRONG poke) and `t`
(tough Claude); `n` (per-vsync granularity) and `v` (per the coordinator: also tough Claude)
were confirmed this pass; `d`/`e`/`f`/`g`/`h` (outside the script's own default `[imt]*`
pattern) turned out to be a mix: `h531_3`/`h539_3` are the HP-poked Bark/Indalecio runs from
`docs/handoff/enemy.md` section 4; `e10_3`/`e41_3`/`f10_3` were recorded with Cross mash
(the effects agent's finding, confirmed); `d10_3`/`g10_3`/`h10_3` (same encounter 10, pass
already) are genuinely idle. Fixes:

- `verify_all.sh`: `case $tag in t*) T=1 ;;` -> `case $tag in t*|v*) T=1 ;;` (the task's
  requested fix; additive, the script's own default pattern still excludes `v*` by default
  so this only matters when it's run with a wider pattern).
- `ref_regress.py`'s `h_enemy`: `TOUGH=1` for tags `t` *and* `v`; `NO_MASH=1` for every tag
  *except* `e`/`f` (which need the default Cross-mash path instead).
- A `resync` column added to `refs.csv` (see "Resolved" section below) carries `SS_ENEMY_HP`,
  `CMP_FROM`/`CMP_R`/`CMP_DMAX` and `FRAMES` per id, read generically by `ref_regress.py`'s
  new `parse_resync()`.

Rerun of the task's list (`v538_3`, `h531_3`, `h539_3`, `t539_3`, `t540_3`, `e10_3`, `e41_3`,
`f10_3`, `i420_3`) plus the REVIEW rows (`m524_3`, `m537_3`, `m538_3`, `m539_3`): **all 13
pass.** `i420_3` just needed the same documented split as the already-passing `n420_3` (same
encounter, same slowdown, different log density) - the two rows were inexplicably given
different `expected` text before this pass. `m524_3`/`m537_3`/`m538_3`/`m539_3`'s old
`expected` text was stale (predated the enemy agent's Phase 2 spell-module work): Wind of
Destruction, Mind Blast/Spicule and Noah are now ported, so their documented split points
moved from the old "spell not ported" positions (b310/b390/b530/b430) to the new
"the console slows down once the cast lands" positions (b530/b410/b930/b450), each confirmed
empirically against `docs/handoff/enemy.md`'s Phase 2 section this pass.

### 4. Re-sync points (resolved as a `resync` column)

Added a `resync` column to `refs.csv`: `KEY=VALUE;KEY=VALUE...`, read generically by
`ref_regress.py`'s `parse_resync()` and applied as env vars by each handler (`FRAMES` is
pulled out and passed positionally instead, since the run/compare scripts take it as an
argument). Documented in full in `tools/refs/README.md`. Tested the coordinator's specific
suggestion (`SS_BATTLE_RNG_SKIP=1`, the battle state 0->1 party-load skip that fixed the
menu agent's late-save final-boss reference) against every candidate it was named for this
pass (`v538_3`, `h531_3`, `h539_3`, and the effects agent's later suggestion of `m538_3`):
**it made every one of them worse**, not better (v538_3 exact -> diverges at b330; h531_3
b440 -> b60; m538_3 b930 -> b50/b330 depending on what else was combined with it). None of
those rows use it. The actual fix for the ones that needed a resync was `CMP_FROM`/`CMP_R`/
`CMP_DMAX` (cmp.py's own boss-intro-voice offset-search override, already documented in
`verify_all.sh`'s own usage note but never wired into `ref_regress.py`) or `SS_ENEMY_HP`. No
row in the current `refs.csv` is confirmed to need `SS_BATTLE_RNG_SKIP`; the mechanism is
in place (same `resync` column) for whichever future reference actually does.

### 5. Known-slowdown / real-lead refs: left alone, reported

`op_m3`/`op_b10`/`op_m10` (fx_battle, the effects agent's own expected-value update) and
`chis_m4`/`stage16c`/`stage36_long` (battle) were this pass's starting leads:
- `stage16c` and `stage36_long` turned out to be **suite setup, not real finds** (see
  "battle refs" below) - both now pass.
- `chis_m4` (fx_battle, `b490`): not touched - fx_battle wasn't in this pass's `--area`
  reruns beyond the full/quick totals, and it's explicitly the effects agent's / battle
  agent's to look at per the task. Still shows the same divergence as `full_head2`
  (unchanged by anything in this pass).
- New real leads surfaced by this pass's own fixes, not fixed here (reported above): `m9_3`/
  `m9b_3` (pad-input-replay timing), `s9a_3` (spell 0x39 "Growth").

### Battle refs (stage16c, stage36_long, caster35, caster35s, stage8_light, stage8_shots)

All suite setup, confirmed against the battle agent's own hand-over notes
(`docs/handoff/battle.md` "Leads for the successor" #1 and "Since the 22:14 resume"):

- `stage16c` is a caster ref (Claude + AI Celine casting), not a plain stage ref; `h_battle`
  had it in `STAGE_PLAIN` (→ `cmpstage.sh`, wrong) instead of `STAGE_CASTER` (→
  `cmpstage_caster.sh`). Moved; now matches to the result screen (forgiven by the existing
  tail heuristic).
- `caster35`/`caster35s` were recorded in the 2-1-2 tick group (their own `"[bw] frames
  b00010"` line reads `3`), which needs `cmpstage_caster7.sh` (`PC_D=7`, `PC_TICKS=1`), not
  the default `cmpstage_caster.sh` (`PC_D=6`) that `stage14c`/`stage16c` (2-2-2 group) use.
  Confirmed empirically (false divergence at b270 with the D=6 script; clean to the exit with
  D=7) and matches the battle agent's own later recheck note. `h_battle` now picks the script
  by id.
- `stage36_long`'s own reference runs 2600 vsyncs, but `cmpstage.sh`'s default `FRAMES=1400`
  cut the port run short, freezing its RNG count well before the reference's end (looked like
  a growing divergence; was just a truncated comparison). `resync=FRAMES=2650` fixes it; the
  tiny (+-9, non-growing) tail jitter in the cart's last few frames is now a documented
  `b2560` split.
- `stage8_light`/`stage8_shots`'s documented `b940` split only shows up with the mash input
  one frame earlier than `cmpstage.sh`'s generic 45i formula gives (the battle agent's own
  note: "inputs at 45i-1"). `resync=CMP_OFF=-1` fixes it.
- Found and fixed a latent judging bug while diagnosing `stage8_shots`: `cmpstage.sh` runs
  `pcrun.sh` as a subroutine, which prints its *own*, unrelated "first mismatch at b N"
  summary line (its own D-search, not respecting `CMP_OFF`); when `cmpstage.sh`'s own
  dedicated check said "equal throughout", `find_divergence()`'s regex could still pick up
  that stale inner line instead and report a false divergence. Reordered the checks so the
  unambiguous "no divergence" wordings are checked first. Reran the whole `battle` area after
  the reorder to confirm nothing else's judging changed (it didn't: same 43 pass / 0 regress
  / 0 review / 28 skip before and after on the ones that weren't literally broken by it).

### 6. Skip reduction

- **menu:** `combref2` promoted to automated and passing. Its Redux plan was already
  translated into the port's own `--input` string and saved
  (`scratchpad/menu/combref2/port_input.txt`), confirmed against a previous port run
  (`comb_p2`, `docs/handoff/menu.md` "Combat, pages 8-10"); only wiring was missing. Needed
  two script fixes to `scratchpad/menu/itemsrun.sh` (additive, disclosed below) and a new
  `h_menu` handler + `MENU_RUNS` table in `ref_regress.py`, plus a menu-specific judge path
  (`cmpdump.sh` prints `"<block> diffs N ..."` per data block, not an RNG trace, so the
  existing `find_divergence()` doesn't apply). Result: config/inventory/party/game_state
  match exactly; `records` has one byte off (+0x4ea), the same "check bytes are random by
  design" phenomenon `docs/handoff/menu.md` already documents for `itemsref1/2`'s inventory
  dumps, just at a different offset cmpdump.sh doesn't filter on its own - tolerated
  (`records: diffs <= 1`), not ignored outright.
  - The other 14 menu refs each have a `plan.txt` (the Redux side) but no independently
    reconfirmed `port_input.txt` this pass. The translation rule is written down now
    (`docs/handoff/menu.md`: "Port frame = Redux frame + 153" for `itemsref1/2`/`equipref1`'s
    plans, confirmed against `combref2`'s own saved pair, plus whatever page-opening presses
    get from the camp root to that page - `combref2`'s own `port_input.txt` has one extra
    `140:triangle,144:none` the plan.txt doesn't show at all), but guessing that per-page
    preamble for the other 14 without a human checking the resulting dumps once felt like
    exactly the kind of false confidence this suite exists to avoid. Left `manual`.
- **world_map** (28 ids) and **fx_legacy** (23 ids): reconfirmed, not newly resolved - neither
  area has any run/compare recipe anywhere in the current `docs/handoff/world.md`
  (`fx_legacy`'s likely real coverage is `engine/tests/test_field_effects.cpp`'s own
  `SS_FX_REF` dump test, already part of `ss_game_tests`, not this suite). Left `needs_owner`.
- `victory_2_3`'s row already carries the effects agent's own finding (`CMP_D=7` - the
  `b270`-`b400` "differences" were `rngcmp.py`'s fallback `D=2`) in its `compare_cmd` column,
  for whoever next promotes it from `manual`.

### Script edits (additive, disclosed per the rules)

- `scratchpad/world/spell/run_port.sh`: `SS_MEMCARD1=...` -> `SS_MEMCARD1=${SS_MEMCARD1:-...}`
  (the parallel-race fix); a new default,
  `[ -z "$PORT_INPUT" ] && [ -n "$BW_BATTLE_INPUT" ] && PORT_INPUT="$BW_BATTLE_INPUT"`, right
  before the existing `--input` handling (replays a sourced cfg's recorded pad sequence when
  nothing else overrides `PORT_INPUT`; every cfg except `sweep_m9.sh`/`sweep_m9b.sh` leaves
  `BW_BATTLE_INPUT` unset, so this is a no-op for them).
- `scratchpad/enemy/verify_all.sh`: `case $tag in t*)` -> `case $tag in t*|v*)` (the task's
  requested fix).
- `scratchpad/menu/itemsrun.sh`: the hardcoded `./engine/build-menu/shattered-stars` became
  `${SS_BIN:-./engine/build-menu/shattered-stars}`; added
  `SS_DISC1=${SS_DISC1:-<the disc path>}` (this script never read either, so a fresh
  `XDG_CONFIG_HOME` with no remembered disc, which is what every parallel job gets, failed
  outright); the shared `cards/p_items.mcd` copy became a per-job `cards/p_items_$(basename
  $1).mcd` (the same race class as world_spell's, fixed the same way, pre-emptively since
  `combref2` is this suite's first user of this script).

No other line in any agent's script was touched.

## Full-set run at HEAD (212bcf4, `engine/build/shattered-stars`, not rebuilt — it postdates
every agent's last commit point) — prior pass, superseded above

`scratchpad/regress/full_head2/summary.md` (directory since cleaned up; figures stand):
**169 pass, 15 regress, 38 review, 0 error, 117 skip** (of 339 rows; 222 automated, matching
169+15+38). The first pass (before the result-screen-fade fix below) showed 142/42/38/0/117
- the judging heuristic, not the engine, moved 27 refs from REGRESS to PASS once it could
tell a result-screen-only divergence from one still inside the move/stage. The remaining 15
REGRESS and 38 REVIEW needed a human (or the owning agent), not a reflex re-fix from that
pass - all addressed above except `chis_m4`/`op_m3`/`op_b10`/`op_m10` (left to the effects/
battle agents per the task) and the genuinely new leads this pass's own fixes surfaced
(`m9_3`/`m9b_3`/`s9a_3`, `s11_3`'s broken recording).

## Caveats worth a second pair of eyes

- [spell agent, 2026-10-23 ~05:00] `m9_3`/`m9b_3`, `s9a_3` and `s11_3` below are resolved
  (replay shift MENU_D=4 + `SS_ORIGINAL_S20` for m9; s9a / s11 re-recorded with the slow
  frames logged): all four pass. Details: `docs/handoff/spell.md`.

- `chis_b1` (fx_battle) still shows a real REGRESS just past the ~350-sample tail window
  (b1250) per the prior pass; not rechecked this pass (fx_battle wasn't in this pass's scope
  beyond the full/quick totals).
- `m9_3`/`m9b_3`: the port now casts (it didn't before this pass at all), landing ~4 frames
  later than the reference; whether that's fixable by a different `PORT_INPUT` replay offset
  or is a genuine port timing difference in how pad input is consumed during a menu
  navigation sequence was not run down further this pass.
- `s9a_3`: spell 0x39 "Growth" diverges from b3320 with every unit sample still exact and no
  slowdown in its own reference log - looks like a real, specific lead, not a suite artifact,
  but not investigated past identifying it.

## Machine-load finding (not a suite bug): re-verify numbers if the machine is this busy

Two re-runs of `--all` right after finishing this pass (`scratchpad/regress/final_check`,
`final_check2`, both since deleted) came back **193 pass, 0 regress, 30 review** - every
`world_spell` sweep ref showing "could not parse compare output" again, the exact symptom
the memcard-race fix (above) was supposed to have ended. Root cause confirmed **not** a
regression in that fix: another agent's `tools/ap_late_proof.py` was running ~20 concurrent
`shattered-stars` processes at the time (`ps aux`), and `free -h` showed swap at 7.5/8.5 GiB
- the machine was thrashing. A `world_spell`-only rerun under that same load also failed the
same way (port.log never even got created for `iLv_3`); the same rerun a few minutes earlier,
before that load ramped up, was clean. The authoritative numbers for this pass
(**219 pass, 3 regress, 1 review, 116 skip**, `scratchpad/regress/full_resync_pass/`) were
captured before that load started. Re-run `--all` (or at least `--area world_spell`) to
reconfirm before trusting a result captured while another agent has a large batch job
running - this suite has no guard against a starved machine, only against its own races.

## Caveats carried over from the prior pass (addressed above, still true as caveats)

- `e10_3`/`e41_3`/`f10_3`: the tag-to-encounter mapping for letters outside `i`/`m`/`t`/`n`/`v`
  was still not independently confirmed for every letter this pass (only `e`/`f`'s mash
  setting, and `d`/`g`/`h`'s idle one, were checked) - a letter not covered by this pass's
  fixes should still be treated as unconfirmed, not assumed idle.
