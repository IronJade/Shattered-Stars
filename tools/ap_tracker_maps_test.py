#!/usr/bin/env python3
"""Headless end-to-end check of `shattered-stars --render-tracker-maps` (docs/ap/tracker_maps.md).

    SS_DISC1=DISC1.cue tools/ap_tracker_maps_test.py BINARY [--scratch DIR]

Runs the command twice under tools/memcap.py's cap (offscreen video, dummy audio) with a
scratch HOME, Documents and XDG_CONFIG_HOME:
- without DIR: a scratch <HOME>/PopTracker folder exists, so the images must land in
  <HOME>/PopTracker/user-override/so2_shattered_stars (PopTracker's own default);
- with DIR (a scratch folder).
Checks: exit code 0 both times; every image of docs/ap/tracker_maps.json (the area and world
images, and format 1.1's rooms) is written with the layout's size (PNG IHDR); both runs give
the same bytes; nothing else appears anywhere under the scratch root (no config.ini, no log, no
card, no stray file). Prints one line per check, each run's time and the set's size on disk,
and exits 1 on a failure. The scratch folder is removed afterwards unless --keep; the two
runs' logs go beside it.
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from memcap import hit_cap, run_capped  # noqa: E402

UID = "so2_shattered_stars"


def files_under(root):
    out = {}
    for base, _, names in os.walk(root):
        for n in names:
            p = os.path.join(base, n)
            out[os.path.relpath(p, root)] = hashlib.sha256(open(p, "rb").read()).hexdigest()
    return out


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(33)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("binary")
    p.add_argument("--scratch", default=os.path.join(REPO, "scratchpad", "maprender", "e2e"))
    p.add_argument("--keep", action="store_true")
    p.add_argument("--timeout", type=float, default=600)
    a = p.parse_args()
    if not os.environ.get("SS_DISC1"):
        raise SystemExit("set SS_DISC1 to the NA disc 1 (.cue)")
    layout = json.load(open(os.path.join(REPO, "docs", "ap", "tracker_maps.json"), encoding="utf-8"))
    expected = {img["path"]: (img["width"], img["height"]) for img in layout["images"]}
    expected.update({room["path"]: (room["width"], room["height"]) for room in layout.get("rooms", [])})

    root = os.path.abspath(a.scratch)
    shutil.rmtree(root, ignore_errors=True)
    home, xdg, out2 = os.path.join(root, "home"), os.path.join(root, "xdg"), os.path.join(root, "out2")
    os.makedirs(os.path.join(home, "PopTracker"))
    os.makedirs(os.path.join(home, "Documents"))
    os.makedirs(xdg)
    env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", HOME=home, XDG_CONFIG_HOME=xdg)
    env.pop("XDG_DOCUMENTS_DIR", None)
    env.pop("SS_TRACKER_DOTS", None)
    env.pop("SS_TRACKER_ONLY", None)
    before = files_under(root)

    failures = []

    def check(ok, what):
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            failures.append(what)

    def run(args, log):
        log = os.path.join(os.path.dirname(root), log)
        t0 = time.time()
        with open(log, "w") as f:
            rc, timed_out, at_cap = run_capped([a.binary, "--render-tracker-maps"] + args, a.binary, a.timeout,
                                               stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, env=env)
        print(f"     {' '.join(args) or '(default folder)'}: {time.time() - t0:.1f} s")
        text = open(log, errors="replace").read()
        at_cap = at_cap or (not timed_out and hit_cap(rc, text, killed=timed_out))
        check(rc == 0 and not timed_out and not at_cap, f"{' '.join(args) or '(default folder)'}: exit 0 "
              f"(rc {rc}, timed out {timed_out}, memory cap {at_cap})")

    run([], "e2e_default.log")
    run([out2], "e2e_dir.log")
    default = os.path.join(home, "PopTracker", "user-override", UID)
    for name, folder in (("default folder", default), ("DIR", out2)):
        sizes = {}
        for rel in expected:
            path = os.path.join(folder, rel)
            sizes[rel] = png_size(path) if os.path.exists(path) else None
        missing = [r for r, s in sizes.items() if s is None]
        wrong = [r for r, s in sizes.items() if s is not None and tuple(s) != tuple(expected[r])]
        check(not missing, f"{name}: all {len(expected)} images written ({len(missing)} missing)")
        check(not wrong, f"{name}: sizes as the layout says ({len(wrong)} differ: {wrong[:3]})")
    after = files_under(root)
    new = {k: v for k, v in after.items() if k not in before}
    allowed = {os.path.relpath(os.path.join(default, r), root) for r in expected} | \
              {os.path.relpath(os.path.join(out2, r), root) for r in expected}
    stray = sorted(set(new) - allowed)
    check(not stray, f"nothing else written under the scratch root ({len(stray)} stray: {stray[:5]})")
    check(not os.listdir(xdg), "XDG_CONFIG_HOME untouched (no config.ini, no log)")
    same = [r for r in expected
            if new.get(os.path.relpath(os.path.join(default, r), root)) == new.get(os.path.relpath(os.path.join(out2, r), root))]
    check(len(same) == len(expected), f"the same bytes in both runs ({len(same)}/{len(expected)})")
    def size(prefix):
        return sum(os.path.getsize(os.path.join(out2, r)) for r in expected
                   if r.startswith(prefix) and os.path.exists(os.path.join(out2, r)))
    rooms = sum(1 for r in expected if r.startswith("images/rooms/"))
    print(f"{len(expected)} images ({len(expected) - rooms} area and world, {rooms} rooms), "
          f"{size('') / 1e6:.1f} MB per run (rooms {size('images/rooms/') / 1e6:.1f} MB)")
    if not a.keep:
        shutil.rmtree(root, ignore_errors=True)
    if failures:
        print(f"{len(failures)} check(s) failed")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
