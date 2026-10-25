# Art creation "regression" (2026-10-25)

Task (coordinator): bisect ad0fb2f (good?) .. dc37aed (bad) for the Item Creation Art run's
4-byte difference from the Redux reference `scratchpad/menu/artr`, explain it, fix if it's a
port regression. Rules: no commit/push; headless only, memcap 4 GB, at most 2 game runs; scratch
in `scratchpad/artregress/`; worktree `scratchpad/bisecttree` only, removed at the end.

The 4 bytes (ref / port, from `scratchpad/artdiff/cmp_art.sh`):
- after_inventory +0x9b: 04 / 84 (entry 77's "new item" bit 15 still set in the port)
- after_records +0x1aa: 45 / 4d (record 2 +0xa: the item-bonus value; Redux has the rescanned one)
- after_game_state +0x1c: 0x0a5d / 0x000d (current music id)

## Findings

### 1. The reference and the current plan are different runs (10:00)

- `scratchpad/menu/artr` was recorded 2026-10-22 09:51-09:52 with plan (`artr/plan.txt`)
  `...,660:CROSS,1150:CROSS,1250:CROSS,1400:CIRCLE,1500:CIRCLE,1600:CIRCLE`, SS_SHOP_END 1750
  (stdout: menu scene at 2322, "dump after at 4072" = menu frame 1750). The three Circles
  back out and close the menu, so Redux's "after" dump is the field after the menu: music back
  to the field's 0x0a5d, the end-of-menu equipment rescan done (record +0xa 0x45), the new-item
  bit cleared.
- `scratchpad/menu/artp` (the port side that matched with 0 diffs) is from the same morning:
  its after_*.bin are 09:58:15, log "stopped after 2003 vblanks" = 1750 + 253, i.e. the old
  plan. The directory's 17:22 mtime is only `d10.png` / `anim2.png` added later.
- At 17:31:55 `crplan.py` regenerated every creation plan (`p_art/alch/auth/comp/ident/mach/
  metal/repro/surv`; cook/cust at 17:40) in the new "menu stays open, dump at
  SS_MENU_DUMP_FRAME = END + 12 (specrun.sh OPEN=1)" form. `runredux.sh` then re-recorded
  the references for all of them (17:33-17:42) **except art**: `p_art.txt` is now
  `...,660:CROSS,1360:CROSS,1560:CROSS`, END 1960 (no Circles), but `artr` is still the 09:52
  close-the-menu run. Every other creation reference's `plan.txt` equals its `p_*.txt`
  and its dump frame equals `p_*.end`; art's do not (1750 vs 1960).
- `scratchpad/hooks/in_art.txt` / `craft_ref.sh` (and `artdiff/run_art.py`) use the new
  `p_art.txt`, so the port's dump is taken at menu frame 1972 with the Art page still open:
  new-item bit still set, record +0xa still the camp page's item-bonus value, music still the
  menu's. That is exactly the 4 bytes.

### 2. Confirmed by runs (all deterministic, dumps compared with `cmp.sh`, i.e. cmpdump.sh)

Port runs: `scratchpad/artregress/run_plan.py LABEL BIN PLAN END [game args]` (run_art.py with
the plan as an argument; port frame = Redux frame + 153, SS_MENU_DUMP_FRAME = END + 12,
DELAY 60, END + 253 frames; writes a scratch config.ini because ad0fb2f still needed a disc1
there). `plan_old.txt`/`end_old.txt` = artr's plan (1750), `plan_new.txt`/`end_new.txt` =
current `p_art.txt` (1960).

| build | plan | vs | result |
|---|---|---|---|
| ad0fb2f (worktree) | current (1960) | artr | the same 4 bytes |
| ad0fb2f (worktree) | artr's (1750) | artr | 0 diffs |
| dc37aed (worktree) | artr's (1750) | artr | 0 diffs, end generator state identical |
| dc37aed (worktree) | current (1960) | fresh Redux ref | 0 diffs, generator identical |
| engine/build (main tree, 2026-10-25 08:07) | artr's / current | artr / fresh ref | 0 / 0, generators identical |

- **Fresh Redux reference** for the current plan: `scratchpad/artregress/redux_ref.py art_new_r
  plan_new.txt end_new.txt` (itemsref.lua, `-cli`, scratch HOME and card copies, under memcap
  4 GB; menu scene at 2322, dump at 4282 = frame 1960) -> `scratchpad/artregress/art_new_r/`.
  It has inventory +0x9b = 0x84, record 2 +0xa = 0x4d, music 0x000d: exactly the port's values.
  Old artr vs the fresh ref differ in exactly the 4 bytes. So the original, at menu frame 1960
  with the Art page still open, is in the state the port dumps.
- So there is **no first bad commit**: ad0fb2f and dc37aed give identical results under both
  plans, and each plan matches its own Redux reference. No `git bisect` was started (the
  premise "ad0fb2f is good with the current plan" is false).

### 3. Mechanism (why the 4 bytes are "menu open" vs "menu closed")

All three bytes change when the field menu closes, in the original's field_update (0x8004c334,
`analysis/decomp/field_full/8004c334_FUN_8004c334.c` ~line 1015): it restores game_state +0x1c
(`*(short *)(DAT_80075710 + 7) = saved music`, the field's 0x0a5d) and sets 0x80076278
(show_battle_results) = 1. On the next field frames (~line 209) that flag runs party_auto_equip
(0x80030d5c) -> stat_calc_auto_equip (0x80030e6c; REA disassembly: per member StatCalc
snapshots via 0x8003b508/0x8003b7b8/0x8003b9fc, 0x80030fc8 equip-best-new, and at the end
`jal 0x80030f84` = inventory_clear_new_flags). That StatCalc has no item data, so record +0xa
falls back from the camp page's item-bonus 0x4d to 0x45 (menu.md "Combat" finding), and bit 15
(new) of the created item's entry is cleared. The port does the same (menu_camp.cpp
`g.show_battle_results = 1; wr16(game_state + 0x1C, music)`, post_battle.cpp
field_post_battle_check -> party_auto_equip -> inventory_clear_new_flags).
artr's plan closes the menu with three Circles (1400/1500/1600) and dumps at 1750, after this;
the current plan leaves the menu open and dumps at 1960/1972, before it. Nothing diverges.

### 4. Other creation cases (main-tree engine/build, same harness)

| case | vs | result |
|---|---|---|
| art, seed (`--ap-seed scratchpad/hooks/craft.apso2`, no-fail off; 1 craft check) | fresh ref art_new_r | 0 diffs |
| alch none / seed (1 craft check) | menu/alchr | 0 / 0 |
| cust none / seed (1 craft check) | menu/custr | 0 / 0 |

### 5. Side note (pre-existing, not a state difference)

The *before* dumps differ in game_state +0x1c too (port 0x0a5d, Redux 0x000d), the same in
the 10-22 artp/artr pair: the port's SS_MENU_DUMP_OPEN is written before menu_launcher_create
(menu_camp.cpp ~1057), Redux's at the menu_scene_create breakpoint, after the menu music id is
set. field_flags +0x14 also differs before the menu (known field state, menu.md "query page").

## Conclusion

Not a port regression and no engine change. The Art reference `scratchpad/menu/artr` is stale:
recorded with the pre-17:31 close-the-menu plan, never re-recorded when crplan.py rewrote
`p_art.txt` (the other ten creation references were). `craft_ref.sh` / `artdiff/run_art.py`
compare the new plan's open-menu dump with the old plan's closed-menu dump.

**Recommendation** (not done: other agents' scratch dirs): adopt the fresh reference, e.g.
`mv scratchpad/menu/artr scratchpad/menu/artr_close1750 && mv scratchpad/artregress/art_new_r
scratchpad/menu/artr` (253 MB, mostly itemsref frame PNGs; its plan.txt = current p_art.txt),
then `craft_ref.sh none art` / `seed art` should report 0 diffs. Correct apv2.md's "a commit
between ad0fb2f and dc37aed regressed it" and hooks.md's "art differs in 4 bytes".

## Cleanup

Worktree `scratchpad/bisecttree` removed (`git worktree remove --force`, pruned); no bisect was
started. Scratch binaries deleted. Kept: `scratchpad/artregress/` (scripts, port dumps, the
fresh Redux reference). Engine suites not rerun: no engine file was changed.
