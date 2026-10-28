# Shattered Stars

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/title-screen.png" alt="The Shattered Stars title screen: the Star Ocean logo over a starfield, with New Game, Continue and Voice Collection" width="720">
</p>

**Shattered Stars** is a native PC port of *Star Ocean: The Second Story*, the PlayStation
role-playing game, in its North American, European (English), French and German releases. The
game's code has been rewritten by hand in modern C++ (C++20), with SDL3 and OpenGL underneath. It runs on Windows and Linux as an
ordinary program and plays from images of your own game discs.

It is **not an emulator**, and it contains **no game code or data**. Everything the game
needs (maps, models, music, text, movies) is read from your disc images while you play.

- Source code: [github.com/IronJade/Shattered-Stars](https://github.com/IronJade/Shattered-Stars)
- Version: 0.03.00 (the Archipelago world, `so2.apworld`, is 0.2.2 and the PopTracker pack
  1.0.0, each released on its own)
- Licence: MIT
- Maintainer: IronJade

## Highlights

- **The whole game, start to finish.** Both discs, every field map, the world map, every
  battle and all 85 private endings. The game can be finished from New Game to the credits.
- **Checked against the original.** Each system was compared with the original game running
  in the PCSX-Redux emulator: screens, battles, random numbers, sound and save data.
- **Full speed.** No slowdown when the screen gets busy, and no waiting for the disc to load.
- **Your saves still work.** Saves use memory card images that are byte-compatible with the
  original. Cards from emulators work, and DexDrive saves can be converted.
- **Bugs fixed, gameplay kept.** The original's bugs are corrected only where the fix doesn't
  change how the game plays. Every decision is listed on
  [Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept).
- **Keyboard or gamepad.** Every button can be rebound. Gamepads have analog walking and
  rumble.
- **Window or full screen.** Choose windowed, borderless or exclusive full screen, with
  optional PlayStation-style colour dithering.
- **Faster dialogue, if you want it.** Optional instant text and auto-advance on the setup
  program's Gameplay page; both off by default, and they never answer a choice for you.
- **Archipelago built in.** Play in an [Archipelago](Archipelago) multiworld randomizer, with
  the network client built into the game: the story, an open world, or a hub with the whole
  game open from the start, with a party of up to twelve.
- **Trackers that follow you.** [Universal Tracker](Trackers#universal-tracker) and a
  [PopTracker pack](Trackers#poptracker) show what you can reach by the world's own logic. The game
  tells them the map you're on and how far the story has got, and draws the pack's maps from your
  own disc.
- **Four releases.** The North American, European, French and German discs all play, each
  with its own text, on the same verified game logic ([Installing and Setup](Installing-and-Setup#your-disc-images)).
- **Mods without touching your discs.** Small patch files can change chests, doors, items,
  shops and enemy stats as the game loads ([Modding](Modding)).

## Status

The port is complete enough to play the whole game:

| Part | State |
|---|---|
| Title screen, logos and movies | Done |
| Field maps, events, shops, menus, item creation | Done, both discs |
| World map, vehicles | Done |
| Battles: every enemy, character, spell and item | Done; all 564 encounters run without crashing |
| Disc change | Done; the game moves to Disc 2 where the original asks for it |
| Endings | All 85 private endings play |
| Saves | Byte-compatible with real memory cards, in each release's own format |
| Releases | North American, European (English), French and German; Japanese later |
| Archipelago | Playable: story, open-world and hub modes; entrance shuffle is planned |
| Trackers | Universal Tracker and a PopTracker pack, with story awareness and maps from your own disc ([Trackers](Trackers)) |
| Mod manager | Done: packages, load order, conflict warnings and a verified list ([Modding](Modding)) |

Download the [latest release](https://github.com/IronJade/Shattered-Stars/releases/latest) (0.03.00), or [build it from source](Building-from-Source).
The Archipelago world and the PopTracker pack have releases of their own:
[so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2) and
[PopTracker pack 1.0.0](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0).

## Pages

**Playing**
- [Installing and Setup](Installing-and-Setup): downloads, requirements, the supported discs,
  choosing your disc images, and the first launch
- [Playing](Playing): controls, saves, the disc change, settings, and what's different from
  the console
- [Bugs Fixed and Quirks Kept](Bugs-Fixed-and-Quirks-Kept): every bug in the original game and
  how the port handles it
- [Installing and Managing Mods](Installing-and-Managing-Mods): installing, ordering and
  removing mods in the setup program
- [Archipelago](Archipelago): the multiworld randomizer
- [Trackers](Trackers): Universal Tracker, the PopTracker pack, and the maps from your own disc
- [FAQ](FAQ): common questions

**Making and modding**
- [Modding](Modding): an overview of mods and the mod manager
- [Making a Mod](Making-a-Mod): a step-by-step guide to making your own
- [Submitting a Mod](Submitting-a-Mod): getting a mod onto the verified list
- [Building from Source](Building-from-Source): compile the game and run its tests
- [How the Port Was Made](How-the-Port-Was-Made): the method, for technical readers
- [Contributing](Contributing): reporting bugs and proposing fixes

## Legal

Shattered Stars contains no code, graphics, sound or other data from *Star Ocean: The Second
Story*. To play, you need your own copy of the game's discs. This project is not affiliated
with or endorsed by Square Enix, tri-Ace or Sony Interactive Entertainment. *Star Ocean* is a
trademark of Square Enix. PlayStation is a trademark of Sony Interactive Entertainment.
