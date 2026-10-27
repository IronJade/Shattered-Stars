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

## Phase 2 (build): agent "hellseed2" from here (2026-10-27; hellseed passed its context budget)
Rules as above, plus: one other agent ("shingofix") works in engine/src/game/ap/ap.cpp's stash /
borrow code and party scenes on main: stay out of ap.cpp's party code. Build dir
engine/build-hellseed (deps from engine/build-hubleader/_deps), -j4.

### The user's words on the port (2026-10-27, verbatim)
"For the Hell seeds, we should have the setup archipelago seed read throw a warning that the seed
being loaded is a "Hell Seed" as well."

### The design (as built by hellseed, checked by hellseed2 against the WIP)
- **Hell seed** (apworld/so2/hell.py `is_hell_seed`): encounter_rate 0 and (killer_move_checks or
  skillsanity special_only / insanity, i.e. learn points). `trained(options)`: encounter_rate > 0
  or a hell seed (the party has battles to train in).
- **Rules on a hell seed** (everything else unchanged): killer-move levels (rules.killer_rule),
  learn points (skillsanity.learn_rule) and guild checks (skillsanity.guild_rule; the arena pays
  its enemies' Fol) count from "the Fun City arena is reachable" (`arena_need`): story mode the
  disc switch, open world stage 16 done + Fun City Pass, hub mode nothing; plus their usual member,
  move and character-item needs. They may hold progression as with battles (EXCLUDED / "maybe"
  members still none). Guild checks in open world: each place's way combined with the arena
  (`arena_way`: stage >= 16, + Fun City Pass); an Expel place's own early way dropped (Expel closes
  at the El voyage, before the arena opens). Story mode: every set from the disc switch. Hub: every
  set from the start (ok places only). Drops stay progression-free at encounter_rate 0.
- **Seed**: world.py adds `hell.FEE_PATCH` = `script 297 0x177f 0x120007d0 0x12000000` to
  `patches` and `hell_seed: true` to slot data / seed options, on hell seeds only (absent
  otherwise). No new seed version (phase 1 decision; party-bench keeps 4, a hell-specific port
  feature would take 5).
- WIP files (uncommitted at the handoff): hell.py (new), locations.py, hub.py, rules.py,
  skillsanity.py, world.py, test/bases.py (check_seed: patch line and `hell_seed` iff hell),
  test/test_v2.py (TestKillerNoEncounters is now a hell-seed test). hellseed2's check: matches the
  design above; no other `encounter_rate` reader needs it (drop_rule / hub drops stay as they are).

### Steps (hellseed2)
1. [x] World side (2026-10-27 ~11:40; committed, "Commits" below): test_skillsanity.py helpers hell-aware; test/test_hell.py; fill rates
   (fillrate3.py, 100 each, seed 75 + enc 0 and two neighbours); batches story_open / hub /
   solo_noexpel 96/96, checker 0 errors, identity vs scratchpad/skilllogic/batch; world tests,
   AP general, scoped. Commit (docs first).
   - (10:20) test_skillsanity.py: `hell` imported; may_hold / learn_need / the held assertion use
     hell.trained / arena_need; TestSkillsanityNoEncounters is now skills_only + enc 0 (not hell)
     + test_not_hell; new TestSkillsanityHellInsanity (story, insanity, enc 0: guilds from the
     switch). New test/test_hell.py: TestHellRule (truth table, the patch line), HellChecks mixin
     (rule, arena_need, what trained locations hold, seed data), TestHellSeed75 (+ Claude),
     TestHellOpenGuilds, TestHellStory, TestHellHub, TestNotHellLowEncounters(+Skills),
     TestHellMarks (8 mixes: patch line / hell_seed iff). test_hell + test_skillsanity + test_v2:
     all pass (t/hell3.log: test_hell 74 passed).
   - Fill rates (scratchpad/hellseed/fill/run.sh, killer0's fillrate3.py; summary.txt): seed 75 +
     enc 0, + story_route claude, + killer_move_progression any: **0/100 FillError each, all
     accessible and beatable**; 133 killer locations and 60 progression moves a seed.
   - tools/ap_batch_check.py: `hell_seed(o)` / `trained(o)` / `check_hell` (the patch line and
     options.hell_seed iff a hell seed, from the options); sk_may_hold and both killer branches use
     trained(o). On the old batches (scratchpad/skilllogic/batch): output identical (0 errors).
   - Batches (10:27-; scratchpad/hellseed/batch/run.sh -> sk_full, sk_hub, solo; identity.py OLD
     NEW: every .apso2 + spoiler by sha256): story_open and hub 96/96, rc 0, checker 0 errors
     (check output identical to skilllogic's), **byte-identical 96/96 each**; solo_noexpel the
     same: **all 288 seeds (768 files) byte-identical** (batch/identity.txt), 0 seeds with an SO2
     player at encounter_rate 0, checker output identical on all three.
   - Extra batch with encounter_rate 0 (batch/players_hell: solo_noexpel's YAML with
     encounter_rate 0, expel_progression and killer_move_checks random; seeds 1-64): **64/64
     generate, checker 0 errors** (55 hell seeds: 29 open, 14 story, 12 hub; 9 ordinary);
     progression at killer levels / learn levels / guild checks 166 / 488 / 306. Checker's
     check_hell refuses each made-up error (fee line missing / present, hell_seed missing / false).
   - Suites (suites/run.sh; -n 4, prlimit 8 GB): world 633 passed, general **379 passed** (117745),
     scoped 686, unittest 633 OK. Mutations (dbg/mutate.py, mutate.txt / mutate2.txt): each part
     caught but two: arena_way's stage floor (TestHellOpenGuilds has teleporter + expel_progression
     on, where the El voyage needs the keys up to the Synard anyway) -> new
     TestHellOpenGuildsNoTeleporter.test_arena_guilds catches it; "Expel early ways kept" is
     equivalent (with the teleporter the early way mapped to stage 16 equals the teleporter's way;
     without it Expel guilds are "expel keys", never the best class): kept as a safeguard.
     Final: world **634 passed** (44972), scoped **687** (50303), unittest 634 OK.
   - Reproducer: hell.is_hell_seed forced False -> TestHellSeed75 / Claude test_fill FillError
     ("13 more progression items than there are available locations"); restored (cmp).
   - Docs: apworld.md Results block + test table row; 0.2.1 notes' test sums.
   - test_hell.py + TestHellOpenGuildsNoTeleporter (an Expel guild's early way dropped: Knowledge 1
     needs the Central City Pass without the teleporter; with it, Cross counts through it).
   - Docs drafted while the batches ran (uncommitted): seed_format.md (`hell_seed` row, "The hell
     seed" section, the Patches example), apworld.md ("The hell seed" section, killer moves /
     skillsanity notes, 0.2.1 changelog bullet; results still to add), release_notes/0.2.1.md
     (new section, TO CONFIRM limitation removed), new release_notes/0.02.51.md (minimal draft),
     the player guide (en_*.md "Hell seeds", setup_en.md).
   - Port edits written (uncommitted, not yet built): seed.h `hell_seed` + `kHellSeedWarning`,
     seed.cpp reads it, setup_ui.{h,cpp} SeedInfo.hell + the kWarning status_line, main.cpp logs
     `[ap] <warning>` after ap::start, tests/test_ap_hell.cpp + CMakeLists line. NOT in ap.cpp
     (shingofix's uncommitted edits there; never stage ap.cpp / ap.h / tests/test_ap.cpp).
2. [ ] Port: seed.cpp reads `hell_seed`; setup's Archipelago page warns; the game logs the line;
   unit test; setup captures if headless. ss_tests, ss_game_tests, full smoke. Commit.
3. [ ] Docs: options.py docstrings, apworld.md, player guide, seed_format.md, 0.2.1 notes, 0.02.51.
4. [ ] Package dist/apworld-0.2.1/ (load alone, solo, seed 75, seed 75 + enc 0, ChecksFinder).

## Commits (hellseed2)
- Step 1: the docs commit (handoff, apworld.md, 0.2.1 notes, player guide), then the world commit
  (apworld/so2 code + tests, tools/ap_batch_check.py).

## Files touched
- docs/handoff/hellseed.md (this file).
- scratchpad/hellseed/ (m279 / m297 / m305 disassembly; port/: gen.py, run.sh, dump.py, grid.py,
  desk_input.txt, arena_input.txt, seeds/, runs/, dumps/, cards/ (copies of the late save)).
