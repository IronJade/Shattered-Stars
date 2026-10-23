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
| `items` | object | this game's items by AP item id: the names and what they give (below). It lists every item of this player's in the multiworld, wherever it was placed: the port gives a received item from this table, and one missing from it is given as nothing (logged "not in the seed's table") |
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
- **A received character stays recruited** (2026-10-23; docs/catalog/formats.md "Flag 112" and
  "The map library's leave routine"). The story still takes members out for a while (splits,
  Mars, the tournament, the ship) and brings them back with its own scenes; it never drops a
  received one for good:
  - **Celine and flag 112.** Her real recruitment is Cross Cave's choice (map 524, chapter 13);
    map 131 then stores "Celine travels with the party" (flag 112) from party_has, and Mars
    reads it: Claude's game, map 462's scene (step 2400) sends her home for a while with it,
    for good without it; Rena's game, map 126 (step 1000) takes her out; map 125 (step 3300,
    both games) adds her back only with it. A received Celine sets flag 112 (on receipt, and in
    free play for an older save) and the scripts cannot clear it (the VM's flag store asks
    `ap::keeps_flag`). So she goes home in Mars and map 125 brings her back, as for a Celine
    the player kept; its join site records its check and adds her through the game's join.
  - **Partings for good** (`kPartings` in ap.cpp, each a call of the map's leave routine):
    Celine at Cross Cave, choice 1 (524 0x72bc); Mars with flag 112 clear (462 0x7030); Leon at
    the Eluria colony, choice 0 "Waiting here is better" (234 0x6eb0); Opera at the Hoffmann
    Ruins, choice 1 "This is where we part" (479 0x8250). The scene plays (the member leaves,
    the accessories come off as in the original); a received member then joins again at the
    next free play ("Celine joined the party"). Log: `Celine leaves at map 524 (...): received,
    so they rejoin at the next free play`.
  - **Every other leave is temporary.** A received member's join still waiting at that moment
    (the item's banner had not gone) is dropped (`... leaves at map 126 for a while`): the
    story's own join brings them back (Celine 125; Leon 422 -> 230; Dias 462 -> 434 / 439; the
    restore routines after 193, 444, 468-474, 548). Members received while already in reserve
    join at the next free play as before.
  - Checked headless (scratchpad/world/celine/runs): Claude's game 462 T7 (flag 112 clear at the
    start: the scene keeps her gear; 125 at step 3300 rejoins her; the pre-fix build left her in
    reserve), Rena's game 126 -> 125, map 131's store kept, the three choice partings (choices
    forced by seed patches where the sweep's confirm picks the other answer). Smoke: `ap_cel_*`.

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

## Phase 4 (port side, 2026-10-23): the open world, shops, the door table

Agreed with the APWorld agent (a1e6498ce791a7ac4). As before, everything is opt-in: without a
seed, or with these options at their defaults, the game plays as the original.

### Confirmed by the APWorld (2026-10-23)

The fields below are confirmed as written; the APWorld will emit them when phase 4's logic is
built. No port change is needed.
- **Doors** are not locations. Door ids are `map * 65536 + offset / 4`, as doors.json gives them.
- **Keys** are `{"kind": "key", "key": N}`, N 1-255, AP id `ITEM_BASE + 0x40000 + N`. Our AP item
  ids are then: game items `ITEM_BASE + n` (1-1023), Fol `+ 0x10000 + amount`, characters
  `+ 0x20000 + party id`, traps `+ 0x30000 + n`, keys `+ 0x40000 + N`; no range overlaps.
- The seed's top-level **`doors`**: `[{id, key, to}]` (`routine` and `name` optional).
- **Which doors get keys:** only the world-map entrances (doors.json `world` 1 / 2, 66 doors),
  one key per `key_group` (35 groups) as the default list; the APWorld names the keys. Field
  doors and the world script's warps never get one, so the "a locked door reached another way
  skips its transfer" rule should not meet a scene.
- **Shop slots:** check type `shop` `{shop, index, item}`, location type 10, id `0x50320000 +
  (10 << 21) + (shop << 8) + index`. Type 5 stays field.json's shop *site* record (one per
  B 0x6d call, reference only). The APWorld always writes `item`, which shop 0x58 (its list
  grows with flags) needs to find the right entry.
- **The Expel teleporter** and `to` (entrance shuffle groundwork): as written.

### Options

| Key | Values | Default | What it does |
|---|---|---|---|
| `world_mode` | `story`, `open_world` (or 0 / 1) | `story` | `open_world`: the seed's doors with a `key` stay shut until that key is received. |
| `expel_teleporter` | true / false | true in `open_world`, else false | Virtual Expel's two ways open from chapter 36 (below). |
| `shuffle_shops` | `off`, `specials`, `full` (or 0-2) | `off` | Read for the status line; which shop slots are locations is the seed's `shop` list. |

### Area keys

- **The item.** `{"kind": "key", "key": N, "name": "Salva Pass"}`, N 1-255 (the APWorld's
  numbering; AP id ITEM_BASE + 0x40000 + N). It arrives like any item ("Received Salva Pass
  from Bob"), can be a starting item or sit at one of this player's locations. The sidecar
  keeps the keys received (`"keys": [3]`); console `ap_give key:N`.
- **A door.** One map-transfer instruction (script ext 0x7d map_transfer, 0xa4, 0xda) with its
  id, from `tools/ap_doors.py` (`docs/ap/doors.json`, below). The seed's top-level `doors`:

```json
"doors": [
  {"id": 59576094, "routine": "0x3c6c", "key": 3, "name": "Expel -> Salva"},
  {"map": 17, "offset": "0x2054", "to": {"map": 23, "x": 224, "y": 0, "z": 106, "dir": 198}}
]
```

| Key | Meaning |
|---|---|
| `id` | `map * 65536 + offset / 4` (either the id, or `map` and `offset`, or both, which must agree) |
| `map` | the script's map: the lowest map of a shared script (909 for every Expel world-map chunk 909-980, 1048 for Nede's 1048-1110, 1 for the world script, 340 for the shared dungeon rooms) |
| `maps` | optional: the maps running the script (the port knows the shared groups anyway) |
| `offset` | the transfer instruction's byte offset in the script code |
| `routine` | the byte offset of the routine's `frame` (doors.json's `routine`): what a trigger region starts |
| `key` | open world: the key the door needs (0 / absent: none) |
| `to` | entrance shuffle: `{map, x, y, z, dir}` sent there instead (`dir` optional) |

- **Locked.** When a trigger region (field doors: thread_start regions; world-map entrances:
  ext 0x76) is about to start a routine whose seed doors all need a missing key, the routine
  does not start: nothing of it runs (the world map's music fade included), the leader keeps
  the controls, the banner says "Salva Pass needed", and the region fires again only after the
  leader has left it (the triggers' own last-trigger rule). A locked door reached another way
  (a talk handler, a scene) skips the transfer instruction itself.
- **Checked as the map loads.** A seed door whose word is not a map transfer on that map's
  script is ignored and logged (`door D: map M offset X holds W, not a map transfer`), as a
  patch with the wrong EXPECTED is.
- **In story mode** keys never lock a door; a door's `to` applies in both modes.
- **The suggested key list** (doors.json `key_group`): the 66 world-map entrances (Expel 44,
  Nede 22), grouped by the area they enter (35 groups). Field-internal doors stay open, so the
  scenes inside towns play as they do. The APWorld picks the final list and names.

### The Expel teleporter

The disc switch (map 625, chapter 35) leaves Expel behind until the Final Save Point opens
Virtual Expel. In an open-world seed a progression item can sit in Expel, so the teleporter
uses the game's own two-way route, opened from chapter 36:

- **Out:** the Fun City Fighting Arena's old man (map 303, actor 25, routine 0x4c14) offers the
  trip to Arlia when `flag[730] == 1 && flag[2] == 0` (flag 730: the Final Save Point). The
  test becomes `chapter >= 36 && flag[2] == 0`. Yes transfers to Arlia (map 18, 0x4dfc).
- **Back:** Arlia's guide (map 18, actor 26, registered at 0x2688 under `chapter >= 60 &&
  flag[2] == 0`, flag 753 clear) offers the trip to the arena (0x1fec -> map 303). His
  chapter test becomes `chapter >= 36`.
- **How:** three patch lines the port adds to the seed's own when `expel_teleporter` is on
  (logged `seed:N` like the seed's, each skipped if the disc's word differs):

      script 303 0x1307 0x3b0302da 0x430000a0   (eq flag 730, 1  ->  ge chapter, ...)
      script 303 0x1308 0x00000001 0x00000024   (...  36)
      script 18 0x996 0x0000003c 0x00000024     (ge chapter, 60  ->  36)

- **Why not set flag 730 early:** it is also read by Central City (map 236: Filia's Private
  Action 0x200 from chapter 58) and Arlia (map 18 0x2738, chapter 60), so setting it would
  change those events. The two tests above have no other reader.
- **Untouched:** the disc-swap flow (B 0x6a at map 625), the Final Save Point, and everything
  at chapter 60. Fun City itself opens with the Nede world map; the logic connects Expel to
  Fun City (both ways) in open world.
- **Checked headless** (scratchpad/world/ap4/runs, event sweep at chapter 40 step 0): map 303
  K25 transfers to map 18 with the teleporter, only talks without it; map 18's K26 exists only
  with it, and its yes branch transfers to map 303.

### Shop slots (`shuffle_shops`)

| `check.type` | Fields | Where the port checks it |
|---|---|---|
| `shop` | `shop` (the B 0x6d shop id), `index` (0-based entry of the shop's list), `item` (optional: the item the entry must sell) | the shop page's settlement (menu_shop.cpp `settle`), bought or ordered |

- Id: type 10, `0x50320000 + (10 << 21) + (shop << 8) + index`.
- `index` is the entry's place in the list the shop shows: the original stock list with the
  seed's own `shop` patch lines applied (without them, the original index). For shop 0x58
  (the stock that grows with flags) it is the place among the entries shown.
- The list shows an unchecked slot's name in colour 2. Buying it (any count): the first copy
  is the check. Its item is given at once (an own item: banner "Got X" once the shop closes;
  inventory full: given later) or sent ("Sent X to Y"); the other copies are the shop's
  item. After the check the entry is an ordinary one. The price is the entry's own.
- Order shops (0x5a-0x5e): the order is the purchase; the slot's copy is not delivered.
- An entry that sells another item than `item` is not the slot (nothing recorded).

### The door table (`tools/ap_doors.py`, `docs/ap/doors.json`)

    tools/ap_doors.py DISC1.cue [--field docs/ap/field.json] [-o docs/ap/doors.json]

- 2529 doors: every map transfer with a constant target map (2449 inline, 80 with their
  arguments pushed as constants), each with `id`, `map` (+ `maps` for a shared script),
  `offset`, `routine`, `op`, `form`, `to` {map, transition, x, y, z, dir, extra},
  `requires` / `via` (as ap_extract writes transfers), `world` (1 / 2 / 3: a world-map
  entrance from a chunk script), `world_script` (the world script's warps: not play),
  `from_area` / `to_area`, `key_group` (world entrances).
- `arrivals`: the 2044 distinct targets (map, x, y, z, dir) with the doors that use them.
- **The validator** (`apworld/so2/doors.py`, standard library only, so the `.apworld` ships
  it): `validate_doors(seed_doors, doors_json)` checks that every door id exists, that every
  `to` is an arrival point of the game (some door's original target), that every key is
  1-255, and that every target map gets back to a world map over the doors (with the seed's
  targets applied). It returns the problems, an empty list for a good table. CLI:
  `python3 apworld/so2/doors.py SEED.apso2 [--doors docs/ap/doors.json]`.
