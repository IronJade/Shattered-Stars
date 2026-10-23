#!/usr/bin/env python3
"""Which Archipelago locations stay reachable after the Final Save Point (docs/ap/apworld.md,
"Logic: the late proof").

The APWorld assumes a location it may fill with progression can be reached at any time from
the moment it is in logic. In Star Ocean 2 a map can close: Expel is left behind at the disc
switch (Virtual Expel reopens it after the Final Save Point), and towns change with the story.
This tool proves, headless in the port, which chests, gifts and boss fights can still be done
in the state the story reaches at the Final Save Point (Fienal 9F, map 671): chapter 60, the
save's step, flag 730 (the save that opens Virtual Expel), disc 2.

The method is a breadth-first search over event sweeps (SS_EVENT_SWEEP,
engine/src/game/field/event_sweep.cpp), one headless `--map N` process per map:
- the state: script globals (chapter G[0xa0], step G[0xa4]), script flags (730; "!1" clears
  flag 1 for Rena's game) and party members, as SS_SCRIPT_GLOBALS / SS_SWEEP_FLAGS /
  SS_SWEEP_PARTY set them at the start;
- the sweep fires every trigger (T), talk (K) and pickpocket (S) entry the map registers, each
  from the same snapshot, answering windows and choices; battles end at once in victory
  (SS_BATTLE_STUB);
- an entry that leaves the player in control on another map is an edge; so is a map whose
  main script moves on at once. Private Action entries (P) are not followed;
- the search starts at the roots (671) and sweeps each map it reaches once;
- the port loads a proof seed with every chest, gift and boss location of
  apworld/so2/data.py (placeholders patched in, so gifts are seen), and logs `[ap] check ID`
  for each one an entry reaches. A location is proven in a game when a sweep of a map the
  search reached checks it.

It is conservative: every entry runs from the map's starting state, so doors behind a switch,
a puzzle or a story flag stay shut, and a choice takes its first answer. Locations it does not
prove only lose the right to hold progression.

Usage:
  tools/ap_late_proof.py --bin engine/build-enemy/shattered-stars --game claude --game rena
                         [--jobs 6] [--work scratchpad/enemy/late] [-o docs/ap/late_proof.json]
SS_DISC1 / SS_DISC2 give the discs (or --disc1 / --disc2). --reparse rebuilds the JSON from
the logs in --work. The run takes about an hour per game with 6 jobs.
"""
import argparse
import datetime
import importlib.util
import json
import os
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = os.path.join(ROOT, "apworld", "so2")

# The state at the Final Save Point (the user's save there: chapter 60, step 4000, disc 2).
CHAPTER, STEP = 60, 0xFA0
GAMES = {
    # flags: 730 = saved at the Final Save Point (Virtual Expel opens); flag 1 = Claude's game,
    # which a --map start sets. party: Rena (1) and Noel (10) join in the story before 60.
    "claude": {"flags": "730", "party": "1,10"},
    "rena": {"flags": "730,!1", "party": "1,10"},
}
ROOTS = [671]
WORLD_MAPS = {1, 2}

ENTRY_END = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) end (.*?) frames")
ENTRY_START = re.compile(r"\[sweep\] map (\d+) vblank \d+ entry (\d+) (\w)(\d+) code (\d+) start")
LEFT = re.compile(r"\[sweep\] map (\d+) vblank \d+ main script left for map (\d+)")
DONE = re.compile(r"\[sweep\] map (\d+) vblank \d+ done")
ENTRIES = re.compile(r"\[sweep\] map (\d+) vblank \d+ entries (\d+)")
CHECK = re.compile(r"\[ap\] check (\d+) \(")


def load(name):
    spec = importlib.util.spec_from_file_location(f"so2_{name}", os.path.join(WORLD, f"{name}.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def proof_seed(path):
    """A solo seed with every chest, gift and boss location of data.py, each holding a
    Blueberry (item 757) behind its placeholder."""
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
            "slot": {"name": "Proof", "id": 1}, "options": {"protagonist": "any"},
            "items": {str(data.ITEM_BASE + 757): {"name": "Blueberry", "kind": "item", "game_id": 757}},
            "locations": locs, "patches": lines, "start": {"items": [], "fol": 0, "flags": []},
            "goal": {"encounters": list(data.GOAL_ENCOUNTERS)}}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(seed, f, indent=1)
    return len(locs)


def run_map(args, game, m, seed, logdir):
    """Sweeps map m (resuming after a crash or hang); returns (lines of every run)."""
    g = GAMES[game]
    env = dict(os.environ)
    env.update({
        "SS_EVENT_SWEEP": "1", "SS_EVENT_SWEEP_FRAMES": str(args.frames), "SS_BATTLE_STUB": "1",
        "SS_EVENT_SWEEP_DISC": "2", "SS_DISC1": args.disc1, "SS_DISC2": args.disc2,
        "SS_SCRIPT_GLOBALS": f"a0={CHAPTER},a4={STEP:#x}", "SS_SWEEP_FLAGS": g["flags"], "SS_SWEEP_PARTY": g["party"],
        "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy",
        # Never the player's settings: a scratch config directory and no memory card.
        "XDG_CONFIG_HOME": os.path.join(os.path.abspath(args.work), "xdg"),
    })
    all_lines = []
    skip = 0
    with open(os.path.join(logdir, f"map_{m:03d}.log"), "w") as log:
        for _ in range(40):
            env["SS_EVENT_SWEEP_SKIP"] = str(skip)
            cmd = [args.bin, "--headless", "--no-memcard", "--frames", "2000000", "--map", str(m), "--ap-seed", seed]
            p = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
            last = [time.time()]
            lines = []

            def reader():
                for line in p.stdout:
                    lines.append(line.rstrip("\n"))
                    if line.startswith("[sweep]"):
                        last[0] = time.time()

            t = threading.Thread(target=reader, daemon=True)
            t.start()
            hung = False
            while p.poll() is None:
                time.sleep(0.5)
                if time.time() - last[0] > args.stall:
                    hung = True
                    p.kill()  # our own child, by its handle
                    break
            p.wait()
            t.join(timeout=5)
            keep = [x for x in lines if x.startswith(("[sweep]", "[ap] check", "[port]"))]
            log.write("\n".join(keep) + "\n")
            log.write(f"[driver] exit {p.returncode}{' (hang: killed)' if hung else ''}\n")
            log.flush()
            all_lines += keep
            if any(DONE.search(x) for x in keep):
                break
            # Died or hung: go on after the entry in progress.
            started = [int(mo.group(2)) for x in keep if (mo := ENTRY_START.search(x))]
            found = [int(mo.group(2)) for x in keep if (mo := ENTRIES.search(x))]
            if not started:
                break
            skip = max(started) + 1
            if found and skip >= found[0]:
                break
    return all_lines


def parse(lines):
    """(edges, checks, entry count, notes) from one map's sweep lines."""
    edges, checks, notes = set(), set(), []
    for x in lines:
        if mo := ENTRY_END.search(x):
            kind, result = mo.group(3), mo.group(5)
            if kind != "P" and result.startswith("map "):
                edges.add(int(result.split()[1]))
        elif mo := LEFT.search(x):
            edges.add(int(mo.group(2)))
        elif mo := CHECK.search(x):
            checks.add(int(mo.group(1)))
        elif "[driver] exit" in x and "exit 0" not in x:
            notes.append(x)
    return edges, checks, notes


def read_log(path):
    return [x.rstrip("\n") for x in open(path, errors="replace")]


def bfs(args, game, seed):
    logdir = os.path.join(args.work, game)
    os.makedirs(logdir, exist_ok=True)
    seen, edges, checks, notes = set(ROOTS), {}, {}, {}

    def done(m, lines):
        e, c, n = parse(lines)
        edges[m], checks[m] = sorted(e), sorted(c)
        if n:
            notes[m] = n
        new = [x for x in sorted(e) if x not in seen and x >= 0]
        seen.update(new)
        return new

    if args.reparse:
        queue = list(ROOTS)
        while queue:
            m = queue.pop(0)
            path = os.path.join(logdir, f"map_{m:03d}.log")
            if not os.path.exists(path):
                notes[m] = ["no log"]
                continue
            queue += done(m, read_log(path))
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            running = {pool.submit(run_map, args, game, m, seed, logdir): m for m in ROOTS}
            while running:
                finished, _ = wait(running, return_when=FIRST_COMPLETED)
                for fut in finished:
                    m = running.pop(fut)
                    new = done(m, fut.result())
                    print(f"[{game}] map {m}: -> {edges[m]} checks {len(checks[m])} (swept {len(edges)}, queued {len(running) + len(new)})",
                          flush=True)
                    for x in new:
                        running[pool.submit(run_map, args, game, x, seed, logdir)] = x
    proven = sorted({c for cs in checks.values() for c in cs})
    return {
        "maps": sorted(edges),
        "edges": {str(m): edges[m] for m in sorted(edges)},
        "checks": {str(m): checks[m] for m in sorted(checks) if checks[m]},
        "proven": proven,
        "notes": {str(m): notes[m] for m in sorted(notes)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(ROOT, "engine", "build", "shattered-stars"))
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1"))
    ap.add_argument("--disc2", default=os.environ.get("SS_DISC2"))
    ap.add_argument("--game", action="append", choices=sorted(GAMES))
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--frames", type=int, default=2400)
    ap.add_argument("--stall", type=float, default=120.0)
    ap.add_argument("--work", default=os.path.join(ROOT, "scratchpad", "late_proof"))
    ap.add_argument("--reparse", action="store_true", help="rebuild the result from the logs in --work")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "docs", "ap", "late_proof.json"))
    args = ap.parse_args()
    games = args.game or sorted(GAMES)
    if not args.reparse:
        if not args.disc1:
            sys.exit("set SS_DISC1 (or --disc1)")
        if not args.disc2:
            args.disc2 = args.disc1.replace("Disc 1", "Disc 2")
    os.makedirs(os.path.join(args.work, "xdg"), exist_ok=True)
    seed = os.path.join(os.path.abspath(args.work), "proof.apso2")
    count = proof_seed(seed)
    out = {}
    if os.path.exists(args.out):
        out = json.load(open(args.out))
    out.update({
        "about": "Locations proven reachable after the Final Save Point, per game (tools/ap_late_proof.py; "
                 "docs/ap/apworld.md, 'Logic: the late proof').",
        "state": {"chapter": CHAPTER, "step": STEP, "disc": 2, "roots": ROOTS,
                  "games": {g: GAMES[g] for g in sorted(GAMES)}, "frames": args.frames,
                  "seed_locations": count},
    })
    out.setdefault("games", {})
    for game in games:
        result = bfs(args, game, seed)
        result["generated"] = datetime.date.today().isoformat()
        out["games"][game] = result
        print(f"[{game}] {len(result['maps'])} maps, {len(result['proven'])} locations proven", flush=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            json.dump(out, f, indent=1, sort_keys=True)
            f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
