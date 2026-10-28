#!/usr/bin/env python3
"""Headless end-to-end check of `shattered-stars --render-tracker-maps` (docs/ap/tracker_maps.md).

    SS_DISC1=DISC1.cue tools/ap_tracker_maps_test.py BINARY [--scratch DIR]

Runs the command under tools/memcap.py's cap (offscreen video, dummy audio) with a scratch HOME,
Documents and XDG_CONFIG_HOME, twice in full:
- without DIR: a scratch <HOME>/PopTracker folder exists, so the images must land in
  <HOME>/PopTracker/user-override/so2_shattered_stars (PopTracker's own default), and the
  Universal Tracker zip beside that folder (<HOME>/PopTracker/user-override/so2_shattered_stars_ut.zip);
- with DIR (a scratch folder): the zip in DIR's parent;
and twice on one room (SS_TRACKER_ONLY): with `--no-ut-zip` (no zip anywhere) and with
`--ut-zip FILE` (the zip there).
Checks: exit code 0 each time; every image of docs/ap/tracker_maps.json (the area and world
images, and format 1.1's rooms) is written with the layout's size (PNG IHDR); both full runs
give the same bytes; the zip (docs/ap/tracker_maps.md "The Universal Tracker zip"): Python's
zipfile reads it and its CRCs check (testzip), every entry stored, dated 1980-01-01, sorted;
its entries are exactly the layout's images, byte for byte the rendered PNGs, plus the boards at
tools/poptracker/boards.json's sizes and the 64x64 RGBA player marker; both full runs' zips are
the same bytes; nothing else appears anywhere under the scratch root (no config.ini, no log, no
card, no .part, no stray file). Prints one line per check, each run's time, the set's size on
disk and the zip's, and exits 1 on a failure. The scratch folder is removed afterwards unless
--keep; the runs' logs go beside it.
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
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from memcap import hit_cap, run_capped  # noqa: E402

UID = "so2_shattered_stars"
UT_ZIP = "so2_shattered_stars_ut.zip"


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
    return png_head(head)


def png_head(head, colour_type=False):
    """(width, height) of a PNG's first bytes, with its colour type when asked; None if no PNG."""
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    size = struct.unpack(">II", head[16:24])
    return (*size, head[25]) if colour_type else size


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
        return ok

    def run(args, log, only=None):
        log = os.path.join(os.path.dirname(root), log)
        t0 = time.time()
        run_env = dict(env, SS_TRACKER_ONLY=only) if only else env
        with open(log, "w") as f:
            rc, timed_out, at_cap = run_capped([a.binary, "--render-tracker-maps"] + args, a.binary, a.timeout,
                                               stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, env=run_env)
        print(f"     {' '.join(args) or '(default folder)'}: {time.time() - t0:.1f} s")
        text = open(log, errors="replace").read()
        at_cap = at_cap or (not timed_out and hit_cap(rc, text, killed=timed_out))
        check(rc == 0 and not timed_out and not at_cap, f"{' '.join(args) or '(default folder)'}: exit 0 "
              f"(rc {rc}, timed out {timed_out}, memory cap {at_cap})")

    run([], "e2e_default.log")
    run([out2], "e2e_dir.log")
    out3, out4, custom = os.path.join(root, "out3"), os.path.join(root, "out4"), os.path.join(root, "custom", "my_ut.zip")
    run([out3, "--no-ut-zip"], "e2e_no_zip.log", only="rooms/18.")
    run([out4, "--ut-zip", custom], "e2e_ut_zip.log", only="rooms/18.")
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

    # The Universal Tracker zip: beside each folder (the default's parent; DIR's parent), or --ut-zip's.
    boards = json.load(open(os.path.join(REPO, "tools", "poptracker", "boards.json"), encoding="utf-8"))
    extras = {b["path"]: (b["width"], b["height"], 2) for b in boards["boards"]}
    extras[boards["player_icon"]["path"]] = (boards["player_icon"]["width"], boards["player_icon"]["height"], 6)
    zips = {"default folder": (os.path.join(os.path.dirname(default), UT_ZIP), default, set(expected)),
            "DIR": (os.path.join(root, UT_ZIP), out2, set(expected)),
            "--ut-zip FILE": (custom, out4, {"images/rooms/18.png"})}
    for name, (zpath, folder, images) in zips.items():
        if not check(os.path.isfile(zpath), f"{name}: the Universal Tracker zip at {os.path.relpath(zpath, root)}"):
            continue
        with zipfile.ZipFile(zpath) as z:
            infos = z.infolist()
            names = [i.filename for i in infos]
            check(z.testzip() is None, f"{name}: Python's zipfile reads the zip, every CRC checks ({len(names)} files)")
            check(all(i.compress_type == zipfile.ZIP_STORED and i.date_time == (1980, 1, 1, 0, 0, 0) and
                      not i.is_dir() for i in infos), f"{name}: every entry stored, dated 1980-01-01, no folders")
            check(names == sorted(set(names)), f"{name}: entries sorted, each once")
            check(set(names) == images | set(extras), f"{name}: the entries are the layout's {len(images)} image(s) "
                  f"and the {len(extras)} extras ({sorted(set(names) ^ (images | set(extras)))[:4]})")
            differ = [n for n in images if n in names and z.read(n) != open(os.path.join(folder, n), "rb").read()]
            check(not differ, f"{name}: the zip's map entries are the rendered PNGs byte for byte "
                  f"({len(images) - len(differ)}/{len(images)})")
            bad = [n for n, want in extras.items() if n in names and png_head(z.read(n)[:33], True) != want]
            check(not bad, f"{name}: the boards at boards.json's sizes (RGB), the marker 64x64 RGBA ({bad})")
    same_zip = all(os.path.isfile(zips[k][0]) for k in ("default folder", "DIR")) and \
        open(zips["default folder"][0], "rb").read() == open(zips["DIR"][0], "rb").read()
    check(same_zip, "the two full runs' zips are the same bytes")
    check(set(os.listdir(out3)) == {"images"} and not os.path.exists(os.path.join(root, UT_ZIP + ".part")),
          "--no-ut-zip: the image only")

    after = files_under(root)
    new = {k: v for k, v in after.items() if k not in before}
    allowed = {os.path.relpath(os.path.join(default, r), root) for r in expected} | \
              {os.path.relpath(os.path.join(out2, r), root) for r in expected} | \
              {os.path.relpath(os.path.join(o, "images", "rooms", "18.png"), root) for o in (out3, out4)} | \
              {os.path.relpath(z, root) for z, _, _ in zips.values()}
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
    if os.path.isfile(zips["DIR"][0]):
        print(f"the Universal Tracker zip: {len(expected) + len(extras)} files, "
              f"{os.path.getsize(zips['DIR'][0]):,} bytes")
    if not a.keep:
        shutil.rmtree(root, ignore_errors=True)
    if failures:
        print(f"{len(failures)} check(s) failed")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
