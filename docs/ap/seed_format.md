# The seed file (`.apso2`) and the save sidecar

Phase 1 of [the Archipelago plan](../archipelago_plan.md). The APWorld (`worlds/so2`) writes one
`.apso2` file for each Star Ocean player in a multiworld. Shattered Stars reads it at start-up,
from `--ap-seed FILE` (this run only) or the `ap_seed` key in config.ini, which the setup
program sets. Without a seed, Archipelago is off and the game plays as the original.

The reader is `engine/src/game/ap/ap.cpp` (`load_seed_text`); `engine/tests/test_ap.cpp` covers
it. A seed that does not load stops the game with the reason ("locations[3]: a chest needs its
opened flag"), because playing on would lose checks.

## Format

The file is UTF-8 JSON, one object. Numbers may be JSON numbers or strings in decimal or `0x`
hex (`"flag": "0x962"`). Ids go past 2^31; the reader keeps 64 bits. Unknown keys are ignored,
so later versions can add to the file.

| Key | Type | Meaning |
|---|---|---|
| `format` | `"apso2"` | required |
| `version` | 1 | required; this build reads 1 |
| `seed` | string | the multiworld's seed id (required); the save sidecar keeps it |
| `game` | string | `"Star Ocean: The Second Story"` |
| `slot` | `{name, id}` | this player's slot name and number |
| `options` | object | the player's options, as the APWorld names them (kept, not read yet) |
| `items` | object | this game's items by AP item id: the names and what they give (below) |
| `locations` | array | this player's locations and what each holds (below) |
| `patches` | array of strings | world edits, one line each, in the mods syntax (below) |
| `start` | `{items, fol, flags}` | the starting inventory, Fol and script flags |

### An item

What an Archipelago item is in this game. It appears in `items` (keyed by AP id), in a
location's `item`, and in `start.items`.

| Key | Meaning |
|---|---|
| `name` | shown in the banner ("Received Revival Card from Alice") |
| `kind` | `"item"` (the default) or `"fol"` |
| `game_id` | `item`: the game's item number, 1-1023 |
| `count` | `item`: how many, 1-20 (default 1) |
| `amount` | `fol`: how much Fol |

### A location

| Key | Meaning |
|---|---|
| `id` | the AP location id (unique in the seed) |
| `name` | for the log and the console |
| `check.type` | `chest`, `gift` or `boss` (other types load but nothing checks them yet) |
| `check.map` | the field map |
| `check.flag` | `chest`: its opened flag, a global script flag 0-2943 |
| `check.offset` | `gift`: the call site's byte offset in the map's script code |
| `check.encounter` | `boss`: the encounter id ([battle.md](battle.md)) |
| `item.player` | who gets the item (default: this slot) |
| `item.player_name` | that player's name, for "Sent Hookshot to Alice" |
| `item.classification` | `progression`, `useful`, `filler` or `trap` (kept) |
| `item.*` | this slot's own item: an item as above; another player's: `name` and `ap_id` |

The location ids come from `tools/ap_extract.py`: `0x50320000 + (type << 21) + (map << 10) +
index`, where the type is 1 for a chest, 2 for a gift, and so on, and the index counts that
map's locations of the type in script order. The port never computes an id. It finds a
location by its check fields and reports the id the seed gives.

### Patches

Each line is a world edit in the syntax of `engine/src/game/field/mods.h`. That covers
`script MAP WORD EXPECTED VALUE` and the `item`, `shop` and `enemy` lines. The seed's lines
are applied before the files in the mods folder, and the log names them `seed:N`. As with
any patch, a line whose EXPECTED value is not on the disc is skipped and logged, never
applied.

A chest's contents are its handler's push. `tools/so2_mod.py chest DISC MAP FLAG (--item ID |
--fol N)` prints the line. For example, `script 18 0x82c 0xffffff38 0xffffec78` makes Arlia's
200-Fol crate (flag 0x962) pay 5000 Fol.

## What the game does with it (phase 1, offline)

- **Checks.** A global script flag that turns on, and is a chest's opened flag in the seed,
  records that location as checked (script_vm `flag_set`). Each check is recorded once and is
  logged as `[ap] check ID (name): what`. Another player's item also gets a banner, "Sent X to
  Alice". The console's `ap_check ID` records any location. Gifts and bosses are read from the
  seed but not hooked yet: phases 2 and 3.
- **Received items.** They wait in a queue. In free play (field state 0, no script running,
  the player in control), the game gives one item at a time: an item goes in the inventory
  (`inventory_add`), Fol goes on the party's total. Each item shows the notice banner, placed
  like the map-name notice, and the next waits until the banner has gone. With a full
  inventory the item waits. The console's `ap_give ITEM [FROM]` takes an AP id or name from
  the seed's `items`, or a game item number.
- **The start.** The first free play of a new game gives `start.items` and `start.fol` and
  sets `start.flags`.
- **The console.** With `--debug`, `ap_status` prints the seed, the slot, checks done of
  total, the received index and the items waiting.

## The save sidecar

The Archipelago state of a save sits in a file next to the memory card, so the card stays
byte-compatible with the console's.

    memcard1.mcd, save 3  ->  memcard1.slot03.ap.json

```json
{
 "checked": [1347569665],
 "format": "apso2-save",
 "received_index": 1,
 "seed": "smoke-1",
 "slot_name": "Claude",
 "start_given": true,
 "version": 1
}
```

- `received_index` counts the received items given in this save. On connect, phase 2 asks
  the server for everything past it.
- `checked` lists the location ids in the order they were checked.
- `start_given` records that the starting inventory is in the save.

The game writes the sidecar once the card write is verified (the save dialog's
`save_finish` returns 1). It writes through `.tmp` and a rename, so a crash never leaves half
a file. It reads the sidecar after a load (the load dialog, or `SS_LOAD_SAVE`):

- A save without a sidecar loads as an ordinary game: no checks, and no starting
  inventory on top of it.
- When the sidecar's seed matches, checks made since the save stay checked, because the
  multiworld keeps them.
- Items still waiting are given in the loaded game.
- A new game starts from an empty state.
- A run without a seed neither reads nor writes sidecars.

## Example

The smoke test's seed (`tools/smoke_field_events.sh`, case `ap_seed`):

```json
{
  "format": "apso2", "version": 1, "seed": "smoke-1",
  "game": "Star Ocean: The Second Story",
  "slot": {"name": "Claude", "id": 1},
  "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21}},
  "locations": [
    {"id": 1347569665, "name": "Arlia: crate by the store",
     "check": {"type": "chest", "map": 18, "flag": "0x962"},
     "item": {"player": 1, "kind": "fol", "amount": 5000, "name": "5000 Fol"}}
  ],
  "patches": ["script 18 0x82c 0xffffff38 0xffffec78"]
}
```

## Open for phase 2

- A chest holding another player's item: the generator needs a patch that makes the chest
  give nothing. One way is to point the handler's give at a no-op, which needs reading the
  chest routine. Until then, such chests still give their original contents.
- The server connection (apclientpp): it sends the recorded checks (`LocationChecks`), and
  received items go into the same queue, numbered by the server's item index.
- Gift, boss and goal hooks, and DeathLink ([battle.md](battle.md)).
