#!/usr/bin/env python3
"""The hub's boss chambers and dungeon keys (docs/ap/seed_format.md "Hub (`world_mode: hub`)",
docs/handoff/hub.md D2-D6, D10; scratchpad/openworld/story_flags.md section 2, W3):
docs/ap/hub_bosses.json.

Each story boss plays its original scene in its chamber through a story view (P6): while the
boss is unbeaten, the chamber's map reads the boss's chapter / step and the listed flags, so the
scene arms as it did in the story; the view ends at the first map load outside its maps or at
the first free play after the battle was won, and stores made to listed flags are dropped
(except `keep`). This tool reads the map scripts and writes, per chamber:

- the encounter, its battle stage / param (battles.json), the chamber map, the boss routine and
  how the scene starts (a trigger region with its corners, the map's set-up on arrival, an
  actor's talk handler: the top of field.json's `via` chain);
- the guard (chapter / step / flags) from the site's conditions;
- the scene's events from the battle on: flag stores, story stores, transfers (the cut), party
  changes, key items given, choices;
- the story-view spec: chapter / step = the guard; `flags` = the flags the chamber map reads
  whose value at that story point differs from the hub's (presentation), and the flags its
  scripts store that the hub must not keep (story bookkeeping, party flags: dropped), each at
  its value at that story point; never chest flags, key items, flag 1 / 2 / 4 / 745, nor the
  boss guards and the Fields' done flags (written through: the hub records them). Curated
  per chamber where the automatic rule is not enough (CURATED, with the reason);
- whether the late state reaches the chamber (docs/ap/late_proof.json, both games: false = P5);
- its dungeon (D4).

and the dungeon key groups: towns open, one key per dungeon, with the doors that enter it from
outside on the late state's edges (doors.json ids), the chambers and the chests behind it.

Usage:
  tools/ap_hub_bosses.py [--disc1 CUE] [-o docs/ap/hub_bosses.json]
It reads docs/ap/hub_state.json (tools/ap_hub_state.py) for the hub's flag values.
"""
import argparse
import collections
import datetime
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ap_extract as A  # noqa: E402
import ap_hub_state as H  # noqa: E402

ROOT = H.ROOT
TRANSFER = re.compile(r"^(map_transfer\w*)\((-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+)\)")
STORY_STORE = re.compile(r"^store s32 G\[0x(a0|a4)\] = (0x[0-9a-f]+)")
REGION = re.compile(r"^(thread_start(?:_ex)?)\((.*)\)")
LOCATION_BASE = 0x50320000

# Dungeons (D4: towns open, one key per dungeon). Maps by area (field.json map_areas, with the
# mislabelled ones fixed by hand: map 596 is the Field of Love's last room, 456-466 the dungeon
# under Mars, 11 / 30 belong to the opening, 625 is the disc-switch hub).
DUNGEONS = collections.OrderedDict([
    ("Salva Drift", {"world": 1, "maps": list(range(61, 73))}),
    ("Cross Cave", {"world": 1, "maps": list(range(524, 548))}),
    ("Mars Dungeon", {"world": 1, "maps": list(range(456, 467))}),
    ("Zand's Mansion", {"world": 1, "area": "Zand's Mansion"}),
    ("Sanctuary of Linga", {"world": 1, "area": "Sanctuary of Linga"}),
    ("Hoffmann Ruins", {"world": 1, "area": "Hoffmann Ruins"}),
    ("Lacour Front Line", {"world": 1, "maps": list(range(438, 449))}),
    ("Eluria Tower", {"world": 1, "area": "Eluria Tower", "minus": [625]}),
    ("Mountain Palace", {"world": 1, "area": "Mountain Palace"}),
    ("Lasguss Mountain", {"world": 1, "area": "Lasguss Mountain"}),
    ("Cave of Trials", {"world": 1, "area": "Cave of Trials"}),
    ("Synard Plant", {"world": 2, "maps": [269, 270, 271]}),
    ("Cave of Red Crystal", {"world": 2, "area": "Cave of Red Crystal"}),
    ("Field of Intelligence", {"world": 2, "area": "Field of Intelligence"}),
    ("Field of Power", {"world": 2, "maps": list(range(597, 617)) + [624]}),
    ("Field of Courage", {"world": 2, "maps": list(range(629, 643))}),
    ("Field of Love", {"world": 2, "maps": list(range(588, 597))}),
    ("Mihne Cavern", {"world": 2, "maps": list(range(449, 456))}),
    ("Portal of Sealing", {"world": 2, "maps": [313] + list(range(386, 402))}),
    ("Fienal", {"world": 2, "area": "Fienal", "plus": [687]}),
])
# Town chambers (no dungeon key: D4 opens towns): Berle at Armlock 306, Shigeo in Fun City 281,
# Marsilio in the Fun City arena 305.
TOWN_OF = {306: "Town of Armlock", 281: "Fun City", 305: "Fun City Fighting Arena"}

# Per-chamber decisions the scripts alone don't make (each with its reason). Keys:
#   maps: the view's maps when the scene runs on into a room the cut should not reach;
#   flags: extra / overriding view flags {n: value}; keep: listed flags written back;
#   view: False for a flag-guarded boss that plays at the hub's own state;
#   note: what the hub checks per chamber must look at.
CURATED = {
    209: {"view": False, "note": "flag-guarded (chapter >= 21 && flag 483 == 0): the hub state clears 483, so "
                                 "the scene arms at chapter 60; the scene sets 483 and the party stays in 412 "
                                 "(hubvm: proven headless, 409:T6 -> 412)"},
    519: {"view": False, "p5": True, "note": "flag-guarded (flag 587 == 0, no chapter test): the hub clears 587; it plays "
                                 "inside Berle 520's tower view (chapter 33), on the way up"},
    523: {"view": False, "note": "the Cross Cave key chest's guardians (set battle 523, flag 449 clear): the routine "
                                 "has no story test; the hub clears 449 and the chest flag 2569. The chest's "
                                 "handler stores chapter 13 / step 0 (dropped by the chapter lock)"},
    520: {"p5": True, "maps": list(range(557, 576)), "cut_to": {"map": 557, "x": -92, "y": 0, "z": 1907, "dir": 270},
          "note": "P5: the late state never got past 557 (door 557 -> 558, routine 0x7808, needs chapter > 32: "
                  "a headless look). The view covers the whole tower at the climb's chapter 33, so Shin 569 "
                  "plays on the way up. The cut must not follow the scene: 576 stores chapter 34 unconditionally "
                  "and goes on to 625 (the disc switch's scene map); cut_to = the tower's world-map arrival"},
    521: {"maps": [264, 269], "after_maps": [271],
          "note": "P5: map 264 (Synard Reception Area, North City) sends its two doors (0x7390 / 0x7410) to 269 "
                  "at chapter <= 38, else to 270 (the plant's later version, same spot): the view on 264 opens "
                  "the way (264 has scenes only at 38/0 and 38/1000; its NPCs show chapter 38). The aftermath "
                  "continues in 271 (38/4000: step 5000, then 272 or 264)"},
    522: {"chapter": 40, "flags": {745: 0, 11: 0, 12: 0}, "keep": [11],
          "note": "guard chapter <= 40 && step == 1000. The aftermath stores chapter 41, sets 11 and 12 (12 must "
                  "not stay: it makes a disc of Nede's world map unwalkable; clear in the late save) and moves "
                  "computed (Central City). Noel's choice to stay (0x7db8) and party changes are dropped "
                  "(hubvm: proven headless with these flags, 418:T2 -> 417)"},
    524: {"p5": False, "after_maps": [555, 628, 556],
          "note": "553's door picks 554 iff flag 175 is clear, which the hub state makes so (no view of 553 "
                  "needed). Aftermath: 554 -> 555 (42: step 1000 -> 628, or step 1010 -> map 14, Shingo "
                  "Forest: a vision in one game) -> ... -> 556 (sets 162). Map 14 is left out of after_maps "
                  "(Expel; a headless look decides whether the vision must be followed or cut)"},
    525: {"maps": [603, 604], "until": "transfer",
          "note": "604's end scene (0x6870 / 0x688c) is guarded only by flags (528 == 1 && 163 == 0, flag 1 for "
                  "the game), no chapter: after the Guardian it must run in the view (chapter 42), so the view "
                  "lasts until the party leaves [603, 604]"},
    527: {"after_maps": [625],
          "note": "Claude's game: the aftermath goes to 625 (the scene map) at step 10 for a vision and returns "
                  "to 642 at step 20 (625 0x5aa0); Rena's game stays in 642 (key item 84, flag 164)"},
    531: {"maps": [453, 454], "chapter": 55, "step": 0,
          "note": "P5: map 453's door (0x3bd0) goes to 455 when chapter > 55, to 454 otherwise; 454 registers "
                  "Bark's trigger unconditionally. View (55, 0) on [453, 454]; the scene stores step 1000 and "
                  "moves to 455 (the cut)"},
    532: {"note": "P5: the arena 305 is not reached by the late state (doors from 279 0xa100, 297 by the "
                  "receptionist's choice 1, 301, 302): a headless look. The scene starts from an actor's talk "
                  "(registered at 57/20) and choice 2"},
    535: {"note": "a town chamber (Armlock 306): the scene starts at 58/0 by a trigger region; the cut is the "
                  "transfer to 314 (Portal of Sealing's side)"},
}


def load(path):
    return json.load(open(os.path.join(ROOT, path)))


def dungeon_maps(field):
    areas = collections.defaultdict(list)
    for m, a in field["map_areas"].items():
        areas[a].append(int(m))
    out = collections.OrderedDict()
    for name, d in DUNGEONS.items():
        ms = set(d.get("maps", [])) | set(areas.get(d.get("area"), []))
        ms |= set(d.get("plus", []))
        ms -= set(d.get("minus", []))
        out[name] = sorted(ms)
    return out


def flat_via(site):
    """Every condition of a site (its own and its callers' / registrations')."""
    out = list(site.get("requires", []))

    def walk(v):
        for x in v:
            out.extend(x.get("requires", []))
            walk(x.get("via", []))
    walk(site.get("via", []))
    return out


def story_guard(conds):
    """(chapter, step) the conditions pin with ==, or None."""
    ch = st = None
    for c in conds:
        if c.startswith("not ("):
            continue
        for mo in re.finditer(r"chapter == (\d+)", c):
            ch = int(mo.group(1))
        for mo in re.finditer(r"step == (\d+)", c):
            st = int(mo.group(1))
    if ch is None:
        for c in conds:
            if not c.startswith("not ("):
                for mo in re.finditer(r"chapter <= (\d+)", c):
                    ch = int(mo.group(1))
    if st is None:
        for c in conds:
            mo = re.fullmatch(r"not \(step != (\d+)\)", c)
            if mo:
                st = int(mo.group(1))
    return ch, st


def trigger_of(mp, site):
    """How the scene starts: the top of the site's via chain."""
    def tops(v, path):
        for x in v:
            p = path + [x]
            if x.get("via"):
                yield from tops(x["via"], p)
            else:
                yield p
    out = []
    for path in tops(site.get("via", []), []):
        conds = [c for x in path for c in x.get("requires", [])]
        regs_on_path = [x for x in path if "registered_at" in x]
        top = regs_on_path[0] if regs_on_path else path[-1]
        if "registered_at" in top:
            n = next(j for j, i in enumerate(mp.ins) if i[0] == top["registered_at"])
            t = mp.ins[n][2]
            rec = {"kind": "thread_start" if top["by"].startswith("thread_start") else
                   "talk" if top["by"] in ("ext_70", "ext_f3") else top["by"],
                   "registered_at": f"{top['registered_at']:#x}", "conditions": conds}
            mo = REGION.match(t)
            if mo:
                a = [int(x) for x in mo.group(2).split(",")]
                rec["region"] = [[a[0], a[1]], [a[2], a[3]], [a[4], a[5]], [a[6], a[7]]]
                rec["routine"] = f"{a[9] * 4:#x}"
                if mo.group(1) == "thread_start_ex":
                    rec["y_range"] = [a[11], a[12]]
            elif top["by"] in ("ext_70", "ext_f3"):
                a = [int(x) for x in re.findall(r"-?\d+", t[t.index("("):])]
                rec["actor"] = a[0]
                rec["routine"] = f"{a[7] * 4:#x}"
            out.append(rec)
        else:
            # A call chain whose top routine nobody calls or registers: the map's set-up.
            n = next(j for j, i in enumerate(mp.ins) if i[0] == top["call_at"])
            k = mp.func_of[n]
            for _ in range(6):   # climb past field.json's depth: the routine nobody calls is the set-up
                callers = mp.callers(k)
                regs = A.registrations(mp).get(mp.ins[mp.funcs[k][0]][0] // 4, [])
                if regs or len(callers) != 1:
                    break
                k = mp.func_of[callers[0]]
            out.append({"kind": "setup" if not callers and not regs else "call",
                        "routine": f"{mp.ins[mp.funcs[k][0]][0]:#x}", "call_at": f"{top['call_at']:#x}",
                        "conditions": conds})
    return out


def scene_events(mp, routine, depth=2):
    """The events of the boss routine and the map routines it calls, in code order (not
    library routines but the give / join ones): battle, flag and story stores, transfers,
    party changes, key items, choices, disc changes."""
    roles, lib = H_LIB
    out, seen = [], set()

    def walk(k, d):
        if k in seen:
            return
        seen.add(k)
        s, e = mp.funcs[k]
        for n in range(s, e):
            i = mp.ins[n]
            t = i[2]
            ev = None
            if t.startswith("battle"):
                ev = "battle"
            elif mo := H.FLAG_STORE.match(t):
                ev = f"flag {int(mo.group(1), 16)} = {int(mo.group(2), 16)}"
            elif mo := STORY_STORE.match(t):
                ev = f"{'chapter' if mo.group(1) == 'a0' else 'step'} = {int(mo.group(2), 16)}"
            elif mo := TRANSFER.match(t):
                ev = f"transfer {mo.group(2)} ({mo.group(4)}, {mo.group(5)}, {mo.group(6)}) dir {mo.group(7)}"
            elif t.startswith("map_transfer"):
                ev = "transfer (computed)"
            elif t.startswith(("ext_ae", "ext_af")):
                ev = "party " + t.split()[0]
            elif t.startswith("B_6a"):
                ev = "disc change"
            elif re.match(r"^ext_(dc|dd|de|df|e0)\b", t):
                ev = "choice"
            elif t.startswith("call "):
                tk = mp.func_at.get(i[3])
                if tk is None:
                    continue
                if mp.shapes[tk] in lib:
                    role = roles.get(mp.shapes[tk])
                    if role and role[0] == "give":
                        v = A.so2_mod.push_value(mp.ins[n - 2])   # the item (the last push is its message)
                        if isinstance(v, int) and v >= 10000:
                            ev = f"key item {v - 10000}"
                        elif isinstance(v, int) and 0 < v < 1024:
                            ev = f"item {v}"
                    elif role and role[0] == "join":
                        ev = "party join"
                    if ev is None:
                        continue
                elif d > 0:
                    walk(tk, d - 1)
                    continue
                else:
                    continue
            if ev:
                out.append([f"{i[0]:#x}", ev])
    walk(mp.func_at[routine], depth)
    return out


def map_flags(mp):
    """{flag: 'r' / 'w' / 'rw'} of every global flag the script reads or stores."""
    out = collections.defaultdict(set)
    for i in mp.ins:
        t = i[2]
        mo = H.FLAG_STORE.match(t)
        if mo:
            out[int(mo.group(1), 16)].add("w")
        elif "flag G#" in t:
            for m2 in H.FLAG_READ.finditer(t):
                out[int(m2.group(1), 16)].add("r")
    return out


def story_value(flag, chapter, setters, hub_value, here):
    """The flag's value when the story reached `chapter` (how it was decided): the latest story
    scene storing it in an earlier chapter decides; a flag stored only in later chapters (or only
    by this chamber's own scenes) was 0; a store by another map's scene in this very chapter
    leaves it unclear (the hub's value is used)."""
    play = [s for s in setters.get(flag, []) if not s["world_script"] and s["story"]]
    if not play:
        return hub_value, "hub"
    inside = [s for s in play if (s["window"][0] is None or s["window"][0] <= chapter)
              and (s["window"][1] is None or s["window"][1] >= chapter)]
    if any(s["map"] not in here for s in inside):
        return hub_value, "unclear"
    before = [s for s in play if s["window"][1] is not None and s["window"][1] < chapter]
    if before:
        last = max(s["window"][1] for s in before)
        vals = {s["value"] for s in before if s["window"][1] == last}
        if len(vals) == 1:
            v = vals.pop()
            return v, f"{'set' if v else 'cleared'} in chapter {last}"
        return hub_value, "unclear"
    return 0, "stored later"


H_LIB = None


def build(args):
    global H_LIB
    field = load("docs/ap/field.json")
    battles = load("docs/ap/battles.json")
    doors = load("docs/ap/doors.json")
    late = load("docs/ap/late_proof.json")
    hub = load("docs/ap/hub_state.json")
    maps = H.load_maps(args.disc1)
    by_map = H.map_of(maps)
    H_LIB = A.library(maps)
    setters, readers = H.flag_index(maps, H_LIB[1])
    hub_flags = set(hub["hub"]["flags"])
    hubv = {int(f): (r["hub"] if r["hub"] is not None else None) for f, r in hub["flags"].items()}
    cat = {int(f): r["cat"] for f, r in hub["flags"].items()}
    chest_flags = {c["flag"] for c in field["chests"]}
    late_maps = {g: set(late["games"][g]["maps"]) for g in late["games"]}
    dmaps = dungeon_maps(field)
    dungeon_of = {m: d for d, ms in dmaps.items() for m in ms}
    names = {b["encounter"]: "/".join(dict.fromkeys(b["names"])) for b in field["bosses"]}
    names[523] = "Cross Cave guardians"
    stage = {}
    for s in battles["scripted_battles"]:
        if "encounter" in s:
            stage[s["encounter"]] = (s["map"], s["stage"], s["param"], s["routine"])
    sites = collections.defaultdict(list)
    for b in field["bosses"]:
        for s in b["sites"]:
            sites[b["encounter"]].append(s)
    m523, st523, pa523, r523 = stage[523]
    sites[523] = [{"map": m523, "routine": r523, "offset": None, "requires": ["flag[449] == 0"], "via": [],
                   "stage": st523, "param": pa523, "window": None}]
    never = {1, 2, 4} | set(H.KEY_ITEMS) | set(H.FIELD_DONE)

    rows = []
    for enc in H.HUB_ENCOUNTERS:
        for site in sites[enc]:
            m = site["map"]
            mp = by_map[m]
            conds = flat_via(site)
            ch, st = story_guard(conds)
            gflags = H.guard_flags(conds)
            gflags = {f: v for f, v in gflags.items() if f not in (2, 4, H.SCENE_FLAG)}
            game = None
            if gflags.get(1) == 1:
                game = "claude"
            elif gflags.get(1) == 0 and enc in (195,):
                game = "rena"
            cur = CURATED.get(enc, {})
            events = scene_events(mp, site["routine"])
            transfers = [e for e in events if e[1].startswith("transfer")]
            # The cut: the first transfer after the battle, or free play.
            bi = next((j for j, e in enumerate(events) if e[1] == "battle"), 0)
            after = [e for e in events[bi:] if e[1].startswith("transfer")]
            cut = {"at": "transfer", "to": after[0][1], "offset": after[0][0]} if after else {"at": "free play"}
            row = {
                "encounter": enc, "name": names.get(enc), "map": m, "maps": [m],
                "area": field["map_areas"].get(str(m)), "dungeon": dungeon_of.get(m) or TOWN_OF.get(m),
                "town": m in TOWN_OF,
                "location": LOCATION_BASE + (9 << 21) + enc,
                "battle": {"stage": site.get("stage"), "param": site.get("param"),
                           "offset": f"{site['offset']:#x}" if site.get("offset") is not None else None},
                "routine": f"{site['routine']:#x}",
                "guard": {"chapter": ch, "step": st, "flags": {str(f): v for f, v in sorted(gflags.items())},
                          "conditions": sorted(set(conds))},
                "trigger": trigger_of(mp, site),
                "events": events,
                "cut": cut,
                "late": {g: m in late_maps[g] for g in sorted(late_maps)},
            }
            if game:
                row["game"] = game
            # ---- the view ----
            ch = cur.get("chapter", ch)
            st = cur.get("step", st)
            vmaps = cur.get("maps", [m])
            row["maps"] = vmaps
            if cur.get("view") is False or ch is None:
                row["view"] = None
                row["view_reason"] = cur.get("note", "no chapter / step guard: plays at the hub's own state")
            else:
                here = set(vmaps) | set(cur.get("after_maps", []))
                scene_stores = {int(e[1].split()[1]) for e in events if e[1].startswith("flag ")}
                mf = collections.defaultdict(set)
                for vm in sorted(here):
                    for f, rw in map_flags(by_map[vm]).items():
                        mf[f] |= rw
                vflags, why = {}, {}
                for f, rw in sorted(mf.items()):
                    if f in never or f in chest_flags or cat.get(f) in ("boss_guard", "chest", "gift_guard"):
                        continue
                    hv = hubv.get(f, 0) or 0
                    ss = [x for x in setters.get(f, []) if not x["world_script"]]
                    lib_only = bool(ss) and all(x["lib"] for x in ss)
                    if f == H.SCENE_FLAG:
                        if "w" in rw:
                            vflags[f], why[f] = 0, "the scene flag (clear in the hub; the set-up fades in only at 0)"
                        continue
                    if lib_only:
                        if f in scene_stores:
                            vflags[f], why[f] = hv, "library scratch stored by the scene: the hub's value, dropped"
                        continue
                    sv, how = story_value(f, ch, setters, hv, here)
                    if f in gflags:
                        sv, how = gflags[f], "guard"
                    stored_here = any(x["map"] in {by_map[vm].id for vm in here} and
                                      (x["window"][0] is None or x["window"][0] <= ch) and
                                      (x["window"][1] is None or x["window"][1] >= ch) for x in ss) or f in scene_stores
                    if sv != hv or (stored_here and cat.get(f) in ("story", "other", "pa", None)):
                        vflags[f] = sv
                        why[f] = how + (" (stored in the view)" if stored_here else "")
                for f, v in cur.get("flags", {}).items():
                    vflags[f] = v
                    why[f] = "curated"
                row["view"] = {"chapter": ch, "step": st if st is not None else 0,
                               "flags": {str(f): v for f, v in sorted(vflags.items())},
                               "keep": sorted(cur.get("keep", []))}
                for k in ("after_maps", "until", "cut_to"):
                    if k in cur:
                        row["view"][k] = cur[k]
                row["view_flags_why"] = {str(f): w for f, w in sorted(why.items())}
                unclear = sorted(f for f, w in why.items() if w.startswith("unclear"))
                if unclear:
                    row["view_check"] = unclear
            row["side_effects"] = sorted({e[1] for e in events[bi:] if e[1].startswith(
                ("key item", "item", "party", "chapter", "step", "choice", "disc"))} |
                {e[1] for e in events[bi:] if e[1] in ("flag 11 = 1", "flag 12 = 1") or
                 re.match(r"flag 16[2-5] = 1", e[1])})
            row["p5"] = cur.get("p5", not any(row["late"].values()) and not set(vmaps) - {m})
            if cur.get("note"):
                row["note"] = cur["note"]
            rows.append(row)

    # ---- dungeon keys ----
    edges = collections.defaultdict(set)
    for g in late["games"].values():
        for a, bs in g["edges"].items():
            edges[int(a)] |= set(bs)
    chests_by_map = collections.defaultdict(list)
    for c in field["chests"]:
        chests_by_map[c["map"]].append(c["flag"])
    proven = set()
    for g in late["games"].values():
        proven |= set(g["proven"])
    dungeons = []
    for key, (name, ms) in enumerate(dmaps.items(), 1):
        mset = set(ms)
        ds = []
        for d in doors["doors"]:
            to = d["to"]["map"]
            src = set(d.get("maps", [d["map"]]))
            if to in mset and not (src & mset) and not d.get("world_script"):
                used = any(to in edges.get(s, ()) for s in src) or (d.get("world") in (1, 2) and to in edges.get(d["world"], ()))
                ds.append({"id": d["id"], "map": d["map"], "routine": f"{d['routine']:#x}" if d.get("routine") is not None else None,
                           "to": to, "world": d.get("world"), "late_edge": used,
                           "requires": d.get("requires", [])[:3]})
        chests = sorted(f for m2 in ms for f in chests_by_map.get(m2, []))
        dungeons.append({
            "key": key, "name": f"{name} Key", "dungeon": name, "world": DUNGEONS[name]["world"], "maps": ms,
            "doors": [x for x in ds if x["late_edge"]],
            "doors_not_on_late_edges": len([x for x in ds if not x["late_edge"]]),
            "chambers": [r["encounter"] for r in rows if r["map"] in mset],
            "chests": len(chests),
            "late_reached_maps": len([m2 for m2 in ms if any(m2 in late_maps[g] for g in late_maps)]),
        })
    return {
        "about": ("The hub's boss chambers (story views) and dungeon keys: tools/ap_hub_bosses.py "
                  "(docs/ap/seed_format.md 'Hub', docs/handoff/hub.md). The APWorld copies `bosses` rows (encounter, "
                  "name, map, maps, view, game) into the seed; the rest is reference for the proofs and the port."),
        "generated": datetime.date.today().isoformat(),
        "excluded": {"losable (D5)": {str(e): names.get(e) for e in H.LOSABLE},
                     "goal": {"539": "Indalecio", "540": "Indalecio (Limiter Off)"},
                     "optional, own guards": {str(e): names.get(e) for e in H.OPTIONAL_BOSSES},
                     "Cave of Trials": "13 bosses, flags 950-962 (cleared in the hub state)"},
        "bosses": rows,
        "dungeons": dungeons,
        "town_chambers": {str(m): t for m, t in TOWN_OF.items()},
        "notes": [
            "p5 = the late state does not reach the chamber and its view's maps don't open the way: Shin 569 / "
            "Berle 575 (Eluria Tower past 557: door 0x7808, a headless look; Berle's tower-wide view may be enough), "
            "Marsilio 305 (the arena's doors). Bark 454 and the Synard 269 are opened by their views' approach rooms "
            "(453, 264); the Field of Intelligence's 554 by the hub state (flag 175 clear).",
            "Lasguss Mountain (no chest, only the optional XINE) and the Portal of Sealing (not reached at chapter 60: "
            "313 -> 400 only at 52-2000; one chest, the Heraldry lab's) gate nothing the hub can use: candidates to "
            "drop from the key list (18 keys), or P5 for the Portal.",
            "Fienal: the D6 requirement (`won`) gates its entrance; a Fienal key on top is optional (proposal: none).",
            "Every view lists the party flags 740-744 at the hub's value (0) so a scene's party bookkeeping drops, and "
            "flag 745 at 0 (hubvm: the set-up fades in only while it is 0).",
        ],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "docs", "ap", "hub_bosses.json"))
    args = ap.parse_args()
    if not args.disc1:
        sys.exit("set SS_DISC1 (or --disc1)")
    out = build(args)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    for r in out["bosses"]:
        v = r["view"]
        print(f"{r['encounter']:4d} {r['name'][:22]:22s} map {r['map']:3d} {str(r['dungeon'])[:20]:20s} "
              f"late {int(any(r['late'].values()))} view {'-' if v is None else str(v['chapter']) + '/' + str(v['step'])} "
              f"flags {0 if v is None else len(v['flags'])} cut {r['cut'].get('to', r['cut']['at'])[:40]}")
    for d in out["dungeons"]:
        print(f"key {d['key']:2d} {d['dungeon']:22s} doors {len(d['doors'])} chambers {d['chambers']} chests {d['chests']} "
              f"maps {len(d['maps'])} reached {d['late_reached_maps']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
