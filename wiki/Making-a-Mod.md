# Making a Mod

This guide walks you through making a mod for Shattered Stars, from your first change to a
package that passes the project's check. You don't need to be a programmer: you'll run one
small script from a terminal and edit two short text files.

Finished mods can be shared with everyone through the verified list: see
[Submitting a Mod](Submitting-a-Mod). How players install them is in
[Installing and Managing Mods](Installing-and-Managing-Mods).

## What a mod can change

A mod is **data, never code**. It is a set of short lines of text, each one changing one value
as the game loads it:

| What | Examples |
|---|---|
| Chests | What a chest gives: a different item, or an amount of Fol |
| Doors | Where a door or map exit leads, and where the party arrives |
| Items | An item's price, attack, defence, effect amount, effect, type or sell value |
| Shops | What a shop sells: replace an item, remove one, or add one at the end |
| Enemies | An enemy type's HP, MP, EXP, Fol, speed, drops and drop chances, attack, defence, level and other stats |

Anything else (graphics, text, music, new maps, new behaviour) is outside what a mod can do.

**Your disc images are never modified.** The game makes each change in memory as it loads the
map, the item table, the shop or the battle.

A few limits come from how the game stores things:

- A chest that holds an **item** can only be changed to another item. A chest that holds
  **Fol** can be changed to either.
- Only doors of the simple, fixed kind can be sent elsewhere. The tool tells you which doors
  those are.

## What you need

- **Your own disc images** of *Star Ocean: The Second Story*: the North American, European,
  French or German release. The examples here use Disc 1's `.cue` file. `so2_mod.py` writes
  the same North American word numbers from any of them, and the game translates them for the
  release being played ([Modding](Modding#mods-and-the-releases)). For a mod you'll submit to
  the curated list, use the North American discs: its automatic check compares every line
  with them ([Submitting a Mod](Submitting-a-Mod#the-rules)).
- **Shattered Stars**, to try your mod (see [Installing and Setup](Installing-and-Setup)).
- **Python 3.10 or newer.** The tools are Python scripts and need nothing else installed.
- **A copy of the Shattered Stars source repository.** The tools live in its `tools` folder
  and read the repository's own tables. On the
  [repository's page](https://github.com/IronJade/Shattered-Stars), choose **Code**, then
  **Download ZIP**, and unpack it. (If you use git: `git clone https://github.com/IronJade/Shattered-Stars.git`.)
- **A plain text editor**, such as Notepad on Windows or any code editor.

### Installing Python

- **Windows:** download the installer from [python.org](https://www.python.org/downloads/).
  In the first screen of the installer, tick the box that adds Python to `PATH`, then install.
- **Linux:** Python 3 is usually installed already. If it isn't, install the `python3`
  package with your distribution's package manager.

Then open a terminal (on Windows, **PowerShell** or **Command Prompt**) and check it works:

```sh
python3 --version
```

On Windows, type `python` (or `py`) wherever this guide says `python3`.

### Opening a terminal in the repository

Every command in this guide is run from the repository's folder (the one that holds `tools`,
`mods` and `README.md`). Open a terminal there:

- **Windows:** open the folder in File Explorer, click the address bar, type `powershell` and
  press Enter.
- **Linux:** right-click inside the folder and choose your file manager's "Open in Terminal",
  or `cd` to it.

In the commands below, **replace `DISC.cue` with the path to your Disc 1 `.cue` file**. Put
the path in quotes if it holds spaces, for example
`"C:\Games\Discs\Star Ocean - The Second Story (Disc 1).cue"`.

## Step 1: Find what to change

`tools/so2_mod.py` reads your disc and shows what is there, with names next to the numbers.
Numbers can be typed in decimal or in hexadecimal with `0x` in front (`277` and `0x115` are the
same number); the tool prints most of them in hexadecimal. Wherever a command takes a map, an
item, a shop or an enemy type, you can type its name instead: `"Resurrection Bottle"`,
`"Cross: Skill Guild"`, `"Hearn's"`. Case, punctuation and plurals don't matter; a name that
fits several things lists them so you can pick one by number, and a misspelt one gets
suggestions.

The [Modding Reference](Modding-Reference) lists every item, map, door, chest, shop and enemy
type with its number and name, so you can look things up without the disc. To search the
names from the command line:

```sh
python3 tools/so2_mod.py find DISC.cue skill guild
```

```text
maps:
  80 Town of Cross: Skill Guild (Skillie)
  123 Port of Clik: Skill Guild Clik Skills
  147 Town of Herlie: Skill Guild Giono Vallon
  ...
```

### A map's chests and doors

```sh
python3 tools/so2_mod.py list DISC.cue 18
```

Map 18 is Arlia's square. The output looks like this:

```text
map 18 (Arlia Village: north: fountain square): chests
  flag 0x961  handler word 0x81f  item 0x2a5 Leather Armor
  flag 0x962  handler word 0x827  200 Fol
  flag 0x963  handler word 0x830  item 0x115 Resurrection Bottle
map 18 (Arlia Village: north: fountain square): map transfers (map_transfer_c)
  0: offset 0x748 -> map 17 (Arlia Village: south: bridge and forest path) mode 0 at (110, 0, -457) dir 180
  1: offset 0x780 -> map 26 (Arlia Village: Hearn's General Store (inside)) mode 0 at (553, 0, -420) dir 180
  ...
```

- Each **chest** is known by its **flag** (`0x962`), followed by what it holds now: an item
  (number and name), or an amount of Fol.
- Each **map transfer** is a door or exit, numbered from 0. It shows the map it leads to (number
  and name), where the party arrives (x, y, z) and the direction they face.
- A map whose script places a **save point** ends with a `save point at (x, y, z)` line.

To find a map's number, look it up in [Reference: Maps](Reference-Maps), search with `find`, or
type its name where a command wants a map: `list DISC.cue "Hearn's General Store"`. A name that
fits several maps (`"Arlia Village: north"` fits the square by day and by night, maps 18 and
21) lists them, and you pick one by number. In the game,
the debug console (`--debug`) shows the map you are on in its status line, and its `where`
command prints it with your position. See [Playing](Playing) for the console. Use it on a copy
of your memory card.

### Items, shops and enemies

```sh
python3 tools/so2_mod.py items DISC.cue "Resurrection Bottle"   # one item's fields
python3 tools/so2_mod.py shops DISC.cue                         # every shop's stock
python3 tools/so2_mod.py enemies DISC.cue                       # every enemy type's stats
python3 tools/so2_mod.py enemies DISC.cue Landworm              # one enemy type's stats
```

For example, `items DISC.cue "Resurrection Bottle"` (item `0x115`) prints:

```text
item 0x115 Resurrection Bottle: price=3600 atk=0 def=0 amount=60 effect=6 type=0 sell=25
  usable item; used in: battle, field; effect: revives, restores HP (%)
```

`shops` prints each shop with the map that opens it, then its items in order, the first one
being entry 0:

```text
shop 1 (0x1): Shop 1 (map 26: Arlia Village: Hearn's General Store (inside))
  0: 0x2d8 Sandals
  1: 0xdd Rose Hips
  ...
```

The same tables, for every item, shop and enemy type, are on the reference pages:
[Items](Reference-Items), [Shops](Reference-Shops), [Enemies](Reference-Enemies),
[Chests](Reference-Chests) and [Doors](Reference-Doors).

## Step 2: Write the patch lines

Make a folder for your mod. Name it after your mod's **id**: lower-case letters, digits and
`-`, for example `my-first-mod`. Then let `so2_mod.py` write the lines into a patch file in
it. Adding `-o my-first-mod/changes.patch` to a command **appends** its lines to that file;
without `-o`, the lines are only printed on the screen, so you can look first.

Each command also writes a comment line (starting with `#`) that says what changed, in words.

This step builds a small example, `my-first-mod`: the crate in front of Hearn's store in Arlia
holds 500 Fol instead of 200, and Resurrection Bottles are free. Those commands have `-o`. The
door, shop and enemy examples show the other commands without `-o`; add it to any of them to
put its lines in your mod.

### Change a chest

```sh
python3 tools/so2_mod.py chest DISC.cue 18 0x962 --fol 500 -o my-first-mod/changes.patch
```

This makes the chest with flag `0x962` on map 18 give 500 Fol instead of 200. It writes:

```text
# map 18 (Arlia Village: north: fountain square) chest 0x962: 200 Fol -> 500 Fol
script 18 0x82c 0xffffff38 0xfffffe0c
```

Use `--item ITEM` instead of `--fol N` to put an item in the chest: a number or a name, such as
`--item "Blueberries"`. A chest that holds an item can only take another item; one that holds
Fol takes either ([Reference: Chests](Reference-Chests) says which is which).

Some places exist as several maps (the same town at different points of the story), and a
chest can be on each of them. This crate is also on map 21, so change it there too:

```sh
python3 tools/so2_mod.py chest DISC.cue 21 0x962 --fol 500 -o my-first-mod/changes.patch
```

[Reference: Chests](Reference-Chests) lists those other maps for every chest (*Also on*), and
if you miss one, the check in Step 6 tells you ("chest 0x962 is also on map 21 (word 0x297): patch those too").

### Change a door

```sh
python3 tools/so2_mod.py door DISC.cue 18 5 --to "Hearn's General Store" --x 553 --y 0 --z -420 --dir 180
```

This sends map 18's transfer 5 (which led to map 27, Rena's house) to map 26, Hearn's store,
instead, arriving at the spot and facing the direction that transfer 1 uses to enter map 26.
`--to 26` does the same. `--x`, `--y`, `--z` and
`--dir` are optional; any you leave out keep the door's old value. The easiest way to get good
values is to copy them from another door that already leads to the same map, as here:
[Reference: Doors](Reference-Doors) lists every door with where it leads and its arrival point.

A changed door can strand the party or skip part of the story, so the check always asks you to
explain it (see [Declaring a risky edit](#declaring-a-risky-edit)).

### Change an item

```sh
python3 tools/so2_mod.py item DISC.cue "Resurrection Bottle" price 0 -o my-first-mod/changes.patch
```

```text
# item 0x115 Resurrection Bottle price: 3600 -> 0
item 0x115 0x0 0xe10 0x0
```

This makes the Resurrection Bottle (item `0x115`) free. A free item is a **risky edit**: the
check in Step 6 asks you to confirm you meant it, and shows how.

The tool writes the field as its position in the item's record (`0x0` is the price); the field
name works too. The fields are:

| Field | What it is |
|---|---|
| `price` | The price in Fol |
| `atk`, `def` | Attack and defence |
| `amount` | The amount of the item's effect |
| `effect` | The kind of effect |
| `type` | The item's type |
| `sell` | The sell price, as a percentage of the price |

An item change applies everywhere the game uses the item: shops, menus, battles and the field.

### Change a shop

```sh
python3 tools/so2_mod.py shop DISC.cue "Hearn's" add "Resurrection Bottle"   # add an item at the end
python3 tools/so2_mod.py shop DISC.cue 1 3 0x115                             # put it at entry 3, in place of what's there
python3 tools/so2_mod.py shop DISC.cue 1 3 0                                 # remove entry 3
```

Shop 1 is Hearn's general store in Arlia; a shop takes its number or the name of the map that
opens it. Entries count from 0, in the order `shops` prints
them. Adding at the end is the friendliest change: when several mods add items to the same
shop, every one of them is added.

### Change an enemy

```sh
python3 tools/so2_mod.py enemy DISC.cue 4 hp 400
```

```text
# enemy 4 Landworm hp (as in encounter 19): 200 -> 400
enemy 4 0x0 0xc8 0x190
```

This doubles the HP of every enemy of type 4, wherever it appears. The fields are `hp`, `mp`,
`exp`, `fol`, `speed`, `drop1` and `drop2` (the items it drops), `atk`, `def`, `int`,
`level`, `eva`, `acc`, `guts`, and `drop1_odds` and `drop2_odds` (the chance of each drop).
The type takes a name too (`enemy DISC.cue Landworm hp 400`), and so does a drop's item:
`enemy DISC.cue Armedknight drop1 "Blackberry"`.

### Your patch file so far

After the three commands with `-o`, `my-first-mod/changes.patch` holds:

```text
# map 18 (Arlia Village: north: fountain square) chest 0x962: 200 Fol -> 500 Fol
script 18 0x82c 0xffffff38 0xfffffe0c
# map 21 (Arlia Village: north: fountain square, at night) chest 0x962: 200 Fol -> 500 Fol
script 21 0x297 0xffffff38 0xfffffe0c
# item 0x115 Resurrection Bottle price: 3600 -> 0
item 0x115 0x0 0xe10 0x0
```

If you run a command twice by mistake, open the file in your text editor and delete the
repeated lines.

## Step 3: Understand a patch line

You never have to type a patch line yourself, but it helps to know what one says. Each line has
five parts:

```text
script 18 0x82c 0xffffff38 0xfffffe0c
│      │  │     │          └─ VALUE: the new value
│      │  │     └─ EXPECTED: the value your disc holds there now
│      │  └─ where: which word of the map's script (or which field, or which shop entry)
│      └─ target: the map (or item, shop or enemy type)
└─ command: script, item, shop or enemy
```

| Line | Changes |
|---|---|
| `script MAP WORD EXPECTED VALUE` | One word of map MAP's event script: this is how chests and doors are changed |
| `item ITEM FIELD EXPECTED VALUE` | A field of item ITEM's record |
| `shop SHOP INDEX EXPECTED VALUE` | Entry INDEX of shop SHOP's stock (VALUE 0 removes it; adding at the end uses EXPECTED 0) |
| `enemy TYPE FIELD EXPECTED VALUE` | A field of the stats of every enemy of type TYPE |

Numbers are decimal or `0x` hexadecimal, and `#` starts a comment. (Fol amounts in a chest are
stored as negative numbers, which is why `200 Fol` looks like `0xffffff38`.)

### The EXPECTED guard

**EXPECTED is a safety lock.** The game changes a value only while it still finds EXPECTED
there. If it finds something else, it leaves the value alone and writes a note in its log
saying the line was skipped. That can happen when:

- the line was written for different data (a value that differs in the release being played,
  for example);
- another mod earlier in the load order already changed the same value;
- an Archipelago seed changes the same place (a seed always wins).

So a line can never corrupt data it wasn't written for. This is also why you should let
`so2_mod.py` write your lines: it reads EXPECTED from your own disc, so it is always right.
The technical reference for the format is
[port/src/game/field/mods.h](https://github.com/IronJade/Shattered-Stars/blob/main/port/src/game/field/mods.h).

## Step 4: Package it

A mod package is a folder (or a `.zip` of one) holding:

```text
my-first-mod/
  mod.json          the manifest (required)
  changes.patch     one or more patch files, applied in the order mod.json lists them
  README.md         optional: plain text shown in setup's info panel
```

**Nothing else is allowed:** no images, no programs, no subfolders. A mod describes itself in
words.

### mod.json

Create `mod.json` in your mod's folder with your text editor. Here is a full example with every
field:

```json
{
  "format": 1,
  "id": "my-first-mod",
  "name": "My first mod",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "The crate in front of Hearn's store in Arlia holds 500 Fol instead of 200, and Resurrection Bottles are free.",
  "min_port_version": "0.01.00",
  "tags": ["chests", "items"],
  "homepage": "https://github.com/your-name/my-first-mod",
  "patches": ["changes.patch"],
  "declared_risks": [
    {"rule": "price-zero", "lines": ["changes.patch:6"], "reason": "Free Resurrection Bottles are the point of this mod."}
  ]
}
```

| Field | Required | Rule |
|---|---|---|
| `format` | Yes | `1` |
| `id` | Yes | 1 to 48 characters: lower-case letters, digits, `-`, `_` and `.`, starting with a letter or digit. The folder is named after it |
| `name` | Yes | 1 to 60 characters: the name players see |
| `version` | Yes | One to four numbers with dots (`1`, `1.2`, `1.2.3`) |
| `author` | Yes | 1 to 60 characters |
| `description` | Yes | 1 to 1000 characters: what it changes, in a sentence or two |
| `min_port_version` | Yes | The oldest Shattered Stars version it works with, written like `0.01.00` |
| `tags` | No | Up to 8, each 1 to 24 of `a-z`, `0-9` and `-`. Suggested: `chests`, `doors`, `items`, `shops`, `enemies`, `difficulty`, `economy`, `qol`, `fix` |
| `homepage` | No | An `https://` address |
| `patches` | Yes | Your `.patch` file names (no folders), in the order they apply |
| `declared_risks` | No | Risky edits you made on purpose, each with a reason (see [Step 6](#declaring-a-risky-edit)) |

Mind the JSON punctuation: double quotes around text, a comma between entries and none after
the last one. If setup or the check says `mod.json` isn't valid JSON, a missing or extra comma
is the usual cause.

### README.md

The README is optional but recommended. Players read it in setup's info panel, and the
maintainer reads it when reviewing a submission. Write it as plain text. A good README says:

- what the mod changes, and where in the game;
- how you made it (the `so2_mod.py` commands you ran);
- anything players should know, such as a door that now leads somewhere else.

The two sample mods in the repository's
[mods folder](https://github.com/IronJade/Shattered-Stars/tree/main/mods) are short examples.

### Limits

At most 32 patch files of up to 256 KiB each; `README.md` up to 64 KiB; `mod.json` up to
16 KiB; the whole package up to 1 MiB. Every file is UTF-8 text.

## Step 5: Try it in the game

1. Open the setup program's **Mods** page and press **Open folder** to open your mods folder.
2. Copy your mod's folder into it. (Or zip the folder and use **Install from .zip...**.)
3. A package copied in by hand **starts disabled**. Tick it in the list, then press **Save** (or
   **Save & Play**). A mod installed from a `.zip` starts enabled.
4. Play to the places you changed. Each kind of change applies at its own moment: chest and
   door changes when the map loads, shop changes when you open the shop, item changes when the
   game loads its item table (a battle, a shop or the camp menu), and enemy changes when a
   battle with that enemy starts.
5. Check the game's log. On Windows it is `shattered-stars.log`, beside `config.ini`. On Linux,
   start the game from a terminal and read what it prints. Each of your lines is listed once,
   when it applies:

   ```text
   [mods] load order: my-first-mod 1.0.0
   [port] [mods] my-first-mod/changes.patch:2: map 18 word 2092 0xffffff38 -> 0xfffffe0c
   ```

   A line that ends in `; skipped` didn't match the data. See
   [Installing and Managing Mods](Installing-and-Managing-Mods#troubleshooting).

Try your changes on a copy of your memory card, especially doors and enemy changes.

## Step 6: Check it with mod_check

`port/tools/mod_check.py` checks a package without the game or the discs: its files, its
`mod.json`, every patch line, and the numbers against the repository's tables of the disc. It
also flags **risky edits** for review. Run it in strict mode, as the project's pull-request
check does:

```sh
python3 port/tools/mod_check.py --strict my-first-mod
```

It prints one summary line per package, then one line per finding:

```text
OK my-first-mod (my-first-mod): 3 line(s) (2 script, 1 item); 0 error(s), 0 warning(s), 1 declared
  declared [price-zero] changes.patch:6: Resurrection Bottle's price becomes 0: free in shops, worth nothing sold (declared: Free Resurrection Bottles are the point of this mod.)
```

- **OK**: nothing to fix.
- **WARN**: risky edits that aren't declared. In strict mode these fail the check.
- **FAIL**: errors. The game can't use the package, or a line is certainly wrong.

The check finishes with exit status 0 when it passes and 1 when it fails. You can name several
packages, or a `.zip`.

### Fixing errors

Each finding names its rule in square brackets, and the file and line it is about:

```text
FAIL my-first-mod (my-first-mod): 3 line(s) (2 script, 1 item); 3 error(s), 0 warning(s), 1 declared
  error [expected] changes.patch:4: map 21 word 0x297 is chest 0x962's contents and holds 0xffffff38 on the disc, not 0xffffff37
  error [grammar] changes.patch:7: unknown command 'scrpit' (only script, item, shop and enemy): scrpit 18 0x82c 0xffffff38 0xfffffe0c
  error [files] preview.png: preview.png isn't allowed in a package (only mod.json, the listed .patch files and README.md; no images or programs)
```

Here a line was edited by hand (its EXPECTED no longer matches the disc), a command name was
mistyped, and a picture was left in the folder.

| Rule | What's wrong | How to fix it |
|---|---|---|
| `manifest` | `mod.json` is missing, isn't valid JSON, misses a required field, or a value breaks the rules in [Step 4](#modjson) | Fix `mod.json`; the message names the field |
| `folder-name` | In the repository's `mods` folder, the folder isn't named after the id | Rename the folder, or the id |
| `files` | A file that isn't allowed (an image, a subfolder), a `.patch` file not listed in `patches`, or a listed one that's missing | Remove the file, or fix the `patches` list |
| `size` | Over the limits above | Make the package smaller |
| `text` | A file that isn't UTF-8 text | Save it as UTF-8 text |
| `grammar` | A line the game wouldn't understand | Write the line again with `so2_mod.py` |
| `range` | A number out of range: a map that isn't a field map, an item outside 1-1023, a shop or enemy type that doesn't exist, a value too wide for its field | Check the numbers; write the line again with `so2_mod.py` |
| `expected` | EXPECTED doesn't match the North American disc, so the line would always be skipped | Write the line again with `so2_mod.py` from your North American disc |
| `same-place` | Two lines of your package change the same place, so the second could never apply | Keep only one of them |
| `declared` | A malformed `declared_risks` entry: an unknown rule or an empty reason | Fix the entry |

### Risky edits

A risky edit isn't wrong, but it can hurt a playthrough, so a person should look at it. In
strict mode each one must be **declared** with a reason, or the check fails.

| Rule | Flags |
|---|---|
| `script-unknown` | A script word that isn't a chest's contents or a door's target, so the checker can't tell what it does |
| `door-missing-map` | A door sent to a map that isn't a field map |
| `door-retarget` | Any door sent elsewhere: say where it leads and how the party gets back |
| `key-item` | A change to a chest or gift that gives a story key item |
| `chest-empty` | A chest set to nothing (item 0 or 0 Fol) |
| `chest-other-sites` | A chest that is on several maps, changed on only some of them |
| `item-unknown` | A chest, shop or drop set to an item number with no name |
| `boss-zero` | A boss's HP or EXP set to 0 |
| `enemy-zero-hp` | Any enemy's HP set to 0 |
| `price-zero` | An item's price set to 0 |
| `price-high` | A price over 21,691,754 (buying 99 would overflow) or over the 999,999,999 Fol cap |
| `sell-over-price` | A sell percentage over 100: buying and selling would make unlimited Fol |
| `item-behaviour` | An item's effect or type changed |
| `raw-field` | An item or enemy field given as a position the checker has no name for |
| `drop-odds` | A drop chance over 100 |
| `shop-only-source` | An item removed from the only shop that sold it |

Some warnings are best fixed rather than declared. For `chest-other-sites`, for example, run
the same `chest` command for the other maps the message lists.

### Declaring a risky edit

`my-first-mod` makes Resurrection Bottles free. Before `declared_risks` is added to its
`mod.json`, the check flags that line and, in strict mode, fails:

```text
WARN my-first-mod (my-first-mod): 3 line(s) (2 script, 1 item); 0 error(s), 1 warning(s), 0 declared
  warning [price-zero] changes.patch:6: Resurrection Bottle's price becomes 0: free in shops, worth nothing sold
```

You meant it, so declare it in `mod.json` with the rule's name, the line, and your reason:

```json
"declared_risks": [
  {"rule": "price-zero", "lines": ["changes.patch:6"], "reason": "Free Resurrection Bottles are the point of this mod."}
]
```

Run the check again, and the warning becomes a declared risk, which passes (the `OK` output
at the start of this step).

- `rule` is one of the risky-edit rules in the table above.
- `lines` is optional. Each entry is the file name and its line number, counting every line of
  the file, comments included (`changes.patch:6` is the sixth line). Without `lines`, the
  declaration covers every line of the package that the rule flags. That suits a door: its
  change takes up to four lines, and one declaration of `door-retarget` with no `lines` covers
  them all.
- `reason` is required, at most 300 characters. It is what the reviewer reads, so say why the
  edit is safe or intended ("The door now leads to map 26; map 26's own exit still leads back
  to map 18.").
- A declaration that matches no line is itself a warning, so remove declarations you no
  longer need.

## Next steps

- Look up any item, map, door, chest, shop or enemy type: [Modding Reference](Modding-Reference)
  (it also explains how to build a local, searchable copy with item descriptions and map
  pictures from your own disc).
- Share your mod: [Submitting a Mod](Submitting-a-Mod).
- Install and arrange mods as a player: [Installing and Managing Mods](Installing-and-Managing-Mods).
- Back to the [Modding](Modding) overview.
