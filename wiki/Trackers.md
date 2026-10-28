# Trackers

Two popular Archipelago trackers work with *Star Ocean: The Second Story* on Shattered Stars:
[Universal Tracker](https://github.com/FarisTheAncient/Archipelago/releases) and
[PopTracker](https://github.com/black-sliver/PopTracker). Both connect to your
[Archipelago](Archipelago) room next to the game, and show what you can reach by this world's own
logic, from what the server says you have received and checked.

This page describes **Shattered Stars 0.03.00**, **`so2.apworld` 0.2.2** and the **PopTracker pack
1.0.0**. Each has a release of its own:
[Shattered Stars 0.03.00](https://github.com/IronJade/Shattered-Stars/releases/tag/v0.03.00),
[so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2) and
[PopTracker pack 1.0.0](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0).

## What you need

| | Universal Tracker | PopTracker |
|---|---|---|
| **The tracker** | Universal Tracker 0.3.6 or newer (`tracker.apworld`), in Archipelago 0.6.8 or newer | PopTracker 0.36.0 or newer, and the pack `so2_shattered_stars_1.0.0.zip` |
| **The room** | made with `so2.apworld` 0.2.0 or later, tracked with 0.2.2 installed (see [Older rooms](#older-rooms)) | made with `so2.apworld` 0.2.2 for the logic and story awareness; an older room is a checklist |
| **A YAML** | not needed | not needed |
| **The game** | any Shattered Stars for items and checks; 0.03.00 for [story awareness](#story-awareness-story-mode) | any for items and checks; 0.03.00 for the tabs that follow you and for story awareness |
| **Your disc** | | disc 1, for the [real maps](#maps-from-your-own-disc) (optional) |

## How trackers see your game

- **They connect to your slot**, like the game: the room's address, your slot name and the room's
  password. One slot can have several connections at once: the game, Universal Tracker and
  PopTracker can all be connected together.
- **They only listen.** The server takes no checks from a tracker, and neither tracker sends any.
- **Items and checks come from the server.** A tracker knows a check once the server does.
- **With Shattered Stars 0.03.00 the game also tells them where you are.** While it is connected
  to its room, it keeps two values in the server's data storage: the map you're on (and on a world
  map, your position there), and the story's chapter and step. They are updated when they change,
  at most twice a second, and again on every connect, so a tracker that connects later reads them
  at once. Played offline, with Archipelago off or with another room's seed, the game writes
  nothing, and nothing of this goes into your saves.

### Trackers update when the game is online

Checks you make while the game is offline (no server, or the connection down) wait in the game,
and reach the server, and your trackers, when it reconnects. The map tab and the chapter follow
again from then on. So a tracker may lag behind an offline stretch, and catches up on the
reconnect.

## Universal Tracker

Universal Tracker (UT) is an Archipelago client. It loads `so2.apworld` itself, builds your room's
world again on your machine and lists the locations you can reach now and haven't checked yet.

### Setting it up

1. Download `tracker.apworld` from
   [Universal Tracker's releases](https://github.com/FarisTheAncient/Archipelago/releases)
   (Tracker_v0.3.6 or newer).
2. Put `tracker.apworld` and **the room's `so2.apworld`** into Archipelago's `custom_worlds`
   folder. For a room made with 0.2.0, 0.2.1 or 0.2.2 that is `so2.apworld` 0.2.2. Keep only one
   `so2.apworld`: Archipelago refuses to load the same game twice.
3. Restart the Launcher and open **Universal Tracker**.
4. Connect to the room with your slot name (and the password), like a text client.

**No YAML.** With `so2.apworld` 0.2.2 UT rebuilds the world from the room's own data: every
option, your party pool and, in hub mode, your start town. Nothing for this game goes into UT's
Players folder.

### What it shows

- **The locations in logic and still to check**, exactly as the generator counted on them, in the
  order the story reaches the areas: Arlia first and the Cave of Trials last, then the skill guilds,
  learn levels, Item Creation and killer moves. Excluded locations (`exclude_locations`) are in
  UT's excluded group.
- **`/explain <location>`** prints what a location needs, with what you have in green and what is
  missing in red, then its region and the ways into it. For example
  `/explain Town of Salva: Heavy Ring chest`.
- **A room it can't track is refused**, never tracked wrong: UT says "The world has informed UT
  that your options are incompatible", and `/faris_asked` prints a message naming the
  `so2.apworld` to install (a room made with a newer world, for example).
- **A map tab**, following the room the game reports and showing a marker for your party on the
  two world maps; its pictures come from a zip UT asks for once: your own maps
  (`so2_shattered_stars_ut.zip`, from the [render](#maps-from-your-own-disc)) or the PopTracker
  pack's. [Below](#universal-trackers-map-tab).

## PopTracker

PopTracker is a tracker program of its own that runs *packs*. The pack for this world,
`so2_shattered_stars`, is made by this project and released on its own.

### Setting it up

1. Install [PopTracker](https://github.com/black-sliver/PopTracker/releases) 0.36.0 or newer.
2. Download `so2_shattered_stars_1.0.0.zip` from
   [its release](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0).
3. Drag the zip into PopTracker's window. (Or copy it, unpacked or not, into a `packs` folder
   PopTracker reads: `PopTracker/packs` in your user folder or in Documents, or `packs` beside the
   program.)
4. **Load** -> **Star Ocean: The Second Story (Shattered Stars)** -> **Map Tracker** (maps, boards
   and items) or **Items Only**.
5. Click **AP** and connect: the room's address (`host:port`), your slot name, the password.

**Updates.** When it loads the pack, PopTracker checks this project for a newer version and asks
"A pack update to version ... is available. Download from ... ?". **Yes** downloads and installs
it. **No** asks whether to skip that version.

### What it shows

- **Maps**: Expel and Energy Nede (each world map, then a tab per area group and per area),
  Fienal, and the Cave of Trials (a tab per level). A square for each spot that holds checks:
  several checks at one spot (one person's gifts) share a square, with an entry each. A world map
  has a square per area group.
- **Boards** for the checks that are no place: Shops, Item Creation, Killer Moves, Guilds and
  Learning, Enemy Drops.
- **Only your room's checks**: on connecting, the pack hides the locations your room doesn't
  have.
- **The colours**: **green** in logic, **red** not (items are missing), **blue** in logic by the
  items but later in the story (story mode, [below](#story-awareness-story-mode)). There is no
  yellow ("out of logic"): the room's logic says what is in logic, not what could be done out of
  it. Items you click in the item panel count too, to see what an item would open.
- **Items** on the left: the area keys, hub mode's dungeon keys, the characters (a member outside
  your pool is marked "out"), traps, skills and moves, and **Room**: your room's options.
- **The banner** at the top: "Logic: from the room" (a 0.2.2 room); "Logic unavailable for this
  room: checklist" (an older room: every check shows as reachable); "not connected"; or, for a
  room of a later world than the pack knows, "logic needs a newer pack" (update the pack).
- **Auto tab** (on by default): with the game connected, the map tabs follow the map the party is
  on. Walking about a world map doesn't switch tabs.
- **Hints**: hinted locations in your world are highlighted.
- **Rooms and doors**: each area's tab opens into room tabs too, with a lettered marker on every
  doorway and a live Doors panel beside the map. [Below](#rooms-and-doors-in-the-pack).

## Maps from your own disc

The pack ships only plain stand-in maps, drawn for it: a grid where each room goes, its name, and
a hint to render the real one. **Nothing from the game is in the pack, or in any release of this
project**, so no part of the game's art is ever published. Instead, the game draws the real maps
for you, from your own disc 1, into the folder PopTracker reads for the pack. They are made from
your copy of the game for your own use: don't share them.

What you get, 862 images in all (about 135 MB):
- **an area map** for each of the 66 areas with a chest, gift, boss or recruitment: the area's
  rooms side by side, each with its name above it;
- **every room on its own** (794): each room the doors of those areas lead to, houses, shops and
  dungeon rooms included, with its map number and name above it and a small lettered box (A, B,
  C...) beside each doorway. Where a door leads isn't painted on the picture: the tracker shows
  that, so it stays right if doors are ever shuffled;
- **the two world maps**, Expel and Energy Nede, seen from above, north up.

The pack's squares sit at the same spots, so on the real maps they land on the chests and on the
people who hand things out.

### Rooms and doors in the pack

Each area's tab opens into an **Overview** and then a tab per room (794 of them), titled by the
game's own map number, in the order the story reaches them. A room's doorways carry a small
lettered marker each, A, B, C... (1,775 in all; PopTracker draws it as a yellow diamond, the one
colour no check uses), and a **Doors** panel beside the map says where
each one leads, for example "A -> 412 Salva Drift" (a trailing `*` marks a door that only exists
at some points of the story). Click a panel line, or a doorway's own marker on the map, and the
room it leads to opens its own tab. With the game connected, the tabs follow the room your party
is in, not just the area (the same **Auto tab** switch as above, down to the room level).

### Universal Tracker's map tab

Universal Tracker has a map tab too, from the same room and world-map pages as the pack: install
`tracker.apworld` and the room's `so2.apworld` as usual (above). Its pictures come from a zip, and
on the first connect UT asks for it with a file dialog. Choose one of two:
- **`so2_shattered_stars_ut.zip`: your own maps.** UT can't read PopTracker's override folder, so
  the render (below) also writes this zip, right beside that folder, in
  `PopTracker/user-override` ([Where they go](#where-they-go)). Setup shows its path when it's done,
  with a **Copy path** button to paste into UT's dialog. It holds every picture of the map tab: the
  rendered maps, and the boards and the world-map marker, which the game draws itself. Like the
  maps, it's made from your disc for your own use: don't share it.
- **`so2_shattered_stars_1.0.0.zip`**, the PopTracker pack: the plain stand-in maps.

Cancel and there is no map tab, or `/reload_pack` in UT to ask again. It remembers the zip's path
in `host.yaml` (`so2_options: ut_pack_path`); to switch later, change it there (or empty it, and UT
asks again). The map tab follows the
room the game reports, with a marker for your party on the two world maps, the same as the pack.
It has **no doors**: a door isn't an AP location, entrance or event, so UT has nothing of its own
to track there — for a door's destination, use the pack.

### With the setup program

Setup's **Archipelago** page has a **Trackers** part at the bottom with a **Render tracker maps
for PopTracker** button. It needs a valid Disc 1 (on the **Discs** page; until then the button is
greyed out). While it runs, a progress bar counts the images ("150 of 862 images") and names the
one in progress, and the rest of setup can still be used. At the end it says how many images it
wrote and where, with an **Open folder** button, and where the zip for Universal Tracker is, with
**Copy path**. It takes about 20 seconds on the development machine. Closing setup stops it before the next image: the images made so far stay, each one whole,
and pressing the button again draws them all. The button saves no settings and touches no save.

### From the command line

```
shattered-stars --render-tracker-maps
shattered-stars --render-tracker-maps "C:\some\folder"
```

(`shattered-stars.exe` on Windows, from a terminal in the game's folder.) It uses Disc 1 as the
game finds it (the one setup chose, or the `discs` folder), opens no window, needs no seed, and
writes nothing but the images and their zip for Universal Tracker: no settings, no log, no saves.
It prints a line per image and a count every 100, then the zip's path, and exits with 0 (or 1 if an
image or the zip couldn't be drawn or written). With a folder after it, the images go there instead
and the zip into the folder above it. `--ut-zip FILE` puts the zip at FILE; `--no-ut-zip` leaves it
out.

### Where they go

Into PopTracker's **user-override** folder for the pack, `PopTracker/user-override/so2_shattered_stars`,
in the first of these places:

1. where such an `so2_shattered_stars` override folder already exists: in your user folder
   (`C:\Users\<you>\PopTracker\...`, `~/PopTracker/...`), then in Documents;
2. else your user folder's `PopTracker`, if that folder exists (PopTracker makes it when it
   installs a pack);
3. else `Documents/PopTracker`.

PopTracker looks for overrides in the same places, in the same order (it also looks in the
folder it was started from first, and beside the program last), and uses a file there in place of
the pack's own: `images/maps/` (the area and world maps) and `images/rooms/`. Load the pack
again to see them. Render again after updating the pack, in case its maps changed.

**A portable PopTracker** (with a `portable.txt` beside the program) reads overrides only from its
own folder. Give the game that folder:
`shattered-stars --render-tracker-maps "<PopTracker's folder>/user-override/so2_shattered_stars"`.

**The zip for Universal Tracker**, `so2_shattered_stars_ut.zip` (about 135 MB), goes beside the
images' folder: `PopTracker/user-override/so2_shattered_stars_ut.zip` next to
`PopTracker/user-override/so2_shattered_stars`. PopTracker ignores it there (it isn't a pack folder
or an override). Keep it out of folders whose names end in "zip" (`myzip`): Universal Tracker
can't read pictures from a zip inside one.

## Story awareness (story mode)

Story mode's chapters need no items, so by the logic alone almost every check is in reach from the
start, and a tracker would list chests in towns the party hasn't reached yet. With Shattered Stars
0.03.00 connected to the room, the game reports how far the story has got, and both trackers follow
it, live, and back again if you load an earlier save:

- **Universal Tracker** lists only what the story has reached: a town's checks join the list when
  the story takes you there, Energy Nede's after the disc switch, the Cave of Trials and the
  chapter-60 scenes on Expel after the Final Save Point. To turn it off, set
  `enforce_deferred_entrances: "off"` under `universal_tracker` in Archipelago's `host.yaml`.
- **PopTracker** shows the checks later in the story in **blue**, and those the story has reached
  green. The **Chapter** item shows the chapter the game reports; with the game not connected,
  clicking it sets a chapter by hand (left +1, right -1; 0 = none). The **Story** item (on by
  default) turns story awareness off. It needs a room made with `so2.apworld` 0.2.2.

Before the game has connected, with an older Shattered Stars, and in open world and hub mode,
both show what the logic alone says.

The story is followed by its 24 points: each story stage starts at its first chapter, and the
last point is the Final Save Point.

| Point | Reached at chapter | Point | Reached at chapter |
|---|---|---|---|
| 1 Shingo Forest and Arlia | 3 (Rena's game starts at 4) | 13 Energy Nede | 36 |
| 2 Salva and Salva Drift | 5 | 14 Synard | 38 |
| 3 Cross | 9 | 15 Noel | 39 |
| 4 Cross Cave | 12 | 16 Cave of Red Crystal | 40 |
| 5 Clik and Mars | 14 | 17 Central City | 41 |
| 6 Herlie | 16 | 18 The four fields | 42 |
| 7 Lacour's tournament | 18 | 19 L'Aqua | 46 |
| 8 Linga and its sanctuary | 20 | 20 Fienal's first visit | 48 |
| 9 Hilton and Hoffmann Ruins | 23 | 21 Armlock and Mihne | 52 |
| 10 Lacour Front Line | 26 | 22 Fun City | 57 |
| 11 El | 29 | 23 The last chapters | 58 |
| 12 Eluria Tower | 32 | 24 the Final Save Point | 60, step 4000 |

A stage is the unit, so this is close but not exact: a gift first given in the middle of a stage
shows from the stage's start; Fienal's floors count from chapter 58, though the party enters at
chapter 60; and the Final Save Point counts from the step before the save.

## Older rooms

| Room made with | Universal Tracker (with 0.2.2 installed) | PopTracker |
|---|---|---|
| `so2.apworld` 0.2.2 | exact | the logic, story awareness |
| 0.2.1 | exact (0.2.1's logic is 0.2.2's) | a checklist |
| 0.2.0 | exact; with Skillsanity on, tracked with a warning in UT's log (below) | a checklist |
| 0.2.0, a hell seed (`encounter_rate: 0` with killer-move checks) | refused: track it with `so2.apworld` 0.2.0 and your YAML in UT's Players folder | a checklist |

**0.2.0 rooms with Skillsanity.** 0.2.1 changed the logic of Skillsanity's guild checks and learn
levels, so UT may show some of them **later** than they really became reachable, never earlier
(measured on 45 such rooms: 29 showed fewer at some point, 16 the same, none more). Regenerate with
0.2.2 for exact tracking.

## Troubleshooting

| Problem | What to do |
|---|---|
| UT: "The world has informed UT that your options are incompatible" | Type `/faris_asked`: it names the `so2.apworld` this room needs. Install that one (only one copy in `custom_worlds`) |
| UT: "Did not load ... as its game ... is already loaded" | Two copies of `so2.apworld` are installed. Keep one, in `custom_worlds` |
| UT warns about Skillsanity in its log | The room was made with 0.2.0 with Skillsanity on: see [Older rooms](#older-rooms) |
| UT lists checks in places the story hasn't reached | Story awareness needs Shattered Stars 0.03.00 connected to the same room and slot, and `enforce_deferred_entrances` not `"off"` in `host.yaml`. Open world and hub mode have none |
| PopTracker: "Logic unavailable for this room: checklist" | The room was made before `so2.apworld` 0.2.2. Every check shows as reachable; generate with 0.2.2 for the logic |
| PopTracker: "logic needs a newer pack" | The room is from a later world than the pack knows. Update the pack |
| PopTracker shows the plain maps | Render them (above), then load the pack again. Check that `PopTracker/user-override/so2_shattered_stars/images/maps/` holds PNGs in the folder PopTracker uses; a portable PopTracker needs its own folder given |
| PopTracker's squares don't sit on the chests | The maps were drawn for another version of the pack. Render them again |
| UT's map tab shows the plain maps | It reads the PopTracker pack's zip: set `ut_pack_path` under `so2_options` in `host.yaml` to your `so2_shattered_stars_ut.zip` (setup's **Copy path**), or empty it and reconnect to be asked again |
| UT's map tab has squares but no pictures | A folder on the zip's path ends in "zip" (`myzip`), which UT can't read pictures through: move the zip and set `ut_pack_path` again |
| PopTracker's tabs don't follow you, or the chapter stays | The game must be Shattered Stars 0.03.00 and connected to the same room and slot (not offline); **Auto tab** must be on |
| A check you made doesn't show | It was made offline: it shows once the game reconnects and sends it |
| Setup's render button is greyed out | Choose a valid Disc 1 on the **Discs** page first |
| A tracker can't connect | Check the address, the slot name and the password. A tracker needs the room's password like the game |

## For developers

The keys the game writes, the map layout, the pack and the world's tracker support are documented
in the repository:
[data storage](https://github.com/IronJade/Shattered-Stars/blob/main/docs/apworld/data_storage.md),
[tracker maps](https://github.com/IronJade/Shattered-Stars/blob/main/docs/trackers/tracker_maps.md),
[the PopTracker pack](https://github.com/IronJade/Shattered-Stars/blob/main/docs/trackers/poptracker.md),
and the world's "Trackers" section in
[docs/apworld/apworld.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/apworld/apworld.md).
