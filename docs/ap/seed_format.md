# The seed file (`.apso2`) and the save sidecar

Phase 1 of [the Archipelago plan](../archipelago_plan.md). The APWorld (`worlds/so2`) writes one
`.apso2` file for each Star Ocean player in a multiworld; Archipelago packs every player's file
into the multiworld's `AP_<seed>.zip`. Shattered Stars reads the seed at start-up, from
`--ap-seed FILE` (this run only) or the `ap_seed` key in config.ini, which the setup program
sets. FILE is the `.apso2` or the archive; in an archive with several Star Ocean seeds,
`--ap-seed-entry NAME` / config.ini's `ap_seed_entry` names the one to play (the game copies it
out to `archipelago/` next to config.ini; `engine/src/launcher/ap_seed.h`). Without a seed, Archipelago is off and the game plays as the original.

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
| `version` | 1, 2 or 3 | required; this build reads 1 to 3 (2: "v2 fields" below; 3: a hub seed, "Hub") |
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
200-Fol crate (flag 0x962) pay 5000 Fol. A hell seed also carries `script 297 0x177f 0x120007d0
0x12000000`, the Fun City arena's fee waived ("The hell seed" below; so2.apworld 0.2.1).

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
  like the map-name notice, and the next waits until the banner has gone. An item the
  inventory has no room for (20 of a kind at most) is set aside [queuebypass, 2026-10-27]: one
  banner, "Inventory full: Blackberries waiting" (when that is too wide for the banner, "X waits
  for room", then "No room for X", then "Inventory full: an item waits"; no banner when another
  of its kind already waits), and the items behind it, the server's included, go on. A server
  item set aside joins the queue with its sender (the received index moves past it). Set-aside
  items are tried again at every free play (after a menu, a shop, a crafting attempt or a battle,
  whatever made room), the oldest first, and arrive with their usual banner ("Received
  Blackberries from Bob"); a failed retry shows and logs nothing. A count that fits in part gives
  what fits and the rest waits the same way. Before this, the item at the front of the queue held
  up everything behind it until there was room, and copies of a count past 20 were lost.
  `ap_status` lists the items set aside (`inventory full, set aside: ...`). The console's
  `ap_give ITEM [FROM]` takes an AP id or name from the seed's `items`, or a game item number.
- **The start.** The first free play of a new game gives `start.items` and `start.fol` and
  sets `start.flags`.
- **The console.** With `--debug`, `ap_status` prints the seed, the slot, checks done of
  total, the received index and the items waiting.

## The save sidecar

The Archipelago state of a save sits in a file next to the memory card, so the card stays
byte-compatible with the console's. A seed plays on its own card,
`archipelago/<seed>_<slot_name>/memcard1.mcd` next to config.ini (`launcher::seed_memcard`), so
its saves and sidecars never mix with ordinary ones; a card named for the run (`--memcardN`,
`SS_MEMCARDN`) overrides it.

    memcard1.mcd, save 3  ->  memcard1.slot03.ap.json

```json
{
 "checked": [1347569665],
 "format": "apso2-save",
 "goal": false,
 "received_index": 1,
 "save_hash": "sha256:9f2c...e41a",
 "seed": "smoke-1",
 "slot_name": "Claude",
 "start_given": true,
 "version": 2
}
```

- `received_index` counts the received items given in this save. On connect, phase 2 asks
  the server for everything past it.
- `checked` lists the location ids in the order they were checked.
- `start_given` records that the starting inventory is in the save.
- `goal` records that the goal battle was won; it is sent again on every connect.
- `save_hash` (version 2, 2026-10-25) is the save as it lies on the card: `"sha256:"` and the
  SHA-256 of the save file's first N bytes, N = the save's own length (u16 at file +0x21a, the
  summary's +0x1a; `util::save_digest`). The game takes it from what the card driver reads back
  after writing the save. Version-1 sidecars (no hash) still read, but a seed refuses to load
  their saves (below).
- `hub_view` (hub mode, optional; agent "hubask", 2026-10-25): the encounter of a chamber that
  asks first (its row's `ask`) whose story view was on when the save was made (the player had
  said yes). The load turns that view on again with the map's load; without it a load in such a
  chamber comes up at the hub's own state. Absent everywhere else.
- `starting_character` (open world and hub, optional; agent "charselect", 2026-10-25): the party
  id (1-12) of the member the player picked on New Game's character select (a seed whose
  `options.starting_character` is `"any"` or a list), when that is not the hero. A load makes them
  the starting character again (they count as received, as below). Absent: none (no pick, or
  the hero picked); a value outside 1-12 reads as none.
- `leader` / `slot_0` (open world and hub with a starting character who is not the hero,
  optional; agent "partybuild", 2026-10-26): the party ids (1-12) of the member the player left
  leading (config +0x41's) and in party slot 0, as of the save (or the last camp-menu close).
  "The hero in slot 1" (under Party v2) puts that order back after a scene reorders it.
  Absent (a save from before, or no starting character): the loaded party's order is taken at
  the first free play; a value outside 1-12 reads as none.
- `bench` / `bench_hash` (a bench seed with someone on the bench; agent "partybuild",
  2026-10-26): the bench's members, places 8-11 ("The bench" under Party v2), each
  `{"place": 8-11, "bytes": "<hex>"}` with the member's party slot (0x60 bytes) and record
  (0xd0 bytes), as the stash's entries; only the places that hold someone. `bench_hash` is
  `"sha256:"` and the SHA-256 of the save's bytes as `save_hash` takes them followed by the four
  places' 0x130 bytes (zeros for an empty place). Written only with someone benched (the
  sidecar is then version 3: "The sidecar's version" below); with nobody benched neither key is
  written. A load takes the bench as the sidecar holds it (no merge: it is the save's state, not
  the multiworld's); a load without a sidecar, and New Game, start with an empty bench. A place outside 8-11 or given twice, other than 0x130 bytes,
  or a member that is no party id 1-12 (or benched twice) makes the sidecar unreadable.
- `lent` (a bench seed with a benched member lent for a fight; agent "partybuild6",
  2026-10-27): `[{"id": 1-12, "place": 8-11}]`, the members lent from the bench to the 8 for a
  fight the story makes them make alone ("Loans" under "The bench") and the place each keeps.
  Their bytes are in the card's 8 slots meanwhile, their place empty. Written only with a loan
  out (version 3, below); `bench_hash` is then written even with nobody else benched; it covers
  the loans (each loan's id and place, one byte each, after the four places; no loan adds
  nothing, so a bench's hash is unchanged). Absent: no loan. A loan of no member 1-12, of a
  benched member, to a place outside 8-11 or a taken one, or given twice makes the sidecar
  unreadable. A loan's borrowed slot is a `stash` entry like a temporary member's, its `by` the
  lent member (`by` below).
- `pending` (optional; agent "pendingsave", 2026-10-27): the items still waiting to be given in
  free play when the save was made, in order. A check that brings items (a guild set under
  skillsanity, a boss, a craft, a killer-move level, learn points at a join...) queues them, and
  they come one per banner; a save in between used to lose the rest. Each entry is
  `{"location": ID}`, a check's own item (the item comes from the seed's location), or
  `{"item": {...}, "from": "Bob", "found": true}`, any other item (the courier's overflow, the
  console's `ap_give`), the item object as in the seed's `items` (`from` and `found` only when set).
  Items set aside for want of room [queuebypass, 2026-10-27] are entries like any other, in their
  place in the queue: a server item set aside is an `{"item": ..., "from": ...}` entry (the
  `received_index` is past it, so it is never given twice), and the rest of a check's count that
  fitted in part is the item itself with the count left. Whether an entry was set aside is not
  written: a load tries every entry again, and one still without room is set aside again with its
  banner.
  Written only with something waiting (then version 3, below: a port from before refuses the save
  instead of dropping the items). Absent: nothing waits.
- `pending_hash` (with `pending`): `"sha256:"` and the SHA-256 of the save's bytes (the same N
  bytes as `save_hash`) followed by the text of `pending` as `ap::Json::dump` writes it (keys
  sorted, one-space indent). The guard reads the list and writes it again before hashing, so
  spacing and key order in the file don't matter; any changed value does (below).

```json
 "pending": [{"location": 1347569672}, {"location": 1347569673}],
 "pending_hash": "sha256:f5ba...5028",
 "version": 3
```

- `stash` entries' `by` (agents "partybuild6" / "shingofix", 2026-10-27; port 0.02.51): the
  party id (1-12) of the member who borrowed the slot (a temporary member; "The party rules"
  below). The stash goes back to that member's slot even after a scene moved them; absent (a
  sidecar from before), the temporary member found in the 8 is taken. Written with every new
  stash; no new version: a port from before ignores it and puts the stash back by its slot, as it did. A
  value outside 1-12 reads as absent.
- `tournament_dias` (agent "shingofix", 2026-10-27; port 0.02.51): the kept Dias's own data
  while the Lacour tournament has rewritten him. Map 477's fights rewrite Dias after his first
  fight (special 0xc: his fixed stats, level 35) and after later ones (0xd: his starting data
  again, then the join level). A kept Dias (received, or the starting character; not the story's
  stand-in) keeps his own (the user's decision: "Save his data before the tournament rewrites
  him, and restore it after his last fight"): his slot (0x60 bytes) is saved before the first
  rewrite, his record (0xd0) before the first 0xd (0xc leaves it alone, so until then it is his
  own and any change the player makes to it counts), and both go back right after his last fight
  there (step 10200 in Claude's game, 16500 in Rena's; chapter 19), wherever he stands then (a
  benched Dias is lent to the 8 for each fight; one held while he is on the bench goes back to
  his bench place: "Loans" under "The bench"). The key holds them as lowercase hex, Dias's id first: 0x60 bytes (slot only) or 0x130 (slot, then
  record). Written only while held (then version 3, below: a port from before refuses the save
  rather than leave him with the tournament's data). Absent: nothing held. Any
  other length, a non-hex character or another member's id makes the sidecar unreadable.
- `tournament_dias_hash` (with `tournament_dias`): `"sha256:"` and the SHA-256 of the save's bytes
  (the same N bytes as `save_hash`) followed by the held bytes themselves.

```json
 "tournament_dias": "05000804...",
 "tournament_dias_hash": "sha256:51d6...5e76",
 "version": 3
```

**The sidecar's version** (one rule for every key above): **3** exactly when the save holds
something an older port would drop: items waiting (`pending`), someone on the bench (`bench`), a
member lent from it (`lent`), or the tournament's Dias held (`tournament_dias`); else **2**, the
same bytes as before those keys existed. `leader`, `slot_0`, `starting_character`, `hub_view` and
the stash's `by` need no new version (a port from before ignores them). A port from before
0.02.51 reads 1 and 2 and refuses a 3 ("unsupported sidecar version"), so such a save does not
load there rather than lose what the keys hold; this port reads 1 to 3.

The game writes the sidecar once the card write is verified (the save dialog's
`save_finish` returns 1). It writes through `.tmp` and a rename, so a crash never leaves half
a file.

**The save guard** (all world modes; the user, 2026-10-25: "block save editing for the archi
runs"; `ap::load_allowed`, called by `menu/menu_save.cpp` `load_finish` before the save reaches
the game's memory). With a seed on, a save loads only when its sidecar names this seed and slot
and its `save_hash` is the hash of the save as the card holds it now. Refused: a save without a
sidecar (an ordinary game's, or one copied without its `.ap.json`), another seed's or slot's, a
sidecar without the hash (version 1, or the field stripped), and a save whose bytes changed on
the card since it was saved. A sidecar with items waiting also needs `pending_hash` to match the
save and the list: an edited, extended or reordered `pending`, one without its hash or a hash
without its list, or the list beside another save is refused (log `... this save was changed
outside the game (its waiting items differ from the ones this seed saved)`). Both keys removed
leave a sidecar with nothing waiting: it loads, and only the items are lost. A sidecar with a
bench (or a loan) needs `bench_hash` to match the save, the bench and the loans the same way: an
edited bench or loan, a bench or loan without its hash, a hash without its bench, or a bench moved
here from another save is refused (`... its bench differs from the one this seed saved`);
stripping all of them only loses the benched members (a lent member stays in the 8). A sidecar
holding the tournament's Dias needs `tournament_dias_hash` to match the save and the held bytes
the same way (`... its Dias held for the tournament differs from the one this seed saved`); both
keys removed load, and Dias keeps the tournament's data. A refused load fails the way a damaged save does: the load screen
shows the game's own "The data could not be loaded. Your data may be faulty." (`SS_LOAD_SAVE`:
"no valid save at that position"), and the log says why (`[ap] load refused, save 1 of CARD:
this save was changed outside the game (it differs from the one this seed saved)` / `... this
save isn't from this Archipelago seed (...)`). This is a guard against accidents and casual save
editors, not cheat-proofing: the port is open source, and anyone can recompute the hash.
Without a seed nothing is checked and the card's bytes are the same as ever (only the sidecar
gained a field). Headless tests that load ordinary saves with a seed set `SS_AP_ANY_SAVE=1`
(logged "loaded anyway"); a windowed seed run clears every `SS_*` hook, so it can't.

Runs in progress stay playable: when a seed's own card is made, the saves an earlier version
made for the seed on the player's cards are copied to it (`launcher::adopt_seed_saves`) and each
copied sidecar is stamped with its save's hash (and version 2). A seed card that a build from
before the guard (2026-10-25, unreleased) already filled holds version-1 sidecars: their saves
no longer load.

After an allowed load the game reads the sidecar (the load dialog, or `SS_LOAD_SAVE`):

- (Only with `SS_AP_ANY_SAVE=1`.) A save without a sidecar loads as an ordinary game: no
  checks, and no starting inventory on top of it; another seed's sidecar is used anyway.
- When the sidecar's seed matches, checks made since the save stay checked, because the
  multiworld keeps them.
- Items still waiting are given in the loaded game [pendingsave, 2026-10-27]: first the save's
  `pending`, as it was saved; then the own items of the checks the running game has and the save
  lacks (made since the save, or reported by the server), because those checks never fire again
  and the loaded game lacks their items, except chests and gifts, which the loaded game can open
  again (their flags come back with the save, and the placeholder gives the item again). The
  queue the running game had is dropped with it: nothing is given twice, a second load of the
  same save gives the same list, and a save loaded in a new session gives the same as one
  loaded in the session that made it. Before this, the queue was kept only within a session, so
  a save made between a check and its last banner lost the rest after a restart.
- On connect, a check the server reports that the running game lacks (made in a game that was
  not saved, or in a later save) queues its own item the same way (not a chest's or a gift's);
  a reconnect adds nothing (only checks new to the game count).
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
  - A victory (outcome 0) checks the seed's `boss` locations for that encounter, and their own
    items are given in free play. So does a time-up (outcome 4: only ext 0x72's 60-second
    fights end that way, with a member standing: the timed survivals Shin 516 and Berle 520)
    and, in a fight the script lets the party lose (field_flags +0x7E = 0, latched by
    `ap::battle_frame` during the fight), the wipe (outcome 2: Dias 204, Shin 517, Marsilio
    530; the story goes on). An escape (1, 3) never does.
  - A goal encounter won (outcome 0 only) sets `goal` in the sidecar and sends CLIENT_GOAL.
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
| `story_route` | `any`, `claude`, `rena` (or 0-2) | `any` | `claude` / `rena`: New Game skips the hero select and the naming screen and starts that hero's game with the default name, in every mode (log `story route Claude (the seed's): New Game skips the hero select and the naming screen (Claude)`; Circle on the combat-mode screen goes back to Vibration; the camp menu's Status page renames as usual). `any`: both screens as the original. (Until 2026-10-25 the port kept the cursor on the seed's side instead.) Seeds from so2.apworld 0.1.0 / 0.1.1 call it `protagonist`; the port reads either. |
| `character_items` | true / false | false | Listed join sites are checks; members join through their items. |
| `enemy_dropsanity` | `off`, `first_drop` (or 0 / 1) | `off` | The first drop of each enemy type with a `drop` location is a check. 0.1.0 / 0.1.1 seeds call it `drops_as_checks`; the port reads either. |
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

### First drops (`enemy_dropsanity: first_drop`)

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
| A timed survival (ext 0x72 rule 3: Shin at the Lacour Front Line 516, Berle 520; a loss is a game over) | as in any fight: the party falls, the game over follows | on the wipe's game over |
| A story battle the script lets the party lose (ext 0xb7's third argument 0, kept in field_flags +0x7e; random and menu battles set it to 1: Dias 204, Shin on the ship 517, Marsilio 530) | waits for the field after it | never: the loss is no game over, the scene goes on |
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
| `keys` | open world: keys the door needs, all of them (a one-way story point: the El voyage's trigger, 0.1.1) |
| `until` | open world: a chapter; the keys are needed only while the story's chapter (G[0xa0]) is below it (an exit kept shut while the story is inside the area) |
| `purpose` | the banner's ending for a missing key ("to sail": "Eluria Pass needed to sail"; left out when the text would not fit the banner, 236 pixels) |
| `to` | entrance shuffle: `{map, x, y, z, dir}` sent there instead (`dir` optional) |
| `to.while_unbeaten` | (hub P5, 2026-10-25) an encounter id: the target applies only while it is not in the sidecar's `goal_won`; after the win the door is the original again (logged `door N: encounter E won, the door's own target (map M)`). The arena 305's door (see "Hub", P5) |

- **Locked.** When a trigger region (field doors: thread_start regions; world-map entrances:
  ext 0x76) is about to start a routine whose seed doors all need a missing key, the routine
  does not start: nothing of it runs (the world map's music fade included), the leader keeps
  the controls, the banner says "Salva Pass needed", and the region fires again only after the
  leader has left it (the triggers' own last-trigger rule). **On a field map the shut region
  holds the leader** (2026-10-26, salvadrift; engine/src/game/field/script_triggers.cpp
  `ShutDoor`): the leader gets no further in than the step that entered it (a step that ends
  deeper along that step's line goes back; stepping back out or aside is free), the banner
  comes once per approach (until the leader is 8 px from the region), and a door that opens
  meanwhile (its key received) fires at the next step in. The original never lets anyone stand
  in a door's region, and past some there is nothing to stand on: behind Alen's bookcase (map
  53, door 3480401) the ramp ends in open space where a refused leader walked on into the dark,
  past Salva's barricade (map 43) onto the cliff picture. World-map entrances lie on open land
  and keep the old rule. A locked door reached another way
  (a talk handler, a scene) skips the transfer instruction itself. A door with `keys` is locked
  while any of them is missing, and the banner names each missing one in turn, with the door's
  `purpose` ("El Beach Pass needed to sail"); a key's banner already waiting is not queued
  again. Such a door must name its `routine`: the gate is the trigger (the El voyage's routine
  stores chapter 28 before its transfer).
- **The APWorld's gates (0.1.1).** After the 66 entrances, the seed lists the El voyage's
  trigger in the Lacour Front Line's meeting room (`{"id": 29106503, "routine": "0x7c28",
  "keys": [18, 19, 17], "name": "El voyage", "purpose": "to sail"}`; with the teleporter
  and expel_progression on, also keys 29, 30 and 31) and two exits kept shut until a chapter:
  Central City's (`{"id": 15407589, "routine": "0x670c", "keys": [28], "until": 38, ...,
  "purpose": "to leave"}`) and Noel's House's (18160748, routine 0x7164, key 30, until 40).
  A port before 0.02.00 reads only `key`, so it does not apply them.
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

### The harbour (Port Hilton, open world; port behaviour, 2026-10-25)

Port behaviour only: no seed field. The port decides from `world_mode: open_world`, so open-world
seeds made by so2.apworld 0.1.1 get it with a port update (docs/handoff/ships.md).

- **The soft-lock it fixes.** Entering Lacour (214) from the world map at chapter 22 step 9999
  stores chapter 23 (909 routine 0x47e8). Port Hilton's sailor (map 169, actor 17, talk routine
  0x714c) is the only way off the Lacour continent, and he refused the passage to Cross at
  chapters 23 and 24 ("'Tis a black day for Lacour", string 107); chapter 25 is stored on map
  231, after the Hoffmann trip, which needs the Map 231 and Hoffmann Passes, and the fill may
  have put those on the Cross continent. In an open-world seed he no longer refuses then, nor at
  chapter 18 from step 6000 (the Tournament Pass).
- **The captain's menu.** His Cross / Don't cross choice becomes "Where to, matey? Cross is N
  FOL." (his own speaker label, the fare he works out) with **Cross / Hoffmann Ruins / El / Not
  now**, only the destinations open now (Cross and Not now alone keep his own strings):
  - Cross: always; his own fare and voyage (flags 16 + 17, landing at Herlie).
  - Hoffmann Ruins: chapters 23-59 (map 231's soldier sails back until 59); the Hoffmann
    soldier's own sail (169 0x7cf8: flags 16 + 22, map 231), which at chapter 23 stores chapter
    24 as the story's first trip does. No fare (the soldier takes none).
  - El: only while the story is at the voyage (chapter 27 step 7000, what room 444's trigger
    waits for; disc 1, so never after the disc switch), and only when every key of the seed's El
    voyage door (29106503, its `keys`) has arrived. Without them it is still listed and choosing
    it shows the El gate's banners ("Eluria Pass needed to sail", one per missing pass) and
    stores nothing: the player sees why the ship will not sail rather than wondering where the
    option went. With them it does what room 444's routine 0x7c28 ends with: the music and the
    screen fade (90 frames), chapter 28 step 0, map_transfer_c(422, 130, -215, 0, 0, 180, 128):
    Lacour Castle's farewell, Port Hilton's departure, the ship (548). The keys are checked
    before anything is stored. Room 444's own trigger and its gate are unchanged.
- **The Hoffmann ship** (actor 24, routine 0x79c0) stands at Port Hilton at chapters 23-59,
  not 23-27, and offers "We'll go to the ruins again" until 59 (the four "Lacour Hope" soldiers
  beside him stay as long: their lines are the chapter 24-27 ones).
- **Landing keys first.** A voyage whose landing door (the world-map script's transfers, 909:
  0x5cf8 Herlie, 0x5ad0 and 0x6120 Port Hilton, 0x5f08 map 231) needs a key not received is
  refused at the NPC, before the fare or any voyage, with the banner "Map 231 Pass needed to
  sail": the captain's Cross and Hoffmann Ruins, the Hoffmann soldier's yes, Herlie's sailor
  (map 144, routine 0x7298; the Hilton landing). Map 231's soldier needs no check (the party
  came through Port Hilton, whose key is the landing's). The landing doors themselves still
  refuse a voyage reached another way.
- **How.** Twelve patch lines the port adds to an open-world seed's own (logged `seed:N`; each
  skipped if the disc's word differs; NA words, other releases through region::script_offset:
  Germany lays map 169 out otherwise from 0x7c70, +84 bytes; verified on NA / EU / FR / DE):

      script 169 0x1c65 0x00000012 0x000003e7   chapter 18 refusal: never
      script 169 0x1c79 0x00000017 0x000003e7   chapter 23 refusal: never
      script 169 0x1c7c 0x00000018 0x000003e7   chapter 24 refusal: never
      script 169 0x1c99 0x12000002 0xfe700000   push 2 (option count) -> port op A 0x70
      script 169 0x1ca6 0x19800004 0x0b800004   push_eq L[4], 0 -> push L[4] ...
      script 169 0x1ca7 0x00000000 0xfef10000   ... port op A 0x71 (the voyage chosen)
      script 169 0x2647 0x0000001b 0x0000003b   actor 24 registered: chapter <= 27 -> 59
      script 169 0x1f23 0x0000001b 0x0000003b   his "again" choice: chapter <= 27 -> 59
      script 169 0x1f3b 0x19800000 0x0b800000   push_eq L[0], 0 -> push L[0] ...
      script 169 0x1f3c 0x00000000 0xfef20016   ... port op A 0x72, landing flag 22
      script 144 0x1cdc 0x19800000 0x0b800000   Herlie's sailor: push L[0] ...
      script 144 0x1cdd 0x00000000 0xfef20012   ... port op A 0x72, landing flag 18

  The port-only ops are table A subs (opcode 0xfe) the original leaves empty (above 0x48;
  field/script_table_a.cpp): **0x70** pushes the option count (the stack holds the choice
  library call's question string; the next instruction pushes the options string; both are
  then replaced while the menu is open); **0x71** pops the option and pushes 1 for the sailor's
  own fare and Cross voyage, 0 for nothing, or goes on in the Hoffmann soldier's sail, or plays
  444's ending; **0x72** (low byte: the landing's flag) pops the option and pushes `option == 0`
  and the landing's key held. The choice library (0x484c) takes up to four options.
- **Story mode, no seed:** none of it (the sailor refuses at chapters 18 / 23-24 as the original).

### Shop slots (`shuffle_shops`)

| `check.type` | Fields | Where the port checks it |
|---|---|---|
| `shop` | `shop` (the B 0x6d shop id), `index` (0-based entry of the shop's list), `item` (optional: the item the entry must sell) | the shop page's settlement (menu_shop.cpp `settle`), bought or ordered |

- Id: type 10, `0x50320000 + (10 << 21) + (shop << 8) + index`.
- `index` is the entry's place in the list the shop shows: the original stock list with the
  seed's own `shop` patch lines applied (without them, the original index). For shop 0x58
  (the stock that grows with flags) it is the place among the entries shown.
- The list marks an unchecked slot with the Archipelago logo after its name (a 12 x 12 port
  image in the menus' text VRAM; when there is no room for it, the name in colour 2).
- Buying it (any count) gives every copy bought, and this first purchase is also the slot's
  check: its item given at once (an own item: banner "Got X" once the shop closes; no room for
  it: given later) or sent ("Sent X to Y"). After the check the entry is an ordinary one. The
  price is the entry's own. (Ports up to 0.02.00: the first copy was the check, so count - 1
  were given, and the colour-2 mark never drew; the port change alone fixes existing seeds.)
- Order shops (0x5a-0x5e; the APWorld makes none of them slots): the order is delivered as
  ordered, and placing it is the check. With a seed, ordered copies the inventory refuses at the
  delivery (20 of a kind) go in as far as they fit and the rest wait as "Got X" items.
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

## v2 fields (so2.apworld 0.2.0, 2026-10-25)

Written by the APWorld agent "apworld2" (log `docs/handoff/apworld2.md`) for the port agents
of wave 2. Everything here is additive: a 0.1.x seed reads as before, and a 0.2.0 seed that
turns none of these features on is a version-1 seed. (It still needs port 0.02.50: 0.2.0 names
two options the new way only, `story_route` and `enemy_dropsanity`, which the 0.02.00 port
doesn't read, so there it would lose its route and its first-drop checks. Release agent,
2026-10-25.)

### The version rule

`version` is the lowest format that can play the seed:
- **1** when the seed uses nothing below that an old port would ignore;
- **2** when it uses any of: `goal.all`, `goal.flags`, a door with `flags` / `won`,
  `party_rules: "open"`, a `starting_character` (a name, or a list / `"any"`: the character
  select), `craft` or `killer_move` locations, `no_fail_crafting`, a `difficulty` other than
  `earth` (an old port would play it on Earth), or `skillsanity` other than `none` (its item kinds
  `skill` / `move`, its checks `guild` / `learn`, a shop slot's `price`) [skillsanity2].

A 0.02.00 port refuses version 2 ("unsupported version 2 (this build reads 1)") instead of
silently ignoring a gate, a party rule or a goal condition. A wave-2 port reads 1 and 2.
slot_data's `seed_format` is the same number. (The port reads both since 2026-10-25, and 3
(a hub seed, "Hub" below) since the hub's port work the same day. It reads the v2 fields in a
version-1 seed too.)

- **4** when `options.bench` is on (the 12-member bench, "The bench" under Party v2; agent
  "partybuild", 2026-10-26), in an open-world or a hub seed. A port that reads 1 to 3 refuses
  it ("unsupported version 4 (this build reads 1 to 3)") instead of playing a 12-member pool
  with 8 places; this port reads 1 to 4 and refuses 5 and up ("unsupported version 5 (this
  build reads 1 to 4)").

### New `options` keys

The port reads the ones marked **read**; the rest are for reference (the APWorld already
applied them to the locations and items it wrote).

| Key | Values | Default | Port |
|---|---|---|---|
| `goal` | `indalecio`, `indalecio_limiter_off`, `indalecio_limiter_off_and_iseria_queen` | `indalecio` | reference; the port reads the top-level `goal` object (below). YAMLs may still say `final_boss` (= `indalecio`). |
| `drop_progression` | `allow`, `force`, `never` | `allow` | reference (where the APWorld lets progression go) |
| `area_keys_in_drops` | true / false | true | reference |
| `craftsanity` | true / false | false | reference; the checks are the seed's `craft` locations |
| `no_fail_crafting` | true / false | false | **read**: item creation never fails by chance (below) |
| `killer_move_checks` | true / false | false | reference; the checks are the seed's `killer_move` locations |
| `killer_move_progression` | `never`, `hero`, `any` | `never` | reference |
| `starting_character` | a character name (as `options.characters` spells them), `null`, or a list of names (or `"any"`): the player picks at New Game | `null` | **read** (open world and hub; below) |
| `exclude_characters` | list of names | `[]` | reference |
| `party_rules` | `"story"`, `"open"` | `"story"` | **read**: the party rules (below) |
| `difficulty` | `earth`, `galaxy`, `universe`, `any` (or 0-3) | `earth` | **read**: New Game's Battle Rank (below) |
| `apworld_version` | `"0.2.1"` (0.2.0 and later) | | reference |
| `skillsanity` | `none`, `skills_only`, `special_only`, `insanity` (or 0-3) | `none` | **read** ("Skillsanity" below) |
| `skillsanity_shop_share` | 0-100 | 10 | reference (the APWorld placed the items; the port reads each slot's `price`) |
| `bench` | true / false (or 1 / 0) | off when absent (the APWorld's option: on by default, B7; so2.apworld 0.2.1 writes `true` exactly where the bench applies, and nothing otherwise) | **read** with `party_rules: "open"` in open world, and in the hub: the 12-member bench, seed version 4 ("The bench" below) |
| `hell_seed` | `true`, only on a hell seed (absent on every other seed) | absent (false) | **read**, display only: setup's warning and a log line ("The hell seed" below; so2.apworld 0.2.1) [hellseed] |

`characters` (phase 3: the members the item pool holds) may now name Claude, Rena and Noel,
and Leon / Dias in either game, under `party_rules: "open"`.

### Difficulty (`difficulty`)

The original's Battle Rank (the difficulty) is the config byte **+0x4f** (handoff 2): 0 Earth,
1 Galaxy, 2 Universe. New Game writes it (title `new_game_sequence` 0x80042cc8: 0 after the
combat mode, the Battle Rank screen's choice); the save keeps it (the config block is saved).
The screen appears only when `g_battle_rank_unlocked` (title 0x8004ec28) is set, which only the
Voice Collection does, in the same power-on, once more than half of the voices are heard in the
cards' saves. The only reader is the battle's `enemy_read_stats` (0x80076298): Galaxy gives the
monsters x1.5 attack and HP; Universe x5/3 attack, x2 HP, x2 MP (at most 999), +0x80 evasion,
+0x50 accuracy and x4/3 intelligence. EXP, Fol, drops and encounters do not change, so the
APWorld's logic is the same on every value.

What the port does (agent "difficulty", `engine/src/game/title/new_game.cpp`), after the combat
mode (so after the story route's skip and the character select, and on every way back):
- `earth` / `galaxy` / `universe`: config +0x4f is set and the Battle Rank screen is skipped,
  whatever the Voice Collection unlocked (log `[ap] difficulty Universe (the seed's): New Game
  skips the Battle Rank screen`).
- `any`: the screen is offered with all three, unlocked or not (a seed's own memory card never
  unlocks it); Circle goes back to the combat mode as in the original (logs `[ap] difficulty any
  (the seed's): New Game offers the Battle Rank screen`, `[ap] difficulty: Galaxy picked`).
- No seed, Archipelago off, or a seed without the key (before so2.apworld 0.2.0's option): the
  original's rule.
- A value outside these stops the seed from loading ("options.difficulty: earth, galaxy,
  universe or any expected"). Continue loads the save's own difficulty; nothing is enforced on load.

### Goals

The top-level `goal` object:

| Key | Meaning |
|---|---|
| `encounters` | any-of (as before): a victory in one of these is the goal. Missing: `[539, 540]`. |
| `all` | new: every one of these encounters must be won, in any order; when present the port ignores `encounters` for the goal |
| `flags` | new: script flags the port sets at New Game **and again after every load** (the sidecar's or not). `start.flags` is applied once and never to a save loaded without a sidecar, which would leave a goal gate shut for good. Setting them again is harmless. |

- **`goal_won`** (sidecar, new): the encounters of `all` won so far, in order. On load it
  merges like `checked` (same seed: runtime ∪ save); an older sidecar reads as empty. The goal
  is sent (CLIENT_GOAL, sidecar `goal`) once every encounter of `all` is in it. A win counts
  as for `encounters` today: outcome 0 only.
- **Door conditions** (new, in the top-level `doors` table, like the El voyage's door):

| Key | Meaning |
|---|---|
| `flags` | script flags that must all be set |
| `won` | encounters that must all be in `goal_won` |
| `banners` | one text per condition, `flags` first then `won`, in that order (at most 236 px each); the port shows the first missing condition's |

  A door with `flags` / `won` is locked like a door with `keys` (the trigger does not start,
  the leader keeps the controls, the banner shows, the region fires again only after the
  leader has left it; on a field map the shut region holds the leader, see "Locked." above) and
  must name its `routine`. **These conditions apply in story mode
  too** (area keys don't). A door may carry `keys` and conditions together; all must hold.
- **The goal gate:** door **43978493** (map 671 -> 684, routine 0x3bec: the Final Save Point
  floor's way to the final corridor), so the banner shows where the player can turn back.

Per goal:

| `options.goal` | `goal` object | doors | version |
|---|---|---|---|
| `indalecio` | `{"encounters": [539, 540]}` (as today) | none | 1 |
| `indalecio_limiter_off` | `{"encounters": [540], "flags": [445, 446]}` | `{"id": 43978493, "routine": "0x3bec", "flags": [556], "banners": ["Talk to Filia in Central City first"], "name": "Indalecio (Limiter Off)"}` | 2 |
| `indalecio_limiter_off_and_iseria_queen` | `{"all": [561, 540], "encounters": [540], "flags": [445, 446]}` | the same door with `"won": [561]` and a second banner "Defeat the Iseria Queen first" | 2 |

- Flags 445 / 446 are the Clik meeting with Filia (Claude's game / Rena's game; both, so either
  game works); flag 556 is Limiter Off, set only by Filia's scene in Central City City Hall
  (map 239) after the Final Save Point; map 685 picks encounter 540 when it is set
  (scratchpad/openworld/goals.md). The preset costs only Clik's Filia Private Action.
- **The Silver Trumpet** (the user, 2026-10-25): in a seed whose `goal.all` holds 561, item 48
  is left out of every shop's sell list while flag 53 (song 7, "The Evil Melody") is clear.
  Selling it before composing would lose the Iseria Queen, the only source being G-Celesta.
- Iseria Queen (561) has no constant battle site: the song summons her (551 + Musical level).
  The port records the win at `battle_exit_to_field` as for any goal encounter.

### Party v2 (`party_rules`, `starting_character`)

**`party_rules: "story"`** (story mode, or open world without `character_items`, and every
0.1.x seed): phase 3's rules, unchanged.

**`party_rules: "open"`** (`world_mode: open_world` with `character_items`):
- **The story members** join through the story as usual: the hero (the story route picked at
  New Game), and under `story_route: "any"` both Claude and Rena. The hero is always in the
  party (Private Actions need them; slot 1 behind a starting character, "The hero in slot 1").
- **Every other member** joins only through their item (received, or found at one of this
  player's locations) or as the starting character. Their item is `{"kind": "character",
  "character": N}` as in phase 3; N may now be 1 Claude, 2 Rena or 11 Noel (the other hero
  and Noel can be items), and Leon / Dias in either game.
  - A join of a member who is neither received nor the starting character does not happen,
    at a listed join site (which records its check, as today) or anywhere else; the scene goes
    on. Exception: the temporary joins inside the story's split windows (the tournament,
    Linga, the Front Line, the ship, the Nede Fields; party_ships.md C), which the port lets
    through and undoes when the window ends.
  - **No lockouts:** Leon in Rena's game, Dias in Claude's, Bowman with Precis, Ashton with
    Opera / Ernest (story_route_refuses is off).
  - A received member's slot is never overwritten: with no free slot, a join is skipped.
  - Members not in `characters` and not story members never join (the APWorld left them out:
    `exclude_characters`, or no room).
- **New join locations** (check type `join`, as in phase 3): Rena's first join in Claude's game
  (map 9, offset 0x6b94), Claude's in Rena's game (map 12, 0x6e08) and Noel's (map 277,
  0x6fe0), listed only when that member is not a story member. Like every join site they hold
  only filler.
- The APWorld keeps the party to 8: story members + starting character + `characters`; with
  the bench (`options.bench`, so2.apworld 0.2.1, on by default) to 12, so `characters` is then
  every member not excluded but the story members and the starting character ("The bench").

**`starting_character`** (open world and hub; `null` in story mode): when it names a member,
- that member joins at the first free play of a new game (before `start.items` are given, so
  the banner order is "X joined the party" first), takes party slot 0 with the hero in slot 1
  ("The hero in slot 1" below) and becomes the leader (the Formation leader, config +0x41):
  the field sprite follows the leader, and dialogue and speaker labels use the leader's name
  where the hero's would be (the user, 2026-10-25: "Yes, swap names");
- they count as received: never refused, never overwritten, rejoining after a parting
  (`kPartings`) like a received member, and a story join of them while present is a no-op;
- when it names the hero of the game the player picked (possible under `story_route: "any"`),
  nobody joins and the hero leads;
- it works with `party_rules` either way: without `character_items` the story's own recruits
  join as in the original, and the starting member takes one of the 8 places (the port skips
  a story join that finds no free slot rather than overwrite a received or starting member).

**The character select** (port feature, agent "charselect", 2026-10-25; the user's request, the
open world and the hub). `starting_character` as a **list of names** (or party ids): the player
picks the starting member at New Game. so2.apworld 0.2.0 writes the list for `any`: the members
this world's party can hold (the heroes and `characters`, at most 8; under the story's party
rules every member not in `exclude_characters`); the logic counts on nobody extra, so the pick's
own character item, when it comes, finds them in the party ("is in the party already"). `"any"`
(the string) is read as all 12 but `exclude_characters`. A list or `"any"` is a v2 field ("The
version rule"); a name in the list that is no member stops the seed loading ("a list of
characters' names expected").
- **The screen** (engine/src/game/title/new_game.cpp `member_select_screen`): after the hero's
  name, or after the story route's skip; the hero select's format: its "Hero select" heading
  art, the twelve members' faces (the Status page's portraits, entries 0x1171-0x117c, scaled) in
  a grid of four by three on a shade panel on the left, the highlighted member's picture (the
  renaming page's, 0xbd3 + member: the hero select's own two are the same bytes) on the right,
  cross-fading as the cursor moves, and the bio window: Claude's and Rena's original bios; for
  the others the name, the race and occupation (Status pack 0xbb3 groups 0xb / 0xa) and the
  favourite food (group 7, item pack 3000; the Status overlay's table 0x80081612), read from the
  disc in its own language, and four lines of our own (engine/src/game/title/member_profiles.h,
  English in every release; when a release's race and occupation need two lines the last line
  is left out). Members not in the list are dark and refused (sfx 7, log `character select:
  this seed's party has no place for that member`); picking the hero means nobody extra. Circle
  goes back to the naming screen (or to Vibration after the story route's skip); Circle on the
  combat-mode screen comes back to the select, the cursor on the pick. Logs: `character select
  opens: 10 of 12 to pick`, `character select: Celine Jules`, then at New Game `starting
  character Celine: picked at New Game`, and the usual `joins at the first free play and leads`.
- **A named member** (open world and hub, not the hero): the same screen with that member's face
  alone, to confirm (Cross; `character select opens: Ashton Anchors to confirm`).
- **`hero`, `null`, story mode, no seed, Archipelago off**: no screen (New Game is the
  original's, or the story route's skip alone).
- The pick is kept in the save's sidecar (`starting_character`, above).

#### The party rules (what the port does; agent "party", `engine/src/game/ap/ap.cpp`)

Read: `options.party_rules` (`"story"` / `"open"`; missing = story; anything else stops the
seed loading) and `options.starting_character` (a name as `options.characters` spells them, a
party id 1-12, or `null` / `"hero"` / `""` / 0 for none; a list or `"any"`: the character
select above). Research: scratchpad/party/splits.md
(every party join and leave in the 820 field scripts, disc 2's being the same, with its
conditions and its return).

- **Joins under `"open"`** (script ext 0xae): the hero, both heroes under `story_route: "any"`,
  received members and the starting character join; anyone else is refused at any site (a listed
  one still records its check; log `join Celine at map 124 ...: neither a story member, received
  nor the starting character: not added`). No lockouts: `story_route_refuses` is off (Leon joins
  Rena's game, Dias Claude's). Leon's leave in Rena's game at the Eluria colony (234 0x74c8) and
  Noel staying at the sanctuary (417 0x7db8, choice 1) are partings for good like `kPartings`.
- **Slots.** A slot holding a story member or a member recruited for good (received, starting) is
  never overwritten, in either rules: with every slot taken a join takes the first reserve slot of
  another member (a story member the scene took along for a while, as the original's overwrite of
  the first reserve slot), else it is skipped (a received member's join then waits).
- **The split windows** (`kSplitWindows`). The story's splits take members out and put back
  exactly those it took (by the flags they stored: 195, 447, 469 / 474, 232, 401 ...); members a
  split does not list (Leon at Linga, Noel on the ship) stay. While a window is on:
  - a member recruited for good whom a split set aside stays out (no rejoin); a new join waits
    while the party is split (windows marked "everyone");
  - the members a fight needs alone are let in for a while, even when not received: **Claude** in
    Shingo Forest (map 12, chapter 4 step 150, Rena's game: the scene takes Rena out for Claude's
    fight) and at **the tournament** (chapter 19: Claude's fights in Rena's game), **Dias** at the
    tournament (both games; 434's join in Rena's game included). They are kept in the sidecar's
    `temporary` and leave the party (their slot emptied) at the first free play after the window.
    With every slot kept, such a member borrows the first reserve slot; its member's slot and
    record go to the sidecar's `stash` and come back when anything joins that member, when the
    borrower is out of the party, or after the window. A scene may move the borrower meanwhile
    (map 12's specials 1 / 2, the tournament's A 0x15 compaction before and after each fight):
    the stash remembers who borrowed it (`by`), and they go back to the borrowed slot first, the
    member there to theirs, so the stash never lands on another member (port 0.02.51).
  - a kept Dias (received, or the starting character) fights the tournament as the story's Dias
    (its specials 0xc / 0xd rewrite him) and leaves it with his own data: the sidecar's
    `tournament_dias` above (port 0.02.51).
  Outside every window (free play, not in a Private Action) a member recruited for good who is in
  reserve rejoins ("Celine joined the party"). The windows: Rena at home (Claude's game, 5/0-1000),
  Claude away (Rena's game, 5/2000-2500), the other hero away (5/3000-6/2100, 7/1000-8/0), Mars
  (15/1000-3300), Dias away in Rena's game (15/2700-19/1500, 19/5500-26/2500), the tournament (all
  of chapter 19), Linga (22/3000-5000), Leon away (25/3000 to the end of 30), the Front Line
  (26/1000-3000), the ship to El (29/0-31/0), Eluria Tower (Claude's game, 33/0-10), the Heraldry
  Weapons Laboratory (53/3000-4000), and while flag 586 (the Fun City arena), 2318 (the Cave of
  Trials' party choice) or 2326 (Fun City's contest) is set. Member-only windows (Dias, Leon, the
  other hero) hold only a member the story set aside, not one newly received.
- **The starting character** (`party_rules` either way): queued first at New Game's first free
  play, before `start.items` (log `starting character Ashton: joins at the first free play and
  leads`), joins through the game's join ("Ashton joined the party"), and becomes the Formation
  leader (config +0x41 = their slot, the front four). Naming the picked game's hero: nobody joins
  (log `... is this game's hero: nobody joins, the hero leads`). Counted as received (never
  refused or overwritten, back after partings and splits; a starting Celine keeps flag 112).
- **Contract corrections** (for the APWorld): only Shingo Forest and the tournament need a member
  who is not received; at Linga, the Front Line, the ship and the Nede Fields the hero stays (or,
  556, the others fight), so their story-only joins are refused like any other: in Rena's game
  Rena goes to the Front Line (439) and lands on El Beach (229) alone when neither Dias nor Leon
  was received (no random encounters on 439-443 / 229-233, no scripted battle there). An excluded
  member can still appear for those fights (Dias, Claude). The port also protects story members'
  slots, not only received ones.
- Unchanged: `party_rules: "story"` without a starting character plays phase 3's rules exactly
  (unit tests `ap_character_items`, `ap_received_members_stay`; smoke `ap_cel_*`).
- Checked: unit tests `ap_v2_party_seed`, `ap_v2_party_open_joins`, `ap_v2_party_slots`,
  `ap_v2_party_windows`, `ap_v2_party_borrow`, `ap_v2_starting_character`; headless (both games):
  map 12's fight with Claude let in, every tournament fight step (Claude's game 4400 / 6200 /
  10200, Rena's 6500 / 8500 / 10200 / 11500 / 12600 / 16500 / 19500), the splits holding received
  members until they are over (468 the tournament and 548 the ship in Claude's game, 444 the
  Front Line and 126 Mars in Rena's game, 193 Linga and 124 / 444 in both), the refusals there
  (Celine at 124, Leon at 230), the starting character at New Game (Claude's game map 7: Ashton;
  Rena's game map 18: Leon), Dias joining Claude's game; smoke `ap_v2_m12`, `ap_v2_t477`,
  `ap_v2_start`, `ap_v2_linga` (docs/handoff/party.md).

#### The hero in slot 1 (what the port does; agent "partybuild", `engine/src/game/ap/ap.cpp`)

The user's decisions D1-D4 (docs/handoff/heroslot.md, 2026-10-26). In a seed with `world_mode:
open_world` or `hub` whose starting character (named, or picked at the character select) is
not the picked game's hero; every other seed (story mode, no starting character, the hero as
the starting character, Archipelago off) keeps the party order exactly as before.

- **D1, the join** (New Game's first free play): the starting member joins through the game's
  join (slot 1), then takes slot 0 and the hero slot 1 (the game's own swap of slots and records,
  as the camp menu's Party Order page makes it), and leads (config +0x41 = 0). Log `heroslot:
  Ashton takes slot 0, Claude slot 1: Ashton, Claude`, then `Ashton leads (Formation leader,
  slot 0)`. The hero keeps fighting, levelling and gaining relation points (the front four).
  Nothing changes before that free play: the opening scenes are written for the hero alone.
- **The order the player leaves** ("the lead" and "the slot-0 member"): at the join, the
  starting member for both; afterwards whoever is in config +0x41's slot and in slot 0 when the
  camp menu closes or a save is made (the sidecar's `leader` / `slot_0`). A player who moves the
  hero back to slot 0, or makes someone else the leader, keeps that order.
- **D3 then D2, at every free play** (and as the camp menu opens): when a scene moved the
  slot-0 member (active, in slots 1-7) they go back to slot 0, the member there taking their
  slot (unless the front four would be left with nobody able to fight); when the leader is
  active in the front four and config +0x41 points elsewhere (a scene's reorder, or a battle
  inside a split re-pointing it, party_validate_before_battle), it points at the leader again.
  Logs `heroslot: Ashton back in slot 0 (from 2), Rena to 2: Ashton, Rena`, `heroslot: Opera leads
  again (Formation leader, slot 0; was 1)`.
- **Not while the party is set aside or split**: a Private Action (script flags 2 / 4, or 746 the
  isolate routine's), a split window keeping everyone away or letting a member in for a fight
  (map 12's Shingo Forest, the tournament, Linga, Mars, the Front Line, the ship, the arenas
  ...), temporary members or a borrowed slot still in the party, the endings (flag 0x2f1), a
  hub story view. Then the story's order stands, and a camp-menu close does not change what is
  remembered; the order comes back at the first free play after.
- **D4**: the save card's title keeps the hero's name and level (save_build reads config +0x45,
  the hero, by id).
- The leader stand-in is unchanged: the walking actor is the hero's whatever the order; config
  +0x41 = 0 now names the starting member, so the stand-in shows them, and the game's own
  fallbacks (the Party Order page's close, party_validate_before_battle: the first active slot)
  land on them too.
- Checked: unit test `ap_heroslot`; headless (docs/handoff/partybuild.md): Claude's opening with
  Ashton (`--map 7` to map 12's first free play) against the build before: the dumps differ only
  by the two members' places and config +0x41, rng and vblank identical; no starting character
  and a story seed: dumps identical; map 12 in Rena's game (Ashton back in slot 0 at map 17),
  Linga with a battle inside the split (Opera leads again), the tournament at 19 / 4400 (dumps
  identical inside its window), a debug battle with [Ashton, Claude] (Ashton controlled, both
  gain the EXP); smoke `ap_hs_m12`, `ap_hs_linga`, `ap_hs_t477`, `ap_hs_save`, `ap_hs_load`.
- Debugging (environment, never config.ini): `SS_HEROSLOT_ORDER=LEAD,FIRST` (party ids) gives a
  `--map` start the order a game in progress remembers, its start (the starting member's join,
  the starting items) counted as given.

#### The bench (what the port does; agent "partybuild", `engine/src/game/ap/bench.h`)

The user's decisions B1-B8 (docs/handoff/bench.md, 2026-10-26), with a separate store the port
owns (the user: "Lets go with a separate store from the start, as then we own the method to
maintain the benched members equipment and settings"). Built on branch `party-bench` (agents
"partybuild" to "partybuild6", 2026-10-26 / 27; docs/handoff/partybuild.md).

**The contract** (what a seed, a save and an older port can count on):
- **The seed.** `options.bench` (true / 1) turns the bench on, read only with `party_rules:
  "open"` in open world and in the hub; anywhere else it reads as off. A seed with it is
  **version 4** (open world and hub alike; "The version rule"): a port that reads 1 to 3
  refuses it instead of playing a pool of up to 12 members with 8 places. Without it the seed's
  version is what its other fields need, and the game is the 8-slot game, byte for byte.
- **The heroslot keys** (`leader`, `slot_0`: "The save sidecar"; any open-world or hub seed with
  a starting member who is not the hero, bench or not): written only when set, no version
  change; an older port ignores them and keeps the card's order.
- **The bench keys.** `bench` (the occupied places 8-11, 0x130 bytes each), `lent` (members lent
  for a fight and the place each keeps) and `bench_hash` (the save's bytes, the four places and
  the loans) are written only while someone is benched or lent. **The sidecar version is 3
  exactly then**, else 2 (with `save_hash`): a port that reads sidecars 1 and 2 refuses a save
  with members on the bench instead of loading it without them. A save with an empty bench is
  the 8-slot sidecar. The card never holds a benched member: its 8 slots are always a valid
  party for the console game or an older port (the benched members, and what they wear, are
  only in the sidecar).
- **The guard.** With a seed, `bench_hash` must match the save, the bench and the loans
  (edited, stripped or moved here: refused like an edited save); stripping the bench keys
  altogether only loses the benched members.
- **What is not stored:** nothing per character id changes (relations, names, flags); the
  stash's `by` ("The party rules") is the slot borrowing's, bench or not.

- **`options.bench`** (true / 1; seed version 4): read only with `party_rules: "open"` in open
  world, and in the hub (where the party rules are always open); story mode and the story's
  party rules keep 8 members and their lockouts, and there the key reads as off.
- **What the APWorld writes** (so2.apworld 0.2.1, agent "benchworld"; docs/ap/apworld.md "The
  bench"): its `bench` option is on by default, and applies exactly where the port reads it
  (open world with character_items, and hub mode). There the seed has `options.bench: true`
  (slot data `bench` too) and version 4; `characters` is every member not in
  `exclude_characters` but the story members and the starting character (no random choice, no
  healer swap); a `starting_character` list (`"any"`) is every member not excluded. Elsewhere,
  or with the option off, `bench` is absent and the seed is what it was before the option.
- **The store.** Four bench places, 8-11, after the game's 8 slots (0-3 the front, 4-7 the
  reserve). A member is what the game's own Party Order swap moves: the party slot (0x60 bytes)
  and the character record (0xd0 bytes), so a benched member keeps everything (equipment,
  skills, killer moves and their use counts, talents, tactics, level, EXP, HP / MP, status).
  The game's blocks stay 8 places: no original code sees the bench, and the memory card's 8
  slots stay a valid party for the original game or an older port (without the benched members
  and what they wear). The bench is in the sidecar (`bench`, `bench_hash` above).
- **Joins.** A received member (or the story's join of a member the port keeps) with all 8
  slots taken goes to the first free bench place before anything else (before the reserve slot
  of a member the port does not keep; with the bench full they wait as before). The member is
  built as the game's join builds one (its starting data straight into the place, the join
  level of the median over the 8 slots, skillsanity's pass, no Link Combo partner). Banner
  `Noel joined the party (bench)`; log `[ap] Noel joins the bench (place 8): Ashton, Claude, ...
  | bench: Noel`. Never the hero, never a temporary member. With room in the 8 a member joins
  there as before.
- **A benched member is in the party** for the port: a later join of theirs adds nobody, and
  the story's own join of a benched member is skipped (B4: they stay benched; log `join Noel at
  map 277 ...: on the bench: stays there`); the story's leaves find no slot. A received move
  (skillsanity) is learned in the benched record, and the killer-move scan reads its use counts.
- **Loans** (B4: "lent only for solo fights"). The split windows that let a member in for a fight
  they make alone (Claude in Shingo Forest, map 12, in Rena's game; Dias's fights and, in Rena's
  game, Claude's at the tournament, maps 468-477) lend a benched member instead of skipping
  their join: their own bytes (not a new join's) go into the slot the game's join would give a
  new member (the first slot not active: an empty one, or a reserve one whose member moves to the
  next empty slot), else a reserve slot of a member the port does not keep, else the first
  reserve slot borrowed (its member's bytes stashed, as for a temporary member, with `by` the
  lent member). Their bench place stays theirs, empty: no join takes it. Log `[ap] join Claude at
  map 12 ...: lent from the bench (place 8, level 17, EXP 8677) for Shingo Forest (map 12), slot
  1: Ashton, Claude, (Rena)`. They go back to that place with what the fight gave them (EXP, HP,
  status; the Link Combo bond cleared, as Party Order clears it behind the front four) at the
  first free play where the story has set them aside or no window needs them, or at once when
  the story's join puts back the member whose slot they borrowed (the borrowed slot first goes
  back to them wherever a scene moved them, as for a temporary member); their slot is empty
  again, or holds its own member again. Log `[ap] Claude back on the bench (place 8, level 17,
  EXP 8682): the window that needed them is over`. Inside the window a join of theirs (a fight
  again) is the game's join; outside it the story's join adds nobody (`... lent from the bench:
  back to it at the next free play`). Party Order and the order memory (heroslot) are locked
  while a loan is out. A save keeps the loan (`lent` above).
- **The story's own party moves never see the bench** (the user, 2026-10-27: Private Actions read
  the party and the reserves, slots 0-7, never the bench). No benched member comes into slots 0-7
  inside a Private Action (B 0x29 / 0x2a): no loan then (`... on the bench: stays there (a
  Private Action: no loan)`), and Party Order's bench moves are refused. Nor inside a saved party
  order (A 0x16 / 0x17) with free play in it: the arena (305 -> 304), the tournament (468 ->
  469), Fun City's contest (688) and the Cave of Trials (704) lie in windows that lock the bench;
  map 479's pair is inside one scene. The tournament's loans are the story's own joins of its
  fighter there (as the original's temporary Dias): no saved member leaves for the bench, so A
  0x17 finds every one it can. A loan never overwrites or stashes the slot of a member who
  borrowed one while that stash waits; with no other slot the member stays benched (`... needed
  for the tournament ... but there is no slot (none free, none of a member it may take): stays on
  the bench`). Party Order is locked while any slot is borrowed.
- **Inns, heals and the endings see the 8 slots only** (B8 no, B5 none for now): the game's heal
  routines (party_restore_hp_all / _mp_all: inns, scripts) and the endings' "in the party" test
  (script ext 0xb1, party_find_slot) read the 8 slots, so a benched member is neither healed nor
  counted for an ending, with no port code.
- **Party Order** (camp menu > Combat > Replacement; B1, the user's pick: Option A of
  scratchpad/bench/shots/mock_party_order.png). With a bench seed the page keeps its two
  columns of four: the left one is always the front four (slots 0-3); the right one shows the
  reserve (slots 4-7) or, after L1 / R1 (the menus' page buttons, config buttons 4 / 5), the
  bench (places 8-11). The help bar says which, where the game's logo goes on other pages
  (`L1/R1: Reserve 5-8` / `L1/R1: Bench 9-12`, the buttons named as the config has them). A pick
  is one of the 12 places, so a first pick survives the flip: any two places change places,
  the reserve and the bench included. The held pick's hand hides while its column is off
  screen, and the help bar's second line names who is held (`Held: Precis`); with the bench
  locked it says `Bench locked`. While open, the page works on a copy of the 8 slots and the
  bench (every box reads the copy); closing it writes the copy back and then does what the
  original's close does (a new front member's voices; the Link Combo bonds of everyone behind
  the front four, the bench included; the leader's slot if it emptied). Log `[ap] Party Order
  closed: Ashton, Claude, ... | bench: Opera`. The bench members' sprites, which the field did
  not load (its packs follow the 8 slots at the map's load), are read from the disc for the
  camp menu (the same pack files; stderr `[menu] bench: member sprites 0x600 from the disc`).
  Without a bench seed the original page opens, untouched (L1 / R1 do nothing there).
- **Refused moves** (sfx 7, logged `[ap] Party Order: Claude <-> an empty bench place refused:
  the hero never goes to the bench`): the hero to the bench; a member set aside (a negative
  slot id) with a bench place either way; a temporary member to the bench; a benched member
  into the 8 while a split window keeps them away (Leon away, Dias away); and any move to or
  from the bench while the party is the story's (locked: a Private Action, flags 2 / 4 / 746, a
  split window keeping everyone away or letting someone in for a fight, temporary members, a
  borrowed slot, the endings, a hub story view). As on the original page, a swap that leaves no
  front member able to fight is undone. Swaps inside the 8, and inside the bench, are as today.
- **The camp menu's other pages** (B2: benched members there too). With a bench seed the camp
  menu's member list has 12 rows, the bench's four after the 8 (an empty place is a blank row,
  as an empty slot is). Equipment, Skills, Status and Specialty open on a benched member, and
  L2 / R2 go through all 12. The pages work on the bench place itself (the camp menu points the
  selected-member accessors at it while it runs; nothing is copied into the 8 slots), so what
  the player changes stays with the benched member and is saved with the bench. Tactics flips
  its right column as Party Order does (L1 / R1: the reserve or the bench), and a benched
  member's box opens their orders. The camp menu's stat pass (the figures derived from level
  and equipment that Equipment and Status show) runs over the bench too.
- **Benched members are managed, not used** (the user, 2026-10-27: "They can be there, they can
  be brought in, but they are not there without explicitly bringing them in to be used."). A
  benched member's own settings can be changed: equipment, skills (with their own SP; battle
  skills on / off), Tactics orders, killer moves in their slots, which spells they use in
  battle (on / off), and their name (Status). They do nothing in the game from the bench: on
  the Specialty page their field specialties (Oracle, Musical Talent, Practice, Scout,
  Familiar) are greyed and refused, their spells are not marked castable and a cast is
  refused, and the Link Combo button is gone (a partner is one of the front four, and choosing
  one can end the front members' own links); each refusal is the menu's refusal sound, logged
  as `[ap] bench: Noel is on the bench: specialty 9 refused`. The help bar says so where the
  game's logo goes: "Benched: can't use" on the list, "Benched / no cast" beside the spells'
  ON / OFF button. What involves the party stays with the 8 slots, as for anyone outside them:
  spell targets, item use and Item Creation, super specialties (a benched member picked earlier
  in the menu is not the one Items uses: the first of the 8 is), and the Equipment Wizard at
  the menu's close (a benched member's equipment stays as they left it). Killer-move voices
  load when the member comes into the front four.
- Checked (2026-10-26): a save with Noel and Ernest benched, Precis (slot 5) to place 9 across
  the flip and Noel (place 8) to slot 3: every member's 0x130 bytes moved as they were, the
  state, config and inventory identical to a control run that cancelled the same picks (the
  game's stat pass recomputes a member's derived figures, slot +0x2c..+0x4c and record +0x2..
  +0xa, whenever the camp menu reopens with them in the 8); no seed: the page, its captures and
  the state dumps byte-identical to the baseline build, L1 / R1 pressed. Unit test
  `ap_bench_swap_rules`; smoke `ap_bench_swap`.
- Checked (2026-10-27, the other pages): the same save; Noel (place 8) through the member list's
  ninth row into Equipment (the picker's figures for him; his weapon off), L2 / R2 to Ernest,
  Status, Specialty, Skills, then Tactics on the bench page (his next orders), and a save: in
  the sidecar only Noel's weapon, orders and Equipment Wizard bit changed, plus the derived
  figures of both (the stat pass filled in those a bench join leaves at zero). Unit test
  `ap_bench_camp_pages` (the accessors on a bench place; the stat pass gives a benched member
  the bytes the same member gets in a slot; unbound, the store is left alone); smoke
  `ap_bench_camp` (benched Opera's weapon and orders, the save, its reload).
- Checked (2026-10-27, managed, not used): the save with Noel benched, the guild set with Danger
  Sense opened (script flag 705); Party Order sends Rena to the bench; benched Noel learns
  Danger Sense with his own SP (290 -> 288; Scout comes with it); his Scout is refused (the
  piece-before build opened Scout's dialog at the same input); R2 to Rena: her Heal is refused
  and switched off for battle; the save: Noel's SP, skill and the stamina it gives, Rena's
  Heal off, nobody's MP spent. Unit test `ap_bench_specialty_uses` (the rule for a slot, a
  reserve slot, a bench place, unbound); smoke `ap_bench_use`.
- Checked (2026-10-27, loans; agent "partybuild6"): map 12 in Rena's game with Claude benched
  (debugging: `SS_BENCH=1` puts him there before the map's own scene) and all 8 slots taken, a
  real battle: Claude lent into Rena's slot (her bytes stashed) at level 17, EXP 8677; special 1
  moves him to slot 0; he fights the monster alone and wins (encounter 190); Rena's join puts him
  back in the slot he borrowed first, Ashton to slot 0, and Claude on the bench at EXP 8679; at
  map 17 all 8 are there (the build before the borrowed-slot fix had lost Ashton here). With room
  in the 8 (Ashton, Rena) he is lent into Rena's slot (she moves to the next empty one) and goes
  back at the first free play, EXP 8677 -> 8682. The tournament (477, Claude's game, all 8
  taken): Dias lent into Claude's slot for his fight, the status line names the loan, and
  Claude's join after it puts Claude back and Dias on the bench. Not reached headless: a free
  play inside the tournament (map 476 then holds a menu; the unit tests play that sequence).
  Unit tests `ap_bench_loans` (both scenes with and without room, a second fight, the sidecar
  and its guard, inns and ext 0xb1 on the 8 only) and `ap_bench_loan_rules` (no loan in a
  Private Action; Party Order locked in a Private Action and in the windows around the
  party-order saves; a loan never takes a borrower's slot; the tournament with heroslot, A 0x16 /
  0x15 / 0x17: nobody lost); smoke `ap_bench_m12`, `ap_bench_t477`.
- The tournament's kept Dias with the bench (`tournament_dias` above, port 0.02.51; agent
  "benchmerge", 2026-10-27). A benched Dias is lent into the 8 for each of his fights, so the
  specials' hooks find him there: his slot is saved before 0xc and his record before the first
  0xd (until then the record is his own: an Equipment change on the bench between fights
  counts), and right after his last fight his own data go back into his lent slot, which the
  loan then takes back to his bench place. A copy still held once the tournament is over while
  he is on the bench (not seen) is put on his bench place at the next free play (the bench's id
  word kept, the record only when held, its Link Combo bond cleared as for any benched member);
  that runs before the loans return. Unit test `ap_tournament_dias` (a lent Dias for 4400 / 6200
  / 10200, back on the bench as he was; a save between fights with `bench` and
  `tournament_dias`, each with its hash; the safety net on the bench, slot alone and slot and
  record).

#### The leader stand-in (what the port does; agent "leader", `engine/src/game/field/leader.h`)

In every seed with `world_mode: open_world` or `hub` (any version; story mode and games without a
seed never; the starting character is only the usual way the leader gets set), on field maps:

- **Sprite.** The walking actor shows the Formation leader (config +0x41, the camp menu's
  Combat > Formation > Leader, the member the player controls in battle) whenever that slot
  holds an active member who is not the hero. Changing the leader in the camp menu shows at
  once when the menu closes. The actor stays the hero's for everything else: slot G[0xc4],
  model, animation state (the frame, timer and loop counts the scripts wait on), position,
  collision; only the drawn sprite changes (the emote above the head and the default message
  row follow the drawn sprite). Running shows the stand-in's walk at the run's pace (only the
  heroes have run sprites). A stand-in whose sprite this map's party packs lack (joined or made
  leader after the map loaded) is drawn from their own pack, read on the side from the disc.
- **Names.** In the map scripts' messages, the hero's literal name token (`{name:00}` in
  Claude's game, `{name:01}` in Rena's) reads as the leader's name, in the text and in the
  speaker labels ("Ronixis: Ashton. You've made it to Ensign."). The other hero's token stays
  theirs; name variables (`0x4007`: "X learned ...") are never changed; Archipelago messages
  ("Got X") and the harbour menu are the port's own text.
- **The hero instead** (sprite and names together, so a box never pairs one with the other):
  on world maps (the members' world sprites have 4 of the 21 animations); when the leader is
  the hero or not active (a Private Action keeps only the hero); when a script gave the walking
  actor another sprite (the bunny, a disguise); and **for the rest of a scene** once
  - the stand-in's own actor exists (the scene shows them as themselves: the two would show),
  - a message names the stand-in (their own name token, or a name variable holding them), from
    that message on, so it never reads "Ashton: ... Ashton,",
  - the walking actor is asked for a scene animation (one of the scene-only gestures, beyond
    the 21 walking ones), which only the hero has.
  The scene ends when free play resumes (the player has the controls back: no event or talk
  running, no fade or window up). A scene that runs through several maps keeps the hero
  throughout.
- **Presentation only.** Flags, chapter, party, inventory and the random generator are the same
  with and without it (headless proofs: Claude's opening with `starting_character: Ashton`
  through 14 rewritten messages and a latched scene; the late save's walk into the final scene;
  Rena's game's Arlia events; byte-identical state dumps and rng signatures;
  docs/handoff/leader.md). Name lengths change
  how long the typewriter takes over a line, as renaming the hero does in the original.
- **The hub** (agent "hubleader", 2026-10-25; the user's ruling: hub mode as well, D7): the same
  rule everywhere, the boss chambers' original scenes (played through their story views)
  included: the stand-in walks into a chamber and the scene names them where it names the hero,
  until one of the latches above puts the hero back for the rest of the scene. The rule reads the
  real party and the real flag 1 (the hero), never the view's. Headless (Ashton as the starting
  character; tools/ap_hub_chambers.py `--starting-character`): Alen-Tax 61 (Ashton walks in,
  "Ashton" in 5 lines and their speaker labels, then Claude's own scene animation latches Claude
  for the rest), Bark 454 (the scene shows Ashton's own actor: Claude for the scene), 14 more
  chambers of both games; state dumps byte-identical with `SS_LEADER_STANDIN=0`; hub New Game with
  a named member and with the character select's pick (Fun City in the leader's place, the world
  map with the hero), a save and its load (docs/handoff/hubleader.md).
- Debugging (environment, never config.ini): `SS_LEADER_STANDIN=0` off, `=1` on without an
  open-world or hub seed; `SS_LEADER_SLOT=N` party slot N as the leader (presentation only);
  `SS_LEADER_TRACE=1` logs what the walking actor shows and why.

### Craftsanity (`craft` locations) and `no_fail_crafting`

| `check.type` | Fields | Id |
|---|---|---|
| `craft` | `item`: the game item number (1-1023) the creation must produce | `0x50320000 + (11 << 21) + item` |

- **Where:** `CreationPage::produce()` (engine/src/game/field/menu_creation.cpp), after
  `inventory_give(made_, 0)`, for the item-making specialties only: page 0xf variants 1 Art,
  4 Customize, 6 Metalwork, 7 Authoring, 10 Compounding, 11 Cooking, 13 Alchemy, 17 Machinery,
  and page 0x10 (super) variants 1 Master Chef, 5 Publishing, 7 Blacksmith, 8 Reverse Side.
  Not Identify (5), Survival (14) or Reproduction (16), which hand back or copy items.
- **What:** the check of item `common_kind(made_)` (a result that came out as its rare kind, an
  unidentified 0x338+ item or the rare item, counts as its common item), when the seed lists a
  `craft` location for it that is not checked yet. Success or failure alike (a failure item is
  a check only if the seed lists it; 0.2.0 lists results only).
- **The crafted item stays the player's** (the user, 2026-10-25): the creation gives it as
  usual; the location's item comes on top (given in free play, "Got X", or sent, "Sent X to
  Y"). Craft locations never hold progression.
- Sidecar: nothing new (`checked` records them).
- **`no_fail_crafting: true`:** in `CreationPage::decide()` after `compute()`, the attempt
  succeeds (`success_ = 1`) whenever the specialty picked a result (`good_item_ != 0`). A member
  without the specialty's talents still counts as level 0 for which result compute() picks
  (the original's rule), but no longer fails. The 30 % rare-kind swap is unchanged. The port
  decides structural non-results (Customize's result equal to its input stays a failure:
  nothing would change).

### Killer-move checks (`killer_move` locations)

| `check.type` | Fields | Id |
|---|---|---|
| `killer_move` | `character` (party id 1-12), `move` (1-15, the id's 4 bits; 0.2.0 uses 1-11, Claude's Mirror Slice being 11: the learn tables' move number, battle/result.cpp `learn_from_table`: learned flag at record + 0x3c + move - 1, use count at record + 0x8a + 2 * move), `level` (for display), `uses` (the use count that reaches the level) | `0x50320000 + (12 << 21) + (character << 14) + (move << 10) + level` |

- **When:** the member's use count of that move (record + 0x8a + 2 * move, the count the
  original's `pc_begin_action` raises by 1 each time the move starts, at most 999) is at least
  `uses`. The count changes only in battle, so the port may test after each battle (and on
  load); a count already past `uses` (an old save) checks at once.
- `uses` = level x the move's divisor in the armed table (scratchpad/killer_prof/move_levels.csv).
  The port compares the raw count, so the bare-handed tables of Claude, Dias and Ashton (other
  divisors for three moves) don't matter.
- 0.2.0 lists the 69 learn-table moves: every level of a move with at most 3 (61 moves, 112
  locations), and for the 7 that count in fine steps the levels 100, 200 and 300 uses reach
  (21). Ashton's Dead Triangle has none; the 8 moves without a learn-table row (Phase Gun,
  Precis's three, Opera's three, Ashton's Tri-Ace) aren't listed.
- The move's level-ups themselves (Claude's upgraded data at 100 / 200 uses...) are the game's.

### Enemy drops

Nothing new for the port: `drop` checks are phase 3's. `drop_progression` and
`area_keys_in_drops` only change what the APWorld puts there (docs/ap/apworld.md, "Logic").

### The port: goals, doors, the trumpet, crafting, killer moves (implemented 2026-10-25)

The hooks agent's part of wave 2 (log docs/handoff/hooks.md). Code: game/ap/seed.cpp
(reading), game/ap/ap.cpp (goal_won, door conditions, sell_locked, on_craft,
scan_killer_moves), field/menu_shop.cpp, field/menu_creation.cpp; tests
engine/tests/test_ap.cpp `ap_v2_*`; smoke cases `ap_v2_*`, `auto_wizard`.

- **Loading.** `goal.all` / `goal.flags` must be arrays (flags 0-2943, encounters >= 0); a
  `craft` needs `item` 1-1023; a `killer_move` needs `character` (1-12 or a name), `move`
  1-15 (the id's 4 bits; Claude's Mirror Slice is move 11) and `uses` 1-999 (`level` optional); a door's `flags` / `won` / `banners` must be
  arrays, and a door with `flags` or `won` must name its `routine`.
- **goal_won.** A victory (outcome 0) in an encounter of `goal.all`, or of any door's `won`,
  goes in the sidecar's `goal_won` once (log `goal: encounter 561 won (goal_won 1, goal.all
  2)`). When every encounter of `goal.all` is in it the goal is set and sent (once; again on
  every connect, as before). A load with the same seed merges the save's list and this
  session's (save's first) and tests `goal.all` again; a save without a sidecar starts empty.
  Without `goal.all` the any-of rule is unchanged and nothing goes in `goal_won`.
- **goal.flags** are set at the first field frame after New Game, after every load (sidecar or
  not), and after the seed loads (a start straight into the field, `--map`): log `goal flags
  set (2)`.
- **Door conditions** lock the trigger (`door_blocked_at_trigger`) and a transfer reached
  another way (`on_map_transfer`) in every world mode, with or without keys. Flags are read
  live (a flag cleared again shuts the door again). The banner is the first missing
  condition's text (after a missing key's banners when the door has keys too); an empty one
  shows "The way is closed for now". The text is not measured at load (no font yet); one wider
  than 236 px shows anyway and is logged (`banner "..." is 248 px, over the banner's 236`).
  Log: `door 43978493 (Indalecio (Limiter Off)): locked, needs flag 556 ("Talk to Filia in
  Central City first")`.
- **The Silver Trumpet:** shops' `add_inventory` (every sell list but shops 0x39 / 0x58, which
  buy only items 0xc4-0xdb) skips item 48 while flag 53 is clear in a seed whose `goal.all`
  holds 561 (log `the Silver Trumpet stays off the sell list ...`). With no other sellable
  item the shop's Sell command has nothing to list.
- **Craft checks** come after the creation's own give, for the 12 item-making specialties
  (the page's variant and super flag): log `craft: item 344 made (success)`, then the check;
  another player's item gets the "Sent X to Y" banner, our own is given at the next free play
  ("Got X"). Compounding and Machinery have no failure item: their failure makes nothing and
  checks nothing.
- **no_fail_crafting.** In `decide()` after `compute()`: `success_ = 1` when `good_item_ != 0`,
  except a structural non-result (port field `no_result_`): Customize's result equal to its
  input, an Alchemy entry with divisor 0, a Survival pick with a zero chance at this kind of
  place. **Art** (specialty 1) picks its result only after a successful roll (`good_item_` is
  0 on a failure), so `Creation1::compute` makes its roll a success under the option. A
  Customize recipe refused by chance (the item's worth too low) gives the tier item instead.
  Talent-less members still pick at level 0. Super specialties 2 and 4 (their own decide) and
  3 / 6 (no decide) are untouched; Identify, Survival and Reproduction go through decide()
  too, so the option makes them succeed as well (they are no craft checks).
- **Killer moves:** after every battle exit and every load (and once after the seed loads),
  at the next free play, each unchecked `killer_move` location whose member is in a party slot
  (active or in reserve) is compared: `record[slot] + 0x8a + 2 * move >= uses` (log `killer
  move: Claude move 3 used 190 times (level 1 at 100)`). A member not in the party is skipped
  until they are. The same record counters also count a spellcaster's spells
  (pc_cast_spell), so only fighters' moves belong in the seed (as 0.2.0 lists them).
- **Checked headless** (scratchpad/hooks/runs): the gate at map 671, chapter 60 (refused
  without flag 556, then without 561; console `battle 561` with `--battle-end 60:0`; then map
  684 loads); `goal_won` written to the sidecar, restored by a load (the door opens) and empty
  after loading the same card without its sidecar; the sell list in Hearn's shop with and
  without flag 53; the menu agent's creation plans on card spec2 with and without no-fail;
  killer moves on a modified spec2 save (counts past on load; a count rising in a battle).

### The join level (open world and hub; port behaviour, agent "skillsanity2", 2026-10-25)

The user's ruling (docs/handoff/skillsanity.md): in every seed with `world_mode` `open_world` or
`hub` (any version: no seed field), a member who joins takes the median level of the party. Story
mode, no seed and Archipelago off keep the original join levels.
- **When:** every new member's starting data (the game's `party_slot_init`: a script's join, a
  received or starting character's, a member let in for a while, the character select's pick),
  and special 0xd's re-init of Dias. A member coming back from the bench or from being set aside
  keeps their data. Code: `engine/src/game/ap/skills.cpp` `on_member_init`, from field
  `vm_svc_party_slot_init`.
- **The target:** the median level (slot +0x28) of the other members in the party's 8 slots (ids
  1-12; anyone set aside, a negative id, not counted), an even count's middle two averaged and
  rounded down, clamped to 1-255. Nobody else: their own level. So a starting character joins at
  the hero's level (1 at New Game).
- **Up:** the EXP the target level starts at, then the game's level-up routine
  (battle/result.cpp `levelup_apply`: HP, MP, SP and the six attributes, random as in a battle)
  and the learn tables' moves up to the target (`levelup_learn_skills`).
- **Down** (the user's option 1; the gains are random and a recruit's join stats are authored, so
  there is no exact inverse): the mean gain of each level from the target to their own taken off
  (HP L*3/2 + 32 below level 50, 94 from it; MP 5; STR, INT, DEX, CON, slot +0x48 and record +6
  by the growth table #1981 0x80087f94: base + (range - 1) / 2), each never below the lower of
  Claude's and Rena's level-1 values (HP 100, MP 20, STR 5, INT 0, DEX 10, CON 2, slot +0x48 20,
  record +6 10) and never raised by it; level and EXP as the target's; the learn tables' moves
  above the target forgotten (they come back with the levels). SP and equipment as they join.
- Logs: `[ap] join level: Ashton joins at level 1 (the party's median; their own 17): down, HP
  1000 -> 288, 2 moves known`; `... joins at level 220 ...: up, HP 2500 -> 9999, SP 999, 10 moves
  known`.

### Skillsanity (`skillsanity`; agent "skillsanity2", 2026-10-25)

The user's design (docs/handoff/skillsanity.md). `options.skillsanity`: `none` (the original),
`skills_only`, `special_only` or `insanity` (both).

**Skills** (`skills_only`, `insanity`):
- **Item** `{"kind": "skill", "skill": N, "name": "Skill: Perseverance"}`, N 1-46 (the skill ids
  of docs/notes/menu.md "Skill guilds"; AP id ITEM_BASE + 0x60000 + N). Received, the party may
  learn the skill with SP as buying its set made it learnable (field `skill_book_allowed` answers
  from the received skills instead of the sets' flags; the skill books too). The sidecar's
  `skills` keeps them. Starting skills stay the members'.
- **Check** `guild` `{set, index}`: set 0-11 (flag 700 + set), index the skill's place in the set.
  Buying the set at any of its guilds (the purchase routine sets the flag) records all its checks
  (`[ap] skillsanity: guild set 4 bought (flag 704): 3 checks`); our own items come at the next free
  play ("Got X"), another player's are sent. The flag stays set (the guild says "you already know
  that skill"), but opens nothing. Id ITEM_BASE + (13 << 21) + (set << 8) + index.
- **Float** (the Cave of Trials Level 11 prize, special 0x12): check `learn` `{"skill": 46}` (id
  ITEM_BASE + (14 << 21) + 46): the prize records it and teaches nothing.
- Hub seeds clear flags 700-711 (hub_state.json, whatever skillsanity says): the guilds sell.

**Killer moves and spells** (`special_only`, `insanity`):
- **Item** `{"kind": "move", "character": C, "index": I, "name": "Claude: Shooting Stars"}`: C the
  party id, I the record +0x3c index (a killer move's number - 1; a spell's place in the class's
  list). AP id ITEM_BASE + 0x70000 + (C << 8) + I. Received, the member knows it at once if in the
  party (any slot), else from their join (the sidecar's `moves`, character * 256 + index).
- **Check** `learn` `{character, index, level?}` (`level` for display): the learn point of that
  move. The port treats every learn point the seed lists as its check and does not teach the move:
  the learn tables at a level-up (battle/result.cpp), script ext 0x73 (the Cave of Trials' learn
  chests, the arena's Private Action), the items 464-469 (item effect `character_flag`: the first
  use records it and is used up; later uses do nothing, the item kept). A learn point the seed
  doesn't list teaches as the original. Id ITEM_BASE + (14 << 21) + (C << 8) + I.
- **The first move:** at a join (and for the heroes at New Game's first free play) the member
  knows their learn table's first move whatever their level; every listed learn point is forgotten
  unless its move was received, and those up to the member's level are checked at once (`[ap]
  skillsanity: Celine at level 220: 19 learn points checked, ...`). The Cave of Trials' learn
  chests' scene still says "learned"; the move comes from its item.

**Shops:** a `shop` location may carry `"price": N` when its item is this world's own `skill` /
`move` item. While its check is open the slot sells that item as itself: the list shows its name
(a move's "Member: " left out when the name would run under the mark and the price) with the
Archipelago mark and the price, at most one, no game item behind it (no held count, no item
viewer); buying it is the check (the item given). Once checked the entry is the shop's own again.
`engine/src/game/field/menu_shop.cpp`, `ap::shop_ap_item`.

### The hell seed (`hell_seed`, the arena's fee; so2.apworld 0.2.1) [hellseed]

A **hell seed** (the user, 2026-10-27: "usually called a "hell" seed, where the grind is
intentionally made preposterously hard"; docs/ap/apworld.md "The hell seed") is one with
`encounter_rate` 0 and `killer_move_checks` on or `skillsanity` `special_only` / `insanity` (learn
points). With no random battles, its only battles to train in are the Fun City arena's, so the
APWorld writes two things on it, and on no other seed:
- **The fee's patch line** (an ordinary `patches` line, version 1): `script 297 0x177f 0x120007d0
  0x12000000`. Map 297 (the Fighting Arena's desk), routine 0x5c20: `push 0x7d0` (2000 Fol, then
  Bargaining's +/- 3 % a level) at 0x5dfc becomes `push 0`: "An entry fee of 0 FOL", the Fol test
  never fails, nothing is taken. The word is the same on NA, Europe, France and Germany
  (docs/handoff/hellseed.md "Fee proofs": 7000 Fol after registering without the line, 9000 with
  it, on each). Every arena fight is an ordinary battle: killer-move uses count, the fighter gets
  the EXP and the party the Fol.
- **`options.hell_seed: true`** (slot data too). Display only: the port plays the seed exactly as
  without it (the fee comes from the patch line). Setup's Archipelago page shows a warning under
  the seed's line, and the game logs the same line when it loads the seed (`[ap] Hell Seed: no
  random battles. Killer-move levels and learn points are trained in the Fun City arena, which is
  free on this seed. Expect a very long grind.`; `game::ap::kHellSeedWarning`, seed.h). Absent:
  no warning.

No new version: the patch line is a version-1 feature and `hell_seed` changes nothing a port does,
and a hell seed is version 2 or 3 anyway (killer-move checks, skillsanity, hub). An older port
plays it the same, without the warning.

### Example (0.2.0, open world, version 2)

```json
{
 "format": "apso2", "version": 2,
 "options": {"world_mode": "open_world", "character_items": true, "party_rules": "open",
             "story_route": "claude", "starting_character": "Ashton",
             "characters": ["Rena", "Dias", "Leon", "Opera", "Noel", "Chisato"],
             "exclude_characters": ["Bowman"], "craftsanity": true, "no_fail_crafting": false,
             "goal": "indalecio_limiter_off", "drop_progression": "allow",
             "area_keys_in_drops": true, "apworld_version": "0.2.0"},
 "goal": {"encounters": [540], "flags": [445, 446]},
 "locations": [
  {"id": 1368523063, "name": "Cooking: Fried Rice", "check": {"type": "craft", "item": 311},
   "item": {"player": 2, "player_name": "Alice", "name": "Map Width", "ap_id": 81001,
            "classification": "useful"}}
 ],
 "doors": [{"id": 43978493, "routine": "0x3bec", "flags": [556],
            "banners": ["Talk to Filia in Central City first"], "name": "Indalecio (Limiter Off)"}]
}
```

## Hub (`world_mode: hub`)

Written by the agent "hubdata" (log `docs/handoff/hubdata.md`), agreed with the port agent
"hubvm" (log `docs/handoff/hubvm.md`, whose proposal the `bosses` rows follow). The design and
the user's decisions: `docs/handoff/hub.md` (D1-D10). The data: `docs/ap/hub_state.json`
(`tools/ap_hub_state.py`) and `docs/ap/hub_bosses.json` (`tools/ap_hub_bosses.py`); the APWorld
copies their rows into the seed.

A hub seed starts New Game after the Final Save Point (chapter 60, flag 730, disc 2) with every
location's done flags cleared; both worlds are open (the Synard on Energy Nede, the Fun City
arena's old man / Arlia's guide and the Synard on Virtual Expel); towns are open and each
dungeon has one key; each story boss plays its original scene in its chamber through a story
view until it is beaten. Without `world_mode: hub` none of this exists.

### The version and the options

- `version`: **3**. A wave-2 port refuses it ("unsupported version 3 (this build reads 1 and
  2)") instead of playing a hub seed as a story game. A hub seed with `options.bench` is
  version 4 ("The bench" under Party v2).
- `options.world_mode`: `"hub"` (or 2). The chapter lock, the story views, the hub New Game and
  the dungeon keys run only then.
- Reference only (the port reads the blocks below): `options.hub_start` (`fun_city`, `arlia`,
  `random_town`: the APWorld picks the concrete `hub.start`, any of hub_state.json's 16 `starts`;
  `options.hub_start_town` names it), `options.fienal_requirement`
  (`fields_and_berle`, `fields`, `all_bosses`: written as the gate door's `won`, below).
- `party_rules` is `"open"` and characters are items (D7); `starting_character` works as in
  open world, the leader stand-in included (below, "The leader stand-in": the chambers' scenes
  too).

### The `hub` block (the state New Game builds)

```json
"hub": {
 "chapter": 60, "step": 4000, "disc": 2,
 "flags": [3, 5, 6, 11, 17, ...],
 "globals": {"0xc4": "0x20100", "0x1bc": "0x270f", "0x30c": "0xc809", ...},
 "games": {"claude": {"flags": [1], "globals": {"0xc4": "0x20100"}},
           "rena":   {"flags": [],  "globals": {"0xc4": "0x20001"}}},
 "start": {"name": "Fun City", "map": 279, "x": 470, "y": 0, "z": 68, "dir": 180},
 "lock": {"chapter": true, "step": true, "step_maps": [685]}
}
```

| Key | Meaning |
|---|---|
| `chapter`, `step` | G[0xa0] / G[0xa4] (game_state +0 / +4) at New Game: 60 and 4000 (why 4000: hub_state.json `step_reason`) |
| `disc` | 2: the field loads Energy Nede's and Fienal's maps from disc 2, which also holds Expel's (field.json `disc2`) |
| `flags` | every global script flag set at New Game; all others are clear. Both games; the hero's own flag comes from `games` |
| `globals` | script global words written at New Game: `{"0xADDR": value}`, ADDR 4-aligned, 0xc4 <= ADDR < 0x340 (game_state +0x24 .. +0x29f), 32-bit little-endian. Never the names (G[0xc8]-G[0x1b7]: New Game's own), the message speed (G[0x1b8]: a setting) or the map / position / music words (G[0xa8]-G[0xc3]: the start sets them) |
| `games` | applied after `flags` / `globals` for the hero the player picked (Claude: flag 1 set, game_state +0x24 / +0x25 = 0 / 1; Rena: flag 1 clear, 1 / 0), as the late proof builds Rena's game |
| `start` | where New Game puts the party: field map, position, facing (the arrival of the town's world-map entrance). `name` for the log |
| `lock.chapter` | P2: outside a story view, a script store to G[0xa0] is dropped (log `hub: chapter store N dropped at map M offset X`) |
| `lock.step` | likewise for G[0xa4] (steps 1000-5000 of chapter 60 belong to Fienal's chambers, which play them inside their views; outside them the step stays 4000) |
| `lock.step_maps` | maps where step stores outside a view go through (default `[685]`: the goal fight's aftermath stores step 5000 on the way to the endings) |

- **Order at New Game** (hubcore, P1): the original New Game (hero choice, names, the starting
  party and inventory), then `flags` (all others cleared), `globals`, `games[hero]`, `chapter` /
  `step`, then the transfer to `start` on disc 2; then `start.items`, `goal.flags` and the
  starting character as in open world. Relationship values (config +0x58 / +0xe8), levels,
  inventory and Fol are New Game's: the hub state holds no config data.
- **The Synards.** `flags` holds flag 11 (the Synard); `globals` holds both parking spots
  (table A 0x19 `set_point`): Expel's (game_state +0x26c x, +0x28c y, +0x270 z, +0x27c facing:
  Arlia's own spot, (51209, -150, 17464, 90), which Virtual Arlia's arrival and Arlia 17 / 19
  store) and Energy Nede's (+0x274, +0x290, +0x278, +0x27e: the start town's own spot; Fun City's
  is (81546, -258, 33880, 176), stored by map 279). So the Synard waits next to the start town
  on its world and next to Arlia on Expel (R8: no first trip through the old man needed). A
  start on Expel keeps Nede's spot at Fun City (the arena's old man lands there).
- **Neutral, not the user's save.** hub_state.json lists every flag and word of the late save
  with its category (story, chest opened, gift guard, boss guard, PA / relationship, ending,
  other) and what the hub does with it.

### The `bosses` block (the chambers)

```json
"bosses": [
 {"encounter": 531, "name": "Bark", "map": 454, "maps": [453, 454],
  "view": {"chapter": 55, "step": 0,
           "flags": {"740": 0, "741": 0, "742": 0, "743": 0, "744": 0, "745": 0}, "keep": []},
  "location": 1364328979, "dungeon": "Mihne Cavern", "keys": [18],
  "trigger": [{"kind": "thread_start", "routine": "0x661c", "registered_at": "0x67c0",
               "region": [[124, 131], [198, 129], [184, 119], [135, 122]]}]},
 {"encounter": 527, "name": "Guardian", "map": 642, "maps": [642],
  "view": {"chapter": 42, "step": 0, "flags": {"523": 0, "524": 0, "745": 0}, "keep": [],
           "after_maps": [625]}},
 {"encounter": 194, "name": "Varmillion", "map": 462, "game": "claude",
  "view": {"chapter": 15, "step": 2400, "flags": {"731": 0, "745": 0}, "keep": []}}
]
```
(Rows abridged; docs/ap/hub_bosses.json has them all.)

Read by the port:

| Key | Meaning |
|---|---|
| `encounter` | the boss battle. A victory in it (outcome 0) goes in the sidecar's `goal_won` (the list door `won` conditions read), so "Defeat X first" doors and the Fienal gate need no new sidecar key. The boss is **unbeaten** while its encounter is not in `goal_won` |
| `name` | logs and banners |
| `map`, `maps` | the chamber: while the party is on one of `maps` (default `[map]`) and the boss is unbeaten, the view is on |
| `view` | the story view (absent: no view; a flag-guarded boss whose guard the hub state clears, such as Visseyer at 412, plays its scene at the hub's own state) |
| `view.chapter`, `view.step` | what the chamber's scripts read in G[0xa0] / G[0xa4]; their stores go to the view |
| `view.flags` | `{"n": 0 or 1}`: flags the chamber's scripts see with these values; reads and writes go to the view. Flags not listed read and write the real flags as usual |
| `view.globals` | (optional) script globals redirected the same way: `{"0xADDR": value}` a 32-bit word (ADDR 4-aligned, >= 0xa8; all four bytes), or `{"0xADDR": {"size": 1 or 2, "value": v}}` for a byte / half global (only those bytes; ADDR aligned to its size): prefer it for the floor G[0x1cf] and the like |
| `view.keep` | flags of `view.flags` whose value at the view's end is written to the real flags (the whitelist); everything else stored in the view is dropped |
| `game` | optional: `"claude"` or `"rena"`: the chamber exists only in that hero's game (Varmillion 462 / Azamgil 463) |
| `view.after_maps` | (optional) maps the view goes on into when its scene transfers there from a view map, without turning the view on by themselves (an aftermath through a scene or vision map: the Field of Courage's 625; the Synard Plant's 271; the Field of Intelligence's 555 / 628 / 556 and Rena's vision on Expel's 14; the Field of Power's visions 622 / 27 / 30; the Field of Love's 262 / 310; Mihne's 455; Armlock's 314) |
| `view.until` | (optional) `"free_play"` (default: the view ends at the first free play after the win) or `"transfer"` (it lasts until a map outside `maps` / `after_maps` loads: the Field of Power, whose end scene in 604 is guarded only by flags) |
| `view.cut_to` | (optional) `{map, x, y, z, dir}`: after the win, the scene's first transfer out of the view goes here instead (Berle 575: the scene goes on to 576, which stores chapter 34 and moves to the disc switch's scene map 625; cut_to = the tower's world-map arrival) |
| `view.done_flag` | (optional, a script flag) after the win the view still comes on in its `maps` (marked won) until this REAL flag is set: an end scene the player reaches by a walk after the fight (the Field of Power: 163, set by 604's vision end; leaving 603 between the Guardian and that vision would otherwise run the flag-guarded vision at chapter 60 and strand the party on 622) [hubchambers] |
| `ask` | (optional) the chamber asks first (the user's rulings on Shigeo 281 and the arena 305; below, "The port: the questions"): `{"text", "answers"?, "doors"?, "again"?, "options"?}`. While the boss is unbeaten its view comes on only after a yes; after a no the row is quiet until a world map (`again`) [hubask] |

- **The view ends** at the first map load outside `maps` / `after_maps` (the scene's transfer
  out, or the player walking out before the fight) or at free play after the boss battle was won:
  the field's own free-play rule (no event or talk running, the map's thread not waiting on a
  load, fade, music or choice, the controls unlocked, no fade or window up) held for 10 frames in a
  row (a battle hands the field back mid-scene with the controls on for a frame or two).
  New Game and a load reset it. Party changes (ext 0xae / 0xaf) and disc changes (B 0x6a)
  inside a view are dropped (D7; no scene reaches a disc change before its cut).
- **What a row's view lists** (tools/ap_hub_bosses.py; `view_flags_why` says why per flag).
  `chapter` / `step`: the scene's guard (`chapter <= N` reads as N; Bark has none and gets the
  chapter the story enters Mihne at, 55). `flags`: the flags the view's maps read whose value at
  that chapter differs from the hub's (the latest story scene storing it before that chapter
  decides), and the flags stored in the view that the hub must not keep (story bookkeeping:
  dropped), each at its value at that story point; flag 745 (the scene flag; a chamber's set-up
  fades in only while it is 0) at 0 wherever a scene stores it. Not listed, so written through:
  the bosses' guards and their "won" flags, the Fields' 162-165, key items 66-91, chest and gift
  flags, the item flags 740-744 (every map's set-up recomputes them from the inventory) and
  door-arrival flags (curated `unlist`: the Field of Courage's 523 / 524, set by the door before
  its transfer and cleared by the arrival). `keep`: Arcmene's flag 11 (its 12 is dropped: it
  makes a disc of Nede's world map unwalkable and is clear in the late save). Every row is proven
  headless by tools/ap_hub_chambers.py (both heroes; docs/handoff/hubchambers.md).

Reference only (the port ignores them; the APWorld and the proofs read them):

| Key | Meaning |
|---|---|
| `location` | the boss location's id (`0x50320000 + (9 << 21) + encounter`) |
| `dungeon`, `keys` | the dungeon key group and its key(s) (below) |
| `trigger` | (a list: one per way in) how the scene starts in the view: `kind` `thread_start` (a trigger region: `region` its four corners (x, z), `registered_at` the registration's byte offset), `setup` (the map's set-up code calls it on arrival), `talk` (an actor's talk handler); `routine` the routine started (doors.json's sense) |
| `late` | whether the late state reaches the chamber (false: P5 needs a door override or an arrival in hub mode) |

### The dungeon keys (D4)

Towns are open; each dungeon has one key (`{"kind": "key", "key": N, "name": "Hoffmann Ruins
Key"}`, as phase 4's area keys). The seed's `doors` locks every door into the dungeon with it:
world-map entrances (as in open world) and the field doors from a town into a dungeon (Salva
Drift, the Mars dungeon, Zand's Mansion, the Synard Plant, the Portal of Sealing...), which the
door table already takes (`id` = map * 65536 + offset / 4, `routine` for a trigger region).
hub_bosses.json `dungeons` lists each key group with its doors (on the late state's edges),
chambers and chests: 20 groups, Expel 11 (Salva Drift, Cross Cave, the Mars dungeon, Zand's
Mansion, the Sanctuary of Linga, Hoffmann Ruins, the Lacour Front Line, Eluria Tower, the
Mountain Palace, Lasguss Mountain, the Cave of Trials) and Energy Nede 9 (the Synard Plant, the
Cave of Red Crystal, the four Fields, Mihne Cavern, the Portal of Sealing, Fienal). Three
chambers stand in towns and need no key: Berle at Armlock 306, Shigeo in Fun City 281, Marsilio
in the Fun City arena 305. Fienal's entrance carries the goal requirement (D6) as `won` (the
chambers' encounters) with a banner; the Final Save Point's way to the final corridor (door
43978493) keeps the goal's own conditions.

### Sidecar

Chamber victories go in `goal_won`; the hub state is built once at New Game and saved with the
game. A save made in a view holds the real state (the view's values never reach it); loading it
starts with the view off, and the loaded map's own load turns it back on in a chamber whose boss
is unbeaten; in a chamber that asks first (`ask`) only when the save was made in its view (the
sidecar's `hub_view`, the one hub field: "The port: the questions"). The save guard ("The save sidecar") applies as in every
mode. Proven headless (hubcore, 2026-10-25): a hub save and its load keep the hub (no second
New Game: the party at the saved position, chapter 60 step 4000, the lock on), and a save made
in Armlock 306 (Berle 535's view on) loads with the view on again.

### The port: the chapter lock and the story views (agent "hubvm", `engine/src/game/ap/hub.cpp`)

What the port does with `world_mode: hub`, `hub.lock` and `bosses` (implemented 2026-10-25; the
hub New Game, the start and the dungeon doors are the "hubcore" agent's). Without a hub seed none
of it runs: the script VM tests one flag (`ap::g_hub_vm`) and takes the original path.

- **Where it hooks.** Every script access to a global variable or flag goes through the VM's
  `var_get` / `var_set` / `flag_get` / `flag_set` (`engine/src/game/field/script_vm.cpp`; also the
  inc / dec ops and block_store 0x61-0x63, which wrote around them before), and with a hub seed
  asks `game/ap/hub` first, byte by byte. Native code that reads or writes the game state
  directly is not redirected: the save icon's chapter (the real one), the world map's chapter
  test (no view is on there), table A 0x19 `set_point` (the vehicles' spots), the music word,
  battle results.
- **The lock** (`hub.lock.chapter` / `.step`, both default on). Outside a story view a script store
  that touches G[0xa0]-G[0xa3] is dropped; one touching G[0xa4]-G[0xa7] too, except on the maps
  of `hub.lock.step_maps` (default `[685]`: the goal fight's aftermath stores step 5000 on the
  way to the endings). Each site is logged once: `[ap] hub: chapter store 13 dropped at map 547
  offset 0x6a80 (the lock)` (the loaded release's script offset). Why steps too: at chapter 60
  the only step stores that belong to the hub are Fienal's (inside their views, D10) and 685's;
  every other one that can run there pairs with a dropped chapter store ("chapter N, step 0":
  547, 576, Arlia 22, the Fields) or is a flag-guarded store from an earlier chapter, and would
  move the hub off step 4000. Script B 0x6a (the disc switch) is dropped in hub mode (logged
  once); with the chapter held at 60 its scene (map 625 at chapter 34) never comes anyway.
- **A view's life.** When a map's script loads (before its set-up code runs) and the party is in
  a row's `maps`, the row's `game` matches (Claude's game = script flag 1) and its encounter is
  not in `goal_won`, the first such row with a `view` turns on: `[ap] hub: story view on: map 61
  (Alen-Tax, encounter 191): chapter 6 step 2100, 1 flags, 0 globals`. A row without a view logs
  `no story view, the scene plays at the hub's state`. While on: reads of G[0xa0]-G[0xa7], of the
  view's `globals` bytes and of its `flags` return the view's values; stores to them change only
  the view; every other variable and flag is the real one (stores go through). Party changes
  (ext 0xae / 0xaf) are dropped (`story view: party_add Rena at map 61 dropped`). The battle
  keeps it (the field reloads the same map); a victory in the row's encounter goes in
  `goal_won` (as for any row's encounter, with or without a view) and marks the view won.
- **A view's end.** At the first map load outside `maps` (the scene's transfer out, or the party
  walking out before the fight: it comes back on re-entry) or at free play after the win: ap's
  free play (field state 0, the player in control, script depth 0) and the field's own rule
  (field/leader.cpp free_play: no event or talk, the thread not waiting on a load / fade / music /
  choice, controls unlocked, no fade or window) held for 10 frames in a row [hubchambers: a battle
  hands the field back mid-scene with the controls on and the thread at depth 0 waiting on the
  map's load for a frame; Shin 447's view ended there and its aftermath ran outside it before
  this rule]. The `keep` flags a script stored in
  the view are written to the real flags then (a chest flag among them records its check as
  usual); everything else it stored is dropped: `[ap] hub: story view off: map 2 (Arcmene,
  encounter 522; map 1088 loaded): dropped chapter 41, step 0, flag 12=1, flag 745=1; kept flag
  11=1`. New Game and a load turn a view off with nothing written back; a save holds the real
  state; a save loaded in a chamber whose boss is unbeaten turns its view on with the map's load.
- **`view.after_maps`, `view.until`, `view.cut_to`, `view.done_flag`** (read by the port): a
  load of an `after_maps` map keeps a view on (it never starts one there); with `until:
  "transfer"` free play doesn't end the view, only a load outside `maps` / `after_maps` does;
  with `cut_to`, once the boss is won the first script transfer to a map outside `maps` /
  `after_maps` goes to `cut_to` instead (`story view: Berle, encounter 520: the transfer to map
  576 goes to map 557 (-92, 0, 1907) instead (cut_to)`, ahead of the seed's door table), and the
  view ends at that load even if `cut_to.map` is one of `maps`; with `done_flag`, a load of one of
  `maps` after the win still turns the view on (won from the start) while that real flag is clear
  (`story view: Guardian, encounter 525 won before; on until flag 163 is set`). Unit-tested
  (ap_hub_view_keys) and proven headless on their chambers (tools/ap_hub_chambers.py:
  magichand / power / power_out / lover / bark / synard / berle306 / tower, both heroes).
- **`view.globals`.** `{"0xADDR": word}` redirects the word's four bytes (the neighbours at the
  seed's values); `{"0xADDR": {"size": 1 | 2 | 4, "value": v}}` (ADDR aligned to size) redirects
  only those bytes. 0xa8 <= ADDR, ADDR + size <= 0x340.
- **Writing a view's flag list.** `SS_HUB_TRACE=1` prints every game-state store and flag store a
  script makes while a view is on, with `view` (taken) or `through` (real): `[ap] hub trace: flag
  745 = 1 at map 417 (view)`, and each flag's first read (`read flag 389 = 0 at map 463 (real)`). Flags a scene stores that the hub must not keep belong in `flags`
  (745, which the Alen-Tax and Arcmene scenes set, needs no listing: every map's set-up clears
  it; docs/catalog/globals.csv script_flags). Proven headless (late proof state, 2026-10-25): Visseyer 412 (no view),
  Alen-Tax 61 and Arcmene 417 (views); docs/handoff/hubvm.md has the runs. Not flag 12 in
  Arcmene's `keep`: it is clear in the hub state and makes part of Nede's world map unwalkable.
- `status_text` (console `ap`) shows `hub | lock chapter on step on | chambers N | story view ...`.

### The port: New Game, the starts, the doors, P5 (agent "hubcore", `engine/src/game/ap/ap.cpp`)

What the port does with the `hub` block's state, `hub.start`, the seed's `doors` in hub mode and
the P5 rows (implemented 2026-10-25; docs/handoff/hubcore.md has the runs).

- **New Game** (`on_new_game_state`, from the field's `field_create_state_block` new-game branch,
  after the original's hero, names, party and chapter 3 / 4): only when `world_mode` is hub and
  the `hub` block holds `flags` or `start` (a lock-only block builds nothing). The script flags
  are cleared and `flags` set, then `globals`, `games[hero]` (flags, globals), the route flag
  forced to the hero picked (flag 1 set in Claude's game, clear in Rena's), `chapter` / `step`,
  config +0x4c = `disc` - 1 with the disc inserted (the insert-disc screen if disc 2 is
  missing), `start.globals`, and the start: game_state +0x1a map, +0x08 position, +0x18 facing
  (`dir` degrees as 4096ths). The field's first map load keeps that position
  (`take_start_position`). Logged: `[ap] hub New Game: Claude's game, 1063 flags, 25 globals (+1
  / 1 the hero's, 7 the start's), chapter 60 step 4000, disc 2; Fun City (map 279 at 470, 0,
  68, facing 180)`. Then `start.items`, `goal.flags` and the starting character as in open world.
  A `--map` debug start keeps its map: the state is applied there only with `SS_HUB_STATE=1`
  (tests). A load never runs it (the snapshot branch).
- **`hub.start`** is a row of hub_state.json `starts` copied whole (16 towns: Expel's Arlia,
  Salva, Cross, Clik, Mars, Herlie, Port Hilton, Lacour, Linga, Eluria Colony; Energy Nede's Fun
  City, Central City, North City, Armlock, Giveaway, Laqua). The port reads `name`, `map`, `x`,
  `y`, `z`, `dir` (the arrival of the town's world-map entrance, checked open at the hub state)
  and `globals` (the seven Synard words); `key`, `world`, `door`, `exit`, `synard_expel`,
  `synard_nede` are for the APWorld and the proofs. The start world's Synard waits at the start
  town's own spot (its script's `set_point`), the other world's at its arrival town (Expel:
  Arlia, Nede: Fun City), whose arrival scripts re-park it. All 16 proven in both games (New Game
  -> free play at the arrival -> the town's exit -> the world map, the Synard where the row
  says). Armlock's start is Berle 535's chamber (its view is on from the arrival).
- **Doors in hub mode.** Keys lock as in open world (`missing_keys`: open world or hub): a door
  with its `routine` is refused at the trigger (nothing runs; the banner "<key> needed"), one
  without at the transfer. The Fienal gate (68685691) has no key: its `won` (the requirement's
  chambers) with one banner per boss, the first missing one shown. At the first free play every
  banner the seed's doors can show is measured in the map font (`[ap] door banners measured: N,
  M too wide`; at most 236 px): all 19 Fienal banners and every key text fit (widest "Defeat
  Berle (Eluria Tower) first", 223 px). The Portal of Sealing is out of hub mode (no key, its
  chest no location); Lasguss Mountain's key exists only when XINE is a location.
- **P5: Eluria Tower.** 557 -> 558 needs nothing: walking into the door region runs the
  routine (at chapter > 32 it walks the leader to the door first, which a sweep that fires the
  routine from elsewhere never finishes). 558 -> 559 needs nothing: the stairs are ramps down
  (floor zones y 0 -> 450) whose bottom regions transfer only when the leader's height is above
  400 (0x3c00 / 0x3c60); walked on both sides. The climb (559 -> 575, Shin 519 at 569, Berle 520
  at 575, then cut_to 557) works with Berle's view at chapter 33 **step 20**; at step 0, 573 ->
  574 is shut and 566 -> 567 plays the 33/0 split scene, which strands the party on 627.
- **P5: the arena 305 (Marsilio 532).** No door enters 305 at chapter 60. The seed's door
  19929509 (the antechamber 304 -> the lobby 296) carries `to` {305, -13, 0, 276, 271} (the
  registration's arrival) with `while_unbeaten` 532 (the APWorld's `hub.P5_DOORS`): while
  Marsilio is unbeaten that door leads into 305, whose view (57/20) registers the Clerk (K16):
  "Go see Narl" plays Marsilio's scene, and its transfer to 296 ends the view; after the win
  the door leads to 296 again. 296 stays reachable from 299 / 300. Inside the view 305 has no
  walk-out exit (the Clerk's training and rest keep the party there), and before the rulings
  below every way into 305 turned the view on: a party could leave only by winning. With the
  row's `ask` (the APWorld writes it) the door asks first, the desk's "Register." is the normal
  arena and the Clerk offers "Leave the arena." ("The port: the questions").

### The port: the questions (agent "hubask", `engine/src/game/ap/hub.cpp`)

The user's rulings (2026-10-25, docs/handoff/hub.md): **Shigeo 281, ask first** (Fun City's 281
lies on the only Energy Nede -> Expel route, and his scene starts from a strip across the street
while he is unbeaten) and **the arena 305, ask plus a way out** ("players will generally use the
arena in Fun City as a way to level up their characters. They need to be able to do that without
being forced to go through Marsilio first"). A row's `ask`:

```json
"ask": {"text": "Marsilio waits in the arena.\nFace Marsilio now?", "doors": [19929509], "again": "visit",
        "options": [{"map": 297, "window": "0x5c50", "choice": "0x5c6c", "text": "Face Marsilio.",
                     "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271}},
                    {"map": 305, "window": "0x6ea8", "choice": "0x6ec4", "text": "Leave the arena.",
                     "to": {"map": 304, "x": 1339, "y": 0, "z": -6, "dir": 180}}]}
```

| Key | Meaning |
|---|---|
| `text` | the question: 1-3 lines (`\n`) of at most 34 characters (the port refuses more) |
| `answers` | (optional) `[yes, no]`, default `["Yes.", "No."]` |
| `doors` | (optional) the door ids (map * 65536 + offset / 4, NA) whose transfers into the chamber ask; absent: every script transfer into the chamber's `maps` from a map outside them asks |
| `again` | (optional; agent "hubask2") when the question comes again after a no: `"visit"` (the default) once per visit: after a no, or an option out of the chamber, the row is quiet (each of its doors a no without the question) until the party has been on a world map; `"always"` at every door |
| `options` | (optional) an extra last option on a choice the game already has: `map` (the script's map), `window` / `choice` (NA byte offsets of the options window's ext 0x68 and of the choice's ext 0xdc N; the port checks their words, and the `push R` / `pop_store L[n]` after the choice, in the loaded release's layout), `text` (the option's line), `to` {map, x, y, z, dir} where choosing it sends the party |

- **While the boss is unbeaten the view comes on only after a yes.** A yes at a question, or an
  option whose `to` lies in the chamber, makes the next map load turn the view on; a no, a load
  (unless the save was made in the view: the sidecar's `hub_view`) or any other way in leaves the
  chamber at the hub's own state (chapter 60). Nothing is recorded: the boss stays unbeaten and
  the question comes again at the next way in (with `again` "visit", the next visit). Once the
  boss is won, `ask` does nothing.
- **Once per visit** (`again` "visit"; the user, 2026-10-25: "If the player wants to face him,
  they would need to go to the world map and then back into Fun City to trigger the prompt
  again"; agent "hubask2"). A no at the question (or a cancel), or an `options` entry whose `to`
  lies outside the chamber (the Clerk's "Leave the arena."), makes the row quiet: its doors
  then take the no without asking (a seed door's own target, else the chamber at the hub's
  state), and the chamber's map is an ordinary map. A world map's load (its chunk scripts, maps
  909-980, 1048-1110, 1113-1114) ends the visit: the next door asks again. Options into the
  chamber (the desk's "Face Marsilio.") stay offered while the row is quiet. The quiet rows are
  not saved: New Game and a load forget them (the next door asks). Logged: `... quiet until the
  party has been on a world map (once per visit)`, `... door 19468777 (map 297 -> map 281): quiet
  this visit: the chamber at the hub's state` (or `: the door's own target (map 296)`), `... the
  world map (map 1072): asks again`.
- **The question** (`hub_ask_transfer`, from `ap::on_map_transfer` after the door table). The
  transfer op hosts it: the op runs again (ip back on it, stack arguments back on the stack) while
  the question's phases run, as the game's own yes / no questions are laid out (map 297's fee):
  the question in window 1 (a hold window at (x, 2), as wide as its longest line in the map font
  needs: a line wraps past the window's width less 16 px), the VM waits for it to print (wait
  state 2), the answers in window 0 under it with the game's lead-in before each line (glyphs
  0xfe 0xfe 0xea on NA / Europe; 0x72 x 4 and 0x82 on France / Germany), the VM's own choice
  (wait state 9: the first option first, cancel allowed = no), both windows closed (wait 2 / -1),
  then the transfer. Yes: as the door table sent it. No: when a seed door had sent the transfer
  into the chamber (`to.while_unbeaten`) its own target; else the chamber at the hub's state.
  Logged: `[ap] hub: ask: Shigeo, encounter 533: door 18356813 (map 280 -> map 281): "Shigeo is
  on the rampage ahead. / Face Shigeo now?" (window 31 cells, its widest line 217 px)`, then
  `... yes: map 281 with its story view` / `... no: map 281 at the hub's state` / `... no: door
  19929509 goes to its own target (map 296)`. A synchronous call (vm_call_sync) never asks.
- **The options** (`hub_fetch`, called by the VM before each instruction while the loaded map has
  armed sites): at `window` the options window opens one line taller (ext 0x68 from the stack)
  with the extra line after the original's (its own lead-in copied; `ap::script_text`); at
  `choice` the choice takes one option more; after the choice's store, when R is the extra
  option: windows 0 / 1 close, ext 0x84 gives the controls back and ext 0xda (from the stack)
  sends the party to `to` (into the chamber: the view comes on; out of it: the view ends there,
  nothing written back). On a chamber map an option shows only while its view is on (the Clerk
  exists only then anyway); elsewhere while the boss is unbeaten. A site whose words differ is
  logged and left alone (`... is not the shape the seed names ...; no extra option`).
- **The two rows** (apworld/so2/hub.py `ASK`; proven headless, docs/handoff/hubask.md):
  - Shigeo 533: `{"text": "Shigeo is on the rampage ahead.\nFace Shigeo now?", "again": "visit"}`:
    the doors into 281 (from 280, 292 and 297) ask. Yes: his scene and fight; no: 281 at the
    hub's state, the strip does nothing (walked across headless), the route on to 297 and Expel
    stays open, and the doors into 281 stay quiet until the party has been on the world map.
  - Marsilio 532 (above): the antechamber's door asks once per visit (yes: the arena with the
    view; no, or the Clerk's way out: the lobby 296, the door quiet until a world map: a player
    training at the arena passes it to 296 without the question); the desk's first menu (297, the receptionist) reads "Hear an explanation. /
    Register. / Face Marsilio.": Face Marsilio goes into the arena with the view, Register is the
    arena as it is (no view: the fee, the battle mode, the ranking battles in 305, back to 304:
    the training any time); in his view the Clerk's menu reads "Do intensive training. / Rest. /
    Go see Narl. / Leave the arena.", the last one back to the antechamber 304 without a fight.
    Why options there and not a yes / no: the desk's own transfer into 305 comes after the fee,
    the battle mode, flag 586 and the party flags 758-769 (a team battle sets members aside; only
    305's arena routine puts them back), so a question at that transfer would cost a yes the fee
    and leave the party split; and 305 is the arena ring itself (no door): the Clerk, its only
    entry in the view, is where a player looks for the way out. The Clerk's own options keep their
    numbers ("Go see Narl" is still option 2).
- The words were checked on all four releases (NA, Europe, France, Germany: 297 alike, 305
  through the region tables' script map).
