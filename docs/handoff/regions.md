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
- (regions-text agent 3, 14:55) `region_map.py` can align a same-size table whose values
  changed a few words off: FR / DE title NA 0x8004eaf8-0x8004eb08 (the name keyboard's column
  map, values 0,0,1,1,2,2,3,3,4,4 -> 0,0,0,1,1,1,1,2,2,2) became a 16-byte segment with delta
  -340 (FR; -364 DE) inside -352 (-376), and the 0xbce overlay the same at 0x80082b78 (entries
  6-9 of its column map at 0x80082b70 unmapped). The text code now reads around it (the
  ASCII tables entry by entry, the column map from its start), but a positional alignment for
  equal-length changed runs would be the general fix (then regenerate fr.inc / de.inc).
- Don't rebuild engine/build-regions while a test runs its binary from there (copy it to
  `scratchpad/regions/bin/` for long runs, as the sweeps do).

## Phase 3: the 0.02.00 finish list (agent 4, `regions-finish`, from 14:33)

Rules as above (budget: -j4, 4 game processes, 2 PCSX-Redux; memcap on every run; NA suites
after every engine change; AP files are another agent's: only run its tools; no commits).
Finish list: (1) region_map.py near matches, (2) full sweeps EU / FR / DE disc 1 + 2 vs NA,
(3) NA vs EU battle comparison, (4) saves per release in Redux, (5) Redux: French battle
result, German Scan, rename page FR / DE, (6) AP messages in FR / DE + AP e2e on EU and FR/DE,
(7) mod_verify on EU, (8) setup: release shown, mixed pairs refused; then a "0.02.00
readiness" section here.

- 14:40 item 1 started: analysis in `scratchpad/regions/rmap/` (islands.py, stretch.py,
  cross.py). Only near-match island the port reads: the FR / DE title column map (NA
  0x8004eaf8, 4 words at -340 inside -352). Reads within 1 KB before a layout change: EU 3,
  FR 5, DE 6 (title 0x8004eafc ASCII table, 0x8004d388 icon y, 0x8004e274 menu rects,
  0x80063198 talents (DE), battle 0x8006e83a byte table, 0x8008519c s16): all readers checked,
  none crosses.
- 14:50 item 1 done (code; suites map1 green: ss_tests 98/0, ss_game_tests 128/0, smoke 86/0,
  ref_regress quick 28/28). `tools/region_map.py`:
  - step 3b `realign`: a changed stretch of <= REALIGN_MAX_WORDS (64) between two trusted runs
    (>= NEAR_WORDS = 16 words) at the same delta is mapped word for word; the diff's short
    runs at other deltas inside it (near matches) are dropped (EU 3 stretches, FR 15, DE 16;
    incl. the title column map 0x8004eaf8 and the 0xbce one 0x80082b78).
  - step 3c `TABLES` / `force_table`: tables indexed by position over their whole extent
    (title / 0xbce column maps, the title / battle / 0xbce ASCII tables, 0x100 bytes) mapped
    word for word from the start; fatal if the start is a near match or the table changed
    size. Found: the FR / DE **battle** ASCII table (0x8006e7c0) had its accented half
    (c >= 0x84) in near matches / unmapped (wrong glyphs for accented names in battle); the
    title's the same from c = 0xa0. Both now positional (27 words each).
  - step 6 checks: a read in a near match = fatal; a read within CROSS_WINDOW (1 KB) before a
    layout change with no later read = "suspicious" unless READ_EXTENTS gives its extent and
    it ends before the change (5 reviewed entries). Summary line: "reads N: U unmapped, S
    suspicious". Now EU / FR / DE: 6690 reads, 0 unmapped, 0 suspicious, 0 fatal.
  - maps regenerated (engine/data/regions/{eu,fr,de}.inc; na_modules.inc unchanged). Scratch:
    `scratchpad/regions/rmap/` (show.py REGION [image lo hi] prints segments / realigned).
- 14:55 item 8 done (code): setup refuses a mixed pair: Save / Save & Play disabled, bottom
  bar shows "Disc 2 is the European release and Disc 1 the French one. Both discs must be the
  same release.", tooltip, error dot on Discs; `save()` refuses too; the Discs note lists
  the four releases. Screens `scratchpad/regions/setup/{mixed,fr}.png` (scratch XDG).
- 14:55 item 6 code: `text::port_symbol_glyph` (text_rules) maps ASCII symbols FR / DE's
  ascii_to_glyph passes through as letters to the map font's own glyphs ('-' 0x0b, ( ) [ ] { }
  < > 0x65-0x6c, '"' 0x7b, # $ % & * + 0x75-0x7a, _ | ~ 0x7d-0x7f, ` -> apostrophe 0x27, \ ->
  '/', @ ^ -> space); `ap.cpp` message_glyph uses it (banner + message windows). NA / EU keep
  NA's mapping (unmapped bytes = NA's blank 0xfe). Unit test text_rules_port_symbol_glyphs.
  AP e2e on EU / FR still to run.
- 14:56 sweep queue started: `scratchpad/regions/q/queue.sh` (3 jobs, binary copy
  `scratchpad/regions/bin/ss_q`; progress `q/queue.log`): event sweeps EU/NA/FR/DE disc 1 + 2,
  then battle sweeps NA/EU/FR/DE (--god --strong 20:30:4, SS_BATTLE_UNITS=1200, read logs in
  `q/reads/`). Rerun it to resume: finished steps (results.json) are skipped.

### Agent 5 (`regions-finish-2`, from 15:25): state taken over

Two attempts between agent 4 and this one were stopped by permission refusals after a session
restart; this is what they left (checked 15:25).

- **Item 1** (`tools/region_map.py`): in place and committed at 0e6683f (15:20): same-size
  changed tables mapped by position (3b), `TABLES` (3c), short out-of-step matches fatal, reads
  that may run past a layout change need `READ_EXTENTS`. EU / FR / DE regenerated: 0 unmapped, 0
  suspicious. The two edits reported as refused are in the file too: the battle ASCII table's
  `TABLES` extent is NA's 0xE0 (France / Germany's table is 0x100), and step 6b
  `code_references` (each release's own unchanged code that addresses a byte, relocated, as a
  witness for the map; disagreements the port reads are fatal; 331-426 per release disagree,
  all where the port doesn't read).
  **Real bug left:** France / Germany's battle ASCII table from c = 0xe0 up (accented letters)
  lies past NA's extent, so the map can't place it; the battle, title and rename text code
  must read the table from its translated start in the release's own layout, then regenerate
  `engine/data/regions/*.inc`, build, test. One realigned spot in overlay 0xba5 is contradicted
  by the release's code; the port doesn't read there.
- **Item 8** done (above, 14:55).
- **Item 6:** FR / DE Archipelago messages map symbols to the right glyphs (unit test); the AP
  e2e passed 27/27 on Europe. Left: the French or German e2e.
- **Item 4:** the right product codes are written. Open: in PCSX-Redux the French original's
  title shows Continue in another colour with the port's save on the card (it may not see the
  save): diagnose, and compare with a save the French original writes itself.
- **Items 2-3:** sweep queue `scratchpad/regions/q/queue.sh` (PID 1279484, JOBS=3, binary
  `scratchpad/regions/bin/ss_q` built 14:56, log `q/queue.log`), 12 steps: ev eu/na d1, eu/na
  d2, fr/de d1, fr/de d2, bt na/eu/fr/de. ev_eu_d1 started 14:56; at 15:25 on map 826 (whose
  ending entry K21 hangs on NA too), 0 failures so far. The item 1 port change only changes
  which glyphs French / German text uses (not results, frames, RNG, positions, HP), so these
  sweeps stay valid; spot-check a few French maps and battles on the new build.
- **Not started:** item 5 (Redux: French battle result, German Scan, rename page FR / DE),
  item 7 (`tools/mod_verify.py` on the two sample mods against a Europe disc), the 0.02.00
  readiness section.
- Other task's jobs, not ours: `tools/ap_late_proof.py` PIDs 1297126, 1297127 (leave alone).

Agent 5 progress:

- 15:45 **item 1 done.** The committed maps (0e6683f) were made before the 0xE0 extent: they
  mapped NA 0x8006e8a0+ (the battle program's globals) into France / Germany's table, which
  happened to give the accented glyphs entry by entry. Port: the battle program's ASCII table is
  read from its translated start in the release's layout (`battle/window.cpp` `ascii_table`,
  used by `text_from_ascii` and `window_draw_small_text`), the title's (`title/text.cpp`) and
  the rename page's (`field/menu_rename.cpp`) the same way (`program_run`, bounded by the
  image). Maps regenerated (`engine/data/regions/{eu,fr,de}.inc`: 2 segment lines each, the
  battle table by position over NA's 0xe0 only; na_modules.inc unchanged): 6690 reads, 0
  unmapped, 0 suspicious, 0 fatal for all three (reports `scratchpad/regions/rmap/report5_*.txt`;
  table dump `rmap/tables.py REGION`: France / Germany's entries 0xe0-0xff now come from the
  table, Europe's are zero as NA's). Suites t5a (`work/suites1.sh`, ref_regress with --jobs 1
  because the queue holds 3 game processes): ss_tests 99/0, ss_game_tests 128/0, smoke 86/0,
  ref_regress quick 28/28. French spot check, new binary `bin/ss_t5` vs the queue's `bin/ss_q`
  (`work/spot5.sh` -> `spot5/`): events of maps 7, 18, 65, 120, 240, 331, 500: 80 / 80 entries
  identical; battles 1, 35, 191, 300 with [bunits]: 4 / 4 identical. No name or keyboard key
  holds a byte >= 0xe0 (regions_text.md section 4), so nothing visible changes in play.
  Notes: regions.md section 10 (tables, step 6b), regions_text.md section 4.
- Item 4 (16:05), two findings:
  1. The grey "Continuer" was a Redux setup fault: `redux/run.sh` passed a relative
     `-memcard1`, which Redux resolves against its config dir ("card1.mcd doesn't exist ...
     failed to load!" in stdout.log), so the original had no card (title clut 0x1E4 = no card
     ready; NA's own title showed the same with an NA save). Fixed: the script passes absolute
     paths (`run.sh`, `run5.sh`).
  2. **Real bug, fixed:** with the card loaded, the French original listed the port's save as
     "Données Star Ocean 2 endommagées" (damaged). The save summary's 16-byte magic is per
     release (title 0x8004deb8 / field 0x800740b0: NA "STAR OCEAN 03/01", Europe "06/09", France
     "08/12", Germany "08/13") and the port hard-coded NA's in `save_summary_check` and
     `save_build`. Now `menu_save.cpp` `save_magic()` reads it from the running program through
     the region map (`table(0x8004DEB8, 0x800740B0)`). NA's card is byte-identical to the old
     binary's (same save input, `runs/save5_na` vs `save5_na_old`). Redux (`redux/runs5/*_new`):
     the NA, Europe, France and Germany originals each list the port's save (Continue
     preselected) and load it into map 18. `work/card_region.py` (NA card -> PAL, the EU smoke)
     now rewrites the magic and both checksums (NA -> NA is byte-identical on two cards).
  Redux: Start skips the intro movie only once it plays (~v1950); `redux/sheet.py DIR` makes a
  contact sheet. In progress: the French original writing its own save (poke
  `*0x80075370 + 0x68 = 1`, field_flags save switch, FR address of NA 0x8007570c), then the port
  loading it.
- 16:15 **item 7 done:** `tools/mod_verify.py --serial` (new opt-in flag: one game process at a
  time, so it fits next to the queue; documented in docs/notes/mods.md section 8) on the Europe
  disc 1 with `bin/ss_t5b`: arlia-crate-fol PASS (both script lines applied, maps 18 / 21 sweep
  as without the mod), hearn-store-plus PASS (shop 1 entry 7 applied, map 26). Reports
  `scratchpad/regions/modverify/<id>/report.md`. Chain `work/chain5.sh` (log
  `scratchpad/regions/chain5.log`) goes on with the Europe smoke on a converted card and the
  French AP e2e.
- Suites t5b (save magic build, 16:10): ss_tests 99/0, ss_game_tests 128/0, smoke 86/0,
  ref_regress quick 28/28.
- Item 4, reverse direction: poking field_flags[0x68] to allow a save on map 18 crashes the
  original (it is the world-map flag; LoadAddressError at 0x80089460). Plan: the port writes a
  French save on world map 1 (saving allowed there), the French original loads it and saves to
  #2, the port loads #2. France's field globals (from its own code, `rmap/coderef.py`; the map
  is 4 bytes off in this zero-filled block, which the port never reads): field_flags pointer
  0x80075374, game_state pointer 0x80075378 (RX_GSTATE).
- 16:28 **item 4 done.** Reverse direction: the port wrote a French save on world map 1 holding
  item 0x61 (`runs/save5w_fr`, also `save5w_de`), the French original loaded it and saved to #2
  (`redux/runs5/fr_wsave`: S02, magic "08/12"), and the port loaded that save (`runs/load5_fr`,
  SS_LOAD_SAVE=1: map 1 at the saved position). Byte compare of the two files: only the slot
  digit in the title, counters, checksums, length, the summary's unused tail (buffer bytes,
  under each checksum) and **header byte 2, the icon flag: NA 0x13, all three PAL releases
  0x11** (title 0x80066110 / page 0xbb6 0x800810ec `addiu v0, zero, 0x11`). Fixed: save_build
  takes that immediate from the running program (`table(0x80066110, 0x800810EC)`); NA's card
  stays byte-identical (`runs/save6_na` = `save5_na_old`), France now writes 0x11
  (`runs/save6_fr`). `card_region.py` sets the flag too. Notes: regions.md section 10 Saves.
- 16:24 **item 5, rename page done:** port (`runs/ren5_fr|de`, camp > Statut / Status > Claude >
  confirm) against the French and German originals (`redux/runs5/fr_rename|de_rename`; side by
  side `redux/runs5/rename_cmp.png`): identical heading, subtitle, name box, three buttons, five
  rows, cursor on Delete, and the Status page before it. **Not done:** the French battle result
  screen and the German Scan window need a battle in Redux: load `runs/save5w_fr|de/card.mcd`
  (world map 1, item 0x61 in the bag) in the original, walk until an encounter (PAL vsyncs:
  hold times x5/6 of the port's), mash Cross to win (result screen), or use item 0x61 from the
  battle item menu (Scan); do the same from the same card in the port (SS_LOAD_SAVE=0).
- 16:21 **item 6:** `tools/ap_e2e.sh` (ChecksFinder stand-in slot, 23 checks incl. phases 3-4)
  on the French disc with `bin/ss_t5b`: 23 PASS, 0 FAIL (`scratchpad/regions/ap_e2e_fr.out`,
  copy `ap_e2e_fr/`). The Europe 27/27 was `tools/ap_e2e_so2.sh` (the real SO2 slot); the French
  run of that one is **still to do**: another agent's `ap_e2e_so2.sh` held its server port at
  16:25. Command: `AP_E2E_WORK=$PWD/scratchpad/regions/ap_e2e_so2_fr SS_DISC1=<FR disc 1>
  XDG_CONFIG_HOME=$PWD/scratchpad/regions/xdg tools/ap_e2e_so2.sh $PWD/scratchpad/regions/bin/ss_t5c`.
- 16:16 Europe smoke on the card converted with the new `card_region.py` (Europe magic):
  85/86, the world-map save cases load and pass; the one failure is the known hearn_talk NA
  text offset (Europe's string table is 2 bytes longer).
- Sweeps (queue): ev_eu_d1 and ev_na_d1 done (15:24, 15:51): 827 maps each, the same tallies
  (1678 transfers, 3640 ok, 2 hang = maps 0 / 826 K21 ending loop, 3 gone, 3 stuck = 591-593
  T9, NA's M8 baseline); `work/evcmp.py q/ev_na_d1 q/ev_eu_d1`: 5162 identical, 164 same result
  with other frames (talk text lengths, the restructured world script K19 / K60), 0 other
  result. ev_eu_d2 running since 15:51 (ended 16:18 with NA's non-ok set:
  0 / 826 hang, 58 / 139 gone, 591-593 stuck); the queue goes on with ev_na_d2, ev_fr_d1,
  ev_de_d1, ev_fr_d2, ev_de_d2, then bt_na, bt_eu, bt_fr, bt_de (hours).

### Agent 6 (`regions-verify`, from 18:40): last checks before 0.02.00

Rules as at the top (headless, memcap, -j4, 3 game processes + 2 Redux of our own, no commits;
`checkpoint_notes.txt` prefix "regions-verify: "). Binary from the working tree incl. the El voyage
gate changes: `bin/ss_v1` (built 18:43, `engine/build-regions`). Scratch: `scratchpad/regions/verify/`
(`evrun.sh` = one map in sweep mode with screenshots + `SS_EVENT_SWEEP_WATCH`, `trace.py` =
VM-state changes, `evcmp2.py` = transfer destinations / big frame deltas, `pngsheet.py`).

- 19:05 **Event sweep comparisons done** (`scratchpad/regions/evcmp/{eu_d2,fr_d1,fr_d2,de_d1,de_d2}.txt`
  and `*_2.txt`; queue binary `bin/ss_q`, same for every release):

  | vs NA | same | same result, other frames | other result | only in release |
  |---|---|---|---|---|
  | EU d2 | 5163 | 163 | 0 | 0 |
  | FR d1 / d2 | 4502 / 4501 | 818 / 819 | 6 / 6 | 2 / 2 |
  | DE d1 / d2 | 4496 / 4495 | 820 / 821 | 10 / 10 | 2 / 2 |

  Non-ok sets: EU = NA's exactly (0 / 826 K21 hang, 58 K19 / S19 + 139 K40 gone, 591-593 T9
  stuck; the same on disc 1 and 2). FR: NA's minus 591-593 stuck, plus 266 K18 timeout. DE: NA's
  minus 591-593 stuck. No transfer goes to another map than NA's where both transfer
  (`evcmp2.py`). Every difference explained; **none is a port fault**:
  1. **The sweep's choice heuristic** (event_sweep.cpp `schedule_input`: a choice gets Cross, but
     every third press made while a choice waits is a cancel). France / Germany's option text has
     more characters (591: NA "Touch it. / Don't" 26 bytes printed by frame 67, FR 31 characters
     in 23 bytes by frame 73; the speed table 0x800751b8 is the same 2 / 60 / 16 / 8 / 4 in all
     four releases), so a choice accepts input one press (6 frames) later and the sweep lands on
     the other option. Scripts 266, 588, 591-594, 598, 605 are NA's logic (only window geometry /
     text ids differ, `work/script_diff3.py`). Cases: 591-594 T9 (FR picks "Laisse tomber" instead
     of touching the orb, so no NA M8 rotating-bridge stuck; 594 no transfer to 593); 605
     (promontory: NA shouts for the echo and the scene returns to 598, so NA's new game on 605
     "left the map" and FR / DE's has the 2 entries; 598 T12 = the same scene, ends on 605); DE
     588 T0 (confirms the return to Energy Nede: map 2); FR 266 K18 (North City library
     encyclopedia: every lap adds the same press count, so the cancel never falls on the topic
     menu at ip 5108 and the laps repeat; the article "L'ère de paix" types out in full,
     `verify/sheet_266.png`, lap trace `ev/fr266w`); and the frames-only outliers 123 K18,
     190 K16, 442 K17, 584 K25 (choice at ip 4775 accepted at 21 frames vs NA's 15). Screens:
     `verify/sheet_591.png`, `sheet_605.png`; traces `verify/ev/*w/run.log`.
  2. **World script K19 / K60 (maps 0 / 826)**: the developers' text test (screen "So2 German
     Revision 027 ... fontmaster.bin", France "So2 France Revision 052"), each release's own
     sequence (EU / FR restructured too: 3644 / 3852 frames vs NA 11852); Germany's ends with a
     transfer to map 282. Debug only, not reachable in play (`verify/sheet_k19.png`).
  The 818-821 "other frames" entries of FR / DE are talk lengths (their own text): 421 within 10
  frames, 347 within 60, 42 within 300, the rest are the cases above.
- 19:25 **`ap_e2e_so2.sh` on France done: 27 PASS, 0 FAIL** (binary `bin/ss_v1` = working tree with
  the El voyage gate changes; `AP_E2E_PORT=38295` so it can't meet another agent's server; output
  `scratchpad/regions/ap_e2e_so2_fr.out`, work dir `ap_e2e_so2_fr/`, both game runs under the cap:
  `memcap.txt`). Captures (`verify/e2e_fr_caps.png`): "Sent Map Width to Bob." and the banner in
  the French map font on map 18.
- 19:38 **Item 5 done: French battle result screen and German Scan window = the originals.**
  Port runs from new cards written by the port on world map 1 at a walkable spot (`--map-pos
  51200,0,18500`: `runs/save6p_fr|de` with item 0x61, `runs/save6s_fr|de` with item 0x36; note
  **item 0x61 is the Insanity Ring: the Scan item is 0x36 Spectacles / Brille**, effect 0x61);
  port scripts `verify/pbt.sh`, originals `redux/run6.sh` + `redux/drive6.lua` (drive.lua plus
  RX_PEEK / RX_POKEP), walk keys x5/6 (`redux/runs6/walk_pal.txt`).
  - France (`verify/fr_result_pair.png`; port `runs/pbt_fr2/cap`, original `redux/runs6/fr_result`):
    the same "Résultat / Points d'expérience / Fol" window (place, size, columns, the spinning
    gem) and the same level-up window "Claude Possession : N Aug. / N point(s) d'aptitude
    obtenu(s)." (France's word order: `result_prefix_order`). The numbers differ because the
    fights differ (the port: encounter 6, 36 EXP / 60 Fol, 2 levels; the original, Claude's HP /
    attack poked to win: 20 / 36, 1 level, so no number before "Aug.", the port's rule too).
  - Germany (`verify/de_scan_cmp.png`; port `runs/pbt_de_s1` = debug `battle 1` from the card,
    Brille from the item ring; original `redux/runs6/de_scan`, a random encounter with the same
    monster): "GP 60/ 60", "MP 0/ 0", "Licht: Schwäche" with "Brille" under it, pixel for pixel
    the same window, text and ä. No port change needed.

## 0.02.00 readiness (agent 6, 19:45; battle rows: coordinator, 22:55)

Verdict (22:55): **releasable.** Every row is filled and passed. The battle comparison found
one port fault, the never-clash spell list misread on PAL (docs/handoff/soulmaster.md), which is
fixed; on the fixed build every encounter of every release matches NA.

| | Europe | France | Germany |
|---|---|---|---|
| Recognised, both discs, mixed pairs refused (setup) | yes | yes | yes |
| Address maps: 6690 port reads, unmapped / suspicious / fatal | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Event sweep disc 1 vs NA (`evcmp/`) | 5162 same, 164 frames only, 0 other result | 4502 same, 818 frames only, 6 other + 2 only-FR: all explained | 4496 same, 820 frames only, 10 other + 2 only-DE: all explained |
| Event sweep disc 2 vs NA | 5163 same, 163 frames only, 0 other | as disc 1 (4501 / 819 / 6 + 2) | as disc 1 (4495 / 821 / 10 + 2) |
| Non-ok entries (both discs) | NA's set exactly | NA's minus 591-593 stuck, plus 266 K18 timeout (sweep heuristic) | NA's minus 591-593 stuck |
| Battle sweep vs NA ([bunits] RNG / positions / HP), NA's stages | 564 / 564 identical | 564 / 564 identical | 564 / 564 identical |
| Text | NA's | own system; opening 88 / 88 lines = Redux | own system; 84 / 84 lines = Redux |
| Title keyboard, camp / Options pages = Redux | n/a | yes | yes |
| Rename page = Redux | n/a | yes | yes |
| Battle result screen = Redux | n/a | yes (`verify/fr_result_pair.png`) | not compared (the French check covers the window code) |
| Scan window = Redux | n/a | not compared (the German check covers it) | yes (`verify/de_scan_cmp.png`) |
| Saves: written by the port load in the original | yes (Redux) | yes | yes |
| Saves: written by the original load in the port | (same format) | yes (Redux-written save) | (same format) |
| Archipelago | e2e_so2 27 / 27 | ap_e2e 23 / 23; **e2e_so2 27 / 27** (`ap_e2e_so2_fr.out`, El voyage gate build) | (FR covers the text path) |
| Mods (`mod_verify`, both samples) | PASS | (same maps as EU) | (same maps as EU) |
| Smoke (converted card) | 85 / 86 (hearn_talk: EU text offset, expected) | - | - |

Event sweep differences explained (agent 6 entry above): the sweep's choice heuristic meets
France / Germany's longer option text one press later and takes the other answer (591-594 T9,
598 T12 / 605, 588 T0, 266 K18 laps, frames-only outliers 123 / 190 / 442 / 584); maps 0 / 826
K19 / K60 are each release's own developer text test (Germany's ends on map 282). Not port
faults; the scripts are NA's logic there.

Known differences (by decision or the release's own data): NA game logic at 60 Hz on every
release (the PAL 6/5 scaling and the projectile-clash removal are not ported); each release's
own text, staff cards, rumble lengths, save product code, save magic and icon flag; the
credits' 50 Hz cue waits converted to 60 Hz.

Suites: agent 5's final build (t5c 16:29): ss_tests 99/0, ss_game_tests 128/0, smoke 86/0,
ref_regress quick 28/28. Agent 6 changed no engine code (only the working tree's El voyage
gate changes, the elgate agent's, are in `bin/ss_v1`).

Still running (coordinator, ~20:40): the sweep queue `scratchpad/regions/q/queue.sh` PID 1279484
(binary `bin/ss_q`): bt_na done 19:22 (564 runs: cap 155, defeat 82, victory 327, 0 crashes),
bt_eu and bt_fr done, bt_de started 20:34 (ends ~21:20, then "queue done" in `q/queue.log`). Do
not start, restart or kill it. Agent 6's own background waits (plain `timeout ... kill -0 1279484`
/ `[ -f q/bt_eu/results.json ]` sleep loops, no game processes) end with the queue or at their
deadlines; nothing else of agent 6's runs.

**Agent 6 ended ~20:40** (coordinator's wrap-up: the session switched to prompted edits, so a
background agent can't run shell commands). No command was refused before that. Agent 6 changed
no engine, tool or test code; scratch only (`scratchpad/regions/verify/`, `redux/drive6.lua`,
`redux/run6.sh`, `redux/runs6/`, `runs/save6*`, `runs/pbt_*`, `runs/bt6_*`).

### Remaining (coordinator runs these)

Run from `scratchpad/regions/` (the repo's `scratchpad/regions`), after "queue done" appears in
`q/queue.log`:

1. Battle sweep health, per release:
   `head -8 q/bt_eu/summary.md q/bt_fr/summary.md q/bt_de/summary.md`
   Expect 564 runs each, 0 crashes / sanitizer reports / timeouts, outcomes close to NA's (cap
   155, defeat 82, victory 327; identical if the traces below are identical).
2. The comparisons (save each output; `mkdir -p bcmp` first):
   `python3 work/bcmp.py q/bt_na q/bt_eu > bcmp/eu.txt`
   `python3 work/bcmp.py q/bt_na q/bt_fr > bcmp/fr.txt`
   `python3 work/bcmp.py q/bt_na q/bt_de > bcmp/de.txt`
   then `head -3 bcmp/*.txt`. The first line is `{'same': N, 'differ': M} only NA x only RG y`.
   Expect `same` 564, `differ` 0, only NA / RG 0 for all three: the battle programs run NA's
   logic, and France's 4 spot battles (1, 35, 191, 300) were trace-identical (agent 5). For
   every `differ` row the script prints the first [bunits] line where NA and the release part
   (RNG hash, draws, positions, HP per frame). A difference is acceptable only if the release's
   own data is read at that point (check the table with `rmap/peek.py IMAGE NA_ADDR LEN`, which
   prints the bytes in NA / EU / FR / DE through the map); otherwise it is a port fault (a
   misread table): flag it, it blocks the verdict until fixed (a fix must keep NA
   byte-identical and rerun the suites, rules at the top).
3. Record the three first lines in the readiness table ("Battle sweep vs NA" row) and set the
   verdict: releasable if all three are 564 / 0, or every difference is explained.
4. After recording: `rm -rf q/bt_na/logs q/bt_eu/logs q/bt_fr/logs q/bt_de/logs` (~90-150 MB
   each; results.json / summary.md stay).

Optional (not required for the verdict; the code paths are covered by the checks done): the
German result screen and the French Scan window. Port:
`SAVE=save6s PARGS=--debug verify/pbt.sh fr pbt_fr_s1 1450
"810:triangle,814:none,850:left,854:none,910:cross,914:none,970:cross,974:none,1030:cross,1034:none"
"$(seq -s, 1030 15 1450)" "SS_DEBUG_INPUT=200:battle 1"` (run from the repo root; captures in
`runs/pbt_fr_s1/cap`); original: as `redux/runs6/de_scan` (keys in the 19:38 entry's runs:
`RX_KEYS="1950:start,1954:none,4700:triangle,4704:none,4740:left,4744:none,4790:cross,4794:none,4840:cross,4844:none,4890:cross,4894:none"`,
`RX_MASH="2050:2230:30:cross"`, `RX_GSTATE=0x80075378`, `RX_MAPKEYS="1:0:<redux/runs6/walk_pal.txt cut at 2128>,2128:none"`,
`redux/run6.sh fr runs6/fr_scan $PWD/../runs/save6s_fr/card.mcd`). Expect the same windows.

Release-notes facts (docs/release_notes/0.02.00.md, the coordinator words them):
- Event sweeps ("TO CONFIRM" at line ~34): done for all three releases on both discs. Europe's
  results equal North America's on disc 2 as on disc 1 (0 other results). France and Germany run
  every entry; the only different results come from the automatic test answering a choice
  differently (their option text is longer, so the test's button press lands on the other
  answer) and from each release's own developer text test in the world script (unreachable in
  play): no port difference. Their non-ok entries are NA's own (the ending loop on maps 0 / 826,
  maps 58 / 139 gone on disc 2) plus France's 266 K18, the test's input looping in North City's
  library encyclopedia (the article displays in full). The battle comparison is still pending
  (step 2 above).
- Battle result / Scan ("TO CONFIRM" at line ~40): confirmed. The French battle result screen
  and the German Scan window match the originals in PCSX-Redux.
- French e2e ("TO CONFIRM" at line ~150): confirmed. The full end-to-end test (27 checks, the
  real SO2 slot) passed on the French discs too, on a build with the El voyage gate changes.
- Correction for anyone following the old item-5 plan: item 0x61 is the Insanity Ring; the
  Scan item is 0x36 Spectacles (German "Brille"), whose effect kind is 0x61.

Not done: the battle comparisons above; README / wiki "Discs"; optional NA-save import in setup.

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

## Battle comparison (coordinator, 21:10)
Queue done 21:05. bt_na / bt_eu / bt_fr / bt_de: 564 runs each, 0 crashes / sanitizer reports /
timeouts. `work/bcmp.py q/bt_na q/bt_RG` (outputs scratchpad/regions/bcmp/{eu,fr,de}.txt):
197 same, 367 differ for every release, but the sweeps are not like for like:
- **Stage choice differs (sweep tool, not the port).** battle_sweep.py picks a stage per
  encounter from the field program's group lists: NA 374 `random`, 130 `default`, 60
  `scripted`; EU / FR / DE 0 `random`, 504 `default` (stage 3), 60 `scripted`, so it finds no
  group lists in the PAL field program (presumably read at NA offsets). 365 encounters ran on
  other stages than NA's; their traces part at line 0 (the stage's party start positions).
  Not comparable until rerun on NA's stages.
- **Same stage (199 encounters):** 197 byte-identical traces and endings in all three
  releases. **2 differ, identically in EU, FR and DE:** enc466 (Soulmaster x3 + Dreamshade x2;
  first diff at trace line 336, battle frame 672: draws 3534 vs NA 3529) and enc478 (Soulmaster
  x4; line 305, frame 610: draws 3067 vs 3062; then defeat 1644 vs NA's cap). Positions and HP
  equal at that line: the PAL runs draw 5 more random numbers in one frame. Open: the release's
  own data (acceptable) or a port read of an NA table (blocks the release).
Next: (1) root-cause the Soulmaster draws (SS_TALK_TRACE / rng trace around frame 610 of
enc478, NA vs EU; rmap/peek.py on the reads in that frame); (2) rerun the PAL sweeps on NA's
stages for the 365 encounters (fix the sweep's PAL group-list read, or pass NA's stage per
encounter), then bcmp again; (3) battle logs (q/bt_*/logs) kept until then.

## Battle comparison, final (coordinator, 22:55)
After the soulmaster fix (never-clash list mapped by position; build engine/build-soulmaster,
copy bin/ss_fix) and the sweep's PAL stage fix (tools/battle_sweep.py reads the PAL field
program's group lists through the region map; stagerun agent), the 365 encounters that had run
on other stages were rerun on ss_fix for EU, FR and DE (work/bf_all.sh -> q/bf_{eu,fr,de}; 365
runs each, 0 crashes) and compared with q/bt_na (work/bf_cmp.py): **365 / 365 identical in all
three, stage = NA's for every run.** The other 199: 197 byte-identical in q/bt_RG (the fix
cannot change them: the PAL map now gives NA's bytes), and enc466 / enc478 identical on the
fixed build (soulmaster's reruns). So 564 / 564 per release. bt2_* (old binary) and bt3_eu
(cut off at 362, when the stagerun agent stopped) are superseded. Battle logs (q/bt_*, bt2_*,
bt3_eu, bf_*: ~150 MB each) are kept until the release, then deleted.
Known difference for the notes (by the NA-logic decision): PAL's own spell_clash_check checks 9
never-clash spells (NA's 7 plus 0x41 The Scream and 0x42 Judgement Day); the port runs NA's 7, so
those two item spells can clash on PAL in the port, unlike on a PAL console.
