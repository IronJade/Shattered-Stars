# Handoff: shingofix (the Shingo Forest borrowed slot, port 0.02.51)

Agent "shingofix", started 2026-10-27, on main. Scratch: scratchpad/shingofix/. Build:
engine/build-shingofix (RelWithDebInfo, Ninja, deps from engine/build-hubleader/_deps), -j4.

## Rules given

- Fix on main the bug partybuild6 found on party-bench: with all 8 slots taken in Rena's game
  (open world), map 12's scene drops a member. Same key name and semantics as the party
  branch's fix (`by` in the sidecar's stash entries), nothing bench-specific (`lent`, loans).
  Never modify scratchpad/wt/party (read only).
- Same state: with the party not full, or no borrowed slot, every run byte-identical to today;
  AP off untouched; save card bytes unchanged; older sidecars keep loading.
- Audit every place the game or the AP layer moves members between slots while the AP layer
  has borrowed, stashed or temporarily added someone; fix losses in the same style with a test;
  stop and report with options if a fix would change how a scene plays.
- Low load: one heavy job at a time (a build or a game process), -j4, game processes under the
  4 GB cap; headless only; scratch XDG_CONFIG_HOME; kill only by exact PID. hellseed2 works on
  apworld/ and the setup program / seed.cpp: don't touch them.
- Commits on main after the verified fix, docs first, staged by path; never push.

## Status

- [x] 1. Reproduce on main headless (runs/r12full: Ashton lost)
- [x] 2. Fix (`by`)
- [x] 3. Audit (table below); one further finding reported with options (special 0xc / 0xd on a
  kept Dias), not fixed: it would change how the tournament plays
- [x] 4. Tests: unit (ap_v2_party_borrow, new ap_v2_party_borrow_moved), smoke case
  ap_v2_m12_full; full suites: see "Suites" below
- [x] 5. Docs: seed_format.md (`by`, the party rules), docs/catalog/formats.md (the tournament's
  fights), release_notes/0.02.51.md (Fixes), this file

## The reproduction (main, da4652b + nothing)

    cd scratchpad/shingofix   # run.sh LABEL MAP FRAMES VAR=VAL... (BIN=, SEED=, CARD=); party.py DUMP...
    ./run.sh r12full 12 3000 SS_BATTLE_STUB=1 'SS_SWEEP_FLAGS=!1' SS_SWEEP_PARTY_DROP=0 \
      SS_SWEEP_PARTY=6,1,2,3,5,7,8,10 SS_SWEEP_CONFIG=45=1 SS_SWEEP_PLAY=999:T0 \
      SS_SCRIPT_GLOBALS=a0=4,a4=150 SS_STATE_DUMP="30:runs/r12full_0030.state,2990:runs/r12full_2990.state"

Seed seeds/v2rena.apso2 (the smoke's: Rena's game, open world, party_rules open, Claude not
received). Party at the start `[Ashton, Rena, Celine, Bowman, Precis, Leon, Opera, Noel]`.
Log (bin0 = main): `Claude borrows slot 1 of Rena`, `join Claude ... for a while (Shingo Forest
(map 12)): Ashton, Claude, ...`; vblank 30 `[Claude, Ashton, Celine, ...]` (special 1 moved
Claude to slot 0, Ashton into the borrowed slot 1); after the fight `Rena back in slot 1 (Ashton
leaves it)`; end `[Rena, (Claude), Celine, Bowman, Precis, Leon, Opera, Noel]`: **Ashton gone**
(Claude, a temporary member, leaves at the next free play: 7 members).

## The fix (the party branch's, minus loans)

- `SaveState::Stash::by` (ap.h): who borrowed the slot (party id; 0 = a sidecar from before).
- ap.cpp: `stash_reserve_slot` (the stash, `by` set) + `borrow_slot` (as on party-bench);
  `restore_stash`: when the slot holds someone else, the borrower (by `by`; from an old sidecar,
  a temporary member) is swapped back into it first (`party_swap_slots`), then the stash lands
  on them; borrower not in the 8: an empty slot if any (not seen); `party_v2_update`: a stash is
  kept while its borrower is in, active and needed, wherever they stand; sidecar: `by` written
  when non-zero, read with 0 for absent / outside 1-12. No sidecar version change.
- Same state: no stash, or the borrower in the slot (every case before a scene moves them):
  the code takes the old path. runs/m12small_b0 vs _b1 (party not full): log and state dump
  identical.
- With the fix: runs/r12fix: `Claude back to the slot they borrowed (1, from 0), Ashton to 0`,
  `Rena back in slot 1 (Claude leaves it)`, end `[Rena, Ashton, Celine, Bowman, Precis, Leon,
  Opera, Noel]`.
- Debug aid: `SS_SWEEP_PARTY_ASIDE=c,...` (event_sweep.cpp): after SS_SWEEP_PARTY, these to the
  reserve (party_remove_member), for a split's party from a --map start.

## The audit

Where the AP layer can hold a borrowed slot (a stash) at all: only while a split window lets a
non-kept member in for a fight (`kSplitWindows` temps: Shingo Forest, Rena's game 4/150; the
tournament, chapter 19 from step 1000) and every slot holds a kept member. The stash ends at the
next free play once the borrower is set aside, or when the stashed member joins. Temporary
members (no stash) are tracked by id and leave by id.

| Site | What it does to slots | Risk with a full party and a borrowed / temporary member | Result |
|---|---|---|---|
| Map 12 ext 0xa8 specials 1 / 2 | special 1: Claude to slot 0 (swap); 2: Rena to 0, Claude to 1 | Claude moved off the borrowed slot; Rena's join put her stash on the member moved in | **Bug, fixed** (`by`); r12full / r12fix; unit + smoke |
| Tournament 477 (A 0x15 before and after each fight; leave / join pairs) | compaction swaps active members up over reserve ones | A kept member set aside before the hero (Claude's game: e.g. a starting / received member in slot 0, set aside by 468 / 469) is the stash; after the fight A 0x15 moves another member into the borrowed slot; the next free play put the stash on them (main's keep test reads the slot's member, not the borrower) | **Bug, fixed** by the same `by` (party_v2_update + restore_stash). Headless: runs/t1_base (main + the sweep aid; starting Ashton set aside in slot 0, Claude in 1) ends the fight at `[Rena, Celine, Bowman, Precis, Leon, Opera, Claude, (Dias)]` with slot 0 stashed (Ashton's): main's next free play would put Ashton over Rena; runs/t4400g (fix; four received members set aside) the same shape, `[Bowman, Leon, Opera, Claude, (Dias), ..]`. The free play itself isn't reached headless (map 476 then holds a menu: state 3); unit test (the exact sequence) fails on main, passes fixed |
| A 0x16 / 0x17 save / restore order (305, 479, 688; 304, 469, 704) | saves 8 ids by position (game_state +0x280); restore moves each id back (swap) | swaps only; a stashed id is not found (skipped); a moved borrower is found by `by` | No loss |
| A 0x15 compact elsewhere (55 sites) | swaps | as above | No loss |
| PAs: B 0x29 party_isolate_hero / B 0x2a party_restore_saved, field_flags +0x6b / +0x6c | saves active members of slots 0-7 (slot << 4 \| id), all but the hero to reserve, hero to slot 0 (swap); restore by id to the saved slot | No stash can exist in free play outside a temp's fight (kept only while the borrower is active and needed); party_v2_update / join_waiting wait while isolated (flags 2 / 4 / 746); swaps only | No loss. **PAs read only slots 0-7** (isolate / restore / B 0x2b / B 0x2c / ext 0xb1 party_find_slot loop 0-7) |
| Leon / Dias temporary joins and re-adds (434, 437, 439, 469, 477) | ext 0xae / 0xaf | v2 open: not received = refused (only the tournament needs Dias: temps); received = game's join in place, else a non-kept reserve slot, else skipped (waits) | No loss |
| Arena 305, Cave of Trials 704, Fun City contest 688 team picks | leave / join, A 0x16 / 0x17 | windows "everyone", no temps: no stash, no temporary member | No loss |
| Map 125's Celine restore | ext 0xae (listed site) | received: game's join (in place or empty slot); full: non-kept reserve slot or skipped | No loss |
| Special 0xd (Dias re-init), 0xc (fixed stats) | rewrite Dias's own slot data (party_find_slot) | no other slot touched; **but a kept Dias's data is rewritten** (see below) | No slot loss; data finding reported |
| party_validate_before_battle (native 0x80052ff4) | first of 4-7 to slot 0 when 0-3 hold nobody active (swap); +0x41 | swaps only; a moved borrower found by `by` | No loss |
| AP join_in_slot via free_reserve_slot | overwrites a reserve member the port does not keep | intended (the original overwrites the first reserve slot); could hit a set-aside temporary borrower mid-scene, then the stash finds no borrower; restore takes an empty slot, else overwrites the slot's member. No known scene does this (after each temp fight the next join is the hero / stashed member, restore_stash_of first) | Residual, not reachable in known scenes; noted |
| party_v2_update temporary leave | zeroes the temporary member's slot, by id | runs after the stash loop (a borrower's stash goes back first) | No loss |

**Finding, not fixed (would change how the tournament plays): special 0xc / 0xd on a kept
Dias.** In the original Dias is never a party member at the tournament (Claude's game; Rena's game
"Dias away" 15/2700-19/1500), so 0xc (after his first fight: level 35, EXP 96982, HP 2500 ...)
and 0xd (after the later ones: his starting data again, then the join level) only touch a stand-in.
With Dias received (or the starting character) they rewrite the player's Dias: runs/t4400dias
(starting Dias, level 1 by the join level) ends at level 35, EXP 96982, HP 2500; a levelled Dias
would go down, and 0xd later wipes his equipment, skills and killer-move counts. Options: (a)
leave it; (b) keep a kept Dias's 0x130 bytes before 0xc and put them back after his last
tournament fight (the fights keep the story's stats, he leaves as he came; recommended);
(c) skip 0xc / 0xd for a kept Dias (his own stats in the fights: the fights change).

**For the bench branch (party-bench):**
- PAs, A 0x15 / 0x16 / 0x17, party_validate_before_battle and the specials read and swap slots 0-7
  only; a benched member must never be swapped into 0-7 between B 0x29 and B 0x2a (restore puts
  members back by the saved slot), nor between A 0x16 and A 0x17 (it would be moved by id).
- With heroslot (the starting member in slot 0, the hero in slot 1), the tournament case above is
  the normal one in Claude's game when the starting member is one 468 / 469 set aside (Rena,
  Celine, Precis, Ashton): keep the `by` fix and the keep test exactly as here (and the loans'
  stash goes through the same path).
- `restore_stash`'s fallback (borrower not in the 8, no empty slot) overwrites the slot's
  member; unreachable today, but a loan or bench move that takes a borrower out of the 8 while a
  stash is pending would reach it. Refuse such moves while `stash` is non-empty (order_locked
  already does for Party Order).

## Tests

- engine/tests/test_ap.cpp: ap_v2_party_borrow (the party branch's added lines, as they are),
  ap_v2_party_borrow_moved (map 12's sequence with free play in between; an old sidecar without
  `by`; the tournament with A 0x15 before / after; `by` parsing). Both fail on main (built in
  scratchpad/shingofix/wt0 with the `.by` lines taken out: 2 failed) and pass fixed.
- tools/smoke_field_events.sh: ap_v2_m12_full (4 checks; mini_smoke.sh: PASS fixed, 3 FAIL on main).

## Suites

Frozen binaries and smoke script in scratchpad/shingofix/final/ (built from the working tree with
hellseed2's uncommitted seed / setup changes in it, as main's tree was): ss_tests 103 / 0,
ss_game_tests 198 / 0, full smoke (final/smoke.log) 311 PASS / 0 FAIL, rc 0.

## Scratch

scratchpad/shingofix/: run.sh, t477.sh, tour.sh, mini_smoke.sh, party.py, lvl.py, seeds/, runs/,
bin0 (main before the fix), binw0 (main + the sweep aid, for the tournament baseline), final/
(the fixed binaries, smoke copy and suite logs). The baseline worktree wt0 was removed.

## Done

Commits 1ab7000 (docs) and a1827a9 (port) on main, not pushed. Open for the coordinator: the
special 0xc / 0xd finding (options above).

## The user's decision on the tournament's Dias (2026-10-27)
"b) Save his data before the tournament rewrites him, and restore it after his last fight."
So: when Dias is a kept member (a starting member or a received character) and the tournament's
specials 0xc / 0xd rewrite him (run t4400dias), the port saves his 0x130 bytes (slot + record)
before 0xc and puts them back after his last fight there; the fights keep the story's Dias.
Not built yet (queued for a follow-up agent, port 0.02.51).
