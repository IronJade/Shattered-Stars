# Regions: continuation log

Phase 1 (scoping, 2026-10-23 morning) is summarised at the end; its report and the user's
decisions are `docs/notes/regions.md`. Phase 2 (this log's top part): the region profile layer,
Europe playable, France / Germany groundwork. Agent `regions-eu` (started 12:05).

## Rules given (phase 2, all mandatory)

- NA discs must play exactly as before (every suite, ref_regress quick, smoke green on NA).
  Europe runs NA's game logic at 60 Hz with its own text and data (user's decision): the PAL
  6/5 scaling and the projectile-clash removal are documented, not ported; European data
  changed for 50 Hz (script waits, movie rates) must be handled so the game plays at NA pacing.
- Saves native per region: BASCES-02159 Europe, BESCES-02160 France, BESCES-02161 Germany.
- One Archipelago seed for NA / Europe / France / Germany (313 chest words match; verify on EU).
- Mods apply to every region; EXPECTED guards skip lines whose data differs.
- No game code, text or assets in git: region data in git is addresses, offsets, sizes,
  hashes, serials only. Disc images read-only.
- Headless only, no audible audio. Scratch XDG_CONFIG_HOME under scratchpad/regions/ (never
  ~/.config/shattered-stars); PCSX-Redux only -cli + scratch HOME, max 3.
- Kill by exact PID; wait loops with deadlines; never `pgrep -f` own pattern; /tmp is RAM
  (scratch in scratchpad/regions/); build in engine/build-regions with -j8; max 6 game
  processes (prefer 4: two map-naming agents render too).
- Catalog: new functions (PAL- and text-specific) into docs/catalog/functions.csv under their
  region modules, via `flock scratchpad/catalog.lock` + scratchpad/fx_worker/cat3/apply3.py.
- Before calling a step done: ss_tests, ss_game_tests (NA), tools/smoke_field_events.sh (NA),
  `tools/ref_regress.py --quick --build engine/build-regions`, plus the new region tests.
- Leave docs/modding/reference/map_names.csv and scratchpad/mapatlas/ alone.
- Don't commit or push. One line per finished step to scratchpad/checkpoint_notes.txt
  prefixed "regions: ".
- Keep this log current after each step and every 30 minutes. Past ~350k tokens of context:
  finish the step, update the log, end the turn.

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

## Status (12:40)

- Done: region layer, chokepoints, launcher/setup recognition, save product code, Europe map
  generated. Europe disc 1 boots to the title (shows "(c) 2000"), map 18 (Arlia) renders the
  same as NA (scratchpad/regions/runs/cmp_*.png).
- In progress: NA suites with SS_REGION_READ_LOG (scratchpad/regions/reads/na_suites/),
  then the read coverage check; then EU sweeps (event, battle) with the read log.
- Known: battle #2129 image includes the overlay area 0x800715d0+ (stale #1984/#1981/enemy
  defaults); 71 jal votes against its load address come from calls into that area.

## Next steps

1. Coverage: merge read logs -> tools/regions/na_reads.txt; region_map.py --check must pass.
2. Europe sweeps: event sweep both discs, battle sweep 564, endings; fix what breaks.
3. Script waits changed for 50 Hz (52 + 7 maps differ): analyse NA vs EU wait operands.
4. Unit tests (profile layer) + disc tests (EU, skip when absent).
5. Redux checks (title, shop, camp, dialogue, save layout), saves' play-time fields.
6. FR / DE maps via the same tool; text-system plug points documented.

## How to verify

- Build: `cmake --build engine/build-regions -j8`.
- NA suites: `scratchpad/regions/work/suites.sh TAG` (ss_tests, ss_game_tests, smoke,
  ref_regress quick -> scratchpad/regions/runs/suites_TAG.txt). Baseline 12:00: 78/0, 119/0,
  smoke 86/0, ref 28/28.
- Region runs: `scratchpad/regions/work/run.sh eu NAME [ENV=..] -- --frames N ...`.

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
