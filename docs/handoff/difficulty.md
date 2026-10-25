# Handoff: the difficulty as a YAML option (Earth / Galaxy / Universe)

Queued and started 2026-10-25 by the main session.

## The user's request (verbatim)
> The last option that I want to add to the archipelago is the game difficulty options. I believe
> they are Earth, Galaxy, Universe. These are normally unlocked via the voice memo completion%, but
> we need it as an available option in the yaml, and on new game, instead of selecting the
> difficulty, whatever the yaml dictates, the difficulty is set to. if "any" is selected in the
> yaml, the option menu appears as normal on New Game selection.

## The coordinator's reading
- APWorld (so2.apworld 0.2.0, unreleased): an option `difficulty`: earth (default) / galaxy /
  universe / any, in every world mode; the seed carries it (options.difficulty). No logic change
  (say if the game's difficulty changes anything the logic models, e.g. encounters or drops).
- Port: with earth / galaxy / universe, New Game sets that difficulty and skips the difficulty
  selection, whatever is unlocked. With `any`, the selection appears: "as normal", but a seed plays
  on its own fresh memory card (launcher::seed_memcard), where the original's unlock condition is
  never met, so with `any` all three are offered (the coordinator's reading; told to the user,
  who may override it). No seed / Archipelago off: the original, untouched (the user's rule).
- Research first (REA, report the findings in this log; no need to stop unless something is the
  user's call): how the original stores the difficulty, how New Game offers it, what unlocks
  Galaxy / Universe (the user thinks the voice memo / collection completion; check), where the
  choice is kept (config / game state / the save) and what it changes. Catalog it.
- Fits with the New Game flow the agents charselect / charselect2 changed
  (engine/src/game/title/new_game.cpp; the story route skip, the character select): the
  difficulty step must work with both (skipped hero select, the select's Circle-back paths).
- Tests: unit / smoke (each value: the difficulty set, the screen skipped; `any`: offered; no seed:
  the original), the APWorld tests and the batches (check.py), docs (seed_format.md, apworld.md,
  the player guide).

## Rules
Headless only; own build dir engine/build-difficulty (like engine/build-hubcore), -j8; memcap 4 GB
(tools/memcap.py / prlimit), at most 2 game processes; scratch XDG_CONFIG_HOME, no config-saving
flags; SDL_AUDIO_DRIVER=dummy; `--debug` runs with stdin from /dev/null; scratchpad/difficulty/;
no commits; this file as the log (Done / In progress / Next / How to verify; every 20 minutes);
suites with counts (baseline ss_tests 99/0, ss_game_tests 176/0, smoke 246/0 in the working tree;
world tests 318, AP general 379); stop and report on a refusal; roll over at ~400k. Enhance,
never degrade.

## Log (agent "difficulty", 2026-10-25)

### Research (REA on scratchpad/difficulty/{title,field,battle}.elf; candidates from scan4f.py)
- **The byte:** config (handoff 2) **+0x4f**: 0 Earth, 1 Galaxy, 2 Universe. Saved with the
  config block (card image 0x0000-0x019f, docs/notes/menu.md), so a save keeps its difficulty;
  Continue never asks.
- **Writers (title #1 only):** `new_game_sequence` 0x80042cc8: `sb zero` at **0x80046648** (the
  combat-mode screen's end: Earth) and `sb` of the window's selection at **0x80046ce8** (the
  Battle Rank screen). No other program writes it: every aligned lb / lbu / sb with
  displacement 0x4f in the boot EXE, title, field #3100, overlays #3101-#3109, battle #2129,
  #1981, #1984 blocks, menus #2980-#3022 was listed; the only other one (#3102 0x80082058) is a
  GPU primitive's code byte (0x3a).
- **Reader (one):** battle `enemy_read_stats` 0x80076298, `lbu` at **0x800766a0** via b_config
  0x8006e8d4: Galaxy ATK x1.5, HP x1.5; Universe ATK x5/3, HP x2, MP x2 (max 999), EVA +0x80,
  ACC +0x50, INT x4/3 (already catalogued; port battle/encounter.cpp). Nothing else reads it:
  not EXP, Fol, drops, drop rates, encounter rates, the field, the menus. **So no logic change:**
  the APWorld models items, regions, drop chances and encounter rates, none of which move.
- **The unlock:** `g_battle_rank_unlocked` **0x8004ec28** (title RAM, in the title image: back to
  0 when the title program reloads). xrefs: cleared at 0x8003fdf8 when the Voice Collection
  opens; read 0x80040228 and set 0x80040270 by `voice_collection` 0x8003fb78 once the cards are
  read and **more than half of the voices are heard** (the heard bits merged from every save on
  the cards into config +0x1a0; SFX 0x13); read at 0x80046154 (combat mode's confirm fades the
  music only when no Battle Rank screen follows) and 0x80046660 (the step to the Battle Rank
  screen). The user is right (voice collection completion), with a catch: it is not saved;
  the player must open the Voice Collection in the same power-on before New Game.
- **The screen:** `battle_rank_window_create` 0x80048cec (strings 0x96 + selection * 7);
  Circle goes back to the combat mode; the port's battle_rank_screen (title/new_game.cpp).

### Done (port, 2026-10-25 ~17:50)
- seed.h / seed.cpp: `Seed::difficulty` from options.difficulty ("earth" / "galaxy" / "universe" /
  "any" or 0-3; missing -1; anything else refuses the seed). ap.h / ap.cpp `seed_difficulty()`
  (-1 off / no seed). All hunks marked [difficulty agent].
- title/new_game.cpp: after the combat mode, 0-2 set config +0x4f and skip the Battle Rank screen
  (log `[ap] difficulty Universe (the seed's): New Game skips the Battle Rank screen`); 3 offers
  it unlocked or not (`... offers the Battle Rank screen`, then `[ap] difficulty: X picked`);
  -1 the original (`t.battle_rank_unlocked`). combat_mode_screen takes `rank_follows` (the music
  fade the original keys on 0x8004ec28). Works with the story route skip and the character
  select (the step is after the combat mode; Circle on the rank screen back to the combat mode).
- title/window.h / .cpp: BattleRankWindow's ctor traces "0x80048cec" (SS_TRACE), for the smoke.
- engine/tests/test_ap.cpp: TEST(ap_difficulty_seed).
- Proofs (scratchpad/difficulty/run.sh, runs/*.log; New Game, Claude's route, then `battle 1`
  from the debug console, SS_BATTLE_UNITS=1): enemy HP Earth 60 / Galaxy 90 / Universe 120;
  `any` + Right Right Cross: the screen at 2170, Universe, 120; no seed and a seed without the
  key: no screen, 60; all reach map 7 at 3600 with the same RNG.

### In progress
- Identity baseline: HEAD built from `git archive` in scratchpad/difficulty/head/build.
- APWorld option, docs, smoke cases.

### Done (APWorld and docs, ~18:35)
- apworld/so2/options.py `Difficulty` (earth 0 default / galaxy 1 / universe 2 / any 3; group
  "Battles and crafting"); world.py: slot_data / seed `options.difficulty` (current_key), and
  `seed_version` 2 for a non-earth value (story / open world; hub stays 3).
- apworld/so2/test/test_difficulty.py: 5 classes (default, galaxy, universe+open world, any, hub
  universe): slot data, version, seed check, and the same chosen locations as on Earth.
- check.py (scratchpad/apworld2/batch): version rule + `difficulty` kind + invalid value error.
- Docs: seed_format.md (version rule, options table, "### Difficulty"), apworld.md, the guide.
- Catalog: globals.csv (0x8004ec28 confirmed with xrefs; +0x4f in field config and b_config),
  functions.csv (new_game_sequence: the two writes).
- Suites so far: ss_tests 99/0, ss_game_tests 181/0, world tests 341 passed (318 + 23 new),
  AP general 379 passed. HEAD identity: no seed and a keyless seed give state dumps identical
  to a HEAD build (scratchpad/difficulty/head/build) at the first field frame and at 3600.

### Done (proofs, ~19:10). Final report sent; nothing committed.
- Smoke (frozen copy of engine/build-difficulty, scratchpad/difficulty/smoke1.log): **261 PASS /
  0 FAIL**, the 13 new ap_diff_* checks among them (plus the negative checks: no rank screen with
  a fixed value, no screen and no difficulty log in ap_diff_none / ap_hero_* / ap_cs_any).
- Batches (scratchpad/difficulty/batch): identity earth vs universe, seeds 1-16 and hub 1-8: all
  32 + 16 `.apso2` identical but for options.difficulty and the version (and skillsanity2's new
  option keys, added between the two generations); spoilers identical but for those lines.
  Mixed batches 32 + 16 seeds, check.py 0 errors on 64 + 32 files; 192 files in all, 0 errors.
- Extra runs (runs/): a hub seed (hubfc + universe): config +0x4f = 2 after the hub New Game,
  HP 120; open world + the character select's back paths + galaxy: set once, HP 90, Celine
  leads; `any` with Circle on the rank screen: offered twice, Galaxy picked, HP 90.
- Scratch cleaned (the HEAD baseline build and the frozen binary deleted; run.sh, seeds/,
  runs/, batch/ kept).

### Decisions for the user (reported)
- `any` offers all three whatever the Voice Collection unlocked (the coordinator's reading; the
  original's rule would never show it on a seed's own card).
- A non-earth difficulty makes a story / open-world seed version 2 (an older port would play it
  on Earth without a word). Recommended; dropping it is one line in world.py and check.py.
- Nothing enforced on Continue: a save keeps the difficulty New Game gave it.

## How to verify
- Unit: `SS_DISC1=<cue> engine/build-difficulty/ss_game_tests` (TEST ap_difficulty_seed),
  `ss_tests`.
- Smoke: `tools/smoke_field_events.sh engine/build-difficulty/shattered-stars` (ap_diff_*).
- One run: `scratchpad/difficulty/run.sh NAME seeds/d_universe.apso2 4000 "$NGA,$NGB" "3700:battle 1"`
  (NGA / NGB as the smoke's NEWGAME_A / NEWGAME_B); `grep -m1 bunits runs/NAME.log`: e0 / e1 HP
  60 Earth, 90 Galaxy, 120 Universe.
- APWorld: apworld2.md "How to verify" (world tests, AP general); `id_compare.py` and check.py on
  scratchpad/difficulty/batch/{id_*,mix,hub_mix} (run from scratchpad/ap/Archipelago with the venv).
- Research: `python3 scratchpad/difficulty/scan4f.py analysis/archive/disc1` (the config +0x4f
  candidates); REA xrefs of 0x8004ec28 in scratchpad/difficulty/title.elf.

## Coordinator (2026-10-25)
Accepted the three recommendations (any offers all three; non-earth -> version 2 for story / open
world; nothing enforced on Continue); told the user, who may override. Not committed yet: its
shared-file hunks (options.py, world.py, seed.*, ap.*, test_ap.cpp, the smoke script, seed_format.md,
apworld.md, the guide) sit beside skillsanity2's in-progress ones; it goes in with skillsanity2's
next verified piece.

## For you (from skillsanity2, 2026-10-25 ~20:30): shared APWorld files
- apworld/so2/options.py: `Skillsanity`, `SkillsanityShopShare` (before NoFailCrafting), two
  dataclass lines after killer_move_progression, the Locations option group. world.py:
  `seed_version` gained `or o.skillsanity.value != 0` after your difficulty line; fill_slot_data two
  keys after killer_move_progression. scratchpad/apworld2/batch/check.py: the v2 expression has
  the skillsanity term after yours, and a new check_skillsanity() (backup check_pre_sk.py).
