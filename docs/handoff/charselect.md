# Handoff: New Game for Archipelago seeds: the story route and a character select

Queued 2026-10-25 by the main session (the machine was at three agents). No agent yet.

## The user's request (2026-10-25, verbatim)

> So, in archipelago, the Story Route choice, if it is locked to specific protagonist, should
> select and skip that screen on New Game setup screens. If Claude is hard selected in the seed,
> then Claude is selected with his default name, same for Rena. If "any" is selected, then the
> player gets to choose their Story Route.
>
> Also, would it be possible to CREATE a screen similar to the Claude/Rena select screen but with
> all 12 of the characters, in the same format where the user highlights a character and it shows
> their background info along with their portrait behind it? This would only be a screen seen in
> "Open World" games, where "any" is set for the Character Select option. Otherwise, reuse the
> same screen with just the selected character from the apso2 file.

## The coordinator's reading (tell the user where it differs)

1. **The story route** (every AP mode: story, open world, hub). With `options.story_route` claude
   or rena, New Game skips the hero select screen *and* the naming screen: that hero, with the
   default name. Today the port keeps the cursor on the seed's side and buzzes the other one
   (seed_format.md `story_route`). With `any`, the original screens are unchanged. With
   Archipelago off, or no seed, the original game is unchanged (the user's rule: memory
   "ap-off-is-natural").
2. **A character select** in the original's hero-select format (highlight a member: their
   portrait behind, their background text), in **both the open world and the hub** (confirmed by
   the user, 2026-10-25: "Both hub mode and open world, if Character Select is set to "any" in the
   yaml"):
   - `starting_character: any`: all 12 members to choose from (excluded ones, `exclude_characters`,
     shown but not selectable). It comes after the story route (picked or skipped). Choosing the
     story route's hero = nobody extra (today's `hero`).
   - `starting_character` a named member: the same screen with only that member (a confirmation /
     introduction), from the .apso2.
   - `starting_character: hero`: no extra screen.
3. **The APWorld** (so2.apworld 0.2.0, unreleased): `any` changes from "the generator picks one" to
   "the player picks at New Game". So the logic must not count on the starting member: every
   character item stays in the pool, and the chosen member's item, when it comes, is "already in the
   party", as both heroes are under `story_route: any`. The seed writes `starting_character: "any"`
   (or a list of the selectable members) instead of a name. Generator-random is still `random` in a
   YAML (Archipelago's own pick-one-of-the-values). Docs and option text follow.

## Research first (report before building)

**Leads (main session, 2026-10-25).** The user thinks the hero select's text is the camp menu's
status / bio text and asks whether that text would serve for the other members. What the port
already knows:
- Portraits for all 12 exist: the camp menu's Status page draws entry 0x1170 + character, a
  160 x 160 16-bit TIM streamed into VRAM (docs/notes/menu.md "Status").
- The hero select's text: engine/src/game/title/window.cpp `CharacterWindow` (ctor 0x80047690,
  draw 0x800476d0) draws title text group 4, ids selection * 7 + i + 0x20: seven lines each for
  Claude and Rena.
- The Status page as ported shows the name, class (group 0xb), HP / MP, LV, EXP, the eight
  abilities, the favourite food (once known), ATK / AC / HIT / AVD / MAG and talents (Triangle):
  no bio paragraph.

So check first: does title group 4 (or any other text, any release) hold lines for the other ten,
and is the heroes' text also anywhere in the camp menu? If only Claude and Rena have text, the
options for the user are: (1) facts the game holds (class, weapon, favourite food, talents, from
the disc, every release); (2) short texts of our own (never copied from a manual or guide;
English unless translated for FR / DE); (3) both.

- Reverse the original's hero-select screen (the New Game Claude / Rena choice): its code, its
  assets (the portrait images, the description text, the layout, the cursor and sounds), with REA;
  catalog it (docs/catalog).
- What the discs hold for the other ten members: portraits at a size that works behind the text
  (menu / status art, others), and any per-character description text (all four releases: NA, EU,
  FR, DE). No game assets in git: everything is read from the disc at run time.
- If there is no description text for the other ten on the disc: STOP and bring the user the
  options (for example stats the game holds such as race / age / weapon / talents, or short texts
  of our own; never text copied from a manual or guide). The user decides.

## Rules (as for the hub agents)
Headless only, own build dir engine/build-charselect, memcap 4 GB, at most 2 game processes,
scratch XDG_CONFIG_HOME, scratchpad/charselect/, no config-saving flags, no commits, log this file,
roll over at ~400k, stop and report on a refusal. Enhance, never degrade: without a seed (or with
Archipelago off) the New Game screens are the original's.

## Log (agent "charselect", started 2026-10-25)

Build dir engine/build-charselect (RelWithDebInfo, SS_BUILD_AP_NET / SS_BUILD_SETUP on, -j8).
Scratch: scratchpad/charselect/ (discread.py, titletext.py, portraits.py, textscan.py; PNGs
in png/, never committed).

### Done
- **Research (step 1), 2026-10-25.** Findings:
  - **The original screen** (already reversed and ported, catalog `confirmed`; checked again
    with REA on scratchpad/regions/elf/na1_title.elf): New Game sequence `new_game_sequence`
    0x80042cc8, the hero select its screen at LAB_80044568 (port `character_screen`,
    engine/src/game/title/new_game.cpp), the text window `CharacterWindow` ctor 0x80047690 /
    draw 0x800476d0 (seven lines, title strings 0x20 + selection * 7 + i, line 0 in palette 4,
    window at x 8 or 0x138 - w, y 0x78, 0xe8 x 0x72, shade frame). The portraits: #16 blocks
    0 / 1 (16-bit, 206 / 228 x 240) in VRAM (0x140 / 0x240, 0x100), drawn full height on the
    side opposite the window, cross-faded by a brightness ramp (+-0x20 a frame); the spotlit
    field sprites of both heroes are in the New Game art (#14 block 3, only Claude and Rena);
    heading "Hero select" and subtitle "Select your character." are art (#14 block 3), not
    text. Sounds: 1 move, 2 confirm, 6 back; the choice goes to config +0x45.
  - **Portraits for all 12** (every release): entries 0xbd4-0xbdf (0xbd3 + member, the renaming
    page's pictures; 16-bit, 240 high, widths Claude 206, Rena 228, Celine / Bowman / Dias /
    Ernest 208, Precis / Noel / Chisato 216, Ashton 232, Leon 192, Opera 224; png/na_names_sheet).
    **#16's two portraits are byte-identical to 0xbd4 / 0xbd5**, so the 12-member screen can
    show every member exactly as the hero select shows the heroes. Also: the Status page's
    160 x 160 TIMs (0x1171-0x117c, 0x10-byte header per sector) and Art's 320 x 1200 pictures
    (0x117d-0x1188). Member order = party ids (1 Claude, 2 Rena, 3 Celine, 4 Bowman, 5 Dias,
    6 Precis, 7 Ashton, 8 Leon, 9 Opera, 10 Ernest, 11 Noel, 12 Chisato).
  - **Text: only Claude and Rena have a profile, in every release.** Title strings (#14 block 0)
    hold ids 0x20-0x2d (the two bios) and nothing for the other ten in NA, EU, FR, DE and JP
    (same id set; FR / DE lack the keyboard's 0x71-0x73, DE has an extra 0x13). Whole-disc
    scans (NA discs 1 and 2, EU disc 1; every archive entry, raw and every SLZ block; title,
    field and ASCII encodings; scratchpad/charselect/scan_*.txt) and a glyph-shape decode of
    every menu page pack (0xba7-0xbd2, menu_0x*.txt): the bios occur only in #14; the camp
    menu has no bio text (the user's guess does not hold).
  - **Facts the game holds for all 12** (Status page pack 0xbb3, every release, translated):
    group 7 labels "Name" / "Race" / "Favorite Food"; group 0xa occupation (Swordsman,
    Wizard/Enchantress, Heraldic Wizard/Enchantress, Herbal Pharmacist, Swordsman, Mechanic,
    Swordsman, Professor, Heraldic Musketeer, Archaeologist, Zoologist, Newspaper Reporter);
    group 0xb race (Human, Expellian x6, Fellpool, Tetragene x2, Nedian x2; a 13th: Rena as
    Nedian after flag 0x2dc); group 0xc family names (Kenni ... Madison; also the renaming
    pack 0xbcf and title ids 100-101); the favourite food (Status overlay table 0x80081612:
    Steak, Shortcake, Baby Rabbit Risotto, Daikon Miso Soup, Chicken Skewers, Chocolate Crepes,
    Hamburger, Carrot Juice, Apple Cider, Hassaku Tea, Big Tuna, Fruit Sandwich). Talents are
    rolled per game (roll_talents tables), so not a fixed fact.


- **The story route skip (item 1), 2026-10-25 15:00.** engine/src/game/title/new_game.cpp:
  `new_game_sequence` reads `ap::required_story_route()` once (1 Claude, 2 Rena; 0 for `any`,
  no seed, Archipelago off); with a hero, Step::Character calls `skip_hero_screens` (config
  +0x45 = the hero, the name box's default name through `NameBoxWindow` + `name_box_commit`, the
  hero's prologue part 1 load 0xa78 / 0xa79 the naming screen would start; log `[ap] story route
  Claude (the seed's): New Game skips the hero select and the naming screen (Claude)`) and goes
  on to the combat mode; Circle there cancels that load and goes back to Vibration.
  `character_screen` is the original again (the old lock: cursor kept on the seed's side, the
  other side buzzing, is gone; it was unreachable). Smoke: `ap_hero_claude` / `ap_hero_rena`
  now check the skip (and that neither window opened), new `ap_hero_any` checks both screens;
  `hub_ng` no longer presses Left / Right. Docs: seed_format.md `story_route` row,
  engine/README.md, the player guide's `story_route` row, options.py StoryRoute docstring.
  - Proofs (headless, scratchpad/charselect/runs/): the first field frame's state (SS_STATE_DUMP:
    state block, config, party, records, inventory) is **byte-identical** between the original
    screens with the default name and the skip: Claude (no seed, Cross on Claude, Start on the
    name) vs a `claude` seed; Rena (no seed, Right, ...) vs a `rena` seed; and the Circle-back
    path (`rena` seed: combat mode -> Circle -> Vibration -> again) vs the plain skip. Window
    traces: with `claude` / `rena` no CharacterWindow (0x80047690) or keyboard (0x800478e8);
    with `any` and with no seed both open (Right gives map 18). Both heroes reach their maps
    (7 / 18) at vblank 3600. "Archipelago off" is the no-seed path (setup's switch drops the
    seed before the game starts; `required_story_route` returns 0 without one).
  - Suites: ss_tests 99/0, ss_game_tests 173/0 (172 + one from another agent), smoke 198 PASS /
    0 FAIL (scratchpad/charselect/smoke2.log). The first smoke run found one timing casualty:
    `ap_hub_fienal`'s debug battle at 3050 now came after the walk reached the Fienal door (the
    game starts ~550 vblanks earlier); moved to 2760 (note in docs/handoff/hubask.md).
- **Mock-ups** of the 12-member screen (real art, title font; scratch only):
  scratchpad/charselect/shots/mock_options.png (facts | our own text | both, Celine highlighted,
  Bowman and Opera excluded), the original for comparison shots/hero_select_pair.png.

- **The user's decisions (2026-10-25, through the coordinator):** text option 3 "Both" (the name;
  a facts line read from the disc, "Expellian. Heraldic Wizard/Enchantress"; "Favorite Food: ...";
  four lines of our own; Claude and Rena keep their original bios); our ten descriptions in one
  data file in the source, listed here for review; heading: the original "Hero select" art with
  "Select your character."; hubask has ended: make the ap.cpp / seed.cpp hunks myself (small;
  hubask's save guard and `hub_view` must keep working). The coordinator committed the story
  route skip with hubask's work; later edits go into the next commit.
- **Port hunks (2026-10-25 15:10, mine, marked [charselect]):**
  - seed.h `Seed::starting_choices` (party ids); seed.cpp: `options.starting_character` "any" =
    all 12 minus `options.exclude_characters`, or a list of names / ids (each must parse; "a list of
    characters' names expected"); a name / id / null / "hero" as before.
  - ap.h: `SaveState::chosen_start` (sidecar key `starting_character`, a party id, written when
    > 0, read back clamped 1-12); `seed_starting_character()`, `starting_choices()`,
    `set_chosen_start(id)`.
  - ap.cpp: `Runtime::chosen_start_pending`; `start_member` = the seed's named member, else the
    state's `chosen_start` (still 0 when it is the picked game's hero); `party_v2` also on with a
    pick; `on_new_game` moves the pending pick into the fresh state (only when the seed has
    choices; log `starting character Celine: picked at New Game`), so the existing path
    (give_start: joins at the first free play and leads) does the rest; sidecar write / read.
- **The character select (2026-10-25 15:30), engine/src/game/title/new_game.cpp** (namespace
  `charselect` + `member_select_screen`, before combat_mode_screen; data file
  engine/src/game/title/member_profiles.h):
  - Step::Member after the naming screen (or after the story route skip); shown when the seed has
    choices, or names a member who is not the hero (then only that member, to confirm). Circle
    goes back to Name (or to Vibration after a skip, cancelling the prologue load); Circle on the
    combat mode goes back to Member when it was shown. With choices the pick goes to
    `ap::set_chosen_start` (the hero: 0, nobody extra).
  - Layout: heading + subtitle sprites as the hero select; 12 faces (the Status TIMs 0x1171-0x117c
    box-filtered to 28 x 28 on the host, uploaded to VRAM (0x240, 0), 15-bit tpage 0x109) in two
    rows of six at (0x0c + col * 0x24, 0x2c + row * 0x22), lit 0x80 under the cursor with a frame
    tile (cyan; blue-grey when not selectable), 0x58 selectable, 0x20 not; drawn in the window's
    context (in s.main the pictures covered them whatever the queue order: not understood; the
    window context is drawn after s.main). The picture (0xbd3 + member) right-aligned at
    0x138 - width, two VRAM slots (the hero select's (0x140 / 0x240, 0x100)) cross-fading like
    the hero select; Claude / Rena re-uploaded into the slots on leaving (the hero select and the
    naming screen draw them from there). Window 8, 0x78, 0xe8 x 0x72, shade frame.
  - Text: heroes: title strings 0x20 + (m-1) * 7 (as the original). Others: line 0 the name
    (member_profiles.h, palette 4); the race (pack 0xbb3 group 0xb) + ". " + occupation (group
    0xa), split over two lines when wider than 0xd8 px; group 7 message 2 ("Favorite Food") + ": "
    + the item name (item pack 3000 block 0, group 3, item from the Status overlay table
    0x80081612 via `program_data_at(0xbb2, 0x8007e000, ...)`); then member_profiles.h's lines up
    to 7 in all. Disc text is drawn with its own pack's font by swapping `title_text().font`
    (same format, height 12). A line wider than the window logs once.
  - Controls: d-pad over the grid (no wrap); Cross / Start picks (sfx 2) or refuses a member not
    allowed (sfx 7, log `[ap] character select: this seed's party has no place for that member`);
    Circle backs out (sfx 6); log `[ap] character select: Celine Jules`.
  - Checked headless (scratchpad/charselect/ow_any.apso2: open world, Claude, "any", Bowman and
    Opera excluded): the skip, the screen, Opera refused, Celine picked, `starting character
    Celine: picked at New Game`, `joins at the first free play and leads`, "Celine leads".
    Captures shots/cs_sheet.png (Claude, Rena, Leon, Opera refused, Celine) and cs_sheet3.png
    (faces over the pictures). Suites after it: ss_tests 99/0, ss_game_tests 173/0 (smoke not
    run since).
- **The ten descriptions** (engine/src/game/title/member_profiles.h; ours, for the user's review):
  - Celine: "A heraldic sorceress and treasure / hunter from Cross. Proud and quick / of tongue,
    she loves fine things. / No old ruin escapes her notice." (charselect2: was "... fine things /
    and unexplored ruins alike."; FR / DE split her race and occupation over two lines and drop
    the last line, so it is now a sentence of its own)
  - Bowman: "A pharmacist from Linga who mixes / his own medicines and fights with / his fists.
    Easygoing, fond of his / pipe, and devoted to his wife."
  - Dias: "A swordsman of rare skill who has / wandered alone since bandits took / his family. He
    says little, trusts / few, and never lowers his guard."
  - Precis: "A cheerful young inventor from / Linga who builds her own robots. / Tireless and
    talkative, she is / never without a gadget in hand."
  - Ashton: "A swordsman who fought a two- / headed dragon and was left with / its heads on his
    back. Gloomy / about his luck, kind at heart."
  - Leon: "A Fellpool prodigy, already a / heraldry scientist at Lacour. / Brilliant and proud, he
    hides a / lonely heart behind sharp words."
  - Opera: "A Tetragene who left her world to / find the man she loves. Bold and / warm, she
    handles her heraldic gun / as surely as she trusts her heart."
  - Ernest: "A Tetragene archaeologist who / travels the stars in search of / lost civilizations.
    Calm, aloof, / and deadly with his whip."
  - Noel: "A Nedian zoologist who looks after / a nature reserve. Gentle and a / little dreamy, he
    understands / animals better than people."
  - Chisato: "A Nedian newspaper reporter always / on the trail of the next scoop. / Energetic and
    nosy, she never lets / a good story get away."

### In progress
- Nothing running. **Ended at ~390k tokens (rollover); a fresh agent continues from "Next steps".**

### Next steps
1. Port tests: unit tests (engine/tests, next to `ap_v2_starting_character`) for seed parsing
   ("any" minus exclude_characters, a list, a bad name), the sidecar round trip of
   `starting_character`, and `start_member` with a pick (and with the hero picked: 0). Smoke cases
   (tools/smoke_field_events.sh, after `ap_hero_*`): an open-world "any" seed (pick a member:
   `picked at New Game`, `joins at the first free play and leads`; a refused member), a named
   seed (`ow_ashton.apso2`: the confirmation screen, Cross), a hero seed / no seed (no screen:
   trace 0x800489dc right after; the dumps proof as for the skip), Circle paths (Member -> Vibration
   after a skip; CombatMode -> Member). Then ss_tests / ss_game_tests / smoke with counts.
2. Check FR / DE (SS_DISC1 = the French / German cue, docs/handoff/regions.md): the facts line
   splits when too wide; captures. Check the hub seed path (`hub_ng` with "any").
3. The APWorld (item 3 of the spec), apworld/so2/world.py `choose_start` / `plan_party` /
   slot data (line ~346 `"starting_character": p.start`): `any` = the player picks at New Game:
   no random pick, `start` None for the logic, every allowed member stays in the pool; the seed
   writes `starting_character` as the list of selectable members (heroes + pool, so the party
   stays within 8; excluded never). options.py StartingCharacter docstring ("any: you pick at New
   Game among the members this world allows"). Tests (apworld/so2/test), the hub batch and the
   open-world batch with scratchpad/apworld2/batch/check.py at 0 errors; the identity check
   (docs/handoff/apworld2.md "How to verify"): story seeds unchanged; open-world / hub seeds with
   "any" change (no random start, the pool keeps the member who used to start, the list in the
   seed). The version rule: a list / "any" is a v2 field (seed_format.md).
4. Docs: seed_format.md (`starting_character` "any" / list, the character select, the sidecar key
   `starting_character`), docs/ap/apworld.md, the player guide (en_Star Ocean The Second
   Story.md: the option row and a New Game paragraph), engine/README.md one line.
5. Report to the coordinator (no commits).

## Log (agent "charselect2", started 2026-10-25, continues "charselect" from "Next steps")

Same rules, build dir and scratch. Coordinator's extra items: the face grid over the portrait (a
polish), and a short look at the draw order charselect could not explain.

### Done
- **The draw order, 2026-10-25.** Not a port bug. An ordering table entry is a LIFO list
  (`psyq::addPrim` puts the primitive at the head, as PsyQ's does), and the title's contexts have
  one entry each, so in one context what is queued last is drawn first (that is why the backdrop
  is queued last). Experiment (scratch switch, removed): the faces queued in s.main *after* the
  pictures were covered by them; queued *before* them they drew on top (shots/exp_sheet.png). So
  "whatever the queue order" was a slip; nothing for the coordinator.
- **The face grid (polish), 2026-10-25.** Before: 6 x 2 faces of 28 px over x 0x0c-0xdc, the
  right-hand ones on the picture's face (the "red X" in cs_sheet3 is Claude's headband and skin
  through the gaps between four faces). After: 4 x 3 faces of 22 px at (0x0e + col * 0x1c,
  0x2e + row * 0x19), x 0x0c-0x78, i.e. on the side away from the picture where the hero select
  keeps its spotlit sprites, so every picture's face is clear (the widest, Ashton 232 / Rena 228,
  start at x 80 / 84 with the face from ~150); on a `FacePanel` (a Window whose draw is the bio
  window's own `window_draw_shade_frame`, its context drawn between s.main and the bio window's),
  so the faces read over a picture's edge (Ashton's dragon). A named member: their face alone in
  the first place, the panel sized to it. Cursor: Left / Right in a row, Up / Down between rows,
  no wrap. Captures: shots/grid_before_after.png (top before, bottom after: Claude, Bowman),
  grid_after_a/b/c.png (Claude, Bowman, Leon, Chisato, Opera refused, Ashton, Rena, the Ashton
  seed's one face).
- **Unit test** `ap_v2_starting_character_choice` (engine/tests/test_ap.cpp): "any" minus
  exclude_characters, a list (names, ids, duplicates once), a bad name, the pick into
  the state and the free-play join, the sidecar round trip (absent / 13: none), the hero (0 or its
  id: nobody extra), one New Game only, a seed without choices ignoring a pick.
- **Screen polish:** a log line when the select opens (`[ap] character select opens: 10 of 12 to
  pick` / `Ashton Anchors to confirm`, for the smoke); back from the combat mode the cursor
  starts on the pick (was: on the hero); New Game clears a pick left from a sequence backed out
  to the title.
- **Smoke cases** (tools/smoke_field_events.sh, after `ap_hero_*`): `ap_cs_any` (Opera refused,
  Celine picked, joins and leads, map 7), `ap_cs_named` (Ashton to confirm), `ap_cs_hero` (no
  select: the combat mode at vblank 1870, map 7), `ap_cs_back` (Circle: select -> Vibration ->
  select -> Celine -> combat mode -> Circle -> select on Celine -> Cross; two skips, two picks,
  map 7); no select in `ap_hero_*`; hub: `ap_hub_ng_cs` (`hubany` seed, "any" minus Bowman / Opera:
  Celine joins at the hub's first free play and leads). `hub_ng` takes `HUB_INPUT`.
- **Suites (2026-10-25 15:35):** ss_tests 99/0, ss_game_tests 174/0, smoke **220 PASS / 0 FAIL**
  (198 + 22 new; scratchpad/charselect/smoke3.log).
- **FR / DE (2026-10-25):** all 12 captured (shots/fr_all.png, ge_all.png): heading and subtitle
  art in each language ("Héros" / "Sélectionnez un héros.", "Held" / "Spielfigur wählen"), the
  disc's race / occupation / food in French / German, the heroes' own bios. Only Celine's facts
  split (FR "Magicien Héraldique/Enchanteresse", DE "Heraldischer Zauberer/Zauberin"), which drops
  her description's last line: reworded so that it is a sentence of its own (member_profiles.h
  comment says so). No line too wide in either.
- **The APWorld (item 3):** world.py `Party.choices`; `choose_start` returns None for "any" (no
  random pick); `plan_party` sets choices = heroes + pool (open world / hub), or every member not
  excluded (story party rules); slot data `starting_character` = the list; `seed_version` v2 with
  choices. options.py StartingCharacter docstring. Tests: TestPartyOpenAnyStart rewritten (start
  None, pool 6, choices, the pick is "item"), new TestPartyAnyStartWithoutItems, TestHubAnyLean
  `test_pick_at_new_game`; check_party_seed's seed check takes the list. World tests 318 passed,
  AP general 379 passed.
- **Docs:** seed_format.md (sidecar `starting_character`, the options row, "The character select"
  under Party v2, the version rule, the read line); apworld.md (Party v2's starting_character,
  the pool, slot data, the options table, Changes, Testing results); the player guide (the
  bullet and the row); engine/README.md one line.
- **Batches (2026-10-25 15:40-15:44):** players -> scratchpad/apworld2/batch/full_cs, players_hub
  -> hub_cs, seeds 1-96: 96/96 exit 0 each; check.py 0 errors on 192 + 192 files (check.py now
  reads the list: `take_choices`; backup check_pre_cs.py). Identity (cs_identity.py, vs
  full_hubask / hub_ask): every seed without an `any` slot byte-identical (142 / 138 files, the
  story ones among them); the 25 / 30 `any` slots and their multiworld partners differ: no random
  start, the old start in the new list in all, the pool one member larger where the party rules
  are open (10 / 29; the old start in it), versions unchanged. (hubask2 edited hub.py at 15:48,
  after both batches.)
- **More port checks:** a list seed with story_route any (scratchpad/charselect/ow_list.apso2):
  hero select -> name -> select (4 of 12) -> Circle back to the naming screen -> name -> select ->
  Precis refused -> Rena picked: she joins Claude's game and leads, map 7. No-seed New Game:
  the first field frame's state dumps (Claude, Rena) byte-identical to charselect's from before
  the select existed (runs/dump_none*.bin vs *2.bin). A -Wshadow in my seed.cpp hunk fixed.
- **Notes left** in hubproof.md and hubask.md (for hubask2, who is editing seed.* / hub.*): the
  batches to compare against, check.py's list, `HUB_INPUT`.
- **Final suites (2026-10-25 ~16:20, the shared tree with hubask2's work in progress):** ss_tests
  99/0, ss_game_tests 174/0, smoke **230 PASS / 0 FAIL** (scratchpad/charselect/smoke4.log, a
  frozen copy of the build: 220 + hubask2's 10 new `ap_hub_ask_visit*` / exit checks; my 22 all
  pass). APWorld: world tests 318 passed, AP general 379 passed, both batches 0 errors.

### In progress (charselect2)
- Nothing. Final report sent to the coordinator. Nothing committed.

### Open
- The user's review of the ten descriptions (member_profiles.h; Celine's last line reworded).
- Our four lines stay English in FR / DE (the user's choice); translations would go in
  member_profiles.h per release.
- The port reads a list / "any" in any world mode (as it reads a named start); the APWorld writes
  one only for open world and hub.

### How to verify
- The character select: scratchpad/charselect/run.sh with ow_any.apso2 (or ow_ashton.apso2) and
  `--input "$NGA,2000:right,..."` (NGA = the smoke's NEWGAME_A), `--capture DIR --capture-frames
  1990,...`; the screen opens ~1880 after a story route skip.
- `SS_DISC1=<cue> engine/build-charselect/ss_tests`, `engine/build-charselect/ss_game_tests`,
  `tools/smoke_field_events.sh engine/build-charselect/shattered-stars` (ap_hero_claude / _rena /
  _any, ap_hub_ng_*).
- The state proof: scratchpad/charselect/run.sh with SS_STATE_DUMP="1:PATH" and the smoke's
  NEWGAME_A / NEWGAME_B inputs (with 2000:right for Rena without a seed); `cmp` the dumps.

## For charselect (from hubask, 2026-10-25 ~16:50)
Thanks for the smoke notes (hub_ng's inputs, ap_hub_fienal 2760): my eight ap_hub_ask_* cases pass
with them (full smoke 198/0 on engine/build-hubask). On your item 2: hubask has finished its
assignment, so please make those ap.cpp / ap.h / seed.cpp / seed.h hunks yourself (the coordinator
assigns ownership from here). My hunks there, to keep: seed.h `HubAsk` + `HubBoss::ask`; seed.cpp
`parse_target`, `parse_ask` and its call in `parse_bosses`; ap.h `transfer_again`, `glyph_text`,
`glyph_text_width`, `SaveState::hub_view`; ap.cpp `on_map_transfer` (restructured: the door table,
then `hub_ask_transfer`), `transfer_again`, the first line of `script_text`, `hub_script_loaded` in
`on_script_loaded`, the sidecar's `hub_view` (sidecar_json / sidecar_from_json, on_saved,
on_loaded), `glyph_text*`. A sidecar field for your starting character can sit beside `hub_view`
(optional, absent = none; the reader takes missing keys).

## For charselect (from hubask2, 2026-10-25 15:52)
Small hunks in files you also edit (all marked [hubask2 agent]; nothing of yours touched):
- engine/src/game/ap/seed.h: one field in `HubAsk` (`once_per_visit`, the row's `ask.again`).
- engine/src/game/ap/seed.cpp: parse_ask's comment and 4 lines reading `ask.again` ("visit" /
  "always").
- tools/smoke_field_events.sh (hub block): the seed's `ask` rows get `"again": "visit"`;
  ap_hub_ask_exit's last step is `304:T4` (the door is quiet after the Clerk's way out) with two
  changed checks; new cases ap_hub_ask_visit and ap_hub_ask_visit_door after ap_hub_ask_exit.
- apworld/so2/test/test_hub.py: one line in TestHubAsk (`again` == "visit").
- apworld/so2/docs/en_Star Ocean The Second Story.md: the "Shigeo and Marsilio ask first" bullets
  (once per visit).

## Coordinator (2026-10-25): charselect2 done; commit pending
Finished and verified (charselect2's report: ss_tests 99/0, ss_game_tests 174/0, smoke 230/0; world
tests 318, AP general 379; full_cs / hub_cs 96/96 with check.py 0 errors; identity as described).
Not committed yet: hubask2 is editing the same files (seed.*, hub.*, hub.py, test_hub.py, the smoke
script); both go in one commit once hubask2 reports. The user still has to review the ten
descriptions in engine/src/game/title/member_profiles.h.
