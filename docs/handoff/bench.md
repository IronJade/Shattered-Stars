# Handoff: bench (feasibility: a 12-member party, bench slots 8-11 kept by the port)

Agent "bench", 2026-10-26. FEASIBILITY STUDY: research and report only; no code changes to
engine/ or apworld/, no commits. Build dir `engine/build-heroslot` (reused from heroslot). Scratch:
scratchpad/bench/.

## The user's request (verbatim, 2026-10-26)

"12-member bench. I like the idea of being able to swap the bench characters in and out. We would
need to add capability for them to retain their equipment and such as well though. That said,
anywhere that swapping characters works right now, from 0-3 to the 4-7 group, should allow also
the 8-11(bench) slot changes. Check the feasability if you could."

Also decided 2026-10-26 (docs/handoff/heroslot.md "Decisions for the user"): D1 the hero goes to
slot 1 when a starting member who is not the hero joins at New Game; D2 the lead stays with the
starting member across scenes; D3 the starting member goes back to slot 0 after a scene reorders
the party, unless the player moved them in the menu; D4 the save card keeps the hero's name.

## Rules given

- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy). Every game
  process under the 4 GB cap, at most 2 at once; builds -j8. Scratch XDG_CONFIG_HOME; never a
  config-saving flag; never the user's config or saves; SS_AP_ANY_SAVE=1 only for headless loads
  of ordinary saves with a seed. Kill only by exact PID.
- No code changes to engine/ or apworld/; edits only to this file, docs/catalog (new rows /
  bullets), scratchpad/bench/. No commits. Others' uncommitted edits in the tree: leave them.
- REA for native MIPS; catalog anything newly reversed.
- Stop at ~400k context after updating this file.

## Status

**Done (2026-10-26).** Findings, design, decisions, verification plan and size below; final
report sent to the coordinator. Catalog: docs/catalog/formats.md, bullet "What a party member
is" under "Party slot (0x60 bytes, handoff 3) and character record". Nothing built.

## Answer in one line

Feasible, and a natural fit: the game itself treats a member as exactly the slot (0x60) and the
record (0xd0) and moves members by copying those 0x130 bytes (its own Party Order swap), so a
port-kept bench of four entries of 0x130 bytes (places 8-11) keeps everything (equipment, skills,
killer moves and their use counts, talents, tactics, level, EXP, HP / MP, status). The game keeps
seeing its 8 slots; the bench lives in the save's sidecar; only the Party Order page and the
Archipelago join path ever touch it. Benched means out of the party for every script (no PAs,
endings, relation points, EXP, Item Creation, inns) until swapped in.

## Findings

### 1. What makes up a member (per member / per character id / per place)

**Per member (moves with the member: inside slot 0x60 + record 0xd0).** Slot: +0 id (negative =
set aside, data kept), +2 field status, +0x10 EXP, +0x14..+0x24 HP / MP (base, max, current),
+0x26 battle level copy, +0x28 level, +0x2a.. attributes. Record: +0xc..+0x18 the seven equipment
slots (+0x16 / +0x18 accessories), +0x1a SP, +0x20 talents, +0x24 name copy, +0x38 / +0x39 Tactics
orders, +0x3a bits (0 "EW", 1 favourite food known, 2 Practice), +0x3b Link Combo partner (an id),
+0x3c + i moves / spells known (sign = on / off), +0x5c + skill skill levels (sign = battle skill
on / off), +0x8a + 2 * move killer-move use counts (what ap.cpp scan_killer_moves and moves.py
read), +0xcc..+0xcf the two killer-move sets. Specialties / super specialties / IC levels are
computed from these (party.cpp char_specialty_info, party_super_specialty_info: super specialties
over the members active in all 8 slots). Evidence:
- The original moves members with exactly these blocks: Party Order `party_swap_members`
  0x8007f0bc (menu_combat.cpp swap_members), `party_move_character_to_slot` 0x800531dc
  (special.cpp:47), party_swap_slots (script_party.cpp: A 0x15, PA isolate / restore).
- Headless (runs/swap07 vs runs/ctl07, scratchpad/bench/member_cmp.py): on a save with 8 members
  (cards/eight.mcd), Party Order swap of slot 0 (Claude) and 7 (Opera): every member's 0x130
  bytes identical after the move; the state block (0xa40), config (0x2a0) and inventory (0xc28)
  identical to a control run that opened the same page and cancelled (only party 36 bytes and
  records 56 bytes differ: the two moved members). config +0x41 stayed 0, so Opera led.

**Per character id (stays put; nothing to move):** config +0x58 / +0xe8 relation tables (12 x 12),
+0x42 / +0x43 renamed heroes, +0x45 the hero; game_state +0x28 + (id - 1) * 0x14 names (renaming
writes both these and record +0x24); script flags by member (758-769 "in the party before a
split", 112 Celine travels, PA scenes); field sprite packs (party_member_mask: ids active in the 8
slots, read at map load: map.cpp:103); battle banter tables by character; the AP sidecar's
characters / characters_waiting / temporary / moves / starting_character.

**Per place (belongs to the slot, never to the bench):** config +0x41 the Formation leader /
controlled member (a slot 0-3; the Party Order destructor re-points it only when its slot
empties; party_validate_before_battle encounter.cpp:479), config +0x40's start positions
(g_party_formations[formation][slot]), the battle voice banks of slots 0-3 (g_voice_reload_member
/ voice_slot_invalidate on a front change), game_state +0x280..+0x287 (A 0x16's saved order;
A 0x17 moves each saved id back by searching the 8 slots: a benched id is simply not found),
field_flags +0x6b / +0x6c.. (PA saved members, slot << 4 | id; B 0x2c saved_party_has reads it),
field_flags +0x38 + 4 * slot (party_stash_slot_ids around a battle), the AP sidecar's `stash`
(a slot a temporary member borrowed), the menu's selected member (party.cpp g.selected).

### 2. Where the game swaps or moves members today

- **Party Order** (camp menu > Combat > "Replacement" in NA; menu_combat.cpp PartyOrderPage,
  0x8007e594 / 0x8007e908 / 0x8007e760): 8 boxes of 0x9c x 0x32 at (col * 0x9c, row * 0x32), two
  columns filling 312 of 320 px and rows 0-199 (help bar 200-223): captures
  shots/po8c_sheet.png. Grid 2 x 4 row by row, box -> slot by table 0x800808e0. First pick pushes
  a second cursor; the second pick swaps; undone when no front member can fight (id > 0, status
  & 7 clear). Close: voices for changed front slots, slots 4-7 lose +0x3b / +0xce / +0xcf, +0x41
  re-pointed if its slot emptied. **The only place the player swaps.**
- **Not swaps:** Tactics ("Strategy", same 8 boxes: orders), Formation (formation + Leader dialog
  among the front four), the camp page's member list (8 rows that scroll; Equipment / Skills /
  Status / Specialty pick a member there), Item Creation / shops (8-slot lists).
- **Battle:** no member swap in SO2; only control switching among the front four (input.cpp:593,
  key 6 toggles with config +0x41's member). The battle binds slots 0-3 at start
  (battle_program.cpp:221); EXP only for slots 0-3 still up.
- **Scripts (never the bench):** A 0x15 compact, A 0x16 / 0x17 save / restore order, B 0x29 /
  0x2a and party_isolate_hero / party_restore_saved (PAs), ext 0xa8 specials 1 / 2 (map 12), ext
  0xb0 (no call site), ext 0xae / 0xaf joins / leaves, special 0xd (Dias re-init, by active
  slot), the library's battle-subset routine (keeps four by id), the arena (305), the Cave of
  Trials (704) and Fun City contest (688) team selections, the tournament's juggling (477).
- **Native:** party_validate_before_battle (front empty -> first of 4-7 to slot 0; +0x41),
  party_stash_slot_ids.
- **AP:** join_waiting / on_party_add / borrow_slot / restore_stash / party_v2_update
  (ap.cpp ~540-650, 1131, 1824), heroslot D1 / D3 (not built).
- The camp menu opens in towns, dungeons, the world map and during split windows; its check
  (menu_camp.cpp:987, field 0x8004c334) tests flags 12 / 14 and the VM, not the PA flags 2 / 4,
  so Party Order is reachable during PAs too (not tried in play).

### 3. Semantics (benched = out of the party)

Nothing in the game reads the bench, so a benched member: has no PA (towns test B 0x2c
saved_party_has / ext 0xb1), no party talk, no relation points (the relation routine reads slots
0..n-1; PAs), no ending (selection = ext 0xb1 pairs; solo endings per member in the party), no
EXP (reserve 4-7 gets none either), no field-step effects (Callways songs, accessory step EXP /
regen), no inn healing (party_restore_hp_all: 8 slots), no Item Creation or super-specialty share,
no accessory flags that map setup reads (B 0x5f party_has_equipped), no field sprite (packs by the
8 slots' ids; a member swapped in mid-map has no field sprite until the next map load, as for
today's mid-map AP joins; the stand-in has its own-pack fallback). Splits never see them (flags
758-769 come from party_has), so restores never re-add them. Today's ninth received member waits
forever ("the party is full (8); received characters wait", runs/ninth.log).

### 4. Storage

- Sidecar key `bench`: up to four `{ "place": 8-11, "bytes": hex of 0x130 }` (the stash's
  format, ap.cpp:797); absent = empty. `bench_hash`: "sha256:" of the save's bytes (as save_hash)
  followed by the four places' 0x130 bytes (zeros for an empty one), written at every save;
  load_allowed refuses a bench that does not match (an edited bench, or one moved to another
  save), with the same message as an edited save. Sidecar `version` 3 only when `bench` is
  present (an older port then refuses the sidecar instead of dropping members); no bench: the
  same sidecar as today.
- The bench is game state, not multiworld state: a load takes the sidecar's bench as it is (no
  merge, unlike `checked`); a load without a sidecar (SS_AP_ANY_SAVE only) and New Game: empty.
- The card stays byte-compatible (the bench never touches it; save_serialize copies the 8 slots,
  menu_save.cpp:983). The card's 8 slots are a valid party for the original game or an older
  port, which simply lack the benched members (and the equipment they wear). The save title
  names the hero by id (save_build menu_save.cpp:850): the hero can never be benched (D4 holds).
- Character select (charselect): unaffected; its choices become every member not excluded.

### 5. The AP join path

- A received member with no room in the 8 (and no window / PA holding the party) goes to the
  bench, built as the game's join builds them: `party_slot_init(bench_slot, bench_record, id)`
  (field characters.cpp:548; it takes pointers, so it can build straight into a bench entry; same
  rng draws as any join), then the join level and skillsanity pass (skills.cpp on_member_init /
  special_pass: needs a pointer variant; the median over the 8 slots and the bench), then the
  bond clear (A 0x3f: +0x3b / +0xce / +0xcf). Order of choice: an empty slot (the game's join,
  unchanged) -> a bench place -> a reserve slot of a member the port does not keep (today's
  free_reserve_slot) -> wait. Banner "Ernest joined the party (bench)" (<= the banner width).
- 12 places for 12 characters: nobody ever waits for room again (only for PAs / windows).
- Bench-aware lookups by id: skillsanity receive_move (skills.cpp:320 slot_of -> the bench
  record, else the move is lost when the member later comes in without a join), scan_killer_moves
  (ap.cpp:2620), slot_of_character users that must not treat a benched member as absent
  (join_waiting "in the party already", on_party_add, party_v2_update's rejoin loop must skip
  benched members, the stand-in and D2 / D3 lookups).
- Story joins of a benched member: no-op (they stay benched; the scene goes on), except a split
  window that needs them alone (Claude at Shingo Forest in Rena's game, Claude / Dias at the
  tournament): lent from the bench into a free slot (or borrowed slot) for the window and put back
  on the bench at its end (a sidecar list like `temporary`). Story leaves / partings of a benched
  member: no-op (stays benched). Special 0xd (Dias re-init) on a benched Dias: no-op.
- A story join of a new member with the 8 full (party_rules story + starting character, today
  "skipped"): to the bench for members the port keeps; temporary ones borrow as today.

### 6. The APWorld

items.PARTY_SIZE 8 -> 12 for open world and hub (story mode stays 8 with the lockouts): in
choose_open_party room >= every allowed member, so the pool is every member not excluded (no
random sample) and the healer guarantee never triggers (Rena / Noel are in unless excluded:
remove the code). Seed: options `bench: true` (or `party_size: 12`) and version 4 (a 1-3 port
refuses: "unsupported version 4 (this build reads 1 to 3)"); hub bench seeds are 4 too. Logic:
unchanged (fighter_status "item" for every pool member is right: a received member can be
swapped in at any free play); more killer-move / craft / learn locations (members no longer left
out). charselect choices: every member not excluded. Trackers (docs/plans/trackers.md 3.1): the
open-world / hub pool stops being random, so UT need not restore it (hub_start_town and story
mode's choose_party stay); every character item is in the pool, which is the tracking fix the
user wanted. Story mode and AP off: untouched.

## Design (recommended)

1. **Bench store** (ap::SaveState): four entries of 0x130 bytes, places 8-11, empty = id 0;
   only in a bench seed (open world / hub, version 4). Sidecar `bench` + `bench_hash` as in 4.
2. **Party Order with the bench** (a variant page class used only with a bench seed; the
   original class untouched): the right column flips between the reserve (5-8) and the bench
   (9-12) with L1 / R1 (and Right from the reserve column), the front four always shown (mock-up
   scratchpad/bench/shots/mock_party_order.png, A). While the page is open it binds a 12-entry
   working copy (party_bind; draw_party_status reads party_slots() + index * 0x60,
   menu_widget.cpp:717, so indices 8-11 work) and writes it back on close before the
   destructor's voice / pairing / +0x41 steps (the pairing clear covers 8-11 too). The pick is
   kept as a place 0-11, so a pick survives the flip; the help bar names it.
   - Refused (sfx 7): the hero to the bench; a temporary member to the bench; a set-aside slot
     (negative id) with a bench place; any bench move during a PA (flags 2 / 4 / 746), a window
     that keeps everyone away, a borrowed slot (stash), the endings (flag 0x2f1), a hub story
     view; a bench member a window keeps away (Leon away, Dias away) into the 8; and, as today,
     a swap leaving no front member able to fight.
3. **Join path** as in 5 (bench after the 8; lend / return for windows; no-op story joins).
4. **D2 / D3 with the bench (heroslot's decisions):** "the lead" and "the slot-0 member" are the
   members the player left as leader (config +0x41's member) and in slot 0 at the last camp-menu
   close (at New Game: the starting member, with the hero in slot 1 per D1). A benched starting
   member is not the lead: whoever holds the leader's slot when the menu closes leads (the
   game's rule: +0x41 is a slot), and D2 keeps that member leading across scenes. A bench member
   swapped into slot 0 becomes the slot-0 member that D3 puts back after scene reorders. Bringing
   the starting member back from the bench does not make them lead by itself (Formation >
   Leader, or swapping them into the leader's slot, does). Scenes never move anyone to or from
   the bench, so D2 / D3 only ever act inside the 8.
5. **Small kindness (recommended):** when the game heals everyone (party_restore_hp_all /
   _mp_all: inns, scripts), heal the bench too.

Same-state rule: with nobody on the bench every path is today's (the 8 first; the median over 8
+ an empty bench; no sidecar key); AP off and story mode: no bench code runs.

## Decisions for the user (recommendation first)

**The user's answers (2026-10-26), all eight decided:**
- B1 Party Order: **three columns that scroll two at a time** (not the recommended flip).
- B2 Bench members in Equipment / Skills / Status / Specialty / Tactics: **yes** (+3-5 days).
- B3 **Everyone but the hero**, the starting member included; temporary members never.
- B4 A story join of a benched member: **they stay benched**, lent only for solo fights.
- B5 Endings: "endings will become a wip over time": the first version gives benched members
  none; counting them is later work.
- B6 Join level: **median over the 8 slots** (today's rule).
- B7 APWorld: **a `bench` option, on by default** (the user, 2026-10-26).
- B8 Inns heal the bench: **no**.
Not scheduled or built yet; build it together with heroslot (D1-D4 decided, docs/handoff/heroslot.md).

- B1 Party Order layout: **right column flips Reserve / Bench** (L1 / R1; front four always on
  screen) / three narrow columns, all 12 at once without HP / MP gauges (mock-up B) / three
  columns scrolling two at a time (the Options page's scroll).
- B2 Bench members in other camp pages (Equipment, Skills, Status, Specialty, Tactics): **no,
  swap them in first** (every page reads the 8 slots) / yes (the member list and each page see
  12: several days more).
- B3 Who can be benched: **everyone but the hero** (PAs, the save title, the opening need them),
  the starting member included; temporary members never.
- B4 A story join of a benched member: **stays benched** (the player's choice wins; a fight the
  member makes alone lends them for that window) / the story brings them in.
- B5 Endings: **benched members get none** for the first version (as members set aside) / they
  count at the endings (ext 0xb1 yes for them during map 581's selection, their sprite packs:
  up to 10 non-heroes at once was never possible on the console, VRAM to verify; +1-2 days).
- B6 Join level: **median over all 12 places** / the 8 slots only (today's rule).
- B7 APWorld: **always on** for open world and hub from the next apworld / a `bench` option
  (default on) keeping 0.2.0's 8 for those who want it.
- B8 Inns heal the bench: **yes** / no (strictly out of the party).

## Verification plan

- Unit tests (engine/tests/test_ap.cpp): sidecar round trip of `bench` (absent = empty, bad
  place / size refused), `bench_hash` refusal (edited bench, a bench copied to another save),
  join to the bench (full 8 -> bench, the game's init, the median, one banner), receive_move
  into a bench record, killer scan over the bench, story join of a benched member (no-op; lent
  and returned in a window), the swap rules (hero, temporary, negative id, locks, no fighter).
- Same-state proofs (leader.md style): a bench seed with <= 8 members vs the same seed with the
  bench forced off (a debug switch): New Game to free play, a Party Order swap among 0-7, joins
  -> byte-identical dumps and rng; story seed / no seed: byte-identical to today.
- Smoke (tools/smoke_field_events.sh): ap_bench_join (9th received -> bench), ap_bench_swap
  (bench <-> slot 0 and back: the 0x130 bytes identical but the pairing bytes; member_cmp.py),
  ap_bench_save (save / load: the bench back, card == a bench-less save's), ap_bench_guard
  (edited bench refused), ap_bench_pa (a PA with a benched member: no PA scene, the bench
  locked, intact after), ap_bench_battle (debug battle after a swap-in: the member fights,
  EXP to the front four), ap_bench_window (tournament: benched Dias lent and back),
  ap_bench_story (125's Celine restore with Celine benched: stays benched).
- Captures (--capture / SS_VRAM_PNG): Party Order reserve view, bench view, a pick across the
  flip, a refused pick (the hero), the locked bench during a PA.
- APWorld: world tests, both batches with check.py 0 errors, identity: story seeds unchanged,
  open-world / hub seeds: pool = every allowed member, version 4.

## Size

Port: bench store + sidecar + guard ~1 day; Party Order page (flip, working copy, rules,
captures) ~2-3 days; join path, lookups, lend / return, locks ~1.5-2 days; tests, proofs, smoke
~2-3 days: **~7-9 days** (B5 endings +1-2; B2 yes +3-5). APWorld (PARTY_SIZE, choose_open_party,
version 4, slot data, tests, batches) **~1-1.5 days**. Docs (seed_format.md Party / sidecar /
version rule, apworld.md, player guide, engine/README, catalog) **~0.5-1 day**. heroslot D1-D3
stay ~3 days on their own (the bench adds only the menu-close memory both need).

With the user's answers (B1 three scrolling columns, B2 yes, B5 none for now): port ~10-14 days
(the scrolling three-column page may cost a little more than the flip), APWorld ~1-1.5, docs
~0.5-1; with heroslot, roughly 2.5-3.5 weeks in all.

## Scratch (scratchpad/bench/)

run.py (heroslot's, plus NEWCARD=path for a card used in place), party.py, member_cmp.py,
mock.py; seeds/ow_claude.apso2 (open world, character items); cards/eight.mcd (Claude, Rena,
Celine, Dias, Precis, Ashton, Leon, Opera; AP sidecar beside it) and cards/other.mcd (Claude,
Bowman, Ernest, Noel, Chisato); runs/ (swap07 / ctl07 dumps, ninth.log); shots/ (po8c_sheet.png
today's page, mock_party_order.png the layouts).

## How to reproduce

    cd scratchpad/bench
    # the swap proof: load the 8-member card, Party Order, swap slot 0 with slot 7, dump
    T=200; IN="$T:triangle,$((T+6)):none"; f=$((T+150)); for i in 1 2 3 4 5 6; do IN="$IN,$f:down,$((f+6)):none"; f=$((f+20)); done
    IN="$IN,$((T+300)):cross,$((T+306)):none,$((T+400)):down,$((T+406)):none,$((T+450)):cross,$((T+456)):none,$((T+600)):cross,$((T+606)):none,$((T+640)):right,$((T+646)):none,$((T+660)):down,$((T+666)):none,$((T+680)):down,$((T+686)):none,$((T+700)):down,$((T+706)):none,$((T+740)):cross,$((T+746)):none,$((T+800)):circle,$((T+806)):none,$((T+860)):circle,$((T+866)):none,$((T+920)):circle,$((T+926)):none"
    python3 run.py swap07 CARD=cards/eight.mcd SS_LOAD_SAVE=0 "SS_STATE_DUMP=150:runs/swap07_before.state,1400:runs/swap07_after.state" "ARGS=--frames 1500 --input $IN"
    python3 member_cmp.py runs/swap07_before.state runs/swap07_after.state
    # (ctl07: the same with circle instead of the second cross at T+740)
