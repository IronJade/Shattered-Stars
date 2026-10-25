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

### In progress
- Nothing: waiting for the user's choice of text for the character select (reported 15:45).

### Next steps
- Report the research and the skip to the coordinator; the user picks the text option (facts,
  our own short texts, or both).
- Then items 2 and 3: the character select (title code, a new screen), the chosen member into
  the AP runtime and the sidecar (a hook in ap.cpp: note to hubask), the APWorld change.

### How to verify
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
