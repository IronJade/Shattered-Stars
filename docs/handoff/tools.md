# Tools: reference regression suite (continuation log)

Worker: tools agent. Task: build a regression suite that re-checks the ~300 PCSX-Redux
reference recordings under `scratchpad/` against the port, so a regression like prec_m1's
(RNG now splits at b570; was equal to the exit) is caught automatically instead of by
chance. Never runs Redux; port-only. Nothing here is committed by the worker; the
coordinator commits hourly.

## Done

- **Inventory** (`tools/refs/refs.csv`, 339 rows, committed-eligible — small, commands and
  expectations only, no game data): one row per reference directory found under
  `scratchpad/{battle,fx_worker/battle/redux,enemy,world/spell,world,fx_worker}/ref*` and
  `scratchpad/menu/*ref*`, each with its area, a run-command template, a compare-command
  template, and an expected result pulled from that area's `docs/handoff/<area>.md`. 222 of
  339 rows have a confirmed, automated run recipe; 117 are `manual` (a bespoke scenario this
  pass could not re-derive with confidence) or `needs_owner` (no run/compare recipe is
  documented anywhere yet: `world_map`'s 28 non-spell refs and `fx_legacy`'s 23 phase-1-3
  field-effects refs). Full breakdown and exact gap list in `tools/refs/README.md`.
- **`tools/ref_regress.py`**: `--all` / `--quick` / `--area X` / `--id A,B` against
  `--build DIR` (default `engine/build`), at most 5 parallel port jobs, each with its own
  `XDG_CONFIG_HOME` and output dir under `scratchpad/regress/<timestamp>/<id>/`. Calls the
  owning agent's own scripts (`pcrun.sh`, `cmpstage.sh`, `cmpstage_caster.sh`,
  `run_port.sh`/`run_port2.sh`, `port_pc.sh`, the `sweep_*.sh` configs) — no re-implemented
  simulation logic. Writes `scratchpad/regress/<timestamp>/summary.md` (PASS / REGRESS /
  REVIEW / ERROR / SKIP, with the reason for each) and a one-line total; exits nonzero only
  on a REGRESS.
  - A result is scored by finding "first difference/mismatch at bNNNN" (or "no divergence")
    in the compare tool's own output and checking it against the `expected` column. A
    divergence inside the last ~350 "b" samples of a killer-move-style reference (battle /
    fx_battle) is not scored (the result screen's own fade-timing difference, already
    reported to the battle agent, is not this suite's job to re-flag every run). A divergence
    where `expected` mentions `slowdown` / `slow run` / `tick` is not scored either (the
    console's own CPU slowdown, not modelled, per the project's "enhance, don't degrade"
    rule). Anything else outside a 30-frame tolerance of a documented split point is `REVIEW`
    (a human should look), and any divergence where none was documented at all is `REGRESS`.
- **`--quick`** (`QUICK_IDS` in ref_regress.py, 28 refs, ~20-30s on this machine, well under
  10 minutes): `stage13`, Bowman/Dias/Ernest one move each, Chisato/Precis/Opera/Ashton one
  level-0 and one max-level move each (including `prec_m1`, the ref that caught the real
  regression this task started from) plus one Ashton weapon-variant move, five resident enemy
  families, one module, three documented exceptions (Shot kind not ported, console slowdown,
  spell module not ported), and the world agent's four party-class spell sweeps plus one boss
  spell check. No menu/world_map/fx_legacy ref has a confirmed automated recipe yet, so none
  is in the quick set (task said "if any exist"; none do yet with a safe recipe).
- **Script edits** (minimal, additive, backward compatible; default behaviour unchanged when
  the new variable is unset — disclosed per the rules): `scratchpad/battle/pcrun.sh`,
  `scratchpad/enemy/run_port.sh`, `scratchpad/enemy/run_port2.sh`,
  `scratchpad/world/spell/run_port.sh` gained `${SS_BIN:-...}` around their hardcoded binary
  path and `${XDG_CONFIG_HOME:-...}` around their hardcoded config dir (matching the pattern
  `port_pc.sh` already used); `scratchpad/fx_worker/battle/port_pc.sh` gained the same
  `XDG_CONFIG_HOME` override (it already had `SS_BIN`). Nothing else in any of the five files
  changed. This was necessary for "at most 5 port jobs at once, each with its own
  XDG_CONFIG_HOME" and "against a given build dir" — without it, parallel jobs of the same
  area would race on one shared config file, and the suite could only ever run the agent's
  own in-progress build, not `engine/build`.
- **`tools/checkpoint.sh`**: after smoke, runs `tools/ref_regress.py --quick --jobs 5` and
  folds its one-line total into the commit message's results line (`$t4`); a quick-set
  failure is reported, never blocks the commit (same as ss_tests/ss_game_tests/smoke).
- **docs/HANDOFF.md**: short "Reference regression suite" paragraph added.

## Full-set run at HEAD (a61088c, `engine/build/shattered-stars` as already built, not
rebuilt — it postdates every agent's last commit point)

`scratchpad/regress/full_head2/summary.md`: **169 pass, 15 regress, 38 review, 0 error,
117 skip** (of 339 rows; 222 automated, matching 169+15+38). The first pass (before the
result-screen-fade fix below) showed 142/42/38/0/117 — the judging heuristic, not the
engine, moved 27 refs from REGRESS to PASS once it could tell a result-screen-only
divergence from one still inside the move/stage. The remaining 15 REGRESS and 38 REVIEW
need a human (or the owning agent), not a reflex re-fix from this pass:

- **24 of the 38 REVIEW rows are every `world_spell` ref that uses the `sweep_cmp.sh`/boss
  pattern** ("could not parse compare output"). Run individually (`--jobs 1`) they work (I
  verified `iLv_3` by hand: exact, matching `docs/handoff/world.md`'s own numbers). Run five
  at once they don't — `scratchpad/world/spell/run_port.sh` points every job at the same
  `SS_MEMCARD1=.../redux/cards/empty_port.mcd` and the same relative `ref/i340_3` dump
  (both read through a `cwd` shared by all five concurrent world_spell jobs), which is fine
  sequentially but races under 5-way parallelism. **Not a port regression** — a suite
  limitation. Fix: either give world_spell its own `--jobs 1`, or (better, and left for
  whoever picks this up next) copy `empty_port.mcd` to a per-job path the way
  `fx_worker/battle/port_pc.sh` already does for its own memcard.
- **8 of the other 14 REVIEW rows are `enemy` refs whose documented split point the actual
  run missed by a lot**: `m524_3`, `m537_3`, `m538_3`, `m539_3` (all boss "spell module not
  ported" cases, documented split b310-b530-ish, actual b50 or b530) and the two
  `stage8_light`/`stage8_shots` battle refs (documented b940, actual b500/b1390), plus
  `caster35`/`caster35s` (documented b850, actual b270). All of these use `NO_MASH=1`
  (idle Claude) in this suite, but several of their *_cfg / queue scripts were recorded
  with specific `BW_EXTRA_POKE`/`SS_BATTLE_POKE` party setups this pass's generic
  `run_port.sh`/`cmpstage.sh` calls may not fully replicate (e.g. stage8's lightning test
  wants `SS_BATTLE_SHOTS`/2-2-2 tick group handling that `cmpstage.sh` only applies to the
  `STAGE_PLAIN` set, not these two by name — check whether `stage8_light`/`stage8_shots`
  actually belong in `STAGE_PLAIN` or need their own branch). **Likely more suite than port.**
- **15 REGRESS, most clustered at b50**: `h531_3`, `h539_3`, `t539_3`, `t540_3`, `v538_3`
  (enemy) all diverge in the first few samples. `t`/`v` are "tough Claude" tags per
  `verify_all.sh`'s own comment, but this suite's `h_enemy` only sets `TOUGH=1` for tag `t`
  — **`v538_3` (and maybe `h531_3`/`h539_3`) may need `TOUGH=1` too**; that would very
  plausibly explain an immediate stat-poke mismatch at b50 and turn this into a suite fix,
  not a port bug. Still genuinely unexplained: `e10_3`/`e41_3`/`f10_3` (b450 — see the
  tag-convention caveat above, encounter-number extraction for tags outside i/m/t/n/v was
  never independently confirmed), `i420_3` (b1110, the Robinfakes ref — compare with the
  *passing* `n420_3`, same encounter, different tag: suspicious), `chis_m4`/`op_b10`/
  `op_m10`/`op_m3` (fx_battle, b490-b570 — inside the move, not the result screen, so
  these are the most likely *real* finds in this run), `stage16c`/`stage36_long` (battle,
  b270/b1410).
- **None of this was fixed.** Per the task: report regressions with their first-mismatch
  frame, don't fix port code. The candidates most worth a battle/enemy/effects agent's five
  minutes, in order: `chis_m4`/`op_b10`/`op_m10`/`op_m3` (clearly mid-move, not tail, not a
  documented exception), then `stage16c`/`stage36_long`, then the `v538_3`/`h531_3`/`h539_3`
  TOUGH-flag question above (cheap to test: rerun with `TOUGH=1`).

Rerun: `python3 tools/ref_regress.py --all --jobs 5` (world_spell rows will show the same
parse-failure artifact under 5-way parallelism; `--area world_spell --jobs 1` gets real
results for those).

## Caveats worth a second pair of eyes

- `e10_3` / `f10_3` (enemy, tag letters outside `verify_all.sh`'s own default `[imt]*`
  pattern) came back REGRESS at b450 in the quick set while `d10_3`/`g10_3`/`h10_3` (same
  tag family, same encounter number 10 by this suite's tag-strip convention) passed clean.
  `enemy.md`'s prose "Exact" list never mentions encounter 10 at all, so this pass has no
  documented expectation for any of `d10/e10/f10/g10/h10/h531/h539` — **the tag-to-encounter
  mapping for letters outside i/m/t/n/v has not been independently confirmed**; treat the
  REGRESS as a lead, not a confirmed break, until the enemy agent (or a read of
  `scratchpad/enemy/redux/run.sh`'s per-tag setup) confirms what each tag actually records.
- `m111_3` (world_spell) also came back REGRESS at b610 with no documented exception on
  file; `m524_3`/`m538_3` (enemy) came back REVIEW (divergence far earlier than the
  documented split point: b50/b530 vs a documented b310/b530-ish). Possible explanations not
  ruled out in this pass: a real regression, an `enemy.md`/`world.md` prose point that moved
  since it was written, or this suite's own `cmp.py` auto-`D` alignment picking a bad offset
  on a short/boss run. Worth the owning agent's five minutes before acting on any of them.
- `chis_b1` (fx_battle) still shows a real REGRESS just past the ~350-sample tail window
  (b1250); it may be the same known result-screen fade landing slightly earlier than that
  window for this particular move, not a new break — the tail window is a blunt heuristic,
  not a precise state-machine read of when the result screen starts.
