#!/usr/bin/env python3
"""Run the port's event sweep (SS_EVENT_SWEEP, engine/src/game/field/event_sweep.h) over maps.

Each map runs in its own headless process started with --map N (a new game on that map)
and SS_BATTLE_STUB=1 (battles end at once in victory). The process prints "[sweep]" lines:
the entries it found, then one "start" and one "end" line per entry. A process that dies
(crash) or stops making progress (hang: no new line for --stall seconds) is recorded
against the entry it was running, and the map is run again from the next entry
(SS_EVENT_SWEEP_SKIP). "[port]" lines (stand-in services, "not ported yet") are kept
with the entry they appeared in.

Usage:
  event_sweep.py --bin engine/build-menu/shattered-stars --disc1 CUE [--disc2 CUE]
                 --out DIR [--maps 0-826] [--jobs 4] [--frames 3600]
Writes DIR/map_NNN.log (raw output) and DIR/results.json, and prints a summary.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

START = re.compile(r"\[sweep\] map (\d+) vblank (\d+) entry (\d+) (\w)(\d+) code (\d+) start")
END = re.compile(r"\[sweep\] map (\d+) vblank (\d+) entry (\d+) (\w)(\d+) end (.*?) frames (\d+) standins (\d+)(?: last (\S+))? unknown_ops (\d+)(?: \| (.*))?$")
ENTRIES = re.compile(r"\[sweep\] map (\d+) vblank \d+ entries (\d+):?(.*)$")
DONE = re.compile(r"\[sweep\] map (\d+) vblank \d+ done (.*)$")
OTHER = re.compile(r"\[sweep\] map (\d+) vblank \d+ (settle timeout.*|main script left.*|settled after.*)$")


def parse_maps(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def run_once(args, m, skip, log):
    env = dict(os.environ)
    env.update({
        "SS_EVENT_SWEEP": "1", "SS_EVENT_SWEEP_FRAMES": str(args.frames), "SS_EVENT_SWEEP_SKIP": str(skip),
        "SS_BATTLE_STUB": "1", "SS_DISC1": args.disc1, "SDL_VIDEO_DRIVER": "offscreen",
        "SDL_AUDIO_DRIVER": "dummy", "SS_MEMCARD1": args.card,
    })
    if args.disc2:
        env["SS_DISC2"] = args.disc2
    cmd = [args.bin, "--headless", "--frames", "2000000", "--map", str(m)]
    p = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace", bufsize=1)
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
            p.kill()  # this process only (our own child)
            break
    p.wait()
    t.join(timeout=5)
    log.write("\n".join(lines) + "\n")
    log.write(f"[driver] exit {p.returncode}{' (hang: killed)' if hung else ''}\n")
    log.flush()
    return lines, p.returncode, hung


def sweep_map(args, m):
    result = {"map": m, "entries": [], "events": [], "found": None, "notes": []}
    skip = 0
    with open(os.path.join(args.out, f"map_{m:03d}.log"), "w") as log:
        for attempt in range(40):
            lines, code, hung = run_once(args, m, skip, log)
            current = None
            last_ended = None
            port_lines = []
            done = False
            for line in lines:
                if (mo := ENTRIES.search(line)) and result["found"] is None:
                    result["found"] = int(mo.group(2))
                    result["entry_list"] = mo.group(3).split()
                elif mo := START.search(line):
                    current = {"n": int(mo.group(3)), "kind": mo.group(4), "index": int(mo.group(5)),
                               "code": int(mo.group(6)), "port": []}
                    port_lines = current["port"]
                elif mo := END.search(line):
                    if current is None:
                        continue
                    current.update(result=mo.group(6), frames=int(mo.group(7)), standins=int(mo.group(8)),
                                   standin=mo.group(9), unknown_ops=int(mo.group(10)), state=mo.group(11))
                    result["entries"].append(current)
                    last_ended = current["n"]
                    current = None
                elif mo := DONE.search(line):
                    done = True
                    result["done"] = mo.group(2)
                elif mo := OTHER.search(line):
                    result["notes"].append(mo.group(2))
                elif line.startswith("[port]"):
                    if current is not None:
                        current["port"].append(line)
                    else:
                        result.setdefault("port", []).append(line)
            if done:
                break
            # Died or hung: blame the entry in progress and go on after it.
            what = "hang" if hung else f"crash (exit {code})"
            if current is not None:
                current.update(result=what, frames=None, standins=None, standin=None, unknown_ops=None, state=None)
                result["entries"].append(current)
                skip = current["n"] + 1
            elif last_ended is not None:
                result["notes"].append(f"{what} reloading the map after entry {last_ended}")
                skip = last_ended + 1
            else:
                result["notes"].append(f"{what} before the map settled")
                break
            if result["found"] is not None and skip >= result["found"]:
                break
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--disc1", required=True)
    ap.add_argument("--disc2")
    ap.add_argument("--out", required=True)
    ap.add_argument("--maps", default="0-826")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--stall", type=float, default=120.0, help="seconds without a [sweep] line before a hang is called")
    ap.add_argument("--card", default=None, help="memory card image (a scratch copy)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.card is None:
        args.card = os.path.join(args.out, "card.mcd")
    maps = parse_maps(args.maps)
    results = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(lambda m: sweep_map(args, m), maps):
            results[r["map"]] = r
            bad = [e for e in r["entries"] if e["result"] not in ("ok",) and not e["result"].startswith("map ")]
            print(f"map {r['map']:3d}: {r['found']} entries, {len(bad)} not ok"
                  + (f" ({', '.join(e['kind'] + str(e['index']) + ' ' + e['result'] for e in bad[:6])})" if bad else "")
                  + (f" notes: {'; '.join(r['notes'])}" if r["notes"] else ""), flush=True)
    with open(os.path.join(args.out, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
