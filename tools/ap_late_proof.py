#!/usr/bin/env python3
"""Which Archipelago locations stay reachable after the Final Save Point (docs/ap/apworld.md,
"Logic: the late proof").

The APWorld may put another world's progression in any location it does not exclude, and
Archipelago assumes a location in logic stays reachable. In Star Ocean 2 maps close: Expel is
left behind at the disc switch (Virtual Expel reopens it after the Final Save Point), and towns
change with the story. This tool proves, headless in the port, which chests, gifts and Cave of
Trials boss fights can still be done once the story reaches the Final Save Point (Fienal 9F,
map 671), the state from which every remaining location must be reachable.

The state is the user's own save at the Final Save Point (a Claude game: chapter 60, step
4000, flag 730 from the save that opens Virtual Expel, disc 2, the Fienal floor globals), with
every location's own "done" flags cleared, as for a player who skipped them all: each chest's
opened flag, each gift's guard flags (the flags it sets and tests), and the flags a Cave of
Trials boss fight needs clear. Rena's game is the same state with flag 1 cleared and the
story_route bytes swapped. The script globals and flags reach a `--map` start through
SS_SCRIPT_GLOBALS and SS_SWEEP_FLAGS; the party is Claude, Rena and Noel (SS_SWEEP_PARTY).

The search is breadth-first over event sweeps (SS_EVENT_SWEEP, engine/src/game/field/
event_sweep.cpp), one headless process per map:
- the roots are map 671 and Energy Nede's world map (map 2): the party walks back down out of
  Fienal after the save (Prima p. 130), which the sweep can't follow through Fienal's stair
  rooms. A map found later is reached by replaying the route the search found to it, entry by
  entry, from its root (SS_SWEEP_PLAY), so it is entered with the globals and
  flags the real transfers leave; then it is swept (SS_SWEEP_PLAY_SWEEP). A map is swept once,
  except where its exits depend on how it was entered: a map whose script reads the floor
  G[0x1cf], the previous map G[0x1c0] or Fienal's stair flag 157 (Fienal's stair rooms, the
  Cave of Trials, a few dungeon rooms) is swept once per value of what it reads, which the
  sweep prints after every entry (SS_SWEEP_SHOW);
- the sweep fires every trigger (T), talk (K) and pickpocket (S) entry the map registers, each
  from the same snapshot, answering windows and choices (the first answer, every third one
  cancelled); battles end at once in victory (SS_BATTLE_STUB, which reports the victory to
  the seed);
- an entry that leaves the player in control on another map is an edge. Private Action
  entries (P) are not followed;
- the port loads a proof seed holding every chest, gift and boss location of
  apworld/so2/data.py, with their placeholders patched in, and logs `[ap] check ID` for each
  one a run reaches. A location is proven in a game when a run of that game's search checks
  it.

It is conservative: every entry runs from the map's arrival state, so doors behind a switch or
a puzzle the save had not solved stay shut, and a choice takes its first answer. A location
it does not prove only loses the right to hold progression (apworld/so2/rules.py).

The teleporter state (--state teleporter, for open world; docs/ap/apworld.md "Open world"):
the same save at chapter 38 step 0 (the Nede story's first free walk on the world map, after
the disc switch) with flag 730 cleared, and the proof seed's expel_teleporter on, so the Fun
City arena's old man (map 303) takes the party to Arlia from chapter 36 (the port's three
patch lines, docs/ap/seed_format.md "The Expel teleporter"). The only root is Energy Nede's
world map. The party has no flight on Expel then (its Synard spot is set only by Virtual
Arlia's arrival, after the Final Save Point), so the search does not follow Expel's world-map
entrances to the islands the teleporter's party can neither walk nor sail to
(TELEPORTER_ISLANDS, from tools/ap_walkmap.py). A location proven there can be reached between
the disc switch and the Final Save Point through the teleporter. Its result goes to the
output's "teleporter" key.

The voyage state (--state voyage, for open world; docs/ap/apworld.md "Open world", "The El
voyage"): the same save at chapter 27 step 7000 with flag 730 cleared, on disc 1, the party
Claude and Rena: the last free play before the El voyage. At that step the Lacour Front Line's
meeting room (map 444) holds the trigger (routine 0x7c28) whose scene stores chapter 28 and
starts the one-way chain to El (the castle scene 422, Port Hilton's departure 169, the ship
548), which an open-world seed gates on the El passes. The only root is map 444. The search
does not follow an entry that leaves the story past chapter 27 (the sweep shows the chapter,
G[0xa0], after each entry), nor Expel's world-map entrances to El (229, 232, 557) or the Cave
of Trials (704); Hoffmann Ruins' island (231, 478) is followed: Port Hilton's ship still sails
there through chapter 27. A location proven there can be reached at the gate, so it may hold
what the gate needs. Its result goes to the output's "voyage" key.

Usage:
  tools/ap_late_proof.py --bin BIN --save CARD.mcd --save-frame 13 [--game claude --game rena]
                         [--jobs 3] [--resume] [--work scratchpad/late_proof] [-o docs/ap/late_proof.json]
SS_DISC1 / SS_DISC2 give the discs (or --disc1 / --disc2). --reparse rebuilds the result from
the logs in --work; --resume reuses the finished node logs there. A game takes about an hour
with 6 jobs; run both games at --jobs 3 each (or one after the other), so the machine never
has more than 6 game processes from it.

Every game process runs under the memory cap (tools/memcap.py: SS_MEM_CAP_GB, default 4 GB of
address space). A run that dies at the cap fails the proof (exit status 3, "at the memory cap"
in its node log); it is never retried or resumed past.
"""
import argparse
import collections
import datetime
import hashlib
import importlib.util
import json
import os
import re
import struct
import subprocess
import sys
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from memcap import Watch, cap_bytes, capped, hit_cap, run_capped  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = os.path.join(ROOT, "apworld", "so2")
# The Final Save Point, and Energy Nede's world map: from the save the party walks back down out
# of Fienal (Prima p. 130 describes leaving it after the save), a way the sweep can't follow
# through Fienal's stair rooms, whose exits depend on the room the party came from.
ROOTS = [671, 2]
TELEPORTER_ROOTS = [2]
TELEPORTER_CHAPTER = 38   # the Nede story's first walk on the world map (chapter 38 is stored at map 246)
# Expel's world map (map 1) entrances the teleporter state does not follow: the sweep fires
# every entrance trigger wherever it lies, but the teleporter's party lands in Arlia, walks the
# Cross continent and sails to the Lacour continent on the Herlie ferry (map 144's sailor,
# chapter >= 16); these lie on islands reached only by the story's ships (Port Hilton:
# chapters 23-27 to map 231, chapter 28 to El) or by Virtual Expel's Synard (after the Final
# Save Point): El Beach 229, Eluria Colony 232, Eluria Tower 557 (the El continent), map 231
# and Hoffmann Ruins 478 (their island), the Cave of Trials 704. tools/ap_walkmap.py derives
# them from the terrain (--check compares). The late state follows them all (the Synard).
EXPEL_WORLD_MAP = 1
TELEPORTER_ISLANDS = frozenset({229, 231, 232, 478, 557, 704})
# The voyage state (module doc): the Lacour Front Line's meeting room at chapter 27 step 7000,
# where the El voyage's trigger lies (apworld/so2/data.py STORY_GATES).
VOYAGE_ROOTS = [444]
VOYAGE_CHAPTER, VOYAGE_STEP = 27, 7000
VOYAGE_ISLANDS = frozenset({229, 232, 557, 704})   # El's continent and the Cave of Trials' island
VOYAGE_PARTY = "1"   # Rena joins the --map start's Claude (Noel is met on Energy Nede)
SCRIPT_FLAGS = 0x170 * 8
KEEP_FLAGS = {1, 2, 4, 730} | set(range(66, 94))  # the game, Private Action mode, the save, key items
PARTY = "1,10"   # Rena and Noel join the --map start's Claude (both are in every party at chapter 60)

ENTRY_END = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) end (.*?) frames")
ENTRY_START = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) code (\d+) start")
LOOP_END = re.compile(r"\[sweep\] map \d+ vblank \d+ entry \d+ \w\d+ end loop frames")
PLAY_DONE = re.compile(r"\[sweep\] map \d+ vblank \d+ play: control on map (\d+) after (\d+) of (\d+) steps")
DONE = re.compile(r"\[sweep\] map (\d+) vblank \d+ done")
ENTRIES = re.compile(r"\[sweep\] map (\d+) vblank \d+ entries (\d+)")
CHECK = re.compile(r"\[ap\] check (\d+) \(")
STATE = re.compile(r"\[sweep\] map \d+ vblank \d+ (settled|entry \d+) state (.*)$")
SHOW = "G1cf:1,G1c0:2,F157"   # the floor, the previous map, Fienal's stair flag (event_sweep.cpp)
SHOW_CHAPTER = SHOW + ",Ga0:4"   # the voyage state also shows the chapter
# The hub state also follows Fienal's maze on floor 6 (map 673: a 4 x 4 grid of rooms, G[0x1c2]
# one axis (inc / dec by two of its doors), G[0x1c6] the other (dec: < 0 leads on to 653, a floor
# higher; inc: >= 4 back to 652); entered from 652 at (1, 3), from 653 at (2, 0)) and the floor
# hall 647, whose rooms are picked by G[0x1c6] (> 1000, < -1000, > 0: the value the room the party
# came from stores): one node per value, so the search walks the maze room by room, within the grid
# and one step outside it (HUB_BOUNDS; further out the doors only count on). From below it is the
# only way past floor 6 (the late proof reached floors 7-9 from its root 671).
SHOW_HUB = SHOW + ",G1c2:2,G1c6:2"
HUB_STATEFUL = {647: {"G1c6"}, 673: {"G1c2", "G1c6"}}
HUB_BOUNDS = {673: {"G1c2": (-1, 4), "G1c6": (-1, 4)}}
CHAPTER_SHOWN = re.compile(r"(?:^|,)Ga0=(-?\d+)")
KEEP_LINES = ("[sweep]", "[ap] check", "[port]")   # what a node's log keeps of a run's output
HUB_LINES = ("[ap] hub", "[ap] goal")   # and in the hub state: the lock, the views, New Game, the bosses won
LOCK = re.compile(r"\[ap\] hub: (chapter|step) store (-?\d+) dropped at map (\d+) offset (0x[0-9a-f]+)")
VIEW_ON = re.compile(r"\[ap\] hub: story view on: map (\d+) \((.*?), encounter (\d+)\)")
NO_VIEW = re.compile(r"\[ap\] hub: chamber map (\d+) \((.*?), encounter (\d+)\): no story view")
GOAL_WON = re.compile(r"\[ap\] goal: encounter (\d+) won")
NEW_GAME = re.compile(r"\[ap\] hub New Game: (\w+)'s game")
PLAY_STEP = re.compile(r"\[sweep\] map \d+ vblank \d+ play: step \d+ on map (\d+): (\w)(\d+)")
NOW_ON = re.compile(r"\[sweep\] map \d+ vblank \d+ now on map (\d+)")


def load(name):
    spec = importlib.util.spec_from_file_location(f"so2_{name}", os.path.join(WORLD, f"{name}.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---- The state ----

def save_snapshot(card_path, frame):
    """The field snapshot of a memory-card save (game_state 0x2a0, then the script flags)."""
    card = open(card_path, "rb").read()
    f = card[0x2000 * frame:0x2000 * frame + 0x2000]
    count = struct.unpack_from("<H", f, 0x380)[0]
    stream = f[0x382:0x382 + count]
    image, i = bytearray(), 0
    while i < len(stream):   # the save's zero-run packing
        c = stream[i]
        if c != 0:
            image.append(c)
            i += 1
        elif i + 1 < len(stream) and stream[i + 1] == 0:
            image.extend(bytes(stream[i + 2] + 2))
            i += 3
        else:
            image.append(0)
            i += 1
    return bytes(image[0x1748:0x1748 + 0x440])


def done_flags(field):
    """The flags that record a location as done: chest opened flags, gift guards (flags a
    gift's routine sets and tests, and its guard_flags), Cave of Trials boss guards."""
    out = {c["flag"] for c in field["chests"]}
    for c in field["chests"]:
        out |= {v.get("flag") for v in c.get("versions", []) if isinstance(v, dict) and v.get("flag") is not None}
    for g in field["gifts"]:
        out |= set(g.get("guard_flags", []))
        out |= {int(k) for k in g.get("flags_set", {})} & set(g.get("flags_tested", []))
    for b in field["bosses"]:
        for s in b["sites"]:
            if 698 <= s["map"] <= 825:
                for r in s.get("requires", []):
                    if mo := re.fullmatch(r"flag\[(\d+)\] == 0|not \(flag\[(\d+)\]\)", r):
                        out.add(int(mo.group(1) or mo.group(2)))
    return out - KEEP_FLAGS


def build_state(snap, field, game, kind="late"):
    """(SS_SCRIPT_GLOBALS, SS_SWEEP_FLAGS, summary) for a game from the save's snapshot. kind
    "teleporter": chapter 38 step 0, flag 730 cleared (the teleporter state, module doc);
    "voyage": chapter 27 step 7000, flag 730 cleared (the voyage state)."""
    gs, flags = snap[:0x2A0], snap[0x2A0:0x2A0 + 0x170]
    words = {}
    for off in range(0, 0x2A0, 4):
        g = 0xA0 + off
        v = struct.unpack_from("<i", gs, off)[0]
        # Chapter and step; the story_route bytes (G[0xc4]); everything from the names on
        # (quest states, the floor G[0x1cf], the previous map G[0x1c0]). Not the map, the
        # leader's place or the music, which the --map start sets, nor the message speed
        # (G[0x1b8], game_state +0x118: a setting; the save's slows the text enough that the
        # sweep's presses answer choices wrongly).
        if g in (0xA0, 0xA4, 0xC4) or (g >= 0xC8 and v != 0 and g != 0x1B8):
            words[g] = v
    if game == "rena":   # game_state +0x24 / +0x25: 1 / 0 in Rena's game, 0 / 1 in Claude's
        words[0xC4] = (words[0xC4] & ~0xFFFF) | 0x0001
    cleared = done_flags(field)
    if kind == "teleporter":
        words[0xA0], words[0xA4] = TELEPORTER_CHAPTER, 0
        cleared = cleared | {730}
    elif kind == "voyage":
        words[0xA0], words[0xA4] = VOYAGE_CHAPTER, VOYAGE_STEP
        cleared = cleared | {730}
    on = [n for n in range(SCRIPT_FLAGS) if flags[n >> 3] >> (n & 7) & 1 and n not in cleared]
    if game == "claude" and 1 not in on:
        on.append(1)
    on = sorted(n for n in on if not (game == "rena" and n == 1))
    flag_text = ",".join(str(n) for n in on) + (",!1" if game == "rena" else "")
    glob_text = ",".join(f"{g:x}={v:#x}" if v >= 0 else f"{g:x}={v}" for g, v in sorted(words.items()))
    summary = {"chapter": words[0xA0], "step": words[0xA4], "flags_set": len(on), "flags_cleared": len(cleared),
               "globals": len(words), "flag_730": 730 in on, "floor_G1cf": gs[0x1CF - 0xA0],
               "previous_map_G1c0": struct.unpack_from("<h", gs, 0x1C0 - 0xA0)[0],
               "party_added": VOYAGE_PARTY if kind == "voyage" else PARTY}
    return glob_text, flag_text, summary


def proof_seed(path, teleporter=False):
    """A solo seed with every chest, gift and boss location of data.py, each holding a
    Blueberry (item 757) behind its placeholder (with the Expel teleporter for the teleporter
    state)."""
    data, patches = load("data"), load("patches")
    locs = []
    for row in data.LOCATIONS:
        t = row["type"]
        if t == "chest":
            check = {"type": "chest", "map": row["map"], "flag": row["flag"]}
        elif t == "gift":
            check = {"type": "gift", "map": row["map"], "offset": row["offset"]}
        elif t == "boss":
            check = {"type": "boss", "encounter": row["encounter"]}
        else:
            continue
        locs.append({"id": row["id"], "name": row["name"], "check": check,
                     "sites": [{"map": m, "word": w, "expected": e, "form": f, "role": r} for m, w, e, f, r in row["sites"]],
                     "item": {"player": 1, "kind": "item", "game_id": 757, "count": 1, "name": "Blueberry",
                              "classification": "filler"}})
    lines = patches.placement_patches(locs)
    for entry in locs:
        del entry["sites"]
    seed = {"format": "apso2", "version": 1, "seed": "late-proof", "game": "Star Ocean: The Second Story",
            "slot": {"name": "Proof", "id": 1},
            "options": {"story_route": "any", "expel_teleporter": teleporter},
            "items": {str(data.ITEM_BASE + 757): {"name": "Blueberry", "kind": "item", "game_id": 757}},
            "locations": locs, "patches": lines, "start": {"items": [], "fol": 0, "flags": []},
            "goal": {"encounters": list(data.GOAL_ENCOUNTERS)}}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(seed, f, indent=1)
    return len(locs)


# ---- The hub state (--state hub; docs/handoff/hubproof.md) ----

def hub_json(name):
    with open(os.path.join(ROOT, "docs", "ap", name), encoding="utf-8") as f:
        return json.load(f)


def hub_start_row(key):
    starts = hub_json("hub_state.json")["starts"]
    rows = {r["key"]: r for r in starts} if isinstance(starts, list) else starts
    if key not in rows:
        sys.exit(f"no hub start {key!r} (docs/ap/hub_state.json starts: {', '.join(sorted(rows))})")
    return rows[key]


def hub_env_state(game, start):
    """(SS_SCRIPT_GLOBALS, SS_SWEEP_FLAGS) of the hub's New Game state for `game` (hub_state.json
    `hub` in the port's order: flags, globals, games[game], then the start's globals), for the
    runs that can't have the port build it (SS_HUB_STATE) and for the summary."""
    hub = hub_json("hub_state.json")["hub"]
    words = {0xA0: hub["chapter"], 0xA4: hub["step"]}
    for block in (hub["globals"], hub["games"][game]["globals"], start.get("globals", {})):
        for a, v in block.items():
            words[int(a, 16)] = int(v, 16) if isinstance(v, str) else v
    flags = sorted(set(hub["flags"]) | set(hub["games"][game]["flags"]))
    if game == "rena":
        flags = [n for n in flags if n != 1]
    glob_text = ",".join(f"{a:x}={struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]}"
                         for a, v in sorted(words.items()))
    flag_text = ",".join(str(n) for n in flags) + (",!1" if game == "rena" else "")
    return glob_text, flag_text, {"chapter": hub["chapter"], "step": hub["step"], "disc": hub["disc"],
                                  "flags_set": len(flags), "globals": len(words)}


def hub_constant(name, default):
    """A literal table of apworld/so2/hub.py (P5_DOORS, ASK), read from the source (hub.py imports
    Archipelago)."""
    import ast
    tree = ast.parse(open(os.path.join(WORLD, "hub.py"), encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == name for t in node.targets):
            return ast.literal_eval(node.value)
    return default


def hub_p5_doors():
    """The APWorld's P5 door rows (hub.py P5_DOORS: the arena 305's redirect while Marsilio is
    unbeaten, Cyril's east side while he is unbeaten)."""
    return [dict(d) for d in hub_constant("P5_DOORS", ())]


def hub_ask():
    """The APWorld's `ask` per chamber (hub.py ASK: Shigeo 533, Marsilio 532), as seed_bosses adds it."""
    return {int(k): v for k, v in hub_constant("ASK", {}).items()}


def hub_proof_seed(path, game, start, doors=(), with_ask=True):
    """A hub seed (version 3, world_mode hub; docs/ap/seed_format.md "Hub") for `game`: the hub
    block as the APWorld writes it (hub_state.json `hub` with `start` = the start's row), every
    chamber row of hub_bosses.json with no key (the proof holds every key: the dungeons' keys are
    the logic's, data.HUB_DUNGEONS), no door rows but `doors` (the P5 ones), and every chest, gift
    and boss location of data.py plus the hub-only rows (data.HUB_LOCATIONS), each holding a
    Blueberry behind its placeholder."""
    data, patches = load("data"), load("patches")
    hub = hub_json("hub_state.json")["hub"]
    rows = hub_json("hub_bosses.json")["bosses"]
    locs = []
    for row in list(data.LOCATIONS) + list(getattr(data, "HUB_LOCATIONS", [])):
        t = row["type"]
        if t == "chest":
            check = {"type": "chest", "map": row["map"], "flag": row["flag"]}
        elif t == "gift":
            check = {"type": "gift", "map": row["map"], "offset": row["offset"]}
        elif t == "boss":
            check = {"type": "boss", "encounter": row["encounter"]}
        else:
            continue
        locs.append({"id": row["id"], "name": row["name"], "check": check,
                     "sites": [{"map": m, "word": w, "expected": e, "form": f, "role": r} for m, w, e, f, r in row["sites"]],
                     "item": {"player": 1, "kind": "item", "game_id": 757, "count": 1, "name": "Blueberry",
                              "classification": "filler"}})
    lines = patches.placement_patches(locs)
    for entry in locs:
        del entry["sites"]
    boss_keys = ("encounter", "name", "map", "maps", "view", "game", "location", "dungeon", "trigger", "late")
    ask = hub_ask() if with_ask else {}
    bosses = [dict({k: b[k] for k in boss_keys if b.get(k) is not None}, keys=[],
                   **({"ask": ask[b["encounter"]]} if b["encounter"] in ask else {})) for b in rows]
    seed = {"format": "apso2", "version": 3, "seed": f"hub-proof-{game}", "game": "Star Ocean: The Second Story",
            "slot": {"name": "Proof", "id": 1},
            "options": {"world_mode": "hub", "story_route": game, "character_items": True, "party_rules": "open",
                        "expel_teleporter": False, "hub_start": start["key"], "hub_start_town": start["key"]},
            "items": {str(data.ITEM_BASE + 757): {"name": "Blueberry", "kind": "item", "game_id": 757}},
            "locations": locs, "patches": lines, "start": {"items": [], "fol": 0, "flags": []},
            "goal": {"encounters": list(data.GOAL_ENCOUNTERS)},
            "doors": list(doors),
            "hub": dict({k: hub[k] for k in ("chapter", "step", "disc", "flags", "globals", "games", "lock")},
                        start=dict(start)),
            "bosses": bosses}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(seed, f, indent=1)
    return len(locs)


# ---- Runs ----

class MemoryCapHit(RuntimeError):
    """A game run died at the memory cap: the proof fails (never a retry)."""


def run_map(args, state, target, route, seed, logpath):
    """Replays route (root, steps) from its root and sweeps the map it ends on (resuming after a
    crash or hang). Returns the kept lines of every run."""
    root, route = route
    glob_text, flag_text = state[:2]
    voyage = args.state == "voyage"
    env = dict(os.environ)
    env.update({
        "SS_EVENT_SWEEP": "1", "SS_EVENT_SWEEP_FRAMES": str(args.frames), "SS_BATTLE_STUB": "1",
        "SS_DISC1": args.disc1, "SS_DISC2": args.disc2,
        "SS_SCRIPT_GLOBALS": glob_text, "SS_SWEEP_FLAGS": flag_text,
        "SS_SWEEP_PARTY": VOYAGE_PARTY if voyage else PARTY,
        "SS_SWEEP_SHOW": SHOW_CHAPTER if voyage else SHOW_HUB if args.state == "hub" else SHOW,
        "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy",
        # Never the player's settings: a scratch config directory and no memory card.
        "XDG_CONFIG_HOME": os.path.join(os.path.abspath(args.work), "xdg"),
    })
    for k in ("SS_HUB_STATE", "SS_SWEEP_PARTY_DROP", "SS_HUB_TRACE"):
        env.pop(k, None)
    if args.state == "hub":
        # state[2]: the hub run's own env (the port's New Game state through SS_HUB_STATE, which
        # then replaces SS_SCRIPT_GLOBALS / SS_SWEEP_FLAGS; the party: the hero alone).
        for k, v in state[2].items():
            if v is None:
                env.pop(k, None)
            else:
                env[k] = v
    # Disc 2 after the switch (the late and teleporter states); the voyage state is on disc 1.
    env.pop("SS_EVENT_SWEEP_DISC", None)
    if not voyage:
        env["SS_EVENT_SWEEP_DISC"] = "2"
    env.pop("SS_SWEEP_PLAY", None)
    if route:
        env["SS_SWEEP_PLAY"] = ",".join(f"{m}:{k}{i}" for m, k, i in route)
        env["SS_SWEEP_PLAY_SWEEP"] = "1"
    all_lines, skip, runs, limit = [], 0, 0, 40
    with open(logpath, "w") as log:
        log.write(f"[driver] root {root} route {env.get('SS_SWEEP_PLAY', '(none)')}\n")
        while runs < limit:
            runs += 1
            env["SS_EVENT_SWEEP_SKIP"] = str(skip)
            cmd = [args.bin, "--headless", "--no-memcard", "--frames", "2000000", "--map", str(root),
                   "--ap-seed", seed]
            if root in getattr(args, "root_pos", {}):   # (the hub's extra roots: an arrival a walk reaches)
                cmd += ["--map-pos", args.root_pos[root]]
            # Under the memory cap (tools/memcap.py), each run its own.
            run_cmd, preexec = capped(cmd, args.bin)
            p = subprocess.Popen(run_cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1,
                                 preexec_fn=preexec)
            watch = Watch(p.pid)
            last, start, lines, reloads, looped = [time.time()], time.time(), [], [0], [False]

            def reader():
                for line in p.stdout:
                    lines.append(line.rstrip("\n"))
                    # Each entry its own reload budget: the count starts again at every entry's
                    # start, so reloads that earlier entries' settles made never count against
                    # the entry in progress.
                    if ENTRY_START.search(line):
                        reloads[0] = 0
                    elif "settle (reload)" in line:
                        reloads[0] += 1
                    # The sweep's own guard (event_sweep.cpp kMaxReloads): after 6 reloads in a row
                    # it marks the next entry "end loop" and goes on without firing it, and in a
                    # map that leaves after every reload it does so for every entry left.
                    if LOOP_END.search(line):
                        looped[0] = True
                    # Any sweep line but play mode's status every 600 frames shows progress.
                    if line.startswith("[sweep]") and (" play: " not in line or " play: step" in line
                                                       or " play: control" in line):
                        last[0] = time.time()

            t = threading.Thread(target=reader, daemon=True)
            t.start()
            why = ""
            while p.poll() is None:
                time.sleep(0.5)
                watch.poll()
                now = time.time()
                # A map whose main script moves on after every reload (map 413 -> 416) would
                # loop in the sweep's settle: stop the run and fire the entries it skipped from a
                # fresh one (more than 6 reloads since the last entry started, or the sweep's
                # own "end loop").
                if now - last[0] > args.stall or now - start > args.timeout or reloads[0] > 6 or looped[0]:
                    why = "hang" if now - last[0] > args.stall else "timeout" if now - start > args.timeout else "reload loop"
                    p.kill()  # our own child, by its handle
                    break
            p.wait()
            t.join(timeout=5)
            if hit_cap(p.returncode, lines, killed=bool(why), watch=watch):
                log.write(f"[driver] exit {p.returncode} (at the memory cap: peak {watch.peak_kb // 1024} MB "
                          f"of {cap_bytes() >> 20} MB)\n")
                log.write("\n".join(lines[-20:]) + "\n")
                raise MemoryCapHit(f"map {target} (route {env.get('SS_SWEEP_PLAY', '(none)')}): a run died at "
                                   f"the memory cap, peak {watch.peak_kb // 1024} MB; see {logpath}")
            keep = [x for x in lines if x.startswith(KEEP_LINES + (HUB_LINES if args.state == "hub" else ()))]
            log.write("\n".join(keep) + "\n")
            log.write(f"[driver] exit {p.returncode}{' (' + why + ': killed)' if why else ''}\n")
            log.flush()
            all_lines += keep
            # The entries this run's sweep marked "end loop" without firing them (its reload guard),
            # and those it fired; the sweep may have gone on to "done" past the unfired ones.
            ends = [(int(mo.group(2)), mo.group(5)) for x in keep if (mo := ENTRY_END.search(x)) and int(mo.group(2)) >= skip]
            unfired = [i for i, result in ends if result == "loop"]
            fired = [i for i, result in ends if result != "loop"]
            if any(DONE.search(x) for x in keep) and not unfired:
                break
            # Died or hung: go on after the entry in progress (the route is replayed).
            started = [int(mo.group(2)) for x in keep if (mo := ENTRY_START.search(x))]
            found = [int(mo.group(2)) for x in keep if (mo := ENTRIES.search(x))]
            if not started:
                break
            if found:   # (each entry may need a run of its own in a map that loops)
                limit = max(limit, found[0] + 5)
            if unfired or why == "reload loop":
                # A reload loop comes in a settle, after an entry ended: go on with the first
                # entry this run did not fire, or after the last one that ended. Never past an
                # entry the run did not finish (the Cave of Trials' bosses on maps 705 / 807
                # were lost so: docs/handoff/elgate.md "Proof gap explained").
                nxt = min(unfired) if unfired else (max(fired) + 1 if fired else max(started) + 1)
                skip = max(nxt, skip + 1)
            else:
                skip = max(started) + 1
            if found and skip >= found[0]:
                break
        log.write("[driver] complete\n")
    return all_lines


def parse(lines, target, route, max_chapter=None):
    """What one map's runs found: the map swept, its exits {(map, entry kind, index): state on
    arrival}, the state it settled in, the checks, notes. max_chapter (the voyage state): an
    entry whose state line shows a later chapter (G[0xa0]) proves none of its checks."""
    swept, exits, checks, notes = (target if not route else None), {}, set(), []
    settled, last_exit = "", None
    in_entry, entry_checks, held = False, set(), None
    for x in lines:
        if mo := STATE.search(x):
            text = ",".join(mo.group(2).split())
            if mo.group(1) != "settled" and held is not None:
                cm = CHAPTER_SHOWN.search(text)
                if cm and int(cm.group(1)) > max_chapter:
                    if held:
                        notes.append(f"checks past chapter {max_chapter} not counted: {sorted(held)}")
                else:
                    checks |= held
                held = None
            if mo.group(1) == "settled":
                settled = settled or text
            elif last_exit is not None and (last_exit[0] != -1 or text != settled):
                # Another map, or the same map in another state (a latch, a floor counter).
                exits[last_exit if last_exit[0] != -1 else (-1,) + last_exit[1:]] = text
            continue
        if held is not None:   # (an entry without a state line)
            checks |= held
            held = None
        last_exit = None
        if max_chapter is not None and ENTRY_START.search(x):
            in_entry, entry_checks = True, set()
        elif mo := PLAY_DONE.search(x):
            if swept is None:
                swept = int(mo.group(1))
                if int(mo.group(2)) != int(mo.group(3)):
                    notes.append(f"route stopped after {mo.group(2)} of {mo.group(3)} steps")
        elif mo := ENTRY_END.search(x):
            kind, index, result = mo.group(3), int(mo.group(4)), mo.group(5)
            if kind != "P" and result.startswith("map "):
                last_exit = (int(result.split()[1]), kind, index)
                exits.setdefault(last_exit, "")
            elif kind != "P" and result == "ok":
                last_exit = (-1, kind, index)   # here; an edge only if the state changed
            if max_chapter is not None:   # (counted once the state line shows the chapter)
                held, in_entry, entry_checks = entry_checks, False, set()
        elif mo := CHECK.search(x):
            (entry_checks if max_chapter is not None and in_entry else checks).add(int(mo.group(1)))
        elif x.startswith("[driver] exit") and x != "[driver] exit 0":
            notes.append(x[9:])
        elif "main script left" in x:
            notes.append(x.split(" vblank ")[1].split(" ", 1)[1])
    return swept, exits, settled, checks, notes


def read_log(path):
    return [x.rstrip("\n") for x in open(path, errors="replace")]


def hub_events(lines):
    """The hub's lines of a node's runs: each store the chapter lock dropped with the entry that
    made it (a route step "MAP:Kn (route)", a sweep entry "MAP:Kn", or "MAP set-up" for a map's
    arrival / reload), the chambers whose story view turned on (or that have none), the
    encounters won, and the hero's game New Game built (SS_HUB_STATE)."""
    drops, views, won, games = {}, collections.Counter(), set(), set()
    entry, here, play_pending = None, None, False
    for x in lines:
        if mo := NOW_ON.search(x):
            here = int(mo.group(1))
        elif mo := PLAY_STEP.search(x):
            entry, play_pending = f"{mo.group(1)}:{mo.group(2)}{mo.group(3)} (route)", True
        elif mo := ENTRY_START.search(x):
            if play_pending:
                play_pending = False
            else:
                entry = f"{mo.group(1)}:{mo.group(3)}{mo.group(4)}"
        elif ENTRY_END.search(x) or " settled after " in x or " start (frame limit " in x:
            entry = f"{here if here is not None else '?'} set-up"
        elif mo := LOCK.search(x):
            key = (mo.group(1), int(mo.group(2)), int(mo.group(3)), mo.group(4))
            drops.setdefault(key, set()).add(entry or f"{mo.group(3)} set-up")
        elif mo := VIEW_ON.search(x):
            views[("view", int(mo.group(3)), int(mo.group(1)))] += 1
        elif mo := NO_VIEW.search(x):
            views[("no view", int(mo.group(3)), int(mo.group(1)))] += 1
        elif mo := GOAL_WON.search(x):
            won.add(int(mo.group(1)))
        elif mo := NEW_GAME.search(x):
            games.add(mo.group(1))
    return drops, views, won, games


def stateful_maps(disc1):
    """Map -> the state variables of SHOW its script reads: the floor G[0x1cf], the previous
    map G[0x1c0] (Fienal's stair rooms, the Cave of Trials, a few dungeons) and Fienal's stair
    flag 157. How such a map is entered decides where its exits go, so the search keeps a node
    per value of what it reads."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import so2_mod
    disc, out = so2_mod.Disc(disc1), {}
    for m in range(0, 827):
        try:
            ins = list(so2_mod.instructions(disc.map_script(m)))
        except BaseException:   # maps without a script section
            continue
        reads = set()
        for i in ins:
            t = i[2]
            if t.startswith("store"):
                continue
            if "G[0x1cf]" in t:
                reads.add("G1cf")
            if "G[0x1c0]" in t:
                reads.add("G1c0")
            if "G#0x9d" in t:
                reads.add("F157")
        if reads:
            out[m] = reads
    return out


def bfs(args, game, state, seed, stateful, root_maps=ROOTS, later_roots=()):
    """The search. A node is a map, or for a stateful map (stateful_maps) a map with the values
    of the state variables its script reads, which the sweep prints (SHOW) for each entry that
    leads there. later_roots (the hub's extra roots): each one only once the search from the
    roots has nothing left to run, and only if it hasn't reached that map itself."""
    logdir = os.path.join(args.work, game)
    os.makedirs(logdir, exist_ok=True)

    def ident(m, st):
        keep = [kv for kv in st.split(",") if kv.split("=")[0] in stateful.get(m, ())] if st else []
        return f"{m}@{','.join(keep)}" if keep else f"{m}"

    def map_of(node):
        return int(node.split("@")[0])

    def log_name(node):
        m = map_of(node)
        tag = "" if "@" not in node else "_" + hashlib.md5(node.encode()).hexdigest()[:8]
        return os.path.join(logdir, f"map_{m:03d}{tag}.log")

    roots = [ident(m, "") for m in root_maps]
    seen, route = set(roots), {k: (map_of(k), []) for k in roots}
    swept_of, edges, checks, notes = {}, {}, {}, {}
    hub = {"drops": {}, "views": collections.Counter(), "won": collections.Counter(), "games": set()}

    def done(node, lines):
        target = map_of(node)
        if args.state == "hub":
            drops, views, won, games = hub_events(lines)
            for key, where in drops.items():
                hub["drops"].setdefault(key, {})[node] = sorted(where)
            hub["views"].update(views)
            hub["won"].update(won)
            hub["games"] |= games
        swept, exits, settled, c, n = parse(lines, target, route[node][1],
                                            VOYAGE_CHAPTER if args.state == "voyage" else None)
        checks[node] = sorted(c)
        if n:
            notes[node] = n
        if swept is None:
            notes.setdefault(node, []).append("no sweep")
            return []
        here = route[node]
        if swept != target:   # the route went on (a scene) or stopped short
            other = ident(swept, settled)
            if other in seen:
                return []
            seen.add(other)
            route[other] = here
            node = other
        swept_of[node] = swept
        exits = {((swept if to == -1 else to), kind, index): st for (to, kind, index), st in exits.items()}
        if args.state == "teleporter" and swept == EXPEL_WORLD_MAP:
            off = sorted({to for to, _, _ in exits if to in TELEPORTER_ISLANDS})
            if off:
                notes.setdefault(node, []).append(f"island entrances not followed (no ship or flight): {off}")
            exits = {k: st for k, st in exits.items() if k[0] not in TELEPORTER_ISLANDS}
        if args.state == "voyage":
            # Not past the gate: an entry that moved the story on (the El voyage's chain stores
            # chapter 28), nor the El / Cave of Trials islands from Expel's world map.
            def past(st):
                mo = CHAPTER_SHOWN.search(st or "")
                return mo is not None and int(mo.group(1)) > VOYAGE_CHAPTER
            late = sorted({(to, kind, index) for (to, kind, index), st in exits.items() if past(st)})
            if late:
                notes.setdefault(node, []).append(
                    "entries past chapter 27 not followed: " + ", ".join(f"{k}{i}->{to}" for to, k, i in late))
            exits = {k: st for k, st in exits.items() if not past(st)}
            if swept == EXPEL_WORLD_MAP:
                off = sorted({to for to, _, _ in exits if to in VOYAGE_ISLANDS})
                if off:
                    notes.setdefault(node, []).append(f"island entrances not followed (only the voyage sails there): {off}")
                exits = {k: st for k, st in exits.items() if k[0] not in VOYAGE_ISLANDS}
        edges.setdefault(swept, set()).update(to for to, _, _ in exits if to != swept)
        new = []
        for (to, kind, index), st in sorted(exits.items()):
            k = ident(to, st)
            if args.state == "hub" and to in HUB_BOUNDS:
                values = dict(kv.split("=") for kv in st.split(",") if "=" in kv) if st else {}
                out = [v for v, (lo, hi) in HUB_BOUNDS[to].items() if v in values and not lo <= int(values[v]) <= hi]
                if out:
                    notes.setdefault(node, []).append(f"{k}: outside the maze's grid, not followed")
                    continue
            if to >= 0 and k not in seen:
                seen.add(k)
                route[k] = (here[0], here[1] + [(swept, kind, index)])
                new.append(k)
        return new

    later = [ident(m, "") for m in later_roots]

    def next_root():
        while later:
            k = later.pop(0)
            if k not in seen:
                seen.add(k)
                route[k] = (map_of(k), [])
                return k
        return None

    if args.reparse:
        queue = list(roots)
        while queue or later:
            if not queue:
                k = next_root()
                if k is None:
                    break
                queue.append(k)
            k = queue.pop(0)
            if os.path.exists(log_name(k)):
                queue += done(k, read_log(log_name(k)))
            else:
                notes[k] = ["no log"]
    else:
        def finished_log(k):
            path = log_name(k)
            if not args.resume or not os.path.exists(path):
                return None
            lines = read_log(path)
            return lines if any("[driver] complete" in x or DONE.search(x) for x in lines) else None

        def submit(pool, k):
            lines = finished_log(k)
            if lines is not None:
                return pool.submit(lambda: lines)
            return pool.submit(run_map, args, state, map_of(k), route[k], seed, log_name(k))

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            running = {submit(pool, k): k for k in roots}
            while running or later:
                if not running:
                    k = next_root()
                    if k is None:
                        break
                    running[submit(pool, k)] = k
                    continue
                finished, _ = wait(running, return_when=FIRST_COMPLETED)
                for fut in finished:
                    k = running.pop(fut)
                    try:
                        lines = fut.result()
                    except MemoryCapHit:
                        # Fail: start nothing more (the runs in progress end by their timeout).
                        pool.shutdown(wait=False, cancel_futures=True)
                        raise
                    new = done(k, lines)
                    print(f"[{game}] {k}: -> {sorted(edges.get(swept_of.get(k), []))} checks {len(checks[k])} "
                          f"(nodes {len(swept_of)}, maps {len(edges)}, running {len(running) + len(new)})", flush=True)
                    for x in new:
                        running[submit(pool, x)] = x
    proven = sorted({c for cs in checks.values() for c in cs})
    by_map = {}
    for k, cs in checks.items():
        by_map.setdefault(swept_of.get(k, map_of(k)), set()).update(cs)
    extra = {}
    if args.state == "hub":
        extra = {
            # Each store the chapter lock dropped: kind, value, map, script offset, and per node
            # the entries that made it.
            "lock_drops": [{"kind": kind, "value": value, "map": m, "offset": off, "nodes": hub["drops"][(kind, value, m, off)]}
                           for kind, value, m, off in sorted(hub["drops"])],
            # The chambers whose view turned on (or that have none) on arrival, in how many runs.
            "views": [{"what": what, "encounter": enc, "map": m, "runs": n} for (what, enc, m), n in sorted(hub["views"].items())],
            "won": {str(e): n for e, n in sorted(hub["won"].items())},
            "new_game": sorted(hub["games"]),
        }
    return dict(extra, **{
        "maps": sorted(edges),
        "nodes": len(swept_of),
        "edges": {str(m): sorted(edges[m]) for m in sorted(edges)},
        "routes": {k: f"{route[k][0]}|" + ",".join(f"{a}:{kk}{i}" for a, kk, i in route[k][1])
                   for k in sorted(swept_of, key=lambda x: (map_of(x), x))},
        "checks": {str(m): sorted(by_map[m]) for m in sorted(by_map) if by_map[m]},
        "proven": proven,
        "notes": {k: [f"{n} (x{c})" if c > 1 else n for n, c in collections.Counter(notes[k]).items()]
                  for k in sorted(notes)},
    })


# ---- The hub proof's result (docs/ap/hub_proof.json) ----

def hub_dungeon(row, chambers, dungeon_of_map):
    """The dungeon key group (hub_bosses.json `dungeons`) a location lies behind, or None (a town,
    a world map): a chamber's by its row, anything else by its map."""
    if row["type"] == "boss" and row["id"] in chambers:
        name = chambers[row["id"]]["dungeon"]
        return next((d for d in dungeon_of_map.values() if d["dungeon"] == name), None)
    return dungeon_of_map.get(row.get("map"))


def hub_summary(full, late_path):
    """The hub proof's rows (every location proven in either game, with its dungeon and key), the
    comparison with the late proof, the lock's drops against the story's own stores
    (scratchpad/enemy/ap5/steps.json, when present) and the dungeon doors check (every edge into
    a dungeon's maps from outside them is one of its doors in hub_bosses.json)."""
    data = load("data")
    rows = {r["id"]: r for r in list(data.LOCATIONS) + list(getattr(data, "HUB_LOCATIONS", []))}
    hb = hub_json("hub_bosses.json")
    dungeon_of_map = {m: d for d in hb["dungeons"] for m in d["maps"]}
    chambers = {b["location"]: b for b in hb["bosses"]}
    late = json.load(open(late_path)) if os.path.exists(late_path) else {"games": {}}
    games = sorted(full["games"])
    proven = {g: set(full["games"][g]["proven"]) for g in games}
    late_proven = {g: set(late["games"].get(g, {}).get("proven", [])) for g in games}
    where = {g: {} for g in games}
    for g in games:
        for m, ids in full["games"][g]["checks"].items():
            for i in ids:
                where[g].setdefault(i, set()).add(int(m))
    out_rows = []
    for i in sorted(set().union(*proven.values())):
        row = rows.get(i, {"id": i, "name": f"location {i}", "type": "?", "map": None, "region": None})
        d = hub_dungeon(row, chambers, dungeon_of_map)
        kind = row["type"] + (f" ({row['boss']})" if row["type"] == "boss" and row.get("boss") else "")
        if row["type"] == "boss" and i in chambers:
            kind = "boss (chamber)"
        out_rows.append({"id": i, "name": row["name"], "type": kind, "map": row.get("map"), "region": row.get("region"),
                         "games": [g for g in games if i in proven[g]],
                         "dungeon": d["dungeon"] if d else None, "key": d["key"] if d and d["dungeon"] != "Fienal" else None,
                         "late": [g for g in games if i in late_proven[g]],
                         "checked_on": sorted(set().union(*(where[g].get(i, set()) for g in games)))})
    comparison = {}
    for g in games:
        maps = set(full["games"][g]["maps"])
        lmaps = set(late["games"].get(g, {}).get("maps", []))

        def brief(i, reached, other):
            """why: the other proof's view (gained: the late proof's; lost: the hub's)."""
            row = rows.get(i, {})
            m = row.get("map")
            why = ("" if m is None else f"{other}: map not reached" if m not in reached
                   else f"{other}: map reached, not checked")
            if row.get("type") == "boss" and i in chambers:
                why = f"a chamber ({other}: " + ("its boss fought there)" if other == "hub" else "no scene at chapter 60)")
            return {"id": i, "name": row.get("name"), "map": m, "why": why}
        comparison[g] = {"hub": len(proven[g]), "late": len(late_proven[g]),
                         "gained": [brief(i, lmaps, "late proof") for i in sorted(proven[g] - late_proven[g])],
                         "lost": [brief(i, maps, "hub") for i in sorted(late_proven[g] - proven[g])],
                         "maps_gained": sorted(maps - lmaps), "maps_lost": sorted(lmaps - maps)}
    full["rows"] = out_rows
    full["comparison"] = comparison
    full["counts"] = {g: dict(collections.Counter(r["type"] for r in out_rows if g in r["games"])) for g in games}
    # The lock's drops: the story's own store at that place (steps.json: its conditions), and
    # whether only flags guard it (story_flags.md 4.4 P2: the known ones).
    steps_path = os.path.join(ROOT, "scratchpad", "enemy", "ap5", "steps.json")
    steps = {}
    if os.path.exists(steps_path):
        for st in json.load(open(steps_path)):
            for m in st.get("maps", [st["map"]]):
                steps[(m, st["offset"])] = st
    drops = {}
    for g in games:
        for d in full["games"][g].get("lock_drops", []):
            key = (d["kind"], d["value"], d["map"], d["offset"])
            x = drops.setdefault(key, {"kind": d["kind"], "value": d["value"], "map": d["map"], "offset": d["offset"],
                                       "games": [], "entries": set()})
            x["games"].append(g)
            for ents in d["nodes"].values():
                x["entries"] |= set(ents)
    lock = []
    for key in sorted(drops):
        x = drops[key]
        st = steps.get((x["map"], int(x["offset"], 16)))
        conds = st["conds"] if st else None
        x["entries"] = sorted(x["entries"])
        x["story_store"] = conds
        x["flag_guarded"] = None if conds is None else not any(re.search(r"\b(chapter|step)\b", c) for c in conds)
        lock.append(x)
    full["lock_drops"] = lock
    # Every edge into a dungeon from outside its maps should be one of its doors (a key bypass
    # otherwise; the scenes' vision returns inside a view are expected: see the log).
    doors = {(x.get("world") or x["map"], x["to"]) for d in hb["dungeons"] for x in d["doors"]}   # (a world-map entrance: from 1 / 2)
    entries = []
    for g in games:
        for a, tos in full["games"][g]["edges"].items():
            a = int(a)
            for b in tos:
                d = dungeon_of_map.get(b)
                if d and dungeon_of_map.get(a) is not d and (a, b) not in doors:
                    entries.append({"game": g, "from": a, "to": b, "dungeon": d["dungeon"]})
    merged = {}
    for e in entries:
        merged.setdefault((e["from"], e["to"], e["dungeon"]), []).append(e["game"])
    full["unlisted_dungeon_entries"] = [{"from": a, "to": b, "dungeon": d, "games": gs}
                                        for (a, b, d), gs in sorted(merged.items())]


def hub_main(args):
    """--state hub: the late proof from the hub's New Game (docs/handoff/hubproof.md)."""
    games = args.game or ["claude", "rena"]
    start = hub_start_row(args.start)
    stateful = stateful_maps(args.disc1)
    for m, reads in HUB_STATEFUL.items():
        stateful[m] = set(stateful.get(m, ())) | reads
    args.root_pos = {}
    roots, later = [start["map"]], []
    for spec in args.extra_root or []:   # MAP@X,Y,Z,FACING: an arrival a walk reaches (the sweep can't)
        m, _, pos = spec.partition("@")
        later.append(int(m))
        if pos:
            args.root_pos[int(m)] = pos
    full = json.load(open(args.out)) if os.path.exists(args.out) else {}
    full["about"] = ("Locations proven reachable in the hub (world_mode: hub), per game: tools/ap_late_proof.py "
                     "--state hub (docs/handoff/hubproof.md; docs/ap/apworld.md 'Hub mode'). The late proof's search "
                     "from the hub's New Game state (hub_state.json, built by the port: SS_HUB_STATE) with a hub seed "
                     "(every chamber row of hub_bosses.json, the chapter lock on, no door locked) and the start map as "
                     "the root; the party is the hero alone. 'rows': every location proven, its dungeon and key; "
                     "'comparison': against late_proof.json; 'lock_drops': every chapter / step store the lock dropped.")
    full.setdefault("games", {})
    hb_text = open(os.path.join(ROOT, "docs", "ap", "hub_bosses.json"), "rb").read()
    hs_text = open(os.path.join(ROOT, "docs", "ap", "hub_state.json"), "rb").read()
    doors = json.load(open(args.hub_doors)) if args.hub_doors else hub_p5_doors()
    for game in games:
        seed = os.path.join(os.path.abspath(args.work), f"hub_{game}.apso2")
        count = hub_proof_seed(seed, game, start, doors)
        glob_text, flag_text, summary = hub_env_state(game, start)
        port = args.hub_new_game == "port"
        hero_env = {"SS_HUB_STATE": ("1" if game == "claude" else "rena") if port else None,
                    # The hero alone (New Game's party): a --map start is Claude's.
                    "SS_SWEEP_PARTY": None if game == "claude" else "1",
                    "SS_SWEEP_PARTY_DROP": None if game == "claude" else "0"}
        if port:   # the port writes the state; the sweep only clears flag 1 (none of the env's)
            hero_env.update({"SS_SCRIPT_GLOBALS": None, "SS_SWEEP_FLAGS": None})
        try:
            result = bfs(args, game, (glob_text, flag_text, hero_env), seed, stateful, roots, later)
        except MemoryCapHit as e:
            print(f"[{game}] FAILED at the memory cap: {e}", file=sys.stderr, flush=True)
            return 3
        want = "Claude" if game == "claude" else "Rena"
        if port and result.get("new_game") != [want]:
            print(f"[{game}] FAILED: the port built {result.get('new_game')} at New Game, not {want}'s game "
                  f"(SS_HUB_STATE={hero_env['SS_HUB_STATE']})", file=sys.stderr, flush=True)
            return 4
        used = sorted({int(r.split("|")[0]) for r in result["routes"].values()})
        result["state"] = dict(summary, roots=roots, extra_roots=[m for m in later if m in used],
                               extra_roots_unused=[m for m in later if m not in used],
                               root_positions={str(k): v for k, v in args.root_pos.items()},
                               start=start["key"], new_game="port (SS_HUB_STATE)" if port else "env (SS_SCRIPT_GLOBALS / SS_SWEEP_FLAGS)",
                               party="the hero alone", frames=args.frames, seed_locations=count, lock=True,
                               hub_bosses_md5=hashlib.md5(hb_text).hexdigest(), hub_state_md5=hashlib.md5(hs_text).hexdigest(),
                               doors=doors, stateful_maps={str(m): sorted(v) for m, v in sorted(stateful.items())})
        result["generated"] = datetime.date.today().isoformat()
        full["games"][game] = result
        print(f"[{game}] {len(result['maps'])} maps, {len(result['proven'])} locations proven, "
              f"{len(result['lock_drops'])} lock drops", flush=True)
        hub_summary(full, args.late)
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            json.dump(full, f, indent=1, sort_keys=True)
            f.write("\n")
    return 0


# ---- --state hub --bosses: the chambers from the hub's New Game (docs/handoff/hubproof.md) ----

def hub_route_to(result, target):
    """The hub proof's shortest route (root, [(map, kind, index)]) to a map, or None."""
    best = None
    for node, text in result.get("routes", {}).items():
        if int(node.split("@")[0]) != target:
            continue
        root, _, steps = text.partition("|")
        route = [(int(a), kk[0], int(kk[1:])) for a, kk in (x.split(":") for x in steps.split(",") if x)]
        if best is None or len(route) < len(best[1]):
            best = (int(root), route)
    return best


def hub_bosses_seed(path, game, start, key=None):
    """A hub seed as the APWorld writes one (data.HUB_DUNGEONS' doors locked with their keys, the P5
    doors, every chamber row with its dungeon's key), every chamber's boss location, and `key` (a
    dungeon key number) in the starting inventory. Fienal's gate is left out (its requirement is
    hubcore's door test; here every Fienal chamber is open)."""
    data = load("data")
    hub = hub_json("hub_state.json")["hub"]
    rows = hub_json("hub_bosses.json")["bosses"]
    groups = {g["dungeon"]: g for g in data.HUB_DUNGEONS}
    keyless = ("Fienal", "Portal of Sealing")
    doors = []
    for g in data.HUB_DUNGEONS:
        if g["dungeon"] in keyless:
            continue
        for d in g["doors"]:
            door = {"id": d["id"], "key": g["key"], "routine": d["routine"], "name": f"{g['dungeon']} (map {d['map']} -> {d['to']})"}
            doors.append({k: v for k, v in door.items() if v is not None})
    doors += hub_p5_doors()
    names = {g["key"]: g["name"] for g in data.HUB_DUNGEONS}
    boss_keys = ("encounter", "name", "map", "maps", "view", "game", "location", "dungeon", "trigger", "late")
    bosses = []
    ask = hub_ask()
    for b in rows:
        g = groups.get(b["dungeon"])
        bosses.append(dict({k: b[k] for k in boss_keys if b.get(k) is not None},
                           keys=[g["key"]] if g and g["dungeon"] not in keyless else [],
                           **({"ask": ask[b["encounter"]]} if b["encounter"] in ask else {})))
    filler = {"player": 1, "kind": "item", "game_id": 757, "count": 1, "name": "Blueberry", "classification": "filler"}
    seed = {"format": "apso2", "version": 3, "seed": f"hub-bosses-{game}", "game": "Star Ocean: The Second Story",
            "slot": {"name": "Proof", "id": 1},
            "options": {"world_mode": "hub", "story_route": game, "character_items": True, "party_rules": "open",
                        "expel_teleporter": False, "hub_start": start["key"], "hub_start_town": start["key"]},
            "items": {str(data.ITEM_BASE + 757): {"name": "Blueberry", "kind": "item", "game_id": 757}},
            "locations": [{"id": b["location"], "name": b["name"], "check": {"type": "boss", "encounter": b["encounter"]},
                           "item": dict(filler)} for b in rows],
            "patches": [],
            "start": {"items": ([{"kind": "key", "key": key, "name": names[key], "count": 1}] if key else []),
                      "fol": 0, "flags": []},
            "goal": {"encounters": list(data.GOAL_ENCOUNTERS)},
            "doors": doors,
            "hub": dict({k: hub[k] for k in ("chapter", "step", "disc", "flags", "globals", "games", "lock")},
                        start=dict(start)),
            "bosses": bosses}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(seed, f, indent=1)
    return {d["id"]: d for d in doors}


# The title -> New Game -> the hero (the cursor: right Claude, left Rena) -> names and opening
# skipped: the field's free play at the hub start by about vblank 3100 (scratchpad/hubcore/run_ng.py).
NEW_GAME_INPUT = ("850:start,854:none,951:cross,957:none,1251:cross,1257:none,1551:cross,1557:none,1851:cross,"
                  "1857:none,2000:{hero},2006:none,2151:start,2157:none,2451:cross,2457:none,2700:start,2706:none,"
                  "3000:start,3006:none,3300:start,3306:none")


def bosses_main(args):
    """Every chamber spec of tools/ap_hub_chambers.py (CHAMBERS: the approach map, the steps into
    the chamber and its scene, what the scene may change) played from the hub's New Game: the hub
    proof's route from the start (docs/ap/hub_proof.json) to the approach map, through the
    dungeon's door with its key in the starting inventory (no "locked" refusal), then the spec's
    steps. Asserts the story view (or its absence), the battle and the stub's victory, the check,
    goal_won, the play reaching its last step and the story at 60 / 4000; the flag and word diff of
    hubchambers' spec is reported (the route's own maps may add to it). Results: the output's
    "bosses" key; a hub_bosses.json row without a spec is "pending"."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ap_hub_chambers as hc
    full = json.load(open(args.out))
    hb = hub_json("hub_bosses.json")
    rows = {b["encounter"]: b for b in hb["bosses"]}
    groups = {g["dungeon"]: g for g in load("data").HUB_DUNGEONS}
    start = hub_start_row(args.start)
    only = {x for x in (args.only or "").split(",") if x}
    specs = [sp for sp in hc.CHAMBERS if not only or sp["key"] in only or str(sp["enc"]) in only]
    work = os.path.join(os.path.abspath(args.work), "bosses")
    os.makedirs(os.path.join(work, "runs"), exist_ok=True)
    os.makedirs(os.path.join(work, "no-mods"), exist_ok=True)
    out = full.setdefault("bosses", {})

    def one(sp):
        game = sp["game"]
        if not args.refire:   # (hubchambers' re-fire sweep after the play: --refire)
            sp = {k: v for k, v in sp.items() if k != "sweep_after"}
        if sp.get("pos"):   # (a spec for one arrival spot: the route arrives where its door puts the party)
            return sp, None, [(True, "skipped: a spec for an arrival position (--map-pos)")], {}
        goal = sp["enc"] not in rows   # the goal fight (Fienal's top: 671 -> 684 -> 685), no chamber row
        row = rows.get(sp["enc"], {"location": -1, "dungeon": "Fienal", "name": "the final boss"})
        result = full["games"].get(game)
        if result is None:
            return sp, None, [(False, f"no hub proof for {game}'s game")], {}
        found = hub_route_to(result, sp["map"])
        if found is None:
            return sp, None, [(False, f"the hub proof has no route to map {sp['map']}")], {}
        root, route = found
        g = groups.get(row["dungeon"])
        key = g["key"] if g and g["dungeon"] not in ("Fienal", "Portal of Sealing") else None
        seed = os.path.join(work, f"seed_{sp['key']}.apso2")
        doors = hub_bosses_seed(seed, game, start, key)
        steps = [f"{m}:{k}{i}" for m, k, i in route] + list(sp["play"])
        log = os.path.join(work, "runs", f"{sp['key']}.log")
        pre, post = os.path.join(work, "runs", f"{sp['key']}.pre"), os.path.join(work, "runs", f"{sp['key']}.post")
        for x in (pre, post):
            if os.path.exists(x):
                os.remove(x)
        env = dict(os.environ)
        glob_text, flag_text, _ = hub_env_state(game, start)
        env.update(SS_MODS_DIR=os.path.join(work, "no-mods"), SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy",
                   SS_EVENT_SWEEP="1", SS_BATTLE_STUB="1", SS_EVENT_SWEEP_DISC="2", SS_DISC1=args.disc1,
                   SS_DISC2=args.disc2, SS_HUB_TRACE="1", SS_SWEEP_PLAY=",".join(steps),
                   SS_STATE_DUMP=f"30:{pre}", SS_SWEEP_PLAY_DUMP=post,
                   XDG_CONFIG_HOME=os.path.join(os.path.abspath(args.work), "xdg"))
        for k in ("SS_SWEEP_PLAY_SWEEP", "SS_SWEEP_PARTY", "SS_SWEEP_PARTY_DROP", "SS_SCRIPT_GLOBALS", "SS_SWEEP_FLAGS",
                  "SS_HUB_STATE"):
            env.pop(k, None)
        if args.hub_new_game == "port":
            env["SS_HUB_STATE"] = "1" if game == "claude" else "rena"
        else:
            env.update(SS_SCRIPT_GLOBALS=glob_text, SS_SWEEP_FLAGS=flag_text)
        if game == "rena":
            env.update(SS_SWEEP_PARTY="1", SS_SWEEP_PARTY_DROP="0")
        for k, v in sp.get("env", {}).items():
            if k == "FLAGS+":
                env["SS_SWEEP_FLAGS"] = (env.get("SS_SWEEP_FLAGS", "") + "," + v).strip(",")
            else:
                env[k] = v
        frames = sp.get("frames", 12000) + 600 * len(route) + 2000
        if sp.get("sweep_after"):   # (hubchambers' re-fire test: the room's active entries swept after the play)
            env.update(SS_SWEEP_PLAY_SWEEP="1", SS_EVENT_SWEEP_FRAMES="1500")
            frames += 30000
        cmd = [args.bin, "--headless", "--no-memcard", "--debug", "--ap-seed", seed]
        if root == start["map"]:
            # The title's New Game (the hero picked with the cursor), as a player starts a hub seed:
            # the port builds the hub state and puts the party at the start (P1).
            env.pop("SS_HUB_STATE", None)
            env.pop("SS_SCRIPT_GLOBALS", None)
            env.pop("SS_SWEEP_FLAGS", None)
            env.pop("SS_SWEEP_PARTY", None)
            env.pop("SS_SWEEP_PARTY_DROP", None)
            frames += 3300
            cmd += ["--frames", str(frames), "--input", NEW_GAME_INPUT.format(hero="right" if game == "claude" else "left")]
        else:   # an extra root (the tower's walk): the hub state on a --map start (SS_HUB_STATE)
            cmd += ["--frames", str(frames), "--map", str(root)]
            if root in getattr(args, "root_pos", {}):
                cmd += ["--map-pos", args.root_pos[root]]
        with open(log, "w") as f:
            f.write("[driver] " + " ".join(cmd) + "\n[driver] SS_SWEEP_PLAY " + env["SS_SWEEP_PLAY"] + "\n")
            f.flush()
            rc, timed_out, at_cap = run_capped(cmd, args.bin, timeout=2400, stdout=f, stderr=f, env=env,
                                               stdin=subprocess.DEVNULL)
        if at_cap:
            raise MemoryCapHit(f"bosses {sp['key']}: a run died at the memory cap; see {log}")
        note = ("timeout " if timed_out else "") + (f"rc {rc}" if rc else "")
        ev = hc.parse_log(log)
        diff = hc.dump_diff(pre, post, game) if os.path.exists(pre) and os.path.exists(post) else None
        # The spec's own events: from its first step on (a route through the chamber's maps turns
        # its view on and off on the way: Arcmene's 417 lies between 415 and 418), with the view
        # still on from the route when the approach is one of its maps (the tower).
        cut = next((n for n, (k, d) in enumerate(ev) if k == "step" and int(d[1]) == len(route)), len(ev))
        if any(k == "battle_end" and int(d[0]) == sp["enc"] and d[1] == "0" for k, d in ev[:cut]):
            # The shortest way to this spec's approach passes the chamber and wins its boss there
            # (Cyril's west side 679 lies behind his room 674 from below): another spec covers it.
            return sp, None, [(True, "skipped: the route to the approach passes the chamber and wins the boss "
                                     "on the way (this arrival lies behind it from the start)")], \
                {"route": f"{root}|" + ",".join(f"{m}:{k}{i}" for m, k, i in route)}
        before = [(k, d) for k, d in ev[:cut] if k in ("view_on", "view_off") and int(d[2]) == sp["enc"]]
        own = ev[cut:]
        if before and before[-1][0] == "view_on":
            own = [before[-1]] + own
        # (the play's step count and its last control map are the whole run's)
        own += [(k, d) for k, d in ev[:cut] if k == "play_done"] if not any(k == "play_done" for k, _ in own) else []
        res = hc.check_chamber(dict(sp, play=steps, play_steps=sp.get("play_steps", len(sp["play"])) + len(route)),
                               row, own, diff, note.strip())
        text = open(log, errors="replace").read()
        # The way in: the route's door into the chamber's dungeon (a keyed door of this seed), the key
        # given at the start, no door refused.
        own = set(g["maps"]) if g else set()
        seq = [root] + [int(x) for x in re.findall(r"\[sweep\] map \d+ vblank \d+ now on map (\d+)", text)]
        crossing = [(a, b) for a, b in zip(seq, seq[1:]) if b in own and a not in own]
        if root == start["map"]:
            want = "Claude" if game == "claude" else "Rena"
            res.append((f"[ap] hub New Game: {want}'s game" in text and f"; {start['name']} (map {start['map']}" in text,
                        f"new game: the title's New Game built {want}'s hub at {start['name']}"))
        locked = re.findall(r"\[ap\] door (\d+)[^\n]*: locked, needs ([^\n]*)", text)
        res.append((not locked, "no door refused" if not locked else f"refused: {locked[:2]}"))
        if key:
            res.append((f"[ap] key {key} (" in text, f"key {key} ({groups[row['dungeon']]['name']}) held from the start"))
            res.append((bool(crossing) or root in own, f"route enters {row['dungeon']}: "
                        + (", ".join(f"{a} -> {b}" for a, b in crossing[:3])
                           or ("starts inside (an extra root: a walk the sweep can't fire)" if root in own else "never"))))
        required = ("view", "no view", "battle", "check", "goal_won", "play", "story", "no door", "key", "route enters",
                    "ends on", "step ", "also won", "run:", "new game")
        if goal or sp.get("walk"):   # a walk (the goal's: Fienal's top 671 -> 684 -> 685): reached, its battle
            required = ("reached", "battle", "no door", "run:", "new game")
        info = {"route": f"{root}|" + ",".join(f"{m}:{k}{i}" for m, k, i in route), "key": key,
                "diff": diff and {k: (v if k != "words" else {f"{a:#x}": x for a, x in v.items()}) for k, v in diff.items()}}
        return sp, required, res, info

    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 2))) as pool:
        for sp, required, res, info in pool.map(one, specs):
            if res and res[0][1].startswith("skipped"):
                out[sp["key"]] = {"encounter": sp["enc"], "game": sp["game"], "result": "SKIPPED", "asserts": [res[0][1]]}
                print(f"SKIP {sp['key']:14s} {sp['enc']:3d} {sp['game']:6s} | {res[0][1]}", flush=True)
                continue
            hard = [r for r in res if required is None or r[1].startswith(required)]
            ok = all(r[0] for r in hard)
            out[sp["key"]] = {"encounter": sp["enc"], "game": sp["game"], "result": "PASS" if ok else "FAIL",
                              "asserts": [("ok " if r[0] else "FAIL ") + r[1] for r in hard],
                              "info": [("" if r[0] else "(differs) ") + r[1] for r in res if r not in hard], **info}
            print(f"{'PASS' if ok else 'FAIL'} {sp['key']:14s} {sp['enc']:3d} {sp['game']:6s} | "
                  + "; ".join(("" if r[0] else "!! ") + r[1] for r in hard if not r[0] or args.verbose), flush=True)
    covered = {(e, sp["game"]) for sp in hc.CHAMBERS for e in [sp["enc"]] + list(sp.get("also", []))}
    pending = []
    for b in hb["bosses"]:
        for game in ((b["game"],) if b.get("game") else ("claude", "rena")):
            if (b["encounter"], game) not in covered:
                pending.append(f"{b['encounter']} {b['name']} ({game})")
    full["bosses_pending"] = pending
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(full, f, indent=1, sort_keys=True)
        f.write("\n")
    fails = [k for k, v in out.items() if v["result"] == "FAIL"]
    print(f"{sum(1 for v in out.values() if v['result'] == 'PASS')}/{len(out)} chamber runs pass; pending (no spec): "
          f"{len(pending)}", flush=True)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(ROOT, "engine", "build", "shattered-stars"))
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    ap.add_argument("--disc2", default=os.environ.get("SS_DISC2"))
    ap.add_argument("--save", default=os.path.join(ROOT, "scratchpad", "latesave", "card.mcd"),
                    help="a memory card holding a save at the Final Save Point (map 671)")
    ap.add_argument("--save-frame", type=int, default=13, help="the save's directory frame on the card")
    ap.add_argument("--game", action="append", choices=["claude", "rena"])
    ap.add_argument("--jobs", type=int, default=3,
                    help="game processes at once (keep the machine's total at 6: two games at 3 each)")
    ap.add_argument("--frames", type=int, default=2400)
    ap.add_argument("--stall", type=float, default=120.0, help="seconds without a sweep line before a run is killed")
    ap.add_argument("--timeout", type=float, default=900.0, help="seconds a run may take at most")
    ap.add_argument("--resume", action="store_true", help="reuse the finished node logs in --work")
    ap.add_argument("--work", default=os.path.join(ROOT, "scratchpad", "late_proof"))
    ap.add_argument("--reparse", action="store_true", help="rebuild the result from the logs in --work")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "docs", "ap", "late_proof.json"))
    ap.add_argument("--state", choices=["late", "teleporter", "voyage", "hub"], default="late",
                    help="late: the Final Save Point; teleporter: chapter 38 with the Expel teleporter (open world); "
                         "voyage: chapter 27 step 7000, at the El voyage's gate (open world); hub: the hub's New Game "
                         "(world_mode hub: docs/ap/hub_proof.json)")
    ap.add_argument("--start", default="fun_city", help="hub: the start (hub_state.json `starts` key): its map is the root")
    ap.add_argument("--extra-root", action="append",
                    help="hub: another root MAP[@X,Y,Z,FACING] (an arrival a walk reaches that the sweep can't fire)")
    ap.add_argument("--hub-new-game", choices=["port", "env"], default="port",
                    help="hub: the port builds the New Game state (SS_HUB_STATE) or the sweep's env does (as the late proof)")
    ap.add_argument("--hub-doors", help="hub: a JSON list of seed door rows for the proof seed (default: the P5 "
                                        "redirects, apworld/so2/hub.py P5_DOORS)")
    ap.add_argument("--late", default=os.path.join(ROOT, "docs", "ap", "late_proof.json"),
                    help="hub: the late proof to compare with")
    ap.add_argument("--bosses", action="store_true",
                    help="hub: play every chamber spec (tools/ap_hub_chambers.py) from the hub's New Game along the "
                         "hub proof's routes, with the dungeon's key (needs the hub proof in -o)")
    ap.add_argument("--only", help="--bosses: chamber keys or encounters, comma-separated")
    ap.add_argument("--refire", action="store_true", help="--bosses: also hubchambers' re-fire sweep after the scene")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    tele = args.state == "teleporter"
    voyage = args.state == "voyage"
    if args.state == "hub":
        if args.work == os.path.join(ROOT, "scratchpad", "late_proof"):
            args.work = os.path.join(ROOT, "scratchpad", "hubproof", "hub_proof")
        if args.out == os.path.join(ROOT, "docs", "ap", "late_proof.json"):
            args.out = os.path.join(ROOT, "docs", "ap", "hub_proof.json")
    elif args.state != "late" and args.work == os.path.join(ROOT, "scratchpad", "late_proof"):
        args.work = os.path.join(ROOT, "scratchpad", "late_proof", args.state)
    games = args.game or ["claude", "rena"]
    if not args.disc1:
        sys.exit("set SS_DISC1 (or --disc1)")
    if not args.disc2:
        args.disc2 = args.disc1.replace("Disc 1", "Disc 2")
    os.makedirs(os.path.join(args.work, "xdg"), exist_ok=True)
    if args.state == "hub" and args.bosses:
        args.root_pos = {}
        for spec in args.extra_root or []:
            m, _, pos = spec.partition("@")
            if pos:
                args.root_pos[int(m)] = pos
        return bosses_main(args)
    if args.state == "hub":
        return hub_main(args)
    field = json.load(open(os.path.join(ROOT, "docs", "ap", "field.json")))
    snap = save_snapshot(args.save, args.save_frame)
    if struct.unpack_from("<h", snap, 0x1A)[0] != 671:
        sys.exit("the save is not at the Final Save Point (map 671)")
    stateful = stateful_maps(args.disc1)
    seed = os.path.join(os.path.abspath(args.work), "proof.apso2")
    count = proof_seed(seed, teleporter=tele)
    full = json.load(open(args.out)) if os.path.exists(args.out) else {}
    if tele:
        out = full.setdefault("teleporter", {})
        out["about"] = ("Locations proven reachable at chapter 38 (after the disc switch) through the Expel "
                        "teleporter, per game: tools/ap_late_proof.py --state teleporter (docs/ap/apworld.md, "
                        "'Open world'). Open-world seeds put progression in Expel only where this proves it.")
    elif voyage:
        out = full.setdefault("voyage", {})
        out["about"] = ("Locations proven reachable at chapter 27 step 7000, the last free play before the El "
                        "voyage (the Lacour Front Line's meeting room, map 444), per game: tools/ap_late_proof.py "
                        "--state voyage (docs/ap/apworld.md, 'Open world'). In open world an Expel location the "
                        "story reaches before the voyage holds progression only where this proves it.")
    else:
        out = full
        out["about"] = ("Locations proven reachable after the Final Save Point, per game: tools/ap_late_proof.py "
                        "(docs/ap/apworld.md, 'Logic: the late proof'). apworld/so2/rules.py keeps progression "
                        "out of every other location.")
    out.setdefault("games", {})
    roots = TELEPORTER_ROOTS if tele else VOYAGE_ROOTS if voyage else ROOTS
    for game in games:
        glob_text, flag_text, summary = build_state(snap, field, game, args.state)
        try:
            result = bfs(args, game, (glob_text, flag_text), seed, stateful, roots)
        except MemoryCapHit as e:
            print(f"[{game}] FAILED at the memory cap: {e}", file=sys.stderr, flush=True)
            return 3
        result["state"] = dict(summary, roots=roots, disc=1 if voyage else 2, frames=args.frames, seed_locations=count,
                               expel_teleporter=tele,
                               stateful_maps={str(m): sorted(v) for m, v in sorted(stateful.items())})
        result["generated"] = datetime.date.today().isoformat()
        out["games"][game] = result
        print(f"[{game}] {len(result['maps'])} maps, {len(result['proven'])} locations proven", flush=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            json.dump(full, f, indent=1, sort_keys=True)
            f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
