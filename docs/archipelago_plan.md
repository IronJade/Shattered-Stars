# Archipelago plan: Star Ocean: The Second Story on Shattered Stars

**Goal.** Make Star Ocean: The Second Story (PlayStation, North American release) a game you
can play in an [Archipelago](https://archipelago.gg) multiworld through this port, and later get
it listed as a supported game.

A seed shuffles the world:
- what chests, gifts, shops and drops give;
- later, who joins the party and which areas are open.

The generator proves the seed can be finished, and the story's checkpoints always stay
reachable.

## How Archipelago works, and what we must build

From the [setup guide](https://archipelago.gg/tutorial/Archipelago/setup_en) and the
[adding games](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/adding%20games.md) docs:

- **Generation.** Each player writes a YAML of options for their game. The generator combines
  all players' YAMLs into one multiworld and places every item at some location in any game.
  The output is a zip holding the server's data plus one patch / seed file per player that needs
  one. It can be hosted on the website or locally.
- **The world (APWorld)** is a Python plugin
  ([World API](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/world%20api.md)). It
  declares:
  - item and location names with numeric IDs;
  - the regions and the rules for entering them (`state.has("Item", player)`);
  - item classes (progression / useful / filler / trap) and a completion condition;
  - `fill_slot_data()` for the client, and `generate_output()` for a patch file.

  The generator's fill only places items where the rules let the player reach them, so a seed
  that generates is finishable as long as our rules match the real game.
- **The client** talks to the server over a websocket
  ([protocol](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/network%20protocol.md)):
  - **Connect:** game, slot name, version, items_handling flags, tags.
  - **Send checks:** report checked locations with `LocationChecks`, and keep any checked while
    offline to send on reconnect.
  - **Receive items:** apply `ReceivedItems` at any time, keeping the item index to resync.
  - **Goal and DeathLink:** report the goal with `StatusUpdate` (CLIENT_GOAL), and optionally
    send and receive DeathLink `Bounce`s.
  - **Connection:** handle ws and wss, reconnect, and a changeable port.

**The precedent.** [so2r-archipelago](https://github.com/Skuldier/so2r-archipelago) already does
this for the 2023 remake, *Star Ocean: The Second Story R*, through a BepInEx plugin.
- **Checks:** chests, monster drops, Challenge Missions and Bucket List objectives.
- **Items:** area keys and character items in an open-world mode, or a story mode.
- **Extras:** loot tiers, DeathLink and traps.

Ours is a different game entry ("Star Ocean: The Second Story", the PS1 original), but the story,
areas and characters are the same. Its design is a useful map of what players expect. Before
reusing any of its data, check its licence and talk to its author.

## Why the native port makes this easier

Emulated games reach Archipelago through emulator scripts that peek and poke RAM (BizHawk / Lua
clients). Shattered Stars runs the game as our own C++, so the randomizer hooks straight into the
code that gives items, sets story flags, ends battles and saves. Nothing on the disc is changed:
everything is applied at run time, the way world edits already are (`game/field/mods.cpp`).

What exists today that the randomizer builds on:

| System | Where in the port | Original | Use for Archipelago |
|---|---|---|---|
| Event scripts (VM, 4 tables of opcodes) | `game/field/script_*.cpp`, `vm_services.cpp` | 0x8004.. / docs/catalog/script_opcodes.csv | every chest, gift, recruitment, story flag and door is a script instruction we can see and intercept |
| Script patching | `game/field/mods.cpp` (`script MAP WORD EXPECTED VALUE`) | — | change chest contents, gifts and door targets per seed, checked against the original word |
| Chests | script B 0x6c (registration with opened flag + handler), B 0x60 give_item | catalog | a location per chest, keyed by its opened flag |
| Item gifts, Fol | B 0x60 give_item, ext 0xd4 add_money | catalog | a location per gift call site (map + script offset) |
| Inventory | `inventory_add`, `common/party.cpp` (10-bit item ids, counts) | field 0x8003c8xx | giving received items |
| Item records, shops, enemy stats | `mods.cpp` (`item`, `shop`, `enemy` lines) | entry 3002, overlay 0xbaa, encounter packs | shop stock shuffle, prices, enemy scaling |
| Party | `party_add_member` (ext 0xae), records, killer moves, Link Combos | catalog | characters as items; starting party |
| Battles | `battle/encounter.cpp` (game_state +0x264 = encounter), `result.cpp` (outcome, EXP / Fol, `result_roll_drops`) | #2129, #1981 | boss checks, the goal, drops as checks, EXP / Fol multipliers, DeathLink (outcome 2 = party wiped) |
| Story state | script flags (0x170 bytes, 2944 flags), script globals G[], game_state | catalog | events, starting flags, open-world unlocks |
| Doors / map transfers | ext 0xda / 0xa4 / 0x7d, `mods.cpp` door patches | catalog | area keys, entrance shuffle (later) |
| Messages, notices | `game/field/message*.cpp`, `notice.cpp` | catalog | "Received Mithril from Alice" banners |
| Saves | `common/menu_save.cpp` (byte-compatible with real cards) | 0xbb6 | a sidecar file keeps the Archipelago state, so the card format never changes |
| Debug console | `--debug` (`warp`, `item`, `flag`, `battle` ...) | — | testing seeds without playing through |
| Setup program | `shattered-stars-setup` (in progress) | — | server address, slot, password, seed file |

**Measured on the discs** (`tools/ap_survey.py`; field data is identical on both discs):
- **Chests:** 338 (330 items, 8 Fol).
- **Shops:** 64 with their stock lists.
- **Enemy types:** 187, each with two drops and odds.
- **Item ids:** about a thousand (10-bit).
- **Script calls:** about 1,300 `give_item` and 1,076 `add_money` call sites (these include shared
  helper routines every map carries, so the real gift count is lower). Exact location tables are
  a Phase 1 deliverable.

## Architecture

```
 YAML options ─▶ Archipelago generator ─▶ AP_xxx.zip ─┬─▶ server (website or local)
                  (worlds/so2: our APWorld)            └─▶ <seed>.apso2 for the player
                                                               │
 shattered-stars-setup: server, slot, password, .apso2 ────────┤
                                                               ▼
 Shattered Stars ── game/ap/ ── seed (placements, options) applied as mods at load
                        ├─ check hooks (chests, gifts, bosses, ...) ─▶ LocationChecks
                        ├─ receive queue ─▶ inventory / party / flags at a safe moment
                        ├─ goal and DeathLink
                        └─ sidecar save: received index, checked locations, seed id
```

### The APWorld (`worlds/so2`, Python)

- **Items:**
  - every game item (id offset + item id);
  - Fol bundles;
  - character items ("Ashton joins");
  - in later phases, area keys and traps (an ambush battle, losing Fol, a status ailment).
- **Locations:** chests, scripted gifts and story rewards, bosses, shop specials. Later also
  first-time enemy drops, item creation firsts and Private Action rewards. Names come from the
  game's own text, generated once by a tool that reads the discs.
- **Regions:** follow the story (names from memory; confirm against the map table in Phase 1).
  - **Expel (disc 1):** Arlia, Salva, Cross, Clik, Mars, Herlie, Linga, Hilton, Lacour, the Front
    Line, El Nede.
  - **Energy Nede (disc 2):** Central City, the Fields of Power, Courage, Wisdom, Love and
    Silence, Fun City, Armlock, Fienal.
  - **Postgame:** the Cave of the Seven Stars.

  Entrances between regions carry the rules; story progress is modelled as events (locked items
  on boss / scene locations).
- **Goal options:** the final boss (default); disc 1's finale (a short game); all bosses;
  postgame bosses.
- **Output:** `.apso2`, a small JSON / zip with the seed id, slot, our own placements (location to
  item, including items for other players), shop stock, enemy scaling, starting inventory and
  options. `fill_slot_data` repeats what the client needs online.
- **Tests:** `WorldTestBase` accessibility tests for each mode (every location reachable, story
  events in order).

### The port (`engine/src/game/ap/`, C++)

- **Client:** [apclientpp](https://github.com/black-sliver/apclientpp). It's header-only (MIT),
  built on websocketpp, asio and nlohmann/json, with OpenSSL for wss (crypt32 on Windows). It's
  pinned with FetchContent like SDL3; MinGW needs `mingw64-openssl`. It runs on a background
  thread and hands events to the game thread through a queue, as the debug console already does.
- **The seed:** read at start-up and turned into mods patches: chest and gift contents, shop
  stock, enemy scaling, starting flags. Our own world's items are placed directly. Items for
  other players become a placeholder ("Alice's Hookshot"): the chest gives nothing and sends the
  check.
- **Check hooks:** one call in each place the game gives a reward. Chest handlers (by opened
  flag), give_item / add_money call sites (map + script offset), `battle_exit_to_field` (encounter
  + outcome), `party_add_member`, the shop buy path, `result_roll_drops`, item creation. Each looks
  up the location id and records the check, offline too, so it can be sent on reconnect.
- **Receiving:** received items wait in a queue until the game is in free play (field state 0,
  no script running, not in a battle or menu). They are then given through `inventory_add`, a
  party join or a flag, with a notice banner. The received index lives in the sidecar save, so
  nothing is given twice and a reload resyncs.
- **Goal:** the goal boss's battle exit with outcome 0 sends CLIENT_GOAL.
- **DeathLink (option):** a wiped party (battle outcome 2) sends a DeathLink. A received one KOs
  the party in battle, or ends the next battle at once.
- **Save sidecar:** `memcardN.mcd` + slot -> `memcardN.slotK.ap.json`, holding the seed id, slot
  name, received index and checked locations. The card stays byte-compatible with the
  original.
- **Story checkpoints:** in story mode, every item the main story needs stays where it is (or
  is given at the right scene). The randomizer can only add to the world, never block the
  story.

## Battle hooks

The battle side of Phase 1 is in [docs/ap/battle.md](ap/battle.md).
- **The tool.** `tools/ap_battles.py` writes `docs/ap/battles.json`: the encounters with
  their monsters, bosses and escape flags; every enemy type's two drops and odds; and the 89
  scripted battle call sites with the story flags their routines test and set.
- **The goal.** The final boss is Indalecio, encounters 539 / 540 on map 685.
- **The hooks.** Boss checks, the goal and the DeathLink send all hook into
  `battle_exit_to_field` (encounter id and outcome). A received DeathLink goes through
  `battle_party_lost`. First drops go through `roll_drops`; EXP / Fol multipliers through
  `award_exp_money`.

## Field locations and hooks

The field side of Phase 1 is in [docs/ap/field.md](ap/field.md).
- **The tool.** `tools/ap_extract.py` writes `docs/ap/field.json`. It holds:
  - 338 chests, 109 gift events, 171 prize-table entries, 1 Fol gift, 86 join sites, 62
    shop sites and 18 item-giving Private Actions, each with a location id and the
    conditions it runs under;
  - the story order and the regions.
- **The point of no return.** It is the disc switch at map 625 (B 0x6a, chapter 35). From
  there the story stays on Energy Nede (chapters 36-60).
- **Private Actions.** They run in script flag 2 / 4 mode, set by the world map's chunk
  scripts. Chisato and Precis join inside Private Actions.
- **The hooks.** For chests, the treasure counter (A 0x01) with the chest routine's opened
  flag. For gifts and joins, give_item / add_money / party_add, with the caller's frame as
  the location. Contents are changed through `mods.cpp` script patches.

## Keeping every seed finishable

1. **The generator's fill is the proof.** Archipelago only accepts a placement where every
   progression item is reachable under our rules (`accessibility: full` or `minimal`). Our job is
   rules that match the real game.
2. **The story always progresses.** In story mode, story key items and scenes are not shuffled.
   Area keys exist only in open-world mode, where we also unlock the doors they gate.
3. **The disc 1 to disc 2 point of no return.** Expel is left behind when the story moves to
   Energy Nede, but the game reopens it as Virtual Expel after Fienal's Final Save Point
   (checked headless; [field.md](ap/field.md), "Virtual Expel"). So the logic keeps Expel's
   locations, and only the time-limited ones count as missable. Before that was known, two
   ways out were planned:
   - **(a) Preferred:** a port-side "return to Expel" teleporter in randomizer mode, so Expel
     stays reachable.
   - **(b) Fallback option:** Expel's locations may only hold non-progression items once the
     point of no return is crossed in logic.
4. **Missables.** Private Actions, exclusive recruitments, Fun City and the cooking contest can
   be missed for good. Each is either:
   - made non-missable by the port (a check fires when the event is first seen);
   - excluded from progression (`exclude_locations` defaults);
   - or optional behind a YAML switch.
5. **A port-side validator.** `shattered-stars --ap-validate seed.apso2` loads every map and checks
   each placed location exists: the chest's flag and handler, the call site, the encounter id. It
   uses the event sweep from the M8 audit, so a seed can't name a location the game lacks.
6. **Tests.** APWorld unit tests; for the port, a scripted run of a short seed (the goal set to an
   early boss) in CI-style headless runs with the debug console.

## Options (first draft)

| Option | Values | Notes |
|---|---|---|
| `goal` | final_boss / disc1_finale / all_bosses / postgame | |
| `world_mode` | story / open_world | open world adds area keys and the Expel teleporter |
| `starting_character` | claude / rena / random | |
| `shuffle_chests` | on | the core |
| `shuffle_gifts` | on / off | scripted rewards |
| `shuffle_shops` | off / specials / full | |
| `character_items` | off / on | recruitments become items (logic respects the party size of 8) |
| `drops_as_checks` | off / first_drop | first drop of each enemy type |
| `exp_multiplier`, `fol_multiplier` | 1-10 | port-side, result screen |
| `encounter_rate` | 0-200% | port-side |
| `deathlink`, `traps` | on / off, percentage | |
| `fix_original_bugs` | on (default) / off | the `original_bugs.md` fixes; off for purists |

## Phases

1. **Foundations** (needs M7 battles and M8 "the game can be finished" first).
   - A disc reader tool, `tools/ap_extract.py`. It writes the location table (chests by flag,
     gifts by call site, shops, bosses by encounter, drops) and the item names as JSON.
   - Region and story-order notes for both protagonists.
   - The `game/ap` module skeleton, the seed file format and the sidecar save. Done on the
     port side (2026-10-22): [docs/ap/seed_format.md](ap/seed_format.md) covers the
     `.apso2` file, the checks and receive queue, and the sidecar; `--ap-seed FILE` or
     config `ap_seed`; the console commands `ap_status`, `ap_check` and `ap_give`.
2. **First playable: chest and gift shuffle, story mode.**
   - APWorld: items, locations, regions, goal = final boss. Written (2026-10-23):
     `apworld/so2`, [docs/ap/apworld.md](ap/apworld.md). It has 412 locations (311 chests,
     101 gift sites). Virtual Expel keeps Expel's locations reachable. 50 seeds fill, and
     the port pays what the spoiler says.
   - Port: chests, gifts and the goal as checks; receiving with notices; offline solo seeds; the
     setup program's Archipelago section.
   - Tests on both sides.
3. **Characters, bosses, drops.** Character items, boss checks, first-drop checks, DeathLink,
   traps, EXP / Fol / encounter options.
4. **Open world.** Area keys gating doors (map transfer patches), the Expel teleporter, shop
   shuffle. Later, entrance shuffle on the same door patches.
   - Port side (2026-10-23, [seed_format.md](ap/seed_format.md) "Phase 4"): the door table
     (`tools/ap_doors.py`, `docs/ap/doors.json`: 2529 doors, 66 world-map entrances), key
     items and locked doors (`world_mode: open_world`), seed door targets for the entrance
     shuffle with a validator (`apworld/so2/doors.py`), the Expel teleporter (Virtual Expel's
     arena old man and Arlia's guide from chapter 36), shop slots as locations.
5. **Release.**
   - First, a custom `.apworld` (custom_worlds; with the `archipelago.json` manifest Archipelago
     0.7 requires) and a port release.
   - Then a pull request to [ArchipelagoMW/Archipelago](https://github.com/ArchipelagoMW/Archipelago)
     for official support. That needs the core-verified requirements: a game info page
     `en_Star Ocean The Second Story.md`, a setup guide, unit tests and a maintainer.

## Getting listed on archipelago.gg

The website lists the worlds that are merged into the main repository. Custom APWorlds work with
any Archipelago install but aren't listed. Two things the reviewers will want:
- **Clear legality.** The port ships no game data and needs the player's own discs (already true:
  `docs/HANDOFF.md`).
- **An easy setup.** The setup guide reads: get Shattered Stars, run setup, choose your discs,
  enter the server, play. The setup program is being built now.

## Open questions

- Exact recruitment rules and exclusions (who can join with whom, the party cap of 8). Read them
  from the scripts in Phase 1, not from memory.
- Which Private Actions give items or set story flags; how relationship values affect endings
  and recruitments.
- North American release only, as Severed Chains and SO2R do with their own versions.
- Whether to cooperate with the SO2R world's author on shared names and regions, so players
  can move between the two.

## Sources

- [Archipelago setup guide](https://archipelago.gg/tutorial/Archipelago/setup_en)
- [Adding games](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/adding%20games.md)
- [World API](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/world%20api.md)
- [Network protocol](https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/network%20protocol.md)
- [so2r-archipelago](https://github.com/Skuldier/so2r-archipelago) (the remake's world)
- [apclientpp](https://github.com/black-sliver/apclientpp)
