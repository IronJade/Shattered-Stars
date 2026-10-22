# Handoff: state of the port (2026-10-21)

**Update (session resumed):** the actor items below were already done by the agent
(ActorSystem created, CamScript, brightness hook, actor services wired); all tests
pass (76 + 34). M4 is reached: map 7's opening plays through and matches the original
from the fade-in on, the script moves to map 8, and the D-pad walks Claude. Captures
now use the display's visible lines (224 on the field), as PCSX-Redux's do.
Continue works (menu agent): title → load screen → field; `SS_LOAD_SAVE=N` skips the menus.
The save file layout is in the menu agent's notes (to be merged into docs/notes). The user's
real save (analysis/saves/dex_north_city.mcd) is on disc 2 / world map 2: it needs the
world-map port and disc 2 support (agent running).
In progress at this commit (agents running): the battle program's combat (phase 1 —
entry, loaders, render, field round trip — is done), the world map (game/field/worldmap*),
the shared party/stat library (game/common/party.*), field effects.
Debug options: `--map N` starts a new game (Claude) directly on field map N;
`SS_BATTLE_STUB=1` makes the not-yet-ported battle program "win" at once and return to the
field; `SS_TRACE=1` logs VM milestones (fades, windows, map loads) and encounter settings
with vblank stamps. Map 10 (Shingo Forest) gives a random battle after ~1800 vblanks of
walking left/right. ASan build without root: `dnf download libasan` (x86_64), `rpm2cpio |
cpio -idm` into a scratch dir, symlink `libasan.so -> libasan.so.8.0.0` there, configure
`engine/build-asan` with `-fsanitize=address` and `-L<dir> -Wl,-rpath,<dir>` in the
linker flags. `gdb.minimal` is installed for backtraces.
PCSX-Redux note: its default memory cards (~/.config/pcsx-redux/memcard*.mcd) were
created by our own reference runs on 2026-10-21 and are written by every run; point
reference runs at scratch cards.

Work was stopped mid-session. This records what every line of work had, so the next
session can pick up without re-deriving anything. Last commit: `f8b4403`; everything
below is uncommitted on top of it.

## Build and tests at the stop

- `cmake --build engine/build` builds clean (system SDL3 3.4.16 from `SDL3-devel`;
  PipeWire / PulseAudio / ALSA audio).
- Last full test run (before the actor agent's final edits): `ss_tests` 76 / 76,
  `ss_game_tests` 23 / 23 with `SS_DISC1` set to the disc 1 `.cue`. Rerun both first.
- New Game → map 7 (Claude) runs headless: the BG fades in, the "Planet Milocinia"
  notice runs, Ronixis's window opens with the right text; only the leader actor is
  drawn (the script's actors still go to stand-ins, see Actors below).
- Reproduce (headless, never pops a window):

  ```sh
  IN="850:start,854:none,951:cross,957:none,1251:cross,1257:none,1551:cross,1557:none,1851:cross,1857:none,2151:start,2157:none,2451:cross,2457:none,2700:start,2706:none,3000:start,3006:none,3300:start,3306:none"
  SS_MEMCARD1=/path/to/scratch/card.mcd ./engine/build/shattered-stars --headless --frames 4600 \
    --input "$IN" --capture OUTDIR --capture-frames 3500,3600,3700
  ```

  (Use a scratch memory card copy, never the user's; `--reference-dither` for
  comparisons. The original's frames for map 7 are in `analysis/reference/firstmap`
  and `analysis/reference/message`.)

## Finished lines of work

| Area | Where | State |
|---|---|---|
| SPU, libspu, sound driver, streamed clips | `psx/spu`, `runtime/libspu`, `runtime/libapi`, `game/boot/sound`, `platform/audio` | Done; title music, effects and prologue clips match PCSX-Redux. No XA / CD-DA on either disc. Tools: `tools/redux_sound.lua`, `compare_sound_events.py`, `compare_audio.py` |
| Event-script VM | `game/field/script_*.cpp`, `vm_services.*` | Done; map 7 / 18 openings run the original's instruction sequence; 1928 script runs clean |
| Message windows, fades, notice, text panels | `game/field/message*`, `fade.*`, `notice.*`, `text_panel.*` | Done; pixel-identical to PCSX-Redux except 1-step gouraud body pixels (GPU) |
| Field overlay 0xc1d catalogue | `docs/notes/field_overlay_c1d.md`, catalog module `field_ovl_c1d` | Done (108 functions) |
| Battle program #2129 catalogue | `docs/notes/battle.md`, catalog modules `battle`, `battle_ovl*` | Done (931 + 187 functions, 405 globals); port order in battle.md §8 |
| Field program integration (main session) | `field_program.cpp`, `map.cpp`, `bg.cpp`, `load_queue.*`, `music.*`, `stash.*` | Subsystems created in the original order; VM services wired to the real field / message / fade / BG / sound / music / preload code; field_update states 0 (no-encounter path) and 1; field_run's return-from-battle path; inventory stash; BG suspend / resume / tint / script animation calls |

## Stopped mid-work (resume these)

### Actors (agent stopped while fixing tests)

Files: `game/field/actor_base.*`, `actor_field.*`, `actors.*`, `camera.*`,
`collision.*`, `sprite_pack.*`, `sprite_render.*`, `tests/test_field_actors.cpp` (all in
CMake; the tree builds). Its last step was "gate the table-dependent tests on the field
image (SS_DISC1) and fix the triangle winding" — run `ss_game_tests` and fix what fails
there first.

Still to do (asked of it, not confirmed done):
1. Create the ActorSystem in `field_create_subsystems` (`field_program.cpp`, at
   `todo("field_create_subsystems: ActorSystem")`, before `fade_construct`).
2. CamScript (camera pan queue, overlay `camscript_construct` 0x8008e100 /
   `camscript_reset` 0x8008e1c0): replace `bg_helper_create()` in bg.cpp / map.cpp.
3. Fill `g_fade_targets.actors_set_brightness` (fade.h).
4. Wire the "Actors" and "Camera script" sections of `vm_services.cpp` to the real
   functions (pattern: `TRACE(...)` + real call, stand-in fallback when no ActorSystem).
5. Then compare map 7 against `analysis/reference/firstmap` frame by frame.

### Menu framework and Continue / load screen (agent stopped while writing a page module)

Files (NOT in CMake yet, may not compile): `game/common/menu_core.*` (task base,
window lists, entry cache, background loader), `menu_scene.*` (scene, session, page
loader), `menu_gfx.*` (VRAM rectangle allocator, upload queue, menu text renderer,
animated face sprites), `menu_widget.*` (widget classes, hand cursor), `menu_stats.*`
(StatCalc party stat recomputation). Header comments give the original address ranges
(title 0x8002fff4-0x8003c000 + 0x80067d4c-0x80074fe4; field 0x8002f9f8-0x8003d000; menu
overlay 0xba5 at 0x800d1b28). It was about to fix "the GTE setup and the header
leftovers, then write the page-0x11 module" (the load-game page). It wrote no catalog
CSV: the names live only in those headers' comments — catalogue them
(`analysis/menu_catalog/menu.csv` → `tools/merge_catalog.py`) before going further.

Goal unchanged: title Continue → load screen (`title_program.cpp:397` still logs "not
ported") → field with the save's snapshot (handoff 0xe, already handled by
`field_create_state_block`). Test card: copy (never modify)
`~/.config/pcsx-redux/memcard1.mcd` / `memcard2.mcd`.

## Next steps, in order

1. Rerun the tests; finish the actor items above; map 7 with all actors vs the original.
2. Walking on map 7 (leader control, collision, camera follow) → M4.
3. Menu agent's files into CMake, catalogue them, finish the Continue screen.
4. Remaining field_update states (camp menu 3, post-battle stat windows 0xf / 0x10 with
   party_scan_equip_effects 0x80030dec, which also sets the Callways walk flags), FieldPanel,
   SceneRenderer. Callways, poison steps and map music are done.
5. Battle port following `docs/notes/battle.md` §8.

## Conventions to keep

- No game code or assets in git; data is read from the user's discs at run time.
- Headless only (`--headless`, `SDL_VIDEO_DRIVER=offscreen`, `SS_VRAM_PNG`); never pop
  windows; never play audio audibly in tests (`SDL_AUDIO_DRIVER=disk` / `dummy`).
- Don't touch `~/.config/shattered-stars/config.ini` from tests (`--reference-dither`,
  `SS_MEMCARD1`).
- Catalogue every reversed function in `docs/catalog` (LF line endings; write CSVs with
  `lineterminator="\n"`); prose goes in `docs/notes/`.
- Parallel agents: separate build dirs (`engine/build-<name>`) and Ghidra project dirs.
