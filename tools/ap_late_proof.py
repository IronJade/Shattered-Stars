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
protagonist bytes swapped. The script globals and flags reach a `--map` start through
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
world map. A location proven there can be reached between the disc switch and the Final
Save Point through the teleporter. Its result goes to the output's "teleporter" key.

Usage:
  tools/ap_late_proof.py --bin BIN --save CARD.mcd --save-frame 13 [--game claude --game rena]
                         [--jobs 3] [--resume] [--work scratchpad/late_proof] [-o docs/ap/late_proof.json]
SS_DISC1 / SS_DISC2 give the discs (or --disc1 / --disc2). --reparse rebuilds the result from
the logs in --work; --resume reuses the finished node logs there. A game takes about an hour
with 6 jobs; run both games at --jobs 3 each (or one after the other), so the machine never
has more than 6 game processes from it.
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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = os.path.join(ROOT, "apworld", "so2")
# The Final Save Point, and Energy Nede's world map: from the save the party walks back down out
# of Fienal (Prima p. 130 describes leaving it after the save), a way the sweep can't follow
# through Fienal's stair rooms, whose exits depend on the room the party came from.
ROOTS = [671, 2]
TELEPORTER_ROOTS = [2]
TELEPORTER_CHAPTER = 38   # the Nede story's first walk on the world map (chapter 38 is stored at map 246)
SCRIPT_FLAGS = 0x170 * 8
KEEP_FLAGS = {1, 2, 4, 730} | set(range(66, 94))  # the game, Private Action mode, the save, key items
PARTY = "1,10"   # Rena and Noel join the --map start's Claude (both are in every party at chapter 60)

ENTRY_END = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) end (.*?) frames")
ENTRY_START = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) code (\d+) start")
PLAY_DONE = re.compile(r"\[sweep\] map \d+ vblank \d+ play: control on map (\d+) after (\d+) of (\d+) steps")
DONE = re.compile(r"\[sweep\] map (\d+) vblank \d+ done")
ENTRIES = re.compile(r"\[sweep\] map (\d+) vblank \d+ entries (\d+)")
CHECK = re.compile(r"\[ap\] check (\d+) \(")
STATE = re.compile(r"\[sweep\] map \d+ vblank \d+ (settled|entry \d+) state (.*)$")
SHOW = "G1cf:1,G1c0:2,F157"   # the floor, the previous map, Fienal's stair flag (event_sweep.cpp)


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
    "teleporter": chapter 38 step 0, flag 730 cleared (the teleporter state, module doc)."""
    gs, flags = snap[:0x2A0], snap[0x2A0:0x2A0 + 0x170]
    words = {}
    for off in range(0, 0x2A0, 4):
        g = 0xA0 + off
        v = struct.unpack_from("<i", gs, off)[0]
        # Chapter and step; the protagonist bytes (G[0xc4]); everything from the names on
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
    on = [n for n in range(SCRIPT_FLAGS) if flags[n >> 3] >> (n & 7) & 1 and n not in cleared]
    if game == "claude" and 1 not in on:
        on.append(1)
    on = sorted(n for n in on if not (game == "rena" and n == 1))
    flag_text = ",".join(str(n) for n in on) + (",!1" if game == "rena" else "")
    glob_text = ",".join(f"{g:x}={v:#x}" if v >= 0 else f"{g:x}={v}" for g, v in sorted(words.items()))
    summary = {"chapter": words[0xA0], "step": words[0xA4], "flags_set": len(on), "flags_cleared": len(cleared),
               "globals": len(words), "flag_730": 730 in on, "floor_G1cf": gs[0x1CF - 0xA0],
               "previous_map_G1c0": struct.unpack_from("<h", gs, 0x1C0 - 0xA0)[0], "party_added": PARTY}
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
            "options": {"protagonist": "any", "expel_teleporter": teleporter},
            "items": {str(data.ITEM_BASE + 757): {"name": "Blueberry", "kind": "item", "game_id": 757}},
            "locations": locs, "patches": lines, "start": {"items": [], "fol": 0, "flags": []},
            "goal": {"encounters": list(data.GOAL_ENCOUNTERS)}}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(seed, f, indent=1)
    return len(locs)


# ---- Runs ----

def run_map(args, state, target, route, seed, logpath):
    """Replays route (root, steps) from its root and sweeps the map it ends on (resuming after a
    crash or hang). Returns the kept lines of every run."""
    root, route = route
    glob_text, flag_text = state
    env = dict(os.environ)
    env.update({
        "SS_EVENT_SWEEP": "1", "SS_EVENT_SWEEP_FRAMES": str(args.frames), "SS_BATTLE_STUB": "1",
        "SS_EVENT_SWEEP_DISC": "2", "SS_DISC1": args.disc1, "SS_DISC2": args.disc2,
        "SS_SCRIPT_GLOBALS": glob_text, "SS_SWEEP_FLAGS": flag_text, "SS_SWEEP_PARTY": PARTY, "SS_SWEEP_SHOW": SHOW,
        "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy",
        # Never the player's settings: a scratch config directory and no memory card.
        "XDG_CONFIG_HOME": os.path.join(os.path.abspath(args.work), "xdg"),
    })
    env.pop("SS_SWEEP_PLAY", None)
    if route:
        env["SS_SWEEP_PLAY"] = ",".join(f"{m}:{k}{i}" for m, k, i in route)
        env["SS_SWEEP_PLAY_SWEEP"] = "1"
    all_lines, skip = [], 0
    with open(logpath, "w") as log:
        log.write(f"[driver] root {root} route {env.get('SS_SWEEP_PLAY', '(none)')}\n")
        for _ in range(40):
            env["SS_EVENT_SWEEP_SKIP"] = str(skip)
            cmd = [args.bin, "--headless", "--no-memcard", "--frames", "2000000", "--map", str(root),
                   "--ap-seed", seed]
            p = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
            last, start, lines, reloads = [time.time()], time.time(), [], [0]

            def reader():
                for line in p.stdout:
                    lines.append(line.rstrip("\n"))
                    if "settle (reload)" in line:
                        reloads[0] += 1
                    # Any sweep line but play mode's status every 600 frames shows progress.
                    if line.startswith("[sweep]") and (" play: " not in line or " play: step" in line
                                                       or " play: control" in line):
                        last[0] = time.time()

            t = threading.Thread(target=reader, daemon=True)
            t.start()
            why = ""
            while p.poll() is None:
                time.sleep(0.5)
                now = time.time()
                # A map whose main script moves on after every reload (map 413 -> 416) would
                # loop in the sweep's settle forever: stop it and go on after the entry.
                if now - last[0] > args.stall or now - start > args.timeout or reloads[0] > 6:
                    why = "hang" if now - last[0] > args.stall else "timeout" if now - start > args.timeout else "reload loop"
                    p.kill()  # our own child, by its handle
                    break
            p.wait()
            t.join(timeout=5)
            keep = [x for x in lines if x.startswith(("[sweep]", "[ap] check", "[port]"))]
            log.write("\n".join(keep) + "\n")
            log.write(f"[driver] exit {p.returncode}{' (' + why + ': killed)' if why else ''}\n")
            log.flush()
            all_lines += keep
            if any(DONE.search(x) for x in keep):
                break
            # Died or hung: go on after the entry in progress (the route is replayed).
            started = [int(mo.group(2)) for x in keep if (mo := ENTRY_START.search(x))]
            found = [int(mo.group(2)) for x in keep if (mo := ENTRIES.search(x))]
            if not started:
                break
            skip = max(started) + 1
            if found and skip >= found[0]:
                break
        log.write("[driver] complete\n")
    return all_lines


def parse(lines, target, route):
    """What one map's runs found: the map swept, its exits {(map, entry kind, index): state on
    arrival}, the state it settled in, the checks, notes."""
    swept, exits, checks, notes = (target if not route else None), {}, set(), []
    settled, last_exit = "", None
    for x in lines:
        if mo := STATE.search(x):
            text = ",".join(mo.group(2).split())
            if mo.group(1) == "settled":
                settled = settled or text
            elif last_exit is not None and (last_exit[0] != -1 or text != settled):
                # Another map, or the same map in another state (a latch, a floor counter).
                exits[last_exit if last_exit[0] != -1 else (-1,) + last_exit[1:]] = text
            continue
        last_exit = None
        if mo := PLAY_DONE.search(x):
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
        elif mo := CHECK.search(x):
            checks.add(int(mo.group(1)))
        elif x.startswith("[driver] exit") and x != "[driver] exit 0":
            notes.append(x[9:])
        elif "main script left" in x:
            notes.append(x.split(" vblank ")[1].split(" ", 1)[1])
    return swept, exits, settled, checks, notes


def read_log(path):
    return [x.rstrip("\n") for x in open(path, errors="replace")]


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


def bfs(args, game, state, seed, stateful, root_maps=ROOTS):
    """The search. A node is a map, or for a stateful map (stateful_maps) a map with the values
    of the state variables its script reads, which the sweep prints (SHOW) for each entry that
    leads there."""
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

    def done(node, lines):
        target = map_of(node)
        swept, exits, settled, c, n = parse(lines, target, route[node][1])
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
        edges.setdefault(swept, set()).update(to for to, _, _ in exits if to != swept)
        new = []
        for (to, kind, index), st in sorted(exits.items()):
            k = ident(to, st)
            if to >= 0 and k not in seen:
                seen.add(k)
                route[k] = (here[0], here[1] + [(swept, kind, index)])
                new.append(k)
        return new

    if args.reparse:
        queue = list(roots)
        while queue:
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
            while running:
                finished, _ = wait(running, return_when=FIRST_COMPLETED)
                for fut in finished:
                    k = running.pop(fut)
                    new = done(k, fut.result())
                    print(f"[{game}] {k}: -> {sorted(edges.get(swept_of.get(k), []))} checks {len(checks[k])} "
                          f"(nodes {len(swept_of)}, maps {len(edges)}, running {len(running) + len(new)})", flush=True)
                    for x in new:
                        running[submit(pool, x)] = x
    proven = sorted({c for cs in checks.values() for c in cs})
    by_map = {}
    for k, cs in checks.items():
        by_map.setdefault(swept_of.get(k, map_of(k)), set()).update(cs)
    return {
        "maps": sorted(edges),
        "nodes": len(swept_of),
        "edges": {str(m): sorted(edges[m]) for m in sorted(edges)},
        "routes": {k: f"{route[k][0]}|" + ",".join(f"{a}:{kk}{i}" for a, kk, i in route[k][1])
                   for k in sorted(swept_of, key=lambda x: (map_of(x), x))},
        "checks": {str(m): sorted(by_map[m]) for m in sorted(by_map) if by_map[m]},
        "proven": proven,
        "notes": {k: [f"{n} (x{c})" if c > 1 else n for n, c in collections.Counter(notes[k]).items()]
                  for k in sorted(notes)},
    }


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
    ap.add_argument("--state", choices=["late", "teleporter"], default="late",
                    help="late: the Final Save Point; teleporter: chapter 38 with the Expel teleporter (open world)")
    args = ap.parse_args()
    tele = args.state == "teleporter"
    if tele and args.work == os.path.join(ROOT, "scratchpad", "late_proof"):
        args.work = os.path.join(ROOT, "scratchpad", "late_proof", "teleporter")
    games = args.game or ["claude", "rena"]
    if not args.disc1:
        sys.exit("set SS_DISC1 (or --disc1)")
    if not args.disc2:
        args.disc2 = args.disc1.replace("Disc 1", "Disc 2")
    os.makedirs(os.path.join(args.work, "xdg"), exist_ok=True)
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
    else:
        out = full
        out["about"] = ("Locations proven reachable after the Final Save Point, per game: tools/ap_late_proof.py "
                        "(docs/ap/apworld.md, 'Logic: the late proof'). apworld/so2/rules.py keeps progression "
                        "out of every other location.")
    out.setdefault("games", {})
    roots = TELEPORTER_ROOTS if tele else ROOTS
    for game in games:
        glob_text, flag_text, summary = build_state(snap, field, game, args.state)
        result = bfs(args, game, (glob_text, flag_text), seed, stateful, roots)
        result["state"] = dict(summary, roots=roots, disc=2, frames=args.frames, seed_locations=count,
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
