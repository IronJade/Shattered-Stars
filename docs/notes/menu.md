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

### The query gauge (0x80034514-0x80034c08)

Script A 0x35 starts a minigame (the 0x801f9800 overlay object; script flag 20), and
while it runs the menu button opens the query page (request 3) for the member the script
names. `menu_scene_run` creates a 0x10-byte gauge for such sessions (session +0x31) and
the field's Panel frame draws it while a window has the focus, at (0x10, 0xa4) in OT
entry 8:

- "TIME" and m:ss, five minutes counting down from 0x80076310 (clock_seconds; -1 after
  map loads and A 0x35, set by the first draw once script flag 19 is on: A 0x36);
- "PRESS" and script variable +0x38, a percentage;
- each with a bar (x + 0x2a, 80 px for full, 12 high): POLY_F4 0x2a8020ff added over
  0x2a507090 averaged.

The text is the 8x8 font in the window art (tpage 0x16, v 0xc8-0xdf, CLUT (0x110,
0x1e0 + palette): 2 for the labels, 1 for the values), drawn by 0x80034c08.

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

## Shops (page 0xb, overlay 0xbaa)

Script B 0x6d asks for menu 2 with the shop id (`g_menu_param`); the launcher reads
0xba8-0xbab and the session starts at page 0xb with the id as its argument. The help bar
shows group 0xc message id - 1 (the shop's name).

- **Shop** (Panel, 0x2b8 bytes): command box Buy / Sell / Settle (order shops 0x5a-0x5e:
  Order / Settle), a member box, the info box (group 7 text of the command), a help text
  box and the money box (Fol, Purchase, Sales, Balance = Fol + sales - purchase).
  Its lists are built once the item data (entries 3000-3003) is in: the stock from
  `k_shop_stock` (shop 0x58: items 0xc4-0xdb whose flag 0x194 + item is set) and the
  inventory's priced items to sell (shops 0x39 / 0x58: owned items 0xc4-0xdb).
- **ShopList** (Dialog): a scrolling list of name, price, "x" and the count in the cart.
  Left / right change the cart (selling up to the count held, buying up to 20 held);
  square toggles the info box between the item's description and its figures;
  confirming asks "Account settlement: is this acceptable?" (a negative balance gets a
  notice instead). Hovering an item loads its file into the item viewer and marks which
  members could wear it and whether it is better (arrows by ATK for weapons, AC else).
- **Prices**: item field 0 (above 9,999,999 counted in tens), item 0x171 + 250 a minute
  of play, item 0x1da negative; selling: * field 0x2d % plus the best skill 4 * 3 % and
  the super specialty 6 bargain; buying: the bargain minus flag discounts 0x2c8 (25 %) /
  0x2c9 (5 %).
- **Settling** (Settle, or Yes in the list): sold items leave the inventory, bought ones
  are added with the new bit, money = the balance, session +0x1e = bought. Order shops
  write up to ten orders to game_state +0x250 due at -(clock + delay by specialty 0xc
  level) in +0x294, which the field turns into a delivery event (menu result 4). Any
  booking clears the one-shot bargain flags 0x2c8 / 0x2c9 / 0x2df / 0x2e0.
- Page 0xc of the same overlay is a debug panel with a cheat list (all items x5, SP max,
  HP / MP, status, level max); 0xd an empty panel.

Verified against PCSX-Redux on a save made by the port in front of Mr. Hearn (Arlia,
map 26, 200 Fol): buying three Sandals, toggling the figures, settling, then selling one
on a second visit leave config and inventory (entries, counts, new bits, recent list)
identical to the original's (captures kept in the git-ignored `scratchpad/menu/`).

## Items (page 3, overlay 0xbbe)

The field handler (0x80035580) pushes the 0x18b0-byte page and maps its result: 1 page
0xf (item creation with the specialty in session +0x1c), 2 page 0x10 (super
specialty), 3 close, 4 page 0x13 (session +0x12 = the item's parameter).

- **The page** is a list of two columns over the whole inventory ((last used + 2) / 2
  rows, 7 shown). Each entry shows a kind icon (pack messages 0x10 usable, 0x12 other,
  0x13 equipment), the name (colour 3 while it is in the recent list) and "x count".
  - The info box is a picture box: the viewer's text pack carries a 4-bit picture of
    the name (w, h, pixels). It is uploaded to (0x200, 0) and drawn with clut 0x7b90,
    additive, over a subtracted shadow, with a rule under it. The description sits
    below.
  - Square toggles the figures (Panel info mode).
  - A count box shows "position / total".
  - The page saves the VRAM it uses for the picture and restores it on exit.
- **Confirm** runs the item effect (below):
  - Kind 4 (no member) is used at once.
  - Kinds 1-3 open the member picker: eight boxes with sprite, LV, HP / MP gauges.
  - Kind 5 is used and ends the page.
  - Equipment opens the equip picker: arrows compare ATK (weapons) or AC (armour), as
    in shops. Equipping it puts the worn piece back in the inventory.
  - Using an item when everyone is down closes the menu (session +0x1b, camp result 2).
- **Triangle** opens the commands:
  - 8 sort orders (icons from page data block 4): recent, battle use, the game's order
    (page data block 2), weapons by ATK, armour by AC, accessories, price, field use.
    A stable merge sort writes the inventory back with new check bytes.
  - Specialty: the 11 item creation specialties a member who can act knows.
  - Super Specialty.
  - Valuables: script flags 0x40-0x5d, names group 0xd, descriptions group 0xe.

### Item effects (entry 3001, 0x8008b9b0-0x8008ce7c)

A 0x100-byte object built on StatChange. `kind` and `usable` are dry runs. They save the
random generator's table and index and put them back, so trying an item costs no draws.
`apply` makes the changes and returns the sound (0x13 heals / status, 0x28 other, 0x16 a
character's own item, 0x23 a knock-out).

The effect is item field 0x28:

| Field 0x28 | Effect |
|---|---|
| 1 / 2 | HP % (field 0x19) on one member / everyone |
| 3 / 4 | MP % on one member / everyone |
| 5 | HP and MP % on one member |
| 6, 0xa | Revive (0xa: everyone, full) |
| 0x15 / 0x20 | 1 / 3 random items (field 0x2b >= 0x1e) |
| 0x21 | Half the time an item, else a battle (game_state +0x264 = 0x227) |
| 0x25-0x27, 0x3b-0x43, 0x47, 0x52, 0x5d-0x62 | Statuses with small heals |
| 0x2a | Swaps HP and MP |
| 0x2f / 0x4c | Config marks |
| 0x35 / 0x4b | Script flags 0x26 / 0x24 |
| 0x3d / 0x3e | Session +0x33 / +0x32, read by the item creation pages |
| 0x60 | A teleport on maps 0x2ba-0x339 (session +0x35, camp result 6) |

Other items act by id:

| Item | Effect |
|---|---|
| 0x1b9 | Plays the song in entry 0xa9b |
| 0x1d0-0x1d5 | Record flags of characters 6 / 9 |
| 0x1e1 | One experience point short of the next level |
| 0x1e3 | The current map's region flag |
| 0x1e4 | 1000 Fol a level for a member who is down |
| 0xa6-0xbb | Skill books: level + 1 and the level bonus 0x8008b530 |
| 6-0x11 | Page 0x13 with a parameter |
| 0xc4-0xdb | Config marks |

Heals add skill 0x12 for some ranges of items. A character's own item (table
0x8008d216) heals fully instead.

Verified against PCSX-Redux from a copy of the Hearn save with five items added and
Claude at 40 / 130 HP (`scratchpad/menu/modsave.py`; `itemsref1/2`, port `items_p4/5`).
The runs used a Sweet Syrup on Claude, sorted by price, toggled the figures, equipped
Sandals, and opened Valuables and Specialty.
- Inventory, config, party and records match.
- The frames match except for the model's turn phase, the help bar's logo phase and the
  CD time before pictures appear.

## Equipment (page 4, overlay 0xbc4)

Picking a member in the camp menu opens the page. The field handler (0x80035688)
pops it whatever its result.

- **The page**:
  - The seven slots, labelled from the window art (tables 0x80081258 / 0x80081264), each
    with its piece. The cursor is a 1 x 7 grid with an underline.
  - The member box adds "EW ON / OFF" (record +0x3a bit 0) and the level.
- **Confirming a slot**:
  - The page stores the attributes and opens a list: "none" first, then the inventory
    entries of the slot's type (0: 1 / 2 / 7, 1-4: 8-11, 5-6: 0xc) that the member can
    wear (item field 4).
  - The cursor puts each piece on, so the page shows ATK / AC / HIT / AVD / MAG "before
    → after" (colour 2 worse, 3 better).
  - Confirming a piece turns EW off, returns the old piece and takes the new one.
    Cancelling puts the old piece back.
- **Triangle** opens the options:
  - Best equipment: slots 0-4 empty into the inventory, then the highest ATK weapon
    and AC armour per slot go on.
  - EW ON / OFF, switched with left / right.
  - The options do not open while flag 0x2db is set and the slot holds item 0x2e7.

Verified against PCSX-Redux on the items card (`equipref1`, port `equip_p1`): Sandals
put on by hand, EW switched back on, then best equipment.
- Party, records, config and inventory match.
- The frames differ only in the sprite's phase, the cursor's glide and the item
  file's CD time.

## Status (page 6, overlay 0xbb2)

The camp menu opens it for the member picked. Script menus use it too
(`menu_run_member`, a single member, help flags 0x506 instead of 0x1507). The handler
(0x800357b0) goes on to page 0xe when the page finishes (confirm, not for a single
member).

- **One status box** holds:
  - the slot labels and equipment names;
  - the sprite and the name. Page group 0xd's message starts with text code 0x4006 n:
    in the field it draws character n's first name (game_state +0x28 + (n - 1) * 0x14)
    in the default font, while the title's copy of the renderer skips it;
  - the class (group 0xb; Rena with flag 0x2dc uses 0xd);
  - HP / MP, LV, EXP / NEXT with the level gauge;
  - the eight abilities and the favourite food (only once known: record +0x3a bit 1;
    table 0x80081612);
  - ATK / AC / HIT / AVD / MAG.
- **The portrait** is entry 0x1170 + character: a 160 x 160 16-bit TIM. Each 0x800
  sector after the first carries 0x10 bytes of header. It streams into VRAM (0x200, 0)
  as it is read, and the box draws it rising 4 rows a frame behind the rows uploaded.
  It drops out when the member changes. The page saves that VRAM and restores it.
- **Triangle** opens the talents: record +0x20 bits, group 4 names, group 5
  descriptions.
- **Square** (with the item data) opens the elements and status resistances of the
  equipment.

Verified against PCSX-Redux on the items card (`statref1`, port `stat_p2`): the page,
talents and resistances match except for the sprite's phase and the help bar. The
portrait rises later in the original, because of CD time.

## Skills (page 5, overlay 0xbc6)

- **The list** has the 46 skills in id order, two to a row, with gaps for skills the
  member neither knows nor may learn. A skill can be learned once its book is read:
  script flag 700 + its group of five in table 0x80081168.
  - Each row shows the name, the level and the SP for the next level: table 0x80080f92
    + skill * 10 + level, less twice skill 0xb's level, at least 1.
  - Battle skills 0x21-0x2e are drawn in colour 3, or colour 4 when switched off.
- **Confirm** pays from the member's SP (record +0x1a) and raises the level (keeping its
  sign). The level's bonus follows: the copy of 3001's skill_level_bonus.
  - A RaiseNotice dialog takes the specialty and super specialty levels before and
    after the raise.
  - For each one that changed it shows a message box (group 0xc, with expansions for the
    character, the specialty and the level): learned (0 / 2) or rose (1 / 3).
- **Square** flips a known battle skill's sign (off / on).
- **Triangle** opens the specialties and super specialties the member has started, with
  a dot per skill (or specialty) needed. A complete one opens its details. Triangle
  there opens the talents, the same dialog as the Status page's.

Verified against PCSX-Redux with copies of the items card that add SP, skills and flag
700 (`skillref1-4`, port `skill_p1-4`). The runs covered raises, the on / off switch,
both notices ("rose to 2", "learned"), the specialty list and a detail.
- Records, party, config and inventory match.
- The frames differ only in the sprite and the help bar.

### Skill guilds (script library; research of the agent "skillsanity", 2026-10-25)

A guild is no shop page: every map script carries the same library, with eight menu
routines (a choice window of three or four sets with their prices, then the purchase routine,
map 80 0x6958). The purchase: the set's flag set already, "Sorry, but it seems that you already
know that skill"; Fol (`get_counter18`) below the price, "not enough money"; else the flag is
set (indexed store), the Fol taken, "Thank you very much", and the one-shot bargain flags
0x2df / 0x2e0 cleared. A price goes through 0x442c first: plus (0x2df) or minus (0x2e0) the
price x super specialty 6 level x 3 %. Buying a set makes its skills learnable by every member
(`skill_book_allowed`: the skill's group in `k_book_groups` 0x8008d2d8, flag 700 + group).

| Set (flag) | Price | Skills (ids) | Guilds |
|---|---|---|---|
| Knowledge 1 (700) | 300 | Mineralogy 5, Herbal Medicine 6, Recipe 0x11 | Cross, Clik, Linga, Central City |
| Sensibility 1 (701) | 400 | Courage 0x1c, Patience 0xc, Esthetic Sense 8, Good Eye 0x12 | Cross, Clik, Central City |
| Technique 1 (702) | 400 | Whistling 0x13, Copying 0x1e, Sketching 1, Kitchen Knife 0x10 | Cross, Clik, Herlie, Central City |
| Combat 1 (703) | 400 | Spirit Force 0x29, Below the Belt 0x21, Strong Blow 0x22, Cancel 0x2b | Clik, Herlie, Lacour, Armlock |
| Knowledge 2 (704) | 1500 | Musical Notation 2, Biology 0xe, Tool Knowledge 4 | Herlie, Hilton, Linga, Central City |
| Sensibility 2 (705) | 1600 | Playfulness 0x1a, Danger Sense 0xd, Perseverance 0xb, Poker Face 0x1d | Herlie, Hilton, North City |
| Technique 2 (706) | 1600 | Mech Knowledge 0x1f, Craft 7, Animal Training 0x14, Writing 9 | Hilton, North City |
| Combat 2 (707) | 1600 | Flip 0x23, Gale 0x2c, Feint 0x25, Mental Training 0x26 | Hilton, Lacour, Armlock |
| Knowledge 3 (708) | 2700 | Mental Science 0xf, Piety 0x19, Fairyology 0x17 | Linga, North City |
| Sensibility 3 (709) | 2700 | Functionality 0x1b, Radar 0x18, Effort 0xa | Lacour, Armlock |
| Technique 3 (710) | 3600 | Music Instrument 3, Metal Casting 0x15, Scientific Ability 0x16, Mech Operation 0x20 | Linga, North City |
| Combat 3 (711) | 4500 | Counterattack 0x24, Parry 0x2a, Body Control 0x28, Motormouth 0x27, Provocation 0x2d | Lacour, Armlock |

- The guild maps and their menus: Cross 80 (K1 S1 T1), Port of Clik 123 (K1 S1 T1 C1), Herlie
  147 (K2 S2 T1 C1), Hilton 173 "Skill Power" (K2 S2 T2 C2), Linga 190 (K1 K2 K3 T3), Lacour 219
  and Armlock 316 (S3 C1 C2 C3), Central City 258 (K1 K2 S1 T1), North City 264 "Boondocks
  Office" (K3 S2 T2 T3). Names: the explainer texts; ids: the specialty table 0x8007197c and
  the battle code (0x21 ignores DEF, 0x22 knock-back, 0x23 flip, 0x24 counter, 0x25 accuracy,
  0x26 attack, 0x27 casting time, 0x28 stun guard, 0x29 DEF, 0x2a evasion).
- The 46th skill, **Float** (0x2e), is in no set: the Cave of Trials level 11 prize (map 807)
  gives it to the active members (special command 0x12).
- **Starting skills** (`init_*`, record +0x5c + skill): Claude Scientific Ability, Functionality;
  Rena Kitchen Knife, Recipe 4, Good Eye (Cooking); Celine Mineralogy, Craft, Esthetic Sense 5,
  Scientific Ability 2, Fairyology 2 (Metalwork, Alchemy); Bowman Herbal Medicine 5, Biology 4,
  Mental Science 2 (Compounding); Precis Functionality 3, Mech Knowledge, Mech Operation
  (Machinery); Leon Mineralogy 3, Scientific Ability 4, Fairyology (Alchemy); Opera Craft, Metal
  Casting, Functionality, Mech Operation 3 (Customize); Ernest Herbal Medicine 5, Patience 5,
  Whistling 4 (Survival); Chisato Copying 4 (Reproduction); Dias, Ashton and Noel none.
- **Killer moves and spells** (record +0x3c + index = learned): the learn tables of #1981
  (`levelup_learn_skills`) teach 69 moves and 82 spells by level; a member joins knowing
  exactly the table's entries up to the join level (Celine 8, Bowman 25, Dias 35, Precis 15,
  Ashton 17, Leon 30, Opera 21, Ernest 25, Noel 40, Chisato 40). Outside the tables: Claude's
  Phase Gun (the opening's ext 0x73, taken back at Salva by ext 0xfc); Precis's Holo-holograph
  (the arena Private Action, map 304, or item 464 Holoprojector), Barrier (465 Plasma Zap-stick)
  and M. Super Beam (466 Mech Launcher); Opera's Healing Star (467 White System), Laser Bit (468
  Black System) and Hyper Launcher (469 Green System); and the Cave of Trials' learn chests:
  Leon's Extinction (map 740), Ashton's tri-Ace (798), Celine's Meteor Swarm (799). Items
  464-469 act through the item effect's `character_flag`; 465-469 come only from Machinery.

- **The port, Archipelago (agent "skillsanity2", 2026-10-25; docs/ap/seed_format.md "Skillsanity",
  "The join level").** With a seed's `skillsanity` skills on, `skill_book_allowed` answers from the
  skill items received, not the flags (a purchase still sets its flag and is its set's checks);
  with special on, the learn tables (levelup_learn_skills), ext 0x73, the items 464-469 and
  special 0x12 (Float) record their learn point when the seed lists it instead of teaching, and a
  joining member keeps only the first move (the table's first entry) plus the moves received.
  Open world and hub: a new member (party_slot_init) takes the party's median level (up by
  levelup_apply, down by the growth table's mean gains, floored at the heroes' level-1 values).
  Neither without a seed. The shop list draws such a slot's skill or move by name through
  TextRenderer::draw_ascii (the 0x4006 name path: the default font via the card's codes).

## Options (page 7, overlay 0xbc8)

The page has two screens of options. Moving the cursor into a group of the other screen
scrolls every box by 0xd8. Only the current row's choices are lit (kind 8 highlights;
bit 15 of each value means not lit).

- **Screen 1:**
  - message speed 1-8 (game_state +0x118), with a sample in group 7 that advances at
    that speed;
  - sound output (config +0x44);
  - the four window corners: a dialog with RGB sliders (gradients and markers) that
    recolours a sample box;
  - three two- or three-way settings (config +0x49, mapped 0 / 1 / 2 to rows 1 / 2 / 0;
    +0x4a; +0x4b).
- **Screen 2:**
  - the eight button assignments. In these rows a pressed button is assigned and the row
    that had it takes the old one; cancel is ignored there;
  - vibration (config +0x46).
- The values go back into the config when the page is destroyed, together with the
  vibration switch and the sound mode.
- The analog and vibration-test rows need a DualShock. The flag +0xe8 that would add
  them is never set in this overlay.
- The page makes a second cursor after its boxes, so it draws on top of them.

Verified against PCSX-Redux (`optref1`, port `opt_p2`): two speed steps, a sound mode,
three red steps and one green step on a corner, a +0x49 change, and square assigned to
the first action.
- Config, party and records match.
- The frames differ only in the sample and highlight phases.

## Combat (pages 8-10, overlay 0xbca)

The camp page's Combat submenu (three choices, always starting on the first) opens one
of three pages. All three are in overlay 0xbca (data 0xbcb).

- **Tactics (page 8).** Eight member boxes, two columns of four (slots 0-3 on the left).
  The grid runs row by row and table 0x800808e0 maps it to the slot. Each box shows the
  member's sprite and current orders: group 4 message class * 6 + record +0x38, with the
  class from table 0x800808e7 by character. A dialog lists the class's six orders.
  Choosing 5 (stand by) from other orders keeps them in record +0x39.
- **Party Order (page 9).** The same boxes with name, LV, HP / MP and status bars. The
  first pick makes a second cursor and the second pick swaps the two slots and their
  records. The swap is undone (the second cursor stays) if no front member can fight:
  a character id above 0 with status & 7 clear. Members away from the party (negative
  ids) count as empty. Cancel drops the second cursor or closes the page. When the page
  closes:
  - front slots whose character changed get their voices reloaded (a field hook);
  - bench members lose record +0x3b, +0xce and +0xcf (their pairing and pending killer
    moves; see party_update_link_combos);
  - config +0x41 (the controlled member) moves to the first filled slot if its slot
    emptied.
- **Formation (page 10).** A list of the 13 formations (group 9; the current one lit), a
  description (group 10), and a 3D preview: the front four's sprites on a floor grid in
  perspective. The preview uses the GTE (RTPT / AVSZ3) with its own 0x400-entry ordering
  table spliced into the box's entry. Places come from table 0x800808f4, the floor from
  0x80080a94. A marker of three frames, eight vblanks each, is over the controlled member.
  Confirm sets config +0x40. Button 2 (triangle by default) opens a dialog that picks
  the controlled member (config +0x41) from the front four; empty slots are refused.

Every field menu ends with the equipment rescan (`show_battle_results`; post_battle.cpp).
Its StatCalc runs without the item data, so stats that the camp page's StatCalc
recomputed with item bonuses (record +0xa, slot +0x4c, record +4) drop back to their
base. A dump at the end of field_menu_update comes before that rescan, so comparisons
with a party wearing such items need `SS_MENU_DUMP_DELAY` (below).

Verified against PCSX-Redux (`combref2`, port `comb_p2` / `comb_p2d`). The card is the
items save with the north city save's party copied in, two members dead and pairing
bytes set (`modsave.py --party-from`). The run covers:
- formation 2 and a refused then accepted leader (Ashton);
- a swap refused for leaving nobody able to fight, two accepted swaps (Ashton to the
  bench), and a cancelled pick;
- Rena's orders set to stand by.

Config, inventory, party, records and game_state match (with a 60-frame dump delay). The
frames match.

## Specialty (page 2, overlay 0xbbc)

The camp page's Specialty command picks a member, then shows what that member can do
outside battle. The first entry depends on the character (table 0x80084057): Killer Move
Setting for fighters, Spell Setting for Rena, Celine, Leon and Noel. After it come the
field specialties the member knows: Oracle (2), Musical Talent (3), Practice (8), Scout
(9) and Familiar (12). A member with only the first entry goes straight to it.

- **Killer Move Setting.** Two slots, record +0xcc / +0xcd, filled from the moves learned
  (record +0x3c + i) that are in no slot. Triangle with accessory 0x15c opens Link Combo
  ON / OFF:
  - ON picks a partner among the other front members (or partner 0x10 with script flag
    0x5d) into record +0x3b. Choosing someone ends the Link Combos that member had.
  - With a partner there are two sets: the second set is record +0xce / +0xcf, and the
    member box shows "LC ON".
  - A member who is someone else's partner cannot set moves.
- **Spell Setting.** The character's spell list (record +0x3c + i is the spell times
  +1 / -1). Square switches a spell off or on for battle. Field spells 0x2f-0x36 can be
  cast on a member:
  - heals of 100 / 800 / 2800 HP, or 1200 / 2200 for everyone, plus the caster's slot
    +0x46 / 4 and half its practice with the spell;
  - a revive at half HP;
  - cures of statuses 8 and 0xe.
  The MP cost comes from table 0x8008406f, halved or cut to two thirds by equipment
  effects (stat_calc_mp_cost).
- **Oracle:** a random message among the first min(level * 5, 0x24).
- **Musical Talent:**
  - Compose takes a Feather Pen (0x2c) and an owned instrument (0x2d-0x34), each of
    which has two songs. A new song (script flags 0x2f-0x3e) comes with a level-based
    chance.
  - Play takes item 0x35 and plays a composed song through Callways (the song's sound
    entry is read in the background), at a negative level without the talents.
- **Practice:** record +0x3a bit 2.
- **Scout:** script flags 0x22 / 0x23 with game_state +0x266 / +0x267 = level * 5 (more
  or fewer encounters). Without the talents, the sign is random.
- **Familiar:** item 0x1a2 with the talent: a chance of level * 8. On success it sets
  game_state +0x288 and session +0x12 = 0x5a + (level - 1) / 2, and the page goes on to
  page 0xb (that shop). Not in places with script flag 0xc or 0xe.

Using a specialty can wake one of the talents it wants (record +0x20). The chance is
table 0x80083fb2 by character and talent, and a woken talent gives 100 SP.

Verified against PCSX-Redux in four runs (`kms`, `spx`, `spell`, `fam`; see the
continuation log). Config, inventory, party, records and game_state match. With the
generator set to the original's state when the menu opens (`SS_MENU_RNG`), the generator
also ends in the same state. Frames differ in sprite, help-bar and model phases, and the
original reads the viewer's model about 20 frames later.

## Item creation (pages 0xf / 0x10, overlays 0xbc0 / 0xbae / 0xbc2)

The Items page's Item Creation and Super Specialty Skills commands open these pages. The
choice goes in session +0x1c. Page 0xf loads overlay 0xbae for specialties 4 (Customize)
and 11 (Cooking) and overlay 0xbc0 for the others (1 Art, 5 Identify, 6 Metalwork,
7 Authoring, 10 Compounding, 13 Alchemy, 14 Survival, 16 Reproduction, 17 Machinery).
Page 0x10 loads overlay 0xbc2 for the eight super specialties (Master Chef, Orchestra,
Comprehension, Come on Bunny, Publishing, Identify All!, Blacksmith, Reverse Side).

All three overlays carry the same Panel (0x108 bytes, compiled into each) with one class
per specialty. The page runs a loop of steps, by state:

| State | Step |
|---|---|
| 0 / 2 | pick materials (scrolling lists of owned items) |
| 4 | the create / stop prompt |
| 6 | the creation animation |
| 8 | the result message |
| 10 | the failure message |
| 12 | talent waking |
| 14 | again |

- Each class supplies its lists, how the result and the chance are worked out ([39]),
  and what is used up ([43]). The chance usually grows with the level, plus a bonus:
  Callways song modifier 13, the creation bonus, and +20 for owning the specialty's
  helper item.
- Config +0x20 counts the tries and +0x28 the successes.
- A result may turn into its rare kind (30%; the item pack's blocks 3-5).
- Without the specialty's talents the level counts as 0. A creation can wake one of them,
  as on the Specialty page.
- Super specialties use only the selected member, skip the failure and talent steps, and
  show the party in the member box. Several are not creations:
  - Orchestra plays song entry 0xaae when 4 instruments and 8 songs are owned.
  - Comprehension switches script flag 0x25.
  - Come on Bunny works only on the world map; success sets session +0x30 and page 0x10
    then closes the menu.
  - Identify All! sets flag 0x2e0 or 0x2df once.
  - Reverse Side also lowers one of config +0x58 / +0xe8's marks.
- Cooking in a query session (the cooking contest) moves PRESS (field flags +0x38). It
  ends when the TIME / PRESS gauge's five minutes run out.

The animation is the CreationViewer (entry 3001, 0x8008a364, 0xa8 bytes), an item viewer
moved to the middle of the screen:
- It loads the four items' models (material 1, material 2, the other result, the result),
  with textures at (0x200 + i * 0x40, 0x100) and CLUT rows 0x1e0 + i.
- It builds four morph pairs. For each vertex of one model it finds the nearest vertex of
  the other, both pushed along their normals (morph_pair_prepare).
- It blends them over 300 frames (model_blend): material 1 to the result and back, then
  the other result to the result and back.

Verified against PCSX-Redux runs of every specialty and super specialty: config,
inventory, party, records, game_state and the generator state after the run match.
Cooking's query path was run without the contest's script (`SS_POKE` / `SS_MENU_POKE`:
script flag 0x14, field variable +0x3c = Claude, PRESS +0x38 = 32). The query page
opens on Cooking for him, and PRESS (32 -> 27), the data and the generator match. The
TIME / PRESS gauge's frames match.
- The port runs start from the original's generator state (`SS_MENU_RNG`) and dump with
  the menu open (`SS_MENU_DUMP_FRAME`).
- The morph data matches byte for byte.
- In the port the animation starts about 77 frames earlier: its four model files load at
  once.

## Page 0x13: pictures (overlay 0xbcc)

The Items page opens it for items 6-0x11, the portraits Art can make. Their effect sets
session +0x12 to the picture, and the picture's entry is 0x117c + n. After 31 frames the
page leaves the menu's frame loop and shows the picture in its own loop (picture_show):
- The picture is 320 x 1200 in 15-bit colour, 75 strips of 320 x 16, shown on an
  interlaced 320 x 480 display, 28 strips at a time.
- Up / down (or the left stick) scroll it with momentum, up to 16 rows a frame.
- Buttons 4 / 5 (L1 / R1) jump to the top / bottom.
- Cancel fades it out.
- Strips coming into view are read from the CD at most three at a time, into two
  buffers. They go into 30 VRAM rows at (0x140, slot * 16) and fade in from 8 to 0x80.
- Five VRAM areas of the menu are saved before and put back after, along with the menu's
  screen setup. The page then closes 31 frames later.

Verified against PCSX-Redux: scrolling, the jumps and closing match frame for frame
once the strips are in. The port reads the strips at once, so they fade in sooner. Data
unchanged.

## Page 0xe: renaming (overlay 0xbce)

Cross on the Status page opens it for the character shown. It is the title's New Game
name entry again: the overlay carries its own copy of the keyboard and name-box windows
and of the text renderer, with data from the page pack (entry 0xbcf: strings, font, a TIM
whose CLUT is the text palettes, and the 4-bit art).
- The page loads the character's picture (entry 0xbd3 + character, 15-bit, 192-232 x 240)
  first. It then leaves the menu's frame loop for its own (rename_screen) on a 320 x 240
  double buffer at y 0 / 0xf0, and fades in from black by subtracting a full-screen tile.
- Layout: the "Naming" heading sprites, the picture on the left, a cyan-to-black gradient
  behind, the name box at (0xa0, 0x30) and the keyboard at (0x50, 0x6e).
- The name box starts from the stored name, parsed into seven slots ('@' toggles kana; a
  voiced mark joins the character before it).
- Keyboard: a top row of Capital / Lower / OK / Delete / Return, then five rows of ten keys
  in two sets (digits, letters, punctuation). The hand slides between keys over 3 frames.
- Controls:
  - Cross presses the key under the hand; a full name sends the hand to the top row.
  - Circle deletes.
  - L1 / R1 (buttons 4-7) switch the set.
  - Select puts the stored name back.
  - Start or OK commits the name; an empty name is refused (sfx 7).
  - Return puts the stored name back and leaves.
- Committing writes the name to record +0x24 and game_state +0x28 + (character - 1) *
  0x14. For the first two characters it also sets config +0x42 / +0x43, the renamed
  flags that pick battle voices without their names.
- Four VRAM areas are saved before and put back after, along with the menu's screen setup.
- Original bugs (docs/notes/original_bugs.md):
  - M1 (fixed): the original compares Claude's name with "Crawd".
  - M2 (fixed): a full name sends the hand to Delete rather than OK.
  - M3 (kept, harmless): the VRAM it saves is not all the VRAM it overwrites.

Verified against PCSX-Redux before the fixes. Two runs: one typing (backspace, digits,
both sets, a full name and Delete, Select, Start), and one keeping "Claude".
- The data (config with +0x42, records, game_state) matches.
- Frames: 4x4 block means match, apart from the cursor glow and colour-cycle phase (the
  screen opens at a different frame). Pixel by pixel, the subtractive keyboard frame
  differs by the known order of dithering and blending (docs/notes/gpu_accuracy.md).
- The Status page afterwards matches.

## The item viewer (entry 3001 at 0x80088000)

Field panels with items (shops, items, equipment) create it at (0x118, 0x4a) depth 0x18.
For the hovered item it streams the item's file from archive entry 0x1189 (sector and
length from block 2 of the item pack) into a five-slot cache. A file holds two SLZ
blocks: a text pack (font, messages: the description the panel shows in its info box)
and a model:

| Header word | Block |
|---|---|
| 0 | triangles, 0x20 bytes: u16 flags; vertex, normal, colour and UV indices (+2, +0xe, +0x14, +0x1a) |
| 1 | vertices (8 bytes: x, y, z) |
| 3 | normals |
| 4 | colours (RGBC words) |
| 5 | UVs (u16) |
| 6 | texture (or 0): 8-bit image to (0x200, 0x100), 256-colour CLUT to (0, 0x1e0) |
| 7 (low half) | triangle count |

Triangle flags: 0x10 textured (POLY_GT3, else POLY_G3), 0x80 semi-transparent (mode bits
from 0x60), lighting 4 (one normal) / 8 (smooth) with 1 (one colour) / 2 (three) through
NCCS / NCCT under light matrix 0x8008d2b8, else the table colours. Triangles go into the
viewer's own ordering table by OTZ (range z * 0x40 +- 0x140), which is spliced into the
owner widget's entry 9. The model turns through six phases (0x800889ac).

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
- The field's pages other than 1-0xb, 0xe-0x11 and 0x13 are not ported: the page loader logs them and
  goes back to the page below (the camp page reopens; a shop or member menu closes).
  Field results 4 (warp) and 5 (bunny call) go to `field_world_menu_result`
  (field_world.cpp). The query page's TIME / PRESS gauge is ported (field 0x80034594)
  and drawn only on page 0xf.
- `SS_SAVE_ANYWHERE=1` sets option a bit 0 (saving allowed) for testing the camp menu's
  Save. For comparisons, `SS_MENU_DUMP=prefix` writes the config, inventory, party,
  records, game_state and field_flags blocks after each field menu (`prefix_config.bin`
  and so on).
  `SS_MENU_DUMP_DELAY=n` writes them n field frames later instead, after the equipment
  rescan. `SS_MENU_DUMP_FRAME=n` writes them at the menu's frame n, with the menu still
  open. `SS_MENU_DUMP_OPEN=prefix` writes them as a menu opens. Each dump has a
  `_rng.bin` with the random generator as the original keeps it (index at 0x8002b464, then
  the table). `SS_MENU_RNG=file` loads such a state as a menu opens, since the field before
  the menu draws differently from PCSX-Redux.
- `SS_MENU_POKE="flag=N,var=OFF:VALUE,clock=N"` sets script flags, field variables
  (field_flags + OFF) and the query gauge's clock when the menu button is pressed. Flag
  0x14 with var 0x3c set to a character opens that member's query page (Cooking) without
  the contest's script. itemsref.lua's `SS_POKE` does the same in PCSX-Redux.
- The original stalls a few vblanks while unpacking the item data or rendering text; the
  port does not, so fades and loads run a few frames earlier.
- Background loads with a sector offset read straight from the archive entry
  (`Archive::read_from_entry`'s first_sector).
- `--load-save` / `SS_LOAD_SAVE=[slot:]position` loads a save without the menu
  (`load_save_without_menu`). It has the same side effects as the load flow: StatCalc,
  last-used position, clock, sound mode.
