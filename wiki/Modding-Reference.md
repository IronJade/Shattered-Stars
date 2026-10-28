# Modding Reference

What the numbers in a mod stand for: every item, map, door, chest, shop and enemy type with its ID, its name and its figures. The tables are read from the disc by `port/tools/mod_reference.py` and hold IDs, names, types, prices and stats only, never the game's text or pictures; for those, build the [local reference](#the-local-reference) from your own disc.

## The tables

| Table | Rows | Wiki | File |
|---|---|---|---|
| Items | 1023 | [Reference: Items](Reference-Items) | [`items.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/items.csv) |
| Maps | 964 | [Reference: Maps](Reference-Maps) | [`maps.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/maps.csv) |
| Doors | 2441 | [Reference: Doors](Reference-Doors) | [`doors.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/doors.csv) |
| Chests | 338 | [Reference: Chests](Reference-Chests) | [`chests.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/chests.csv) |
| Shop entries | 648 | [Reference: Shops](Reference-Shops) | [`shops.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/shops.csv) |
| Enemy types | 187 | [Reference: Enemies](Reference-Enemies) | [`enemies.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/enemies.csv) |

The CSV files open in any spreadsheet; their columns are explained in [`docs/modding/reference/README.md`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/README.md).

## Reading the IDs

- **Numbers** are written in decimal or in hex with `0x` (`677` and `0x2a5` are the same item, Leather Armor). `so2_mod.py` and patch files take either.
- **Items** are numbered 1-1023. Two items can share a name (the game has two Fanzines); then use the number.
- **Maps** are field map numbers. Our names read "area: place", for example `18 Arlia Village: north: fountain square`; a map without a description of its own yet shows the banner the game puts up on entering, or its area. The world maps' chunks (909-980 for Expel, 1048-1110 for Energy Nede) share one script: a patch names one map, so repeat it for each.
- **Doors** are numbered per map from 0, in the order `so2_mod.py list` prints them.
- **Chests** are known by their opened flag, a number the game sets once the chest is opened (`0x962` on map 18 holds 200 Fol). One chest can sit in several versions of a map.
- **Shop entries** are a shop number and an index from 0.
- **Enemy types** are the numbers in the encounter packs; the stats are the same in every battle the type is in.

## Names instead of numbers

Every `so2_mod.py` argument that takes an item, a map, a shop or an enemy type also takes its name, without regard to case, punctuation or plurals:

```
python3 tools/so2_mod.py door DISC.cue 18 1 --to "Cross: Skill Guild"
python3 tools/so2_mod.py chest DISC.cue 18 0x962 --item "Blueberries"
python3 tools/so2_mod.py shop DISC.cue "Hearn's" add "Resurrection Bottle"
python3 tools/so2_mod.py enemy DISC.cue Armedknight hp 400
```

A name that fits more than one entry lists them and stops (pick one, by its number); a misspelt one gets the closest names. To search every name:

```
python3 tools/so2_mod.py find DISC.cue skill guild
```

The listings print names next to the numbers, too: `so2_mod.py list DISC.cue 18` shows `flag 0x962 ... 200 Fol` and `-> map 26 (Arlia Village: ...)`.

## The local reference

`so2_mod.py reference` builds one web page on your computer with every table above, searchable and sortable, plus each item's description read from your disc. With `--game` pointing at the built game it also renders a small picture of every map (the game runs without a window and without sound, a few at a time):

```
python3 tools/so2_mod.py reference DISC.cue --out my-reference
python3 tools/so2_mod.py reference DISC.cue --out my-reference --game port/build/shattered-stars
python3 tools/so2_mod.py reference DISC.cue --out my-reference --game port/build/shattered-stars --thumbs 17-40
```

Open `my-reference/index.html` in a browser. The page is made from your own disc and holds the game's text and pictures: keep it to yourself; don't upload or share it. Rendering every map takes a while; `--thumbs` picks maps and `--jobs` (1-6) how many run at once; pictures already made are kept for the next run.

## Keeping the tables current

`python3 port/tools/mod_reference.py DISC1.cue` rewrites the CSV files (only those that changed); `--check` compares them with the Archipelago tables (313 chests, 1023 items, 64 shops, 187 enemy types, 2441 doors). `python3 port/tools/mod_reference_wiki.py` turns them into these pages.

See also: [Making a Mod](Making-a-Mod) · [Modding](Modding) · [the patch grammar](https://github.com/IronJade/Shattered-Stars/blob/main/port/src/game/field/mods.h)

---

Made by `port/tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`port/tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
