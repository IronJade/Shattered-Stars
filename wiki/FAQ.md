# FAQ

## About the project

### Do I need the game?

Yes. Shattered Stars contains no game data at all. You need images of your own *Star Ocean: The
Second Story* discs (the PlayStation version: the North American, European, French or German
release). Every map, model, sound
and line of text is read from those images while you play. Nothing is included with the
download, and nowhere offers the game data.

### Is this an emulator?

No. An emulator runs the original PlayStation program on a simulated console. Shattered Stars
is the game's code rewritten by hand in modern C++, running as a normal PC program. Only the
game's data comes from your discs; none of its original code runs. See
[How the Port Was Made](How-the-Port-Was-Made).

### Is it the same game?

Yes. Hit timing, damage, difficulty and pacing were measured against the original running in an
emulator, and they match. The differences are improvements that don't change how the game
plays: no slowdown, no loading waits, a window of any size, and fixes for bugs in the original.
See [Playing](Playing#whats-different-from-the-console) and
[Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept).

### Can I finish the game?

Yes. The whole game plays from New Game to the credits: both discs, every field map, every
battle and all 85 private endings.

### Is this related to *The Second Story R*?

No. Shattered Stars is a port of the original PlayStation game, not of the 2023 remake.

### Is it legal?

Shattered Stars contains no code, graphics, sound, text or other data from the game. Its own
code is released under the MIT licence. To play, you supply images made from discs you own;
the laws on making such copies differ between countries, so check the rules where you live.

This project is not affiliated with or endorsed by Square Enix, tri-Ace or Sony Interactive
Entertainment. *Star Ocean* is a trademark of Square Enix. PlayStation is a trademark of Sony
Interactive Entertainment.

### Who makes it?

Shattered Stars is maintained by IronJade. The source is on
[GitHub](https://github.com/IronJade/Shattered-Stars).

## Discs and saves

### Do PAL (European) discs work?

Yes, from version 0.02.00: the European (English, SCES-02159 / SCES-12159), French
(SCES-02160 / SCES-12160) and German (SCES-02161 / SCES-12161) releases all play, as well as the
North American one (SCUS-94421 / SCUS-94422). Both discs must come from the same release. See
[Installing and Setup](Installing-and-Setup#your-disc-images).

### Do the PAL discs run at 50 Hz?

No. They play with their own text and data on the North American game logic at 60 Hz: the
same gameplay as the North American release, at the full frame rate. The PAL versions'
changes for 50 Hz, such as movement scaled by 6/5 per frame and the removed projectile clash,
are not reproduced. See [Playing](Playing#european-french-and-german-discs).

### Do Japanese discs work?

Not yet. The setup program recognises the Japanese discs (SLPM-86105 / SLPM-86106) and says
they aren't supported yet. The Japanese release is a different build of the game, with its own
changes to enemies, party moves, spells and text, so it is planned for a later release.

### Can I use a save from another release?

Not yet. Each release keeps its own saves, as on the console: a save carries its release's
product code and signature, and the game uses the saves of the release you play. Importing a
save from another release isn't offered yet.

### What disc image formats work?

`.cue` with `.bin`, `.iso` and `.img`. CHD, PBP, ECM, CSO, MDS and CCD images can't be read yet,
so convert them to `.cue` / `.bin` first. See
[Installing and Setup](Installing-and-Setup#your-disc-images).

### Do I need both discs to start?

No. Disc 2 is only needed when the story changes discs. If it isn't set up by then, the game
shows the original's "insert disc" screen and waits for you to choose it. (An Archipelago seed
in hub mode starts where the story would be on Disc 2, so it needs Disc 2 from New Game.)

### Will my saves work?

Yes. Saves are byte-compatible with the original's memory card saves. A raw memory card from an
emulator (`.mcd` or `.mcr`) works as it is; a DexDrive save (`.gme`) can be converted in the
setup program. Saves made in Shattered Stars also load in the original game. See
[Playing](Playing#saves-and-memory-cards).

### Where are my saves?

In `memcard1.mcd`, in the settings folder: `%APPDATA%\ShatteredStars\` on Windows,
`~/.config/shattered-stars/` on Linux. Copy that file to back them up. An Archipelago seed saves
on a card of its own, in the `archipelago` folder there; see
[Archipelago](Archipelago#your-saves-a-memory-card-for-each-seed).

## Running it

### Which systems are supported?

64-bit Windows, and 64-bit Linux with glibc 2.36 or newer. Both need OpenGL 3.3. There is no
macOS build today.

### Does it work with a gamepad?

Yes. Any gamepad SDL recognises works, and acts as a DualShock: the left stick walks, and the
game's Vibration setting drives the rumble. Every button can be rebound in the setup program.

### Can I play in full screen?

Yes. Choose borderless or exclusive full screen in the setup program, or press F11 or Alt+Enter
while playing.

### Can the dialogue go faster?

Yes, if you want it to. The setup program's Gameplay page has **Instant text** (each message
page appears at once) and **Auto-advance** (a message moves on by itself after a wait you
choose). Both are off by default, and auto-advance always stops at a choice. See
[Installing and Setup](Installing-and-Setup#gameplay).

### Is there widescreen or higher-resolution rendering?

No. The picture is the original's, scaled to fit the window or screen while keeping its shape.
You can turn the PlayStation's colour dithering on or off.

### Where do I download it?

From the [Releases page](https://github.com/IronJade/Shattered-Stars/releases/latest): the Windows zip or the Linux tarball. You can also
[build it from source](Building-from-Source). The Archipelago world (`so2.apworld` and its
options template) and the PopTracker pack have releases of their own, apart from the game's:
[so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2) and
[PopTracker pack 1.0.0](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0).

## Archipelago and mods

### Does it support Archipelago?

Yes, with the network client built into the game, in three world modes: the story, an open
world, or a hub with the whole game open from the start. See [Archipelago](Archipelago).

### Which version of the Archipelago world do I need?

`so2.apworld` 0.2.2, from [its own release](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2), with Shattered Stars 0.03.00. A seed made
with the bench (on by default in open world with `character_items` and in hub mode) needs
Shattered Stars 0.02.51 or later; Shattered Stars 0.03.00 also plays seeds made with 0.2.1, 0.2.0,
0.1.1 and 0.1.0. See [Archipelago](Archipelago#what-changed-in-022).

### Does it work with trackers?

Yes: [Universal Tracker](Trackers#universal-tracker), with no YAML, and a
[PopTracker pack](Trackers#poptracker) made for this world. With Shattered Stars 0.03.00 connected,
both follow your game: the map you're on, and in story mode how far the story has got. The pack's
real maps are drawn from your own disc by the game. See [Trackers](Trackers).

### Where did my saves go while I play a seed?

An Archipelago seed plays on its own memory card, so your ordinary saves and the seed's never
mix. Turn Archipelago off with the switch in the setup program (or clear the seed) to play on
your ordinary cards again. See [Archipelago](Archipelago#your-saves-a-memory-card-for-each-seed).

### Does a seed work with my European, French or German discs?

Yes. One seed works with the discs of any supported release, so the players of a multiworld
can each use their own. If you play `world_mode: open_world`, don't use a seed made with
`so2.apworld` 0.1.0: some of its open-world seeds can't be finished. See
[Archipelago](Archipelago#what-changed-in-011).

### Can I mod it?

Yes. Mods change chests, doors, items, shops and enemy stats as the game loads, without
touching your discs, and the setup program's Mods page installs and arranges them. See
[Installing and Managing Mods](Installing-and-Managing-Mods), or [Making a Mod](Making-a-Mod)
to make your own.

### Do mods work on European, French and German discs?

Yes. A mod applies to every supported release. Each line is checked against the data of the
disc you play, and a line whose data differs in that release is skipped, so a mod never
corrupts anything. See [Modding](Modding#mods-and-the-releases).

## Problems

### The game says my disc isn't right.

Make sure the image is *Star Ocean: The Second Story* from a supported release (North
American, European, French or German), that both discs come from the same release, and that
the right disc is in the right box. The message under each disc on the setup program's Discs
page says what it found.

### My character walks by itself.

Raise the analog stick's dead zone on the setup program's Controls page.

### I found a bug.

Please report it. See [Contributing](Contributing#reporting-a-bug) for what to include.
