# Other regions: how far the European, French, German and Japanese discs are from NA

Survey of 2026-10-23 (regions agent, `docs/handoff/regions.md`). Everything here was measured
from the user's eight non-NA disc images against the North American pair; the scripts and their
outputs are under `scratchpad/regions/` (git-ignored). No game data is quoted, only addresses,
sizes and counts.

## Summary

| | Europe (English) | France | Germany | Japan |
|---|---|---|---|---|
| Boot executable | its own (shared with FR) | same as EU | its own | its own |
| Archive layout | same entry numbers and counts; 2596 of 4155 entries byte-identical | same numbers; 2363 identical | same numbers; 2387 identical | same numbers; 657 identical |
| Code | NA code relinked + a small PAL patch set (~80 catalog rows) | EU + a text / name-entry set (~115 rows) | as France (~118 rows) | the original release NA was localised from (~510 rows) |
| Party, enemy, spell modules | code identical (relinked) except 3 words in one spell | same as EU | same as EU | real changes in 6 / 7 / 7 modules |
| Items, shops, enemy stats | identical | identical | identical | 4 items and 1 enemy record differ |
| Event scripts | 905 of 964 maps' code identical | code identical in layout, message-window arguments differ in 938 maps | as France, more words | 472 maps differ in length |
| Text | NA encoding and font | 132-glyph Latin font, other glyph numbers | as France, its own font | kana / kanji, per-map fonts |
| Timing | PAL 50 Hz; movement scaled by 6/5 per frame | as EU | as EU | NTSC 60 Hz |
| Save product code | `BASCES-02159` | `BESCES-02160` | `BESCES-02161` | `BISLPM-86105` |

Europe is NA's code running at 50 Hz with a known set of PAL edits; France and Germany are
Europe plus a different text system; Japan is a different build of the game whose gameplay
code differs in hundreds of places.

## 1. Identity

| Disc | SYSTEM.CNF boot file | Size | MD5 of the executable | Disc sectors |
|---|---|---|---|---|
| NA 1 / 2 | `SCUS_944.21` / `SCUS_944.22` | 131072 | `3f6fc3bdc2471e9b1d4cb94ed76618f5` (both) | 236531 / 307542 |
| Europe 1 / 2 | `SCES_021.59` / `SCES_121.59` | 133120 | `07743d702bcfb2a624830bdb8ebc5774` (both) | 236508 / 307519 |
| France 1 / 2 | `SCES_021.60` / `SCES_121.60` | 133120 | `07743d70...` (the same file as Europe) | 235435 / 306446 |
| Germany 1 / 2 | `SCES_021.61` / `SCES_121.61` | 133120 | `0da7560087a24e44e97cf763f0647070` | 235446 / 306457 |
| Japan 1 / 2 | `SLPM_861.05` / `SLPM_861.06` | 131072 | `aec8258f0055ca84c00ba8ab8cec2bb2` | 236372 / 307385 |

- Every disc has the archive at LBA 300 with the same encrypted table (first decrypted word
  `0x34829314`, the one `loader_is_disc` tests) and the same entry counts: 4155 on disc 1
  (last entry #4586), 4170 on disc 2 (last #4604). Entry numbering is the same everywhere.
- The archive is `SO2.BIN` in the file system on disc 1 everywhere; on disc 2 NA, Europe,
  France and Germany hide it, Japan lists it.
- No disc has XA-ADPCM or CD-DA sectors (sector submode census): all sound is SPU, as on NA.
- Disc 2's program, overlay, party-module, menu and item entries are byte-identical to its
  disc 1's in every region, as on NA.

### Archive entries against NA (disc 1; disc 2 is the same picture)

| | identical | same size, other bytes | other size |
|---|---|---|---|
| Europe | 2596 | 1546 | 13 |
| France | 2363 | 812 | 980 |
| Germany | 2387 | 789 | 979 |
| Japan | 657 | 1783 | 1715 |

- Identical in all PAL versions: the large runs #18-#1404, #1985-#2128, #2160-#2270,
  #2435-#2575, #2577-#2734 (graphics and models). Every program and overlay differs (they
  are compressed, so any change shows).
- Movies (20 entries in #4563-#4586 on disc 1, 32 in #4563-#4604 on disc 2): byte-identical
  in all four regions, except Japan's opening #4563.
- Map entries (#3207 + map): same count (964 scripts per disc, maps 0-1114 with gaps) and
  numbering in every region; their scripts differ (section 3 below).

## 2. Code

### Method

`scratchpad/regions/cmpall.py` unpacks each region's programs and compares them with NA one
catalogue row at a time (5452 rows, 149 modules: boot, title, field, battle, credits, the
field and battle overlays, the 17 menu overlays, the 12 party modules, the 40 enemy-code
blocks and the 55 spell modules). Instructions are normalised (jal / j targets, `lui`
immediates and the offsets of registers derived from them, gp offsets) and the two word
streams are aligned with GNU diff. Two build differences are filtered out first:

- **Division traps.** NA's compiler emitted the divide-by-zero / overflow `break` sequences
  after `div`; the European builds omit many of them. They are a compiler option, not a code
  change, and are ignored.
- **Load addresses.** The whole-program images do not load where NA's do (next table), so
  every absolute address inside them is shifted. That is "shifted", not "changed".

A row is *same* (bytes equal at the same address), *shifted* (equal after normalisation),
*code* (instructions differ before the function's final `jr ra`) or *data* (only the bytes
after it differ: its tables, or a module's tail of vtables pointing into the moved program).

### Load addresses

| Image | NA | Europe | France | Germany | Japan |
|---|---|---|---|---|---|
| Programs #1, #3100, #2129, #3107 (`kProgramBase`) | `0x8002f810` | +0x1b0 (`0x8002f9c0`) | +0x1b0 | +0x1b0 | -0x290 (`0x8002f580`) |
| Field overlays #3101-#3109, menu overlays | `0x8007e000` etc. | same | same | same | same |
| Battle #1981 | `0x80085518` | +0xa4 | +0xdc | +0xd4 | +0x14 |
| Battle #1984 blocks | `0x800715d0` / `0x80081728` | +0x3fc | +0x434 | +0x42c | block 1 -0x4 (block 0 differs) |
| Enemy code slots A / B / C | `0x80088110` / `0x80089f18` / `0x8008b1f0` | +0xa4 | +0xdc | +0xd4 | +0x8 |
| Spell module | `0x8008cc00` | same | same | same | same |

(Measured from the jal targets and code pointers each image makes into itself.) The three PAL
versions have three different battle layouts, so France and Germany cannot share Europe's
battle addresses.

### Result

Rows whose code differs (excluding stale copies in the #2129 image and the data blobs that
follow the last function of a program):

| Area (rows) | Europe | France | Germany | Japan |
|---|---|---|---|---|
| boot (660) | 7 | 7 | 7 | 15 |
| title (751) | 10 | 21 | 22 | 51 |
| field (796) | 18 | 24 | 24 | 83 |
| battle (932) | 17 | 20 | 20 | 152 |
| credits (63) | 5 | 5 | 5 | 1 |
| field overlays (245) | 6 | 7 | 7 | 25 |
| menu overlays (1183) | 11 | 26 | 29 | 60 |
| battle overlays (187) | 3 | 5 | 3 | 64 |
| party modules (226) | 0 | 0 | 0 | 20 |
| enemy modules (296) | 0 | 0 | 0 | 31 |
| spell modules (113) | 1 | 1 | 1 | 8 |

Everything else is shifted-identical. The full per-function lists are in
`scratchpad/regions/code_<region>.txt` and `code_union.txt`.

### Europe (also in France and Germany): the PAL edits

- **Movement and camera scaled by 6/5 per frame** (`x * 6 / 5`, a signed divide by 5 via
  `0x66666667`), read in the REA diffs (`scratchpad/regions/diffs/eu1_*`): `fieldactor_walk_step`
  and `camscript_update` (#3101), `worldactor_walk` and `worldactor_vehicle_control` (#3102),
  `bobj_move` and `camera_update` (battle). The divide-by-5 constant appears 3 more times in
  #3101, 4 more in #3102 and 3 more in the battle program than on NA. Also changed, not read
  in full yet: `worldactor_move_step`, `camera_step_eye` / `camera_step_lookat`,
  `sprite_update`, `input_target_select_update`, `input_ring_menu_update`, and
  `shot_k08_firebolt` ... `shot_k1d_wind_blade` (in `shot_k08_firebolt` only the stack frame
  and one global offset differ).
- **Display:** `screen_set_display_window` in every program, `menu_scene_create` / `_destroy`
  / `_run` / `_frame_begin` / `_frame_end` (title and field), `movie_create`, every menu
  page's `*_on_close`, `boot_main` (one added call with argument 1 at start-up, presumably
  the PAL video mode) and `gpu_video_mode_flag_get` removed: PAL video mode and screen
  placement.
- **Timers:** `camp_menu_update_time` (the play-time display: one of NA's divide-by-60 constants became
  a divide-by-25 / 50 one; not read in full), `credits_vblank` /
  `credits_run` / `credits_compose`, `field_main_loop`, `battle_main_loop`.
- **Memory allocator:** `mem_pool_init`, `mem_alloc_low`, `mem_alloc_high`, `mem_free`,
  `mem_realloc_inplace` changed (a fix or a different allocator policy; not read yet).
- **Projectile clash removed:** in #1984 block 1, `clash_register` and `clash_resolve` are
  stubs (`jr ra`) and `spell_clash_check` changed. On NA, opposing elemental projectiles
  whose hit boxes overlap cancel or overpower each other; the PAL builds dropped that.
  This changes the play state, so the port must keep it per region.
- Smaller: `field_load_map`, `text_render_line`, `strbuf_format_int`, `sprite_resample`,
  `menu_colour_code` / `menu_colour` removed, `creation_viewer_update`, `rename_page_frame`,
  `spell_thundercloud_bolt` (3 words), `save_build` (the product code's address).
- The 40 enemy-code blocks, the 12 party modules and 54 of the 55 spell modules are the NA
  code relinked against the moved battle program: their bytes differ (vtables and calls
  point into it), their instructions do not.

### France and Germany, on top of Europe

- Text system: `text_layout_line` (214 of 442 words), `text_ascii_to_glyph`,
  `text_number_width`, `text_format_number`, `msg_window_print`, `text_from_ascii` (title
  and battle), `hud_draw`, `scan_window_draw` (DE).
- Name entry: `name_keyboard_create` / `_draw`, `keyboard_move`, `keyboard_confirm`, the
  rename page (`rename_run`, `rename_screen`, `rename_keyboard_*`, `rename_name_box_*`).
- Layout: `new_game_sequence`, `voice_collection`, `cvram_alloc` (title and #2981),
  `skills_page_refresh_list`, `result_draw_drops` / `result_draw_levelups` (FR),
  `save_menu_create` (DE), `options_draw_screen1`, `the_end` (#3105).

### Japan

Japan is the release NA was translated from, not a derivative of it: different loader and CD
callbacks, sound save / restore, memory-card code, the character initialisers
(`init_rena` ... `init_noel`), the battle item and message menus (#1984 block 0 has a
different layout), and gameplay code in enemy AI (Marsilio, Indalecio, Cyril, Magic Hand,
Vesper, Decus, Shin, Bark), party moves (Bowman, Dias, Ashton, Opera, Precis, Chisato) and
spells (Forget, Fairy Light, Lunar Light, Raise Dead, Fairy Heal, Wind of Destruction,
Divine Comedy). NA also added debug helpers Japan lacks (`script_flags_stats`,
`preload_table_dump`, `mem_pool_gc`). Each of these is a separate port of the Japanese
function.

## 3. Data formats

- **Item records** (#3002, 1023 x 0x30 bytes): identical in Europe, France, Germany. Japan:
  items 274, 357, 392 and 454 differ (price bytes +0x00-0x02 and +0x2b).
- **Shops** (#2986's table of 99 stock lists, 665 entries): identical in every region; the
  table moved (Europe/France/Germany `0x80081cdc`, Japan `0x80081ca0`, NA `0x80081c98`).
- **Enemy stats** (0x5c-byte records in the 564 encounter packs): the stat bytes
  (`+0x00`-`+0x4b`) are identical in all PAL versions; Japan differs in one record
  (encounter 540, type 207, the MP field). The name bytes (`+0x4c`-`+0x5b`) differ: 16 in
  Europe, most in France / Germany, all in Japan. Formation type lists are identical.
- **Event scripts**: same opcodes (no new opcode found; the differing words are operands).

  | | code identical | same length, words differ | other length | differing words |
  |---|---|---|---|---|
  | Europe | 905 | 52 | 7 (maps 0-3, 826 = the shared scripts, 688, 704) | 2288 |
  | France | 2 | 938 | 24 | 8372 |
  | Germany | 2 | 930 | 32 | 37753 |
  | Japan | 2 | 490 | 472 | 70214 |

  In Europe, France and Germany the differing words are `push` immediates and inline
  arguments of the message ops (`msg_open`, `msg_open_hold`, `msg_say_w`, `menu_request_3`,
  `wait`): text-box sizes, positions and display waits for the translated text. The
  script headers' save-variable sizes (+0x14 / +0x18) are the same as NA in every PAL map
  (Japan: one map).
- **Archipelago words**: all 313 chest words in `docs/ap/field.json` hold the same value at
  the same word in Europe, France and Germany (Japan 179 of 313). Doors (`doors.json`,
  2529): Europe 2522 equal (maps 474, 688 differ), France 2460, Germany 2341, Japan 1558.
- **Maps**: same count and numbering everywhere.
- **Save data**: the card file is `<product>S02-Snn` with product `BASCUS-94421` (NA),
  `BASCES-02159` (Europe, which kept the "BA" prefix), `BESCES-02160` (France),
  `BESCES-02161` (Germany), `BISLPM-86105` (Japan); the string is in #1, #2576 and #3100.
  The save writer (`save_build`) differs from NA only by that string's address in the PAL
  builds, and the script save-variable sizes match, so the PAL saves should share NA's
  layout (to confirm byte by byte with a real PAL save); Japan's save code differs.

## 4. Text

- **NA and Europe**: same encoding (one byte below 0x80, else two), same control codes, same
  per-map 12-pixel font (276-280 glyphs; the same bytes as NA in the maps checked).
- **France and Germany**: every map carries the same 132-glyph font (and #14's title font is
  132 glyphs instead of 96): Latin letters, digits, punctuation and the language's accented
  letters, no kana. 83 of its glyphs are NA glyphs at other numbers (France glyph 0 is NA
  glyph 0xa9), so glyph codes, the character classes `text_layout_line` uses for wrapping
  (digits 0xaa-0xb3, letters 0xb6-0xe9 on NA), and `text_ascii_to_glyph` (names, numbers)
  all differ; the French and German fonts differ from each other.
- **Japan**: per-map fonts of 316-600 glyphs (kanji subsets), a 404-glyph title font, and a
  different `text_box_draw_frame`, `text_render_line` and `text_layout_line` (the ruby
  codes 0x4009-0x400b presumably matter here; not checked).

## 5. Timing

- The PAL builds run at 50 vblanks a second and compensate the per-frame movement and
  camera steps by 6/5 (section 2). The vblank handler (`vsync_callback`, whose clock counts
  60 vblanks to the second) is unchanged, so that clock (shop prices and item keys use it)
  runs 5/6 as fast on PAL; the camp menu's play-time conversion changed (section 2) and
  needs reading against Redux.
- Music: sequences tick on root counter 2 (system clock / 8), not on vblanks, and the sound
  driver's code is unchanged in the PAL builds, so tempo should be the same in wall time.
- Movies: the STR files are identical; they stream at 150 sectors / 15 frames a second,
  so the PAL builds show each movie frame for 3.33 vblanks instead of 4.
- What is unchanged per frame (anything counted in frames: wait counters, battle timers,
  animation frames, the RNG's draw order) runs 5/6 as fast in wall time on PAL, as on the
  real console.
- To play each PAL version like its own original the port must run the PAL code at 50
  vblanks a second (`Runtime` field period, audio samples per field, movie pacing), with
  the PAL scaling in place. Running PAL data at 60 Hz with NA's per-frame code would be a
  faster, different game. Japan is NTSC like NA.
- Not yet checked against PCSX-Redux: the PAL display window and the remaining timer
  changes (`credits_vblank`, `field_main_loop`, `battle_main_loop`).

## 6. What the port assumes about NA

| Assumption | Where | Europe | France / Germany | Japan |
|---|---|---|---|---|
| Serial check: `scus_944.21` / `.22` and entries 6 / 7 present | `launcher/discs.cpp` `identify_disc`, `describe` (text), `discs.h` | breaks | breaks | breaks |
| Program images load at `0x8002F810` | `game/common/program_image.h` (`kProgramBase`), `program_data` | breaks (+0x1b0) | breaks | breaks (-0x290) |
| ~730 live NA addresses (tables, strings, layouts) read from the images: `field_data`, `battle_data`, `title_data`, `table(title, field)`, `page_table`, `ovl_data`, `effect_*`, `image_*`, `viewer_matrix`, `boot_s16`, `boot_exe_data` (106 files; most in `menu_creation.cpp`, `menu_specialty.cpp`, `party.cpp`, `characters.cpp`, `menu_status.cpp`, `menu_save.cpp`, `menu_camp.cpp`, `voice_queue.cpp`, `title/window.cpp`) | engine/src/game/** | every address shifts | shifts, and translated strings and tables change size | shifts and changes |
| Boot executable tables at NA addresses (sound pitch / bank order `0x8002B6C0`, libspu reverb presets `0x8002BC04` / `0x8002BC34`) | `game/boot/sound.cpp`, `runtime/libspu.cpp` | breaks (the PAL exe is laid out differently) | breaks | breaks |
| Enemy / spell modules found by FNV-1a 64 of the unpacked block; party modules by their entry | `battle/enemy_modules.cpp`, `battle/spell_modules.cpp`, `battle/pc_modules.cpp` | no block matches (relinked bytes): every monster falls back to the base class | same | same, and real code changes |
| Enemy slot / overlay link addresses (`kCtorA` ... `0x80088110` etc., `0x800715d0`, `0x80085518`) | `battle/enemy_modules.cpp`, `battle/*` | shift (+0xa4 / +0x3fc) | shift (FR and DE differ) | shift |
| Glyph numbers and character classes, control codes | `game/field/message*.cpp`, `title/text*`, menu text | holds | breaks (132-glyph font) | breaks (kana / kanji, ruby) |
| Save file name `BASCUS-94421S02-S` | `game/common/memcard.cpp` (`kSaveName`), `setup/setup_ui.cpp` (`kSavePrefix`), `menu_save.h` | breaks | breaks | breaks |
| NTSC field rate | `runtime/runtime.cpp` (`kFieldPeriod`), `platform/audio.cpp` (samples per field), `title/movie.cpp` (`kVblankHz`) | breaks (50 Hz) | breaks | holds |
| NA per-frame code (no 6/5 scaling, projectile clash on) | ported field / world / battle movement, `battle` clash | breaks | breaks | holds (but other Japanese code changes) |
| Archive entry numbers (#1, #3100, #2129, #3101, #3002, #14-#16, #3207 + map, ...) | throughout | holds | holds | holds |
| Archipelago tables (chests, doors, battles, late proof) | `docs/ap/*.json`, `apworld/so2`, `game/ap` | chests hold; 7 door words differ | chests hold; 69 / 188 door words differ | 134 chests and ~970 doors differ |
| Mod `EXPECTED` values and the repository tables `tools/mod_check.py` checks against | `tools/so2_mod.py`, `tools/mod_check.py`, `game/field/mods.cpp` | items / shops / enemy stats hold; script words mostly hold | items / shops / stats hold; script words often differ | several differ |

The engine already skips a mod line whose `EXPECTED` does not match the disc, so a mod made
on NA can never corrupt another region's data; it just does nothing there.

## 7. Feasibility probe

Not run. With every program loading at another address and the port reading its tables at
NA addresses, a bypassed serial check can only show the first wrong table read; the
measurements above already say where it would fail (the title's first `title_data` read,
then the sound driver's boot-executable tables). The probe is worth doing once phase 1's
address translation exists.

## 8. Plan

### Phase 1: Europe (English, PAL). Size: large.

1. **Recognise the discs** (small): `identify_disc` by serial table (region, disc number,
   product code); setup shows "Disc 1, Europe"; keep refusing a mix of regions.
2. **Region profile** (medium): one table per region with the program load bases, the
   battle overlay / enemy slot deltas, the product code and the video rate, chosen from the
   disc at boot.
3. **Address translation** (large, the bulk of the work): every NA address the port reads
   from an image (~730 sites plus the boot executable's tables) needs its region address. Build
   per-region translation maps offline from the alignment (`scratchpad/regions/cmpcode.py`
   style, NA address -> region address per module, with a check that the bytes read agree)
   and route `program_data`, `program_data_at`, `boot_exe_data` and the wrappers through them.
   No game data enters the repository: the maps hold only address pairs, or are computed
   from the disc at start-up.
4. **Modules by normalised hash** (small): register enemy and spell modules by a hash of
   their normalised instructions (or by NA hash plus per-region hashes generated by a tool),
   with their tables read through the translation.
5. **PAL code paths** (medium): a `pal` flag that selects the 6/5 scaling in the ported
   movement / camera / projectile / cursor functions, turns the projectile clash off, and
   takes Europe's display window and timer changes. Each of the ~80 changed rows gets read
   in full and ported beside the NA version.
6. **50 Hz** (medium): field period, audio samples per field, movie pacing, frame-based
   timers; the window still presents at the monitor's rate.
7. **Saves** (small): Europe's product code on the card and in setup's import; confirm the
   layout matches NA's with a real PAL save; a save stays in its region.
8. **Verification**: the existing reference method with a Europe disc in PCSX-Redux (`-cli`,
   PAL BIOS): openings of maps 7 and 18 instruction for instruction, the field walk / world
   map / vehicle positions per frame, a set of battles (positions, RNG to the exit), the
   credits, the play clock, a save loaded in both. Then the event sweep, battle sweep and
   smoke suites with the Europe disc.
9. **Archipelago**: the chest words are the same, so field locations carry over; regenerate
   the door table per region (7 words differ in Europe) and confirm the battle tables
   (enemy types and stats are identical). A seed can stay region-neutral if the item / location
   ids stay NA's.
10. **Mods**: items, shops and enemy stats are the same as NA, so those lines apply as they
    are; script lines apply where the word still holds `EXPECTED` (905 maps unchanged).
    `mod.json` gets an optional `regions` list (default `["na"]`, or "any" for data-only mods);
    `tools/so2_mod.py` writes `EXPECTED` from the chosen disc and records its region;
    `tools/mod_check.py` checks against per-region tables.
11. **Setup and docs**: README / wiki "Discs" page lists the supported releases, the
    50 Hz note, saves per region; release notes for 0.02.00.

### Phase 2: France and Germany. Size: medium-large (after phase 1).

- Address translation maps for each (their battle layouts differ from Europe's and each
  other's), product codes.
- Text: per-language glyph tables, character classes and `text_ascii_to_glyph` for the
  132-glyph fonts, the changed `text_layout_line` / `text_render_line`, the name-entry and
  rename keyboards, the changed layouts (`new_game_sequence`, `voice_collection`,
  `cvram_alloc`, result screen, skills page, save menu in German).
- Verification: as Europe, plus message pages checked against Redux captures (wrapping,
  accents), name entry, the rename page.
- Archipelago: chests unchanged; door tables per language (69 and 188 words differ).
- Mods: script lines will often be skipped (message words differ in ~930 maps); chest and
  door words mostly hold.

### Phase 3: Japan. Size: very large.

- Address maps; NTSC like NA.
- About 510 changed rows to read and port as Japanese variants, including gameplay code in
  enemy AI, party moves and spells, the battle item / message menus, the loader and sound
  save / restore, the memory-card code and character setup; 4 item records and 1 enemy
  record differ.
- Text: kana / kanji per-map fonts (and probably ruby), Japanese name entry, a different
  text-box frame.
- Scripts: 472 maps change length, so chest and door words must be found again per map; the
  Archipelago tables and the APWorld need a Japanese variant (134 chest locations move).
- Verification: everything again against a Japanese Redux reference; the event and battle
  sweeps.

## 9. Decisions for the user

1. **One build, a region profile chosen by the disc?** (recommended) or a separate build per
   region.
2. **PAL speed**: play the PAL versions at 50 Hz like the originals (the rule), with an
   optional, clearly marked 60 Hz mode later, or not at all?
3. **Projectile clash**: the PAL builds removed it. Keep it removed for PAL (the rule), and
   list it in `original_bugs.md`-style notes as a region difference?
4. **Archipelago seeds across regions**: keep NA's location and item ids for every region so
   one seed works with any disc (chests already match for Europe / France / Germany; doors
   need per-region tables), or tie a seed to a region (the APWorld option and the client's
   check)? Japan likely needs its own tables either way.
5. **Saves**: keep each region's own product code on the card (saves stay per region, as on
   the console), or let the setup program import a save from another region when the
   layout is the same (Europe / France / Germany from NA)?
6. **Mods**: default a mod to NA only, or to "any region" when it only edits items, shops and
   enemy stats (identical in the PAL versions)?
7. **Order and scope of 0.02.00**: Europe alone first, or Europe + France + Germany together
   (they share the PAL work; France / Germany add the text system)? Japan as a later
   release?

## Decisions (the user, 2026-10-23)

- **0.02.00 covers Europe (English), France and Germany.** Japan is a later milestone of its own.
- **One build, region profiles.** The disc is recognised at start and selects a profile: address
  maps, module lookup, text system, save product code.
- **European discs run the North American game logic at 60 Hz.** They use their own text and data,
  but the timing and gameplay code is NA's: the PAL 6/5 step scaling is not ported, and the
  projectile clash stays on as in NA. Their PAL-only changes are documented, not reproduced. Any
  European *data* changed for 50 Hz (script waits, movie rates) must be handled so the game plays
  at NA's pacing.
- **Archipelago:** one seed works on NA, Europe, France and Germany, since all 313 chest words
  match. Japan will get its own tables when it is supported.
- **Saves:** native per region (BASCES-02159 Europe, BESCES-02160 France, BESCES-02161 Germany),
  so cards stay compatible with that region's console. Importing an NA save may come later.
- **Mods** apply to every region by default. Their EXPECTED guards skip lines whose data differs.

## 10. The implementation (phase 2: one build, region profiles)

How the port plays the other releases, following the decisions above. Code:
`engine/src/runtime/region.{h,cpp}`; generated data: `engine/data/regions/*.inc`
(`tools/region_map.py`); continuation log: `docs/handoff/regions.md`.

### Profiles

`region::Profile` per release: key, names, the two serials, the save product code, the text
system (`Text`: NA's for North America and Europe; French, German, Japanese), the rate its
data counts in (`data_hz`: 50 for the PAL releases) and `supported`. The launcher recognises
a disc by its SYSTEM.CNF serial (`launcher/discs.cpp`): a supported release's disc 1 / 2, a
known release the port can't play yet (`kUnsupportedRelease`: Japan, and for now France and
Germany, until their text system is in; the message names the releases it plays), or
another game. Both discs must be the same release. `main.cpp` selects the profile from the
boot disc's serial before anything reads the discs.

### Address maps

The ported code reads the original's tables from the program images at their NA addresses.
For another release, `tools/region_map.py` aligns each image the port reads (boot
executable, the four programs, the field / menu / battle overlays, the 12 party modules, the
40 enemy-code and 55 spell-code modules) with NA's and writes segments (NA offset, size,
offset delta), the pointer words to relocate, load addresses and hashes, nothing else. Every
reader goes through one function (`region::image_byte`): `program_data_at`, `boot_exe_data`,
the battle overlay and module readers. At load, `region::prepare` checks the image is the one
the map was made from (another revision of a disc is refused, not misread) and rewrites the
listed pointer words to NA values, so a pointer the port follows stays in NA space. Enemy and
spell modules are identified by a hash of their normalised instructions (the same NA module
relinked elsewhere hashes alike). On NA nothing is translated.

Coverage: the port records every (image, NA address) it reads with `SS_REGION_READ_LOG`
(NA suites and sweeps -> `tools/regions/na_reads.txt`); `region_map.py --check` fails if any
of them has no counterpart in a release, and lists the read bytes whose value differs from
NA's (Europe: none; France / Germany: 3 bytes, text-related).

| | Europe | France | Germany |
|---|---|---|---|
| Images / segments / pointer runs | 149 / 1357 / 4336 | 149 / 1512 / 5693 | 149 / 1507 / 5665 |
| Map scripts laid out otherwise | 7 | 24 | 32 |

Data a release lays out its own way (a list whose length differs) is read from its translated
start in the release's own order (`program_run`): the credits' card script (Europe's own
staff, 65 cards to NA's 78).

### Map scripts

Archipelago doors, routines and join sites and the mods' `script` lines name places in a map
script by NA's code offsets. A release whose script for a map has another layout gets a script
map (instruction alignment, the same segment form); `region::script_offset` /
`script_na_offset` translate, and `ap.cpp` / `mods.cpp` use them, so one seed and one mod file
serve every release. `tools/so2_mod.py` / `so2_data.py` read any supported release's disc and
still write NA word numbers.

### Timing

Every release runs NA's game logic at 60 Hz (the decision): the PAL 6/5 movement scaling, the
removed projectile clash and the PAL display / timer code are not ported. Data the PAL builds
rescaled to 50 Hz is converted back where the port reads it (`region::ntsc_vblanks`, x6/5):
the credits' cue waits and music fade (NA 720, Europe 600, ...). The map scripts' `wait`
operands are not rescaled in the PAL releases (no same-length map differs in a wait; the
shared world script was restructured), so they need nothing. Europe's scripts set the
vibration countdown G[0x358] / G[0x3ac] to 500 frames where NA has 1800 (rumble length, 24
places): the release's own data, kept. The play clock (`vsync_callback` counts 60 vblanks a
second on every release) and the save's minutes field mean the same in every release, so
saves need no conversion.

### Saves

Native per release: the card file is `<product>S02-Snn` (Europe `BASCES-02159S02-S`, France
`BESCES-02160...`, Germany `BESCES-02161...`; the strings are in each release's #1 / #3100), and
the save writer is NA's (the PAL `save_build` differs only by that string's address), so a card
written by the port loads on that release's console. The setup program counts saves of every
release.
