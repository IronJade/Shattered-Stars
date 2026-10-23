# French and German text: how the original does it, and the port's design

The text system of the French and German releases (0.02.00; `docs/notes/regions.md` is the
survey and the user's decisions, `docs/handoff/regions_text.md` the continuation log).
Everything here was measured from the user's discs; no game text, font or table is quoted,
only addresses, counts, glyph numbers and how the code uses them. Scratch tools and extracts:
`scratchpad/regions_text/` (git-ignored).

## Summary

| | North America / Europe | France | Germany |
|---|---|---|---|
| Encoding | bytes; < 0x80 one byte, else `(b0 & 0x7f) \| b1 << 7`; codes < 0x4000 glyphs, 0x4000-0x400f control | same | same |
| Map font | 276-280 glyphs (kana, Latin, symbols) | 132 glyphs, Latin with accented letters | as France, plus glyph 0x62 |
| Glyph numbers | digits 0xaa-0xb3, A-Z 0xb6-0xcf, a-z 0xd0-0xe9, space 0x105 | digits 1-10, A-Z 0x0d-0x26, a-z 0x28-0x41, accented 0x42-0x61, space 0x72 | as France, 0x62 a letter |
| Word wrap | classes kana / digits / letters / codes >= 0x107 | classes digits / letters; no break before " ?" / " !" | as France |
| Name entry | two sets (capitals, lower case), five buttons | one set (digits, capitals, punctuation), three buttons | as France |
| Changed functions | - | 41 (text, name entry, layouts) | 43 |

France and Germany are one localisation job: the same font layout (the German font adds one
letter), the same code changes, and German moves a few more menu positions for its longer
words. Europe (English) kept NA's text system unchanged. What did not change anywhere: the
string encoding and control codes (`docs/catalog/formats.md`, Text), the font block format,
how glyphs are rendered and uploaded (the field's 4-bit line bitmaps in the window's text
cells; the title's and the rename page's line buffers), the message windows and their cell
allocator, `text_char_name_width`, `text_measure_block`, `text_blit_glyph`, the menu text
renderer (which takes its digits from the string packs, so it needed no change).

## 1. Fonts

All three fonts keep NA's block format (`s32 count, s32 height (12), u8 width[count]`, padded
to 4, then 12 `u16` rows per glyph, bit 15 leftmost), the places they live and the way they
are drawn.

- **Map font** (entry 3207 + map, section 1, SLZ block 1; installed with the map like NA's):
  132 glyphs in 962 of the 964 maps. 886 French maps share one font; the others differ in
  the per-map symbols from 0x82 (7 maps have 136 glyphs), and one map (626, the same font in
  both languages) in 27 glyphs of the accented range. Maps 1113 / 1114 carry a 492-glyph font
  (not looked at further). The French and German fonts are identical except glyph 0x62
  (German u-umlaut; an empty, zero-width glyph in the French font).
- **Title font** (#14 block 1) and **battle font** (#1980 block 1): 132 glyphs (NA 96 / 104),
  ASCII order like NA's (code = ASCII - 0x1f for 0x20-0x7e), then a few symbols and, from code
  0x63, the 33 accented letters in the map font's order (map code + 0x21). The German title and
  battle fonts are the same block.
- **Side font** (#4034 = 0xfc2, the item-name strings' font for windows 10-19): 132 glyphs
  (NA 268), the map font's numbering.

Map-font glyph numbers (1-based codes, both languages):

| Codes | Glyphs |
|---|---|
| 0x01-0x0a | digits 0-9 |
| 0x0b, 0x0c | '-', '.' |
| 0x0d-0x26 | A-Z |
| 0x27 | apostrophe (title font: the grave accent's place) |
| 0x28-0x41 | a-z |
| 0x42-0x61 | accented letters, Latin-1 order: capitals with grave, circumflex, diaeresis, AE, C cedilla, E's, I circumflex / diaeresis, O's, U's, sharp s, then the same in lower case |
| 0x62 | German: u-umlaut; French: empty |
| 0x63, 0x64 | '?', '!' |
| 0x65-0x68 | closing brackets / quotes (NA 0xf0, 0xf2, 0xf4, 0x11d) |
| 0x69-0x6c | opening brackets / quotes (NA 0xf7, 0xf9, 0xfb, 0x11b) |
| 0x6d-0x71 | '=', ':', ';', ',', '/' |
| 0x72 | space |
| 0x73 | the letter-like apostrophe (NA 0x106) |
| 0x74-0x81 | symbols (heart, currency, arrows, ...) |
| 0x82-0x88 | per-map symbols |

The European text uses Latin-1 bytes for accented letters only in one dormant place (the
title's and battle's ASCII tables, section 4); character names are plain ASCII in every
release, and the name keyboards offer no accented letters.

## 2. Field text (field program; France == Germany except the letter range)

| Function | NA | FR / DE |
|---|---|---|
| `text_ascii_to_glyph` (character names) | 0x8005e8f8 | 0x8005e704: a jump table over 0x20-0x7a maps ' ' ! , . / 0-9 : ; = ? A-Z a-z; any other byte stays as it is (NA: 0xfe). The half-width kana path is NA's. |
| `text_format_number` / `text_number_width` | 0x8005cbf0 / 0x8005c9e4 | digits 1 + d, '-' 0x0b |
| `text_render_line` | 0x8005cd44 | the space dropped at the start of a wrapped line is 0x72 (NA 0x105) |
| `msg_window_print` | 0x80059db8 | a glyph in 0x69-0x6c (NA 0xf7-0xfd) that would be a line's last starts the next line |
| `text_layout_line` | 0x8005c2fc | 0x8005c24c: the word-wrap rules below |

### Word wrap (`text_layout_line`)

The structure is NA's: glyphs and inserted strings (names, numbers, items, fixed spaces) are
measured (width + 1 each); a class change records a break opportunity (glyph count, width,
ruby count); when a glyph overflows the window (`w - 16`), the line either ends there or goes
back to the last opportunity. Only the classes and the overflow rules changed:

| | NA | FR / DE |
|---|---|---|
| Classes | kana 0x55-0xa9 (1), digits 0xaa-0xb4 (2), letters 0xb6-0xe9 and 0x106 (3), codes 0x107-0x3fff (4); 0xea-0xfd and '.' keep the class; 0x400c (5); any other glyph (0) | digits 1-10 (2), letters 0x0d-0x61 (DE 0x62) and 0x73 (3); 0x63-0x6c and 0x0b-0x0c keep the class; 0x400c (5); any other glyph (0), **unless the next code is 0x63 or 0x64 ('?', '!'): then it keeps the class and is no break** |
| Break before a glyph | its class differs from the previous one (letters after digits / letters, kanji after kanji / digits, kana after kana / digits do not break) | the same pairs (no class 1 / 4) |
| Overflow: go back to the last break | kana after kana / digits; digits, letters, ',' after digits / letters; codes >= 0x107 after kanji / digits; NA's closing punctuation (0xea-0xf6, 4-0xe, 0xa7-0xa9, 0x106, '.') always | digits, letters, ',' (0x70) after digits / letters; 0x63-0x68 (? ! and closers), 0x73, '-', '.' always; a space when '?' / '!' follows |
| Other NA rules | glyph 1 ends a line that has glyphs; glyphs 2 / 3 may squeeze into the last two pixels | none of these (glyph 1 is '0', 2 / 3 are digits) |
| Continued line | glyph 0x107 costs nothing at its start (the renderer drops 0x105: both are "space + 2" off) | glyph 0x74 (space 0x72 + 2: the same off-by-two kept by the localisation) |

The effect: French typography (a space before ? ! : ;) never leaves '?' or '!' alone at the
start of a line, and an apostrophe never starts one.

### Message windows and their sizes

Window creation, sizes in cells (6-40 wide), the cells, the typewriter and the control codes
are NA's. The localised map scripts carry their own window sizes, positions and waits as
operands of the message opcodes (938 French and 930 German maps differ there; the survey's
section 3); the port reads them from the disc, so nothing is to port. The field's own windows
that moved are in section 6 (the equipment report, the disc-lid notice).

## 3. Name entry (title `name_keyboard_*`, rename page `rename_keyboard_*`)

NA: two character sets (capitals, lower case; L1 / L2 / R1 / R2 or the Capital / Lower
buttons switch), five row-0 buttons (Capital, Lower, OK, Delete, Return; text ids 0x6e-0x72,
x = 0xc + 0x2a * column), the cursor starting on OK (column 2).

France and Germany: **one set** (the table's first five rows: digits, capitals, punctuation;
the lower-case rows still follow it in the image but are never drawn), **three buttons** (OK,
Delete, Return on columns 0-2; ids 0x6e-0x70; x = 0xc + 0x4c * column), the cursor starting on
**Delete** (column 1), a full name moving the cursor to OK (column 0), and the set switch
removed from both screens' input loops. The column map that links the ten key columns to the
row-0 buttons is data (NA 0x8004eaf0: 0,0,1,1,2,2,3,3,4,4; FR / DE: 0,0,0,1,1,1,1,2,2,2). The
rename page also moves its hand / cursor / blank-mark sprites (new UVs), saves one more VRAM
area, and compares the new name with "Claude" (the localisation fixed NA's "Crawd",
`original_bugs.md` M1, which the port had fixed the same way). Checked in PCSX-Redux: the
French title keyboard shows three buttons, the cursor on Delete, and five rows.

## 4. Title and battle ASCII conversion

The title's and the battle's `text_from_ascii` (names into the 132-glyph fonts) look each byte
up in their program's ASCII table (data, read through the region maps). France added a branch
for bytes >= 0x81 outside kana mode, `glyph = table[c] + 4` (title) / `+ 6` (battle); Germany
only in the battle program, `+ 0xc`. The tables (the same values in both languages) map the
Latin-1 letters to 0x5c + n, so only the French battle branch lands on the accented glyphs
(code 0x63 + n); the French title branch is two glyphs short, the German battle branch six
past, and German titles have no branch. No name or keyboard key holds such a byte, so none of
this runs in play; the port reproduces it as it is. The rename page kept NA's copy of the
function.

## 5. Text VRAM allocator (`cvram_alloc`, title and the menu core #2981)

The menus' text lines are allocated in a VRAM free list. NA takes the fitting free block with
the least height (first on ties); France and Germany take the last fitting block that is no
wider and no taller than the best so far, which packs the wider translated lines better. It
changes only where a line's texels go.

## 6. Layouts moved for the translations

Every constant below is a literal in the named function; the port has the NA value as a
literal in the listed file. "-" = as NA.

| Function (NA address) | Port | What | NA | FR | DE |
|---|---|---|---|---|---|
| field_equip_report_draw (0x8004b7a8) | field/post_battle.cpp report_draw | window 9 width (cells); item windows 10-14 dx; item windows width | 0x14; 0; 0x14 | 0x24; 8; 0x12 | as FR |
| field_main_loop (0x80049bfc) | (not ported: the disc-lid notice) | window 5 x, y, w | 5, 8, 0x1e | 2, 0xa, 0x24 | as FR |
| options_draw_screen1 (#3016 0x8007eea8) | field/menu_options.cpp | pen_move after group 5 message 0; group 6 pen x; row 8 first move; row 0xc move | 0x70; 0xa8; 0x38; 0x50 | 0x78; 0xb0; -; - | 0x78; 0xb0; 0x48; 0x30 |
| options_draw_screen2 (0x8007f268) | field/menu_options.cpp | icons x, step; pen_move after values[7] message 0 | 0xb8, 0xe; 0x40 | -; 0x50 | 0xc8, 0xc; 0x50 |
| specialty_page_show_costs (#3004 0x8007f0d0) | field/menu_specialty.cpp | MP label x | 0x88 | 0x70 | 0x70 |
| link_combo_dialog_create (0x800803ac) | field/menu_specialty.cpp | ON / OFF x | 0xa0 / 0xc0 | 0x80 / 0xa8 | as FR |
| skills_page_refresh_list (#3014 0x8007ee80) | field/menu_skills.cpp | list heading | ASCII literal at 0x38 / 0xcc | group 0xf message 0 at 0x10 / 0xa4 | as FR |
| specialty_detail_show (0x8007fc14) | field/menu_skills.cpp | pen x | 0xe0 | - | 0x100 |
| equip_options_create (#3012 0x800802fc) | field/menu_equip.cpp | width; choices x | 0xd0; 0x80 / 0xa0 | 0x110; 0x88 / 0xc0 | as FR |
| check_dialog_setup (title 0x8006514c, #2998 0x80080120) | common/menu_save.cpp CheckDialog::setup | width | 200 | 0x100 | 0x100 |
| save_menu_create (title 0x80063350, #2998 0x8007e000) | common/menu_save.cpp | slot grid x; step | 0xa8; 0x40 | - | 0x88; 0x50 |
| save_menu_rebuild_slots (0x80063b70 / 0x8007e898) | common/menu_save.cpp | slot labels x | 0xb8 / 0xf8 | - | 0x98 / 0xe8 |
| sound_window_draw (title 0x8004716c) | title/window.cpp | lines when selection is 2 | 5 | - | 6 |
| vibration_window_draw (0x800477dc) | title/window.cpp | heading x | 0x54 | 0x30 | - |
| draw_number (0x800411a4) | title/voice_collection.cpp | digit strip v | 0x32 | 0x31 | 0x31 |
| voice_collection (0x8003fb78) | title/voice_collection.cpp | label quads VOICE w; EXIT w, v; BGM w, h, v; RANDOM w, u; STOP w, h, u, v | 0x2d; 0x1f, 0x50; 0x2e, 0xc, 0x32; 0x3f, 5; 0x24, 0xd, 4, 0x60 | -; 0x39, 0x4f; 0x43, 0xe, 0x30; 0x4a, 4; 0x3c, 0xb, 3, 0x5f | 0x4b; rest as FR |
| new_game_sequence (0x80042cc8) | title/new_game.cpp lines 243 / 336 / 441 | subtitle sprites x | 0x8c / 0x98 / 0x80 | 0x68 / 0x68 / 0x58 | - |
| | title/new_game.cpp 579-580 | name entry heading w; subtitle x | 0x80; 0x80 | 0x90; 0x98 | - |
| | title/new_game.cpp 634-635 | tpage-7 screen heading w; subtitle x | 0x86; 0x88 | 0xb0; 0xa8 | as FR |
| | title/new_game.cpp 686-688 | tpage-8 screen heading w; subtitle x; icons lit when selected | 0x96; 0x90; no | 0xb0; 0xb0; yes | -; -; yes |
| rename_screen (#3022 0x8007e4dc) | field/menu_rename.cpp | heading w; subtitle x, w, u, tpage | 0x80; 0x88, 0x80, 0x80, 0x25 | 0x90; 0x98, 0x90, 0, 0x26 | as FR |
| rename_keyboard_draw / rename_name_box_draw | field/menu_rename.cpp | hand u; cursor u; blank marks u | 0x80; 0xa0; 0x90 | 0xc8; 0xe8; 0xd8 | as FR |
| rename_run (0x8007e308) | field/menu_rename.cpp | VRAM saved around the screen | 4 areas | + (0x180, 0, 0x40, 0x40) | as FR |
| the_end (#3105 0x801f9a64) | field/field_sub_overlays.cpp | third logo quad x1, u1 | 0x26c, 0x88 | 0x205, 0x98 | 0x235, 0xc8 |
| result_draw_drops / result_draw_levelups (#1981) | battle/result.cpp | drop line; learned-skill line | name then message 5 / 8 | message 5 / 8 then the name | - |
| scan_window_draw (battle 0x80064dd4) | battle/spell_windows.cpp | first page (and its sound) | 0 (enemy name) | - | 1 (HP; the name page skipped) |
| hud_draw (battle 0x800654f4) | (not ported: the disc-lid notice) | window x, w | 0x3c, 0xc0 | 0x1c, 0x100 | - |

The instruction addresses and the French / German function addresses are in the catalog rows
(`docs/catalog/functions.csv`, modules `fr_*` / `de_*`).

## 7. Other differences in the same functions (not text)

Found while reading the changed rows; the port keeps NA's behaviour (the user's decision:
European discs run NA's logic):

- `title_screen`: the checks of a local flag (leave for the attract sequence) moved after the
  title music's load; control flow only.
- `the_end` (German): the display is enabled after the second frame instead of before the loop.
- `rename_screen`: one DrawSync more before leaving.
- The German Scan window skips the enemy-name page (section 6); kept as a layout flag.

Functions that differ from NA only as Europe's do (the PAL edits, `docs/notes/regions.md`) are
not repeated here; neither are stale overlay copies inside the #2129 image.

## 8. The port's design

### The module

`engine/src/game/common/text_rules.{h,cpp}` (in `ss_core`, no game state): the language-
dependent part of the text system as plain functions and tables, keyed by `region::Text`
(the profile's text system; Europe has NA's). It holds no game data: tables the original keeps
in its images (the title / battle ASCII tables, the keyboard characters and column map, the
0x400d widths, the string packs) stay there and are read through the region maps like every
other table.

| API | Original | NA | FR / DE |
|---|---|---|---|
| `map_glyphs(lang)` | digit / minus / space glyphs, the layout's skip glyph, the typewriter's push range | 0xaa, 0xb4, 0x105, 0x107, 0xf7 + 7 | 1, 0x0b, 0x72, 0x74, 0x69 + 4 |
| `ascii_to_glyph(lang, ...)` | text_ascii_to_glyph | NA's switch | the jump table |
| `format_number`, `number_width` | text_format_number, text_number_width | | |
| `layout_line(lang, window, host, text, out)` | text_layout_line | NA's rules | FR / DE rules |
| `wrap_class(lang, code, next)` | the class step of text_layout_line (for tests and tools) | | |
| `ascii_high_bias(lang, program)` | the >= 0x81 branch of title / battle text_from_ascii | -1 (none) | title FR 4, battle FR 6 / DE 0xc |
| `name_keyboard(lang)`, `keyboard_button(lang, column)` | the keyboards' button count, step, first column, OK column, set switch, row-0 actions | | |
| `cvram_choose(lang, blocks, w, h)` | cvram_alloc's choice | | |
| `layout(lang)` | every moved constant of section 6, named after its ported site | | |

`layout_line` takes the window fields it reads (`LayoutWindow`) and a `LayoutHost` for what it
asks the game (glyph widths of the current font, message variables, name / item / number
widths, the ruby measure, the 0x400d widths), so it is the same code for every release and
testable without a running field.

### Wiring (for the region profile's owner)

Nothing in the game calls the module yet. To plug it in (each a small change at the NA
literal; NA behaviour is unchanged by construction, and `ss_game_tests` proves `layout_line`
equals the current NA function):

1. `field/message_text.cpp`: `text_layout_line` -> `text::layout_line(text::lang(), ...)` with a
   `LayoutHost` over the MsgWindow (the test's `FieldHost` is that adapter); `text_render_line`'s
   0x105 -> `map_glyphs(lang).space`; `text_format_number` / `text_number_width` ->
   `format_number` / `number_width`; `text_ascii_to_glyph` -> `ascii_to_glyph`.
2. `field/message.cpp` `msg_window_print`: `code - 0xF7 < 7` -> the push range.
3. `title/text.cpp` and `battle/window.cpp` `text_from_ascii`: when `ascii_high_bias` >= 0 and
   `c >= 0x81` outside kana mode, `glyph = table[c] + bias`.
4. `title/window.cpp` and `field/menu_rename.cpp`: `NameKeyboard` (buttons, step, first column,
   OK column), `keyboard_button` in the confirm functions, `charset_switch` in
   `title/new_game.cpp` / the rename screen's input loop; only set 0 is drawn. The character
   table, the row counts and the column map keep NA's layout (both sets, five rows each), so
   they are read at NA's addresses through the region map as they are now.
5. `common/menu_gfx.cpp` `cvram_alloc` -> `cvram_choose`.
6. Every row of section 6: the literal -> `text::layout(text::lang()).<field>`.
7. Launcher: France and Germany become supported releases once 1-6 are in and their address
   maps pass `region_map.py --check` (the three differing read bytes it reports are text
   data).

The European (English) discs keep using every NA path (`Text::kNorthAmerica`).

## 9. Verification

- **NA equivalence** (`ss_game_tests` `text_rules_na_layout_matches_field`, NA disc):
  `layout_line(kNorthAmerica)` against the ported `text_layout_line` for every string of 140
  maps, from every code boundary, at four widths, continuing or not, with names, numbers and
  items: 3,302,816 layouts, 0 differ.
- **French / German discs** (`text_rules_french_disc` / `_german_disc`, `SS_DISC1_FR` /
  `SS_DISC1_DE`): 962 of 964 maps carry a 132-glyph font with the assumed digit and space
  widths and glyph 0x62 empty in French only; every string (about 107,000 per disc) lays out
  line by line without a line that cannot advance; the title and battle fonts have 132 glyphs.
- **PCSX-Redux** (`text_rules_redux_lines`, `SS_TEXT_REDUX_LOG`): breakpoints on the original's
  `text_layout_line` entry and return (French / German 0x8005c24c / 0x8005c878) log the window
  (width, word wrap, continuing, line height), the text, the current font's widths, the names,
  the message variables and the result while a script mashes into a new game and through the
  opening's dialogue (`scratchpad/regions_text/redux/`); every logged line is replayed through
  `layout_line`. Results: see the continuation log (filled in as the runs finish).
- **Unit tests** (`ss_tests`, `text_rules_*`, no disc): glyph tables, the ASCII conversion
  (French / German / NA, the kana path equal everywhere), numbers, the classes, word wrap on
  synthetic fonts (spaced punctuation, apostrophe, digits, German 0x62, continued lines, NA's
  glyph-1 and squeeze rules, no word wrap), the keyboards, the allocator and the layouts.
