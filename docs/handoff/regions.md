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

## 0.02.00 readiness (agent 5, 16:32)

Verdict: **not yet releasable; close.** Europe, France and Germany boot, play, save and load
on one build with NA unchanged, but the disc 2 / France / Germany sweeps and the battle
comparison are still running, and three Redux checks are open.

| | Europe | France | Germany |
|---|---|---|---|
| Recognised, both discs, mixed pairs refused (setup) | yes | yes | yes |
| Address maps: 6690 port reads, unmapped / suspicious / fatal | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Event sweep disc 1 vs NA (same binary) | 5162 same, 164 frames only, 0 other result | queued | queued |
| Event sweep disc 2 | done, NA's non-ok set; vs NA pending (ev_na_d2 queued) | queued | queued |
| Battle sweep vs NA ([bunits] RNG / positions / HP) | queued | queued (spot: 4 / 4 same as the queue binary) | queued |
| Text | NA's | own system; opening 88 / 88 lines = Redux | own system; 84 / 84 lines = Redux |
| Title keyboard, camp / Options pages = Redux | n/a | yes | yes |
| Rename page = Redux | n/a | yes | yes |
| Battle result screen / Scan window = Redux | n/a | open | open |
| Saves: written by the port load in the original | yes (Redux) | yes | yes |
| Saves: written by the original load in the port | (same format) | yes (Redux-written save) | (same format) |
| Archipelago | e2e_so2 27 / 27 | ap_e2e 23 / 23; e2e_so2 open | (FR covers the text path) |
| Mods (`mod_verify`, both samples) | PASS | (same maps as EU) | (same maps as EU) |
| Smoke (converted card) | 85 / 86 (hearn_talk: EU text offset, expected) | - | - |

Known differences (by decision or the release's own data): NA game logic at 60 Hz on every
release (the PAL 6/5 scaling and the projectile-clash removal are not ported); each release's
own text, staff cards, rumble lengths, save product code, save magic and icon flag; the
credits' 50 Hz cue waits converted to 60 Hz.

Suites on the final build (`engine/build-regions`, copy `bin/ss_t5c`; t5c 16:29): ss_tests
99/0, ss_game_tests 128/0, smoke 86/0, ref_regress quick 28/28.

**Agent 5 ended 16:33 (context budget). Still running:** the sweep queue
`scratchpad/regions/q/queue.sh` PID 1279484 (binary `bin/ss_q`; ev_na_d2 started 16:18, then
ev_fr_d1 ... bt_de; rerun it to resume, finished steps are skipped). Not ours (both ended by 16:33): the
`tools/ap_late_proof.py` runs (PIDs 1297126, 1297127); another agent's `ap_e2e_so2.sh` may still run. The
queue's binary predates the save fixes (they change no sweep result: sweeps run without a card).

Not done: French battle result screen and German Scan window against Redux (item 5, plan
above); `ap_e2e_so2.sh` on France (item 6); the rest of the queue and its comparisons (items
2-3: `work/evcmp.py q/ev_na_dN q/ev_RG_dN`, `work/bcmp.py q/bt_na q/bt_RG`, then delete the
battle logs); README / wiki "Discs" and the 0.02.00 release notes; optional NA-save import in
setup (product code, magic, icon flag and both checksums, as `card_region.py`).

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
