# Handoff: heroslot (feasibility: the starting character in party slot 0, the hero to the back)

Agent "heroslot", 2026-10-26. FEASIBILITY STUDY: research and report only; no code changes to
engine/ or apworld/, no commits. Build dir `engine/build-heroslot` (RelWithDebInfo, AP net + setup,
deps from build-hubleader/_deps; linked 17:32 from the working tree at HEAD 5d43f47; other agents'
uncommitted ap.cpp / script_triggers.cpp edits are dated after it and touch no party code; 694 MB,
reusable by an implementing agent or deletable). Scratch:
scratchpad/heroslot/ (scan_slots.py, scan_rel.py, mapdis.py, run.py, party.py, seeds/, runs/).

## The user's question (verbatim)

"I had originally had the selection only capable of the 12, granted the leader.h and .ccp weren't
written at the time, it was prose. If that is the case, can the 'hero' then be backlined into slot 7
so that the primary character is the selected character IF a selected character is taken and is not
the hero?"

## Rules given

- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy). Every game process
  under the 4 GB cap (tools/memcap.py), at most 2 at once; builds -j4. Scratch XDG_CONFIG_HOME; never
  a config-saving flag; never the user's config or saves. Kill only by exact PID.
- No code changes to engine/ or apworld/; catalog rows (docs/catalog) for anything newly reversed
  are allowed. REA for native MIPS.
- Stop at ~400k context after updating this file.

## Status

**Done (2026-10-26).** Findings, design, decisions and the verification plan below; final report
sent to the coordinator. Catalog: docs/catalog/formats.md, two bullets after the map library's
battle routine ("relation routine", "Script ops that move party slots by position").

## Answer in one line

Feasible and legal: the original's own Party Order page already lets the player put anyone in slot 0
and the hero in any slot (slot 7 included, with empty slots in between), so every reader copes; the
change is "do once, at the starting member's join, what the player could do in the menu". But
party order is not what makes a character "primary" in the field: the walking actor is always the
hero's (G[0xc4] from script flag 1), so the leader stand-in stays as it is. Slot 0 gives the
battle's controlled member / first formation place, first place in every menu, and makes the
game's own fallbacks land on the starting member. Slot 7 benches the hero (no battles, no EXP).

## Findings

### Layout (engine/src/game/field/script_party.cpp, game/common/party.h)
- Handoff 3: 8 slots x 0x60 (+0 s16 id: 1-12 active, -id reserve (data kept), 0 empty); handoff 4: 8
  records x 0xd0, same index. Slot order = the Party Order page's two columns: slots 0-3 left (the
  front four), 4-7 right (the bench): table 0x800808e0 = 00 04 01 05 02 06 03 07 (REA, NA overlay
  0xbca). Slot 7 = the bottom-right box.
- Gaps are legal. Joins (party_add_member, script_party.cpp ~80): an existing reserve member comes
  back in its own slot; a new one takes the first slot that is not active (an empty slot, or a
  reserve slot whose occupant moves to the next empty one); with none, the first reserve slot is
  overwritten (the port protects kept members).
- Battle: battle_program.cpp ~223 takes slots 0-3 as they are (id < 1 = no unit, so gaps are
  fine); controlled member = config +0x41 (battle_init_state, party.cpp ~721, input.cpp ~598);
  formation start places by slot (g_party_formations). **EXP only for slots 0-3 still up**
  (result.cpp award_exp_money ~245). Defeat = no front member standing; no hero rule.

### The original can already reach "hero not in slot 0" (the key point)
- Party Order page (menu_combat.cpp PartyOrderPage; NA 0x8007e908 checked with REA): the first and
  second pick are any of the 8 boxes, **empty ones included**; the swap is undone only when no
  front member could fight (a character with status & 7 clear). So the player can put the hero in
  slot 7 with slots 1-6 empty, or anyone in slot 0. The destructor (0x8007e760) reloads voices,
  clears the bench's Link Combo pairings (record +0x3b / +0xce / +0xcf for slots 4-7) and, when
  config +0x41's slot is not active, re-points it to the first active slot. Catalog rows
  confirmed against PCSX-Redux (members swapped).
- So every positional reader below already copes with any order in the original.

### Positional readers of party slot 0 / party order (impact if the hero is not in slot 0)
- Field walking actor (map.cpp ~330): G[0xc4] = 0 / 1 from flag 1, actor model = the hero.
  **Not party order.** World map leader (map.cpp ~458, gs+0x24): same. -> no effect.
- Script ops: the only positional READ is A 0x26 get_slot_char, used only by the map library's
  relation routine (12 call sites; n = 4 at 211, 306, 523, 575, 603, 642, 685 x2; n = 8 at 208,
  209, 437, 469): pairs in slots 0..n-1 gain relation points (config +0x58 / +0xe8, the endings'
  tables). -> fine with the hero in 0-3; with the hero on the bench (slot 7) they miss the n = 4
  gains (pair endings).
- Script ops that WRITE positions: A 0x15 party_compact (55 sites, 26 maps; the tournament before
  each fight), A 0x16 / 0x17 save / restore order (305, 479, 688; restore 304, 469, 704), B 0x29 /
  0x2a PA isolate / restore, ext 0xa8 special 1 / 2 (map 12 only: Rena's game ch 4 step 150 forces
  Rena to slot 0, Claude to 1). ext 0xb0 swap: no call site. -> they can undo the reorder (below).
- party_validate_before_battle (encounter.cpp ~470, 0x80052ff4): front four empty -> first bench
  member to slot 0; config +0x41 not an active front slot -> first active front slot (persistent).
- Private Actions: party_isolate_hero saves (slot << 4 | id), puts everyone but the hero (by id) in
  reserve and swaps the hero into slot 0; party_restore_saved puts every saved member back in its
  saved slot (traced with the hero in slot 1 and in slot 7: exact restore). -> fine.
- Menus: menu_scene.cpp ~149 party_select(0) at menu open (default member = slot 0's: the starting
  member); camp menu / Party Order / Tactics / save list draw 8 fixed rows by slot (gaps = blank
  rows); shop / crafting "who" lists are slot order. -> cosmetic.
- Save: save_build (menu_save.cpp ~850) names the member with id == config[0x45] + 1 (the hero,
  by id) and level; the save list's party sprites are in slot order. -> cosmetic (the card title
  keeps the hero's name).
- Field: poison, Callways, auto-equip, post-battle report, pickpocket thief: all 8 slots, order
  only changes the report order. Voices: slots 0-3 (rebuilt on change). -> fine.
- AP layer (ap.cpp, skills.cpp, hub.cpp): all by id (slot_of_character, join_in_slot,
  free_reserve_slot, the status trap over slots 0-3). -> fine.
- Endings, relationship tables, death / game over: by id / by battle outcome. -> fine.
- The leader stand-in (leader.cpp candidate ~186): follows config +0x41's SLOT. -> works unchanged
  with slot 0.

### Runs (build-heroslot, headless, scratchpad/heroslot/runs; party.py prints slots + config +0x41)
Same scene, party as today `[hero, X]` lead slot 1 vs heroslot `[X, hero]` lead slot 0
(SS_SWEEP_PARTY_DROP / SS_SWEEP_PARTY order the slots; SS_SWEEP_CONFIG=41=N; seeds without a
starting character so the port's lead() does not reset +0x41):
- **Map 12, Rena's game, ch 4 step 150** (r12_*): both end `[Rena, (Claude), Ashton]`; today
  +0x41 = 1 -> Claude in reserve -> "leader not active"; heroslot +0x41 = 0 -> Rena: "the hero
  leads". **Both lose the stand-in after this scene (a pre-existing gap of today's stand-in in
  Rena's game)**, and the heroslot order is undone (special 2).
- **Tournament 477, Claude's game, ch 19 step 4400** (t477_*, Ashton active): both end
  `[Ashton, Claude, (Dias)]`; today +0x41 = 1 -> Claude: **today loses the stand-in**; heroslot
  +0x41 = 0 -> Ashton: kept. (In the real flow 468 / 469 set Rena, Celine, Precis, Ashton aside
  first; an unlisted starting member stays active as here.)
- **Linga 193, Claude's game, ch 22 step 3000** (l193*, Opera received): the split leaves only
  Claude active; no compaction: Opera comes back to her own slot (slot 0 in heroslot: order kept).
  With a debug battle inside the split (l193b_*): party_validate_before_battle re-points +0x41 to
  Claude (0 today / 1 heroslot) and it stays there after the split: **both lose the lead**.

## Design (recommended)

1. **The reorder (open world and hub, a starting member who is not the hero, New Game only).** In
   ap.cpp join_waiting's lead() (the starting member's join at the first free play, give_start's
   lead_start): after the join, `party_move_character_to_slot(X - 1, 0)` (special.cpp; the game's
   0x800531dc, the same swap the menu and scripts use) -> X slot 0, the hero in the slot X took
   (slot 1 at New Game), config +0x41 = 0. Nothing at create_game_state (the opening scenes before
   the first free play are written for the hero alone). Story mode, no seed, AP off, `hero`, a pick
   of the hero: no change (start_member() == 0). Loads: no change (lead_start is New Game only;
   the order is in the save like any order).
   - Slot 7 variant (the user's literal wording): then `party_move_character_to_slot(hero - 1, 7)`
     and clear the hero's record +0x3b / +0xce / +0xcf as the menu does for the bench.
2. **The lead follows the starting member (recommended; fixes the gap above in today's design
   too).** The port remembers the leader by character (X at the join; whoever the player leaves
   as leader when the camp menu closes); at free play outside PAs (flags 2 / 4 / 746) and split
   windows, when that member is active in slots 0-3 and config +0x41 points elsewhere, re-point
   it. Sidecar key (e.g. `leader`), so loads keep it.
3. **X back to slot 0 after story reorders (recommended with 1).** Same detection: if the player
   left X in slot 0 (last camp-menu close, or the join) and a scene moved them (map 12 special 2,
   compactions around splits), at the first free play outside PAs / windows swap X back into slot
   0 (the slot-0 occupant takes X's slot). That is exactly a Party Order swap at free play.
- The stand-in (leader.{h,cpp}) is unchanged and still needed (sprite and names): the walking
  actor is the hero's whatever the order. It gets more robust, not simpler: the game's own
  fallbacks (the Party Order destructor, party_validate_before_battle) pick the first active slot,
  which is now X.

## Decisions for the user

**The user's answers (2026-10-26), all four decided:** D1 "Hero goes to slot 1"; D2 yes, the
lead stays with the starting member across scenes; D3 yes, the starting member goes back to slot
0 after a scene reorders the party; D4 the save card keeps the hero's name. Built together with
the 12-member bench (docs/handoff/bench.md) on branch party-bench, since both touch the same join
and party-order code: docs/handoff/partybuild.md (piece 1), docs/ap/seed_format.md "The hero in
slot 1".

- D1 where the hero goes: **slot 1** (recommended: "the back" of a two-member party at the join;
  later joins fill 2-7; the hero keeps fighting, levelling and gaining relations) / **slot 7**
  (literal: the hero on the bench: no battles, no EXP (slots 0-3 only), misses the n = 4 relation
  events; the tournament's own fights (477 steps 3000-7000: everyone else set aside, A 0x15 brings
  the hero to slot 0) and any battle in a split that keeps only the hero then meet a hero at the
  level the bench left him; camp menu shows six blank rows) / slot 3 (last of the front four; two blank
  rows until two more join).
- D2 keep the lead with the starting member across scenes: yes (recommended).
- D3 put the starting member back in slot 0 after scenes reorder: yes (recommended), unless the
  player moved them in the menu.
- D4 the save card's title keeps the hero's name and level (save_build by config +0x45): keep
  (recommended; it is the card's identity) or show slot 0's member.

## Verification plan

- Unit tests (engine/tests/test_ap.cpp beside ap_v2_starting_character): the join -> X slot 0, hero
  slot 1 (or 7), +0x41 = 0; no starting member / the hero picked / story seed / no seed: party
  untouched; a load: untouched; D2 / D3 re-point and swap back, not after a camp-menu change, not in
  a PA or a window; party_isolate_hero / party_restore_saved with [X, hero, Y] (exact restore).
- Same-state proofs (leader.md style): New Game Claude's game (Ashton) and Rena's game (Leon) to
  the first free play: dumps differ from today's build only by the slot 0 / 1 swap (party and
  records) and +0x41 (a script swaps one back, then cmp); story seed / no seed / `hero`:
  byte-identical to today; rng signatures identical.
- Scenes (smoke cases from scratchpad/heroslot runs): map 12 (Rena's game) -> X back in slot 0 and
  leading after the window; Linga with a battle inside -> X leads after; 477 at 4400 / 6200 / 10200;
  a world-map PA from a save (X slot 0 -> the hero alone in slot 0 -> X back in 0); a debug battle
  with [X, hero] (X controlled; both get EXP); save / load round trip.
- Update smoke expectations that name slots: ap_v2_start ("joins the party (slot 1)", "leads
  (Formation leader, slot 1)"), ap_cs_*, ap_hub_leader_*; seed_format.md ("The hero is always in
  the party (slot 0, Private Actions)", starting_character, the stand-in's notes), the player guide.
- Suites: ss_tests, ss_game_tests, smoke; the APWorld needs no change (logic does not see order).

## Size

D1 alone: ~0.5 day code + ~1 day tests / proofs / docs. D2 + D3: +1.5-2 days (menu-close detection,
sidecar key, gating, the scene runs above). Total ~3 days for all three.

## How to reproduce the runs

    cd scratchpad/heroslot   # run.py LABEL KEY=VAL ... (memcap, scratch config), party.py DUMP...
    COMMON="SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1 SS_LEADER_TRACE=1 SS_SWEEP_PLAY=999:T0"
    python3 run.py r12_slot0 $COMMON SS_SWEEP_FLAGS=!1 SS_SWEEP_PARTY_DROP=0 SS_SWEEP_PARTY=6,1 \
      SS_SWEEP_CONFIG=45=1,41=0 SS_SCRIPT_GLOBALS=a0=4,a4=150 SS_INSTANT_TEXT=1 SS_AUTO_ADVANCE=500 \
      SS_SWEEP_PRESS=choices SS_STATE_DUMP="2700:runs/r12_slot0_2700.state" \
      "ARGS=--debug --ap-seed seeds/ow_rena.apso2 --frames 12000 --map 12"
    python3 party.py runs/r12_slot0_2700.state
    (t477: seeds/ow_claude.apso2, --map 477, a0=19,a4=4400, party 6,0 / 0,6 with DROP=0 for the
     first; l193b: seeds/ow_linga.apso2, --map 193, a0=22,a4=3000, party 8,0,1,2 / 8,1,2,
     SS_DEBUG_INPUT "5:ap_give Rena Bob;6:ap_give Celine Bob;7:ap_give Opera Bob;900:battle 1;1500:global 0xa4 5100")
