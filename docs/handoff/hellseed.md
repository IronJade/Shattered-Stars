# Handoff: hellseed (encounter_rate 0 + training = a "hell seed": the Fun City arena, free)

Agent "hellseed", started 2026-10-27. Builds the hell-seed rule for so2.apworld 0.2.1 (not released)
and port 0.02.51 (ships with it). Replaces killer0's option (b) (docs/handoff/killer0.md).

## The user's words (2026-10-27, verbatim)
On whether the Fun City arena could train killer-move proficiency and levels when encounter_rate is
0: "I believe that there is a fee to use the training arena in Fun City, but if we can, I think a
better route would be to eliminate the fee to train there IF and ONLY IF those rules are set on the
seed. Something like that is usually called a "hell" seed, where the grind is intentionally made
preposterously hard."

## Rules given
- Phase 1 (verify) before Phase 2 (build); if a Phase 1 answer fails, stop and report.
- Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python, SKIP_REQUIREMENTS_UPDATE=1.
- Low load: pytest -n 4; one heavy job at a time; Python generation under prlimit --as 8 GB; builds
  -j4; game processes under 4 GB (prlimit). Headless only, scratch XDG; kill by exact PID.
- A port change only if needed, outside engine/src/game/ap/ap.{h,cpp} if possible ("queuebypass"
  edits them on main; its uncommitted files are not mine). Never touch branch party-bench or
  scratchpad/wt/party (read-only `git show party-bench:...` is fine).
- Commits on main after each verified piece, docs first, staged by path; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push, tag, publish.
- Scratch in scratchpad/hellseed/. A denied tool call: write the blocked step here and stop.
- Roll over at ~400k context.

## Phase 1 (verify)
1. **The fee** (map 297's script, NA disc; `tools/so2_script.py dis extracted/disc1/SO2.BIN 297`,
   scratchpad/hellseed/m297.dis / .str): a map-script fee, no native code.
   - The receptionist's talk 0x5c20 (actor 16; registered by ext 0xf3 at chapter != 57): "Hear an
     explanation. / Register." (string 156). Register: `push 0x7d0` at **0x5dfc** (word 0x177f,
     0x120007d0), `call 0x25a4` (the Bargaining adjustment: fee = 2000 + / - 2000 * 3 * A_3e / 100
     when flag 0x2df / 0x2e0, the super specialty 6 price flags of menu_shop.cpp), stored in L[0x10]
     and G[0x38] (the window's number). String 164 "An entry fee of N FOL is required to enter the
     Fighting Tournament. Is that all right?"; Enter / Forget it (165). 0x5e88: get_counter18 (Fol)
     < fee: "Sorry, but you don't have enough money." (166), end.
   - Then the game (168 / 169: Duel, Bullying, Team, Survival; ext 0xdc 4 at 0x5f20), the party
     flags 758-769, the mode routine (0x4fec / 0x5224 / 0x54a8 / 0x5b0c: member, rank, "Is this
     OK?"). Only once one returns (not -1): **0x60e8 `add_counter18(-fee)`** (the Fol taken), string
     171 "All right then, good luck!", flag 586 = 1, map_transfer_c(305, ...) at 0x6124.
   - So: **2000 Fol per registration, every game alike** (one fee for a Duel's 4 matches, a
     Bullying's or Team's 3, Survival's 50), taken at the desk after the game, member and rank are
     picked; Bargaining moves it by 3 % per level. The receptionist is the only registration (map
     305's Clerk at chapter 57 step 20 offers "Do intensive training" without a fee; the hub's
     "Face Marsilio." option is decided at runtime by the port, hub.cpp, and leaves the word alone).
   - **Waiver:** `push 0x7d0` -> `push 0` makes the fee 0 (0 +/- 0 under Bargaining), the Fol test
     never fails, `add_counter18(0)`; the window says "An entry fee of 0 FOL". One seed patch line:
     `script 297 0x177f 0x120007d0 0x12000000`. Seed patches are a version-1 feature (every port
     applies them, `mods_apply_script`, NA words relocated per release by region::script_offset;
     seed_format.md: 297's words alike on NA / Europe / France / Germany).
2. **encounter_rate in the port**: ap.cpp `random_encounters_off()` / `scale_encounter_distance()`
   are read only by field_program.cpp `field_update_encounters` (the field's and the world map's
   random-encounter distance). Arena fights are script battles: map 305's 0x4f48 calls ext 0xb7
   `battle(encounter, 0x16, 0)` (param 0: a loss is no game over), which never reads the rate.
3. **Killer-move uses and EXP accrue in arena fights; no arena flag skips them.**
   - By reading: the use count's only writer is battle `PartyUnit::begin_action` (party_states.cpp,
     0x800373b4: record + 0x7e + 2 * kind, +1 per move that starts, cap 999; killer_prof.md: no
     flag, game-state or story test). `record` = handoff 4 (the field's own character records,
     g.records) + slot * 0xd0. EXP and Fol: result.cpp `award_exp_money` sums the enemy units'
     EXP and Fol with no arena test. Map 305 sets no battle rule (no ext 0x72): a duel's fighter
     is player-controlled.
   - **Headless proof** (engine/build-skilllogic, HEAD's port; scratchpad/hellseed/port: run.sh,
     gen.py seeds, dump.py, desk_input.txt; runs/p8, p9, p10): the late save
     (scratchpad/latesave/card.mcd, Final Save Point, chapter 60; Ashton Lv 255, L1 Dragon Breath =
     move 6, learn level 64, 140 uses a level, count 10; the save maps confirm to Circle), debug
     `warp 297 -150 0 381 270`, then pad presses: talk, Register, Enter tournament, Duel, Ashton,
     rank E, Yes, then Circle (attack) and L1 (Dragon Breath) through the fights. Story seed
     (encounter_rate 0) with a killer_move location Ashton move 6 at 11 uses. Four fights
     (encounters 268-271: Lizardaxe, Vopalbunny, Armedknight, Alraune), all won; back in 304:
     **`[ap] killer move: Ashton move 6 used 11 times (level 1 at 11)`** and the check;
     SS_STATE_DUMP before / after: Ashton **EXP 82503362 -> 82503466 (+104** = 10 + 16 + 40 + 38,
     battles.json's EXP of the four), **Dragon Breath 10 -> 11** (AP off, p10: 10 -> 12). During
     the fights every other member is set aside (reserve slot ids), all back in 304.
   - (The user, 2026-10-27: Claude's Phase Gun is gone in hub / open-world runs by the chapter;
     it has no learn row and no check. The proof uses Ashton's Dragon Breath, a learn-table move.)
   - Harness notes: the event sweep's play mode erases every scripted pad entry after "now" on
     each field frame (event_sweep.cpp schedule_input), so a sweep can't be followed by --input;
     the proof drives the desk with --input only.
4. **Who enters, and Fol.**
   - Duel / Bullying / Survival: one member, picked from every member in the party (0x487c lists
     characters 0-11 that `party_has`; 0x4f84 refuses one "in no state to be able to fight").
     Seen: Claude, Rena, Celine, Precis, Ashton, Leon, Noel, Chisato offered. Team: five 1-on-1
     fights by members in turn ("one person cannot enter two times in a row"), needs 2 or more who
     can fight (string 170). The fighter fights with his own record (proof above).
   - **The arena pays Fol**: each fight pays its enemy's Fol like any battle (award_exp_money):
     rank E duel 18 + 25 + 25 + 34 = **102 Fol** (proof: +102 after the fee), and by battles.json
     (encounters 268-287 duel E-A, 288-305 bullying F-A, 306-311 team, 312-361 survival): Duel D
     847, C 7110, B 3920, A 8270; Bullying F 204, E 905, D 5232, C 12200, B 8250, A 10140; plus
     the prizes (Bullying: skill points and Fol, Prima). With the fee (2000) a rank E or D
     registration loses Fol; without it every registration gains.
5. **When the arena is open.**
   - The desk: talk registered at chapter != 57 (ext 0xf3). In chapter 57 step 20 (Marsilio's
     view) map 305's Clerk offers "Do intensive training" instead. Map 477 is Lacour Castle's
     coliseum (the Tournament of Arms, chapters 18-19 on Expel), not the Fun City arena.
   - Fun City's gate (map 279): before the N.F.I.D. (Mirage gives it, chapter 57) every entry
     needs a ticket, **75 Fol a person** (`push 0x4b` at 0xaba4, times ext_d5 the party size;
     flag 0x1fe), void once the party leaves. **Closed in chapter 58 only** ("For the sake of
     accident prevention, Fun City is currently closed": `G[0xa0] == 0x3a` at 0xa58c / 0xaa84).
   - Story mode: Fun City lies on an island the Synard reaches (data.TELEPORTER_STAGE 16, the
     Expel teleporter's own rule); the story reaches it with no item after the disc switch, so the
     disc switch stands for it (as it already does for killer moves and dear guild sets).
   - Open world: stage 16 done + the Fun City Pass (the Expel Teleporter event's rule; the old
     man is in the same building, map 303). Chapter 58's closing is passed by the story with no
     new key: stage 23's keys (Armlock, L'Aqua, map 687) are all needed by earlier stages.
   - Hub mode: open from the start (Fun City is a hub town; "Register." is the arena as it is,
     hubask). After the Final Save Point it is open in every mode (Virtual Expel's old man, 303).
   - The ticket is a pre-existing assumption of every Fun City rule (the Expel teleporter too):
     75 Fol a person from story fights, sales or chests; once inside, a free arena pays Fol.

**Phase 1 holds: no stop.**

## Fee proofs (no port change; the seed line)
- Late save, story seed, encounter_rate 0 (p8 / p9 / p10, the arena run above): without the line
  4800685 -> 4798685 at the desk (2000), 4798787 after (+102); **with
  `script 297 0x177f 0x120007d0 0x12000000`: "An entry fee of 0 FOL" (cap frame 770), 4800685
  kept, 4800787 after**; AP off (no seed): 2000 taken, as the disc.
- Hub New Game at Fun City (start Fol 9000, the sweep to the desk, battles stubbed; p11): 9000 ->
  7000 without the line, 9000 with it (the port's "Face Marsilio." option leaves the word alone).
- `--map 297` hub starts on NA, Europe, France, Germany (r_*): 7000 without, 9000 with, on each
  (German text seen: "Empfangsdame / Willkommen in der Kampfturnierarena").

## Files touched
- docs/handoff/hellseed.md (this file).
- scratchpad/hellseed/ (m279 / m297 / m305 disassembly; port/: gen.py, run.sh, dump.py, grid.py,
  desk_input.txt, arena_input.txt, seeds/, runs/, dumps/, cards/ (copies of the late save)).
