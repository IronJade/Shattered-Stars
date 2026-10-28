# Data storage: the port's tracker keys

Port 0.03.00 (the tracker plan's B5, the user's decision D6). While an Archipelago game is connected to
its room, Shattered Stars writes where the party is and how far the story has got into the
server's data storage, so trackers can follow along: Universal Tracker's map tab (switching to the
right map and drawing a player icon) and the PopTracker pack. Items and checks need none of this;
they reach a tracker through the server as always.

The code: `ap_net::Link::set` (port/src/ap_net/link.h; the real client in
port/src/ap_net/client.cpp) and `publish_tracker_keys` in port/src/game/ap/ap.cpp. Tests:
port/tests/test_ap_ds.cpp (unit, a fake link) and step 9 of apworld/tools/ap_e2e_so2.sh (a real server;
apworld/tools/ap_ds_read.py reads the keys back).

## The keys

`{team}` and `{slot}` are the team and the slot (player) number of the server's `Connected`
packet: the team counts from 0, the slot from 1. Universal Tracker spells the same template
`so2_map_{team}_{player}`.

| Key | Value | Written |
|---|---|---|
| `so2_map_{team}_{slot}` | `{"map": M}`; on a world map `{"map": M, "x": X, "z": Z}` | when it changes, at most twice a second |
| `so2_story_{team}_{slot}` | `{"chapter": C, "step": S}` | when it changes, at most twice a second (read by both trackers' story awareness: "Reading the keys") |
| `so2_entrances_{team}_{slot}` | reserved for Phase C (below) | not written yet |

Examples: `{"map":26}` (Hearn's general store in Arlia), `{"map":1,"x":51200,"z":18500}` (on
Expel's world map, west of Arlia), `{"chapter":3,"step":100}`.

- **map** is the field map the game has loaded (game_state +0x1a): the map ids of
  docs/modding/reference/map_names.csv (`map` column; the `area` / `place` columns name them).
  It stays the same through a battle and a menu.
- **x**, **z** come only on the world maps (maps 1 Expel, 2 Energy Nede and 3, the unused third;
  the field's own test, `map - 1 < 3`). They are the party's position in map units, the same units
  as the world-map `arrivals` in docs/apworld/doors.json (the leader's position >> 12; while flying,
  the vehicle's). During a battle on the world map they hold the spot where the battle began. On
  other maps the key has `map` alone.
- **chapter** and **step** are the story's position: script globals G[0xa0] and G[0xa4]
  (game_state +0 and +4); a seed door's `until` compares the same chapter. In hub mode the game
  holds chapter 60 throughout (the hub's chapter lock, port/src/game/ap/hub.h); a boss chamber's
  story view shows the chamber's scripts their own chapter and step in a copy, so the key stays at
  chapter 60 there too.
- All numbers are JSON integers. Objects have exactly these fields; a later version may add
  fields, so readers should ignore the ones they don't know.

## When the port writes

Only while all of these hold:

1. Archipelago is on: a seed is loaded (`--ap-seed` or config.ini's `ap_seed`).
2. A server is configured (setup's Archipelago page, or `SS_AP_SERVER` in tests), so the game has
   a link at all.
3. The slot is connected **to the seed's own room** (the room's seed matches the `.apso2`'s; the
   port's status says "connected"), and the link's state is connected right now.
4. A game is running: not the title, and a map is loaded.

Then, every vblank after the link's events are handled:

- each key is written when its value differs from the one last written, but at most once every
  30 vblanks (about twice a second; the change waiting meanwhile goes out when the 30 are up), so a
  walk on the world map sends at most two position updates a second and standing still sends none;
- after every connect and reconnect both keys are written again at once, whatever they held, so
  the server always has the current values (a tracker that connects later reads them with `Get`).

Hub mode, story mode and open world all write the same keys the same way.

The write is the protocol's `Set` with one `replace` operation (the value is also the `default`)
and `want_reply` false. It is fire and forget: the game never waits for the server, and a write
made while the connection is down is dropped (the next connect writes both keys again anyway).
The port's log (shattered-stars.log) has a line for each story write and for each map change:
`[ap] tracker: so2_map_0_1 {"map":26}` (a position-only update has no line).

## What never happens

- **Nothing is written with Archipelago off, without a seed, or with a seed played offline**
  (no server, still connecting, the server has another room's seed, the connection down). In
  those runs the code does not run at all: game/ap's per-vblank poll exists only while there is a
  link to a server.
- **Nothing is added to saves or the save sidecar**, and nothing of the game's state changes: the
  keys are read from the game's memory, the values last written live only in memory, and no random
  number or timing of the game is touched. Headless runs without a seed, with Archipelago off and
  with an offline seed gave byte-identical logs, frame captures, memory cards and sidecars before
  and after this feature (the dskeys handoff log).
- The port reads no data-storage key and takes nothing from them; what a tracker writes there
  changes nothing in the game.

## Reading the keys (trackers)

A tracker logs in to the slot (tags `["Tracker"]`, any `items_handling`), takes `team` and `slot`
from `Connected`, then sends `Get` for both keys (the current values; `null` before the port has
ever connected) and `SetNotify` for both (a `SetReply` with the new `value` at each write). The
server sends every `SetReply` to every subscriber, so a tracker that only switches map tabs can
compare `map` with the previous value and skip position-only updates.

    scratchpad/ap/ap-venv/bin/python apworld/tools/ap_ds_read.py ws://localhost:38281 Claude --watch 60

prints `retrieved` lines for both keys, then a `set` line for each change for 60 seconds.

**Who reads `so2_story_*`** (story awareness, story mode only: docs/apworld/apworld.md "Story awareness";
the points a position has reached: docs/apworld/seed_format.md "Story awareness"):
- **Universal Tracker** (so2.apworld 0.2.2): in its regeneration of a story-mode room, unless
  host.yaml's `enforce_deferred_entrances` is `"off"`, the world sets its
  `found_entrances_datastorage_key` to `so2_story_{team}_{player}`; UT then Gets and SetNotifies it
  and hands each value to the world's `reconnect_found_entrances`, which opens the deferred story
  events of the points the position has reached and shuts the later ones. UT lists only what the
  story has reached. Open world and hub mode don't watch it.
- **The PopTracker pack** (1.0.0): already subscribed (Clear handler: Get + SetNotify); a value sets
  the Chapter item and the step, and the logic holds back the story's events and locations of points
  not reached yet (the export's `story` key, story mode only): sections in logic by the items but
  later in the story show as Inspect. The "Story" setting item turns it off.
- Before the port has ever connected (`null`), or with a value that isn't `{"chapter": number, ...}`
  with a chapter above 0, both show the plain logic. Both follow the value as it is, also backwards (an earlier save loaded).

Universal Tracker (B6): `map_page_setting_key = "so2_map_{team}_{player}"` with a
`map_page_index` that maps the value's `map` to a page; `location_setting_key` may name the same
key, with `location_icon_coords` turning `x` / `z` into the page's pixels (the world map pages'
own scale). On every `SetReply` of its `map_page_setting_key` UT checks the page (no reload when
it is the same) and updates the tracker, and on one of its `location_setting_key` it moves the
icon (UT 0.3.6, worlds/tracker/TrackerClient.py); the throttle keeps that to twice a second at
most.

## Reserved: `so2_entrances_{team}_{slot}` (Phase C)

The entrance randomizer (Phase C, world 0.3.0; docs/plans/next.md) will add a
third key: the redirected doors the player has used so far, written the first time each one is
used, for UT's deferred entrances (`found_entrances_datastorage_key`,
`reconnect_found_entrances`) and the pack's entrance display. Its shape is Phase C's to define.
The port writes nothing under this name before then; the spot for it is marked at the end of
`publish_tracker_keys`.
