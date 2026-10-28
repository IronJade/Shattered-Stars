#!/usr/bin/env python3
"""Universal Tracker's map tab, headless (docs/apworld/apworld.md "Universal Tracker's map tab"): UT's
own map loader (TrackerClient's TrackerGameContext.load_pack / load_map /
update_location_icon_coords) run in-process against this world's tracker_world, with no window.

  SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \\
      trackers/tools/ap_poptracker_utmap.py --tracker scratchpad/ap_ut/tracker.apworld \\
      [--zip dist/poptracker/so2_shattered_stars_1.0.0.zip] [--apworld so2.apworld] [--ut-dir DIR]

--zip is either picture zip UT's dialog names: the PopTracker pack's, or the port's own
so2_shattered_stars_ut.zip (`shattered-stars --render-tracker-maps`, docs/trackers/tracker_maps.md "The
Universal Tracker zip"). UT reads a picture as `ap:zip:<zip>/<path>` and splits that at the first
"zip/", so no folder on the zip's path may end in "zip" (scratchpad/utzip/... can't hold it).

1. A UT copy of the scratch Archipelago (trackers/tools/ap_ut_check.py's setup: a symlink farm with real
   copies of host.yaml and the top-level scripts, tracker.apworld in custom_worlds, the repo's
   apworld/so2 linked as worlds/so2 or --apworld in custom_worlds), with host.yaml's
   `so2_options: ut_pack_path` set to the pack zip (so UT never asks with a file dialog), run with
   DISPLAY / WAYLAND_DISPLAY / DBUS_SESSION_BUS_ADDRESS unset and XDG_RUNTIME_DIR at scratch.
2. Inside it: an open-world room generated in-process; UT's TrackerCore rebuilds the slot from its
   slot data (yaml-less); a TrackerGameContext (no connection) with a stand-in for the Kivy UI loads
   the pack: the JSON from the APWorld (hybrid), the images from the zip.
3. Checks: the map pages and UT's map menu (every room under its area); so2_map_{team}_{slot} values
   as the port writes them open the room's page (map_page_index), the world map's with the player
   marker at the x / z (location_icon_coords), and keep the page for a map without one; no value
   (load_pack's load_map, before UT has read the key) opens Expel's world map; every page's
   image is in the zip and every square's locations are the room's own AP locations (UT's own
   matching of section names: no name mapping); the rooms' squares are the layout's room dots; every
   page's picture and the marker load through UT's own image loader (worlds/tracker/TrackerKivy.py's
   `ap:zip:` load, decoded by Kivy as UT's map tab does) at the layout's / the boards' sizes.
Exit status 0 when every check passes.
"""
import argparse
import json
import os
import subprocess
import sys
import types
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))  # trackers/tools/ -> the repo
GAME = "Star Ocean: The Second Story"
YAML = """name: Mapper
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  world_mode: open_world
  boss_checks: true
"""
failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def outside(a):
    sys.path.insert(0, HERE)
    import ap_ut_check
    setup_args = types.SimpleNamespace(ut_dir=a.ut_dir, ap_dir=a.ap_dir, tracker=a.tracker, apworld=a.apworld)
    ap = ap_ut_check.setup(setup_args)
    host = os.path.join(ap, "host.yaml")
    text = open(host, encoding="utf-8").read()
    zip_path = os.path.abspath(a.zip)
    block = f'so2_options:\n  ut_pack_path: "{zip_path}"\n'
    import re
    if re.search(r"(?m)^so2_options:\n", text):
        text = re.sub(r'(?m)^so2_options:\n(?:[ \t]+.*\n)*', block, text)
    else:
        text += block
    with open(host, "w", encoding="utf-8") as f:
        f.write(text)
    env = ap_ut_check.env_for(os.path.abspath(a.ut_dir), ap)
    cmd = [sys.executable, os.path.abspath(__file__), "--inside", "--zip", zip_path, "--ut-dir", os.path.abspath(a.ut_dir)]
    return subprocess.run(cmd, cwd=ap, env=env, stdin=subprocess.DEVNULL).returncode


class FakeUI:
    """What TrackerGameContext.load_map writes to: the page's image, square sizes, its menu name."""
    source = None
    current_map = None
    loc_size = loc_icon_size = loc_border = None
    auto_tab = True


def inside(a):
    sys.path.insert(0, os.getcwd())
    sys.path.insert(0, HERE)
    import logging
    import warnings
    warnings.simplefilter("ignore")
    logging.disable(logging.WARNING)
    import ap_ut_check
    from worlds.AutoWorld import AutoWorldRegister
    from worlds.tracker import UTMapTabData
    from worlds.tracker.TrackerClient import TrackerGameContext
    out = os.path.join(a.ut_dir, "maproom")
    os.makedirs(os.path.join(out, "players"), exist_ok=True)
    with open(os.path.join(out, "players", "Mapper.yaml"), "w") as f:
        f.write(YAML)
    mw = ap_ut_check.generate(os.path.join(out, "players"), out, 4321)
    player = next(p for p in mw.player_ids if mw.game[p] == GAME)
    world_cls = AutoWorldRegister.world_types[GAME]
    world = mw.worlds[player]
    slot_data = json.loads(json.dumps(world.fill_slot_data()))     # as the server hands it over
    check(hasattr(world_cls, "tracker_world"), f"{GAME} defines tracker_world ({world_cls.__module__})")

    import asyncio

    async def make():       # CommonContext wants a running loop (no_connection=True breaks: no checksums)
        return TrackerGameContext(None, None)
    loop = asyncio.new_event_loop()
    ctx = loop.run_until_complete(make())
    ctx.game, ctx.slot, ctx.team = GAME, player, 0
    ctx.stored_data = {}
    ctx.ui = FakeUI()
    ctx.server_locations = {loc.address for loc in mw.get_locations(player) if loc.address is not None}
    core = ctx.tracker_core
    core.set_slot_params(GAME, player, mw.player_name[player], 0)
    core.initalize_tracker_core(world_cls, slot_data)
    check(not core.tracker_disabled and core.get_current_world() is not None, "UT rebuilt the slot yaml-less")
    captured = {}

    def coords_func(coords, dcoords, dlcoords, use_split, size):
        captured.clear()
        captured.update(coords=coords, entrances=dcoords, events=dlcoords, size=size)
        return {}, {}, {}
    ctx.map_page_coords_func = coords_func
    ctx.tracker_world = UTMapTabData(ctx.slot, ctx.team, **world_cls.tracker_world)
    key = f"so2_map_0_{player}"
    check(ctx.tracker_world.map_page_setting_key == key and ctx.tracker_world.location_setting_key == key,
          f"the map key is the port's: {key}")
    ctx.load_pack()
    if not check(ctx.tracker_world is not None and ctx.maps, f"UT loaded the map tab (hybrid: JSON from the "
                 f"APWorld, images from {os.path.basename(a.zip)}): {len(ctx.maps)} maps, {len(ctx.locs)} location trees"):
        return 1
    layout = json.load(open(os.path.join(REPO, "docs", "trackers", "tracker_maps.json"), encoding="utf-8"))
    rooms = {r["map"]: r for r in layout["rooms"]}
    page_of = {m["name"]: k for k, m in enumerate(ctx.maps)}
    # load_pack's load_map runs before UT has read the key: no value must lay a page out, or UT 0.3.6
    # drops the connection when the key's Retrieved moves the marker (ut_map.map_page_index; the real
    # connect sequence, with UT's own map tab widget: trackers/tools/ap_ut_connect.py)
    check(ctx.map_id == page_of.get("world_expel") and (ctx.root_pack_path or "").startswith("ap:zip:"),
          f"load_pack lays out Expel's world map before the key is read ({ctx.map_id}, {ctx.root_pack_path})")
    check(all(f"room_{m}" in page_of for m in rooms) and len(ctx.maps) == len(rooms) + len(layout["images"]) + 5,
          f"the pages: {len(rooms)} rooms, {len(layout['images'])} area and world maps, 5 boards")
    named = sum(1 for m in rooms if ctx.map_to_name.get(f"room_{m}", "").endswith("/" + rooms[m]["label"]))
    check(named == len(rooms), f"UT's map menu names every room '<map> <name>' under its area ({named})")
    z = zipfile.ZipFile(a.zip)
    names = set(z.namelist())
    lid = world_cls.location_name_to_id

    def open_key(value):
        ctx.stored_data[key] = value
        ctx.load_map(None)
        return ctx.map_id
    # rooms: the port's key opens the room's page, its squares are the layout's room dots
    by_room = {}
    for name, c in layout["checks"].items():
        if lid[name] in ctx.server_locations:
            by_room.setdefault(c["map"], {}).setdefault((c["room"]["x"], c["room"]["y"]), set()).add(lid[name])
    wrong = []
    for m in sorted(by_room):
        page = open_key({"map": m})
        got = {xy: set(ids) for xy, (ids, _) in captured["coords"].items()}
        if page != page_of[f"room_{m}"] or ctx.ui.source != f"ap:zip:{os.path.abspath(a.zip)}/{rooms[m]['path']}" \
                or got != by_room[m]:
            wrong.append((m, page, sorted(got.items())[:2], sorted(by_room[m].items())[:2]))
    check(not wrong, f"so2_map {{map: M}} opens room M's page for the {len(by_room)} rooms with checks, its squares "
          f"the layout's room dots of the slot's checks (e.g. 62: {open_key({'map': 62})}, '{ctx.ui.current_map}')"
          + (f": {wrong[:2]}" if wrong else ""))
    # every page: its image is in the zip, every square's locations are in the slot and named on it
    bad = []
    for k, mp in enumerate(ctx.maps):
        ctx.load_map(k)
        if mp["img"] not in names:
            bad.append((mp["name"], "image"))
        for xy, (ids, _) in captured["coords"].items():
            if not ids or not set(ids) <= ctx.server_locations:
                bad.append((mp["name"], xy))
    check(not bad, f"all {len(ctx.maps)} pages load, images in the zip, squares the slot's locations" + (f": {bad[:3]}" if bad else ""))
    # every page's picture, and the marker, through UT's own image loader (TrackerKivy.load: the
    # ap:zip: path split and zipfile read; Kivy's decoder from kvui, as the map tab's AsyncImage)
    import contextlib
    import io
    import worlds.tracker.TrackerKivy as ut_kivy
    from kvui import ImageLoaderPkgutil

    class Loader:
        _bytes_to_data = staticmethod(ImageLoaderPkgutil._bytes_to_data)
    sizes = {i["path"]: (i["width"], i["height"]) for i in layout["images"] + layout["rooms"]}
    boards = json.load(open(os.path.join(REPO, "trackers", "poptracker", "boards.json"), encoding="utf-8"))
    sizes.update({b["path"]: (b["width"], b["height"]) for b in boards["boards"]})
    icon = pages_icon = boards["player_icon"]
    sizes[icon["path"]] = (icon["width"], icon["height"])

    def ut_load(source):
        try:
            with contextlib.redirect_stdout(io.StringIO()):     # TrackerKivy.load prints each name
                data = ut_kivy.load(Loader(), source)
            return data[0].width, data[0].height
        except Exception as e:      # noqa: BLE001 (what UT's map tab would fail on)
            return repr(e)[:80]
    unloaded = []
    for k, mp in enumerate(ctx.maps):
        ctx.load_map(k)
        got = ut_load(ctx.ui.source)
        if got != sizes.get(mp["img"]):
            unloaded.append((mp["img"], got))
    got = ut_load(f"{ctx.root_pack_path}/{pages_icon['path']}")
    if got != sizes[icon["path"]]:
        unloaded.append((icon["path"], got))
    check(not unloaded, f"UT's own image loader opens all {len(ctx.maps)} pages' pictures and the marker from the "
          f"zip at their sizes" + (f": {unloaded[:3]}" if unloaded else ""))
    check(not any(captured.get("entrances") or ()) and not any(captured.get("events") or ()),
          "no door squares in UT (doors are no AP locations, entrances or events)")
    # a world map: its page, and the player marker at x / z
    world_img = next(i for i in layout["images"] if i["kind"] == "world" and i["world_map"] == 1)
    x, zz = 51200, 18500
    page = open_key({"map": 1, "x": x, "z": zz})
    ctx.update_location_icon_coords()
    want = ((x - world_img["x0"]) // world_img["units_per_pixel"], (world_img["z0"] - zz) // world_img["units_per_pixel"],
            "images/status/you.png")
    check(page == page_of["world_expel"] and ctx.location_icons == [want] and want[2] in names,
          f"so2_map {{map: 1, x, z}} -> the Expel world map, the player marker at {want[:2]} ({ctx.location_icons})")
    check(len(captured["coords"]) > 10, f"the world map's area squares ({len(captured['coords'])})")
    # a field map: the marker hides; a map with no page keeps the page
    page = open_key({"map": 62})
    ctx.update_location_icon_coords()
    check(ctx.location_icons == [] and page == page_of["room_62"], "a room: no marker (the key has no position)")
    before = ctx.map_id
    open_key({"map": 7})
    check(ctx.map_id == before, "a map with no page (the opening, map 7) keeps the page")
    open_key(None)
    check(ctx.map_id == page_of["world_expel"], "no value opens Expel's world map (the page UT lays out at connect)")
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s)")
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--tracker", default=os.path.join(REPO, "scratchpad", "ap_ut", "tracker.apworld"))
    p.add_argument("--zip", default=os.path.join(REPO, "dist", "poptracker", "so2_shattered_stars_1.0.0.zip"))
    p.add_argument("--apworld", help="a built so2.apworld (default: the repo's apworld/so2)")
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--ut-dir", default=os.path.join(REPO, "scratchpad", "maptab", "ap_ut"))
    p.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    a = p.parse_args()
    return inside(a) if a.inside else outside(a)


if __name__ == "__main__":
    sys.exit(main())
