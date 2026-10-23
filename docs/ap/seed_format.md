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
| `goal` | `{encounters: [n, ...]}` | a victory in one of these is the goal (default `[539, 540]`: the final fight, two versions of it; 540 is the harder one after Filia's event) |

`options.deathlink` (true / false) turns DeathLink on.

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
| `placeholder` | the value the seed's patch puts where the chest's or gift's item was (below) |

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

### Placeholders (chests and gifts)

A chest or gift whose item is shuffled gets a `placeholder` and a patch line that puts it in
the location's item push:
- 0x400-0x270f for an item routine (the chest routine's item branch, a gift's give routine:
  script `give_item`). No game item has such a number.
- 0xf00000-0xffffff for a Fol routine (the money routine, ext 0xd4 add money). This range fits
  the money routine's argument, a 24-bit push (`push imm24` is read unsigned), and no script
  gives that much Fol. The port also reads 0x7f000000-0x7fffffff, but only a full-word site can
  hold it, so the generator uses the 24-bit range.
- For a chest that held Fol, the patch replaces its negative value with an item placeholder,
  so the chest takes the item branch.
- Every gift location needs a placeholder, even one holding its original item: the port checks
  a gift only through its placeholder. A chest is also checked by its opened flag.

When the game reaches a placeholder (`ap::on_give_item` / `on_give_money`):
- it gives the location's own item, or sends another player's;
- it records the check;
- the message window the script opens in the same vblank says "Got Blueberries." or "Sent
  Hookshot to Alice.", in place of the chest's text, which names the original item. Several
  placeholders reached in one vblank (a gift giving items and Fol, a prize table's run of gives)
  show one page each in that window. With no window in that vblank, a banner says each one.
- A placeholder reached again in the same vblank is ignored, so a loop that gives copies of an
  item (the prize tables on map 688 give 9) gives the location's item once.
- Own items are given every time the game reaches the give in a later vblank. The game's own
  flags keep a chest or gift from repeating, as they did with its original item.

Example: `script 18 0x82c 0xffffff38 0x00000400` with `"placeholder": 1024` (smoke case
`ap_placeholder`).

#### The generator's helper: `apworld/so2/patches.py`

Agreed with the APWorld's author (the enemy agent, 2026-10-23). It is pure Python, standard
library only, with no disc access, so the `.apworld` ships it.

```python
ITEM_PLACEHOLDER_FIRST, ITEM_PLACEHOLDER_LAST = 0x400, 0x270f
FOL_PLACEHOLDER_FIRST, FOL_PLACEHOLDER_LAST = 0xF00000, 0xFFFFFF

placeholder_for(n: int, role: str) -> int          # "item": 0x400 + n, "fol": 0xF00000 + n
placeholder_patches(sites: list[dict], placeholder: int) -> list[str]
placement_patches(locations: list[dict], start: int = 0) -> list[str]
```

- `placement_patches` gives every location dict that has `sites` a `placeholder` (in place),
  keeps one already there, and returns all the patch lines. `generate_output` appends them to
  `patches`.
- A **site** is one patched word: `{"map", "word", "expected", "form", "role"}`. `word` is the
  word index in the map's script code. `expected` is the word the disc holds there (the port
  skips a line whose word differs). `role` is `"item"` or `"fol"`: the routine that reads the
  value. A chest has one site per map version of its opened flag; a gift has one.

| form | instruction | `word` | patched value |
|---|---|---|---|
| `imm24` | push imm24 (op 0x12) | value_at / 4 | `(expected & 0xff000000) \| placeholder` |
| `word` | push word (op 0x13) | value_at / 4 + 1 | `placeholder` |
| `inline`, item | give_item (B 0x60): the item in bits 0-15, an s16 | value_at / 4 | `(expected & 0xffff0000) \| placeholder` |
| `inline`, fol | add money (ext 0xd4, 3 arguments): the amount in the next word | value_at / 4 + 1 | `placeholder` |

The helper raises `ValueError` when `expected` is not the instruction its form names, or the
placeholder is outside its role's range or doesn't fit the form.

- **CLI (tests):** `python3 apworld/so2/patches.py SEED.json [--field docs/ap/field.json]
  [--disc DISC1.cue] [-o OUT]`. It fills a hand-written seed: a chest or gift location given by
  id only gets its sites from field.json, and a site without `expected` reads it from the
  disc. For the 0x962 crate it writes the smoke case's line.

#### Gifts: the cross-check (2026-10-23)

Every reward site in `docs/ap/field.json` was followed from its give to the next message
window (`scratchpad/world/ap/gifts/xcheck.py`, over ap_extract's disassembly):

| Records | Sites | The give | Its message |
|---|---|---|---|
| gifts | 94 | the library give routine (B 0x60) | its own window, in the same vblank (give_item, play_sfx, call, ext 0x64). "Got X." / "Sent X to Y." |
| gifts | 5 | the library money routine (ext 0xd4), Fol in a gift that also gives items (maps 75, 210 ×3, 220) | its own window, in the same vblank |
| Fol gift | 1 | the money routine, map 148 (100 Fol) | the same |
| Fol gift | 1 | an inline add money in the world script (map 1: +999999, a choice menu next to a -999999) | none; not a location (no id) |
| gifts | 1 | an inline give_item on map 75 (0x9d3c, the second Weighty Ring) | none: the window before it (the library give's) named the first ring. The banner says it. |
| gifts (+1 prize) | 33 (+1) | the give routine's flag branch (value ≥ 10000: key items set flags 66-91, with the fanfare) | not shuffled. A placeholder there would take the item branch and never set the key flag, so these stay vanilla until the key-item design (phase 4). |
| prize tables | 99 | the library give routine | its own window, same vblank |
| prize tables | 2 | the money routine (maps 295, 305) | same vblank |
| prize tables | 215 | inline give_item, in runs (map 305 gives 5 in a row, 469 four) or loops (688: 9 copies) | mostly before the gives (469, 688), or after a wait (305: ext 0x7b). Each placeholder records its check and gives or sends once. A run in one vblank shares one window or one banner per item, and the earlier window names the original prize. |

So every gift's give reaches the hook: B 0x60 and ext 0xd4, stack and inline forms. It records
the check and honours its placeholder whenever the message comes. A message the same vblank
doesn't open is a banner.

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
 "goal": false,
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
- `goal` records that the goal battle was won; it is sent again on every connect.

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

## Phase 2 (port side)

- **Server:** main.cpp builds an `ap_net::Client` (setup agent, `engine/src/ap_net/`) when
  ap_server is set, and hands it to `game::ap::connect`. game/ap polls it once per vblank
  (`Runtime::net_poll`).
  - **Connecting:** on kConnected with a matching seed it sends every recorded check, and the
    goal if the save has it. A different seed keeps the game offline.
  - **Received items:** kept in the server's index order. They are given in free play from the
    save's `received_index` on, and duplicates after a reconnect are dropped. The server sends
    only other worlds' items (`items_handling` 0b001).
- **Bosses and the goal:** `battle_exit_to_field` calls `ap::on_battle_exit(encounter,
  outcome)`.
  - A victory checks the seed's `boss` locations for that encounter, and their own items are
    given in free play.
  - A goal encounter sets `goal` in the sidecar and sends CLIENT_GOAL.
- **DeathLink out:** `field_begin_game_over` sends one, unless a DeathLink caused the death.
- **DeathLink in:** a received one (or the console's `ap_deathlink`) applies at the next safe
  point.
  - In a fight, every standing member falls, the battle is lost and the usual game over follows.
  - In field free play, it is a game over at once.

### The end-to-end check (`tools/ap_e2e.sh`)

`SS_DISC1=DISC1.cue tools/ap_e2e.sh [BINARY]` runs one scripted multiworld session on
localhost, using the install and the two-slot multiworld of `tools/ap_test_server.sh`. It starts
its own MultiServer on 127.0.0.1:38291 (`AP_E2E_PORT`) and stops it by PID afterwards. Until
there is a Star Ocean world to host, the multiworld's ChecksFinder slots stand in: the port logs
in as Alice (`SS_AP_GAME=ChecksFinder`) with a hand-written seed for that slot.

1. `apworld/so2/patches.py` fills the seed. Arlia's 200-Fol crate (map 18, flag 0x962) is
   Alice's Tile 1 and holds Bob's Map Width, so it gets placeholder 0x400 and the line
   `script 18 0x82c 0xffffff38 0x00000400`.
2. The port runs headless and paced (`SS_PACE=1`), with a scratch config and no memory card.
   It connects (`SS_AP_SERVER`, `SS_AP_SLOT`), starts on map 18 and opens the crate. The check
   goes out, the window says "Sent Map Width to Bob.", and no Fol is paid.
3. Bob, a Python client, receives Map Width from location 81000, then checks his Tiles 1, 4
   and 10, which hold Alice's items. The port gives Blueberries, Blackberries and 100 Fol in
   free play with the banner ("Received Blueberries from Bob").

All 7 steps passed on 2026-10-23. Captures are written to `scratchpad/ap_e2e/cap/`: frame
700 shows the window, frame 850 the first banner.

Test switches: `SS_AP_SERVER=ws://HOST:PORT` (with `SS_AP_SLOT`, `SS_AP_GAME`) connects any run,
headless ones included, without config.ini. `SS_PACE=1` paces a headless run at the field rate.
