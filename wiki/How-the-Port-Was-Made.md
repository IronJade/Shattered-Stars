# How the Port Was Made

This page is for technical readers: how *Star Ocean: The Second Story* was turned into a native
C++ program, and how the result is checked against the original. It assumes some familiarity
with reverse engineering and the PlayStation.

## Written with AI

In the open: most of this project was written by **Claude**, Anthropic's AI model, working through
[Claude Code](https://claude.com/claude-code) under the direction of the project's maintainer.
That includes the reverse engineering and the function catalog, the C++ port, the setup program,
the European, French and German releases, all of the Archipelago work, and this wiki. The
maintainer chose the project and its goals, made the design decisions, played and tested the
port, and decides each release. Every commit Claude wrote carries a `Co-Authored-By: Claude`
line, so the repository's history shows which ones.

Nothing is taken on trust because an AI wrote it: every function is checked against the original
game running in PCSX-Redux, as the sections below describe, and the port's behaviour is measured,
not assumed. The repository's history starts in October 2026; the maintainer's earlier work on the
game is not part of it.

## The approach: a hand port

There are three common ways to bring a console game to the PC:

| Approach | What it means | Used here? |
|---|---|---|
| Emulation | Run the original machine code on a simulated PlayStation | No |
| Static recompilation | Translate the machine code into C automatically, instruction by instruction | No |
| Hand port | Understand each function, then rewrite it by hand as readable source code | **Yes** |

Shattered Stars follows the method of
[Severed Chains](https://github.com/Legend-of-Dragoon-Modding/Severed-Chains), the hand port of
*The Legend of Dragoon*: every game function is rewritten by hand from the disassembly, on top
of a native replacement for the PlayStation hardware. The result is ordinary C++20 that can be
read, debugged, fixed and extended. No original code runs, and the repository contains no game
code or data: the game's assets are read from the player's disc images at run time.

## The layers

The port is built in layers, from the hardware up to the game:

| Layer | What it does | How it was checked |
|---|---|---|
| Disc | Reads `.bin` / `.cue` / `.iso` images, decrypts the archive's table of contents, and decompresses the game's SLZ compression | Byte-identical to the console's own output |
| GPU | A software renderer of the PlayStation's video memory: polygons, sprites, lines, transfers, blending, mask bits, dithering | Matches hardware video-memory dumps from public PS1 GPU test programs; in places it is closer to the hardware than PCSX-Redux |
| GTE | The geometry coprocessor (3D maths), all 22 commands, and Sony's libgte library | Bit-exact with PCSX-Redux, and with Sony's own library code run in Redux |
| MDEC / movies | The video decoder and the STR movie player | All 23,869 movie frames on both discs decode, exactly as jPSXdec's PlayStation-accurate mode does |
| SPU / sound | The sound chip, Sony's libspu, and the game's own sound driver and sequencer | Music and effects match PCSX-Redux; the title music's 974 driver calls are identical |
| Memory cards | Raw 128 KB card images, the card file system and the game's card code | Saves load in both directions; save blocks match byte for byte, apart from a few bytes the original fills with leftover memory |
| Controller | Keyboard and gamepads, presented as a DualShock in analog mode | The game's own controller code is ported, including the analog stick and rumble set-up |
| Platform | SDL3 for the window, input and audio; OpenGL 3.3 for presenting the picture | The Windows build renders the same frames as the Linux build, byte for byte |

On top of these sits the game code itself, in `engine/src/game/`.

## Reading the game

The game is not one program. The boot executable (`SCUS_944.21` for Disc 1) loads further
programs and code overlays (blocks of code loaded over each other at the same addresses) from
the disc's archive as you play. All of them were read:

| Program | What it is |
|---|---|
| Boot executable | The resident core: loader, sound, controller, memory card, shared libraries |
| Title (archive entry 1) | Logos, movies, the title menu, New Game set-up |
| Field (entry 3100) | Field maps, the event-script interpreter, messages, characters, camera, shops, menus |
| Field overlays | The field-map and world-map code overlays, and six smaller sub-programs |
| Menu overlays | 17 overlays for the camp menu pages, item creation, save and load, renaming and more |
| Battle (entry 2129) | Combat, the HUD, the camera, enemy AI, and its 12 overlay blocks (including the result screen) |
| Party modules | 12 character code modules (each character's killer moves) |
| Enemy modules | 40 enemy code blocks carried inside the encounter data |
| Spell modules | 55 spell code modules |
| Credits (entry 3107) | The credits program |

The tools:

- **Disassembly and decompilation** with the [REA](https://github.com/morluto/rea) reverse-engineering tools and Ghidra.
  `tools/psxexe2elf.py` wraps a PS-X EXE (and overlays or RAM dumps) in an ELF file that both
  can open, with the catalog's names embedded as symbols.
- **Library matching.** Ghidra's signature matcher identified 406 functions from Sony's PsyQ
  SDK, which keep their SDK names. `tools/match_functions.py` finds functions shared by two
  programs linked at different addresses, so a function only has to be understood once.
- **Format tools.** `tools/so2_archive.py` reads the archive and its compression;
  `tools/so2_script.py` disassembles the field event scripts.
- **The original, running.** PCSX-Redux with Lua scripts (`tools/redux_*.lua`) records
  reference frames, sound driver calls and event-script traces from the original game.

## The catalog

Everything identified is recorded in
[docs/catalog](https://github.com/IronJade/Shattered-Stars/tree/main/docs/catalog), so it never
has to be worked out twice:

| File | Contents |
|---|---|
| `functions.csv` | Every function: module, address, name, C-style signature, summary, confidence, evidence, and where it lives in the port |
| `globals.csv` | Global variables and the structures behind them (about 940 rows) |
| `script_opcodes.csv` | The event-script instruction set |
| `formats.md` | File and data formats: archive, compression, programs, maps, scripts, text, sound |
| `COVERAGE.md` | A generated report of the catalog's coverage per module |

At the time of writing, `functions.csv` has **5,536 functions** across 172 code modules: 149
modules of the North American programs, and 23 of the French and German releases' own code
(their text system; see [Other releases](#other-releases-region-profiles)):

| Confidence | Functions | Meaning |
|---|---:|---|
| Confirmed | 5,536 | Read in full, or checked against live data or the reimplementation |
| Likely | 0 | Consistent with the code and its callers |
| Guess | 0 | A working name from call sites only |

And by port status:

| Port status | Functions |
|---|---:|
| Ported to C++ | 4,547 |
| Not needed: dead code nothing calls, stale copies, or hardware the PC doesn't have (CD drive, physical memory cards, the console's own drawing paths) | 901 |
| Sony library routines replaced by the port's runtime | 88 |

No row is left without a status. A gap scanner (`tools/catalog_gaps.py`) searches every
program's code for call targets and function entries the catalog lacks; it reports none
missing. Prose companions, explaining how each program fits together, are in
[docs/notes](https://github.com/IronJade/Shattered-Stars/tree/main/docs/notes).

## Writing the C++

Each system is written by hand in modern C++, following the decompilation closely but
naturally:

- Names follow the catalog (`subsystem_verb_object`, for example `loader_select_entry`), and
  comments cite the original addresses, so any line can be traced back to the original.
- Data stays on the disc. Even where code overlays are native C++, their data tables are read
  from the disc images at run time.
- Some things are deliberately different from the console and documented as such: loading is
  synchronous (instant) instead of streamed from a drive, memory pools are larger, and where the
  original reads uninitialised memory the port uses a safe value.
- Every program can run headless (no window, no sound), so tests and sweeps can run the real
  game code at full speed.

## Verifying against the original

The standard for "done" is that the port behaves like the original, measured, not by eye.

**Comparisons with PCSX-Redux.** The same scene is run in the original (in PCSX-Redux, with
the same input) and in the port, and compared:

- **Screens:** logos and the title screen are pixel-identical; frames are compared in the
  console's 15-bit colour space (`tools/compare_frames.py`).
- **Random numbers:** the game's random-number generator is the most sensitive test there is.
  Battles are compared draw by draw, and many references are *RNG-equal to the exit*: every
  random draw from the start of the battle to its end matches the original.
- **Units and effects:** positions, animations, damage numbers and projectiles frame by frame.
- **Save data and memory:** save blocks, the battle's handoff data and menu state, byte for byte.
- **Sound:** driver calls and the mixed output (`tools/compare_audio.py`).

**Sweeps of the whole game.**

- The *event sweep* ran every trigger, Private Action, conversation and pickpocket entry of all
  827 field maps on both discs, 5,326 entries in all, with no crash and no unfinished code
  reached.
- The *battle sweep* ran all 564 encounters headless without a crash, including a pass with
  AddressSanitizer, a memory-error detector.
- All 85 ending ids were played in the port. Five ending lists from a late-game save were
  compared with PCSX-Redux and came out the same, and that save was played from the final save
  point through the final battle and the endings in both, with the same input and the same
  seven endings.

**Automated tests.** Three suites are run before a change counts as verified (see
[Building from Source](Building-from-Source#tests)); `tools/checkpoint.sh` builds and runs them
all, then the quick reference set below:

| Suite | What it covers |
|---|---|
| `ss_tests` | Unit tests of the hardware layers, plus disc tests against reference values |
| `ss_game_tests` | The game code: event scripts, menus, battles, Archipelago and more |
| Smoke test | Whole-game headless runs: doors, chests, shops, talking, mods, Archipelago seeds |

**The reference regression suite.** Every PCSX-Redux recording made while porting is
inventoried in `tools/refs/refs.csv`: 339 references covering killer moves, stages, enemy
families, spells, menus and the world map. `tools/ref_regress.py` replays the 223 that have an
automated recipe through the port (never through PCSX-Redux) and judges each against its
documented expectation. A quick set of 28 references, which takes well under a minute, runs
alongside the three suites. At the last full run, 219 of the 223
automated references passed; the rest were open leads under investigation. How each reference
is judged, and the gaps still to close, are in
[tools/refs/README.md](https://github.com/IronJade/Shattered-Stars/blob/main/tools/refs/README.md).

## Other releases: region profiles

The port was written against the North American release. The European (English), French and
German releases play on the same build: the game recognises a disc by its serial and picks
that release's **profile**, which holds its address maps, how its code modules are found, its
text system and its save product code. The code is in `engine/src/runtime/region.cpp`; the
design is in
[docs/notes/regions.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/notes/regions.md).

- **Address maps.** The ported code reads the original's tables from the program images at
  their North American addresses. The other releases load their programs elsewhere and moved
  many tables, so `tools/region_map.py` aligns each of the 149 images the port reads with the
  North American one and writes only pairs of offsets (no game data) to
  `engine/data/regions/`. Every read goes through one translating function, and each image is
  checked against the one its map was made from. Enemy and spell modules are found by a hash
  of their normalised instructions, since the same code relinked elsewhere hashes alike. A
  coverage check confirms that every address the port reads in the suites and sweeps (6,690 of
  them) has a counterpart in each release.
- **Map scripts.** Where a release lays out a map's script differently (7 maps in Europe, 24 in
  France, 32 in Germany), a script map translates the North American offsets that Archipelago
  seeds and mods use, so one seed and one mod file serve every release.
- **Text.** Europe uses the North American text system. France and Germany have their own: a
  132-glyph Latin font, other glyph numbers, word-wrap rules, name-entry keyboards and moved
  layouts, read from their programs and ported as a module of its own. Its North American rules
  give the same result as the ported field code for 3,302,816 layouts, and the lines of the
  opening match the French and German originals running in PCSX-Redux.
- **Timing.** Every release runs the North American game logic at 60 Hz, by decision: the PAL
  versions' 6/5 movement scaling and their removal of the projectile clash are documented, not
  ported. Data the PAL versions count in 50 Hz frames, such as the credits' cues, is converted
  to 60 Hz.
- **Saves.** Each release writes its own product code, save signature and icon flag, taken from
  the running program, and the originals load the port's saves in PCSX-Redux.

The same checks as for North America then run on the other discs: on Disc 1, the European
event sweep gives the same result as North America's for all 5,326 entries.

The Japanese release is a different matter: it is the version North America was translated
from, with about 510 functions changed, gameplay code among them. It is recognised, but not
supported yet.

## Enhance, don't degrade

The port aims to play **better** than the original without changing **how** it plays:

1. **No slowdown.** The original drops frames under heavy load. The port does not model the
   console's CPU time, so it always runs at full speed. Comparisons stop where the original
   slows down.
2. **No old bugs, unless they are gameplay.** A real bug is corrected, with the original line
   kept as a comment marked `ORIGINAL BUG (original_bugs.md <id>)` and the case listed in
   [original_bugs.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/notes/original_bugs.md).
3. **A fix never changes the play state.** Hit timing, hits, damage, difficulty and pacing stay
   the original's. The faithful version is verified against PCSX-Redux first; the fix goes in
   only if the measurements stay the same. Otherwise it is held for a decision. Visual fixes
   that need randomness draw from a separate, looks-only random source, so the game's own
   random sequence is untouched.
4. **Data formats stay exact.** Saves remain byte-compatible with real memory cards, leftover
   bytes included.

Every decision is listed for players on [Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept).

## What's left

The game is finishable from start to end, but work continues:

- **Archipelago:** entrance shuffle is designed but not built. The world ships as a custom
  `.apworld` with each release; asking for it to become part of Archipelago itself is still to
  come.
- **Mods:** the mod manager is done; the curated list holds two sample mods so far
  ([Modding](Modding)).
- **The reference suite:** 116 references have no automated recipe yet (mostly menu, world-map
  and early field-effect recordings).
- **The Japanese release:** recognised but not supported yet (see
  [Other releases](#other-releases-region-profiles)).
- **Known small differences:** a battle voice line's talking pose is a few frames shorter,
  because the port loads instantly where the console waited for the disc. The timing of the
  battle AI's first random draw in one late-game reference is still being investigated. A few
  original bugs are marked "not fixed yet".
- **Polish** found by testing and by players' reports; see
  [Bugs in the port](Bugs-Fixed-and-Quirks-Kept#bugs-in-the-port) for the latest fixes.

Port progress per function is the `port` column of the catalog's
[functions.csv](https://github.com/IronJade/Shattered-Stars/blob/main/docs/catalog/functions.csv).
