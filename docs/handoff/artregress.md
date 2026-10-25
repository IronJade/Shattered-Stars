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

So the expected-good commit ad0fb2f should show the same 4 bytes with the current plan. Being
checked next (worktree build), plus the old plan on HEAD and a fresh Redux reference for the
current plan.

## Steps
