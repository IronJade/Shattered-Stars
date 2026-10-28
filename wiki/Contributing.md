# Contributing

Thank you for helping. Bug reports from players are just as valuable as code: every report of
something that differs from the console helps make the port faithful.

## Reporting a bug

Open an issue on [GitHub](https://github.com/IronJade/Shattered-Stars/issues). Please include:

| What | Why, or how to find it |
|---|---|
| The version | `shattered-stars --version`, or the package name (for example `ShatteredStars-0.03.00-win64.zip`) |
| Your system | Windows or Linux, and whether you use a release package or your own build |
| Your disc images | Which disc, and the format (`.cue` / `.bin`, `.iso`, ...) |
| What happened, and what you expected | As exactly as you can |
| Where in the game | The place and what you were doing. With the debug console (`--debug`), the `where` command prints the map number and your position |
| How to make it happen again | The steps, if you know them |
| A save near the problem | A copy of your memory card file (`memcard1.mcd`). Say which save slot |
| Screenshots or a short video | Especially for anything visual |
| The game's output | If you started the game from a terminal, copy what it printed |
| Mods or Archipelago | Which mods were enabled on the setup program's Mods page, or whether a seed was loaded |
| The original's behaviour | If you can check it, does the same thing happen on a console or in an emulator? That tells a port bug from a bug in the original |

**Never attach disc images or any files taken from the game discs.** A memory card file is
fine: it holds only your save.

If the problem is a bug in the original game, it is still worth reporting: it may belong on
[Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept).

## Proposing a fix

Fork the repository, make your change on a branch, and open a pull request that explains what
it changes and how you tested it.

### The play-state rule

Shattered Stars fixes bugs **without changing how the game plays**. Hit timing, the number of
hits, damage, difficulty and battle pacing must stay exactly as in the original. So:

1. **Measure the faithful version first.** For anything that can affect gameplay, compare the
   port with the original running in [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) with the
   same input: the game's random-number draws, the frames hits land on, the hits and the damage.
2. **Then make the fix, and measure again.** If any of those change, the fix is held, and the
   numbers go to the maintainer to decide.
3. **Keep the original visible.** Comment out the original line with
   `// ORIGINAL BUG (original_bugs.md <id>): ...`, write the corrected code below it, and add a
   row with the next id in its area to
   [docs/notes/original_bugs.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/notes/original_bugs.md):
   the original address, the port file, the bug in one line, and its status.
4. **Visual fixes that need randomness** must not draw from the game's random-number generator;
   the spell effects have a separate, looks-only random source for this.
5. **Don't model slowdown.** The port runs at full speed by design.
6. **Keep saves byte-compatible** with real memory cards.

### Testing

Before opening a pull request, build and run the three suites
([Building from Source](Building-from-Source#tests)):

```sh
cmake --build engine/build
SS_DISC1=".../Disc 1.cue" engine/build/ss_tests
SS_DISC1=".../Disc 1.cue" engine/build/ss_game_tests
SS_DISC1=".../Disc 1.cue" tools/smoke_field_events.sh engine/build/shattered-stars
```

Tests must run headless: never open a window or play audible sound in a test (use
`--headless`, `SDL_VIDEO_DRIVER=offscreen` and `SDL_AUDIO_DRIVER=dummy`), and never write to
your real settings (a headless run never writes `config.ini`).

The game has debug options for reaching a scene quickly, such as starting on a given map
(`--map N`) or in a given battle (`--battle ENC`). They are listed under "Debugging switches" in
[engine/README.md](https://github.com/IronJade/Shattered-Stars/blob/main/engine/README.md#debugging-switches).
A windowed game playing an Archipelago seed ignores them (and the `SS_*` debugging variables);
headless runs keep them all, so the tests can use them with a seed.

## Coding conventions

- **C++20**, in the namespaces under `ss::` (for example `ss::game::battle`). Ported game code
  lives in `engine/src/game/`, the hardware replacements in `engine/src/psx/` and
  `engine/src/runtime/`, and the window and input in `engine/src/platform/`.
- **Names follow the catalog**, in `subsystem_verb_object` snake case (for example
  `loader_select_entry`). Sony library functions keep their SDK names.
- **Cite the original.** Comments give the original function's address and the structure
  offsets they use, so any line can be checked against the disassembly.
- **No game code or assets in the repository**, ever: no extracted textures, sprites, audio,
  logos or data files. The game reads everything from the player's discs at run time. The only
  exception is a handful of documentation screenshots of the port running, in `docs/images/`.
- **Document as you go.** Every function you identify gets a row in the catalog; explanations
  of how things fit together go in `docs/notes/`.

## Where things live

| Path | What it holds |
|---|---|
| [docs/catalog](https://github.com/IronJade/Shattered-Stars/tree/main/docs/catalog) | The reverse-engineering catalog: every function (`functions.csv`), global (`globals.csv`), event-script opcode (`script_opcodes.csv`) and data format (`formats.md`). Keep each module's rows in address order within its block, with LF line endings |
| [docs/notes](https://github.com/IronJade/Shattered-Stars/tree/main/docs/notes) | Prose notes on each program, the original's bugs, GPU accuracy, the endings and more |
| [docs/CONVENTIONS.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/CONVENTIONS.md) | The project's standing rules (enhance, don't degrade; no game data in git; headless tests; memory caps) |
| [docs/ap](https://github.com/IronJade/Shattered-Stars/tree/main/docs/ap) | The Archipelago design: the seed format, the world's logic, the release checklist |
| [apworld/so2](https://github.com/IronJade/Shattered-Stars/tree/main/apworld/so2) | The Archipelago world (Python) |
| [tools](https://github.com/IronJade/Shattered-Stars/tree/main/tools) | Disc, script and mod tools, comparison scripts, sweeps and packaging |
| [tools/refs](https://github.com/IronJade/Shattered-Stars/tree/main/tools/refs) | The reference regression suite's inventory |
| [engine](https://github.com/IronJade/Shattered-Stars/tree/main/engine) | The C++ source and tests |

## Licence

Shattered Stars is released under the MIT licence; see
[LICENSE](https://github.com/IronJade/Shattered-Stars/blob/main/LICENSE).
