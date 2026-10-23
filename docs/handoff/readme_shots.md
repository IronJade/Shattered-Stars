# README/wiki documentation screenshots: continuation log

Owner area: `docs/images/`, the `SS_SHOT` debug option (renderer/frame-loop only), and the
screenshot scratch under `scratchpad/readme_shots/`. One-off task from the coordinator
(2026-10-23), not an ongoing area; nothing left running.

## DONE (2026-10-23): all 10 shots captured, optimised, committed to docs/images (pending commit)

### What was needed and wasn't there yet
The port had no way to save its *final OpenGL-rendered* frame headless. `--capture
DIR --capture-frames A,B,...` / `SS_VRAM_PNG` only dump the raw PS1 display buffer
(`psx::Gpu::display_to_rgba`) straight to PNG — no window scaling, no letterboxing, not what
a player's window shows — and a headless run never opens the real `platform::Window` at all
(`main.cpp`: `if (!headless) { window.create(...); rt.window = &window; }`), so there was no
GL context to read back from.

### The fix: `SS_SHOT` (new debug option, default off; documented in `docs/HANDOFF.md`)
`SS_SHOT="VBLANK:path.png[,VBLANK:path2.png,...]"`. When set, `main.cpp` opens a **second**,
separate `platform::Window` (`shot_window`, distinct from the normal `window`) purely to
render into; `runtime::Runtime` gets two new fields, `shot_window` and
`shot_frames` (`runtime/runtime.h`). At the listed vblanks only, `Runtime::present()`
(`runtime/runtime.cpp`) calls `shot_window->present(...)` with the exact same pixels /
letterboxing the real window would get (the border-framing logic was pulled out into a
shared local so both paths draw identically), then `Window::read_pixels` (new method,
`platform/window.{h,cpp}`) does `glPixelStorei`/`glReadPixels` and flips the rows (GL is
bottom-up), and the result is written with `util::write_png`. Keeping it a *second* window
(rather than reusing `rt.window`) matters: `Runtime::vblank()` ties real-time pacing and
input polling to `window` existing (`if (window || paced) { ...sleep_until 59.94Hz... }`), so
if a headless+SS_SHOT run set `rt.window` itself, every screenshot run would slow to real
gameplay speed. `shot_window` is never paced, never polled, so a headless run stays exactly
as fast with or without it.

When `SS_SHOT` is unset, `shot_frames` stays empty, `shot_window` stays null, and nothing in
`present()` / `vblank()` changes versus before — verified by diffing: the only behavioural
difference possible is additive (an `if (shot_window)` branch that's never entered).

**Bug found and fixed along the way (not mine originally, but only `SS_SHOT` exposed it):**
`platform::Window::~Window()` called `SDL_Quit()` unconditionally. That's fine with exactly
one `Window` object per process (the normal case), but `SS_SHOT` is the first thing in this
codebase to construct a second one in the same process — even a *default-constructed*
`Window` that never had `create()` called still ran this destructor at scope exit. Two
`Window`s both calling `SDL_Quit()` tears everything down twice (second one operates on an
SDL state that's already gone) and segfaults. Fixed with a `live_windows()` reference count
in `window.cpp`: only `Window`s that actually called `SDL_Init` (tracked by a new
`initialized_` member) participate, and `SDL_Quit()` only runs once the last one is destroyed,
regardless of destruction order. Repro before the fix: any windowed (non-`--headless`) run
with `SS_SHOT` unset still crashed at exit once `shot_window` (never `create()`'d) existed as
a local in `main()` — caught by `ss_game_tests`' `game_windowed_run_writes_its_log` (RAM
dump showed `run.code == -11`, i.e. SIGSEGV). Fixed, then that test (and everything else)
passed again.

### Files touched (engine)
- `engine/src/main.cpp`: `SS_SHOT` parsing (`rt.shot_frames`), the `shot_window` object and
  its `create()` call (1280x960, windowed, vsync off), top-of-file doc comment. Stayed clear
  of `--version` and the areas near it that other agents were editing (re-read the file right
  before each edit; verified after with `grep -n SS_SHOT`).
- `engine/src/runtime/runtime.h` / `runtime.cpp`: `shot_window`, `shot_frames`; `present()`
  refactored (pulled the border-framing math into a shared local, `window` and `shot_window`
  both consume it — behaviour-preserving for the `window` path, checked by the full suites);
  `vblank()`'s `present()` trigger condition extended.
- `engine/src/platform/window.h` / `window.cpp`: `Window::read_pixels`; the `SDL_Quit`
  reference-count fix (`live_windows()`, `initialized_`).
- `engine/src/platform/gl.h`: added `ReadPixels` to the loaded GL function table and the
  `PACK_ALIGNMENT` constant (both missing before; only `TexImage`/`TexSubImage` existed).
- `docs/HANDOFF.md`: one bullet in "Debug options" for `SS_SHOT`.

### Verification (engine/build-shots, own build dir, -j8)
`ss_tests` 78/0, `ss_game_tests` 110/0 (regrew from 78/0 while other agents' unrelated work
landed in between; both at 0 failed on my tree), `tools/smoke_field_events.sh` 18/0. All
green with the fix in; without it, `ss_game_tests` showed exactly the one new SIGSEGV
failure above (`game_windowed_run_writes_its_log`) and nothing else.

### Final images (`docs/images/`, ~1.5 MB total, each well under 500 KB)
title-screen, field-arlia, world-map, battle-spell, boss-battle, camp-status,
shop-equipment-wizard, late-game-fienal, ap-item-received, setup-discs,
setup-archipelago (11 PNGs for the 10 requested scenes; the setup program's two pages each
got their own file). One sentence each, `docs/images/README.md`. All captured at 1280x960
(4:3, the internal 320x240 at an even x4 scale) except the two setup shots, which are the
setup window's own 920x660. Losslessly re-encoded with Pillow (`optimize=True,
compress_level=9`, RGBA -> RGB since the alpha is always opaque) from `SS_SHOT`'s raw output
(`util::write_png` doesn't compress, so the raw captures were ~2.5 MB each).

Full list of candidates, scenes and exact reproduction commands:
`scratchpad/readme_shots/shots.md`. Every probe sweep (dozens of vblanks tried per scene,
picked the best) is in `scratchpad/readme_shots/probe*/` — scratch, safe to delete; not
committed (git-ignored `scratchpad/`).

### How each shot was gotten (short version; full commands in shots.md)
- **Title**: fresh boot, no `--map`; logos + the opening movie run for a while (~11500
  vblanks) before the title menu settles — found by a wide SS_SHOT sweep, not guessed.
- **Field (Arlia)** / **shop**: `--map 18` (debug new-game start on Arlia, map 18); the shop
  reused the `arlia_wizard` input script already in `tools/smoke_field_events.sh`.
- **World map**: `analysis/saves/dex_north_city.mcd` (copied, never modified) via
  `SS_LOAD_SAVE=0` + `--map-pos`, captured a couple of vblanks after load, before the
  world-map entrance trigger (same position) auto-transitions into North City.
- **Battle / boss**: `--battle ENC[:STAGE]` (`game/battle/battle_debug.h`); `SS_BATTLE_PARTY`
  fills slots with real characters (1 Claude 2 Rena 3 Celine 4 Bowman), `SS_BATTLE_STRONG`/
  `SS_BATTLE_GOD` keep the party alive long enough to get a clean frame, plain `--input`
  mashing (cross = attack, l1 = killer move) — no RAM-dump reference comparison needed
  (that machinery in `scratchpad/battle/pcrun.sh` is for matching PCSX-Redux, overkill for a
  screenshot). Boss = encounter 538 (`docs/notes/battle_enemies.md`: Vesper/Decus/Cyril).
- **Camp status**: the camp menu's command column order is Specialty, Items, Equipment,
  Skills, **Status**, Options, Combat, Data (`menu_camp.cpp` comment) — Triangle opens it
  already on Specialty, so Status needs 4 "down"s then Cross, then Cross again on the
  character.
- **Late-game (Energy Nede)**: `scratchpad/latesave/card.mcd` (copied, never modified),
  `SS_LOAD_SAVE=0`, no `--map-pos` needed (the save already sits at the Final Save Point).
- **Archipelago item received**: see shots.md's "Archipelago shot" section — a real local
  MultiServer (`scratchpad/ap/`, the same infra `tools/ap_e2e.sh` builds) and a Python "Bob"
  client send a real item over the wire; the port shows the normal in-game banner. Chose this
  over `--debug` + the console's `ap_give` (also valid, HANDOFF-documented) specifically
  because `--debug` always draws a status band over the picture
  (`runtime/debug_overlay.cpp`) and `SS_AP_SERVER` doesn't need `--debug` at all.
- **Setup (Discs / Archipelago pages)**: `tools/setup_screenshots.sh` against the existing
  `engine/build-setup/shattered-stars-setup` binary (built before another agent's
  in-progress Mods-page edit broke that target's *link* — `setup_ui.cpp` currently has
  undefined references to `mods_catalog`/`mods_install`/etc. that aren't implemented yet).
  Did not touch `setup_ui.cpp` and did not try to rebuild `shattered-stars-setup`; used the
  prebuilt binary as-is, which still runs fine.

## Rules this task was given (for any follow-up)
- Headless only; never pop windows; never audible audio
  (`--headless`/`SDL_VIDEO_DRIVER=offscreen`/`SDL_AUDIO_DRIVER=dummy`).
- Never touch `~/.config/shattered-stars` or `~/.config/pcsx-redux`; scratch
  `XDG_CONFIG_HOME` under `scratchpad/readme_shots/`, named scratch memory cards or
  `--no-memcard`.
- Kill processes only by exact PID (used for the local AP server: `setsid` + its own PID,
  confirmed stopped with `ps aux | grep MultiServer` after).
- `scratchpad/readme_shots/`, own build dir `engine/build-shots -j8`.
- Changed engine code -> ran `ss_tests`, `ss_game_tests`, `tools/smoke_field_events.sh` from
  `engine/build-shots` before calling it done (see Verification above).
- Kept out of `engine/src/setup/setup_ui.cpp`, `engine/src/main.cpp`'s `--version`, and
  `engine/src/game/ap/*` (other agents' areas); re-read `main.cpp` and `runtime.h` right
  before each edit since they changed on disk mid-task from unrelated concurrent work, and
  confirmed afterward (`grep -n SS_SHOT`) that my hunks were still intact.
- No commit/push from here; the coordinator commits hourly.
