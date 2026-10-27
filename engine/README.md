# Shattered Stars engine

A native, hand-ported reimplementation of *Star Ocean: The Second Story* in C++20,
following the method of [Severed Chains](https://github.com/Legend-of-Dragoon-Modding/Severed-Chains):
each game function is rewritten by hand from the decompilation, on top of a native
replacement for the PS1 hardware. It is not an emulator, and it contains no game code
or data: everything is read at runtime from your own disc images.

Shattered Stars itself (this code) is released under the MIT licence; see `LICENSE`
(bundled alongside this README in each release). Third-party components it links
against have their own licences, listed in `THIRD-PARTY-LICENSES.txt`.

## Status

| Layer | State |
|---|---|
| Disc image (`.bin` / `.cue` / `.iso`) | done |
| Archive table + SLZ decompression | done, byte-identical to the original |
| GPU (VRAM, polygons, sprites, lines, transfers, blending, mask, dithering) | software renderer, unit-tested |
| Window, OpenGL presentation, keyboard and gamepad | done |
| GTE + libgte | done, bit-exact with PCSX-Redux / Sony's PsyQ 4.2 code |
| MDEC + STR movies | decoder and player done, exact against jPSXdec's PS1 mode |
| SPU (sound), memory cards | SPU, libspu and the game's sound driver ported (music and effects match PCSX-Redux); memory cards (raw 128 KB images) |
| Game code | boot, title (logos, movies, menu, New Game, Continue, Voice Collection); field maps (event scripts, messages, actors, doors, chests, shops, encounters, effects, embedded map code, the credits and the end screen); world map (vehicles, bunny, courier, map); every camp menu page, item creation, save / load (byte-compatible with real memory cards); battles (combat, HUD, result screen, every enemy class, Claude, Rena, Celine, Leon, Noel, Ernest, Bowman, Chisato, battle items), every spell and party member, and the party AI; every one of the 564 encounters runs; the game is finishable on both discs with all 85 endings — see `docs/CHECKLIST.md` |

Port progress per function is the `port` column of `docs/catalog/functions.csv`.

## First run

You need images of your own discs, from the North American, European, French or German
release (both from the same one): each disc's `.cue` file with its `.bin`, or an `.iso` /
`.img` image. Disc 2 is optional until the story asks for it.

**Windows:** unzip `ShatteredStars-<version>-win64.zip` (e.g. `ShatteredStars-0.02.50-win64.zip`)
anywhere and run **`setup.exe`**. On the Discs page, choose Disc 1 (and Disc 2) with
*Browse...*. The other pages are Display,
Sound, Controls, Gameplay, Memory cards, Mods and Archipelago. Then press *Save & Play*. Or just run
`shattered-stars.exe`: it opens setup by itself while it has no disc.

**To skip browsing,** put the disc images in the `discs` folder next to the game
(subfolders are fine). The game finds them by itself, and setup's *Auto-detect* fills
them in.

**Linux:** the same two programs sit side by side in the build folder, `shattered-stars`
(the game) and `shattered-stars-setup` (setup). They work the same way. The package also has
`install-desktop-entry.sh` (optional): it adds both programs to your application menu with
their icon, for your user only (`~/.local/share/applications` and `~/.local/share/icons`).
GNOME and KDE Plasma on Wayland show a program's name and icon in the taskbar only once its
desktop entry is installed. Run it again after moving the folder; `--uninstall` removes it.

| | Windows | Linux |
|---|---|---|
| The game | `shattered-stars.exe` | `shattered-stars` |
| Setup | `setup.exe` | `shattered-stars-setup` |
| Settings | `%APPDATA%\ShatteredStars\config.ini` | `~/.config/shattered-stars/config.ini` (or `$XDG_CONFIG_HOME/shattered-stars/`) |
| Memory card 1 | `memcard1.mcd` beside `config.ini` | the same |
| Mods | `mods\` beside `config.ini` | `mods/` beside `config.ini` |
| Disc folders | `discs\` next to the game, or beside `config.ini` | `discs/` next to the game, or beside `config.ini` |

While playing, F11 or Alt+Enter switches between the window and full screen, and Esc
pauses the game and asks before quitting (Keep playing / Quit; `src/runtime/quit_prompt.h`).
The window's close button and Alt+F4 quit at once. Esc and F11 are the port's own keys:
they never press a game button, whatever `config.ini` binds to them. `config.ini` is plain
`key=value` text: setup writes it, and keys it doesn't know are kept. The keys are listed in
`src/launcher/config.h`.

## Build

Needs CMake 3.24+, a C++20 compiler and, ideally, SDL3 development files
(`sudo dnf install SDL3-devel` / `apt install libsdl3-dev`). Without them the build
fetches SDL 3.4.18 (pinned by SHA-256) and builds it, but that copy only has audio
if your system has the PipeWire, PulseAudio or ALSA development headers.

```sh
cmake -S engine -B engine/build -G Ninja
cmake --build engine/build
```

### Windows

The Windows build is cross-compiled from Linux with MinGW-w64 (on Fedora:
`sudo dnf install mingw64-gcc-c++ mingw64-SDL3`):

```sh
tools/build_windows.sh   # -> dist/ShatteredStars-<version>-win64.zip: shattered-stars.exe,
                         #    setup.exe, SDL3.dll, this README, LICENSE and an empty discs\ folder
```

The C / C++ runtimes are linked in, so the zip holds everything the game needs (see
*First run*). The Windows build renders the same frames as the Linux one (checked byte for
byte), and its test suites pass under Wine (`engine/build-win/ss_tests.exe`,
`ss_game_tests.exe`).

### Linux package

```sh
tools/build_linux.sh --container   # -> dist/ShatteredStars-<version>-linux-x86_64.tar.gz:
                                   #    shattered-stars, shattered-stars-setup, README, LICENSE, discs/,
                                   #    install-desktop-entry.sh, desktop/ (.desktop files, icons)
```

The release is built with podman in a Debian 12 container (`tools/linux-build/Containerfile`,
pinned by digest), so it runs on any distribution with glibc 2.36 or newer. SDL 3 and Dear
ImGui are built from source there, by the project's own CMake, pinned by SHA-256. SDL is linked in, and
it loads its display and sound backends (X11, Wayland, PipeWire, PulseAudio, ALSA) at run
time. libstdc++ is linked in as well, so the player needs only glibc and OpenGL 3.3. The
repository is mounted read-only; the build goes to `engine/build-linux-container`. Without
`--container` the script builds on this machine instead, with the system's SDL3 copied beside
the programs. That build needs this machine's glibc (Fedora 44: 2.43).

### Icon and program names

The icon is an original design (a gold star broken into shards on a midnight-blue tile;
nothing from the discs), drawn from vector shapes by `tools/make_icon.py` (pycairo and Pillow)
into `res/icon/`: PNGs from 16 to 256 px, a Windows `.ico` and an SVG. Those files are
committed, so the build never runs the script. The build compiles the 64, 128 and 256 px PNGs
into both programs (`src/platform/app_icon.h`); on Windows each `.exe` also gets the `.ico`, a
version resource (Task Manager and the taskbar call them "Shattered Stars" and "Shattered
Stars Setup") and a manifest (`asInvoker`, so `setup.exe` is never taken for an installer),
from `res/windows/`. The game's window is titled "Shattered Stars", setup's "Shattered Stars
Setup". Both programs tell SDL their name and app id before it starts:
`io.github.IronJade.ShatteredStars` and `io.github.IronJade.ShatteredStars.Setup`, the Wayland
app_id / X11 window class that `res/linux/`'s `.desktop` files match.

### Setup program

The setup program uses Dear ImGui (MIT licence), fetched by CMake and pinned by SHA-256.
`-DSS_BUILD_SETUP=OFF` leaves it out. To check its layout without opening a window,
`tools/setup_screenshots.sh [PROGRAM] [OUT_DIR]` renders every page and state offscreen as
PNGs, with scratch settings. Pass `engine/build-win/setup.exe` to run the Windows build
under Wine. setup's own test options are listed at the top of `src/setup/setup_main.cpp`.

## Run

```sh
# Settings window (discs, display, sound, memory cards, mods):
engine/build/shattered-stars-setup
# Play: opens the game window. Disc paths given here are remembered in
# ~/.config/shattered-stars/config.ini, so later runs need no arguments.
engine/build/shattered-stars --disc1 ".../Star Ocean - The Second Story (Disc 1).cue" \
                             --disc2 ".../Star Ocean - The Second Story (Disc 2).cue"
engine/build/shattered-stars
```

Memory card 1 is `memcard1.mcd` next to `config.ini` (created formatted on first use);
`--memcard1 FILE` / `--memcard2 FILE` use other raw 128 KB images. A DexDrive save
(`.gme`) converts with setup's *Make a .mcd copy* button or with
`tools/gme_to_mcd.py SAVE.gme CARD.mcd` — point `--memcard1` at a copy, since the game
writes to the card it uses. Headless runs (`--headless`) use only cards named for the run
(`--memcard1` / `SS_MEMCARD1`) and never write `config.ini`.

Keyboard pad: arrows = d-pad, Z = Cross, X = Circle, A = Square, S = Triangle,
Enter = Start, Backspace = Select, Q/W = L1/R1, 1/2 = L2/R2. Gamepads work through SDL and read as a DualShock in analog mode: the left stick walks
(and works the d-pad in menus), and the game's Vibration setting drives the rumble.

Dialogue (setup's **Gameplay** page; port enhancements, both off by default, every game
mode including Archipelago seeds). With both off the messages behave exactly as the
original's; with them on only the waiting changes, never what happens:

- **Instant text** (`instant_text=1`): each message page appears at once, as if the message
  speed were infinite, and lines that scroll up move at once. The text's own pauses and timed
  waits, and the window's opening and closing, stay as they are.
- **Auto-advance** (`auto_advance=1`, `auto_advance_delay=MS`, 100-10000, default 1000; setup
  offers 0.5 / 1 / 1.5 / 2 / 3 seconds): a message waiting for the confirm button moves on by
  itself once its page has been complete that long, exactly as a press of confirm would move
  it on. It stops at every choice (yes / no, shops, inns, name entry) and never answers it.
  The battle result screen's pages (which move on by themselves after 6 seconds) and the
  post-battle equipment report move on the same way, as a confirm press would.

Headless runs ignore these keys (tests play as the original); `SS_INSTANT_TEXT=0|1` and
`SS_AUTO_ADVANCE=0|1|MS` set them for any run.

Other modes:

```sh
# Texture viewer: Left/Right change entry, Up/Down change block, V shows all of VRAM.
engine/build/shattered-stars --viewer
# Write an entry's textures as PNGs instead of opening a window.
engine/build/shattered-stars --entry 10 --dump analysis/render/entry10
```

World edits: patch files change map scripts and game data as they load. Put `*.patch`
files in `mods/` next to `config.ini` (or pass `--mods DIR`); they apply in name order,
and every patch carries the value the disc holds, so the game skips (and logs) a patch
whose data no longer matches. The disc images are never modified. Mod packages (a folder
or .zip with a `mod.json` and its `.patch` files) are installed, enabled and ordered on
setup's Mods page; the curated, verified ones are in the repository's `mods/` folder
(`mods/README.md`, design in `docs/notes/mods.md`). `tools/so2_mod.py`
reads your disc and writes the lines (`-o FILE` appends to a file):

```sh
tools/so2_mod.py list    DISC.cue MAP                # a map's chests and doors
tools/so2_mod.py chest   DISC.cue MAP FLAG --item ID # (or --fol N)
tools/so2_mod.py door    DISC.cue MAP INDEX --to MAP2 --x X --z Z
tools/so2_mod.py items   DISC.cue ITEM               # an item record's fields
tools/so2_mod.py item    DISC.cue ITEM FIELD VALUE   # e.g. item 0x15 price 4321
tools/so2_mod.py shops   DISC.cue                    # every shop's stock
tools/so2_mod.py shop    DISC.cue SHOP INDEX ITEM    # INDEX `add` appends, ITEM 0 removes
tools/so2_mod.py enemies DISC.cue [TYPE]             # enemy stats from the encounter packs
tools/so2_mod.py enemy   DISC.cue TYPE FIELD VALUE   # e.g. enemy 4 hp 9999
```

The patch lines (numbers in decimal or 0x hex, `#` comments; details in
`src/game/field/mods.h`):

| line | changes |
| --- | --- |
| `script MAP WORD EXPECTED VALUE` | a word of map MAP's event script |
| `item ITEM FIELD EXPECTED VALUE` | item ITEM's record, wherever the game loads it (menus, shops, battles, the field) |
| `shop SHOP INDEX EXPECTED VALUE` | stock entry INDEX of shop SHOP |
| `enemy TYPE FIELD EXPECTED VALUE` | the stats of every enemy of type TYPE when a battle loads |

Item fields: `price`, `atk`, `def`, `amount`, `effect`, `type`, `sell`; enemy fields:
`hp`, `mp`, `exp`, `fol`, `speed`, `drop1`, `drop2`, `atk`, `def`, `int`, `level`, `eva`,
`acc`, `guts`, `drop1_odds`, `drop2_odds`; or a byte offset `OFFSET[:SIZE]`.

Debug console: start with `--debug` and type commands into the terminal (`help` lists
them): `where`, `warp MAP [X Y Z [DIR]]`, `fol N`, `item ID [COUNT]`, `heal`,
`flag N [0|1]`, `global OFF [VALUE]`, `battle ENC [STAGE]`, `noclip`, and with a seed
`ap_status`, `ap_check ID`, `ap_give ITEM [FROM]`. The status line
(map, field state, the leader's position, Fol) and the console's last lines are shown
over the top of the picture, and the status line in the window title ("Shattered Stars -
map 18 | state 0 | ..."); without `--debug` neither appears.

Debugging switches (start on a map, battles, traces, headless runs) are listed in
`docs/HANDOFF.md`.

## Archipelago

Setup's **Archipelago** page loads a seed (*Browse...*, starting in Archipelago's output
folder: the generator's `AP_<seed>.zip` as it is, or a loose `.apso2`; an archive with
several Star Ocean seeds shows a slot list, saved as config.ini's `ap_seed_entry`; the game
copies the chosen seed out to `archipelago/` next to config.ini at start-up,
`launcher/ap_seed.h`) and, once one is chosen, offline or online play:

- **On / off:** the switch on setup's Archipelago sidebar row (config.ini `ap_enabled`,
  default on). Off, the game never loads the seed: the original game on the player's own
  cards, the seed and server keys kept.
- **No debugging in an Archipelago game:** a windowed run with a seed ignores `--debug` (and
  `SS_DEBUG_INPUT`), `--map` / `--map-pos`, `--battle` / `--battle-end`, `--input`,
  `--capture`, cards named for the run, and every `SS_*` variable but `SS_AP_SERVER` /
  `SS_AP_SLOT` / `SS_AP_GAME`, `SS_MODS_DIR` / `SS_MOD_ORDER` and the dialogue ones, and logs
  what it ignored. Headless runs (the tests) keep them all.
- **Its own memory card:** a seed plays on `archipelago/<seed>_<slot>/memcard1.mcd` next
  to config.ini (slot 2 empty), never on the player's cards, so ordinary saves can't be
  loaded into a seed or overwritten by one; a card named for the run (`--memcardN`,
  `SS_MEMCARDN`) still wins, and headless runs use the seed's card only with
  `SS_AP_SEED_CARD=1`. When the card is new, the saves an earlier version made for that seed
  on the player's card (sidecars naming the seed and slot) are copied to it once; the
  player's card is left as it is (`launcher/ap_seed.h`).
- **Only the seed's own saves load:** with a seed, a save loads only when its sidecar names
  this seed and slot and holds the hash of the save as it was saved (`save_hash`, and with a
  bench `bench_hash` over the save and the bench); a save of
  another seed or an ordinary game, one without its sidecar, or one changed on the card since
  fails as a damaged save does ("The data could not be loaded"), and the log says why. A guard
  against accidents and casual save editors, not cheat-proofing (`docs/ap/seed_format.md` "The
  save sidecar"; headless tests that need an ordinary save set `SS_AP_ANY_SAVE=1`).
- **Offline:** leave Server empty. The seed's patch lines join the world edits; chests
  record checks; received items are given in free play with a banner; each save's
  state is kept in a sidecar file next to the memory card (`memcard1.slot03.ap.json`),
  so the card itself stays console-compatible. See `docs/ap/seed_format.md`.
- **Online:** fill in Server (`ws://host:port` or `wss://host:port`), Slot (hint:
  the seed's own slot name) and, if the room has one, Password. The game connects on
  the next windowed run, sends every check and the goal as they happen, and receives
  other players' items live instead of only from the seed file. **DeathLink** follows
  the seed's own option (set when it was generated): if it's on, a party wipe sends a
  DeathLink and a DeathLink from anyone else ends the current battle or the field
  session, same as dying.
- **The seed's play options** (each off by default, which plays as the original): New Game
  starts the story_route the seed was made for (skipping the hero select and the naming screen); recruitments can be items (a character
  joins when their item arrives, room permitting); the first drop of each enemy type can
  be a check; traps (an ambush, losing Fol, a status ailment); EXP / Fol multipliers
  (1-10); the random-encounter rate (0-200%). DeathLink waits for a safe moment (out of
  menus, scenes and story battles the party may lose) and is never sent for a scripted
  story defeat, nor received during the ending or the credits. `docs/ap/seed_format.md`
  "Phase 3" has the details.
- **Open world** (`world_mode: open_world`): world-map entrances wait for their area's key
  (a banner names a missing one) and the El voyage for its passes. Port Hilton's sailor
  becomes a captain offering Cross / Hoffmann Ruins / El / Not now (El only when the story is
  ready to sail and every pass has arrived), and never strands the party on the Lacour
  continent; ships check their landing's key before the fare. `docs/ap/seed_format.md`
  "Phase 4" has the details. On field maps the player walks as the Formation leader (camp
  menu: Combat > Formation > Leader; the seed's starting character leads from the start; with
  `starting_character` "any" New Game adds a character select of all twelve in the hero select's
  style, `docs/ap/seed_format.md` "The character select"), and
  the hero's name in dialogue and speaker labels becomes the leader's; the hero returns on
  world maps and for a scene that shows the leader as themselves, names them, or needs one of
  the hero's own gestures. Presentation only: the story runs as with the hero
  (`docs/ap/seed_format.md` "The leader stand-in"; debugging: `SS_LEADER_TRACE=1`,
  `SS_LEADER_STANDIN=0|1`, `SS_LEADER_SLOT=N`).
- **so2.apworld 0.2.0 seeds** (seed version 2): the Limiter Off goals (Clik's Filia meeting
  preset, the Final Save Point floor's way to Indalecio shut, with a banner, until Filia's
  scene in Central City and, for the Iseria Queen goal, her defeat; that goal's Silver
  Trumpet can't be sold before The Evil Melody is composed); craftsanity (the first creation
  of each listed item is a check, the item stays yours) and creation without failures; killer
  moves' use counts as checks (checked after a battle or a load). `docs/ap/seed_format.md`
  "v2 fields".
- **Open-world party** (seed version 2, `party_rules: open`): besides the hero (both heroes
  when the seed lets the player pick), only members whose item has arrived join, wherever the
  story would add them; Leon and Dias join either game. Nobody recruited is ever overwritten
  when the party is full. The story's splits (the tournament, Linga, the Front Line, the ship
  to El, Mars...) still split the party, and a member they set aside rejoins once it is over;
  a fight the story gives one member alone (Claude in Shingo Forest in Rena's game, the
  tournament's fights) borrows that member for the fight only. A starting character joins at
  the first free play and leads (`docs/ap/seed_format.md` "The party rules"). In the open
  world and the hub a starting character who is not the hero takes party slot 0 (the hero
  slot 1), and the order the player leaves in the camp menu (who is in slot 0, who leads) comes
  back after a scene reorders the party, except inside a split or a Private Action
  (`docs/ap/seed_format.md` "The hero in slot 1"; debugging: `SS_HEROSLOT_ORDER=LEAD,FIRST`).
  With `options.bench` (seed version 4; open world with the open party rules, and the hub)
  four bench places the port keeps follow the 8 slots: a member received with the 8 taken
  joins the bench, kept whole (slot and record) in the save's sidecar (`bench`, guarded by
  `bench_hash`; the sidecar is version 3 only while someone is benched or lent, so an older
  port refuses it rather than drop them; the card's 8 slots stay a valid console party); the
  camp menu's Party Order flips its right column between the reserve and the bench with L1 /
  R1, and a pick survives the flip; the camp menu's member list, Equipment, Skills, Status, Specialty and Tactics (the same flip) work on
  benched members where they are. Benched members are managed, not used: their settings can be
  changed, but their field specialties, spell casts and Link Combo are refused. A fight the
  story makes a benched member make alone (Claude in Shingo Forest, the tournament's) lends
  them to the 8 as they are, and they go back to their bench place with what the fight gave
  them; the story's Private Actions and party-order saves never see the bench, and inns and the
  endings read the 8 slots only (`docs/ap/seed_format.md` "The bench"; debugging: `SS_BENCH=ID,...`
  puts members on the bench for a `--map` scene test).
- **The hub** (seed version 3, `world_mode: hub`): New Game starts after the Final
  Save Point (chapter 60, disc 2, both worlds open, every location undone) in the seed's start
  town (Fun City, Arlia, or any of 16 towns), with the Synard parked by it; each dungeon opens
  with its key, Fienal once the seed's bosses are beaten. The story stays at chapter 60
  (scenes that would move the chapter or step on are held back, a log line each), and each
  story boss plays its original scene in its chamber: while the boss is unbeaten, the chamber
  sees that boss's own point in the story (a "story view"), and what the scene stores there is
  dropped once it is over, except what the seed keeps. A door the hub needs (the arena
  antechamber's into Marsilio's arena) leads there only while its boss is unbeaten. A chamber
  the seed marks `ask` asks first, in the game's own windows (Shigeo's street in Fun City: "Face
  Shigeo now?"; no lets the party pass, and the street stays quiet until the party has been on
  the world map: once per visit): its scene plays only after a yes; extra options on the
  game's own menus do the rest (the arena desk's "Face Marsilio.", beside "Register." for the
  ordinary arena; the Clerk's "Leave the arena." in Marsilio's scene). Saves keep all of it.
  The open-world party rules, the starting character and the leader stand-in apply as in the
  open world: the player walks as the Formation leader, and the chambers' scenes name them
  where they name the hero (`docs/ap/seed_format.md` "The leader stand-in").
  Debugging: `SS_HUB_TRACE=1` lists what a scene stores in its view; `SS_HUB_STATE=1` builds
  the hub's New Game state on a `--map` debug start; `SS_SWEEP_PLAY=MAP:K16:1/0/0` answers a
  step's choices one by one (`docs/ap/seed_format.md` "Hub": "The port: the chapter lock and the
  story views", "The port: New Game, the starts, the doors, P5", "The port: the questions").
- **Skillsanity, difficulty and join levels** (seed version 2, any world mode unless noted):
  with `skillsanity`, skills and killer moves / spells arrive as items, a guild purchase checks
  each skill in its set, and each learn point is a check (members start with their first move
  only); `skillsanity_shop_share` sells some of them in shops as themselves. `difficulty` sets
  New Game's Battle Rank and skips its screen (`any`: the screen with all three). In open world
  and hub mode a member joins at the party's median level, at any seed version (story mode, no
  seed or Archipelago off: the original levels). Shop slots give what was bought and the
  slot's check, marked with the Archipelago logo until checked. `docs/ap/seed_format.md`
  "Skillsanity", "Difficulty", "The join level", "Shop slots".
- `--ap-seed FILE` loads a seed for a one-off run without saving it to config.ini; the
  server / slot / password, if config.ini already has them (from setup), still apply.
- Debug console (`--debug`): `ap_status`, `ap_check ID`, `ap_give ITEM [FROM]` (ITEM: an AP
  id or name, a game item id, a character's name, or `trap:ambush`, `trap:fol_loss[:PCT]`,
  `trap:status[:poison|paralysis|stone]`), `ap_deathlink [FROM]`.

`apworld/so2/patches.py` turns a placement into the seed's placeholder patches.

*Developer note:* `tools/ap_test_server.sh` runs a local, 127.0.0.1-only Archipelago
server (and `start-wss` a TLS one with a self-signed certificate) for testing the
network client without touching a public server; `tools/ap_e2e.sh` plays a scripted
multiworld session against it. Neither is needed, or useful, to a player — a real seed
is hosted by the room the player was given.

## Tests

```sh
SS_DISC1=".../Star Ocean - The Second Story (Disc 1).cue" engine/build/ss_tests
SS_DISC1=".../Star Ocean - The Second Story (Disc 1).cue" engine/build/ss_game_tests
tools/smoke_field_events.sh engine/build/shattered-stars   # whole-game headless checks
```

The disc tests compare against reference values from `tools/so2_archive.py` and the
live RAM dumps; without `SS_DISC1` they are skipped and only the unit tests run.

## Layout

| Path | Contents |
|---|---|
| `src/disc/` | disc image, archive table, SLZ |
| `src/psx/` | replacements for the PS1 hardware (GPU, GTE, MDEC) |
| `src/runtime/` | PsyQ-shaped libraries (libgpu, libgte, VSync) over the host |
| `src/platform/` | SDL3 window, OpenGL presentation, input, the programs' icon and app ids |
| `src/game/` | ported game code, named as in `docs/catalog/functions.csv` |
| `src/util/` | PNG writer |
| `tests/` | unit and disc regression tests |
