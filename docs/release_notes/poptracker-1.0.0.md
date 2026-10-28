# PopTracker pack 1.0.0 (Star Ocean: The Second Story, Shattered Stars)

> **DRAFT** (2026-10-27): not released. Written by releasedocs from docs/ap/poptracker.md and the
> Phase B logs (packgen, logicexport, storyaware, maptab; docs/handoff/phaseb.md) and finished by
> rc030. Left for release time (docs/ap/release_checklist.md section 12): the SHA-256 sum.

The first release of the [PopTracker](https://github.com/black-sliver/PopTracker) pack for
*Star Ocean: The Second Story* played on Shattered Stars with Archipelago. It connects to your
room as a tracker and shows, on maps and boards, what your slot has received and which of the
room's checks are done, green where a check is in logic and red where items are missing, exactly
as the Archipelago world's own rules say. With the game connected, its map tabs follow you, and in
story mode it shows what the story hasn't reached yet in blue. Its maps can be drawn from your own
disc by the game.

It has its own release, apart from [the game's](https://github.com/IronJade/Shattered-Stars/releases/tag/v0.03.00)
and [the Archipelago world's](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2):
[Shattered Stars 0.03.00](https://github.com/IronJade/Shattered-Stars/releases/tag/v0.03.00) and
[so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2) go with it.

## What you need

| | |
|---|---|
| **PopTracker** | 0.36.0 or newer, from [its releases](https://github.com/black-sliver/PopTracker/releases) (Windows, Linux, macOS) |
| **A room** | made with **so2.apworld 0.2.2** for the logic and story awareness. A room made with 0.2.0 or 0.2.1 works as a checklist: every check shows as reachable, and the banner says so |
| **The game** | **Shattered Stars 0.03.00** connected to the same room, for the map tabs that follow you and for story awareness. Items and checks reach the pack through the server with any version |
| **Your disc 1** | only for the real maps (below); the pack works with its plain stand-ins without it |

## What it shows

- **Maps.** Expel and Energy Nede (their world map, then a tab per area group, then a tab per area
  inside a group of several), Fienal, and the Cave of Trials (a tab per level). A square per spot
  on an area map: checks at the same spot (one person's several gifts, the Cave's reused chest
  rooms) share a square, with one entry per check. The world maps have a square per area group that
  sums up the checks behind it. 472 chests, gifts, bosses and recruitments are placed, on 408
  squares.
- **Boards** for the checks that are no place: Shops (a cell per shop), Item Creation (per
  specialty), Killer Moves (per member), Guilds and Learning (skill guilds, learn points per
  member) and Enemy Drops (per area).
- **Only your room's checks.** On connecting, the pack reads which locations the room has and hides
  the rest.
- **Items** on the left: the area keys, hub mode's dungeon keys, the characters (a member outside
  your room's pool is marked "out"), traps received, skills and moves (Skillsanity), and **Room**:
  your room's options.
- **Logic from the room.** A room made with so2.apworld 0.2.2 carries the world's own logic, and the
  pack evaluates it: a check is green when it is in logic with what you have received (and the
  items you click), red when not. The same as Universal Tracker, for every option and seed, with no
  game logic copied into the pack.
- **Story awareness** (story mode, with Shattered Stars 0.03.00 connected). Story mode's chapters
  need no items, so the logic alone puts nearly every check in reach from the start. The pack
  follows how far the story has got and shows a check that is in logic by your items but **later in
  the story** in blue (PopTracker's "Inspect"). Green is what the story has reached; red still
  needs items. It follows the chapter live, also back when you load an earlier save. The
  **Chapter** item shows the chapter; clicking it sets one by hand (left +1, right -1) when the game
  isn't connected. The **Story** item (on by default) turns story awareness off. Open world and
  hub mode are as the logic says.
- **The map follows you** (with Shattered Stars 0.03.00 connected): the tabs switch to the map the
  party is on. The **Auto tab** item (on by default) turns it off.
- **Hints**: hinted locations in your world are highlighted.
- **Rooms and doors.** Each area's tab opens into **Overview** plus a tab per room (794 of
  them), titled by the game's own map number, in story order. A room's doorways carry a small
  lettered marker each (1,775 in all), and a **Doors** panel beside the map says where each one
  leads, for example "A -> 412 Salva Drift" (a trailing `*` for a story-only door). Click a
  panel line, or a doorway's own marker, and the room it leads to opens. With the game connected,
  the tabs follow you room by room, not just area by area (**Auto tab**, above).

The pack only listens: it sends no checks and no hints. Two variants: **Map Tracker** (maps,
boards and items) and **Items Only**.

## Installing

1. Install PopTracker 0.36.0 or newer.
2. Drag `so2_shattered_stars_1.0.0.zip` into PopTracker's window, or copy it (unpacked or not) into
   a `packs` folder PopTracker reads: `PopTracker\packs` in your user folder or in Documents, or
   `packs` beside the program.
3. **Load** -> **Star Ocean: The Second Story (Shattered Stars)** -> **Map Tracker** (or Items
   Only).
4. Click **AP** and connect to your room: its address (`host:port`), your slot name and the room's
   password. The game can be connected to the same slot at the same time.

**Updates.** The pack has an update address. When you load it, PopTracker checks for a newer
version and asks: "A pack update to version ... is available. Download from ... ?". Yes downloads
and installs it; No asks whether to skip that version.

## The real maps, from your own disc

The pack ships only plain stand-in maps, drawn for it: a grid where each room goes, its name, and
a hint to render the real one. **Nothing from the game is in the pack or in any release of this
project**, so nothing of the game's art is published. The game draws the real maps for you, from
your own disc 1, into the folder PopTracker reads for this pack:

- **In the setup program**: the **Archipelago** page, **Trackers**, **Render tracker maps for
  PopTracker**. It shows its progress and says where the images went, with **Open folder**.
- **From a terminal**: `shattered-stars --render-tracker-maps` (`shattered-stars.exe` on Windows);
  with a folder after it, into that folder instead.

It writes 862 PNG images (about 135 MB, about 20 seconds on the development machine) into
`PopTracker/user-override/so2_shattered_stars`: in your user folder if PopTracker keeps its folder
there (it does once it has installed a pack), else in your Documents folder. PopTracker uses files
there in place of the pack's own, so the two world maps, the area overviews and every room's own
tab show the real backgrounds, each room's name above it, and the squares sit on the chests and
the people who hand things out. Load the pack again to see them. Render again after updating the
pack, in case its maps changed. These pictures are for your own use: don't share them.

A portable PopTracker (`portable.txt` beside it) reads overrides only from its own folder: give the
game that folder, `--render-tracker-maps "<PopTracker's folder>/user-override/so2_shattered_stars"`.

## Known limitations

- **The map, the story and your checks update while the game is online.** The pack hears of a
  check from the server, so checks made while the game is offline appear once it reconnects and
  sends them; the map tabs and the chapter follow from then on too.
- **Story awareness follows stages, not single scenes**: a gift first given in the middle of a stage
  shows from the stage's start; Fienal's floors count from chapter 58, though the party enters at
  chapter 60; the Final Save Point counts from the step before the save. Rooms made before
  so2.apworld 0.2.2 have no story awareness.
- **No "out of logic" (yellow).** The room's logic says what is in logic, not what could be done
  out of it: a check is green, blue or red.
- **Older rooms are a checklist** (so2.apworld 0.2.0 and 0.2.1): every check shows as reachable.
- **Shops and other boards are not on the maps**: the people who run shops have no squares; the
  shops are on the Shops board.
- **Not in PopTracker's own pack list yet**: install it from this release. Updates still come
  through PopTracker's update check.

## What's in this release

| File | What it is |
|---|---|
| `so2_shattered_stars_1.0.0.zip` | The pack (uid `so2_shattered_stars`, version 1.0.0), for PopTracker 0.36.0 and newer. Install it as it is, without unpacking. |
| `SHA256SUMS` | Its SHA-256 sum (also below). |

The pack is generated in this repository from the Archipelago world's own tables
(`tools/ap_poptracker.py`, `tools/build_poptracker.sh`; docs/ap/poptracker.md), so its locations,
items and areas can't drift from the world's: all 1,991 locations of the world have their entry,
and 264 of its items (every key, character, trap, skill and move) their place in the item panel.
It has 794 room maps and 1,775 door locations (never counted as checks), 1,201 files in all,
about 5.6 MB zipped. It was checked with PopTracker's pack checker in strict mode (no warning),
its own tests (41: the ids, the layout, the Lua run in Lua 5.4, the logic equal to the world's
over 16 rooms and 246 item states, story awareness at 15 positions, the room placeholders, the
Lua door panel and its click handler), and the real PopTracker 0.36.0 against local rooms (44
checks: both variants, autotracking, the logic at three points of a game, the story room at six
positions, and a door click that opens its destination room's tab).

## Licence

The pack is released under the [MIT licence](https://github.com/IronJade/Shattered-Stars/blob/main/LICENSE),
like Shattered Stars. Its images are original: drawn by the generator from plain shapes and text,
with Pillow's bundled font, Aileron Regular (by Sora Sagano, CC0 1.0). It contains nothing from
*Star Ocean: The Second Story*. This project is not affiliated with or endorsed by Square Enix,
tri-Ace or Sony Interactive Entertainment.

## SHA-256 sum

```
TODO  so2_shattered_stars_1.0.0.zip
```
