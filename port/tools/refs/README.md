# Reference regression suite

`port/tools/refs/refs.csv` inventories every PCSX-Redux reference recording under `scratchpad/`
(battle killer moves and stages, enemy families/modules, the world agent's spell sweeps,
the effects agent's party-module moves, plus menu/world-map/legacy-effects refs that have
no confirmed automated recipe yet). `port/tools/ref_regress.py` replays the automatable ones
through the owning agent's own port-run script and compare tool — **never PCSX-Redux** —
and judges the result against the `expected` column.

## refs.csv columns

- `id`: the reference's directory name under its area's `ref/` (or `redux/ref/`).
- `area`: `battle`, `fx_battle`, `enemy`, `world_spell`, `menu`, `world_map`, `fx_legacy`.
- `run_cmd`: how the port side is reproduced by hand, for a human reading the sheet
  (`{build}` / `{out}` placeholders). `port/tools/ref_regress.py` does the same thing in Python,
  calling the same scripts (`pcrun.sh`, `cmpstage.sh`, `cmpstage_caster.sh`,
  `run_port.sh`/`run_port2.sh`, `port_pc.sh`, `sweep_*.sh`), not a re-implementation.
- `compare_cmd`: the compare tool (`rngcmp.py`, `unitcmp.py`, `framecmp.py`, `cmp.py`), or
  `(builtin: ...)` when the run script already prints the comparison itself.
- `resync`: `KEY=VALUE;KEY=VALUE...` extra env vars for a reference that needs a re-sync
  point to compare correctly - a CD-timed load, a boss-intro-voice RNG draw, an enemy HP
  poked mid-fight, a mash-timing offset, or a run that needs more frames than a handler's
  default. Empty for most rows. `ref_regress.py`'s `parse_resync()` applies every key as an
  env var to both the port run and the compare step, except `FRAMES`, which each handler
  passes positionally (the run/compare scripts take it as an argument, not env). Known keys:
  `SS_ENEMY_HP`, `TOUGH` (enemy's own `h_enemy` sets this from the id's tag instead, see
  below), `CMP_FROM`/`CMP_R`/`CMP_DMAX` (`cmp.py`'s boss-fight offset search),
  `CMP_OFF` (`cmpstage.sh`'s mash-offset override), `FRAMES`, `SS_ORIGINAL_S20` (m9_3: the
  port keeps the original's dropped first-action menu Fairy Light, original_bugs.md S20).
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

## Known gaps (as of 2026-10-23, tools agent's resync/skip pass)

- **menu** (14 of 15 ids): `combref2` is now automated (`h_menu` in `ref_regress.py`,
  `scratchpad/menu/combref2/port_input.txt` - a port `--input` string already translated and
  confirmed against port run `comb_p2`, the menu handoff log "Combat, pages 8-10"); PASS
  (config/inventory/party/game_state exact, records' one known random check byte at +0x4ea
  tolerated, matching the "check bytes are random by design" note). The other 14 each have a
  documented Redux/port pairing and a `plan.txt`, but only `combref2`'s plan was also saved
  translated into the port's own `FRAME:button,...` form (confirmed rule from
  the menu handoff log: "Port frame = Redux frame + 153" for `itemsref1/2`/`equipref1`, plus
  whatever page-opening button presses get from the camp root to that specific page, which
  combref2's saved `port_input.txt` shows includes one extra `140:triangle,144:none` before
  the +153-shifted plan - not reverified independently for the other ids this pass).
  `cmpdump.sh` is the compare tool once a port run exists; `h_menu`'s `MENU_RUNS` table is
  where to add the next one.
- **world_map** (28 ids, `scratchpad/world/ref/{cour1,explore1,menu1-3,mov1-3,...}`): recorded
  2026-10-22 by an earlier world-agent session; still no the world handoff log revision
  documents how they verify (reconfirmed this pass - no mention of any of these ids). Needs an
  owner to characterize before they can be automated.
- **fx_legacy** (23 ids, `scratchpad/fx_worker/ref129`, `ref548`, `refcred3/4`, ...): phase
  1-3 field-effects / credits references. `run_ref.sh`/`run_ref2.sh` only *record* Redux; no
  port-run script was found for them. Reconfirmed this pass: `port/tests/test_field_effects.cpp`
  has its own `SS_FX_REF`-driven dump test (already in `ss_game_tests`) - likely the real
  coverage for these, not duplicated here.
- **world_spell** (9 of 39 ids: `i340`, `iC340`, `it340`(+`_raw`), `iu340`(+`_raw`), `j340`,
  `r340`, `r340d`, `rn340`, `rt340`): item-use / Rena-cast tests with a `*_cfg.sh` env script
  each, but which cfg matches which id was not confirmed.
- **battle** (26 of 70 ids: `axe372`, `claude_l6`, `conf340`, `dense10`/`order10`, `enc35*`,
  `enc65`, `equip_71_85`, `equip_9f_86`, `fighter_ai`, `forced_1_3`, `moves_9_5`, `op_m8_trail`,
  `pg_cash`, `phasegun_*`, `rena_ai*`, `victory_2_3*`): bespoke scenarios, each with its own
  `pcrun.sh` env vars (`PC_IN`, `PC_EXTRA_POKE`, `PC_TICKS`, `PC_D`) documented in prose in
  the battle handoff log but not re-derived into a literal command with full confidence.
  `victory_2_3`'s row already carries the effects agent's `CMP_D=7` finding in its
  `compare_cmd` column, for whoever promotes it next.
- **fx_battle** (14 of 95 ids): `as_b6`, `asv_b4d`, `chis_m4_box`, `op_b1_dense`,
  `op_b10_dense`, `op_m8_trail`, `prec_b4_box`, `prec_b4_cam`, `precsmoke`, `talk_ko`,
  `talk_prov`, `talk_flee`, `talk_ko2`, `talk_prov2`: diagnostic / one-off refs outside the
  killer-move `NAME CHAR BYTE [USES]` convention.

## Resolved this pass: RNG re-sync points for CD-load-crossing / boss-voice references

The coordinator's rule (2026-10-23): a reference whose recording crosses a CD-timed load (the
field voice queue across a map load; battle state 0, the party loader) needs an explicit
resync, or the generator position after the load won't line up, which this suite would
otherwise score as a hard REGRESS even though nothing ported is wrong. This is now `refs.csv`'s
`resync` column (see above), read generically by `ref_regress.py`'s `parse_resync()`.

- `SS_BATTLE_RNG_SKIP=N` (`battle_debug.cpp`) makes N extra draws at the port's battle state
  0->1, to land on the console's state-1 frame after its party loader (~44 frames/member).
  Checked equal to Redux at all 250 samples from b102 on the late-save final-boss reference
  with N=1 (the menu agent's `scratchpad/battle/late/p2`). **Tested this pass against the
  candidates it was suggested for (`v538_3`, `h531_3`, `h539_3`) and found it does not help
  them** - it made each one's match worse (v538_3 goes from exact to diverging at b330; h531_3
  from b440 to b60), so none of those three use it. They were a different resync need
  (`CMP_FROM`/`CMP_R`/`CMP_DMAX`, below, or no override at all for h531_3) or needed a party
  HP poke (`SS_ENEMY_HP`), not a CD-load skip. **No row in the current `refs.csv` is confirmed
  to need `SS_BATTLE_RNG_SKIP`** (the effects agent proposed it for `m538_3` too; tested, also
  made that one worse - b920/b930 without it vs b50/b330 with it). Still correct for any
  field-voice-queue or battle-state-0-crossing reference added later; wire it through the same
  `resync` column.
- `CMP_FROM`/`CMP_R`/`CMP_DMAX` (`scratchpad/enemy/cmp.py`'s own boss-fight offset-search
  override, `verify_all.sh`'s documented usage: "boss fights need CMP_FROM=60 CMP_R=1
  CMP_DMAX=80 (537: CMP_R=2)"): the actual fix for the boss-intro-voice encounters (537 Vesper
  + Decus, 538 Cyril, 539/540 Indalecio) - the console draws RNG for the intro voice that the
  port draws during setup, and the default 0..8 D-search picks the wrong offset without this.
  Set on `h539_3`, `t539_3`, `t540_3`, `v538_3` (all `CMP_R=1`) and `m537_3`/`m538_3`/`m539_3`
  (`m537_3` needs `CMP_R=2`). `h531_3` (Bark) has no intro voice and does **not** use this -
  the plain default search already lands on its documented split.
- `SS_ENEMY_HP=DELAY:INDEX:VALUE` (the enemy handoff log section 4): replicates a mid-fight
  enemy HP poke the Redux reference was recorded with. Set on `h531_3` (`60:0:130000`) and
  `h539_3` (`0:0:300000`).
- `FRAMES=N`: a longer run than a handler's own default. Set on `t539_3` (3050), `t540_3`
  (2550, both tough-Claude Indalecio runs that need 2500-3000 vsyncs to reach their documented
  split) and `stage36_long` (2650 - its own reference runs 2600 vsyncs but `cmpstage.sh`'s
  default FRAMES=1400 cut the port run short, freezing its RNG count well before the end).
- `CMP_OFF=-1` (`cmpstage.sh`'s own mash-offset override): `stage8_light`/`stage8_shots`'
  documented b940 split only shows up with the cross-press mash one frame earlier than
  `cmpstage.sh`'s generic 45i formula gives (the battle handoff log: "inputs at 45i-1").
- Tag-driven, not resync-column: `h_enemy` sets `TOUGH=1` for tags `t` and `v` (both "tough
  Claude" recordings per `verify_all.sh`'s own comment - `v538_3` was missing this before this
  pass), and does *not* set `NO_MASH=1` for tags `e`/`f` (recorded with Cross mash, not idle -
  the effects agent's finding for `e10_3`/`f10_3`; every other tag's references were recorded
  idle via `batch.sh`'s own override). `stage16c` moved from `cmpstage.sh` to
  `cmpstage_caster.sh` (a caster ref, not a plain stage ref); `caster35`/`caster35s` moved to
  `cmpstage_caster7.sh` (recorded in the 2-1-2 tick group, not cmpstage_caster.sh's 2-2-2).

Promoting any `manual`/`needs_owner` row to automated just means: confirm the exact run
parameters (grep the id in its area's handoff log: the battle, enemy, menu, world and effects
logs were removed in the 2026-10-28 cleanup; docs/HANDOFF.md says how to read them), add a
row-specific entry to the small per-area tables in `port/tools/ref_regress.py` (`FB_CHAR`,
`MENU_RUNS`, `SWEEP_IDS`/`BOSS_IDS`, ...) or the `resync` column, and flip `expected` in
`refs.csv` away from `manual`.

## Editing the agents' scripts

`port/tools/ref_regress.py` needs to run several ref ids of the same area in parallel, each in
its own scratch config, and against whichever build directory the suite is pointed at. Five
of the agents' run scripts hardcoded both their own `XDG_CONFIG_HOME` and their own build
directory's binary path, which is fine for one agent running sequentially but unsafe for
five parallel jobs sharing one `XDG_CONFIG_HOME` (concurrent writes to the same config file)
and inflexible for "run this against `port/build`, not `port/build-battle`". Each got one
minimal, additive, backward-compatible edit (default behaviour unchanged when the new
variable is unset):

- `scratchpad/battle/pcrun.sh`, `scratchpad/enemy/run_port.sh`,
  `scratchpad/enemy/run_port2.sh`, `scratchpad/world/spell/run_port.sh`: `XDG_CONFIG_HOME=...`
  became `XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-...}`, and the hardcoded binary path became
  `${SS_BIN:-...}` (matching the pattern `scratchpad/fx_worker/battle/port_pc.sh` already used).
- `scratchpad/fx_worker/battle/port_pc.sh`: the same `XDG_CONFIG_HOME` override (it already
  had `SS_BIN`).

No other line in any agent's script was touched.
