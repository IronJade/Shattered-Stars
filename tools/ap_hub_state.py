#!/usr/bin/env python3
"""The hub's New Game state (docs/ap/seed_format.md "Hub (`world_mode: hub`)", docs/handoff/hub.md
D1): docs/ap/hub_state.json, from the late proof's state.

The late proof (tools/ap_late_proof.py, docs/ap/apworld.md "Logic") plays from the user's save at
the Final Save Point (a Claude game: chapter 60, step 4000, flag 730, disc 2) with every
location's done flags cleared; Rena's game is the same with flag 1 clear and the story_route
bytes swapped. The hub starts New Game in that state, but it must not inherit what belongs to
one player's game rather than to "the story has passed": this tool sorts every flag the save
has set (and every flag the hub changes) into

  story          set by a story scene (its routine sits under a chapter / step test), or one of
                 the story's own state flags (route, Virtual Expel, the Synard, the ships ...):
                 kept, the story is passed;
  chest          a chest's opened flag: cleared (a location);
  gift_guard     a gift's guard flags: cleared (a location), as the late proof does;
  boss_guard     a hub chamber's guard (the flags its scene tests, at the value that leaves the
                 boss waiting, and the flag its scene sets when won), the Fields' done flags
                 162-165, the Cave of Trials' guards (as the late proof) and the optional bosses'
                 done flags: reset so every boss waits;
  pa             set by a Private Action (relationship and PA-chain conditions): cleared, a new
                 game's (relationship values themselves live in config +0x58 / +0xe8, which
                 the hub never touches: New Game's);
  ending         the ending flags (0x2ee-0x2f1), the special ending pairs' flags (0x1ab
                 Celine + Chris, 0x163 Ashton + Eleanor) and Limiter Off (556, set only by
                 Filia's scene after the Final Save Point; the limiter-off goals need it clear):
                 cleared;
  other          everything else (one-time talks, puzzle and door state, quest flags without a
                 story test, key items): kept as the late proof proved it, except the key items
                 a hub chamber's scene gives (cleared: the boss gives them).

and every script global word of the save into the same kind of table (story words, the route
bytes per game, quest state, the engine's statistics and the vehicles' parking spots, which the
hub sets to the game's own spots next to Arlia and the start town).

Usage:
  tools/ap_hub_state.py [--save scratchpad/latesave/card.mcd] [--save-frame 13]
                        [--disc1 CUE] [-o docs/ap/hub_state.json]
SS_DISC1 gives the disc (the map scripts: who sets each flag, under which story test). No game
run: the state is read from the card as the late proof reads it.
"""
import argparse
import collections
import datetime
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ap_extract as A  # noqa: E402
import ap_late_proof as L  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_FLAGS = L.SCRIPT_FLAGS
FLAG_STORE = re.compile(r"^store flag G#(0x[0-9a-f]+) = (0x[0-9a-f]+)")
FLAG_READ = re.compile(r"flag G#(0x[0-9a-f]+)")
WORLD_SCRIPT_MAPS = {0, 1, 2, 3, 826}   # the world script: its warp list is not play

# The hub chambers (docs/handoff/hub.md D2 / D5 / D10; tools/ap_hub_bosses.py has the rows):
# every story boss but the four losable fights (Zand 265, Dias 204 and the tournament, Shin on
# the ship 517, Marsilio's first Fienal fight 530), plus the Cross Cave guardians (set battle
# 523) and Fienal's three pairs. The final boss (539 / 540) is the goal, not a chamber: its
# scene plays at the hub's own chapter 60 step 4000.
HUB_ENCOUNTERS = [191, 523, 194, 195, 209, 210, 516, 519, 520, 521, 522, 524, 525, 527, 528, 531,
                  532, 533, 535, 536, 537, 538]
LOSABLE = [265, 204, 517, 530]
OPTIONAL_BOSSES = [196, 197, 199, 211]   # Ashton's and Opera's quests: their own guards
# The four Fields' done flags (Intelligence 162 at 556, Power 163 at 604, Courage 164 at 642,
# Love 165 at 596): each Field's last scene tests the other three before storing chapter 46.
FIELD_DONE = [162, 163, 164, 165]
ENDING = {750: "credits shown (0x2ee)", 751: "ending flow (0x2ef)", 752: "final screen (0x2f0)",
          753: "ending mode / auto text (0x2f1)", 427: "Celine + Chris special ending (0x1ab, map 78)",
          355: "Ashton + Eleanor special ending (0x163, map 156)",
          556: "Limiter Off (Filia's scene, map 239, after the Final Save Point); picks encounter 540"}
# The story's own state flags (story_flags.md 1.1), kept whatever their setters look like.
STORY_STATE = {1: "story route (set: Claude's game)", 11: "the Synard (vehicle 1)", 12: "riding the Synard",
               14: "Nede world-map terrain mode", 17: "ship Herlie -> Hilton", 18: "ship landing (Hilton)",
               21: "ship Hilton -> map 231", 22: "ship landing (map 231)", 112: "Celine's party bookkeeping",
               730: "Virtual Expel (the Final Save Point)"}
PA_MODE = {2, 4}          # Private Action mode (common flags; clear outside a PA)
SCENE_FLAG = 745          # 0x2e9: every map's set-up clears it, scenes set it (common flag)
KEY_ITEMS = range(66, 94)  # given as item 10000 + n by the shared give routine; never tested

# The globals (G[addr] = game_state + addr - 0xa0), per 4-byte word of the save.
SYNARD_EXPEL = (51209, -150, 17464, 90)   # Arlia's own spot: Arlia 17 / 19 and Virtual Arlia (18 0x5a4)
# Start points (D9). The arrival of the town's world-map entrance, and the town's own Synard
# spot on its world (table A 0x19 set_point with constant arguments in the town's script). A
# start on Expel keeps Energy Nede's spot at Fun City, where the arena's old man lands.
STARTS = {
    "fun_city": {"name": "Fun City", "world": 2, "door": 68686160, "map": 279, "x": 470, "y": 0, "z": 68, "dir": 180,
                 "synard": (81546, -258, 33880, 176), "synard_from": "map 279 0xa06c"},
    "arlia": {"name": "Arlia", "world": 1, "door": 59576065, "map": 17, "x": -126, "y": 0, "z": -614, "dir": 90,
              "synard": SYNARD_EXPEL, "synard_from": "map 17 0x1570, Virtual Arlia map 18 0x5a4"},
}


def load_maps(disc1):
    _, maps = A.load_maps(disc1)
    return maps


def map_of(maps):
    """{map id: Map} including every map of a shared script."""
    out = {}
    for mp in maps.values():
        for m in getattr(mp, "maps", [mp.id]):
            out[m] = mp
    return out


def flag_index(maps, lib=None):
    """{flag: [setter]} (each store of a global flag with its map, offset, value, the chapter
    window its conditions allow, and whether it sits in a library routine: `lib` = the library
    shapes, ap_extract.library) and {flag: {maps reading it}}."""
    setters, readers = collections.defaultdict(list), collections.defaultdict(set)
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            t = i[2]
            mo = FLAG_STORE.match(t)
            if mo:
                k = mp.func_of[n]
                ctx = A.context(mp, k, n, depth=3)
                conds = A.all_conditions(ctx)
                lo, hi = A.story_window(conds)
                story = any(re.search(r"\b(chapter|step)\b", c) for c in conds)
                setters[int(mo.group(1), 16)].append({
                    "map": mp.id, "offset": i[0], "value": int(mo.group(2), 16), "window": [lo, hi],
                    "story": story, "world_script": mp.id in WORLD_SCRIPT_MAPS,
                    "lib": bool(lib) and mp.shapes[k] in lib})
            elif "flag G#" in t:
                for m2 in FLAG_READ.finditer(t):
                    readers[int(m2.group(1), 16)].add(mp.id)
    return setters, readers


def routine_closure(mp, k, depth=3, seen=None):
    """Routine k of mp and the map routines it calls (not library ones), depth levels down."""
    seen = set() if seen is None else seen
    if k in seen or depth < 0:
        return seen
    seen.add(k)
    s, e = mp.funcs[k]
    for n in range(s, e):
        if mp.ins[n][2].startswith("call "):
            tk = mp.func_at.get(mp.ins[n][3])
            if tk is not None:
                routine_closure(mp, tk, depth - 1, seen)
    return seen


def stores_in(mp, ks):
    """{flag: value} stored by the routines ks (the last store wins)."""
    out = {}
    for k in sorted(ks):
        s, e = mp.funcs[k]
        for n in range(s, e):
            mo = FLAG_STORE.match(mp.ins[n][2])
            if mo:
                out[int(mo.group(1), 16)] = int(mo.group(2), 16)
    return out


def gives_in(mp, ks):
    """Key items (10000 + n) the routines ks give through the shared give routine."""
    out = set()
    for k in ks:
        s, e = mp.funcs[k]
        for n in range(s, e):
            if mp.ins[n][2].startswith("call ") and n >= 2:
                for j in (1, 2):   # the give routine's item argument (the last push is its message)
                    v = A.so2_mod.push_value(mp.ins[n - j])
                    if isinstance(v, int) and 10000 + KEY_ITEMS.start <= v < 10000 + KEY_ITEMS.stop:
                        out.add(v - 10000)
    return out


GUARD = re.compile(r"flag\[(\d+)\] == ([01])|not \(flag\[(\d+)\]\)|(?<![\w(])flag\[(\d+)\](?! ==)")


def guard_flags(conds):
    """{flag: value it needs} from condition strings ('flag[n] == 0', 'not (flag[n])',
    'flag[n] == 1', 'not (flag[n] == 0)', a bare 'flag[n]')."""
    out = {}
    for c in conds:
        neg = c.startswith("not (") and c.endswith(")")
        body = c[5:-1] if neg else c
        if "||" in body or (neg and "&&" in body):
            continue
        for mo in re.finditer(r"flag\[(\d+)\] == ([01])", body):
            v = int(mo.group(2))
            out[int(mo.group(1))] = (1 - v) if neg else v
        for mo in re.finditer(r"(?:^|\(|&& )flag\[(\d+)\](?=\)|$| &&)", body):
            out.setdefault(int(mo.group(1)), 0 if neg else 1)
    return out


def chamber_guards(field, battles, maps):
    """Per hub encounter: the flags its scene's guard tests ({flag: value that leaves the boss
    waiting}), the flags the scene stores and the key items it gives."""
    by_map = map_of(maps)
    sites = collections.defaultdict(list)
    for b in field["bosses"]:
        for s in b["sites"]:
            sites[b["encounter"]].append(s)
    for s in battles["scripted_battles"]:
        if s.get("encounter") == 523:
            sites[523].append({"map": s["map"], "routine": s["routine"], "requires": [],
                               "flags_tested": s.get("flags_tested", [])})
    out = {}
    for enc in HUB_ENCOUNTERS + OPTIONAL_BOSSES:
        guard, stored, keys, seen = {}, {}, set(), collections.defaultdict(set)
        for s in sites[enc]:
            conds = list(s.get("requires", []))

            def walk(v):
                for x in v:
                    conds.extend(x.get("requires", []))
                    walk(x.get("via", []))
            walk(s.get("via", []))
            for f, v in guard_flags(conds).items():
                seen[f].add(v)
            mp = by_map[s["map"]]
            k = mp.func_at.get(s["routine"])
            if k is not None:
                ks = routine_closure(mp, k, 2)
                stored.update(stores_in(mp, ks))
                keys |= gives_in(mp, ks)
        # A flag one site needs set and another clear is quest state (XINE: Ashton in the party
        # or not), not a guard.
        guard = {f: next(iter(v)) for f, v in seen.items() if len(v) == 1}
        for f in (1, 2, 4, SCENE_FLAG):
            guard.pop(f, None)
            stored.pop(f, None)
        out[enc] = {"guard": guard, "stored": stored, "key_items": sorted(keys)}
    # The set battle's own flag (battles.json flags_set 449) is its guard.
    out[523]["guard"].setdefault(449, 0)
    return out


def category_of(flag, field, chest_flags, gift_flags, cave_guards, boss, pa_flags, setters):
    """(category, kind, why) of a flag."""
    if flag in ENDING:
        return "ending", "ending", ENDING[flag]
    if flag in chest_flags:
        return "chest", "chest opened", "a chest's opened flag (late proof: cleared)"
    if flag in cave_guards:
        return "boss_guard", "Cave of Trials", "a Cave of Trials boss fight's guard (late proof: cleared)"
    if flag in boss:
        return "boss_guard", boss[flag][0], boss[flag][1]
    if flag in gift_flags:
        return "gift_guard", "gift", "a gift's guard flag (late proof: cleared)"
    if flag in STORY_STATE:
        return "story", "story state", STORY_STATE[flag]
    if flag in PA_MODE:
        return "pa", "PA mode", "Private Action mode (clear outside a PA)"
    if flag in KEY_ITEMS:
        return "other", "key item", f"key item {flag} (item {10000 + flag}; never tested by a script)"
    if flag == SCENE_FLAG:
        return "other", "scene flag", "0x2e9: cleared by every map's set-up, set by its scenes"
    play = [s for s in setters.get(flag, []) if not s["world_script"]]
    if flag in pa_flags and not any(s["story"] for s in play):
        return "pa", "PA result", "set by a Private Action (" + ", ".join(f"map {m}" for m in sorted(pa_flags[flag])[:4]) + ")"
    if any(s["story"] for s in play):
        w = sorted({tuple(s["window"]) for s in play if s["story"]}, key=lambda x: (x[0] or 0, x[1] or 99))
        maps = sorted({s["map"] for s in play if s["story"]})
        return "story", "scene", f"set by a story scene (maps {maps[:4]}{'...' if len(maps) > 4 else ''}, windows {w[:3]})"
    if flag in pa_flags:
        return "pa", "PA result", "set by a Private Action and elsewhere"
    if play:
        maps = sorted({s["map"] for s in play})
        return "other", "no story test", f"set without a story test (maps {maps[:4]}{'...' if len(maps) > 4 else ''})"
    if setters.get(flag):
        return "other", "world script", "set only by the world script's warp list (not play)"
    return "other", "no setter", "no script stores it (set by native code or the save)"


def build(args):
    field = json.load(open(os.path.join(ROOT, "docs", "ap", "field.json")))
    battles = json.load(open(os.path.join(ROOT, "docs", "ap", "battles.json")))
    snap = L.save_snapshot(args.save, args.save_frame)
    if struct.unpack_from("<h", snap, 0x1A)[0] != 671:
        sys.exit("the save is not at the Final Save Point (map 671)")
    gs, fl = snap[:0x2A0], snap[0x2A0:0x2A0 + 0x170]
    save_on = {n for n in range(SCRIPT_FLAGS) if fl[n >> 3] >> (n & 7) & 1}
    late_cleared = L.done_flags(field)
    late_on = save_on - late_cleared
    maps = load_maps(args.disc1)
    setters, readers = flag_index(maps)

    chest_flags = {c["flag"] for c in field["chests"]}
    for c in field["chests"]:
        chest_flags |= {v.get("flag") for v in c.get("versions", []) if isinstance(v, dict) and v.get("flag") is not None}
    gift_flags = set()
    for g in field["gifts"]:
        gift_flags |= set(g.get("guard_flags", []))
        gift_flags |= {int(k) for k in g.get("flags_set", {})} & set(g.get("flags_tested", []))
    gift_flags -= L.KEEP_FLAGS
    cave_guards = set()
    for b in field["bosses"]:
        for s in b["sites"]:
            if 698 <= s["map"] <= 825:
                for r in s.get("requires", []):
                    if mo := re.fullmatch(r"flag\[(\d+)\] == 0|not \(flag\[(\d+)\]\)", r):
                        cave_guards.add(int(mo.group(1) or mo.group(2)))
    pa_flags = collections.defaultdict(set)
    for p in field["private_actions"]:
        for f in p.get("gives", {}).get("flags", []):
            pa_flags[f].add(p["map"])

    guards = chamber_guards(field, battles, maps)
    names = {b["encounter"]: "/".join(dict.fromkeys(b["names"])) for b in field["bosses"]}
    names[523] = "Cross Cave guardians"
    boss = {}          # flag -> (kind, why); hub value in boss_value
    boss_value = {}
    chamber_keys = set()
    for enc in HUB_ENCOUNTERS + OPTIONAL_BOSSES:
        g = guards[enc]
        kind = "chamber" if enc in HUB_ENCOUNTERS else "optional boss"
        for f, v in g["guard"].items():
            if f in KEY_ITEMS or f in STORY_STATE or f in chest_flags:
                continue
            if kind == "optional boss" and v != 0:
                continue   # quest state (member joined, quest step): kept as the story left it
            boss.setdefault(f, (kind, f"guard of {names[enc]} [{enc}]: needs {v}"))
            boss_value.setdefault(f, v)
        for f, v in g["stored"].items():
            if f in KEY_ITEMS or f in STORY_STATE or f in chest_flags or f in boss:
                continue
            if v == 1 and (f in guards[enc]["guard"] or len([s for s in setters.get(f, []) if not s["world_script"]]) <= 2):
                boss.setdefault(f, (kind, f"set by {names[enc]} [{enc}]'s scene when won"))
                boss_value.setdefault(f, 0)
        if enc in HUB_ENCOUNTERS:
            chamber_keys |= set(g["key_items"])
    for f in FIELD_DONE:
        boss[f] = ("chamber", "a Field's done flag (162 Intelligence, 163 Power, 164 Courage, 165 Love): "
                              "each Field's last scene tests the other three")
        boss_value[f] = 0

    flags_considered = save_on | set(boss) | set(ENDING) | set(STORY_STATE)
    table = {}
    for f in sorted(flags_considered):
        cat, kind, why = category_of(f, field, chest_flags, gift_flags, cave_guards, boss, pa_flags, setters)
        save = int(f in save_on)
        late = int(f in late_on) if f != 1 else 1
        if cat in ("chest", "gift_guard", "pa", "ending"):
            hub = 0
        elif cat == "boss_guard":
            hub = 0 if kind == "Cave of Trials" else boss_value.get(f, 0)
        elif cat == "other" and kind == "key item" and f in chamber_keys:
            hub, why = 0, why + "; given by a hub chamber's scene (cleared: the boss gives it)"
        else:
            hub = late
        if f == 1:
            hub = None   # per game (games.claude / games.rena)
        table[f] = {"cat": cat, "kind": kind, "save": save, "late": late, "hub": hub, "why": why}

    hub_flags = sorted(f for f, r in table.items() if r["hub"] == 1)
    # Any flag the late state has that the table did not consider (none expected).
    missing = sorted(late_on - set(table))
    assert not missing, missing

    # ---- globals ----
    words = {}
    for off in range(0, 0x2A0, 4):
        words[0xA0 + off] = struct.unpack_from("<I", gs, off)[0]
    nede = STARTS[args.start]["synard"] if STARTS[args.start]["world"] == 2 else STARTS["fun_city"]["synard"]
    ex = SYNARD_EXPEL

    def u16pair(lo, hi):
        return (lo & 0xFFFF) | ((hi & 0xFFFF) << 16)
    spot = {
        0x30C: (ex[0], "Expel Synard x (+0x26c): Arlia's own spot"),
        0x310: (ex[2], "Expel Synard z (+0x270)"),
        0x32C: (ex[1] & 0xFFFFFFFF, "Expel Synard y (+0x28c)"),
        0x314: (nede[0], "Energy Nede Synard x (+0x274): the start town's own spot"),
        0x318: (nede[2], "Energy Nede Synard z (+0x278)"),
        0x330: (nede[1] & 0xFFFFFFFF, "Energy Nede Synard y (+0x290)"),
        0x31C: (u16pair(ex[3], nede[3]), "Synard facings (+0x27c Expel, +0x27e Energy Nede)"),
    }
    gtable = []
    hub_globals = {}
    for g in sorted(words):
        v = words[g]
        go = g - 0xA0
        row = {"addr": f"{g:#x}", "game_state": f"+{go:#x}", "save": f"{v:#x}"}
        if g in (0xA0, 0xA4):
            row.update(cat="story", hub="chapter / step keys", why="G[0xa0] chapter, G[0xa4] step")
        elif g < 0xC4:
            if not v:
                continue
            row.update(cat="engine", hub="not set", why="arrival position / map / music: the start sets them")
        elif g == 0xC4:
            row.update(cat="story", hub="games", why="+0x24 / +0x25 the story route (Claude 0 / 1, Rena 1 / 0); +0x26 / +0x27 kept")
        elif 0xC8 <= g < 0x1B8:
            if not v:
                continue
            row.update(cat="names", hub="not set", why="character names (20 bytes each): New Game's own")
        elif g == 0x1B8:
            row.update(cat="setting", hub="not set", why="message speed (a setting)")
        elif g in spot:
            hub_globals[g] = spot[g][0]
            row.update(cat="vehicle", hub=f"{spot[g][0]:#x}", why=spot[g][1] + " (table A 0x19 set_point)")
        elif g in (0x2E4, 0x2E8, 0x2EC):
            if v:
                row.update(cat="statistics", hub="0", why={0x2E4: "+0x244 battle escapes (A 0x08): a new game's",
                                                           0x2E8: "+0x248 treasure count (A 0x01 / 0x02; the chests are cleared "
                                                                  "too), +0x24a pickpocket tries: a new game's",
                                                           0x2EC: "+0x24c pickpocket wins: a new game's"}[g])
            else:
                continue
        elif g == 0x2F0:
            if not v:
                continue
            row.update(cat="engine", hub="0", why="+0x250 the familiar's goods in flight: none at New Game")
        elif g in (0x320, 0x324):
            if not v:
                continue
            row.update(cat="party", hub="0", why="+0x280..+0x287 saved party order (A 0x16 / 0x17 party_reload_changed would "
                                                 "move the save's members back into slots): none")
        elif g in (0x308, 0x33C):
            if not v:
                continue
            row.update(cat="vehicle", hub="0", why="vehicle 4 (flag 16, clear) position +0x268 / +0x29c: unused")
        elif g == 0x328:
            if not v:
                continue
            row.update(cat="engine", hub="0", why="+0x288 the familiar's kind / rank (specialty), +0x28a vehicle 4 facing: a new game's")
        else:
            if not v:
                continue
            hub_globals[g] = v
            row.update(cat="quest / scene state", hub=f"{v:#x}", why=GLOBAL_NOTES.get(g, "kept as the late proof proved it"))
        gtable.append(row)
    route_word = words[0xC4]
    games = {"claude": {"flags": [1], "globals": {"0xc4": f"{(route_word & ~0xFFFF) | 0x0100:#x}"}},
             "rena": {"flags": [], "globals": {"0xc4": f"{(route_word & ~0xFFFF) | 0x0001:#x}"}}}

    cats = collections.OrderedDict()
    for c, rule, why in CATEGORY_RULES:
        rows = {f: r for f, r in table.items() if r["cat"] == c}
        cats[c] = {"rule": rule, "why": why, "count": len(rows),
                   "save_set": sum(r["save"] for r in rows.values()),
                   "late_set": sum(r["late"] for r in rows.values()),
                   "hub_set": sum(1 for r in rows.values() if r["hub"] == 1),
                   "reset": sorted(f for f, r in rows.items() if r["late"] == 1 and r["hub"] == 0),
                   "set_by_hub": sorted(f for f, r in rows.items() if r["late"] == 0 and r["hub"] == 1),
                   "kinds": dict(collections.Counter(r["kind"] for r in rows.values()))}

    starts = {}
    for key, s in STARTS.items():
        nd = s["synard"] if s["world"] == 2 else STARTS["fun_city"]["synard"]
        starts[key] = {"name": s["name"], "world": s["world"], "map": s["map"], "x": s["x"], "y": s["y"], "z": s["z"],
                       "dir": s["dir"], "door": s["door"],
                       "synard_expel": {"x": ex[0], "y": ex[1], "z": ex[2], "dir": ex[3], "from": STARTS["arlia"]["synard_from"]},
                       "synard_nede": {"x": nd[0], "y": nd[1], "z": nd[2], "dir": nd[3], "from": STARTS["fun_city"]["synard_from"]
                                       if s["world"] == 1 else s["synard_from"]}}
    return {
        "about": ("The hub's New Game state (world_mode: hub; docs/ap/seed_format.md 'Hub', docs/handoff/hub.md D1): "
                  "the late proof's state (the user's save at the Final Save Point, tools/ap_late_proof.py build_state) "
                  "with what belongs to one player's game reset. Written by tools/ap_hub_state.py; the APWorld copies "
                  "`hub` into the seed's hub block (start and the Synard words per hub_start)."),
        "generated": datetime.date.today().isoformat(),
        "source": {"save": os.path.relpath(args.save, ROOT), "frame": args.save_frame, "save_map": 671},
        "chapter": 60, "step": 4000, "disc": 2,
        "step_reason": STEP_REASON,
        "late_state": {"flags_set": len(late_on | {1}), "flags_cleared_from_save": len(save_on & late_cleared),
                       "done_flags": len(late_cleared)},
        "hub": {"chapter": 60, "step": 4000, "disc": 2, "flags": hub_flags,
                "globals": {f"{g:#x}": f"{v:#x}" for g, v in sorted(hub_globals.items())},
                "games": games, "start": {k: v for k, v in starts[args.start].items() if k in ("name", "map", "x", "y", "z", "dir")},
                "lock": {"chapter": True, "step": True, "step_maps": [685]}},
        "counts": {"hub_flags": len(hub_flags), "late_flags": len(late_on | {1}),
                   "reset": len([f for f, r in table.items() if r["late"] == 1 and r["hub"] == 0]),
                   "set_by_hub": len([f for f, r in table.items() if r["late"] == 0 and r["hub"] == 1])},
        "categories": cats,
        "starts": starts,
        "start_random": ("random: the APWorld picks one of `starts` (proposal: the two; more towns with their own "
                         "Synard spot can be added: see notes)"),
        "globals_table": gtable,
        "chamber_guards": {str(e): {"name": names.get(e), "guard": {str(k): v for k, v in sorted(g["guard"].items())},
                                    "stored": {str(k): v for k, v in sorted(g["stored"].items())},
                                    "key_items": g["key_items"]} for e, g in guards.items()},
        "flags": {str(f): r for f, r in table.items()},
        "notes": NOTES,
    }


CATEGORY_RULES = [
    ("story", "keep", "the story is passed: chapter 60 with every scene's flags as the user's game left them, which the "
                      "late proof swept (610 maps); flag 1 comes per game"),
    ("chest", "clear", "a location: every chest waits (the late proof's done flags)"),
    ("gift_guard", "clear", "a location: every gift waits (the late proof's done flags)"),
    ("boss_guard", "reset", "every boss waits: a chamber's guard at the value that leaves its scene armed, its scene's own "
                            "'won' flag clear, the Fields' 162-165 clear (no Field done), the Cave of Trials' guards clear "
                            "(late proof), the optional bosses' done flags clear (their quest state is kept)"),
    ("pa", "clear", "Private Action results belong to the user's party (relationship and PA-chain conditions); a new game "
                    "has none. Relationship values are config data (+0x58 / +0xe8), New Game's"),
    ("ending", "clear", "the ending flow, the special ending pairs and Limiter Off (the limiter-off goals need it clear)"),
    ("other", "keep", "one-time talks, puzzle / door state and quest flags without a story test: as the late proof proved "
                      "them; key items kept except those a hub chamber's scene gives"),
]

STEP_REASON = (
    "4000, the save's step (after Cyril, at the Final Save Point). Every map was swept at chapter 60 step 4000 by the late "
    "proof, and the goal needs it: the Final Save Point floor's way to the corridor (671 -> 684) and the final boss's "
    "scene (685: chapter 60 step 4000, which stores 5000). Fienal's three pairs (D10) play their own scenes at their "
    "steps (657 Ruprecht & Nicolus at 1000, 669 Vesper & Decus at 2000, 674 Cyril at 3000) inside their chambers' story "
    "views, so the hub never needs a lower step; L'Aqua's chapter-60 departure scene (686, step 0 -> 1000) stays "
    "silent. With the views on, Fienal's rooms show their pre-pair states until each pair is beaten, so Fienal is "
    "proven again (the hub proof)."
)

GLOBAL_NOTES = {
    0x1BC: "G[0x1bc] Ashton's quest (9999: done) / G[0x1be] Opera's quest (0: not started; Ashton rules her out)",
    0x1C0: "G[0x1c0] previous map / G[0x1c2]: Fienal's stair rooms and the Cave of Trials read them",
    0x1C4: "G[0x1c4] / G[0x1c6]: Cave of Trials / Fienal room state",
    0x1C8: "G[0x1c8] (cave) / G[0x1ca] (every map's set-up stores it)",
    0x1CC: "G[0x1cc] (every map's set-up) / G[0x1ce] / G[0x1cf] the floor (Fienal 8)",
    0x1D8: "G[0x1d8] (every map's set-up counts it) / G[0x1d9] (cave)",
    0x1E0: "G[0x1e0] (maps 318 / 332 / 333: Giveaway)",
    0x1E4: "G[0x1e4] Precis's quest at Linga (99999: done; not a Private Action: no PA routine touches it)",
    0x1EC: "the tournament's saved positions (468 / 469: written before read)",
    0x1F0: "the tournament's saved positions (468 / 469)",
    0x1F4: "the tournament's saved positions (468 / 469)",
    0x1F8: "the tournament's saved positions (468 / 469) / G[0x1fa]",
    0x1FC: "G[0x1fc] Linga / G[0x1fd] Cross Castle / G[0x1fe] / G[0x1ff] Hilton",
    0x200: "G[0x200] the Front Line / G[0x201] L'Aqua 577-584 / G[0x202] / G[0x203] Cross, Linga, Lacour",
    0x204: "G[0x204] the tournament (468 / 469 / 474)",
    0x214: "G[0x214] Chisato's quest / G[0x215] Fun City and Eluria Tower / G[0x216] / G[0x217] the Fun City arena",
    0x218: "G[0x218] the Fun City arena and Eluria Tower's puzzle (560-568) / G[0x219..0x21b] arena, Cave of Trials",
    0x304: "+0x264 the forced battle (0: none) / +0x266 (kept)",
}

NOTES = [
    "The hub state holds no config data: relationship values (+0x58 / +0xe8), levels, inventory and Fol are New Game's.",
    "Flag 556 (Limiter Off) and 445 (the Clik Filia meeting, Claude's game) were set in the user's save; 556 is cleared "
    "(the limiter-off goals); 445 / 446 stay as the save has them and goal.flags presets both anyway.",
    "Expel's Synard: the save already had Expel's spot set (the user flew on Virtual Expel), so the late state flew there "
    "too; the hub uses Arlia's own spot rather than where the user parked (R8: no first trip through the old man).",
    "Energy Nede's Synard: the save parked it where the user last landed; the hub parks it at the start town's own spot "
    "(Fun City's for a start on Expel, where the arena's old man lands), so the party can always reach it on foot.",
    "Optional bosses (Flarelizard 196, Nightmare 197, XINE 199, Ghost 211): their done flags are reset but their quests "
    "(G[0x1bc] Ashton 9999, G[0x1be] Opera 0, flags 113 / 141) are the save's, so none of them is armed at chapter 60; "
    "they stay excluded locations ('optional quest').",
    "Saved party order (+0x280..): cleared; otherwise A 0x17 (party_reload_changed) would move the save's members into "
    "slots.",
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", default=os.path.join(ROOT, "scratchpad", "latesave", "card.mcd"))
    ap.add_argument("--save-frame", type=int, default=13)
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    ap.add_argument("--start", choices=sorted(STARTS), default="fun_city", help="the start point written in hub.start")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "docs", "ap", "hub_state.json"))
    args = ap.parse_args()
    if not args.disc1:
        sys.exit("set SS_DISC1 (or --disc1)")
    out = build(args)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    c = out["counts"]
    print(f"hub flags {c['hub_flags']} (late {c['late_flags']}; reset {c['reset']}, set by the hub {c['set_by_hub']})")
    for k, v in out["categories"].items():
        print(f"  {k:10s} {v['rule']:5s} count {v['count']:4d} save {v['save_set']:4d} late {v['late_set']:4d} "
              f"hub {v['hub_set']:4d} reset {len(v['reset']):3d} kinds {v['kinds']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
