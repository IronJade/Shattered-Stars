# Regions text (FR / DE): continuation log

Worker: regions-text agent (started 2026-10-23 12:05). Task: reverse the French and German
text system (encoding, font, wrapping, message-box sizing, name entry, menus / HUD), catalog
every function read under `fr:` / `de:` modules, write `docs/notes/regions_text.md` (how it
works, how it differs from NA, the port's design and how it plugs into the region profile),
and implement it as self-contained engine code with unit tests (not wired into the game).

## Rules given

- No game code, text or assets in git: addresses, formats, counts, short descriptions only.
  Disc images read-only; extracts only under `scratchpad/regions_text/`.
- Discs: `Emulation/Roms/Star Ocean 2/Star Ocean - The Second Story (France|Germany) (Disc 1|2)/`
  (.cue/.bin, ignore zips); NA in `Emulation/Roms/PS/Star Ocean - The Second Story (Disc N)/`.
- Headless only; scratch `XDG_CONFIG_HOME` under `scratchpad/regions_text/`; never touch
  `~/.config/shattered-stars` or `~/.config/pcsx-redux` (Redux only `-cli`, scratch HOME, max 2).
- Kill by exact PID; wait loops with deadlines; no `pgrep -f` of own pattern; /tmp is RAM;
  build in `engine/build-text` with -j8; max 4 game processes.
- NA behaviour must not change; run ss_tests and ss_game_tests (NA disc) from the build dir
  before calling anything done.
- Don't edit the region agent's files (launcher, setup, disc recognition, region profile /
  address-map files); don't wire into the game. Don't commit or push.
- One line per finished step in `scratchpad/checkpoint_notes.txt`, prefixed `regions-text: `.
- Keep this log current after each step and at least every 30 minutes. Past ~350k context:
  finish the step, update the log, end the turn.
- Catalog under the lock: `flock scratchpad/catalog.lock python3 scratchpad/fx_worker/cat3/apply3.py <file>`.
- REA server is shared: re-open the binary before each batch and check the subject (the
  REA CLI `scratchpad/regions/rea.sh` has its own Ghidra and avoids that).

## Done

(nothing yet)

## In progress

- Step 1: reverse the FR / DE text paths. Scratch: `scratchpad/regions_text/`
  (`codediff.py` -> `frde_rows.txt`: the 47 rows whose code differs in FR / DE but not EU).

## Next steps

1. Build FR / DE ELFs (`scratchpad/regions/mksyms.py MOD fr1|de1`), decompile the changed text
   functions, compare with NA.
2. Extract the FR / DE fonts and strings into `scratchpad/regions_text/` only.
3. Catalog rows, formats.md, `docs/notes/regions_text.md`.
4. `engine/src/game/common/text_pal.{h,cpp}` + tests; Redux captures for wrapping.
