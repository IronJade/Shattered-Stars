# Modder's reference tables

What the numbers in a world-edit patch stand for. Made by `tools/mod_reference.py` from the
disc and the repository's own tables: IDs, names, types, prices, stats and our own map names
only, never the game's text or pictures. Every table is sorted the same way each time it is
made.

The map names come from `map_names.csv` (our own descriptive names, "area: place") where it
names a map, else from the area banners the game shows on entering, else "Map N".

| File | Rows | What it lists |
|---|---|---|
| `items.csv` | 1023 | every item: name, category, price, attack / defence, effect, sell percentage, who can equip it |
| `maps.csv` | 964 | every field map: our name, area, kind, door / chest counts, shops, save point |
| `doors.csv` | 2441 | every door `so2_mod.py door` can change: where it leads, arrival point, the command |
| `chests.csv` | 338 | every chest: its flag (ID), what it holds, the command |
| `shops.csv` | 648 | every shop's stock, entry by entry, and where the shop is |
| `enemies.csv` | 187 | every enemy type: stats, drops, and the encounters, maps and areas it appears in |

## How the IDs plug in

Each table's IDs are what `tools/so2_mod.py` takes and what a patch line holds (the grammar
is in `engine/src/game/field/mods.h`). `so2_mod.py` also takes a name wherever it takes an
item, map, shop or enemy type: `--to "Cross: Skill Guild"`, `--item "Blueberry"`. A name that
fits more than one entry lists them and stops; `so2_mod.py find DISC.cue TEXT` searches every
name.

- **items.csv**: `id` / `hex` is the item number (1-1023) for `so2_mod.py item`, `chest
  --item`, `shop` and the enemy fields `drop1` / `drop2`; a patch line `item ID FIELD EXPECTED
  VALUE`. `category` is in words; `type` and `effect_kind` are the record's numbers (the
  values the `type` and `effect` fields take). `use`: where it can be used (battle, field).
  `price` is what a shop asks; `sell_pct` is the percentage of it a shop pays. `amount` is
  the effect's amount (for the restoring kinds, the percentage). `equip`: who can wear or
  wield it.
- **maps.csv**: `map` is the field map number for `so2_mod.py list`, `chest`, `door`, the
  door target `--to`, and a patch line `script MAP WORD EXPECTED VALUE`. `doors` counts the
  doors `so2_mod.py door` can change; `chests` the chests; `shops` the shop numbers whose
  menu the map opens; `save_point` whether the map's own script places one. Maps listed in
  `same_script` run one script: a patch names one map, so repeat it for each map it should
  change. `discs`: the disc the story visits the map on (from the world it belongs to; maps
  open again in Virtual Expel list both; every map's data is on both discs, and the scripts
  are identical). `name_from` says where the name came from.
- **doors.csv**: `map` + `index` is a door for `so2_mod.py door DISC.cue MAP INDEX --to
  NEW_MAP` (`--x --y --z --dir` move the arrival point; the current ones are in the row).
  `target` is the map it leads to now. Only the doors written as one inline instruction
  (map_transfer_c) can be retargeted; the other transfers are not listed.
- **chests.csv**: `flag` is the chest's ID (its opened flag) for `so2_mod.py chest DISC.cue
  MAP FLAG --item ITEM` or `--fol N`. `can_hold`: a chest holding an item can only be
  changed to another item; one holding Fol takes either. `also_on` lists other maps with the
  same chest (one chest seen in several versions of a map): change each.
- **shops.csv**: `shop` + `index` is a stock entry for `so2_mod.py shop DISC.cue SHOP INDEX
  ITEM` (`add` for INDEX appends, ITEM 0 removes); a patch line `shop SHOP INDEX EXPECTED
  VALUE`. `where` names the maps that open the shop.
- **enemies.csv**: `type` is the enemy type for `so2_mod.py enemy DISC.cue TYPE FIELD VALUE`
  and the patch line `enemy TYPE FIELD EXPECTED VALUE`; the stat columns are the field
  names it takes (hp, mp, exp, fol, level, atk, def, int, speed, eva, acc, drop1, drop2,
  drop1_odds, drop2_odds). `encounters` are the encounter numbers it is in; `maps` and
  `areas` where those encounters happen (random encounters, scripted battles and the world
  maps); `stats_from` the encounter its stats were read from (every encounter carries the
  same record).

## The local reference

`python3 tools/so2_mod.py reference DISC.cue --out DIR` builds one HTML page with every table
above, searchable, plus the item descriptions read from your disc; with `--game PATH` (the
built game) it adds a thumbnail of every map, rendered without a window. That page is made
from your own disc, on your machine, and is not for sharing.

Regenerate these tables with `python3 tools/mod_reference.py DISC1.cue` (`--check` compares
them with the Archipelago tables in `docs/ap`).
