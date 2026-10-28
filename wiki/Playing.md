# Playing

Shattered Stars plays like the original game. This page covers the controls, saving, the disc
change, the settings, and the few things that are different from the PlayStation.

<table>
  <tr>
    <td width="50%"><img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/field-arlia.png" alt="Claude in the village of Arlia, near its fountain"><br><sub>A field map: Arlia, an early town on Expel</sub></td>
    <td width="50%"><img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/world-map.png" alt="The world map, with the party leader approaching a town gate and the minimap in the corner"><br><sub>The world map</sub></td>
  </tr>
  <tr>
    <td><img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/battle-spell.png" alt="A forest battle with an ice spell and Thunderbolt on screen at once"><br><sub>A battle, with two spells on screen at once (party stats raised for the capture)</sub></td>
    <td><img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/camp-status.png" alt="The camp menu's Status page for Claude: stats, equipment and portrait"><br><sub>The camp menu's Status page</sub></td>
  </tr>
</table>

## Controls

The game reads your keyboard or gamepad as a PlayStation controller. These are the defaults:

| PlayStation button | Keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| Cross | Z | South button (A on Xbox-style pads) |
| Circle | X | East button (B) |
| Square | A | West button (X) |
| Triangle | S | North button (Y) |
| Start | Enter | Start |
| Select | Backspace | Back / Select |
| L1 / R1 | Q / W | Left / right shoulder |
| L2 / R2 | 1 / 2 | Left / right trigger |
| L3 / R3 | (none) | Left / right stick click |

**Gamepads** work through SDL and act as a DualShock in analog mode. The left stick walks, as
it did on the console, and also works the d-pad in menus. The game's own Vibration setting
drives the rumble. The keyboard on its own acts as a plain digital pad.

**Rebinding:** open the setup program's **Controls** page, click a box and press the key or
button you want. See [Installing and Setup](Installing-and-Setup#controls).

**Window keys** while playing:

| Key | Does |
|---|---|
| F11 or Alt+Enter | Switch between the window and full screen |
| Esc | Pause and ask "Quit Shattered Stars?" |

**Esc asks before quitting.** The game pauses (picture, sound and all) under a small window with
two choices, **Keep playing** (highlighted) and **Quit**. Left / Right choose, and Enter, or the
button you use as Cross, picks one; Esc again, or Circle, goes back to the game exactly where it
was. Quit closes the game: progress since your last save is lost. The window's close button and
Alt+F4 still close the game straight away. Esc and F11 are the port's own keys, so they can't be
bound to a PlayStation button.

## Saves and memory cards

The game saves exactly as on the PlayStation: at save points, onto a memory card. The card is
a file on your computer.

- **Card 1** is `memcard1.mcd` in the settings folder (`%APPDATA%\ShatteredStars\` on Windows,
  `~/.config/shattered-stars/` on Linux). It is created, already formatted, the first time you
  play.
- **Card 2** is empty unless you insert one on the setup program's **Memory cards** page.
- Each card has 15 blocks, and a save takes one block.

**Saves are byte-compatible with the original.** A save made in Shattered Stars loads in the
original game (for example in an emulator), and a save made in the original loads in
Shattered Stars with the same state. That means you can:

- carry on from a save made in an emulator: point a slot at its raw 128 KB card (`.mcd` or
  `.mcr`) on the **Memory cards** page;
- use a DexDrive save (`.gme`): the **Make a .mcd copy** button on the same page converts it
  into a card;
- take a Shattered Stars card back to an emulator.

**Each release keeps its own saves.** A save is written in the format of the release you play,
with that release's product code (`BASCUS-94421` North American, `BASCES-02159` European,
`BESCES-02160` French, `BESCES-02161` German), save signature and icon setting, so the card
works with that release's console and emulators. Checked in PCSX-Redux: the North American,
European, French and German originals each list and load a save Shattered Stars wrote, and
Shattered Stars loads a save the French original wrote. The game uses the saves of the release
you play; importing a save from another release isn't offered yet. The setup program's
**Memory cards** page counts the saves of every release.

The game writes to the card it uses, so work on a copy when you try an old card for the first
time. To back up your saves, copy the card file.

**Archipelago seeds have memory cards of their own.** While you play an
[Archipelago](Archipelago#your-saves-a-memory-card-for-each-seed) seed, the game saves on that
seed's own card (`archipelago/<seed>_<slot>/memcard1.mcd` in the settings folder), never on your
ordinary cards, and only that seed's own saves load. Each save's Archipelago progress is kept in
a small file beside the card (for example `memcard1.slot03.ap.json`), so the card itself stays
compatible with the console.

## Changing discs

The story moves from Disc 1 to Disc 2 partway through, as on the console.

- If you chose Disc 2 in the setup program, the change happens by itself where the original
  asked you to swap discs. The game also finds Disc 2 on its own in Disc 1's folder, in a
  folder named like Disc 1's (for example "... (Disc 2)" beside "... (Disc 1)"), or in a
  `discs` folder.
- If not, the game shows the original's "insert disc" screen. Choose Disc 2 in the setup
  program, then press the action button (it stands in for closing the disc lid). The game
  checks the images again and says "Wrong Disc" if it still can't find Disc 2.

Disc 2 must come from the same release as Disc 1. The game looks only for that release's
Disc 2, and leaves out a Disc 2 of another release.

## Settings

All settings are in the setup program, which writes them to `config.ini`:

| Page | What you set |
|---|---|
| Discs | Your Disc 1 and Disc 2 images |
| Display | Windowed, borderless or full screen; window size or resolution; monitor; vertical sync; dithering |
| Sound | Sound on or off; volume |
| Controls | Keyboard and gamepad buttons; the analog stick |
| Gameplay | Instant text; auto-advance and its wait |
| Memory cards | The card file in each slot |
| Mods | The mods folder; which mods are enabled, and their order |
| Archipelago | On or off; the seed (the generator's zip or a seed file) and your slot; the server, slot name and password |

The game's own options, such as vibration, are in its in-game menus, as on the console.

Each page is described on [Installing and Setup](Installing-and-Setup#the-setup-program).

## What's different from the console

Shattered Stars aims to play **better** than the original without changing **how** it plays.
Hit timing, damage, difficulty and pacing are the original's.

| | On the console | In Shattered Stars |
|---|---|---|
| Slowdown | The game slows down when the screen is busy, for example several enemies casting at once | No slowdown: the game always runs at full speed |
| World map | Usually runs at 20 frames a second | Runs at its full frame rate |
| Loading | Waits for the disc | Loads at once |
| The original's bugs | Present | Fixed where the fix doesn't change how the game plays; see [Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept) |
| Picture | Fixed TV picture | Any window size or full screen; dithering optional |
| Saves | Memory cards | Memory card files, compatible both ways |
| Messages | Typed out letter by letter; wait for the button | The same by default; optionally instant, and moving on by themselves (never past a choice) |
| Extras | | [Archipelago](Archipelago) and [mods](Modding) |

Because loading is instant, a character's talking pose during a battle voice line can be a
few frames shorter than on the console, which waited for the voice clip to load from the
disc. Nothing else about the battle changes.

The original showed a notice when the disc lid was open or a read failed. Disc images never do
either, so those notices don't appear.

## European, French and German discs

These releases play with their own text and data: the translation and its fonts, the French
and German name-entry keyboards and menus, the staff in the credits, and the rumble lengths of
their scenes. Underneath, the game logic is the North American one, running at 60 Hz, so they
play the same verified game at the full frame rate.

The PAL originals ran at 50 frames a second and changed a few things, mostly for that. Shattered Stars
documents those changes but doesn't reproduce them:

| | On a PAL console | In Shattered Stars |
|---|---|---|
| Frame rate | 50 frames a second | 60, as in the North American release |
| Movement and camera | Each frame's step scaled by 6/5, to make up for the slower frame rate | The North American steps |
| Anything counted in frames (waits, battle timers, animations) | Runs 5/6 as fast in real time | The North American pace |
| Projectile clash | Removed | On, as in the North American release: opposing elemental projectiles whose hit boxes overlap cancel or overpower each other |
| Spells that never clash | The North American seven, plus the item spells The Scream and Judgement Day | The North American seven, so those two item spells can clash with another caster's spell |
| The credits | Timed in 50 Hz frames | The same timing, converted to 60 Hz |

An [Archipelago](Archipelago) seed works with any of the four releases, and
[mods](Modding#mods-and-the-releases) apply to every release.

## Command-line options

You never need these to play, but they can be handy. Run `shattered-stars --help` for the full
list.

| Option | Does |
|---|---|
| `--disc1 IMAGE`, `--disc2 IMAGE` | Use these disc images (remembered in `config.ini`) |
| `--memcard1 FILE`, `--memcard2 FILE` | Use these card files |
| `--no-memcard` | Play with no memory card |
| `--dither`, `--no-dither` | Turn dithering on or off |
| `--mods DIR` | Read mods from another folder |
| `--ap-seed FILE` | Play an [Archipelago](Archipelago#playing-offline) seed for this run only: the generator's zip or a `.apso2` |
| `--ap-seed-entry NAME` | With `--ap-seed`: which seed to play, when the zip holds several Star Ocean seeds |
| `--render-tracker-maps [DIR]` | Draw the [PopTracker pack's maps](Trackers#maps-from-your-own-disc) from your Disc 1 into PopTracker's override folder for the pack (or into DIR), then exit. Opens no window, saves nothing else |
| `--debug` | Open a debug console in the terminal, for testing (see below) |
| `--version` | Print the version and exit |

**The debug console** (`--debug`) is meant for testing and bug reports. You type commands into
the terminal the game was started from; `help` lists them. They include `where` (where you
are), `warp` (move to another map), `fol`, `item`, `heal`, `flag`, `battle` and `noclip`. A
status line (map, the leader's position, Fol) is shown over the picture and in the window
title ("Shattered Stars - map 18 | ..."). These commands change your game directly, so use them on a copy of your memory card.

**Not in an Archipelago game.** While a seed is being played, the debug console and the other
testing switches (starting on a chosen map or in a battle, scripted input, memory cards named on
the command line) are ignored. Turn Archipelago off in the setup program to use them.
