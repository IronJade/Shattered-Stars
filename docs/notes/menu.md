# The menu framework, the save / load page and the save file

tri-Ace's menu class library, shared by the title program (Continue) and the field
program (camp menu, save points, shops). The title links the library at
0x8002fff4-0x8003c000 and 0x80067d4c-0x80074fe4, plus page 0x11 (save / load) at
0x80063350-0x800673fc. The field links the kernel (scene, session, page loader, window
lists, bgload, StatCalc) at 0x8002f9f8-0x8003c2c0. Its widget library comes from
overlay entry 0xba5 at 0x800d1b28, and each page's code from an overlay pair (below).
Function names are in `menu.csv` and globals in `menu_globals.csv`.

Port: `engine/src/game/common/menu_core.*` (kernel), `menu_scene.*` (scene, session,
page loader), `menu_gfx.*` (VRAM allocator, upload queue, text renderer, sprites),
`menu_widget.*` (widgets, cursor), `menu_stats.*` (StatCalc), `menu_save.*` (page 0x11,
card operations), `game/common/party.*` (the party / stat library), `game/title/load_screen.*`
(title 0x800317e0 / 0x8003186c) and `game/field/menu_camp.*` (the field's launcher,
runners, field_update hooks and the camp menu's first page).

## Running a menu

`load_game_screen_run` (title 0x8003186c) shows the whole sequence:

1. Unpack the resource pack into `g_menu_res` (the window-art TIMs) and the page data
   into `g_menu_data` (messages, font, tables, the page's own TIMs). Make the sprite pack
   resident: `entry_cache_load(…, 0xbd2)`. For Continue these are archive entries
   #3024-#3026 (0xbd0-0xbd2), which the title screen reads in one go while it fades.
2. **Session** (`menu_session_create`, 0x38 bytes, `g_menu_session`). It records what the
   menu is for:
   - `+0x1d` start page, `+0x15` load mode (page 0x11), `+0x1c` variant, `+0x19` / `+0x1a`
     whether to free the cached 0xbd2 / 0xbbb;
   - `+0x16` result: 1 = a game was loaded;
   - `+0xc` the focused window's help flags, `+0x20` the button icon map.

   It also points the stat library at the handoff blocks (config 2, party 3, records 4,
   inventory 6). When handoff 0xe (the field snapshot) is absent it allocates it; the
   destructor frees it again unless a game was loaded.
3. **Scene** (`menu_scene_create`, `g_menu_scene`):
   - 320x240 double buffer (y 0 / 0xd8, margins 12/12/4/4);
   - a 60000-byte primitive arena and a 15-entry ordering table;
   - the window list;
   - the VRAM it borrows: the icon page moves from (0x180,0x100) to 0x140; the CLUT rows
     and (0x200,0x100) are saved; window-art TIMs 0 / 1 go to (0x180,0x100) and
     (0x180,0x1c8);
   - greyed palette copies for each character row (0x1e4 + gfx, columns 0x10 / 0x20);
   - the CVram allocator over the free VRAM.

   The scene's card driver is the caller's (`+0x18` = 1, `+0x14` = the title's card
   manager) or its own.
4. `menu_scene_run`. It creates the **page loader**, then loops:
   - `frame_begin`: flip, background loads, pad reduced to its lowest bit, a 4-frame
     input lock after any press;
   - `menu_pages_update`;
   - `window_list_update`: every widget draws and reads input;
   - `frame_end`: card driver step, DrawSync, the VRAM upload queue, VSync, DrawOTag.

   It stops when the pages reach 0x14, and returns session `+0x16`.
5. The destructors free the widgets, restore the borrowed VRAM and turn the display off.
   The Panel destructors run StatCalc, which recomputes every member's derived stats.

### Pages and overlays

The page loader keeps a stack of {widget, page}. `menu_pages_update` runs the handler
of the current page:

- with no focused window (just started, or a page's widget closed);
- or when the focused widget is the current page and has a result.

A handler creates its page widget (`push`, substate 1), and reacts to the result (2
cancelled, 3 confirmed) by going to another page (`goto`) or back (`pop`; depth 0 →
page 0x14 = leave).

`menu_pages_request` maps each page to its overlay pair: code entry N, data entry N | 1.
The pair is loaded in the background when the entry changes.

| Page | Entry |
|---|---|
| 1 | 0xba6 |
| 2 | 0xbbc |
| 3 | 0xbbe |
| 4 | 0xbc4 |
| 5 | 0xbc6 |
| 6 | 0xbb2 |
| 7 | 0xbc8 |
| 8-10 | 0xbca |
| 11-13 | 0xbaa |
| 14 | 0xbce |
| 15 | 0xbae for variants 4 / 0xb, else 0xbc0 |
| 16 | 0xbc2 |
| 17 (0x11, save / load) | 0xbb6 |
| 19 | 0xbcc |

The code overlay unpacks to the loader's code address (field 0x8007e000). The data
replaces `g_menu_data`. The title has no loads: page 0's handler turns loading on, but
the first request only records the entry. Page 0x11 (`menu_page11_update`, SaveMenu) is
linked into the title image, and the title's other handlers are empty. In the field the
page handlers live in the field image (`menu_page01_main` … `menu_page13_update`), and
the widget library in overlay 0xba5.

### Classes

Every object starts with a vtable whose slots are 8 bytes {s16 this-adjust, s16, fn}.
Slot n is at `vt + 8n`.

- **Task** (vtable 0x8004b898): [1] destroy, [2] type, [3] close, [4] on_close,
  [5] update.
- **WindowList** (0x18): an array of {task, layer 0-4, flags}. Flags: 1 active, 2
  closing, 4 dying, 8 dead, 0x10 unfocusable. The focus is the last active entry that
  is not unfocusable; a newly added task takes it. `update` runs the tasks layer by
  layer, including tasks added during the walk, then deletes dying ones and compacts.
  `depth` (1-based draw position) is where a widget's OT goes into the scene's.
- **Widget** (0x48, vtable 0x80074ad0), on the scene's list at layer 2:
  - `+8` parent (the focus when it was created), `+0xc` its own RenderContext (scene
    arena, 15-entry OT), `+0x10` a children list (its text boxes and cursor), `+0x14`
    its Cursor;
  - `+0x20` colour (0x01808080: the 0x01 makes its textured primitives raw);
  - `+0x24` / `+0x28` repeat / pressed, `+0x2c` flags (2 or 8 = no input), `+0x2e` /
    `+0x30` result state / result, `+0x36` the sound to play this frame, `+0x38` input
    delay, `+0x3c` / `+0x40` the child it waits on, `+0x44` / `+0x46` pending result /
    closing.

  `draw` [6] clears its OT, runs `frame` [9], then links the OT in at its depth (OT
  entries 0-14 go back to front). `frame`:
  - reports a finished child through [10] `on_child_done(id, child)`;
  - runs input [11] when focused;
  - updates the children;
  - plays the sound.

  Input maps the configured buttons: config + 0..7 as u16 pad masks. Buttons 0-3 call
  [13]-[16] (confirm, cancel, …), held → [17], pressed → [18], left/right → [19], and
  buttons 6/7 → [20]. The default handlers only pick a sound. `finish(code)` /
  `cancel()` set the result and close the widget.
- **Dialog** (0x50, vtable 0x800746e8): input off for 4 frames after opening; closes 6
  frames after `close()`.
  - **ConfirmDialog** (vtable 0x80074640): confirm finishes, cancel is ignored.
- **Panel** (0x98, vtables 0x80074898 / 0x80074888), a full-screen page:
  - a 16-frame fade in / out, done with a subtractive full-screen TILE at OT slot 10;
  - the help bar (a HeaderBox at y 200 that rebuilds every frame: the game logo opening
    and closing, button hints, the page name, member-switch arrows);
  - a StatCalc at `+0x48`;
  - member switching with buttons 6/7.
- **TextBox** (0x84, vtable 0x80074a78). It is a task in its owner's children list,
  drawing into the owner's OT at `+0x60` (5; the frame and body go at slot - 1).
  - Elements (0x24 bytes): 1 ASCII string (8x8 SPRTs of the window font, palette codes),
    2 a text line, 4 an icon (POLY_FT4, optionally animated), 5 a character sprite, 6-8
    numbers.
  - A 4-frame open / close animation clipped with DR_AREAs.
  - Subclasses: HeaderBox (help bar gradients), TitledBox (red title strip),
    MessageBox (sized to a message, "next" arrow).
- **Cursor** (0x8c, vtable 0x80074570), the hand. It holds up to 8 groups: grid
  (`0x80069a80`, wrap / scroll modes) and list (`0x80069c78`, scrolling a quarter row a
  frame). Only the active group reads input. Moves animate over 4 frames, with a sound.
  The `0x800` flag ("moved") makes the owner call [12].
- **TextRenderer** (0x114). It rasterises each line once with the CPU into a 4-bit
  image:
  - glyphs are ORed through colour-expansion tables at sub-pixel offsets;
  - the image goes into a CVram rectangle (two for lines of 200 px or more), through the
    upload queue;
  - each line is drawn as POLY_FT4s plus a one-pixel drop shadow (CLUT 0x7a11).

  Codes: 0x3000 pen x, 0x4006 font switch, 0x4009 an expansion message, 0x400c a button
  icon (the session's icon map).
- **CVram**: a best-fit rectangle allocator over the four free VRAM regions. Freed
  rectangles merge with free neighbours.
- **Sprites** (0x3c): characters from the cached 0xbd2 pack.
  - Frames are uploaded on demand into a 48-cell VRAM cache region and evicted when not
    drawn this frame.
  - All sprites of one animation share the first one's timer each frame.
  - Directions 1-5 are mirrored.

### Input flow

`frame_begin` reads the pad (current / repeat / pressed, lowest bit only, nothing while
the input lock runs). Only the focused widget — the last one opened and still active —
copies the scene's repeat and pressed in `input` [11]. It turns them into handler calls
for its cursor group and buttons. A child widget reports back through the parent's
[10]. The page loader sees the page widget's result and moves between pages.

## The field's menus

### Opening (field_update 0x8004c334, state 0)

Before its state switch, state 0 checks the save point: `f+0x38` = the leader touches
actor 0x80 (`actor_touches_save_point`), sfx 0xc when it becomes set. Then a menu opens
when the menu button (config +4) is pressed or a script asked for one (`g_menu_request`
0x80076196), `field_flags[0x93]` is 0, and (for the button) the script VM is not in
wait states 1 / 3 / 8, script flags bits 0xc / 0xe are clear, `field_flags+0x10` (the
controlled actor) is not -1, no event runs (VM +0x109c == -1) and `field_flags[0x26]` is 0.
A request skips those checks. Opening:

1. sfx 2 for the button; with script flag 0x14 the request becomes 3 (query) with
   `g_menu_param` = the party slot of `field_flags+0x3c`;
2. fade_push(3, 0xf); voice streaming off; `preload_save_table`; on the world map the
   terrain block load is cancelled and its streaming flag cleared; load queue reset;
   `preload_drop_incomplete`;
3. `f+0x3c` = `field_flags+0x10`, then -1; state 3;
4. unless request 5: the leader's position (>> 12), `+0x0c` and facing into game_state
   +8..+0x18 / +0x21 (in a vehicle also its parking slots +0x26c.. / +0x274..);
5. BG vtable +0x1c (`bg_suspend` / `world_bg_suspend`), its animation copy freed;
6. a new `MenuLauncher` (0x800752d4) started with kind = request or 1.

### State 3

Waits for the launcher's loads and the fade. Then:

- `field_set_option_a` = (world map or save point) | (`field_flags[0x25]` << 1),
  option b 0; the pad's vblank handler is removed; ResetGraph;
- handoff 0xe = game_state (0x2a0) + script flags (0x170); game_state +0x1c (music id)
  becomes its index in `k_music_ids` (0x800750cc) for the save;
- the runner of the request: 2 `menu_run_shop` (result to script global 0), 3
  `menu_run_query`, 4 `menu_run_member`, 5 `menu_run_save`, else `menu_run_camp`;
- the launcher is freed; pad handler back; `field_setup_screen`, PutDispEnv / PutDrawEnv,
  flip, clear;
- result 1 (a game was loaded): sound clips 1-8 released, field_flags cleared, game_state
  and script flags from handoff 0xe (music index back to an id), sound stopped, disc
  check (0x800759a4), actors' view and sprite caches reset, the preload copy dropped,
  `field_load_map(game_state +0x1a, keep position, music 1)`;
- otherwise: clear colour from the scene (+0x6f..+0x71), `0x80076278` = 1 (the
  post-battle equipment check), the music id back, streaming on, `preload_restore_table`,
  actors' view (the BG tint kept) and sprite caches reset, `field_flags+0x10` back, state 0,
  the script's menu-close entry (VM +0x1198) run, request cleared, fade_push(1, 0xf)
  unless result 5 / 6; result 2 game over, 3 a battle (state 0x21), 4 a warp (state 0x1b),
  5 a world-map exit (state 0x11), 6 a teleport to map 0x2c0.

### Launcher and runners (0x80030470-0x80030c64)

`menu_launcher_start(kind)` picks four consecutive entries: 1 camp 0xba4, 2 shop 0xba8,
3 query 0xbac, 4 member 0xbb0, 5 save 0xbb4. They are the resource pack, a copy of the
class library (unpacked to 0x800d1b28), the start page's code (0x8007e000) and its data.
The runners call `menu_launcher_finish`, then build a session (start page 1 / 0xb / 6 /
0xf / 0x11) and a field scene. The field scene is the title's with `screen_init`, an
arena of the free heap minus 0x14000, and the Callways task updated every menu frame
(songs and money ticks continue). `menu_run_camp` turns the session into its result
(above); the Callways hooks 0x800319d0 / 0x800319d8 run around it.

The field library is the title's code at fixed distances in 0xba5: +0x6c5e8 for text
boxes, widgets and the VRAM allocator, +0x69cf0 for cursors and panels, +0x69ddc for
sprites, +0xa5850 for the text renderer (`menu.csv` module `menu_0ba5`). Its data is laid
out differently (e.g. the sprite pose table title 0x80074b7c is 0x800e119c). The field's
entry cache is the sprite pack table 0x8007585c: the camp page finds the party's sprites
there. The save page (0xbb6) keeps the title page 0x11's constants 0x1afdc higher.

### Camp menu, page 1 (overlay 0xba6, 0xec bytes)

- Left: the command box (0x68 by 0xb8): the logo strip turned on its side, eight 20x20
  icons and the messages of group 3 (Specialty, Items, Equipment, Skills, Status, Options,
  Combat, Data) with the current one underlined, arrows after Combat and Data.
- Right: a box per party slot (at 0x58, 8 + 0x2e n; 0xe0 by 0x2e): the sprite, "HP"/"MP"/"LV
  n NEXT" ASCII with palette codes, the name, element kind 6 (HP / MP numbers and
  gauges) and kind 7 (experience to the next level and its gauge). They scroll with the
  member list.
- Bottom: TIME (config +0x10, minutes; updated from the clock each new second, the
  colon blinking), FOL (config +0x18), BATTLES (config +0x14).
- Specialty, Equipment, Skills and Status first pick a member (a second hand cursor on
  the member list, session +0xe / +0x10); the member needs a character and, except for
  Status, no status bits 0-2. Items and Options go straight on. Combat opens a
  three-choice dialog (pages 8-10), Data a Save / Load dialog (Save greyed unless option
  a bit 0; page 0x11 with session +0x15 = the choice).
- Its `on_close` takes the item data loaded in the background (entries 3000-3003: item
  pack, code for later pages at 0x80088000, item records the inventory points at (+0x820),
  the resident 0xbbb) and uploads the item icons.

The page handler (`menu_page01_update`) maps choices 0-12 to pages 2, 3, 4, 5, 6, 7,
0x11, 8, 9, 10, 0xb, 0xc, 0xd. The field's page 0x11 handler pops one level more after a
save or load, so the camp menu closes with it.

## Page 0x11: save / load

These classes are listed in `menu.csv` with their vtables.

1. **SaveMenu** (Panel). The slot chooser ("Slot 1 / Slot 2", greyed when unusable),
   the "no memory card" notice, and the help text "Load" / "Save" (group 2 message
   0x11 + load mode). Confirming a usable slot:
   - unformatted: FormatFlow (saving) or a notice (loading);
   - files or free blocks: SaveList;
   - otherwise a notice.
2. **SaveList**: one box per position (free blocks + saves), three rows visible. It
   starts on the last used position (config + 0x198 + slot * 4) or the newest save.
   Each entry shows:
   - the number badge;
   - "Star Ocean 2 data not found." / damaged data, or the save's window colours;
   - the party sprites (anim 9);
   - TIME h:mm and BATTLES (config + 0x14).
3. **LoadFlow**: a CheckDialog (YES / NO), plus an overwrite check when saving over a
   save. Then ProgressDialog, then the result: sound 0x12 and finish, or an error notice.
   After a load it restores config + 0x198 / 0x19c and sets the slot's last position.
4. **ProgressDialog**: builds the file (saving) or starts the read (loading). The bar
   follows the card driver's state. Then `save_finish` / `load_finish`.
5. On success SaveList and SaveMenu finish. SaveMenu applies the loaded play-time clock
   (config + 0x10 minutes × 60), the vibration and sound-mode settings, and sets session
   +0x16 = 1. Loading also turns the scene's input off.

## Save file

One block, file name `BASCUS-94421S02-Snn` (nn = 01-15). Read from `save_build`
(0x80065fec), `save_serialize` (0x80066a94), `load_finish` (0x800669b4) and
`save_summary_check` (0x80066c98). Checked against the user's DexDrive save: both
checksums match. A save built from a RAM dump with the same code loads in the original.

| Offset | Size | Contents |
|---|---|---|
| 0x000 | 0x200 | PS1 header: `SC`, 0x13 (3-frame icon), 1 block. +4 Shift-JIS title `ＳＯ２　nn　h:mm　Name　ＬＶn` (position, play time, leader, level). +0x60 icon CLUT, +0x80 three 16x16 icon frames. The title's copy of the icon (0x80067b9c) is zero; the field's page has the real one. |
| 0x200 | 0x80 | Summary (the load list reads only this). See below. |
| 0x280 | 0x100 | config + 0x1a0: the unlock flags. At boot the title ORs the blocks of every save together. |
| 0x380 | n | Packed image: u16 count, then the zero-run stream (below). |

Summary (+0x200):

| Offset | Contents |
|---|---|
| +0x00 | "STAR OCEAN 03/01" |
| +0x10 | u32 byte sum of the summary, computed with +0x10 and +0x18 zeroed |
| +0x14 | u32 byte sum of file[0 .. length), computed with 0x210..0x219 zeroed |
| +0x18 | u16 0x5555, written by the card driver |
| +0x1a | s16 length = 0x380 + count + 2. The load reads this many bytes, rounded to 128. |
| +0x1c | play time in minutes (config + 0x10) |
| +0x20 | battles (config + 0x14) |
| +0x24 | 4 window colours (config + 0x30) |
| +0x34 | 8 × {u8 character id (0 when the slot is empty), u8 party + 2, s16 level (party + 0x26)} |
| +0x54 | save counter: highest on the card + 1; config + 0x24 is also incremented |
| +0x58 | leader name, 8 bytes of card codes (`text_to_card_codes`) |

Zero-run code: nonzero bytes are literal. A single zero is 0. A run of k zeros
(2 ≤ k ≤ 256) is `0 0 k-2`. Unpacked, the image is 0x1b88 bytes:

| Offset | Size | Block |
|---|---|---|
| 0x0000 | 0x1a0 | config (handoff 2; the unlock flags come from 0x280) |
| 0x01a0 | 0x300 | party (handoff 3, 8 × 0x60) |
| 0x04a0 | 0x680 | records (handoff 4, 8 × 0xd0) |
| 0x0b20 | 0xc28 | inventory (handoff 6), saved with its check bytes (+0x824..+0xc23) zeroed. Both saving and loading regenerate them with a new random key (`inventory_check_byte`, 0x8003b944). |
| 0x1748 | 0x440 | the field snapshot (handoff 0xe): game_state 0x2a0 (state block + 0x220) and script flags 0x170 (+0x8c0). The field fills it when field_update enters state 3 (camp menu); `field_create_state_block` restores it. |

Loading through Continue in the port gives handoff blocks identical to the original's
(`ss_game_tests` `menu_load_save_matches_original`; the inventory check bytes are random
by design).

## Differences in the port

- Card I/O and CD loads finish at once. The slot names turn white sooner, the progress
  bar fills faster, and the field starts about 50 frames earlier than in PCSX-Redux.
- The original spends CPU frames rasterising text (about 6 vblanks when the list opens).
  The port does not, so screens that open and animated sprites run a few vblanks
  earlier.
- Remaining pixel differences on static screens are one-step gouraud body pixels, the
  known GPU item in docs/CHECKLIST.md.
- `sprite_cache_cell` keys cells by host address, not by the low 16 bits of the PS1
  address, so the original's rare key collisions do not happen.
- The field's pages other than 1 and 0x11 are not ported: the page loader logs them and
  goes back to the page below (the camp page reopens; a shop or member menu closes).
  Field results 4 and 5 (warps) and the query page's play-time object are not ported.
- `SS_SAVE_ANYWHERE=1` sets option a bit 0 (saving allowed) for testing the camp menu's
  Save.
- `--load-save` / `SS_LOAD_SAVE=[slot:]position` loads a save without the menu
  (`load_save_without_menu`). It has the same side effects as the load flow: StatCalc,
  last-used position, clock, sound mode.
