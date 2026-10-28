#!/usr/bin/env python3
"""Universal Tracker against a real room, scripted (docs/apworld/apworld.md "Trackers"; the release
workflow, docs/release/release_checklist.md). Not a unit test: it runs UT's own client.

  trackers/tools/ap_ut_check.py --tracker tracker.apworld [--apworld so2.apworld] [--players DIR] [--seed N]
                       [--port N] [--spheres N] [--ut-dir DIR] [--ap-dir DIR]

1. Sets up an Archipelago copy for UT (--ut-dir, default scratchpad/ap_ut): a symlink farm of the
   scratch Archipelago 0.6.8 (--ap-dir, default scratchpad/ap/Archipelago, made by
   apworld/tools/ap_test_server.sh) with its own host.yaml and scripts, tracker.apworld (UT, e.g. Tracker_v0.3.6's
   release asset) in custom_worlds, and our world: the repo's apworld/so2 linked as worlds/so2, or a
   built so2.apworld (--apworld) in custom_worlds. The shared install is never changed: another
   process may be using it, and UT adds its settings to host.yaml.
2. Generates a room in-process (Generate + Main, as Generate.py does) from --players (default: four
   Star Ocean players: story mode with a random story party, open world without the bench (a random
   pool of 8) and starting_character any, hub mode with a random start town and an excluded
   location, and a player's own rich hub YAML) and hosts it on 127.0.0.1:PORT with MultiServer
   (--disable_save, as apworld/tools/ap_test_server.sh does; its own process group, stopped at the end).
3. UT as a library first (TrackerCore, the way its fuzzer hook drives it): each slot's room rebuilt
   yaml-less with the same party, and a room with logic_version 2 refused (UT's TrackerException:
   tracker_disabled, our message in gen_error).
4. At each point (fresh, then after each of the room's first --spheres spheres is checked), for each
   Star Ocean slot: runs UT headless (`Launcher.py "Universal Tracker" -- --nogui --count --list
   --connect localhost:PORT --name SLOT`) and compares its in-logic locations (the count and the
   listed names) with the world's own: the generated multiworld's locations of that slot reachable
   with what the server has given it, and still unchecked, worked out in-process the way UT's
   updateTracker does. Checks are sent as the port sends them: a client connected as the slot
   (items_handling 0b001) sends LocationChecks. Then the same through TrackerCore.updateTracker
   in-process, whose tab lines (after UT's own sort_log_lines) must be in custom_ut_sort's order:
   the --list lines come before UT sorts them.
5. Story awareness (docs/apworld/apworld.md "Story awareness"; skipped with --no-story), on the fresh room
   (between the fresh point and the first sphere's): for the story-mode slot, a client connected as
   the slot Sets `so2_story_{team}_{slot}` as the port does (replace, want_reply false) at several
   story positions, a step-only change among them; at each, UT headless
   (which watches the key: found_entrances_datastorage_key, reconnect_found_entrances) must list
   exactly what the reference evaluator (trackers/tools/ap_tracker_logic.py) gives for the room's tracker
   export with that position, still unchecked, and so must TrackerCore in-process after
   reconnect_found_entrances; then once with host.yaml's enforce_deferred_entrances "off", where UT
   lists the world's own again. Then the key goes back to null (no position) for the spheres' points.
Exit status 0 when every point matches. XDG_CACHE_HOME / XDG_DATA_HOME point under --ut-dir, so the
clients' data package cache stays there; so does XDG_RUNTIME_DIR (with DISPLAY, WAYLAND_DISPLAY and
DBUS_SESSION_BUS_ADDRESS unset: no window, no dialog, no session bus).

Run with the scratch install's Python (any directory):
  SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python trackers/tools/ap_ut_check.py \\
      --tracker scratchpad/ap_ut/tracker.apworld
"""
import argparse
import glob
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # trackers/tools/ -> the repo
GAME = "Star Ocean: The Second Story"
PLAYERS = {
    "UTStory": {"world_mode": "story", "character_items": "true", "craftsanity": "true",
                "killer_move_checks": "true", "killer_move_progression": "any", "skillsanity": "insanity"},
    "UTOpen": {"world_mode": "open_world", "character_items": "true", "bench": "false",
               "starting_character": "any", "exclude_characters": ["Leon"], "shuffle_shops": "specials",
               "enemy_dropsanity": "first_drop", "killer_move_checks": "true", "killer_move_progression": "any",
               "craftsanity": "true", "goal": "indalecio_limiter_off"},
    "UTHub": {"world_mode": "hub", "hub_start": "random_town", "fienal_requirement": "all_bosses",
              "killer_move_checks": "true", "killer_move_progression": "any", "skillsanity": "skills_only",
              "exclude_locations": ["Town of Salva: Heavy Ring chest"]},
    # A player's own hub YAML (2026-10-27; written for 0.2.0, so no `bench`: the default, on).
    "UTRich": {"world_mode": "hub", "hub_start": "arlia", "fienal_requirement": "all_bosses",
               "goal": "indalecio_limiter_off_and_iseria_queen", "story_route": "claude", "shuffle_shops": "full",
               "cave_of_trials": "true", "enemy_dropsanity": "first_drop", "craftsanity": "true",
               "killer_move_checks": "true", "killer_move_progression": "any", "skillsanity": "insanity",
               "starting_character": "opera", "character_items": "true", "traps": 50, "difficulty": "universe",
               "death_link": "true"},
}


def parse(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--tracker", required=True, help="tracker.apworld (Universal Tracker's release asset)")
    p.add_argument("--apworld", help="a built so2.apworld (default: the repo's apworld/so2)")
    p.add_argument("--players", help="a directory of player YAMLs (default: the three built in)")
    p.add_argument("--seed", type=int, default=2026)
    p.add_argument("--port", type=int, default=38296)
    p.add_argument("--spheres", type=int, default=4, help="check the room's first N spheres, one point each")
    p.add_argument("--ut-dir", default=os.path.join(REPO, "scratchpad", "ap_ut"))
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--no-story", action="store_true", help="skip the story-awareness points")
    p.add_argument("--run", action="store_true", help=argparse.SUPPRESS)    # phase 2, inside the UT copy
    return p.parse_args(argv)


# ---- Phase 1: the Archipelago copy for UT (no Archipelago import) --------------------------------
def setup(a):
    ut_dir = os.path.abspath(a.ut_dir)
    ap = os.path.join(ut_dir, "Archipelago")
    if not os.path.isdir(ap):
        os.makedirs(ut_dir, exist_ok=True)
        subprocess.run(["cp", "-as", os.path.abspath(a.ap_dir), ap], check=True)
    # Real copies, not links: host.yaml (UT adds its settings, AP may rewrite it) and the top-level
    # scripts (Python puts a script's resolved directory first on sys.path: a linked Launcher.py or
    # MultiServer.py would import the shared install's modules and worlds, not this copy's).
    for name in ["host.yaml"] + [f for f in os.listdir(ap) if f.endswith(".py")]:
        path = os.path.join(ap, name)
        if os.path.islink(path):
            target = os.path.realpath(path)
            os.unlink(path)
            shutil.copyfile(target, path)
    custom = os.path.join(ap, "custom_worlds")
    os.makedirs(custom, exist_ok=True)
    for old in glob.glob(os.path.join(custom, "*.apworld")):
        os.unlink(old)
    shutil.copyfile(a.tracker, os.path.join(custom, "tracker.apworld"))
    link = os.path.join(ap, "worlds", "so2")
    if os.path.islink(link):
        os.unlink(link)
    elif os.path.isdir(link):
        shutil.rmtree(link)
    if a.apworld:
        shutil.copyfile(a.apworld, os.path.join(custom, "so2.apworld"))
    else:
        os.symlink(os.path.join(REPO, "apworld", "so2"), link)
    # UT's own Players folder: an empty one in this copy. Without it UT asks for one with a desktop
    # folder picker (kdialog / zenity), and a path picked there sticks in host.yaml.
    players = os.path.join(ap, "Players")
    os.makedirs(players, exist_ok=True)
    host = os.path.join(ap, "host.yaml")
    with open(host, encoding="utf-8") as f:
        text = f.read()
    text = re.sub(r'(?m)^(universal_tracker:\n(?:[ \t]+#.*\n)*[ \t]+player_files_path:).*$',
                  lambda m: m.group(1) + ' "' + players + '"', text)
    with open(host, "w", encoding="utf-8") as f:
        f.write(text)
    return ap


def env_for(ut_dir, ap):
    # XDG_CONFIG_HOME too: kvui puts Kivy's home (KIVY_HOME, its config.ini) in platformdirs' config
    # dir, the user's ~/.config/Archipelago otherwise.
    env = dict(os.environ, SKIP_REQUIREMENTS_UPDATE="1", PYTHONPATH=ap,
               XDG_CACHE_HOME=os.path.join(ut_dir, "cache"), XDG_DATA_HOME=os.path.join(ut_dir, "data"),
               XDG_CONFIG_HOME=os.path.join(ut_dir, "config"))
    env.pop("PYTHONHOME", None)
    # Headless: no display and no session bus, so nothing UT or AP does can open a dialog or window
    # on the desktop (a missing folder makes them ask with one); it fails instead. XDG_RUNTIME_DIR
    # too: libdbus falls back to $XDG_RUNTIME_DIR/bus without DBUS_SESSION_BUS_ADDRESS.
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    run = os.path.join(ut_dir, "run")
    os.makedirs(run, exist_ok=True)
    os.chmod(run, 0o700)
    env["XDG_RUNTIME_DIR"] = run
    return env


# ---- Phase 2: inside the UT copy ------------------------------------------------------------------
def write_players(out):
    import yaml
    d = os.path.join(out, "players")
    os.makedirs(d, exist_ok=True)
    for name, options in PLAYERS.items():
        with open(os.path.join(d, f"{name}.yaml"), "w", encoding="utf-8") as f:
            yaml.safe_dump({"name": name, "game": GAME, GAME: options}, f, sort_keys=False)
    return d


def generate(players, out, seed):
    import Generate
    import Main
    args = Generate.mystery_argparse(["--player_files_path", players, "--outputpath", out, "--seed", str(seed),
                                      "--spoiler", "1"])
    erargs, seed = Generate.main(args)
    return Main.main(erargs, seed)


def wait_port(port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return True
        time.sleep(0.2)
    return False


def send_checks(port, slot, game, ids):
    """Connect as the slot (as the port does: items_handling 0b001) and send LocationChecks; wait for
    the server's RoomUpdate."""
    from websockets.sync.client import connect
    with connect(f"ws://localhost:{port}", max_size=None, open_timeout=30) as ws:
        json.loads(ws.recv(timeout=30))     # RoomInfo
        ws.send(json.dumps([{"cmd": "Connect", "password": "", "game": game, "name": slot, "uuid": "ap-ut-check",
                             "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                             "items_handling": 0b001, "tags": [], "slot_data": False}]))
        deadline = time.time() + 30
        while time.time() < deadline:
            if any(m.get("cmd") == "Connected" for m in json.loads(ws.recv(timeout=30))):
                break
        else:
            raise RuntimeError(f"{slot}: not connected")
        if not ids:
            return
        ws.send(json.dumps([{"cmd": "LocationChecks", "locations": sorted(ids)}]))
        while time.time() < deadline:
            msgs = json.loads(ws.recv(timeout=30))
            if any(m.get("cmd") == "RoomUpdate" and "checked_locations" in m for m in msgs):
                return
        raise RuntimeError(f"{slot}: no RoomUpdate after LocationChecks")


def set_story(port, slot, game, value):
    """Connect as the slot as the port does (items_handling 0b001) and Set its story key the port's
    way (docs/apworld/data_storage.md: one replace, the value also the default, want_reply false). Returns
    the key."""
    from websockets.sync.client import connect
    with connect(f"ws://localhost:{port}", max_size=None, open_timeout=30) as ws:
        json.loads(ws.recv(timeout=30))     # RoomInfo
        ws.send(json.dumps([{"cmd": "Connect", "password": "", "game": game, "name": slot, "uuid": "ap-ut-story",
                             "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                             "items_handling": 0b001, "tags": [], "slot_data": False}]))
        deadline = time.time() + 30
        connected = None
        while time.time() < deadline and connected is None:
            connected = next((m for m in json.loads(ws.recv(timeout=30)) if m.get("cmd") == "Connected"), None)
        if connected is None:
            raise RuntimeError(f"{slot}: not connected")
        key = f"so2_story_{connected['team']}_{connected['slot']}"
        ws.send(json.dumps([{"cmd": "Set", "key": key, "default": value, "want_reply": False,
                             "operations": [{"operation": "replace", "value": value}]}]))
        # read it back, so the write has landed before UT connects
        ws.send(json.dumps([{"cmd": "Get", "keys": [key]}]))
        while time.time() < deadline:
            for m in json.loads(ws.recv(timeout=30)):
                if m.get("cmd") == "Retrieved" and m.get("keys", {}).get(key) == value:
                    return key
        raise RuntimeError(f"{slot}: {key} not stored")


def set_enforce(ap, value):
    """host.yaml's universal_tracker.enforce_deferred_entrances in this UT copy."""
    host = os.path.join(ap, "host.yaml")
    with open(host, encoding="utf-8") as f:
        text = f.read()
    text, n = re.subn(r'(?m)^([ \t]+enforce_deferred_entrances:).*$', lambda m: m.group(1) + f' "{value}"', text)
    if n != 1:
        raise RuntimeError("host.yaml: no enforce_deferred_entrances line")
    with open(host, "w", encoding="utf-8") as f:
        f.write(text)


def run_ut(ap, env, port, slot, log):
    """UT headless: its in-logic count and listed lines."""
    r = subprocess.run([sys.executable, "Launcher.py", "Universal Tracker", "--", "--nogui", "--count", "--list",
                        "--connect", f"localhost:{port}", "--name", slot],
                       cwd=ap, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=300)
    text = r.stdout + r.stderr
    with open(log, "w", encoding="utf-8") as f:
        f.write(text)
    count, lines = None, []
    for line in text.splitlines():
        if "In logic locations :" in line:
            count = int(line.rsplit(":", 1)[1])
        else:
            lines.append(line.strip())
    return r.returncode, count, lines


def own_in_logic(mw, player, checked):
    """The world's own (UT's updateTracker on the generated multiworld): the slot's locations
    reachable with its precollected items and the items found at every checked location (any
    slot's), events swept, less the checked ones."""
    from BaseClasses import CollectionState
    state = CollectionState(mw)
    for loc in checked:
        if loc.item.player == player:
            state.collect(loc.item, True, loc)
    state.sweep_for_advancements(locations=[loc for loc in mw.get_locations(player) if loc.address is None])
    return {loc.name for loc in mw.get_reachable_locations(state, player)
            if loc.address is not None and loc not in checked}


def ut_core(mw, player, data):
    """UT as a library (its fuzzer hook's way): TrackerCore.initalize_tracker_core with the slot's
    data, yaml-less. Returns the core (tracker_disabled / gen_error when the world refused)."""
    import logging
    from worlds.tracker.TrackerCore import TrackerCore
    core = TrackerCore(logging.getLogger("ap_ut_check"), False, False)
    core.set_slot_params(GAME, player, mw.player_name[player], 0)
    core.initalize_tracker_core(type(mw.worlds[player]), data)
    return core


def ut_core_update(core, mw, player, checked):
    """updateTracker with the server's view (missing locations, received items as NetworkItems):
    the in-logic names, and the tab's lines per group after UT's own sort_log_lines."""
    from NetUtils import NetworkItem
    missing = {loc.address for loc in mw.get_locations(player) if loc.address is not None and loc not in checked}
    received = [NetworkItem(i.code, -2, player, int(i.classification)) for i in mw.precollected_items[player]
                if i.code is not None]
    received += [NetworkItem(loc.item.code, loc.address, loc.player, int(loc.item.classification))
                 for loc in checked if loc.item.player == player and loc.item.code is not None]
    core.set_missing_locations(missing)
    core.set_items_received(received)
    ret = core.updateTracker()
    groups = [[line.location_label for line in core.log_lines[g]] for g in sorted(core.log_lines)]
    return set(ret.in_logic_locations), groups


def run(a):
    ut_dir = os.path.abspath(a.ut_dir)
    ap = os.getcwd()
    env = env_for(ut_dir, ap)
    out = os.path.join(ut_dir, "room")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    players = a.players or write_players(out)
    mw = generate(players, out, a.seed)
    world = mw.worlds[next(p for p in mw.player_ids if mw.game[p] == GAME)]
    from worlds.so2 import ut as so2_ut

    def in_order(names):
        keys = [so2_ut.sort_key("", n) for n in names]
        return keys == sorted(keys)
    zips = glob.glob(os.path.join(out, "AP_*.zip"))
    print(f"room: seed {a.seed}, {len(mw.player_ids)} slots, so2.apworld {world.slot_options()['apworld_version']} "
          f"from {world.__module__} ({os.path.dirname(sys.modules[world.__module__].__file__)})", flush=True)
    server_log = open(os.path.join(out, "server.log"), "w")
    server = subprocess.Popen([sys.executable, "MultiServer.py", zips[0], "--host", "127.0.0.1", "--port", str(a.port),
                               "--disable_save", "--loglevel", "info", "--release_mode", "disabled",
                               "--collect_mode", "disabled"], cwd=ap, env=env, stdin=subprocess.DEVNULL,
                              stdout=server_log, stderr=subprocess.STDOUT, start_new_session=True)
    ok = True
    try:
        if not wait_port(a.port, 60):
            raise RuntimeError(f"the server did not listen on {a.port}: {out}/server.log")
        slots = {p: mw.player_name[p] for p in mw.player_ids if mw.game[p] == GAME}
        rooms = {p: json.loads(json.dumps(mw.worlds[p].fill_slot_data())) for p in slots}
        cores = {p: ut_core(mw, p, rooms[p]) for p in slots}
        for p, core in cores.items():
            same = core.multiworld is not None and core.multiworld.worlds[1].party == mw.worlds[p].party
            ok &= same
            print(f"TrackerCore     {slots[p]:8s} rebuilt yaml-less: {'the same party' if same else 'FAILED'}", flush=True)
        # A room this world can't reproduce (a newer logic_version): UT must refuse it with our message.
        p = next(iter(slots))
        refused = ut_core(mw, p, dict(rooms[p], logic_version=2, apworld_version="9.9.9"))
        good = refused.tracker_disabled and "needs so2.apworld 9.9.9 or later" in (refused.gen_error or "")
        ok &= good
        print(f"refusal         {slots[p]:8s} logic_version 2: {'TrackerException, ' if good else 'NOT REFUSED: '}"
              f"{refused.gen_error!r}", flush=True)
        spheres = [s for s in mw.get_sendable_spheres()]
        checked = set()
        points = [("fresh", set())] + [(f"after sphere {k + 1}", spheres[k])
                                       for k in range(min(a.spheres, len(spheres)))]
        for label, sphere in points:
            for p in mw.player_ids:     # every slot's, other games' too (their items may be ours)
                send_checks(a.port, mw.player_name[p], mw.game[p], {loc.address for loc in sphere if loc.player == p})
            checked |= set(sphere)
            for p, slot in slots.items():
                log = os.path.join(out, f"ut_{slot}_{label.replace(' ', '_')}.log")
                rc, count, lines = run_ut(ap, env, a.port, slot, log)
                mine = own_in_logic(mw, p, checked)
                names = set(world.location_name_to_id)
                listed = [line for line in lines if line in names]
                # (The --list lines come before UT sorts them, TrackerCore.updateTracker: the order is
                # checked on the tab's lines below.)
                match = rc == 0 and count == len(mine) and set(listed) == mine
                ok &= match
                print(f"{label:15s} {slot:8s} UT in logic {count} (listed {len(listed)}), the world's own {len(mine)}: "
                      f"{'match' if match else 'MISMATCH'}{'' if rc == 0 else f'; UT exit {rc}'}", flush=True)
                if not match:
                    print(f"  only UT: {sorted(set(listed) - mine)[:10]}  only ours: {sorted(mine - set(listed))[:10]}")
                # The same through UT's TrackerCore in-process: its tab's lines after its own sort.
                core = cores[p]
                names_in, groups = ut_core_update(core, mw, p, checked)
                lib_ok = names_in == mine and all(in_order([n for n in g if n in names]) for g in groups)
                ok &= lib_ok
                print(f"{'':15s} {'':8s} TrackerCore in logic {len(names_in)}: "
                      f"{'match, tab in custom_ut_sort order' if lib_ok else 'MISMATCH or order'}", flush=True)
            if label == "fresh" and not a.no_story:
                # the story points on the fresh room (the most the story holds back), then the key
                # back to null, which UT reads as no position: the spheres' points as before
                ok &= story_points(a, ap, env, mw, slots, rooms, cores, checked)
    finally:
        set_enforce(ap, "default")
        os.killpg(server.pid, signal.SIGTERM)
        try:
            server.wait(timeout=20)
        except subprocess.TimeoutExpired:
            os.killpg(server.pid, signal.SIGKILL)
        server_log.close()
    print("UT MATCHES THE WORLD" if ok else "UT DIFFERS")
    return 0 if ok else 1


STORY_POSITIONS = [(3, 0), (9, 0), (12, 0), (20, 0), (36, 0), (41, 0), (58, 0), (60, 3000), (60, 4000)]


def story_points(a, ap, env, mw, slots, rooms, cores, checked):
    """Step 5: the story-mode slot at STORY_POSITIONS, UT headless and TrackerCore in-process against the
    reference evaluator on the room's export; then enforce_deferred_entrances "off"."""
    sys.path.insert(0, os.path.join(REPO, "trackers", "tools"))
    from ap_tracker_logic import TrackerLogic
    ok = True
    story = [p for p in slots if "story" in rooms[p]["tracker"]]
    if not story:
        print("story           no story-mode slot: STORY POINTS SKIPPED")
        return False
    names = set(mw.worlds[story[0]].location_name_to_id)
    for p in story:
        slot, world = slots[p], mw.worlds[p]
        reference = TrackerLogic(rooms[p]["tracker"])
        counts = {}
        for item in [i for i in mw.precollected_items[p] if i.code is not None] + \
                [loc.item for loc in checked if loc.item.player == p and loc.item.code is not None]:
            counts[item.code] = counts.get(item.code, 0) + 1
        done = {loc.address for loc in checked if loc.player == p}
        plain = own_in_logic(mw, p, checked)
        seen = []
        for chapter, step in STORY_POSITIONS:
            value = {"chapter": chapter, "step": step}
            key = set_story(a.port, slot, GAME, value)
            view = reference.evaluate(counts, value)
            want = {world.location_id_to_name[i] for i in view.locations - done}
            label = f"story {chapter}/{step}"
            rc, count, lines = run_ut(ap, env, a.port, slot, os.path.join(ut_dir_of(a), "room",
                                                                          f"ut_{slot}_story_{chapter}_{step}.log"))
            listed = {line for line in lines if line in names}
            match = rc == 0 and count == len(want) and listed == want
            ok &= match
            seen.append(count)
            print(f"{label:15s} {slot:8s} UT in logic {count} (listed {len(listed)}), the reference's {len(want)} "
                  f"(later in the story {len(view.later - done)}, the items alone {len(plain)}): "
                  f"{'match' if match else 'MISMATCH'}{'' if rc == 0 else f'; UT exit {rc}'}", flush=True)
            if not match:
                print(f"  only UT: {sorted(listed - want)[:10]}  only the reference: {sorted(want - listed)[:10]}")
            core = cores[p]
            core.multiworld.worlds[1].reconnect_found_entrances(key, value)
            names_in, _ = ut_core_update(core, mw, p, checked)
            lib_ok = names_in == want
            ok &= lib_ok
            print(f"{'':15s} {'':8s} TrackerCore after reconnect_found_entrances: {len(names_in)}: "
                  f"{'match' if lib_ok else 'MISMATCH'}", flush=True)
        grows = seen == sorted(seen) and seen[0] < seen[-1] <= len(plain)
        ok &= grows
        print(f"{'':15s} {slot:8s} in logic by position {seen}: {'follows the story' if grows else 'DOES NOT FOLLOW'}")
        # enforce_deferred_entrances "off": UT ignores the key (back at chapter 3); the world's own again
        set_story(a.port, slot, GAME, {"chapter": 3, "step": 0})
        set_enforce(ap, "off")
        rc, count, lines = run_ut(ap, env, a.port, slot, os.path.join(ut_dir_of(a), "room", f"ut_{slot}_story_off.log"))
        listed = {line for line in lines if line in names}
        match = rc == 0 and count == len(plain) and listed == plain
        ok &= match
        print(f"{'story off':15s} {slot:8s} UT in logic {count} at chapter 3 with enforce off, the world's own "
              f"{len(plain)}: {'match' if match else 'MISMATCH'}", flush=True)
        set_enforce(ap, "default")
        key = set_story(a.port, slot, GAME, None)
        core.multiworld.worlds[1].reconnect_found_entrances(key, None)
    return ok


def ut_dir_of(a):
    return os.path.abspath(a.ut_dir)


def main(argv):
    a = parse(argv)
    if a.run:
        return run(a)
    ap = setup(a)
    env = env_for(os.path.abspath(a.ut_dir), ap)
    cmd = [sys.executable, os.path.abspath(__file__), "--run", "--tracker", os.path.abspath(a.tracker),
           "--ut-dir", os.path.abspath(a.ut_dir), "--seed", str(a.seed), "--port", str(a.port),
           "--spheres", str(a.spheres)]
    cmd += ["--players", os.path.abspath(a.players)] if a.players else []
    cmd += ["--no-story"] if a.no_story else []
    return subprocess.run(cmd, cwd=ap, env=env, stdin=subprocess.DEVNULL, timeout=3600).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
