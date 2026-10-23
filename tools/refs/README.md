# Reference regression suite

`tools/refs/refs.csv` inventories every PCSX-Redux reference recording under `scratchpad/`
(battle killer moves and stages, enemy families/modules, the world agent's spell sweeps,
the effects agent's party-module moves, plus menu/world-map/legacy-effects refs that have
no confirmed automated recipe yet). `tools/ref_regress.py` replays the automatable ones
through the owning agent's own port-run script and compare tool — **never PCSX-Redux** —
and judges the result against the `expected` column.

## refs.csv columns

- `id`: the reference's directory name under its area's `ref/` (or `redux/ref/`).
- `area`: `battle`, `fx_battle`, `enemy`, `world_spell`, `menu`, `world_map`, `fx_legacy`.
- `run_cmd`: how the port side is reproduced by hand, for a human reading the sheet
  (`{build}` / `{out}` placeholders). `tools/ref_regress.py` does the same thing in Python,
  calling the same scripts (`pcrun.sh`, `cmpstage.sh`, `cmpstage_caster.sh`,
  `run_port.sh`/`run_port2.sh`, `port_pc.sh`, `sweep_*.sh`), not a re-implementation.
- `compare_cmd`: the compare tool (`rngcmp.py`, `unitcmp.py`, `framecmp.py`, `cmp.py`), or
  `(builtin: ...)` when the run script already prints the comparison itself.
- `expected`:
  - `rng_equal_to_exit` — RNG draws should run even with the reference clear through the
    battle's own end (the last ~350 "b" samples of a killer-move-style reference are the
    result screen and its exit fade, where a known, already-reported timing difference in
    `battle/result.cpp` shows up; `ref_regress.py` does not score a divergence found there).
  - `rng_equal_to:bNNNN reason` — a documented, expected split point (a spell module not yet
    ported into that path, a Shot kind not yet ported, an enemy-side original-bug fix that
    deliberately changes a position). A result within 30 frames of `bNNNN` passes; a big
    difference is `REVIEW`, not an automatic `REGRESS`.
  - anything mentioning `slowdown`, `slow run` or `tick` — the console ran one or more
    3-vblank frames here (not modelled, per the project's "enhance, don't degrade" rule); any
    divergence at or after it is expected, not scored.
  - `manual` — no automated recipe yet (the run needs parameters this pass could not confirm
    with confidence); `ref_regress.py` reports it as `SKIP`, never runs it.
  - `needs_owner` — no run **or** compare recipe is documented anywhere for this id yet
    (`world_map`'s non-spell refs, `fx_legacy`'s phase 1-3 field-effect refs); also `SKIP`.

## Known gaps (as of this pass)

- **menu** (15 ids): each has a documented Redux/port pairing in `docs/handoff/menu.md`
  (`equipref1` / `equip_p1`, `skillref1-4` / `skill_p1-4`, ...), but the literal plan string
  passed to `itemsrun.sh`/`specrun.sh` for each one was not re-derived with confidence in
  this pass. `cmpdump.sh` is the compare tool once a port run exists.
- **world_map** (28 ids, `scratchpad/world/ref/{cour1,explore1,menu1-3,mov1-3,...}`): recorded
  2026-10-22 by an earlier world-agent session; no current `docs/handoff/world.md` revision
  documents how they verify. Needs an owner to characterize before they can be automated.
- **fx_legacy** (23 ids, `scratchpad/fx_worker/ref129`, `ref548`, `refcred3/4`, ...): phase
  1-3 field-effects / credits references. `run_ref.sh`/`run_ref2.sh` only *record* Redux; no
  port-run script was found for them. Likely already covered by `ss_game_tests`' `SS_FX_REF`
  dump tests (`test_field_effects.cpp`) rather than this suite — worth confirming, not
  duplicating.
- **world_spell** (9 of 39 ids: `i340`, `iC340`, `it340`(+`_raw`), `iu340`(+`_raw`), `j340`,
  `r340`, `r340d`, `rn340`, `rt340`): item-use / Rena-cast tests with a `*_cfg.sh` env script
  each, but which cfg matches which id was not confirmed.
- **battle** (26 of 70 ids: `axe372`, `claude_l6`, `conf340`, `dense10`/`order10`, `enc35*`,
  `enc65`, `equip_71_85`, `equip_9f_86`, `fighter_ai`, `forced_1_3`, `moves_9_5`, `op_m8_trail`,
  `pg_cash`, `phasegun_*`, `rena_ai*`, `victory_2_3*`): bespoke scenarios, each with its own
  `pcrun.sh` env vars (`PC_IN`, `PC_EXTRA_POKE`, `PC_TICKS`, `PC_D`) documented in prose in
  `docs/handoff/battle.md` but not re-derived into a literal command with full confidence.
- **fx_battle** (14 of 95 ids): `as_b6`, `asv_b4d`, `chis_m4_box`, `op_b1_dense`,
  `op_b10_dense`, `op_m8_trail`, `prec_b4_box`, `prec_b4_cam`, `precsmoke`, `talk_ko`,
  `talk_prov`, `talk_flee`, `talk_ko2`, `talk_prov2`: diagnostic / one-off refs outside the
  killer-move `NAME CHAR BYTE [USES]` convention.

## Open: RNG re-sync points for CD-load-crossing references

The battle agent flags (2026-10-23, via the coordinator): a reference whose recording
crosses a CD-timed load (the field voice queue across a map load; battle state 0, the party
loader — the console takes ~44 frames per member, the port reaches state 1 at b0) needs an
explicit resync, or the generator position after the load won't line up at all, which this
suite would currently score as a hard REGRESS even though nothing ported is wrong. Port side:
`SS_BATTLE_RNG_SKIP=N` (new in `battle_debug.cpp`) makes N extra draws at the port's state
0->1 to land on the console's state-1 frame; checked equal to Redux at all 250 samples from
b102 on the late-save final-boss reference with N=1. Not yet wired into `refs.csv`/
`ref_regress.py`: add columns (or fold into `run_cmd`) for the sync event, the console frame
it happens at, and the N (or a Redux dump path, as `SS_ROUTE_RNG` does for route sync) to
pass as `SS_BATTLE_RNG_SKIP`/equivalent for that reference's handler. No current row in this
pass's `refs.csv` is known to need it, but any field-voice-queue or battle-state-0-crossing
reference added later will.

Promoting any of these from `manual`/`needs_owner` to automated just means: confirm the
exact run parameters (grep the id in its area's `docs/handoff/*.md`, or ask that area's
agent), add a row-specific entry to the small per-area tables in `tools/ref_regress.py`
(`FB_CHAR`, `STAGE_EXPECT`, `SWEEP_IDS`/`BOSS_IDS`, ...), and flip `expected` in `refs.csv`
away from `manual`.

## Editing the agents' scripts

`tools/ref_regress.py` needs to run several ref ids of the same area in parallel, each in
its own scratch config, and against whichever build directory the suite is pointed at. Five
of the agents' run scripts hardcoded both their own `XDG_CONFIG_HOME` and their own build
directory's binary path, which is fine for one agent running sequentially but unsafe for
five parallel jobs sharing one `XDG_CONFIG_HOME` (concurrent writes to the same config file)
and inflexible for "run this against `engine/build`, not `engine/build-battle`". Each got one
minimal, additive, backward-compatible edit (default behaviour unchanged when the new
variable is unset):

- `scratchpad/battle/pcrun.sh`, `scratchpad/enemy/run_port.sh`,
  `scratchpad/enemy/run_port2.sh`, `scratchpad/world/spell/run_port.sh`: `XDG_CONFIG_HOME=...`
  became `XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-...}`, and the hardcoded binary path became
  `${SS_BIN:-...}` (matching the pattern `scratchpad/fx_worker/battle/port_pc.sh` already used).
- `scratchpad/fx_worker/battle/port_pc.sh`: the same `XDG_CONFIG_HOME` override (it already
  had `SS_BIN`).

No other line in any agent's script was touched.
