#!/usr/bin/env python3
"""Universal Tracker's map tab through a real connection, in the order a player gets it
(docs/apworld/apworld.md "Universal Tracker's map tab"; docs/release/release_checklist.md 12.3): UT's own
client with its own Kivy window and map tab (worlds/tracker/TrackerClient.py's TrackerGameContext,
TrackerManager and VisualTracker, nothing replaced) connected to a local MultiServer room, while a
client connected as the slot writes the game's data storage keys the way the port does.

  SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \\
      trackers/tools/ap_ut_connect.py --tracker scratchpad/ap_ut/tracker.apworld \\
      --zip dist/poptracker/so2_shattered_stars_1.0.0.zip [--zip .../so2_shattered_stars_ut.zip] \\
      [--apworld so2.apworld] [--old-apworld dist/apworld-0.2.1/so2.apworld] [--port 38471] [--ut-dir DIR]

No window opens: UT runs with DISPLAY / WAYLAND_DISPLAY / DBUS_SESSION_BUS_ADDRESS unset, SDL's
offscreen video driver (Kivy draws into an offscreen surface) and XDG_* (Kivy's and AP's config,
cache and data) under --ut-dir. UT's file dialog (AP's Utils.open_filename) finds a kdialog stand-in
first on PATH that answers with the zip, or Cancel, and logs how it was asked.

1. A UT copy of the scratch Archipelago (trackers/tools/ap_ut_check.py's setup) with the repo's
   apworld/so2 or --apworld; with --old-apworld a second copy holding that so2.apworld (0.2.1).
2. A story room generated in each (one slot per case; the old one, one slot), hosted by MultiServer
   on 127.0.0.1:--port (and --port + 1 for the old room).
3. The cases, each in its own process, so each is the first connection of a UT session (UT's
   VisualTracker, the map tab's widget, lives as long as the session). --zip's first zip is the
   preset one; the dialog answers with the last.
     launch       UT started with --connect (its main()'s order), host.yaml's ut_pack_path preset,
                  nothing stored yet; then the game connects and moves: a room, Expel's world map,
                  Energy Nede's world map, a map with no page; then /connect again, and a move
     room-key     the GUI's Connect and slot prompt, the game already in a room; /connect again
     world-key    the same, the game already on Expel's world map (the marker at once)
     dialog       ut_pack_path empty: UT's dialog asks once, answered with the zip; UT remembers it
                  in host.yaml
     cancel       ut_pack_path empty, the dialog cancelled: no map tab, UT still tracks
     old-room     a room made with so2.apworld 0.2.1 (--old-apworld): UT 0.2.2 tracks it, map tab
     auto-tab-off informational: /toggle_auto_tab before the first connect (UT 0.3.6's own bug: no
                  page can be laid out at connect then, whatever the world; not counted); then
                  the way back: /toggle_auto_tab again (Auto Tab on lays a page out) and /connect;
                  then Auto Tab off once connected (the checkbox's way) while the game moves
   Each checks UT stays connected with no exception, the map tab, the page UT opened (ctx.map_id,
   the picture from the zip), the marker widgets on VisualTracker, and that UT lists locations in
   logic.
Each case's log (<ut-dir>/case-NAME.log) holds its checks, UT's log records ("log  " lines, with any
traceback) and its stderr. Exit status 0 when every counted check passes.
"""
import argparse
import asyncio
import hashlib
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))  # trackers/tools/ -> the repo
GAME = "Star Ocean: The Second Story"
UT_036_SHA256 = "39e9bc0f733aa7cd96fd57052b1c04233804862ac8543c6f4675af5f2f5cb750"  # Tracker_v0.3.6's asset
NEW_SEED, OLD_SEED = 7101, 7102
STORY = {"chapter": 3, "step": 0}
EXPEL = {"map": 1, "x": 51200, "z": 18500}      # docs/apworld/data_storage.md's example: west of Arlia
NEDE = {"map": 2, "x": 60000, "z": 40000}
ROOM_26 = {"map": 26}                           # Hearn's general store in Arlia
ROOM_62 = {"map": 62}
NO_PAGE = {"map": 7}                            # the opening: no page, UT keeps the one it shows

CASES = [
    dict(name="launch", slot="ConnLaunch", pack="preset", zip=0, launch="argv", stored=None,
         moves=[ROOM_26, EXPEL, NEDE, NO_PAGE], reconnect=True, after=[ROOM_26]),
    dict(name="room-key", slot="ConnRoom", pack="preset", zip=0, launch="gui", stored=ROOM_62, reconnect=True,
         after=[EXPEL]),
    dict(name="world-key", slot="ConnWorld", pack="preset", zip=-1, launch="gui", stored=EXPEL, moves=[ROOM_26]),
    dict(name="dialog", slot="ConnDialog", pack="dialog", zip=-1, launch="gui", stored=ROOM_62, moves=[EXPEL]),
    dict(name="cancel", slot="ConnCancel", pack="cancel", launch="gui", stored=ROOM_62),
    dict(name="old-room", slot="OldRoom", room="old", pack="preset", zip=0, launch="gui", stored=ROOM_26,
         moves=[EXPEL]),
    dict(name="auto-tab-off", slot="ConnAutoTab", pack="preset", zip=0, launch="gui", stored=None, auto_tab_off=True,
         informational=True),
]
DIALOG_STUB = """#!/bin/sh
# kdialog stand-in for UT's file dialog (trackers/tools/ap_ut_connect.py): logs how it was asked,
# answers with $UTCONNECT_DIALOG_ANSWER (empty: Cancel, as kdialog exits 1 with no output).
{ printf '%s\\n' "$@"; echo ---; } >> "$UTCONNECT_DIALOG_LOG"
[ -n "$UTCONNECT_DIALOG_ANSWER" ] || exit 1
printf '%s\\n' "$UTCONNECT_DIALOG_ANSWER"
"""


# ---- outside: the copies, the rooms, the server, the cases ------------------------------------------

def wait_port(port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return True
        time.sleep(0.2)
    return False


def set_pack(host, value):
    """host.yaml's so2_options: ut_pack_path ("" = UT asks with its dialog)."""
    text = open(host, encoding="utf-8").read()
    block = f'so2_options:\n  ut_pack_path: "{value}"\n'
    if re.search(r"(?m)^so2_options:\n", text):
        text = re.sub(r'(?m)^so2_options:\n(?:[ \t]+.*\n)*', block, text)
    else:
        text += block
    with open(host, "w", encoding="utf-8") as f:
        f.write(text)


def get_pack(host):
    m = re.search(r'(?m)^so2_options:\n(?:[ \t]+#.*\n)*[ \t]+ut_pack_path:[ \t]*(.*)$', open(host, encoding="utf-8").read())
    return m.group(1).strip().strip("'\"") if m else None


def case_env(ut_dir, ap):
    sys.path.insert(0, HERE)
    import ap_ut_check
    env = ap_ut_check.env_for(ut_dir, ap)
    env.update(XDG_CONFIG_HOME=os.path.join(ut_dir, "config"),    # kvui's KIVY_HOME (platformdirs' config dir)
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
               UTCONNECT_DIALOG_LOG=os.path.join(ut_dir, "dialog.log"), UTCONNECT_DIALOG_ANSWER="")
    stub = os.path.join(ut_dir, "dialog-bin")
    os.makedirs(stub, exist_ok=True)
    with open(os.path.join(stub, "kdialog"), "w") as f:
        f.write(DIALOG_STUB)
    os.chmod(os.path.join(stub, "kdialog"), 0o755)
    env["PATH"] = stub + os.pathsep + env.get("PATH", "")
    return env


def run_one(plan, ap, env, log_path, timeout):
    """One case's process: its lines shown as they come and kept in its log (stderr too); past the
    timeout it is asked for its stacks (SIGUSR2: the asyncio tasks; SIGUSR1: the threads) and killed."""
    import threading
    lines = []
    with open(log_path, "w") as log:
        proc = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--case", json.dumps(plan)], cwd=ap,
                                env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=log, text=True)
        timed_out = threading.Event()

        def expire():
            timed_out.set()
            for sig in (signal.SIGUSR2, signal.SIGUSR1):    # the asyncio tasks, then the threads
                try:
                    proc.send_signal(sig)
                    proc.wait(timeout=3)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    pass
            if proc.poll() is None:
                proc.kill()
        timer = threading.Timer(timeout, expire)
        timer.start()
        try:
            for line in proc.stdout:
                line = line.rstrip("\n")
                lines.append(line)
                log.write(line + "\n")
                log.flush()
                if line.startswith(("ok ", "FAIL", "info")):
                    print(f"  {line}", flush=True)
            code = proc.wait()
        finally:
            timer.cancel()
    if timed_out.is_set():
        lines.append(f"FAIL the case did not finish in {timeout} s (its stacks: {log_path})")
        print(f"  {lines[-1]}", flush=True)
        code = code or 1
    return lines, code


def outside(a):
    sys.path.insert(0, HERE)
    import ap_ut_check
    ut_dir = os.path.abspath(a.ut_dir)
    zips = [os.path.abspath(z) for z in a.zip]
    for z in zips:
        if "zip/" in os.path.dirname(z) + "/":
            print(f"FAIL {z}: a folder on its path ends in 'zip' (UT splits ap:zip: paths at the first 'zip/')")
            return 1
    digest = hashlib.sha256(open(a.tracker, "rb").read()).hexdigest()
    print(f"info tracker.apworld sha256 {digest[:16]}... "
          f"({'Tracker_v0.3.6' if digest == UT_036_SHA256 else 'NOT the Tracker_v0.3.6 asset'})", flush=True)
    farms = {"new": ap_ut_check.setup(types.SimpleNamespace(ut_dir=ut_dir, ap_dir=a.ap_dir, tracker=a.tracker,
                                                            apworld=a.apworld))}
    if a.old_apworld:
        farms["old"] = ap_ut_check.setup(types.SimpleNamespace(ut_dir=os.path.join(ut_dir, "old"), ap_dir=a.ap_dir,
                                                               tracker=a.tracker, apworld=a.old_apworld))
    rooms, servers, logs = {}, [], []
    try:
        for room, ap in farms.items():
            out = os.path.join(ut_dir, "rooms", room)
            shutil.rmtree(out, ignore_errors=True)
            os.makedirs(out)
            slots = [c["slot"] for c in CASES if c.get("room", "new") == room]
            env = case_env(ut_dir, ap)
            gen = subprocess.run([sys.executable, os.path.abspath(__file__), "--gen", out, "--seed",
                                  str(NEW_SEED if room == "new" else OLD_SEED)] + slots,
                                 cwd=ap, env=env, stdin=subprocess.DEVNULL, timeout=1800, capture_output=True, text=True)
            made = [f for f in os.listdir(out) if f.startswith("AP_") and f.endswith(".zip")]
            if gen.returncode != 0 or not made:
                print(f"FAIL the {room} room did not generate:\n{(gen.stdout + gen.stderr)[-2000:]}")
                return 1
            print(gen.stdout.strip().splitlines()[-1], flush=True)
            port = a.port + (0 if room == "new" else 1)
            if wait_port(port, 0.5):
                print(f"FAIL port {port} is already in use")
                return 1
            log = open(os.path.join(out, "server.log"), "w")
            logs.append(log)
            servers.append(subprocess.Popen(
                [sys.executable, "MultiServer.py", os.path.join(out, made[0]), "--host", "127.0.0.1", "--port", str(port),
                 "--disable_save", "--loglevel", "info"], cwd=ap, env=env, stdin=subprocess.DEVNULL, stdout=log,
                stderr=subprocess.STDOUT, start_new_session=True))
            if not wait_port(port, 90):
                print(f"FAIL the {room} room's server did not listen on {port} ({out}/server.log)")
                return 1
            rooms[room] = port
        failed, infos = [], []
        for case in CASES:
            room = case.get("room", "new")
            if a.only and case["name"] not in a.only.split(","):
                continue
            if room not in rooms:
                print(f"skip {case['name']}: no --old-apworld", flush=True)
                continue
            ap = farms["new"]
            host = os.path.join(ap, "host.yaml")
            env = case_env(ut_dir, ap)
            zip_path = zips[case.get("zip", 0)]
            set_pack(host, zip_path if case["pack"] == "preset" else "")
            env["UTCONNECT_DIALOG_ANSWER"] = zip_path if case["pack"] == "dialog" else ""
            open(env["UTCONNECT_DIALOG_LOG"], "w").close()
            plan = dict(case, port=rooms[room], zip_path=zip_path)
            print(f"--- {case['name']}: slot {case['slot']} ({room} room), "
                  f"{'host.yaml preset' if case['pack'] == 'preset' else 'UT asks: ' + case['pack']}, "
                  f"{os.path.basename(zip_path) if case['pack'] != 'cancel' else 'no zip'}", flush=True)
            lines, code = run_one(plan, ap, env, os.path.join(ut_dir, f"case-{case['name']}.log"), a.case_timeout)
            if not any(line.startswith(("ok ", "FAIL")) for line in lines):
                lines.append("FAIL the case printed no checks (see its log)")
                print(f"  {lines[-1]} {os.path.join(ut_dir, 'case-' + case['name'] + '.log')}", flush=True)
            asked = open(env["UTCONNECT_DIALOG_LOG"]).read().split("---\n")[:-1]
            if case["pack"] == "preset":
                ok = not asked
                what = "UT never asked for the zip (host.yaml's path preset)"
            else:
                ok = len(asked) == 1 and "Select the Shattered Stars maps" in asked[0] and "(*.zip)" in asked[0]
                what = "UT's dialog asked once, with our prompt and a .zip filter"
            line = ("ok   " if ok else "FAIL ") + what + ("" if ok else f": {asked!r}"[:300])
            print(f"  {line}", flush=True)
            lines.append(line)
            if case["pack"] == "dialog":
                saved = get_pack(host)
                line = ("ok   " if saved == zip_path else "FAIL ") + f"UT remembered the chosen zip in host.yaml ({saved})"
                print(f"  {line}", flush=True)
                lines.append(line)
            bad = [ln for ln in lines if ln.startswith("FAIL")] or ([f"FAIL exit status {code}"] if code else [])
            if case.get("informational"):
                infos += [f"{case['name']}: {ln[5:]}" for ln in lines if ln.startswith("info Auto Tab")] or \
                         [f"{case['name']}: no outcome (see its log)"]
            elif bad:
                failed.append(f"{case['name']}: {bad[0][5:]}")
        for i in infos:
            print(f"info {i} (informational, not counted)")
        print(f"{'FAILED' if failed else 'passed'}: {len(failed)} case(s) failed" + "".join(f"\n  {f}" for f in failed))
        return 1 if failed else 0
    finally:
        for server in servers:
            try:
                os.killpg(server.pid, signal.SIGTERM)
                server.wait(timeout=20)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(server.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        for log in logs:
            log.close()


def gen_room(a):
    """Inside a copy: a story room, one slot per name (as Generate.py does, in-process)."""
    sys.path.insert(0, os.getcwd())
    sys.path.insert(0, HERE)
    import logging
    import warnings
    warnings.simplefilter("ignore")
    logging.disable(logging.WARNING)
    import ap_ut_check
    players = os.path.join(a.gen, "players")
    os.makedirs(players, exist_ok=True)
    for name in a.names:
        with open(os.path.join(players, f"{name}.yaml"), "w") as f:
            f.write(f'name: {name}\ngame: "{GAME}"\n"{GAME}":\n  world_mode: story\n')
    mw = ap_ut_check.generate(players, a.gen, a.seed)
    world = mw.worlds[next(p for p in mw.player_ids if mw.game[p] == GAME)]
    print(f"room {os.path.basename(a.gen)}: seed {a.seed}, {len(mw.player_ids)} story slots, so2.apworld "
          f"{getattr(world, 'world_version', '?')} ({world.__module__})", flush=True)
    return 0


# ---- inside one case: one UT session -----------------------------------------------------------------

def run_case(a):
    sys.path.insert(0, os.getcwd())
    import faulthandler
    case = json.loads(a.case)
    # SIGUSR1 (the outside's timeout): every thread's stack even if the event loop is stuck, and
    # the asyncio tasks' stacks when it isn't
    faulthandler.register(signal.SIGUSR1, file=sys.stdout, all_threads=True, chain=False)
    return asyncio.run(drive(case))


def dump_tasks():
    print("info the asyncio tasks:", flush=True)
    for t in asyncio.all_tasks():
        frames = t.get_stack(limit=6)
        where = " < ".join(f"{os.path.basename(f.f_code.co_filename)}:{f.f_lineno} {f.f_code.co_name}"
                           for f in reversed(frames))
        print(f"info   {t.get_name()}: {where}"[:400], flush=True)


class Game:
    """The port's side of data storage (docs/apworld/data_storage.md "When the port writes"): a client
    connected as the slot (items_handling 0b001) that writes each key with one Set: a replace, the
    value also the default, want_reply false."""

    async def open(self, port, name):
        import websockets
        self.ws = await websockets.connect(f"ws://localhost:{port}", max_size=None, ping_interval=None)
        await self.until(lambda m: m.get("cmd") == "RoomInfo")
        await self.ws.send(json.dumps([{"cmd": "Connect", "password": "", "game": GAME, "name": name,
                                        "uuid": "ap-ut-connect-game", "items_handling": 0b001, "tags": [],
                                        "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                        "slot_data": False}]))
        c = await self.until(lambda m: m.get("cmd") in ("Connected", "ConnectionRefused"))
        if c["cmd"] != "Connected":
            raise RuntimeError(f"the game's connection was refused: {c}")
        self.map_key = f"so2_map_{c['team']}_{c['slot']}"
        self.story_key = f"so2_story_{c['team']}_{c['slot']}"
        self.drain = None

    async def until(self, pred, seconds=30):
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = await asyncio.wait_for(self.ws.recv(), max(0.1, deadline - time.time()))
            for m in json.loads(data):
                if pred(m):
                    return m
        raise TimeoutError("no reply from the server")

    async def set(self, key, value):
        await self.ws.send(json.dumps([{"cmd": "Set", "key": key, "default": value, "want_reply": False,
                                        "operations": [{"operation": "replace", "value": value}]}]))

    async def stored(self, map_value):
        """Both keys written (as after the port's connect) and read back, before UT connects."""
        await self.set(self.story_key, STORY)
        await self.set(self.map_key, map_value)
        await self.ws.send(json.dumps([{"cmd": "Get", "keys": [self.map_key, self.story_key]}]))
        await self.until(lambda m: m.get("cmd") == "Retrieved" and m["keys"].get(self.map_key) == map_value)

    def start_drain(self):
        async def drain():
            try:
                async for _ in self.ws:
                    pass
            except Exception:       # noqa: BLE001 (closed at the end)
                pass
        if self.drain is None:
            self.drain = asyncio.create_task(drain())

    async def close(self):
        await self.ws.close()
        if self.drain:
            await self.drain


async def drive(case):
    """One case: its steps, then finish() whatever happened (a step that raises is a FAIL, not a
    hang: asyncio.run waits for Kivy's task, which only ends once UT's window is stopped)."""
    st = types.SimpleNamespace(ctx=None, game=None, results=[])
    try:
        await steps(case, st)
    except Exception as e:      # noqa: BLE001 (the test's own error: reported, then the cleanup)
        import traceback
        print(f"FAIL the case's script raised {type(e).__name__}: {e}", flush=True)
        traceback.print_exc(file=sys.stdout)
        st.results.append(False)
    finally:
        code = await finish(st.ctx, st.game, st.results)
    return code


async def steps(case, st):
    import importlib
    import logging
    results = st.results

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what, flush=True)
        results.append(ok)
        return ok

    class Catch(logging.Handler):
        """Keeps the client's log records (the crash check) and echoes them into the case log
        ("log  " lines: not checks, so the outside doesn't show them; a traceback comes with its record)."""
        def __init__(self):
            super().__init__(logging.INFO)
            self.records = []
            self.setFormatter(logging.Formatter("log  %(levelname)s %(name)s: %(message)s"))

        def emit(self, record):
            self.records.append(record)
            try:
                print(self.format(record), flush=True)
            except Exception:       # noqa: BLE001 (a record that can't be shown is still kept)
                self.handleError(record)
    catch = Catch()
    logging.getLogger().addHandler(catch)
    logging.getLogger().setLevel(logging.INFO)
    asyncio.get_running_loop().add_signal_handler(signal.SIGUSR2, dump_tasks)

    def crashes():
        return [r for r in catch.records if r.exc_info and r.exc_info[0]]

    def crash_text():
        import traceback
        r = crashes()[0]
        frames = [f for f in traceback.extract_tb(r.exc_info[2]) if "tracker" in f.filename.replace("\\", "/")]
        where = "; ".join(f"{os.path.basename(f.filename)}:{f.lineno} {f.name}" for f in frames[-3:])
        return f"{r.exc_info[0].__name__}: {r.exc_info[1].args[0] if r.exc_info[1].args else ''} ({where})"

    async def until(cond, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if cond():
                return True
            await asyncio.sleep(0.05)
        return cond()

    from CommonClient import server_loop
    from worlds.AutoWorld import AutoWorldRegister
    from worlds.tracker.TrackerClient import TrackerGameContext
    world_cls = AutoWorldRegister.world_types[GAME]
    ut_map = importlib.import_module(world_cls.__module__.rsplit(".", 1)[0] + ".ut_map")
    tw = world_cls.tracker_world
    port, slot, zip_path = case["port"], case["slot"], case["zip_path"]
    want_map = case["pack"] != "cancel"

    if case.get("stored") is not None:
        st.game = Game()
        await st.game.open(port, slot)
        await st.game.stored(case["stored"])
        st.game.start_drain()

    # UT, as its main() starts it (TrackerClient.py: the server task, run_generator, run_gui)
    ctx = st.ctx = TrackerGameContext(f"localhost:{port}" if case["launch"] == "argv" else None, None,
                                      print_count=False, print_list=False)
    if case["launch"] == "argv":
        ctx.auth = slot
    ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
    ctx.run_generator()
    ctx.run_gui()
    built = await until(lambda: getattr(ctx.ui, "server_connect_bar", None) is not None
                        and ctx.tracker_page is not None and ctx.map_page_coords_func is not None, 120)
    if not check(built, "UT's window is up (Kivy offscreen)"):
        return

    def type_line(text):        # the console's text field, Enter (kvui's on_message)
        ctx.ui.textinput.text = text
        ctx.ui.textinput.dispatch("on_text_validate")

    if case.get("auto_tab_off"):
        type_line("/toggle_auto_tab")
        check(not ctx.auto_tab, "/toggle_auto_tab before connecting: Auto Tab off")
    if case["launch"] == "gui":     # the address bar's Connect, then the slot name at "Enter slot name:"
        ctx.ui.server_connect_bar.text = f"localhost:{port}"
        ctx.ui.connect_button_action(ctx.ui.server_connect_button)
        if not check(await until(lambda: ctx.input_requests > 0, 60), "UT asks for the slot name"):
            return
        type_line(slot)

    def connected():
        return ctx.server is not None and ctx.server.socket is not None and not ctx.server.socket.closed

    def keys_in():
        if ctx.slot is None or ctx.team is None or ctx.tracker_core.multiworld is None:
            return False
        keys = [f"so2_story_{ctx.team}_{ctx.slot}"] + ([f"so2_map_{ctx.team}_{ctx.slot}"] if ctx.tracker_world else [])
        return all(k in ctx.stored_data for k in keys)

    def page():
        if ctx.map_id is None:
            return None
        return ctx.maps[ctx.map_id]["name"]

    def icons():
        mp = ctx.map_page
        if mp is None or not hasattr(mp, "location_icons"):
            return None
        return [((round(i.pos[0]), round(i.pos[1])), i.source) for i in mp.location_icons]

    def marker_for(value):
        got = ut_map.location_icon_coords(ctx.map_id, value)
        return [] if got is None else [((got[0], got[1]), f"{ctx.root_pack_path}/{got[2]}")]

    def tab_shown():
        return bool(ctx.ui.show_map) and ctx.map_page is not None and ctx.map_page.parent is not None

    def state_checks(label, value, page_name, marker_wanted):
        if not check(connected() and not crashes(), f"{label}: UT is still connected, no exception"
                     + ("" if not crashes() else f": {crash_text()}")):
            return False
        if not want_map:
            check(not tab_shown() and ctx.tracker_world is None, f"{label}: no map tab (no zip chosen)")
            return True
        check(tab_shown(), f"{label}: the Map Page tab is shown")
        img = ctx.maps[ctx.map_id]["img"] if ctx.map_id is not None else None
        check(page() == page_name and ctx.ui.source == f"ap:zip:{zip_path}/{img}",
              f"{label}: UT shows {page_name} ({page()}, {ctx.ui.source[-40:]})")
        want = marker_for(value) if value is not None else []
        if marker_wanted:
            check(bool(want), f"{label}: {value} has a marker on {page_name}")
        got = icons()
        check(got == want, f"{label}: the map tab's marker widgets "
              f"{[(xy, src.rsplit('/', 1)[-1]) for xy, src in got] if got is not None else None} "
              f"(want {[(xy, src.rsplit('/', 1)[-1]) for xy, src in want]}, the image from the zip: "
              f"{all(src.startswith(f'ap:zip:{zip_path}/') for _, src in (got or []))})")
        return True

    await until(lambda: crashes() or keys_in(), 120)
    await asyncio.sleep(0.8)        # anything the keys' Retrieved sets off
    first = ut_map.map_page_index(None)     # the page load_pack lays out before UT has read the key
    default = ctx.maps[first]["name"] if want_map and 0 <= first < len(ctx.maps) else None
    if case.get("informational"):
        print(f"info Auto Tab off at the first connect: "
              f"{'UT dropped the connection: ' + crash_text() if crashes() else 'UT stayed connected'}", flush=True)
        check(True, "informational case ran")
        if crashes():       # the way back: Auto Tab on again (its setter runs load_map(None)), /connect
            seen, old = len(crashes()), ctx.server
            type_line("/toggle_auto_tab")
            type_line("/connect")
            again = await until(lambda: len(crashes()) > seen or (ctx.server is not None and ctx.server is not old
                                                                  and ctx.tracker_world is not None and keys_in()), 120)
            await asyncio.sleep(1.5)    # the Retrieved of the keys
            back = again and len(crashes()) == seen and connected()
            print(f"info Auto Tab back on (/toggle_auto_tab), then /connect: "
                  f"{'UT stayed connected, ' + str(page()) + ' laid out' if back else 'UT did not stay connected'}"
                  + ("" if len(crashes()) == seen else f" ({crashes()[-1].exc_info[0].__name__} again)"), flush=True)
            if back:        # Auto Tab off once a page is laid out, the game moving: a room, Expel's world map
                type_line("/toggle_auto_tab")
                if st.game is None:
                    st.game = Game()
                    await st.game.open(port, slot)
                    await st.game.set(st.game.story_key, STORY)
                    st.game.start_drain()
                seen = len(crashes())
                for value in (ROOM_26, EXPEL):
                    await st.game.set(st.game.map_key, value)
                    await until(lambda: len(crashes()) > seen or ctx.stored_data.get(st.game.map_key) == value, 30)
                    await asyncio.sleep(0.3)
                stayed = len(crashes()) == seen and connected()
                print(f"info Auto Tab off once connected, the game moving to a room and Expel's world map: "
                      f"{'UT stayed connected on ' + str(page()) if stayed else 'UT did not stay connected'}, "
                      f"its marker widgets {[xy for xy, _ in icons() or []]}", flush=True)
        return
    if not check(not crashes() and keys_in(), f"UT connected as {slot} and read the slot's keys"
                 + ("" if not crashes() else f": UT dropped the connection: {crash_text()}")):
        return
    if not state_checks("at connect", case.get("stored"), default, case.get("stored") == EXPEL):
        return
    tracked = ctx.updateTracker()
    check(len(tracked.in_logic_locations) > 0 and not ctx.tracker_core.tracker_disabled,
          f"UT tracks the slot: {len(tracked.in_logic_locations)} locations in logic")
    current = default

    async def move(value):
        nonlocal current
        if st.game is None:
            st.game = Game()
            await st.game.open(port, slot)
            await st.game.set(st.game.story_key, STORY)
            st.game.start_drain()
        await st.game.set(st.game.map_key, value)
        key = st.game.map_key
        seen = await until(lambda: crashes() or ctx.stored_data.get(key) == value, 30)
        await asyncio.sleep(0.2)
        index = tw["map_page_index"](value)
        if want_map and index >= 0:
            current = ctx.maps[index]["name"]
        check(seen, f"UT got the game's {value}")
        return state_checks(f"the game at {value}", value, current, value in (EXPEL, NEDE))

    for value in case.get("moves", []):
        if not await move(value):
            return
    if case.get("reconnect"):
        old = ctx.server
        type_line("/connect")
        again = await until(lambda: crashes() or (ctx.server is not None and ctx.server is not old
                                                  and ctx.tracker_world is not None and keys_in()), 120)
        await asyncio.sleep(1.5)    # the Retrieved of the keys UT already watches
        check(again, "/connect again: UT connected again")
        last = (case.get("moves") or [case.get("stored")])[-1]
        index = tw["map_page_index"](last)
        if index >= 0:              # load_pack lays out the last value's page (AP keeps stored_data)
            current = ctx.maps[index]["name"]
            if not state_checks("after /connect again", last, current, last in (EXPEL, NEDE)):
                return
        else:
            check(connected() and not crashes(), "after /connect again: UT is still connected, no exception"
                  + ("" if not crashes() else f": {crash_text()}"))
        for value in case.get("after", []):
            if not await move(value):
                break


async def finish(ctx, game, results):
    """The game's connection closed, UT's window stopped and UT shut down; each only if it got that far."""
    if game is not None:
        try:
            await asyncio.wait_for(game.close(), 10)
        except Exception:       # noqa: BLE001
            pass
    if ctx is not None:
        try:
            if getattr(ctx, "ui", None) is not None:
                ctx.ui.stop()
            await asyncio.wait_for(ctx.shutdown(), 30)
        except Exception as e:      # noqa: BLE001
            print(f"info UT's shutdown: {e!r}", flush=True)
    return 0 if results and all(results) else 1


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--tracker", default=os.path.join(REPO, "scratchpad", "ap_ut", "tracker.apworld"))
    p.add_argument("--zip", action="append", help="a picture zip: the PopTracker pack's or the port's "
                   "so2_shattered_stars_ut.zip; give two to use both (preset: the first; the dialog: the last)")
    p.add_argument("--apworld", help="a built so2.apworld (default: the repo's apworld/so2)")
    p.add_argument("--old-apworld", help="so2.apworld 0.2.1, for the old-room case (skipped without it)")
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--ut-dir", default=os.path.join(REPO, "scratchpad", "ut_connect"))
    p.add_argument("--port", type=int, default=38471, help="the room's port (the old room: PORT + 1)")
    p.add_argument("--case-timeout", type=int, default=600)
    p.add_argument("--only", help="run only these cases (comma-separated names)")
    p.add_argument("--case", help=argparse.SUPPRESS)
    p.add_argument("--gen", help=argparse.SUPPRESS)
    p.add_argument("--seed", type=int, help=argparse.SUPPRESS)
    p.add_argument("names", nargs="*", help=argparse.SUPPRESS)
    a = p.parse_args()
    if a.case:
        return run_case(a)
    if a.gen:
        return gen_room(a)
    a.zip = a.zip or [os.path.join(REPO, "dist", "poptracker", "so2_shattered_stars_1.0.0.zip")]
    return outside(a)


if __name__ == "__main__":
    sys.exit(main())
