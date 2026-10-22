# The credits program (#3107)

Archive entry #3107 (SLZ1, 85 KB unpacked, at 0x8002f810 like every program). Map 581
carries a byte copy as its section 6 (0x800756f4); table A 0x2d tears the field down and
hands that copy to `boot_run_main_program`. Map 581's init calls it once the endings are
over (script flag G#0x2ee set, G#0x2ef clear: it sets 0x2ef first), so the credits run
between the private endings and the final "fin." screen (#3105, A 0x40), which map 581
shows when the field comes back. Catalog module: `credits_3107`. Port:
`engine/src/game/credits/credits_program.cpp`, A 0x2d in
`engine/src/game/field/field_section6.cpp`.

## Layout of the image

| Range | What |
|---|---|
| 0x8002f810-0x8002f8dc | entry, program base class (`program_create`, set / get system), base vtable 0x8002f8c4, program pointer 0x8002f8dc |
| 0x8002f954-0x800303fc | the screen library (arenas, ordering tables, buffers): an older build than the title's (the buffer index and the overflow callback are gp-relative), so only some functions match the title with relocations masked. The credits never call it |
| 0x80030400-0x8003078c | the screen library's data |
| 0x80030790-0x80031c40 | the title's movie player (title 0x8004f014-0x8004fa94) without the prologue, then libpress (DecDCT*, MDEC_*, DecDCTvlc): the same code word for word |
| 0x80031c44-0x800427f4 | "error : %d", the movie vtable 0x80031c54, libpress's VLC table, the movie globals 0x80031cb4-0x80031ce2 |
| 0x80042e64-0x80044230 | the credits themselves (8 functions) |
| 0x80044230-0x800446c4 | layout jump table, vtables, the cue list, the card script, colours, globals |

## How it runs (`credits_run`, 0x80042ea0)

1. Clears VRAM x 0x1e0-0x3c0 (it sets up two DISPENVs there, 0x80044680, and never
   shows them).
2. Sound: entries #0x983-#0x985 in one read (the ending theme, SLZ, then two banks);
   the first bank is loaded, #0x986 is read (a third bank, loaded only once the music
   reaches bar 82, `Sound +0x80`), the second bank is loaded, the theme is unpacked into
   a 0xc000-byte buffer and started.
3. The name images (#4503 = 0x1197, SLZ) into a buffer kept at program +0x20.
4. The movie player: `movie_create(.., 0, 0, 320, 240, interlaced, 24-bit)`, then the
   mode the title never uses, `frames_in_ram = 3`, with a 0xa8c00-byte buffer: three RAM
   frames of 320x240x3 bytes. A frame is stored as 20 columns 16 pixels wide (0x2d00
   bytes each, rows of 0x30 bytes): pixel (x, y) is at `frame + y * 0x30 + (x & 15) * 3 +
   (x >> 4) * 0x2d00`, the order the MDEC writes them. `movie_update` decodes into RAM
   frame `draw_buffer` (0-2) and loads nothing into VRAM.
5. A vblank handler (12 bytes, vtable 0x80044248, in the pad module's list) and
   `DrawSyncCallback(0x800436f0)`.
6. The loop (no VSync in it; it spins):
   - the cue list (0x80044290, `{s16 op, s16 arg}`), unless a wait runs: 0 end, 1 open
     movie `arg` once no movie plays (`draw_buffer = 2` first), 2 wait `arg` vblanks, 3
     `sound_fade_out(arg)`;
   - `movie_update` while a movie plays; at its end `movie_close`;
   - a finished movie frame: the current card is composed onto it, it becomes the next
     upload, `draw_buffer = (draw_buffer + 1) % 3`;
   - no movie frame coming (state not 2), nothing waiting to upload and the last upload
     on screen: RAM frame 0 or 1 (alternately) cleared to black and the card composed
     onto it, and that is the next upload;
   - bar 82: the third bank.
7. The end: `loader_cancel`, `sound_stop`, the handler and the callback removed, the
   movie deleted, buffers freed, `SetDispMask(0)`, sound bank #8 (the field's) loaded,
   the field program (#3100) read and started. The field binds the state block that
   survived (handoff 9); nothing else is handed over (A 0x2d freed the inventory stash
   and the battle setup), so it loads `game_state +0x1a` (581) fresh, and map 581's init
   now runs the final screen.

Cue list: wait 720; then the eleven movies #4594-#4604 (disc 2 only), each followed by
two waits (the first runs while the movie plays, the next one opens only when it has
ended); a 960-frame fade-out of the music before the last waits (360 + 660). About 21400
vblanks (6 minutes).

### Display: the vblank handler (0x80043aac) and the DrawSync callback (0x800436f0)

The handler counts down from 4. When the buffer uploaded last (0x800446b9) is not the one
on screen (0x800446b8) and the countdown is below 1, it adds 4, calls `SetDispMask(1)` at
the first flip (0x800446b0 counts 2 down), shows that buffer (`PutDispEnv` of the
movie's DISPENV: buffer 0 is VRAM y 240, buffer 1 y 0) and records it. Then, if a frame
waits (0x8004428c) and the last upload is on screen, it uploads RAM frame 0x80044284 in
20 `LoadImage`s of 24 x 240 halfwords into the buffer 0x80044288. When the GPU has
finished them, the DrawSync callback stores that buffer as uploaded and toggles
0x80044288. So the display flips at most every fourth vblank (15 fps, the movies' rate)
and only to a complete frame.

### Cards (`credits_compose` 0x80043714, `credits_draw_image` 0x80043eac)

The card script (0x80044324) is a list of cards: a layout (0 centred on (160, 112), 1 top
left at 16, 2 right-aligned at 308 top, 3 bottom left (bottom at 224), 4 bottom right),
then codes: `n > 0` stacks the next `n` images of #4503 below each other, `n < 0` switches
to colour `0x8004466c[-n]` (gold 0x40c0ff by default and for -1, blue 0xffb060 for -2,
pale 0xfff0c0 for -3; every second colour code also leaves a 16-row gap), -16 ends the
card. x is rounded down to a multiple of 4. The script ends with a card whose layout
reads -16, which is the low half of the colour table's first word.

An image is `u16 w, h` and then a u16 per 4 pixels, 4 bits per pixel: 0 leaves the frame
alone, 1-15 is a brightness index. For each colour `credits_set_palette` builds the colour
at 15 steps (`c * (15 - i) / 14 * 16`, index 1 full, 15 none) and `credits_set_level`
scales it by the card's level (4.12) and gives each index a weight for the frame under
it (`max(0, 0x1000 - (i - 1) * level * 5 / 56)`: index 1 adds the colour to the frame,
higher indices darken it towards an outline). The pixel is then one GTE GPL (sf=1):
`MAC = colour * level`, `IR0 = weight`, `IR = pixel << 4`.

Each compose steps the card's fade: +0x200 to 0x1000 (8 frames), held 207 vblanks
(0x800446c0, counted by the handler), -0x200 to 0, then a 5-vblank gap and the next card.

## Checked against PCSX-Redux

Disc 2 in Redux needs two hooks in `scratchpad/fx_worker/fx_ref.lua` (the title's New
Game reads the prologue cue lists #4504-#4507, which disc 2 lacks): `loader_is_disc`
answers yes, `prologue_update` answers "skipped" and `loader_load_sync` of #4504-#4507
returns at once. Map 581 with G[0x79c] = 0x24000 then runs the credits at once (Redux
spawn 0x80042ea0 at vsync 2226; port vblank 2). Redux spends 190 vblanks in the loads
the host does at once; after that a Redux vblank k corresponds to port vblank k - 189
(main loop) and a Redux capture k to port capture k - 190.

- Composes, DrawSync callbacks and flips: the same pattern (a black frame composed in
  the vblank after each flip, flips every 4).
- Movie opens: 911, 2531, 4272, 6552, 9612, 11772, 13303, 14563, 15703, 19003, 19963
  against Redux 911, 2532, 4272, 6552, 9612, 11772, 13304, 14564, 15704, 19004, 19964.
- Each movie's first frame within a vblank of Redux's (DecDCTin 938, 2538, 4277, ...),
  its end within 1-3 (Redux's movie_close 2140, 3924, ...); the credits end (sound bank
  #8) at 21583 against 21584.
- Name cards on black are pixel-identical; movie frames under the names are within 2-3
  LSB (MDEC rounding). Where the flip falls a vblank apart, a card in mid-fade can be one
  0x200 step apart (offset 9000).
- Hand-back: Redux reads #3100, then `field_load_map(581, keep 0, music 0)` and the
  final screen's entries (#4335 = 0x10ef, #2675); the port runs map 581's `the_end`.

The movie start latency matters here: PCSX-Redux seeks 3 us a sector of distance plus
1 ms (cdrom.cc computeSeekDelay), and the first sector comes about 30 ms after that. The
port's movie player now keeps the drive head (`Loader::head`) and seeks from it; with a
single latency for every movie (the title's opening's) the credit movies after the first
(30 sectors from where the previous one stopped) started 16 vblanks late.

## Port notes

- `game::Movie` is shared with the title and the field; the frames-in-RAM paths of
  `movie_dma_callback` and `movie_update` were ported for it.
- The pad module's vblank handler list is ported (`VblankHandler`,
  `pad_add_vblank_handler`): `vsync_callback_with` runs the handlers first, as
  0x80013578 does. The title's prologue counter still catches up from the runtime's
  vblank count instead.
- The host GPU is synchronous, so the DrawSync callback is timed: the uploads finish
  kUploadVblanks after the vblank that queued them (the main loop may still see the old
  value right after the handler, as on the console), and always before the next vblank.
- The loop spends console time for the frame clear and the compose (cycle estimates per
  4-pixel group and pixel). It idles itself (`MovieGlobals::caller_idles`, so
  `movie_update` never waits) after its own checks: to the player's next event
  (`movie_next_event`), the uploads' end or the next vblank, and not at all after a cue
  was read or when a vblank passed during the pass (the console loop re-checks at once).
- The movies are on disc 2 only; if the drive holds disc 1 the port swaps in disc 2
  (the field asks for disc 2 long before the endings on the console).
- Debugging: `SS_CREDITS_CUES=1` prints every cue read with the console time; SS_TRACE
  prints `credits_compose`, `credits_drawsync`, `credits_flip`, `credits_movie_frames`,
  `credits_movie_end`.
