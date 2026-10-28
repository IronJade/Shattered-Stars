# Modding

Shattered Stars can change parts of the game as it loads, without touching your disc images.
Mods come as small packages that you install, turn on and off, and put in order on the setup
program's **Mods** page. The project keeps a curated list of mods that the maintainer has
verified with the real game.

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-mods.png" alt="The setup program's Mods page: the installed mods in load order with tick boxes, conflict and error marks, and the Install from .zip, Move up, Move down and Remove buttons" width="640">
</p>

## Guides

| Guide | For | Covers |
|---|---|---|
| [Installing and Managing Mods](Installing-and-Managing-Mods) | Players | Setup's Mods page, installing from .zip, enabling, load order, conflicts, removing, Archipelago, troubleshooting |
| [Making a Mod](Making-a-Mod) | Creators | Writing patches from your disc with `so2_mod.py`, the patch format, packaging, testing, and the `mod_check.py` check |
| [Modding Reference](Modding-Reference) | Creators | Every item, map, door, chest, shop and enemy type with its number and name; names in `so2_mod.py`; the local reference with descriptions and map pictures |
| [Submitting a Mod](Submitting-a-Mod) | Creators | The curated list, what "verified" means, the pull request, the automatic check, the maintainer's verification, updates |

## What a mod can change

A mod is **data, never code**: lines of text in a small patch format. They can change:

| What | Examples |
|---|---|
| Chests | What a chest gives: a different item, or an amount of Fol |
| Doors | Where a door or map exit leads |
| Items | An item's price, attack, defence, effect, type or sell value |
| Shops | What a shop sells: replace, remove or add an item |
| Enemies | An enemy type's HP, MP, EXP, Fol, speed, drops and drop chances, attack, defence and other stats |

**Your discs are never modified.** Each change is made in memory as the data loads.

**Each change is checked first.** Every line carries the value the disc is expected to hold, as
well as the new value. The change is made only if the original value is still there. A line
written for different data is skipped (and noted in the game's log) instead of corrupting
anything.

## Mods and the releases

A mod applies to every supported release: North American, European, French and German. Its
lines name places in the map scripts as they are on the North American disc, and the game
translates them for the disc you play, so one mod file serves every release. The guard does
the rest: a line applies only where that release holds the expected value.

- Items, shops, enemy stats and chests hold the same values in all four releases, so those
  lines apply everywhere.
- Doors mostly match too. The French and German map scripts differ in more places than the
  European one (their text windows are sized for the translation), so a line that changes
  other script words is more often skipped there. The game's log says which lines it skipped.

`so2_mod.py` reads the disc of any supported release and always writes North American word
numbers. Both sample mods pass the maintainer's verification on the European disc.

## Packages

A mod package is a folder, or a `.zip` of one, holding:

```text
my-first-mod/
  mod.json          the manifest: id, name, version, author, description, the oldest
                    port version it works with, and its patch files
  changes.patch     one or more patch files
  README.md         optional: plain text shown in setup
```

Nothing else: no images, programs or subfolders. Packages live in the **mods** folder, `mods`
beside `config.ini` (`%APPDATA%\ShatteredStars\mods\` on Windows,
`~/.config/shattered-stars/mods/` on Linux).

In short, on the Mods page:

- **Install from .zip...** installs a package, enabled at the end of the list.
- A package **copied into the folder by hand starts disabled** until you tick it.
- Enabled mods apply from top to bottom. When two change the same thing, **the later one wins**,
  and setup warns you about the conflict.
- Single `.patch` files placed straight in the folder still work, as **Loose patches**.

## The curated list

The [mods folder](https://github.com/IronJade/Shattered-Stars/tree/main/mods) of the
repository holds the curated list. Only the mods marked verified in its `index.json` are on
it. Every submission goes through an automatic check (`port/tools/mod_check.py --strict`, no discs
needed), and then the maintainer runs it in the real game with `port/tools/mod_verify.py`: every
patch line must apply, and every map and battle the mod touches must load and run as it does
without the mod. Only the maintainer marks a mod verified. Today the list holds two small
sample mods.

## Quick reference

### The patch format

One change per line. Numbers can be decimal or `0x` hexadecimal, and `#` starts a comment.

| Line | Changes |
|---|---|
| `script MAP WORD EXPECTED VALUE` | One word of map MAP's event script (this is how chests and doors are changed) |
| `item ITEM FIELD EXPECTED VALUE` | Item ITEM's record, everywhere the game uses it: menus, shops, battles and the field |
| `shop SHOP INDEX EXPECTED VALUE` | Entry INDEX of shop SHOP's stock |
| `enemy TYPE FIELD EXPECTED VALUE` | The stats of every enemy of type TYPE, when a battle loads |

Item fields: `price`, `atk`, `def`, `amount`, `effect`, `type`, `sell`.
Enemy fields: `hp`, `mp`, `exp`, `fol`, `speed`, `drop1`, `drop2`, `atk`, `def`, `int`,
`level`, `eva`, `acc`, `guts`, `drop1_odds`, `drop2_odds`.
Either can also be a byte offset into the record, written `OFFSET[:SIZE]`.

The technical reference for the format is
[port/src/game/field/mods.h](https://github.com/IronJade/Shattered-Stars/blob/main/port/src/game/field/mods.h).

### The tools

All in the repository's `tools` folder; Python 3.10 or newer. `DISC.cue` is your Disc 1 (or
Disc 2) of any supported release. Details in
[Making a Mod](Making-a-Mod) and [Submitting a Mod](Submitting-a-Mod).

```sh
tools/so2_mod.py list    DISC.cue MAP                # a map's chests and doors
tools/so2_mod.py chest   DISC.cue MAP FLAG --item ID # make a chest give item ID (or --fol N)
tools/so2_mod.py door    DISC.cue MAP INDEX --to MAP2 [--x X] [--y Y] [--z Z] [--dir D]
tools/so2_mod.py items   DISC.cue ITEM               # show an item's fields
tools/so2_mod.py item    DISC.cue ITEM FIELD VALUE   # e.g. item 0x15 price 4321
tools/so2_mod.py shops   DISC.cue                    # every shop's stock
tools/so2_mod.py shop    DISC.cue SHOP INDEX ITEM    # INDEX "add" appends, ITEM 0 removes
tools/so2_mod.py enemies DISC.cue [TYPE]             # enemy stats
tools/so2_mod.py enemy   DISC.cue TYPE FIELD VALUE   # e.g. enemy 4 hp 9999
                                                     # add -o FILE to append the lines to a file

port/tools/mod_check.py --strict PACKAGE                  # the static check (no discs)
port/tools/mod_index.py build                             # regenerate mods/index.json
port/tools/mod_verify.py PACKAGE                          # the maintainer's check with the game (needs SS_DISC1)
```

The full design, including the conflict rules and every check, is in
[docs/port/notes/mods.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/port/notes/mods.md).

## Archipelago and mods

An [Archipelago](Archipelago) seed uses the same mechanism. Its changes always apply first, and
no mod can override them: a mod's change to a place the seed changes is skipped. See
[Installing and Managing Mods](Installing-and-Managing-Mods#archipelago-and-mods).
