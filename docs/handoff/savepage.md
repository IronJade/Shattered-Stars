# Save page fixes: verification log

Worker: savepage verification agent. Task: verify two field-save-page fixes
(`engine/src/game/common/menu_save.{h,cpp}`, `engine/src/game/field/menu_camp.cpp`
`field_save_page_open`) against PCSX-Redux: (1) Callways song stop, (2) the 0xBD2
character sprite pack load so a save slot with a character the current session lacks
shows that character's sprite. Background in `docs/handoff/effects.md` "Phase 8",
section "Not verified against Redux (tried, blocked; the next agent can finish it)".

**Status at hand-back: fix 2 done and CONFIRMED MATCHING. Fix 1 blocked on a specific,
reproducible menu-navigation snag in the test harness (not a fix question) -- see
below. Stopping here per the task's time-box for fix 1.**

Rules: never modify `analysis/saves/dex_north_city.mcd` or `scratchpad/latesave/card.mcd`
(read-only sources, confirmed untouched); everything else under `scratchpad/savepage/`;
headless only; PCSX-Redux only with `-cli` and a scratch HOME, killed by exact PID; port
runs use a scratch XDG_CONFIG_HOME under `scratchpad/savepage/`.

## Step 1: the combined card (done, verified)

`scratchpad/savepage/build_card.py OUT.mcd SRC1[:SUFFIX] SRC2[:SUFFIX] ...`: merges
single-save PS1 memory cards into one 128 KiB / 16-block card, copying each save's
directory-frame chain (state 0x51/0x52/0x53, `+8` next-block link, verbatim bytes --
see the bug note below) and data blocks, keeping the source's own block numbers when
free (both sources here use different blocks, 1 and 13, so no renumbering was
needed), and renaming the trailing `-S0N` directory-entry suffix on a name collision
(both sources are named `BASCUS-94421S02-S01`) so the card filesystem has two
distinct entries. Only the filesystem entry name and its checksum change; the save
image itself is untouched.

**Bug found and fixed**: the first version started each destination directory frame
blank (state + next-pointer only) instead of copying the source frame's bytes
verbatim, losing the filesize field (offset 4) and making the port's loader reject
the card ("SS_LOAD_SAVE: no valid save at that position"). Fixed: copy the full
128-byte source frame first, then patch only state/next/name and recompute the
offset-0x7F XOR checksum.

Output: `scratchpad/savepage/card_combined.mcd`. Verified structurally (all 16
directory-frame checksums correct; block 1 byte-identical to
`analysis/saves/dex_north_city.mcd`'s block 1; block 13 byte-identical to
`scratchpad/latesave/card.mcd`'s block 13 except the renamed directory entry) and
behaviourally (both the port's and the original's save list show both saves
correctly -- step 2).

**Important finding**: despite "early game" in the task description, config+0x4c is
1 for both source saves, and **both need disc 2 mounted** to load past the title in
Redux (confirmed: with only disc 1 mounted, the title/load screen runs fine but the
field then shows "Please insert DISC 2" and freezes at that prompt; no amount of
waiting progresses it). The port auto-discovers disc 2 next to disc 1 and loads
through transparently (logs `[loader] disc 2 inserted`); Redux has no in-emulator
disc-swap in this harness, so it must be launched with `-loadiso` pointed at the
disc 2 .cue directly from the start (matches how `scratchpad/fx_worker/run_ref2.sh`
handles other disc-2-only references). The "early" dex_north_city save is actually
past SO2's disc split story-wise even though it's a low map number (map 2).

**Second finding**: the Continue/load screen's slot list does **not** default its
cursor to slot 1 (the directory's first/lowest-numbered block) -- it defaults to the
most-recently-saved slot (here, slot #2, the late save, which has the higher save
counter). To load the early save specifically, the harness needs an explicit UP
press after the list appears but **before** the second scripted CROSS (which
confirms the highlighted slot) -- the press must land in roughly a 40-vblank window
and needs a longer hold (~10 frames) than a plain confirm CROSS (4-6 frames) to
register; see the working recipe below.

## Step 2: fix 2 (0xBD2 sprite pack) -- DONE, CONFIRMED MATCH

**Port**: `scratchpad/savepage/port/cap/frame00500.png`. Command:
```
XDG_CONFIG_HOME=scratchpad/savepage/port/xdg SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
SS_DISC1="<disc 1 cue>" SS_LOAD_SAVE=0 SS_SAVE_ANYWHERE=1 \
engine/build-savepage/shattered-stars --headless --memcard1 scratchpad/savepage/port/card.mcd \
  --frames 520 --input "60:triangle,66:none,160:up,166:none,220:cross,226:none,320:cross,326:none,420:cross,426:none" \
  --capture scratchpad/savepage/port/cap --capture-frames 0,10,...,520
```

**Redux**: `scratchpad/savepage/redux/out/shop00400_312x216_16.png` (after
`raw2png.py`). Working command (loads the EARLY save specifically, opens Data > Save
at the save point the save already sits on -- no poke needed, see below):
```
cd scratchpad/savepage/redux
HOME=$PWD/home SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_OUT=$PWD/out \
SS_LOAD_CAP=150 SS_UP_AT=55 SS_OPEN=250 \
SS_SHOP="100:UP:6,150:CROSS:6,250:CROSS:6,350:CROSS:6" SS_SHOP_END=400 \
timeout 900 /home/ironjade/.local/bin/pcsx-redux -cli -interpreter -debugger \
  -loadiso "<disc 2 cue>" -dofile saveref.lua -run
```
(`SS_UP_AT` / `SS_LOAD_CAP` are additions in my copy of the lua, not in
`scratchpad/fx_worker/cat3/redux/saveref.lua`, which I copied from and did not edit.)

**Result: the two captures match.** Both show:
- Slot #1 (the currently-loaded early save): Time 21:58, Battles 547, a 3-sprite row
  (Claude, Rena, Ashton -- the early save's own resident party).
- Slot #2 (the late save, map 671, whose party the early session never loaded): Time
  45:10, Battles 1045, a full 8-sprite row (Ashton, Noel, Claude, Chisato, Leon, Rena,
  Celine, Precis) -- **all render correctly, none missing or blank**, in both the
  port and the original. This is exactly the fix-2 behaviour.
- Slot #3: "Star Ocean 2 data not found." in both.

**A poke is unnecessary and actively harmful here**: an earlier attempt used
`SS_POKE="0x8007570c,0x68,1,1,="` (field_flags[0x68] = "on save point" / "saving
allowed") to force the Save option open, following the harness convention named in
the task. On this map, with this card, that poke reliably crashes PCSX-Redux
(`Unaligned address 0xffffffff in SH from 0x80089368`, a StoreAddressError, hang
after) right when TRIANGLE opens the camp menu. Both saves on this card are
genuinely saved AT a save point (SO2 always resumes exactly where you saved), so
`f->on_save_point` is already true on load and Data > Save is available with no poke
at all -- the plain save-point path from the task description works cleanly and is
what both working runs above use. The poke's crash looks like a harness-induced bad
write (forcing that field outside its normal init path), not a port-vs-original
difference; flagged here so nobody re-tries it on this card.

**Catalog**: both relevant rows (`menu_0bb6,0x8007e000,save_menu_create` and
`field,0x80031744,callways_stop_song_resume`) are already at confidence `confirmed`
in `docs/catalog/functions.csv` (set by the prior phase-8 agent from disassembly
alignment), so there is nothing to raise per the task's "if currently below
confirmed" rule. No catalog edit made.

## Step 3: fix 1 (Callways stop) -- investigated, BLOCKED, stopping here

**Mechanism confirmed reachable, in detail:**
- Catalog: `callways_song_active` (0x80031718) / `callways_stop_song_resume`
  (0x80031744, confirmed, caller #2998 save page).
- Cheapest path considered: `item_effect_play_song`
  (`engine/src/game/field/item_effect.cpp:374`, item id 0x1b9 "Music Box", effect
  case `0x1B9` in `ItemEffect::apply`) plays a Callways song at once, no Specialty
  gating. **Neither source save carries item 0x1b9** (checked both inventories by
  decoding the save image), so this path needs the item first (a map-75 gift, not on
  either card) -- not used.
- Actual path used: the Specialty menu's **Musical Talent > Play** action
  (`engine/src/game/field/menu_specialty.cpp`, `PlayList::on_confirm` /
  `MusicDialog`). Confirmed empirically that **every one of the late save's 8 party
  members already qualifies**: `docs/notes/menu.md` ("Play takes item 0x35 and plays
  a composed song through Callways... at a negative level without the talents" --
  so it plays even without the awakened-talent bonus); the late save's records show
  skill ids 2 and 3 (the two skills table 0x8007197C requires for specialty 3,
  confirmed by reading the field ELF's data directly:
  `objdump -s --start-address=0x80071970 --stop-address=0x800719b0
  scratchpad/fx_worker/catalog/f3100.elf`, table bytes at +6/+7 = 0x03/0x02) at level
  10/10 for all 8 characters, giving `char_specialty_level(3)` = 10 (Specialty Level
  6 as actually displayed in-game, confirmed on screen -- see below); all 16 song
  flags (0x2f-0x3e) are composed, all 8 instruments and the Feather Pen (0x2c x20)
  and Play item (0x35 x18) are owned. **Opening Camp > Specialty > (any member) >
  Musical Talent confirmed on screen** (`scratchpad/savepage/redux/out/
  shop00320_312x216_16.png`): the entries list shows Killer Move Setting, Oracle,
  Musical Talent (selected, description "A specialty command that composes and
  plays music."), Practice, Scout, Familiar, with Claude at "Specialty Level 6" (MP
  37) -- so this is readily reachable, a few menu steps, no item/skill gathering
  needed.

**Specific blocker**: inside `MusicDialog` (the Compose / Play two-item choice that
opens from "Musical Talent"), **a scripted DOWN press to move the cursor from
"Compose" (default-highlighted, index 0) to "Play" (index 1) does not register**,
reproducibly, across two separate attempts with different timing (DOWN at
shop-relative +70 and +120 vblanks after the entry-confirm CROSS, holds of 10 and 14
frames respectively -- both well past the UI's own settle time, which the earlier
Specialty-entries-list DOWN presses needed only ~40 vblanks and a 10-frame hold for,
successfully, twice, in the very same run). Both attempts instead left the cursor on
"Compose" and the subsequent CROSS triggered `ComposeList` (an instrument/song
picker that *writes* new state -- it consumes a Feather Pen and rolls a chance to
compose a new song, confirmed by the captured "Consumed Items: Feather Pen: 4" /
"~can't seem to come up with a good sound." in
`scratchpad/savepage/redux/out/shop00750_312x216_16.png`). No song ever played
(`callways_song()` -- a small addition to my lua copy reading `g_callways` (global
`0x800752e0`) `+0x1a` -- read back 0 at every checkpoint I logged: shop-relative
+700/+750/+1050/+1300/+1350 in the first attempt, +850/+1150/+1400/+1450 in the
second).

Traced the likely cause as far as the source without running a debugger session on
it: `MusicDialog::build()` calls `cursor->add_grid(&choice_, 1, 1, 2, 0, 0x28, 0,
0xE, box_)` (mode=1, 1 col x 2 rows) vs. the entries list's `add_grid(&choice_, 0, 1,
count_, ...)` (mode=0). Reading `Cursor::add_grid` / `GridGroup::move` in
`engine/src/game/common/menu_widget.cpp`, mode bit 0 only changes out-of-bounds
wraparound behaviour (mode 0 bounces at the edges, mode 1 wraps) -- it does not
change which input moves the cursor, and the arithmetic for a plain index-0-to-1
move (our case) is identical either way, so the *navigation logic itself* looks
correct and DOWN should work. The actual failure is more likely an input-routing
issue (e.g. `Cursor::active` / multiple registered `CursorGroup`s when a child
`Dialog` opens over a still-present parent list) that needs either a Redux
breakpoint on `GridGroup::move` / `Cursor::input` or stepping the port itself
(faster to iterate locally than through Redux's interpreter) to pin down. **This is
untested against the port** -- it's unknown whether the port's own menu reproduces
the same input-eating behaviour or not; that would be the fastest next check (if the
port's Compose/Play DOWN also fails to move the cursor, it's a live port bug
independent of this verification task; if it works fine in the port, the Redux
scripted-input timing/routing is still the open question).

**Not reached**: actually getting a Callways song playing in Redux, and therefore
the whole point of fix 1 (does opening Data > Save stop it) is **untested**. Per the
task's rule, stopping here rather than continuing to iterate -- this is a
test-harness navigation problem, not a port-vs-original difference, so there is
nothing to flag as "port differs."

**Time spent**: this item alone took several full Redux boot-to-field cycles (each
~2000+ vblanks of slow interpreter-mode boot plus the scripted sequence, several
minutes of wall time each) across multiple navigation-timing attempts, beyond the
task's one-hour guidance for this item; stopping per the task's explicit permission
rather than continuing to retry.

**What the next agent needs to finish this**:
1. Figure out why `MusicDialog`'s DOWN doesn't move `choice_` 0 -> 1 -- cheapest
   first step: try it in the **port** first (fast iteration, add a one-line debug
   print of `choice_` in `MusicDialog::on_cursor`, or use `--debug` interactively)
   rather than burning more Redux boot cycles on it.
2. Once Play is reachable and a song starts (confirm via the `callways_song()` lua
   helper added here, or the port's own state), capture before/after Data > Save on
   **the same late save** (map 671; it's already parked at/near a save point per
   `docs/notes/m8_audit.md` -- confirm `field_flags[0x68]` / `on_save_point` the
   same way as step 2, no poke needed if so) in both Redux and the port, and compare
   `callways_song_active` / the port's equivalent before and after the save page
   opens.
3. Reuse `scratchpad/savepage/redux/saveref.lua` (my copy; has `SS_UP_AT`,
   `SS_LOAD_CAP`, `SS_SHOP_STEP` (throttle shop captures), `SS_CW_AT` (print
   `callways_song()` at given shop-relative ticks) on top of the original) and the
   working load-screen recipe above (`SS_UP_AT=55` with a 10-frame hold, before the
   confirming CROSS at relative since=80).

## Files

- New: `scratchpad/savepage/build_card.py`, `scratchpad/savepage/card_combined.mcd`.
- Copied (not edited in place): `scratchpad/savepage/redux/saveref.lua` (+
  `SS_UP_AT`, `SS_LOAD_CAP`, `SS_SHOP_STEP`, `SS_CW_AT`/`callways_song()` additions),
  `scratchpad/savepage/redux/raw2png.py` (unmodified).
- Scratch: `scratchpad/savepage/port/` (card copy, xdg, captures -- `cap/frame00500.png`
  is the key fix-2 port capture), `scratchpad/savepage/redux/home/` (scratch HOME +
  `memcard1.mcd`), `scratchpad/savepage/redux/out/` (captures -- `shop00400_312x216_16.png`
  is the key fix-2 Redux capture; `shop00320_312x216_16.png` shows Musical Talent
  reachable; `shop00750_312x216_16.png` shows the Compose-instead-of-Play snag).
- No engine code changed by this agent (both fixes were already in place from the
  prior phase-8 agent: `engine/src/game/common/menu_save.{h,cpp}`,
  `engine/src/game/field/menu_camp.cpp`).
- Build: `engine/build-savepage` (RelWithDebInfo, matches `engine/build`'s cache
  settings), built clean, unmodified since (no engine changes to re-verify).

## Fix 1 (Callways stop): DONE, CONFIRMED MATCHING (leads agent, 2026-10-23 10:20)

Log: `docs/handoff/port_leads.md` lead 6. The blocker was not a MusicDialog problem:
- The original's dialog is vertical (Compose over Play music) and its grid call is the port's
  exactly (music_dialog_build 0x80080dd4: `cursor_add_grid(.., 1, 1, 2, 0, 0x28, 0, 0xe, ..)`);
  its vtable uses the shared Dialog input (slot [11] 0x800e05f0). No orientation or input bug.
- A rerun on the early save with the same timing (DOWN at shop-relative +520) moves the cursor
  to "Play the tune." and a second DOWN wraps back to Compose (mode 1), in Redux
  (`scratchpad/leads/cw/redux/out3/`) and the port alike; the earlier failure was a harness
  artifact (the captures in `scratchpad/savepage/redux/out/` likely came from another run).
- The late save confirms with CIRCLE and cancels with CROSS (its config), the early save with
  CROSS.

The check, late save (card_combined slot 2 in Redux, `SS_LOAD_SAVE=1` in the port), Ashton:
Camp > Specialty > Ashton > Musical Talent > Play music > The Song of Salvation, back out with
CROSS x 3, Data > Save:
- Redux (`scratchpad/leads/cw/redux/cwref.lua`, a copy of saveref.lua with breakpoints on
  callways_stop_song_resume 0x80031744 and callways_end_song 0x80031ad8; `out2/log.txt`):
  callways song 1 from +850; the save page's constructor calls callways_stop_song_resume at
  +1461 (ra 0x8007e168), which calls callways_end_song(c, 1); song 0 from then on
  (`out2/shop01430` the Save / Load submenu, `shop01500` the save page).
- Port (`scratchpad/leads/cw/port_run.sh`, a probe build with [pcw] traces; `port/m4/log.txt`):
  play_song id 1 at vblank 562; field_save_page_open at 1000 finds song 1 and calls
  callways_stop_song(c, true) (the stop with the field music resumed).
- In Redux a "secret talent 'Sense of Rhythm' worked" message follows the song (an RNG roll).
  The port's plain run has none (its generator starts from SS_LOAD_SAVE, without the title's
  draws); started from the original's generator at the menu (`SS_MENU_RNG=
  scratchpad/leads/cw/late_rng_open.bin`, Redux `out/before_rng.bin`), the port shows the same
  message after the same song (`port/m5/cap`).
