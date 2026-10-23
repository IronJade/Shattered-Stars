# Regions: continuation log

Phase 1 (scoping, 2026-10-23 morning) is summarised at the end; its report and the user's
decisions are `docs/notes/regions.md`. Phase 2 (this log's top part): the region profile layer,
Europe playable, France / Germany groundwork. Agent `regions-eu` (started 12:05).

## Rules given (phase 2, all mandatory; agent 2 from 12:30)

- NA discs must play exactly as before (every suite, ref_regress quick, smoke green on NA).
  Europe runs NA's game logic at 60 Hz with its own text and data (user's decision): the PAL
  6/5 scaling and the projectile-clash removal are documented, not ported; European data
  changed for 50 Hz (script waits, movie rates) must be handled so the game plays at NA pacing.
- Saves native per region: BASCES-02159 Europe, BESCES-02160 France, BESCES-02161 Germany.
- One Archipelago seed for NA / Europe / France / Germany (313 chest words match; verify on EU).
- Mods apply to every region; EXPECTED guards skip lines whose data differs.
- Japan recognised but refused with a clear message.
- The text agent (`docs/handoff/regions_text.md`) does FR / DE text: align with its design,
  don't edit its files.
- No game code, text or assets in git: region data in git is addresses, offsets, sizes,
  hashes, serials only. Disc images read-only.
- Headless only, no audible audio. Scratch XDG_CONFIG_HOME under scratchpad/regions/ (never
  ~/.config/shattered-stars); PCSX-Redux only -cli + scratch HOME.
- **Machine budget (new):** builds -j4; at most 4 game processes and 2 PCSX-Redux at once.
- **Every headless game run has a memory cap (~4 GB)** (coordinator, 12:45): `tools/memcap.py`
  (the sweeps, ref_regress and smoke use it), `prlimit --as=$((4<<30))` for one-off runs
  (`scratchpad/regions/work/run.sh` does; `MEMCAP_GB=`), ASan builds in a systemd scope with
  MemoryMax. A run that hits the cap is a failure, never a retry.
- Kill by exact PID; wait loops with deadlines; never `pgrep -f` own pattern; /tmp is RAM
  (scratch in scratchpad/regions/).
- Catalog: new functions via `flock scratchpad/catalog.lock` + scratchpad/fx_worker/cat3/apply3.py.
- Before calling a step done: ss_tests, ss_game_tests (NA), tools/smoke_field_events.sh (NA),
  `tools/ref_regress.py --quick --build engine/build-regions`, plus the region tests.
- Leave docs/modding/reference/map_names.csv and scratchpad/mapatlas/ alone.
- Don't commit or push. One line per finished step to scratchpad/checkpoint_notes.txt
  prefixed "regions: ".
- Keep this log current after each step and every 30 minutes. Past ~350k tokens of context:
  finish the step, update the log, end the turn.

## Incident (12:45): EU map 0 took 28 GB

The event sweep's map-0 run (entry K21 plays the ending, then the credits #3107) looped in
`credits::compose` over the European card script: the script is the release's own (Europe 65
cards / 193 name images, NA 78 / 256), so past its common start the NA addresses had no
counterpart, the reader returned 0 forever, and SS_REGION_READ_LOG recorded every new address
(28 GB). Fixed: `program_run` (program_image.h) reads data a release lays out its own way from
its translated start, in the release's order; the credits read the card script that way
(a read past it ends the script; a card with more than 64 codes stops the cards); the miss log
names 8 addresses per image then one "more" line; the read log stops at 1M entries. The
credits' cue list is the same length but its waits / fade count 50 Hz vblanks (NA 720, EU
600: all x5/6); `region::ntsc_vblanks` converts them (Profile::data_hz). Verified: EU credits
run to the end and show the European staff cards (scratchpad/regions/runs/cred1/*.png).

## Design (implemented)

- `engine/src/runtime/region.{h,cpp}` (in ss_core): the profiles (NA, Europe, France, Germany,
  Japan: serials, adjective, product code, text system, supported flag), `from_serial`,
  `select` / `current`, and the image maps: `entry_map`, `boot_map`, `module_map`,
  `module_identity`, `prepare` (hash check + pointer relocation), `translate`,
  `to_na_address`, `image_byte` (the one reader every chokepoint calls), `module_hash`,
  and `SS_REGION_READ_LOG` (path or folder/ -> "image na_address" lines).
- Generated data `engine/data/regions/eu.inc` (per image: segments {NA offset, size, delta}
  with evidence comments, pointer runs {NA offset, words, target image}, bases, sizes, FNV of
  both images, module hash) and `na_modules.inc` (NA enemy/spell module FNV -> normalised hash).
- Chokepoints routed: `program_data_at` (all programs, overlays, menu pages),
  `boot_exe_data` (prepare at boot_exe_open), `ovl1981_data` / `ovl1984_data` (loaders.cpp,
  prepare at load), `ovl1984b1_data` (spells.cpp), `enemy_module_u8` (enemy_modules.cpp:
  identity by normalised hash, map per slot), `spell_module_u8` (spellcast.cpp; spell_modules
  find by NA FNV from module_identity), `pc_module_u8` (pc_modules.cpp, map by entry).
- Launcher: `DiscInfo.region`, `kUnsupportedRelease`, `disc_number(path, &region)`,
  `describe` ("Disc 1, European release: OK"), `describe_pair` (both discs same release),
  region-aware `find_discs` / `find_disc2_near`. main.cpp selects the region from the boot
  disc's serial before boot_exe_open; a disc 2 of another release is dropped (reported).
- Saves: memcard.cpp save names from `region::current().product`; setup counts saves of
  every release's product code.
- Setup: release shown in the disc status lines, pair mismatch shown as an error on disc 2.

## Tool: tools/region_map.py

`tools/region_map.py --na NA1.cue --disc EU1.cue [--cache scratchpad/regions/cache]
[--report FILE] [--check]` -> engine/data/regions/<region>.inc + na_modules.inc. Docstring
explains the method (trap stripping, normalisation, GNU diff alignment + slide-down of
insertions in repeated runs, same-size changes mapped word for word, load address by relinked
jal votes, pointer words by "region word == mapped place of NA word", read coverage check
from tools/regions/na_reads.txt). Europe: 149 images, ~1357 segments, ~4336 pointer runs.

## Status (13:55, agent 2)

- Done (agent 1): region layer, chokepoints, launcher/setup recognition, save product code,
  Europe map. EU disc 1 boots to the title, map 18 renders as NA.
- Done (agent 2):
  - Memory caps everywhere (see rules); map-0 runaway fixed (Incident above).
  - `program_run` + credits card script as the release's run; credits cue waits / fade
    converted from 50 Hz (`region::ntsc_vblanks`, `Profile::data_hz`).
  - Script maps: `region_map.py` aligns map scripts laid out otherwise -> `kScripts` in the
    .inc (EU 7 maps, FR 24, DE 32); `region::script_offset` / `script_na_offset`; used by
    `ap.cpp` (doors, door routines, join sites: one seed for every release) and `mods.cpp`
    (script lines). Unit test `region_europe_script_offsets`.
  - France / Germany maps generated and compiled in (`fr.inc`, `de.inc`, 0 fatal); profiles
    stay `supported = false` until the text system is in (disc refused with the message).
    Disc tests `region_france_disc_images` / `region_germany_disc_images` (SS_DISC1_FR/_DE).
  - `tools/so2_data.py` knows the releases (serial, `region_tables` parses the .inc,
    `na_script_offset`, `image_offset`; shop table moved on PAL), `tools/so2_mod.py` writes NA
    word numbers from any supported disc (EU map 688 door -> the same line as NA's).
  - Script waits analysed: no 50 Hz rescaling in PAL scripts (only text ids / window sizes,
    and the rumble countdown 1800 -> 500); credits cues are the only 50 Hz data found so far.
  - Play time: EU `camp_menu_update_time` changed only the colon blink (/30 -> /25); clock
    and save minutes mean the same; save layout = NA's (only the product string moved).
  - docs/notes/regions.md section 10 (the implementation).
  - EU event sweep disc 1 (13:12, capped, read log): 827 maps, the same non-ok entries as
    NA's M8 baseline (map 0 / 826 K21 hang = the ending loop, 591-593 T9 stuck, 58 / 139
    gone); reads merged (6690) and `region_map.py --check` 0 fatal for EU / FR / DE.
  - `so2_mod.py` / `so2_data.py` for any supported disc (above); a game test refusing Japan
    (`game_refuses_the_japanese_release`, SS_DISC1_JP).
  - EU smoke (13:30): 73/86 with the stock script: the 12 world-map cases need a save and
    the test card is NA's (saves are per release); with an EU-renamed copy of it
    (`scratchpad/regions/work/card_region.py`, `work/smoke_eu.sh`) 85/86: the one left is
    hearn_talk's NA text offset (EU's string table is 2 bytes longer: +0xc8d, EU data). So a
    renamed NA save loads and plays on EU (same layout).
  - AP: a seed with map 688's 7 doors (`scratchpad/regions/ap/doors688.apso2`) loads on EU
    with no "not a map transfer" (translated); a wrong offset is still reported.
  - `SS_REGION_TRY=fr,de` (region::playable): development runs of France / Germany. France
    map 18 plays, Mr. Hearn's dialogue shows French text from the map font (screens in
    `scratchpad/regions/runs/frtalk/`); the wrap uses NA's classes (a line starts with " ?")
    until text_rules is wired. Europe's dialogue matches NA's (`runs/eutalk/`).
  - NA vs EU event sweep, disc 1, same binary (13:55; `work/evcmp.py sweep/na_d1
    sweep/eu_d1`): 5326 entries, 5162 identical in result and frame count, 164 the same
    result with a few frames more or less (talk entries: EU's text differs a little; the
    shared world script's K19 / K60 are EU's restructured sequence), 0 with another result.
  - Final suites (13:33, `runs/suites_final.txt`): ss_tests 98/0 (8 region tests incl. the
    EU / FR / DE disc tests), ss_game_tests 126/0 (EU run, Japan refusal), smoke 86/0,
    ref_regress quick 28/28.
- Agent 2 ended at ~13:55 (context budget). Nothing running.
- Known: battle #2129 image includes the overlay area 0x800715d0+ (stale #1984/#1981/enemy
  defaults); 71 jal votes against its load address come from calls into that area.
- FR / DE: reads whose bytes differ from NA (text agent's area): battle 0x8006d118-0x8006d11c
  (near the HUD), title 0x8004d37c, 0x8004e264.
- Don't rebuild engine/build-regions while a test runs its binary from there (copy it to
  `scratchpad/regions/bin/` for long runs, as the sweeps do).

## Next steps

1. Disc 2 sweeps (`ev_sweep.sh eu 2 eu_d2`, `na 2 na_d2`, then evcmp.py) — disc 1 is done.
2. Battle sweeps EU vs NA (same binary copy): `SS_BATTLE_UNITS=1200 python3 tools/battle_sweep.py
   --bin scratchpad/regions/bin/ss_bt --disc1 EU1 --disc2 EU2 --out scratchpad/regions/bsweep/eu
   --jobs 4 --frames 4000` (and NA), then `scratchpad/regions/work/bcmp.py NA_DIR EU_DIR`; the
   [bunits] lines carry the RNG hash, draws, positions and HP per frame, so any misread table
   shows; delete the logs afterwards (~150 MB each).
3. Wire the text agent's `game/common/text_rules.h` (its header lists every call site:
   text_layout_line, text_render_line, msg_window_print, text_ascii_to_glyph,
   text_format_number / text_number_width, title / battle text_from_ascii, the name keyboards,
   cvram_alloc, the Layout constants). NA must stay byte-identical (their NA layout test: 0 of
   3.3M layouts differ). Then FR / DE `supported = true` in region.cpp, the launcher test's
   expected "plays ..." message, and sweeps with SS_DISC1_FR / _DE.
4. README / wiki "Discs": Europe supported (saves per release, 60 Hz NA logic), France /
   Germany when wired, Japan refused. Release notes 0.02.00.
5. Optional: a setup action to import an NA save into a PAL card (rename + checksum, as
   `card_region.py` does; the layout is the same).

## How to verify

- Build: `cmake --build engine/build-regions -j4`.
- NA suites: `scratchpad/regions/work/suites.sh TAG` (ss_tests, ss_game_tests, smoke,
  ref_regress quick -> scratchpad/regions/runs/suites_TAG.txt; exports SS_DISC1_EU / _FR / _DE
  / _JP so the region disc tests run). 13:30: see `suites_final.txt`.
- Region tests: ss_tests `region_*` (profiles, map consistency EU / FR / DE, script offsets,
  module hashes, disc images EU / FR / DE); ss_game_tests `game_runs_the_european_release`,
  `game_refuses_the_japanese_release`, launcher release messages.
- Maps: `python3 tools/region_map.py --na NA1.cue --disc RG1.cue --cache scratchpad/regions/cache
  [--check] [--report F]` (writes engine/data/regions/<key>.inc; 0 fatal for EU / FR / DE).
- Region runs: `scratchpad/regions/work/run.sh eu|fr|de|jp NAME [ENV=..] -- --frames N ...`
  (capped at MEMCAP_GB=4; `SS_REGION_TRY=fr` to run France / Germany).
- Sweeps: `scratchpad/regions/work/ev_sweep.sh REGION DISC NAME [maps]` (read log on).

---

# Phase 1: regions survey (scoping agent, done 2026-10-23 ~12:00)

Measured how the Europe, France, Germany and Japan discs differ from NA; report
`docs/notes/regions.md`. Tools under `scratchpad/regions/` (git-ignored): `disc.py` (raw .bin
reader), `hash_entries.py`, `scan.py TAG` -> `pickles/<tag>.pickle` (unpacked code/data
entries), `cmpcode.py` (normalise + trap strip + diff alignment, classify per catalog row),
`cmpall.py TAG` -> `code_<tag>.txt/.json`, `mksyms.py` / `rdiff.py` (REA diffs, cached in
`asm/`, output `diffs/`), `data_cmp.py`, `scripts.py TAG` -> `scripts/<tag>.pickle`.
Open from the survey: read in full the remaining EU diffs (battle_main_loop, field_main_loop,
camera_step_eye, input_* updates, sprite_update, menu_scene_*, screen_set_display_window,
mem_* allocator, credits_vblank, *_on_close); FR/DE text functions; JP.
