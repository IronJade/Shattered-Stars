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
| `options` | object | the player's options, as the APWorld names them; the port reads `deathlink` and the phase-3 options ("Phase 3" below) |
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
| `kind` | `"item"` (the default), `"fol"`, `"character"` or `"trap"` ("Phase 3" below) |
| `game_id` | `item`: the game's item number, 1-1023 |
| `count` | `item`: how many, 1-20 (default 1) |
| `amount` | `fol`: how much Fol |

### A location

| Key | Meaning |
|---|---|
| `id` | the AP location id (unique in the seed) |
| `name` | for the log and the console |
| `check.type` | `chest`, `gift`, `boss`, `join` or `drop` ("Phase 3" below; other types load but nothing checks them) |
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
map's locations of the type in script order. Two types carry no map: 8 is an enemy type's
first drop (`0x50320000 + (8 << 21) + enemy type`) and 9 a boss (`0x50320000 + (9 << 21) +
encounter`). The port never computes an id. It finds a location by its check fields and
reports the id the seed gives.

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

## Phase 3 (port side, 2026-10-23)

Agreed with the APWorld agent. Every option defaults to the original game: without a seed, or
with an option at its default, the game plays exactly as on the console (a no-seed and an
options-off run of the victory reference give the same RNG and handoffs as PCSX-Redux).

### Options

| Key | Values | Default | What it does |
|---|---|---|---|
| `protagonist` | `any`, `claude`, `rena` (or 0-2) | `any` | New Game starts on that protagonist and keeps the cursor there (the other side buzzes; log `this seed is for Claude's game`). |
| `character_items` | true / false | false | Listed join sites are checks; members join through their items. |
| `drops_as_checks` | `off`, `first_drop` (or 0 / 1) | `off` | The first drop of each enemy type with a `drop` location is a check. |
| `exp_multiplier` | 1-10 | 1 | The battle's EXP total, at the result screen. |
| `fol_multiplier` | 1-10 | 1 | The battle's Fol total, at the result screen. |
| `encounter_rate` | 0-200 (percent) | 100 | Random-encounter frequency on field and world maps. 0 = none. |
| `deathlink` | true / false | false | As in phase 2, with the safe points below. |

A value out of range stops the seed from loading ("options.exp_multiplier: 1-10 expected").
`traps` (the share of filler that becomes traps) is the APWorld's; the port reads only the trap
items.

### New item kinds

| Kind | Fields | What the game does |
|---|---|---|
| `character` | `character`: the party id 1-12 (1 Claude, 2 Rena, 3 Celine, 4 Bowman, 5 Dias, 6 Precis, 7 Ashton, 8 Leon, 9 Opera, 10 Ernest, 11 Noel, 12 Chisato) or the name | The member joins at the next safe point (below). field.json's `joins` values are these minus 1. |
| `trap` | `trap`: `ambush`, `fol_loss` (+ `percent` 1-100, default 10) or `status` (+ `status`: `poison`, `paralysis`, `stone`; default `poison`) | Below. |

Both arrive like any item: in the server's index order, one per banner, "Received Celine from
Bob", "Trap from Bob: lost 250 Fol". Either can also sit at one of this player's own
locations (a placeholder): it is given then, the join waiting for free play.

### New check types

| `check.type` | Fields | Where the port checks it |
|---|---|---|
| `join` | `map`, `offset` (the site's `at`: field.json `recruitments[].sites[]` with `what == "join"`), `character` (optional) | script ext 0xae party_add. The site is the instruction itself (an inline party_add) or the call into the library's join routine: the innermost call frame's return offset - 4. |
| `drop` | `enemy`: the enemy type (battles.json `enemy_types` key = the stats record's +0 id) | the result screen's roll_drops (#1981 0x800869fc). Id: `0x50320000 + (8 << 21) + enemy`. |

### Character items (`character_items`)

- **A listed join site** records its check (its item: given in free play, or sent). The
  member joins there only if their item was received already and there is room.
- **Every other join site** plays as the original: members a scene takes along for a while
  (Leon on map 437, Dias at the tournament on 477) and the 32 sites that put the party back
  together after a split. So the APWorld lists every site where a shuffled character joins for
  good (Celine 124 / 125, Dias 130 / 439 / 229 0x7c9c, Leon 230 0x6c58 ...), each with its own
  id.
- **A received character** joins at the next free play through the game's own join (the
  library routine's two ops: ext 0xae party_add_member, A 0x3f party_member_clear_bond):
  - not during a Private Action (script flags 2 / 4: the party is set aside);
  - with a free party slot. The 8 slots count members in reserve (a member a scene took out
    keeps their slot), and a member in reserve takes their own slot back. With no room the
    character waits (`characters_waiting` in the sidecar) and joins once a slot frees.
  - Leon only in Claude's game (script flag 1 set), Dias only in Rena's: in the other game the
    item is dropped with a banner ("Leon cannot join Rena's party").
  - Bowman / Precis and Ashton / Opera + Ernest: the port lets both join. The APWorld keeps
    the pool to one of each pair.
- Banner: "Received Celine from Bob", then "Celine joined the party".
- A 0x3f after a held-back join finds no slot and does nothing (the original would select the
  bytes before the party block; its own join never fails).

### First drops (`drops_as_checks: first_drop`)

- In roll_drops the RNG draw and the one-drop-per-enemy rule are unchanged.
- When an enemy type drops anything and the seed has its `drop` location unchecked, the item is
  not given and not listed on the result screen. The check is recorded, the enemy type joins
  the sidecar's `first_drops`, and the location's item comes in free play ("Got X") or is sent
  ("Sent X to Y").
- Later drops of that type are the game's.

### Traps

- **`ambush`:** after its banner, at the next free play on a map with random encounters
  (field_flags +0x25 non-zero; not riding, not inside a map transfer's preload trigger), the
  field's forced battle (state 0x21, as a camp-menu item starts one) on the area's encounter
  group (+0x14) and stage (+0x12). State 0x21 takes encounter -(game_state +0x264); minus the
  group makes field_prepare_encounter pick a random member of the group, as a random encounter
  does. Waiting ambushes are kept in the sidecar (`ambushes`). Losing it is a game over.
- **`fol_loss`:** the party loses `percent` % of its Fol (rounded down).
- **`status`:** field statuses live in party slot +2 (1 down, 2 paralysis, 4 stone, 8 poison;
  item effects 0x42 / 0x62 / 0x41 cure them). Poison takes every active member who is up.
  Paralysis or stone takes one (the last active member up), and never the last one standing:
  then it is poison instead.

### Multipliers and the encounter rate

- **EXP / Fol:** award_exp_money (#1981 0x80086290) scales the summed totals before the
  original's doubling bonuses, sharing and member bonuses (log `battle rewards: exp 99000 ->
  297000`). Capped at 999999999. More EXP means more level-ups, so a run with a multiplier
  draws more RNG at the result screen.
- **Encounter rate:** field_update_encounters (field_program.cpp) scales each new distance to
  the next encounter by 100 / rate after the original's randomisation. 0 % turns random
  encounters off (the area's group still feeds ambushes). Walking on world map 2 for 6000
  frames: 100 % = no seed (same log), 200 % 10 encounters against 5, 50 % 3.

### DeathLink: the safe points

| Where the game is | Received DeathLink | Sent |
|---|---|---|
| Free play (field or world map) | the party falls at once (field_begin_game_over) | on any game over |
| A scene (an event script holding the controls) | waits. It applies in free play, or once a scene has left the controls to the player for 30 frames (a scene's walk-around: a lost scripted battle's game over starts inside a routine too) | |
| A menu (camp, shop, save point) | waits for the menu to close | |
| A ride (vehicle, bunny; field states 9-0x20) | waits for the player to get off | |
| A battle transition (field states 2 / 4 / 5 / 0x21, the battle's intro) | waits for the fight: every standing member falls, the battle is lost, the game over follows | |
| A fight | between special moves, the party falls | outcome 2 with a game over |
| A story battle the script lets the party lose (ext 0xb7's third argument 0, kept in field_flags +0x7e; random and menu battles set it to 1) | waits for the field after it | never: the loss is no game over, the scene goes on |
| The game over itself | a waiting one is dropped (the party fell anyway) | |
| The title, a load or a new game | dropped | |
| The ending (after the final boss 539 / 540, or script flag 0x2f1, the endings flow) and the credits (#3107) | dropped | |

The program switches come from boot's run_program (`ap::on_program`). Checked in the game
(scratchpad/world/ap3): the world map (falls at once), the camp menu (received at vblank 400
inside it, falls at 720 once it closed), an ambush's transition (falls in the battle at fight
frame 2), the ship's Shin fight on map 548 (encounter 517, lost as scripted with no game over;
a DeathLink received in it falls 30 frames after the field gets the controls back, vblank
2624), the endings and the credits from map 581 (both dropped; the endings play to the end).

### Sidecar additions

```json
{"characters": [7], "characters_waiting": [], "first_drops": [197], "ambushes": 0}
```

`characters` lists the character items received (the order they came), `characters_waiting`
those not joined yet, `first_drops` the enemy types whose first drop was a check, `ambushes`
the ambushes still to fight. An older sidecar without them reads as empty.

### Console (with `--debug`)

`ap_give` also takes a character's name (`ap_give Celine Bob`) and `trap:ambush`,
`trap:fol_loss[:PERCENT]`, `trap:status[:poison|paralysis|stone]`. `ap_status` adds a line
with the phase-3 options and one with the received characters.
